import type { GanttDocument, Task, Milestone, Bracket, Dependency, Row } from '../types/document';
import { addDays, diffDays } from '../utils/dates';
import { dateToX, xToDate } from './timeAxis';

export const ROW_HEIGHT = 36;
export const BAR_HEIGHT = 22;
export const MILESTONE_SIZE = 14;

/**
 * Horizontal clearance a connector keeps at each end before it turns
 * (SRS-ELEM-043). Ten pixels is enough to read as a lead-out at the day scale
 * and still short enough that it never reaches the neighbouring bar.
 */
export const DEP_STUB = 10;
/**
 * How far below a bar's centre the crossing leg runs when predecessor and
 * successor share one row band, where the midpoint between the two centres
 * would lie on the bars themselves.
 */
export const DEP_LANE = BAR_HEIGHT / 2 + 6;

/** Inclusive painted span in days (matches native SpanWidthDays / width formula). */
export function spanWidthDays(start: string, end: string): number {
  return Math.max(2, diffDays(start, end) + 1);
}

export interface SummaryBar {
  rowId: string;
  rowLabel: string;
  start: string;
  end: string;
  x: number;
  y: number;
  width: number;
  rowIndex: number;
}

export interface LaidOutTask {
  task: Task;
  x: number;
  y: number;
  width: number;
  height: number;
  rowIndex: number;
  subRow: number;
}

export interface LaidOutMilestone {
  milestone: Milestone;
  x: number;
  y: number;
  rowIndex: number;
  /** Stacking track within the row (same unit as LaidOutTask.subRow). */
  subRow: number;
}

export interface LaidOutBracket {
  bracket: Bracket;
  x: number;
  width: number;
  topRow: number;
  bottomRow: number;
}

export interface LaidOutDependency {
  dependency: Dependency;
  path: string;
  fromX: number;
  fromY: number;
  toX: number;
  toY: number;
  /**
   * Which way the last segment travels into the successor: +1 rightwards,
   * -1 leftwards (SRS-ELEM-042).
   *
   * Every surface that draws a head needs this. The web renderer leans on
   * `marker orient="auto"`, which reads the direction off the path and so was
   * at the mercy of a leg that ran backwards; the exported figure has no
   * marker and orients its polygon from this sign (SRS-ELEM-044).
   */
  entryDir: 1 | -1;
}

export interface LayoutResult {
  tasks: LaidOutTask[];
  milestones: LaidOutMilestone[];
  brackets: LaidOutBracket[];
  dependencies: LaidOutDependency[];
  summaries: SummaryBar[];
  rowHeights: number[];
  chartHeight: number;
  rowOffsets: number[];
  visibleRows: Row[];
}

interface LayoutInput {
  doc: GanttDocument;
  viewStart: string;
  pxPerDay: number;
  /**
   * Override the document's own setting. Omitted — which is every caller in
   * the app — the document decides (SRS-LAY-024). This parameter existed
   * from the day the lane packer was written and no caller ever passed it, so
   * overlap was reachable only from a test.
   */
  allowOverlap?: boolean;
}

