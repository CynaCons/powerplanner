import { create } from 'zustand';
import type { GanttDocument, Task, Milestone, Bracket, Dependency, Marker, Row, TimeScale } from '../types/document';
import { sampleDocument } from '../utils/sample';
import { applyOps, type DocOp } from './docOps';

const HISTORY_LIMIT = 100;

/**
 * Who caused a mutation. Only used to keep `userRevision` honest so an agent can
 * tell that the user edited underneath it (SRS-ABR-020); it grants no different
 * privileges — an agent edit and a user edit are the same edit.
 */
export type EditSource = 'user' | 'agent';

interface DocumentState {
  doc: GanttDocument;
  past: GanttDocument[];
  future: GanttDocument[];

  /** Bumped by every mutation, whatever its source. */
  revision: number;
  /** Bumped only by user-sourced mutations. */
  userRevision: number;
  /** True while an agent bridge session is attached (SRS-ABR-060). */
  agentActive: boolean;
  lastAgentEditAt: number | null;

  setDocument: (doc: GanttDocument) => void;
  replaceDocument: (doc: GanttDocument) => void;

  /**
   * Apply an ordered batch of operations as ONE undo frame and ONE store
   * commit (SRS-ABR-013). All-or-nothing: if any operation is invalid the
   * document is left untouched and the DocOpError propagates (SRS-ABR-012).
   *
   * This is the single path through which agent edits reach the document, so
   * every agent mutation gets identical validation and history semantics.
   */
  applyBatch: (ops: DocOp[], source?: EditSource) => void;
  setAgentActive: (active: boolean) => void;

  setTitle: (title: string) => void;
  setScale: (scale: TimeScale) => void;
  setFiscalYearStart: (month: number) => void;
  setTheme: (theme: GanttDocument['style']['theme']) => void;

  addTask: (task: Task) => void;
  updateTask: (id: string, patch: Partial<Task>) => void;
  deleteTask: (id: string) => void;

  addMilestone: (m: Milestone) => void;
  updateMilestone: (id: string, patch: Partial<Milestone>) => void;
  deleteMilestone: (id: string) => void;

  addBracket: (b: Bracket) => void;
  updateBracket: (id: string, patch: Partial<Bracket>) => void;
  deleteBracket: (id: string) => void;

  addDependency: (d: Dependency) => void;
  updateDependency: (id: string, patch: Partial<Dependency>) => void;
  deleteDependency: (id: string) => void;

  addMarker: (m: Marker) => void;
  updateMarker: (id: string, patch: Partial<Marker>) => void;
  deleteMarker: (id: string) => void;

  addRow: (r: Row) => void;
  updateRow: (id: string, patch: Partial<Row>) => void;
  deleteRow: (id: string) => void;
  /** Move a row (with its packages, if it is a section) to a new position. */
  moveRow: (id: string, toIndex: number) => void;
  setWorkingDays: (days: number[]) => void;
  setHolidays: (dates: string[]) => void;

  captureBaseline: () => void;
  clearBaseline: () => void;

  undo: (source?: EditSource) => void;
  redo: (source?: EditSource) => void;
  canUndo: () => boolean;
  canRedo: () => boolean;
}

function pushHistory(state: DocumentState): { past: GanttDocument[]; future: GanttDocument[] } {
  const past = [...state.past, state.doc].slice(-HISTORY_LIMIT);
  return { past, future: [] };
}

function mutate(
  set: (fn: (s: DocumentState) => Partial<DocumentState>) => void,
  mutator: (doc: GanttDocument) => GanttDocument,
  source: EditSource = 'user',
): void {
  set((s) => {
    const history = pushHistory(s);
    return {
      doc: mutator(s.doc),
      past: history.past,
      future: history.future,
      revision: s.revision + 1,
      userRevision: source === 'user' ? s.userRevision + 1 : s.userRevision,
      lastAgentEditAt: source === 'agent' ? Date.now() : s.lastAgentEditAt,
    };
  });
}

