import type { Selectable } from '../stores/selectionStore';
import type { GanttDocument, ISODate } from '../types/document';
import { todayISO, addDays } from '../utils/dates';

/**
 * The rows and dates a new bracket covers (SRS-ELEM-032).
 *
 * The button used to pass `doc.rows[0]` and `today … today + 14` whatever was
 * selected, and row zero is a section header in every shipped template — so
 * the bracket landed on a heading a fortnight from today. The keyboard, the
 * palette and the selection group all read the span off the selection; this
 * is that rule, in one place, for all of them to share.
 *
 * With nothing selected it brackets the whole plan rather than refusing: a
 * phase over everything is a legible starting point, and a fortnight from
 * today is only reached when there is nothing at all to measure.
 */
export function bracketSpanFor(
  doc: GanttDocument,
  selection: readonly Selectable[],
): { start: ISODate; end: ISODate; rowIds: string[] } {
  const ids = new Set(selection.map((s) => s.id));
  const pickedRows = selection.filter((s) => s.kind === 'row').map((s) => s.id);
  const onPickedRow = (rowId: string) => pickedRows.includes(rowId);
  const tasks = doc.tasks.filter((t) => ids.has(t.id) || onPickedRow(t.rowId));
  const milestones = doc.milestones.filter((m) => ids.has(m.id) || onPickedRow(m.rowId));
  const chosen = tasks.length + milestones.length > 0;
  const spanTasks = chosen ? tasks : doc.tasks;
  const spanMilestones = chosen ? milestones : doc.milestones;

  const dates: ISODate[] = [
    ...spanTasks.flatMap((t) => [t.start, t.end]),
    ...spanMilestones.map((m) => m.date),
  ];
  const today = todayISO();
  const start = dates.length ? dates.reduce((a, b) => (a < b ? a : b)) : today;
  const end = dates.length ? dates.reduce((a, b) => (a > b ? a : b)) : addDays(today, 14);

  // Rows in the document’s own order, so a bracket’s top and bottom are the
  // top and bottom of what it covers however the selection was built up.
  const covered = new Set(
    pickedRows.length
      ? pickedRows
      : [...spanTasks.map((t) => t.rowId), ...spanMilestones.map((m) => m.rowId)],
  );
  const rowIds = doc.rows.filter((r) => covered.has(r.id)).map((r) => r.id);
  return { start, end, rowIds: rowIds.length ? rowIds : doc.rows.slice(0, 1).map((r) => r.id) };
}
