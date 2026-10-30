/**
 * Document operations — the pure vocabulary for describing a change to a
 * GanttDocument.
 *
 * Introduced for the web agent bridge (PLAN.md Phase 14 / v2.11.x,
 * spec/srs-web/SRS-agent-bridge.md), but deliberately independent of it: this
 * module knows nothing about MCP, sockets, or agents. It is plain document
 * algebra, so it stays in the shipped build and is unit-testable without a
 * browser.
 *
 * Two properties matter and are the reason this exists as a separate layer:
 *
 *  - `applyOps` reduces N operations into ONE resulting document, so a caller
 *    can commit a whole batch as a single history frame (SRS-ABR-013).
 *  - Reduction happens against a draft; a batch containing an invalid operation
 *    changes nothing at all (SRS-ABR-012).
 */

import type {
  GanttDocument,
  Task,
  Milestone,
  Bracket,
  Dependency,
  Marker,
  Row,
  TimeScale,
  ThemeName,
  ISODate,
  GridDelimiter,
  WeekendDisplay,
  DateAnchor,
} from '../types/document';
import { DATE_ANCHORS, GRID_DELIMITERS, WEEKEND_DISPLAYS } from '../types/document';

/**
 * A caution for whoever adds the next operation: it must dirty the document.
 *
 * `shouldAutosave` is `dirty || !offerPending` (src/persistence/autosave.ts), so
 * while a restore offer is unanswered a clean document is deliberately NOT
 * written — newer work wins, and an operation that changed something without
 * marking it dirty would be silently dropped for as long as that banner is up.
 * Every op below goes through `applyBatch` → `mutate`, which marks it, so this
 * is a rule to keep rather than a bug to fix.
 */
export type DocOp =
  | { op: 'set_title'; title: string }
  | { op: 'set_scale'; scale: TimeScale }
  | { op: 'set_fiscal_year_start'; month: number }
  | { op: 'set_theme'; theme: ThemeName }
  // The four below closed a parity hole found 2026-10-29 (SRS-ABR-025): each of
  // these is a setting a person can change from the document-settings context of
  // the command bar, and none of them had an operation, so an agent could only
  // reach them by replacing the whole document — which is not an operation at
  // all, and loses the all-or-nothing and single-undo-frame guarantees.
  | { op: 'set_grid_delimiter'; delimiter: GridDelimiter }
  | { op: 'set_weekend_display'; display: WeekendDisplay }
  | { op: 'set_allow_overlap'; allow: boolean }
  | { op: 'set_style_preset'; preset: string }
  | { op: 'add_row'; row: Row }
  | { op: 'update_row'; id: string; patch: Partial<Row> }
  | { op: 'delete_row'; id: string }
  | { op: 'move_row'; id: string; toIndex: number }
  | { op: 'set_working_days'; days: number[] }
  | { op: 'set_holidays'; dates: ISODate[] }
  | { op: 'add_task'; task: Task }
  | { op: 'update_task'; id: string; patch: Partial<Task> }
  | { op: 'delete_task'; id: string }
  | { op: 'add_milestone'; milestone: Milestone }
  | { op: 'update_milestone'; id: string; patch: Partial<Milestone> }
  | { op: 'delete_milestone'; id: string }
  | { op: 'add_bracket'; bracket: Bracket }
  | { op: 'update_bracket'; id: string; patch: Partial<Bracket> }
  | { op: 'delete_bracket'; id: string }
  | { op: 'add_dependency'; dependency: Dependency }
  | { op: 'update_dependency'; id: string; patch: Partial<Dependency> }
  | { op: 'delete_dependency'; id: string }
  | { op: 'add_marker'; marker: Marker }
  | { op: 'update_marker'; id: string; patch: Partial<Marker> }
  | { op: 'delete_marker'; id: string }
  /**
   * Delete a whole selection, the way one Delete key does (SRS-ABR-027).
   *
   * A batch of single deletes cannot express this. `delete_task` already
   * cascades that task's dependencies, so a selection holding a task AND one of
   * that task's dependencies — the commonest multi-selection there is — makes
   * the following `delete_dependency` throw on a row that is already gone, and
   * SRS-ABR-012's all-or-nothing reduction then discards the entire batch and
   * deletes nothing. `documentStore.transact` exists for exactly this reason on
   * the user's path; this is the operation form of it.
   *
   * Unknown ids are skipped rather than fatal: after a cascade they are the
   * normal case, not a caller error.
   */
  | { op: 'delete_items'; items: Array<{ kind: 'task' | 'milestone' | 'bracket' | 'dependency' | 'marker' | 'row'; id: string }> }
  | { op: 'capture_baseline' }
  | { op: 'clear_baseline' };

