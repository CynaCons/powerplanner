import { create } from 'zustand';
import type { GanttDocument, Task, Milestone, Bracket, Dependency, Marker, Row, TimeScale } from '../types/document';
import { sampleDocument } from '../utils/sample';

const HISTORY_LIMIT = 100;

interface DocumentState {
  doc: GanttDocument;
  past: GanttDocument[];
  future: GanttDocument[];

  setDocument: (doc: GanttDocument) => void;
  replaceDocument: (doc: GanttDocument) => void;

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

  undo: () => void;
  redo: () => void;
  canUndo: () => boolean;
  canRedo: () => boolean;
}

function pushHistory(state: DocumentState): { past: GanttDocument[]; future: GanttDocument[] } {
  const past = [...state.past, state.doc].slice(-HISTORY_LIMIT);
  return { past, future: [] };
}

function mutate(set: (fn: (s: DocumentState) => Partial<DocumentState>) => void, mutator: (doc: GanttDocument) => GanttDocument): void {
  set((s) => {
    const history = pushHistory(s);
    return { doc: mutator(s.doc), past: history.past, future: history.future };
  });
}

export const useDocumentStore = create<DocumentState>((set, get) => ({
  doc: sampleDocument(),
  past: [],
  future: [],

  setDocument: (doc) => set({ doc, past: [], future: [] }),
  replaceDocument: (doc) => mutate(set, () => doc),

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

  undo: () => {
    const s = get();
    if (s.past.length === 0) return;
    const previous = s.past[s.past.length - 1];
    set({ doc: previous, past: s.past.slice(0, -1), future: [s.doc, ...s.future] });
  },
  redo: () => {
    const s = get();
    if (s.future.length === 0) return;
    const next = s.future[0];
    set({ doc: next, past: [...s.past, s.doc], future: s.future.slice(1) });
  },
  canUndo: () => get().past.length > 0,
  canRedo: () => get().future.length > 0,
}));
