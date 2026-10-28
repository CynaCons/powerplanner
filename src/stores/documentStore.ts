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
  /**
   * The `revision` the document was last written to disk at. Anything newer
   * means there is work in the window that is not in a file, which is what the
   * New and Open guards ask about (SRS-EDIT-018). It tracks `revision`, not
   * `userRevision`, so a chart an agent built counts as work too.
   */
  savedRevision: number;
  /** The gesture the next write may join, and when it last took one. */
  frameKey: string | null;
  frameAt: number;
  /** True while an agent bridge session is attached (SRS-ABR-060). */
  agentActive: boolean;
  lastAgentEditAt: number | null;

  setDocument: (doc: GanttDocument) => void;
  replaceDocument: (doc: GanttDocument) => void;
  /**
   * Replace the document with a fresh one and treat it as clean: a roadmap the
   * user just started has nothing in it worth warning them about losing
   * (SRS-EDIT-017).
   */
  newDocument: (doc: GanttDocument) => void;
  /** Record that the current state reached a file. */
  markSaved: () => void;

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

  setTitle: (title: string, key?: string) => void;
  setScale: (scale: TimeScale) => void;
  setFiscalYearStart: (month: number) => void;
  setTheme: (theme: GanttDocument['style']['theme']) => void;
  /** Let bars on one row overlap instead of stacking (SRS-LAY-024). */
  setAllowOverlap: (v: boolean) => void;

  addTask: (task: Task) => void;
  updateTask: (id: string, patch: Partial<Task>, key?: string) => void;
  deleteTask: (id: string) => void;

  addMilestone: (m: Milestone) => void;
  updateMilestone: (id: string, patch: Partial<Milestone>, key?: string) => void;
  deleteMilestone: (id: string) => void;

  addBracket: (b: Bracket) => void;
  updateBracket: (id: string, patch: Partial<Bracket>, key?: string) => void;
  deleteBracket: (id: string) => void;

  addDependency: (d: Dependency) => void;
  updateDependency: (id: string, patch: Partial<Dependency>) => void;
  deleteDependency: (id: string) => void;

  addMarker: (m: Marker) => void;
  updateMarker: (id: string, patch: Partial<Marker>) => void;
  deleteMarker: (id: string) => void;

  addRow: (r: Row) => void;
  updateRow: (id: string, patch: Partial<Row>, key?: string) => void;
  deleteRow: (id: string) => void;
  /** Move a row (with its packages, if it is a section) to a new position. */
  moveRow: (id: string, toIndex: number) => void;
  setWorkingDays: (days: number[]) => void;
  setHolidays: (dates: string[]) => void;

  captureBaseline: () => void;
  clearBaseline: () => void;

  /**
   * Close the open gesture frame, so the next edit starts a new undo step.
   * Called from a field's blur, a dialog's close, and from undo/redo.
   */
  endFrame: () => void;
  /**
   * Run `fn` as ONE undo frame, whatever it calls inside (SRS-EDIT-022).
   *
   * NOT `applyBatch`: that is all-or-nothing over a validated op list, and a
   * `delete_task` cascades its dependencies — so a selection holding a task
   * AND a dependency attached to it would abort the whole batch and delete
   * nothing, turning a cosmetic defect into a blocking one. `transact` only
   * groups history; it changes no other semantics.
   */
  transact: (fn: () => void) => void;

  undo: (source?: EditSource) => void;
  redo: (source?: EditSource) => void;
  canUndo: () => boolean;
  canRedo: () => boolean;
}

function pushHistory(state: DocumentState): { past: GanttDocument[]; future: GanttDocument[] } {
  const past = [...state.past, state.doc].slice(-HISTORY_LIMIT);
  return { past, future: [] };
}

/**
 * How long a gesture frame stays open with nothing arriving (SRS-EDIT-022).
 *
 * A BACKSTOP, not the mechanism. The real boundaries are the key changing and
 * `endFrame` — a field blurring, a dialog closing, an undo. This only stops a
 * frame staying open across a pause long enough that the next edit is plainly
 * a second intention.
 */
const COALESCE_IDLE_MS = 1000;

/**
 * The key of the gesture currently running, for callers that cannot pass one
 * per write — `transact` sets it, and every `mutate` underneath joins it.
 */
let ambientKey: string | null = null;

/**
 * Every write goes through here, and every write used to push a history frame.
 *
 * The store had no notion of a gesture, so one keystroke cost one undo step:
 * a 12-character rename spent 12 of a 100-frame history and a 120-character
 * note evicted the session. `key` names the gesture a write belongs to —
 * subject AND field, so leaving a field and coming back starts a new frame.
 * A write with a different key, or none, closes whatever was open, which
 * means the old uncoalesced behaviour is still the default for every caller
 * that does not opt in.
 *
 * The `doc` commit stays per event. Debouncing that would delay the live
 * redraw that typing a name gives for free; it is the FRAME that coalesces.
 */
