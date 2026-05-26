import type { GanttDocument, Task, Milestone, Bracket, Dependency, Row } from '../types/document';
import { dateToX, xToDate } from './timeAxis';

export const ROW_HEIGHT = 36;
export const BAR_HEIGHT = 22;
export const MILESTONE_SIZE = 14;

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
  allowOverlap?: boolean;
}

export function layoutDocument({ doc, viewStart, pxPerDay, allowOverlap = false }: LayoutInput): LayoutResult {
  // Determine which rows are visible (collapse children of collapsed groups)
  const collapsedGroups = new Set(doc.rows.filter((r) => r.collapsed).map((r) => r.id));
  const visibleRows = doc.rows.filter((r) => !r.groupId || !collapsedGroups.has(r.groupId));
  const rowIndex = new Map<string, number>();
  visibleRows.forEach((r, i) => rowIndex.set(r.id, i));

  // Determine sub-rows per row (auto-stacking when bars overlap)
  const subRowsPerRow: number[] = visibleRows.map(() => 1);
  const taskSubRow = new Map<string, number>();

  if (!allowOverlap) {
    for (const row of visibleRows) {
      const tasks = doc.tasks.filter((t) => t.rowId === row.id).sort((a, b) => a.start.localeCompare(b.start));
      const tracks: string[] = []; // each track = ISO end date currently occupying
      for (const t of tasks) {
        let placed = -1;
        for (let i = 0; i < tracks.length; i++) {
          if (tracks[i] <= t.start) {
            tracks[i] = t.end;
            placed = i;
            break;
          }
        }
        if (placed === -1) {
          tracks.push(t.end);
          placed = tracks.length - 1;
        }
        taskSubRow.set(t.id, placed);
      }
      subRowsPerRow[rowIndex.get(row.id)!] = Math.max(1, tracks.length);
    }
  } else {
    for (const t of doc.tasks) taskSubRow.set(t.id, 0);
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
      const x = dateToX(m.date, viewStart, pxPerDay);
      const y = rowOffsets[rIdx] + ROW_HEIGHT / 2;
      return { milestone: m, x, y, rowIndex: rIdx };
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
    const fromX = dep.type === 'finish-to-start' || dep.type === 'finish-to-finish' ? from.x + from.width : from.x;
    const fromY = from.y + from.height / 2;
    const toX = dep.type === 'finish-to-start' || dep.type === 'start-to-start' ? to.x : to.x + to.width;
    const toY = to.y + to.height / 2;
    const midX = (fromX + toX) / 2;
    const path = `M ${fromX} ${fromY} L ${midX} ${fromY} L ${midX} ${toY} L ${toX} ${toY}`;
    return [{ dependency: dep, path, fromX, fromY, toX, toY }];
  });

  return { tasks, milestones, brackets, dependencies, summaries, rowHeights, chartHeight, rowOffsets, visibleRows };
}

export { dateToX, xToDate };