export const useDocumentStore = create<DocumentState>((set, get) => ({
  doc: sampleDocument(),
  past: [],
  future: [],

  revision: 0,
  userRevision: 0,
  agentActive: false,
  lastAgentEditAt: null,

  setDocument: (doc) => set((s) => ({ doc, past: [], future: [], revision: s.revision + 1, userRevision: s.userRevision + 1 })),
  replaceDocument: (doc) => mutate(set, () => doc),

  applyBatch: (ops, source = 'agent') =>
    // applyOps reduces against a draft and throws before anything is committed,
    // so an invalid batch never reaches `set` — one frame or none.
    mutate(set, (d) => applyOps(d, ops), source),

  setAgentActive: (active) => set({ agentActive: active }),

  setTitle: (title) => mutate(set, (d) => ({ ...d, title })),
  setScale: (scale) => mutate(set, (d) => ({ ...d, calendar: { ...d.calendar, scale } })),
  setFiscalYearStart: (month) => mutate(set, (d) => ({ ...d, calendar: { ...d.calendar, fiscalYearStart: month } })),
  setTheme: (theme) => mutate(set, (d) => ({ ...d, style: { ...d.style, theme } })),

  addTask: (task) => mutate(set, (d) => ({ ...d, tasks: [...d.tasks, task] })),
  updateTask: (id, patch) =>
    mutate(set, (d) => ({ ...d, tasks: d.tasks.map((t) => (t.id === id ? { ...t, ...patch } : t)) })),
  deleteTask: (id) =>
    mutate(set, (d) => ({
      ...d,
      tasks: d.tasks.filter((t) => t.id !== id),
      dependencies: d.dependencies.filter((dep) => dep.from !== id && dep.to !== id),
    })),

  addMilestone: (m) => mutate(set, (d) => ({ ...d, milestones: [...d.milestones, m] })),
  updateMilestone: (id, patch) =>
    mutate(set, (d) => ({
      ...d,
      milestones: d.milestones.map((m) => (m.id === id ? { ...m, ...patch } : m)),
    })),
  deleteMilestone: (id) => mutate(set, (d) => ({ ...d, milestones: d.milestones.filter((m) => m.id !== id) })),

  addBracket: (b) => mutate(set, (d) => ({ ...d, brackets: [...d.brackets, b] })),
  updateBracket: (id, patch) =>
    mutate(set, (d) => ({ ...d, brackets: d.brackets.map((b) => (b.id === id ? { ...b, ...patch } : b)) })),
  deleteBracket: (id) => mutate(set, (d) => ({ ...d, brackets: d.brackets.filter((b) => b.id !== id) })),

  addDependency: (dep) => mutate(set, (d) => ({ ...d, dependencies: [...d.dependencies, dep] })),
  updateDependency: (id, patch) =>
    mutate(set, (d) => ({
      ...d,
      dependencies: d.dependencies.map((dep) => (dep.id === id ? { ...dep, ...patch } : dep)),
    })),
  deleteDependency: (id) => mutate(set, (d) => ({ ...d, dependencies: d.dependencies.filter((dep) => dep.id !== id) })),

  addMarker: (m) => mutate(set, (d) => ({ ...d, markers: [...d.markers, m] })),
  updateMarker: (id, patch) =>
    mutate(set, (d) => ({ ...d, markers: d.markers.map((m) => (m.id === id ? { ...m, ...patch } : m)) })),
  deleteMarker: (id) => mutate(set, (d) => ({ ...d, markers: d.markers.filter((m) => m.id !== id) })),

  addRow: (r) => mutate(set, (d) => ({ ...d, rows: [...d.rows, r] })),
  updateRow: (id, patch) =>
    mutate(set, (d) => ({ ...d, rows: d.rows.map((r) => (r.id === id ? { ...r, ...patch } : r)) })),
  deleteRow: (id) =>
    mutate(set, (d) => ({
      ...d,
      rows: d.rows.filter((r) => r.id !== id),
      tasks: d.tasks.filter((t) => t.rowId !== id),
      milestones: d.milestones.filter((m) => m.rowId !== id),
    })),

  // Order, sections and the calendar go through the same op reducer the bridge
  // uses, so a user action and an agent action cannot diverge (SRS-ABR-019).
  moveRow: (id, toIndex) => mutate(set, (d) => applyOps(d, [{ op: 'move_row', id, toIndex }])),
  setWorkingDays: (days) => mutate(set, (d) => applyOps(d, [{ op: 'set_working_days', days }])),
  setHolidays: (dates) => mutate(set, (d) => applyOps(d, [{ op: 'set_holidays', dates }])),

  captureBaseline: () => mutate(set, (d) => ({
    ...d,
    baseline: {
      capturedAt: new Date().toISOString().slice(0, 10),
      tasks: d.tasks.map((t) => ({ id: t.id, start: t.start, end: t.end })),
    },
  })),
  clearBaseline: () => mutate(set, (d) => {
    const next = { ...d };
    delete next.baseline;
    return next;
  }),

  undo: (source: EditSource = 'user') => {
    const s = get();
    if (s.past.length === 0) return;
    const previous = s.past[s.past.length - 1];
    set({
      doc: previous,
      past: s.past.slice(0, -1),
      future: [s.doc, ...s.future],
      revision: s.revision + 1,
      userRevision: source === 'user' ? s.userRevision + 1 : s.userRevision,
    });
  },
  redo: (source: EditSource = 'user') => {
    const s = get();
    if (s.future.length === 0) return;
    const next = s.future[0];
    set({
      doc: next,
      past: [...s.past, s.doc],
      future: s.future.slice(1),
      revision: s.revision + 1,
      userRevision: source === 'user' ? s.userRevision + 1 : s.userRevision,
    });
  },
  canUndo: () => get().past.length > 0,
  canRedo: () => get().future.length > 0,
}));