function mutate(
  set: (fn: (s: DocumentState) => Partial<DocumentState>) => void,
  mutator: (doc: GanttDocument) => GanttDocument,
  source: EditSource = 'user',
  key?: string,
): void {
  const frameKey = key ?? ambientKey;
  set((s) => {
    const now = Date.now();
    const join = frameKey !== null && frameKey === s.frameKey && now - s.frameAt < COALESCE_IDLE_MS;
    // Joining leaves `past` and `future` exactly as they are. `pushHistory`
    // clears `future`, so a coalesced write must not run it — redo would
    // come back from the dead.
    const history = join ? { past: s.past, future: s.future } : pushHistory(s);
    return {
      doc: mutator(s.doc),
      past: history.past,
      future: history.future,
      frameKey,
      frameAt: now,
      // `revision` still bumps per write: the dirty flag and the debounced
      // autosave depend on it, and they are about the document, not history.
      revision: s.revision + 1,
      userRevision: source === 'user' ? s.userRevision + 1 : s.userRevision,
      lastAgentEditAt: source === 'agent' ? Date.now() : s.lastAgentEditAt,
    };
  });
}

let transactSeq = 0;

export const useDocumentStore = create<DocumentState>((set, get) => ({
  doc: sampleDocument(),
  past: [],
  future: [],

  revision: 0,
  userRevision: 0,
  savedRevision: 0,
  frameKey: null,
  frameAt: 0,
  agentActive: false,
  lastAgentEditAt: null,

  // A document that arrives whole — from a file, from the HTML it was embedded
  // in, from a restored snapshot — IS the saved state, so it starts clean.
  // Piecemeal edits go through `mutate`, which leaves `savedRevision` behind.
  setDocument: (doc) =>
    set((s) => ({
      doc,
      past: [],
      future: [],
      revision: s.revision + 1,
      userRevision: s.userRevision + 1,
      savedRevision: s.revision + 1,
      frameKey: null,
    })),
  replaceDocument: (doc) => mutate(set, () => doc),
  newDocument: (doc) => get().setDocument(doc),
  markSaved: () => set((s) => ({ savedRevision: s.revision })),

  applyBatch: (ops, source = 'agent') =>
    // applyOps reduces against a draft and throws before anything is committed,
    // so an invalid batch never reaches `set` — one frame or none.
    mutate(set, (d) => applyOps(d, ops), source),

  setAgentActive: (active) => set({ agentActive: active }),

  setTitle: (title, key) => mutate(set, (d) => ({ ...d, title }), 'user', key),
  setScale: (scale) => mutate(set, (d) => ({ ...d, calendar: { ...d.calendar, scale } })),
  setFiscalYearStart: (month) => mutate(set, (d) => ({ ...d, calendar: { ...d.calendar, fiscalYearStart: month } })),
  setTheme: (theme) => mutate(set, (d) => ({ ...d, style: { ...d.style, theme } })),
  setAllowOverlap: (v) => mutate(set, (d) => ({ ...d, style: { ...d.style, allowOverlap: v } })),

  addTask: (task) => mutate(set, (d) => ({ ...d, tasks: [...d.tasks, task] })),
  updateTask: (id, patch, key) =>
    mutate(set, (d) => ({ ...d, tasks: d.tasks.map((t) => (t.id === id ? { ...t, ...patch } : t)) }), 'user', key),
  deleteTask: (id) =>
    mutate(set, (d) => ({
      ...d,
      tasks: d.tasks.filter((t) => t.id !== id),
      dependencies: d.dependencies.filter((dep) => dep.from !== id && dep.to !== id),
    })),

  addMilestone: (m) => mutate(set, (d) => ({ ...d, milestones: [...d.milestones, m] })),
  updateMilestone: (id, patch, key) =>
    mutate(set, (d) => ({
      ...d,
      milestones: d.milestones.map((m) => (m.id === id ? { ...m, ...patch } : m)),
    }), 'user', key),
  deleteMilestone: (id) => mutate(set, (d) => ({ ...d, milestones: d.milestones.filter((m) => m.id !== id) })),

  addBracket: (b) => mutate(set, (d) => ({ ...d, brackets: [...d.brackets, b] })),
  updateBracket: (id, patch, key) =>
    mutate(set, (d) => ({ ...d, brackets: d.brackets.map((b) => (b.id === id ? { ...b, ...patch } : b)) }), 'user', key),
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
  updateRow: (id, patch, key) =>
    mutate(set, (d) => ({ ...d, rows: d.rows.map((r) => (r.id === id ? { ...r, ...patch } : r)) }), 'user', key),
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

  endFrame: () => set({ frameKey: null }),

  transact: (fn) => {
    const previous = ambientKey;
    ambientKey = `tx${++transactSeq}`;
    try {
      fn();
    } finally {
      ambientKey = previous;
      set({ frameKey: null });
    }
  },

  undo: (source: EditSource = 'user') => {
    const s = get();
    // An undo must never land inside an open frame: the next keystroke would
    // join the frame it just took back.
    set({ frameKey: null });
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
    set({ frameKey: null });
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

/**
 * Whether there is work in the window that is not in a file (SRS-EDIT-018).
 *
 * Agent edits count: an agent that built a chart and a user who built one have
 * both made something there is no copy of.
 */
export function useIsDirty(): boolean {
  return useDocumentStore((s) => s.revision !== s.savedRevision);
}