export function layoutDocument({ doc, viewStart, pxPerDay, allowOverlap }: LayoutInput): LayoutResult {
  const overlap = allowOverlap ?? doc.style.allowOverlap ?? false;
  // Determine which rows are visible (collapse children of collapsed groups)
  const collapsedGroups = new Set(doc.rows.filter((r) => r.collapsed).map((r) => r.id));
  const visibleRows = doc.rows.filter((r) => !r.groupId || !collapsedGroups.has(r.groupId));
  const rowIndex = new Map<string, number>();
  visibleRows.forEach((r, i) => rowIndex.set(r.id, i));

  // Determine sub-rows per row (auto-stacking when bars overlap)
  const subRowsPerRow: number[] = visibleRows.map(() => 1);
  const taskSubRow = new Map<string, number>();

  // Milestone sub-rows (Defect D parity with native).
  const milestoneSubRow = new Map<string, number>();

  if (!overlap) {
    for (const row of visibleRows) {
      const tasks = doc.tasks.filter((t) => t.rowId === row.id).sort((a, b) => a.start.localeCompare(b.start));
      // Each track holds the last painted ISO day (not raw end): inclusive-end
      // bars and the 2-day width floor mean a task ending on E paints through E,
      // and a 1-day task paints start..start+1. Free only when lastPainted < next.start.
      const tracks: string[] = [];
      const spans: { start: string; last: string; subRow: number }[] = [];
      for (const t of tasks) {
        const lastPainted = addDays(t.start, spanWidthDays(t.start, t.end) - 1);
        let placed = -1;
        for (let i = 0; i < tracks.length; i++) {
          if (tracks[i] < t.start) {
            tracks[i] = lastPainted;
            placed = i;
            break;
          }
        }
        if (placed === -1) {
          tracks.push(lastPainted);
          placed = tracks.length - 1;
        }
        taskSubRow.set(t.id, placed);
        spans.push({ start: t.start, last: lastPainted, subRow: placed });
      }
      const milestones = doc.milestones
        .filter((m) => m.rowId === row.id)
        .sort((a, b) => a.date.localeCompare(b.date));
      const msDays: { date: string; subRow: number }[] = [];
      let laneCount = tracks.length;
      for (const m of milestones) {
        let placed = -1;
        for (let k = 0; k < laneCount; k++) {
          let free = true;
          for (const sp of spans) {
            if (sp.subRow === k && sp.start <= m.date && m.date <= sp.last) {
              free = false;
              break;
            }
          }
          if (free) {
            for (const md of msDays) {
              if (md.subRow === k && md.date === m.date) {
                free = false;
                break;
              }
            }
          }
          if (free) {
            placed = k;
            break;
          }
        }
        if (placed === -1) {
          placed = laneCount;
          laneCount += 1;
        }
        msDays.push({ date: m.date, subRow: placed });
        milestoneSubRow.set(m.id, placed);
      }
      subRowsPerRow[rowIndex.get(row.id)!] = Math.max(1, laneCount);
    }
  } else {
    for (const t of doc.tasks) taskSubRow.set(t.id, 0);
    for (const m of doc.milestones) milestoneSubRow.set(m.id, 0);
  }

  const rowHeights = subRowsPerRow.map((n) => Math.max(ROW_HEIGHT, n * ROW_HEIGHT));
  const rowOffsets: number[] = [];
  let acc = 0;
  for (let i = 0; i < visibleRows.length; i++) {
    rowOffsets.push(acc);
    acc += rowHeights[i];
  }
  const chartHeight = acc;

  const tasks: LaidOutTask[] = doc.tasks
    .filter((t) => rowIndex.has(t.rowId))
    .map((task) => {
      const rIdx = rowIndex.get(task.rowId)!;
      const subRow = taskSubRow.get(task.id) ?? 0;
      const x = dateToX(task.start, viewStart, pxPerDay);
      const endX = dateToX(task.end, viewStart, pxPerDay);
      const width = Math.max(2, endX - x + pxPerDay);
      const subRowOffset = subRow * ROW_HEIGHT;
      const y = rowOffsets[rIdx] + subRowOffset + (ROW_HEIGHT - BAR_HEIGHT) / 2;
      return { task, x, y, width, height: BAR_HEIGHT, rowIndex: rIdx, subRow };
    });

  const milestones: LaidOutMilestone[] = doc.milestones
    .filter((m) => rowIndex.has(m.rowId))
    .map((m) => {
      const rIdx = rowIndex.get(m.rowId)!;
      const subRow = milestoneSubRow.get(m.id) ?? 0;
      const x = dateToX(m.date, viewStart, pxPerDay);
      const y = rowOffsets[rIdx] + subRow * ROW_HEIGHT + ROW_HEIGHT / 2;
      return { milestone: m, x, y, rowIndex: rIdx, subRow };
    });

  // Compute summary bars for group/summary rows (rows that contain other rows)
  const summaries: SummaryBar[] = [];
  const childRowsByParent = new Map<string, Row[]>();
  for (const r of doc.rows) {
    if (!r.groupId) continue;
    if (!childRowsByParent.has(r.groupId)) childRowsByParent.set(r.groupId, []);
    childRowsByParent.get(r.groupId)!.push(r);
  }
  for (const parent of doc.rows) {
    const children = childRowsByParent.get(parent.id);
    if (!children || children.length === 0) continue;
    if (!rowIndex.has(parent.id)) continue;
    const childIds = children.map((c) => c.id);
    const childTasks = doc.tasks.filter((t) => childIds.includes(t.rowId));
    if (childTasks.length === 0) continue;
    const start = childTasks.reduce((a, t) => (t.start < a ? t.start : a), childTasks[0].start);
    const end = childTasks.reduce((a, t) => (t.end > a ? t.end : a), childTasks[0].end);
    const x = dateToX(start, viewStart, pxPerDay);
    const endX = dateToX(end, viewStart, pxPerDay);
    const width = Math.max(2, endX - x + pxPerDay);
    const parentIdx = rowIndex.get(parent.id)!;
    const y = rowOffsets[parentIdx] + ROW_HEIGHT / 2 - 4;
    summaries.push({ rowId: parent.id, rowLabel: parent.label, start, end, x, y, width, rowIndex: parentIdx });
  }

  const brackets: LaidOutBracket[] = doc.brackets.map((b) => {
    const x = dateToX(b.start, viewStart, pxPerDay);
    const endX = dateToX(b.end, viewStart, pxPerDay);
    const width = Math.max(2, endX - x + pxPerDay);
    const rowIdxs = b.rowIds.map((rid) => rowIndex.get(rid) ?? 0);
    const topRow = Math.min(...rowIdxs, Number.POSITIVE_INFINITY);
    const bottomRow = Math.max(...rowIdxs, 0);
    return { bracket: b, x, width, topRow, bottomRow };
  });

  const taskById = new Map(tasks.map((t) => [t.task.id, t]));
  const dependencies: LaidOutDependency[] = doc.dependencies.flatMap((dep) => {
    const from = taskById.get(dep.from);
    const to = taskById.get(dep.to);
    if (!from || !to) return [];
    // Which cap each end of the link hangs off (SRS-ELEM-041), and therefore
    // which way the connector must travel there (SRS-ELEM-042). A leg leaving
    // a `finish` cap heads right and a leg arriving at a `start` cap arrives
    // heading right; the other two caps are their mirrors. These signs are the
    // whole fix: the turn used to be placed at the midpoint of the two
    // endpoints, which says nothing about which side of the successor it lands
    // on, so when a bar finished on the day the next one started — the painted
    // span runs through the end day, putting the predecessor's cap a day to the
    // right of the successor's — the last leg ran backwards and the arrowhead
    // rotated to match it.
    const leavesFinish = dep.type === 'finish-to-start' || dep.type === 'finish-to-finish';
    const entersStart = dep.type === 'finish-to-start' || dep.type === 'start-to-start';
    const exitDir: 1 | -1 = leavesFinish ? 1 : -1;
    const entryDir: 1 | -1 = entersStart ? 1 : -1;

    const fromX = leavesFinish ? from.x + from.width : from.x;
    const fromY = from.y + from.height / 2;
    const toX = entersStart ? to.x : to.x + to.width;
    const toY = to.y + to.height / 2;

    // The two points the elbow is allowed to turn at: one stub out of the
    // predecessor, one stub short of the successor on the side the arrowhead
    // has to come from.
    const outX = fromX + exitDir * DEP_STUB;
    const inX = toX - entryDir * DEP_STUB;

    // A single riser serves when one x satisfies both ends at once: at or
    // beyond `outX` in the exit direction, at or short of `inX` in the entry
    // direction. Links whose two ends face the same way (FS, SF) only have
    // such an x when the bars are far enough apart; links whose ends face
    // opposite ways (SS, FF) always do, and the riser goes to whichever
    // endpoint is furthest out, which is the shape those links are drawn with
    // everywhere.
    const sameWay = exitDir === entryDir;
    const roomForRiser = sameWay ? (entryDir > 0 ? outX <= inX : inX <= outX) : true;

    let path: string;
    if (roomForRiser) {
      const riserX = sameWay
        ? (outX + inX) / 2
        : entryDir > 0
          ? Math.min(outX, inX)
          : Math.max(outX, inX);
      path = `M ${fromX} ${fromY} L ${riserX} ${fromY} L ${riserX} ${toY} L ${toX} ${toY}`;
    } else {
      // Doubling back: the successor starts before (or where) the predecessor
      // ends, so the connector cannot reach the far side of the successor on
      // one riser without the last leg reversing. It goes out on its stub,
      // crosses in the band between the two rows, and comes back in on the
      // successor's stub — five segments, both stubs intact, and the crossing
      // leg in the gutter between the bars rather than along them. Bars that
      // merely happen to lie on that leg are crossed: the owner declined
      // obstacle routing (SRS-ELEM-043).
      const laneY = Math.abs(toY - fromY) > 1 ? (fromY + toY) / 2 : fromY + DEP_LANE;
      path =
        `M ${fromX} ${fromY} L ${outX} ${fromY} L ${outX} ${laneY} ` +
        `L ${inX} ${laneY} L ${inX} ${toY} L ${toX} ${toY}`;
    }
    return [{ dependency: dep, path, fromX, fromY, toX, toY, entryDir }];
  });

  return { tasks, milestones, brackets, dependencies, summaries, rowHeights, chartHeight, rowOffsets, visibleRows };
}

export { dateToX, xToDate };
