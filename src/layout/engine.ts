import type { GanttDocument, Task, Milestone, Bracket, Dependency } from '../types/document';
import { dateToX, xToDate } from './timeAxis';

export const ROW_HEIGHT = 36;
export const BAR_HEIGHT = 22;
export const MILESTONE_SIZE = 14;

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
  rowHeights: number[];
  chartHeight: number;
  rowOffsets: number[];
}

interface LayoutInput {
  doc: GanttDocument;
  viewStart: string;
  pxPerDay: number;
  allowOverlap?: boolean;
}

export function layoutDocument({ doc, viewStart, pxPerDay, allowOverlap = false }: LayoutInput): LayoutResult {
  const rowIndex = new Map<string, number>();
  doc.rows.forEach((r, i) => rowIndex.set(r.id, i));

  // Determine sub-rows per row (auto-stacking when bars overlap)
  const subRowsPerRow: number[] = doc.rows.map(() => 1);
  const taskSubRow = new Map<string, number>();

  if (!allowOverlap) {
    for (const row of doc.rows) {
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
  for (let i = 0; i < doc.rows.length; i++) {
    rowOffsets.push(acc);
    acc += rowHeights[i];
  }
  const chartHeight = acc;

  const tasks: LaidOutTask[] = doc.tasks.map((task) => {
    const rIdx = rowIndex.get(task.rowId) ?? 0;
    const subRow = taskSubRow.get(task.id) ?? 0;
    const x = dateToX(task.start, viewStart, pxPerDay);
    const endX = dateToX(task.end, viewStart, pxPerDay);
    const width = Math.max(2, endX - x + pxPerDay);
    const subRowOffset = subRow * ROW_HEIGHT;
    const y = rowOffsets[rIdx] + subRowOffset + (ROW_HEIGHT - BAR_HEIGHT) / 2;
    return { task, x, y, width, height: BAR_HEIGHT, rowIndex: rIdx, subRow };
  });

  const milestones: LaidOutMilestone[] = doc.milestones.map((m) => {
    const rIdx = rowIndex.get(m.rowId) ?? 0;
    const x = dateToX(m.date, viewStart, pxPerDay) + (rIdx >= 0 ? 0 : 0);
    const y = rowOffsets[rIdx] + ROW_HEIGHT / 2;
    return { milestone: m, x, y, rowIndex: rIdx };
  });

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

  return { tasks, milestones, brackets, dependencies, rowHeights, chartHeight, rowOffsets };
}

export { dateToX, xToDate };
