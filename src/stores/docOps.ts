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
} from '../types/document';

export type DocOp =
  | { op: 'set_title'; title: string }
  | { op: 'set_scale'; scale: TimeScale }
  | { op: 'set_fiscal_year_start'; month: number }
  | { op: 'set_theme'; theme: ThemeName }
  | { op: 'add_row'; row: Row }
  | { op: 'update_row'; id: string; patch: Partial<Row> }
  | { op: 'delete_row'; id: string }
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

    case 'add_row':
      assertUniqueId(doc.rows, op.row.id, 'row');
      return { ...doc, rows: [...doc.rows, op.row] };

    case 'update_row':
      return { ...doc, rows: patched(doc.rows, op.id, op.patch, 'row') };

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