export type DocOpKind = DocOp['op'];

/** Thrown when an operation cannot be applied to the given document. */
export class DocOpError extends Error {
  constructor(
    message: string,
    readonly opIndex: number,
    readonly kind: DocOpKind,
  ) {
    super(message);
    this.name = 'DocOpError';
  }
}

function requireIndex<T extends { id: string }>(items: T[], id: string, what: string): number {
  const index = items.findIndex((item) => item.id === id);
  if (index === -1) throw new Error(`${what} '${id}' does not exist`);
  return index;
}

function patched<T extends { id: string }>(items: T[], id: string, patch: Partial<T>, what: string): T[] {
  requireIndex(items, id, what);
  return items.map((item) => (item.id === id ? { ...item, ...patch } : item));
}

function assertUniqueId(items: Array<{ id: string }>, id: string, what: string): void {
  if (items.some((item) => item.id === id)) throw new Error(`${what} '${id}' already exists`);
}

/**
 * Apply one operation to a document, returning a new document.
 *
 * Throws when the operation refers to something that does not exist, or would
 * create a duplicate id. Referential mistakes are rejected here rather than
 * silently producing a document with dangling references — an agent is an
 * untrusted input source in exactly the way a loaded file is (SRS-ABR-011).
 */
/**
 * An anchor the reader does not know is not a no-op: `anchorOffsetDays` falls
 * through to the column's end for anything it does not recognise, so a typo
 * from an agent would move the mark to a place nobody asked for and autosave
 * it there (SRS-ELEM-070).
 */
function requireAnchor(anchor: unknown): void {
  if (anchor === undefined) return;
  if (!DATE_ANCHORS.includes(anchor as DateAnchor)) {
    throw new Error(`anchor must be one of ${DATE_ANCHORS.join(', ')}`);
  }
}

