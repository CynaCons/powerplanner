import type { GanttDocument } from '../types/document';
import { diffDays } from './dates';

/**
 * Document-level counts and spans.
 *
 * Extracted when the inspector's Statistics section was retired (PLAN.md
 * v2.13.0 / C0a): the same numbers were being computed in the inspector and
 * partially again in the status bar. One function, one answer.
 */
export interface DocStats {
  tasks: number;
  milestones: number;
  /** Earliest dated entity, or null when the document has no dates. */
  min: string | null;
  /** Latest dated entity, or null. */
  max: string | null;
  spanDays: number;
  /** Human-scaled span: days under a month, weeks under a year, else years. */
  spanLabel: string;
  /** Mean percent complete across tasks, rounded. */
  avgPercent: number;
}

export function computeDocStats(doc: GanttDocument): DocStats {
  const dates = [
    ...doc.tasks.flatMap((t) => [t.start, t.end]),
    ...doc.milestones.map((m) => m.date),
  ];

  const min = dates.length > 0 ? dates.reduce((a, b) => (a < b ? a : b)) : null;
  const max = dates.length > 0 ? dates.reduce((a, b) => (a > b ? a : b)) : null;
  const spanDays = min && max ? diffDays(min, max) + 1 : 0;

  const spanLabel =
    spanDays === 0
      ? '—'
      : spanDays < 31
        ? `${spanDays}d`
        : spanDays < 365
          ? `${Math.round(spanDays / 7)}w`
          : `${(spanDays / 365).toFixed(1)}y`;

  const avgPercent =
    doc.tasks.length > 0
      ? Math.round(doc.tasks.reduce((a, t) => a + (t.percentComplete ?? 0), 0) / doc.tasks.length)
      : 0;

  return {
    tasks: doc.tasks.length,
    milestones: doc.milestones.length,
    min,
    max,
    spanDays,
    spanLabel,
    avgPercent,
  };
}