export function applyOp(doc: GanttDocument, op: DocOp): GanttDocument {
  switch (op.op) {
    case 'set_title':
      return { ...doc, title: op.title };

    case 'set_scale':
      return { ...doc, calendar: { ...doc.calendar, scale: op.scale } };

    case 'set_fiscal_year_start':
      if (!Number.isInteger(op.month) || op.month < 1 || op.month > 12) {
        throw new Error(`fiscalYearStart must be an integer 1-12, got ${op.month}`);
      }
      return { ...doc, calendar: { ...doc.calendar, fiscalYearStart: op.month } };

    case 'set_theme':
      return { ...doc, style: { ...doc.style, theme: op.theme } };

    /**
     * The grid and weekend settings are validated against the same lists the
     * reader uses (SRS-TAX-057), not merely typed: an agent is untrusted input,
     * and a calendar carrying `"gridDelimiter": "fortnight"` would pass the
     * compiler, write cleanly to the autosave, and then be silently defaulted
     * on every open — a value that is wrong only after a reload.
     */
    case 'set_grid_delimiter':
      if (!GRID_DELIMITERS.includes(op.delimiter)) {
        throw new Error(`gridDelimiter must be one of ${GRID_DELIMITERS.join(', ')}, got ${String(op.delimiter)}`);
      }
      return { ...doc, calendar: { ...doc.calendar, gridDelimiter: op.delimiter } };

    case 'set_weekend_display':
      if (!WEEKEND_DISPLAYS.includes(op.display)) {
        throw new Error(`weekendDisplay must be one of ${WEEKEND_DISPLAYS.join(', ')}, got ${String(op.display)}`);
      }
      return { ...doc, calendar: { ...doc.calendar, weekendDisplay: op.display } };

    case 'set_allow_overlap':
      if (typeof op.allow !== 'boolean') {
        throw new Error(`allowOverlap must be a boolean, got ${typeof op.allow}`);
      }
      return { ...doc, style: { ...doc.style, allowOverlap: op.allow } };

    case 'set_style_preset':
      if (typeof op.preset !== 'string' || op.preset.length === 0) {
        throw new Error('preset must be a non-empty string');
      }
      return { ...doc, style: { ...doc.style, preset: op.preset } };

    case 'add_row':
      assertUniqueId(doc.rows, op.row.id, 'row');
      return { ...doc, rows: [...doc.rows, op.row] };

    case 'update_row':
      return { ...doc, rows: patched(doc.rows, op.id, op.patch, 'row') };

    /**
     * Row order is array position, and until now nothing could change it
     * (SRS-ABR-017). A section moves with its packages: moving a group header
     * and leaving its children behind would silently dismantle the grouping,
     * which is never what the caller meant.
     */
    case 'move_row': {
      requireIndex(doc.rows, op.id, 'row');
      const moving = doc.rows.filter((r) => r.id === op.id || r.groupId === op.id);
      const movingIds = new Set(moving.map((r) => r.id));
      const rest = doc.rows.filter((r) => !movingIds.has(r.id));
      const at = Math.max(0, Math.min(rest.length, Math.trunc(op.toIndex)));
      return { ...doc, rows: [...rest.slice(0, at), ...moving, ...rest.slice(at)] };
    }

    case 'set_working_days': {
      const days = op.days;
      if (!Array.isArray(days) || days.some((d) => !Number.isInteger(d) || d < 0 || d > 6)) {
        throw new Error('workingDays must be integers 0-6 (0 = Sunday)');
      }
      return {
        ...doc,
        calendar: { ...doc.calendar, workingDays: [...new Set(days)].sort((a, b) => a - b) },
      };
    }

    case 'set_holidays': {
      if (!Array.isArray(op.dates) || op.dates.some((d) => !/^\d{4}-\d{2}-\d{2}$/.test(d))) {
        throw new Error('holidays must be ISO dates (YYYY-MM-DD)');
      }
      return { ...doc, calendar: { ...doc.calendar, holidays: [...op.dates].sort() } };
    }

    case 'delete_row': {
      requireIndex(doc.rows, op.id, 'row');
      // Cascade exactly as user-initiated deletion does (SRS-ABR-015).
      const doomedTasks = new Set(doc.tasks.filter((t) => t.rowId === op.id).map((t) => t.id));
      return {
        ...doc,
        rows: doc.rows.filter((r) => r.id !== op.id),
        tasks: doc.tasks.filter((t) => t.rowId !== op.id),
        milestones: doc.milestones.filter((m) => m.rowId !== op.id),
        dependencies: doc.dependencies.filter((d) => !doomedTasks.has(d.from) && !doomedTasks.has(d.to)),
      };
    }

    case 'add_task':
      assertUniqueId(doc.tasks, op.task.id, 'task');
      requireIndex(doc.rows, op.task.rowId, 'row');
      return { ...doc, tasks: [...doc.tasks, op.task] };

    case 'update_task': {
      if (op.patch.rowId !== undefined) requireIndex(doc.rows, op.patch.rowId, 'row');
      return { ...doc, tasks: patched(doc.tasks, op.id, op.patch, 'task') };
    }

    case 'delete_task':
      requireIndex(doc.tasks, op.id, 'task');
      return {
        ...doc,
        tasks: doc.tasks.filter((t) => t.id !== op.id),
        dependencies: doc.dependencies.filter((d) => d.from !== op.id && d.to !== op.id),
      };

    case 'add_milestone':
      assertUniqueId(doc.milestones, op.milestone.id, 'milestone');
      requireIndex(doc.rows, op.milestone.rowId, 'row');
      return { ...doc, milestones: [...doc.milestones, op.milestone] };

    case 'update_milestone': {
      if (op.patch.rowId !== undefined) requireIndex(doc.rows, op.patch.rowId, 'row');
      requireAnchor(op.patch.anchor);
      return { ...doc, milestones: patched(doc.milestones, op.id, op.patch, 'milestone') };
    }

    case 'delete_milestone':
      requireIndex(doc.milestones, op.id, 'milestone');
      return { ...doc, milestones: doc.milestones.filter((m) => m.id !== op.id) };

    case 'add_bracket':
      assertUniqueId(doc.brackets, op.bracket.id, 'bracket');
      for (const rowId of op.bracket.rowIds) requireIndex(doc.rows, rowId, 'row');
      return { ...doc, brackets: [...doc.brackets, op.bracket] };

    case 'update_bracket': {
      if (op.patch.rowIds) for (const rowId of op.patch.rowIds) requireIndex(doc.rows, rowId, 'row');
      return { ...doc, brackets: patched(doc.brackets, op.id, op.patch, 'bracket') };
    }

    case 'delete_bracket':
      requireIndex(doc.brackets, op.id, 'bracket');
      return { ...doc, brackets: doc.brackets.filter((b) => b.id !== op.id) };

    case 'add_dependency':
      assertUniqueId(doc.dependencies, op.dependency.id, 'dependency');
      requireIndex(doc.tasks, op.dependency.from, 'task');
      requireIndex(doc.tasks, op.dependency.to, 'task');
      if (op.dependency.from === op.dependency.to) {
        throw new Error(`dependency '${op.dependency.id}' links task '${op.dependency.from}' to itself`);
      }
      return { ...doc, dependencies: [...doc.dependencies, op.dependency] };

    case 'update_dependency': {
      if (op.patch.from !== undefined) requireIndex(doc.tasks, op.patch.from, 'task');
      if (op.patch.to !== undefined) requireIndex(doc.tasks, op.patch.to, 'task');
      return { ...doc, dependencies: patched(doc.dependencies, op.id, op.patch, 'dependency') };
    }

    case 'delete_dependency':
      requireIndex(doc.dependencies, op.id, 'dependency');
      return { ...doc, dependencies: doc.dependencies.filter((d) => d.id !== op.id) };

    case 'add_marker':
      assertUniqueId(doc.markers, op.marker.id, 'marker');
      return { ...doc, markers: [...doc.markers, op.marker] };

    case 'update_marker':
      requireAnchor(op.patch.anchor);
      return { ...doc, markers: patched(doc.markers, op.id, op.patch, 'marker') };

    case 'delete_marker':
      requireIndex(doc.markers, op.id, 'marker');
      return { ...doc, markers: doc.markers.filter((m) => m.id !== op.id) };

    case 'capture_baseline':
      return {
        ...doc,
        baseline: {
          capturedAt: new Date().toISOString().slice(0, 10),
          tasks: doc.tasks.map((t) => ({ id: t.id, start: t.start, end: t.end })),
        },
      };

    case 'clear_baseline': {
      const next = { ...doc };
      delete next.baseline;
      return next;
    }

    case 'delete_items': {
      if (!Array.isArray(op.items)) throw new Error('delete_items.items must be an array');
      // Folded one at a time through the same cases a single delete uses, so
      // every cascade rule stays in one place and cannot drift. The skip is
      // what makes the fold possible: `delete_task` removes that task's
      // dependencies, so a dependency named later in the same selection is
      // already gone and its absence is expected, not an error.
      let next = doc;
      for (const item of op.items) {
        if (!item || typeof item.id !== 'string') throw new Error('delete_items.items need a kind and an id');
        const present =
          item.kind === 'task' ? next.tasks
          : item.kind === 'milestone' ? next.milestones
          : item.kind === 'bracket' ? next.brackets
          : item.kind === 'dependency' ? next.dependencies
          : item.kind === 'marker' ? next.markers
          : item.kind === 'row' ? next.rows
          : null;
        if (!present) throw new Error(`delete_items cannot delete a '${String(item.kind)}'`);
        if (!present.some((e) => e.id === item.id)) continue;
        next = applyOp(next, { op: `delete_${item.kind}`, id: item.id } as DocOp);
      }
      return next;
    }

    default: {
      // Exhaustiveness: adding a DocOp variant without a case is a type error.
      const unreachable: never = op;
      throw new Error(`unknown operation: ${JSON.stringify(unreachable)}`);
    }
  }
}

/**
 * Reduce an ordered sequence of operations into a single resulting document.
 *
 * All-or-nothing: reduction runs against a draft, so a failure at operation N
 * leaves the caller's document untouched rather than half-written
 * (SRS-ABR-012). The thrown DocOpError names the offending index so a caller
 * can report *which* operation was bad, not merely that the batch failed.
 */
export function applyOps(doc: GanttDocument, ops: DocOp[]): GanttDocument {
  let draft = doc;
  for (let i = 0; i < ops.length; i++) {
    const op = ops[i];
    try {
      draft = applyOp(draft, op);
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      throw new DocOpError(`operation ${i} (${op.op}) failed: ${message}`, i, op.op);
    }
  }
  return draft;
}
