import { create } from 'zustand';

/**
 * Which sections of the side panel are open (SRS-LAY-030).
 *
 * The bar this replaced was a mode switch: the tools, the selection and the
 * document settings took turns in the same strip, so opening one closed
 * another and the gear had to clear the selection to take its place. Sections
 * coexist, so the choice of what to show is the planner's and it persists —
 * a panel that reopens the way you left it is the difference between a tool
 * and a surprise.
 */

export type SectionId = 'tools' | 'selection' | 'insert' | 'view' | 'document';

const OPEN_BY_DEFAULT: Record<SectionId, boolean> = {
  tools: true,
  selection: true,
  insert: true,
  view: true,
  // Theme, fiscal year, delimiters, weekends, working days: set once a
  // quarter, not once a minute.
  document: false,
};

const KEY = 'powerplanner:panel:v1';

function load(): Record<SectionId, boolean> {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return { ...OPEN_BY_DEFAULT };
    const saved = JSON.parse(raw) as Partial<Record<SectionId, boolean>>;
    return { ...OPEN_BY_DEFAULT, ...saved };
  } catch {
    // A browser with storage switched off still gets a working panel.
    return { ...OPEN_BY_DEFAULT };
  }
}

function save(open: Record<SectionId, boolean>): void {
  try {
    localStorage.setItem(KEY, JSON.stringify(open));
  } catch {
    // ignored — the preference is a convenience, never a correctness matter
  }
}

interface PanelState {
  open: Record<SectionId, boolean>;
  toggle: (id: SectionId) => void;
  setOpen: (id: SectionId, open: boolean) => void;
}

export const usePanelStore = create<PanelState>((set, get) => ({
  open: load(),
  toggle: (id) => {
    const next = { ...get().open, [id]: !get().open[id] };
    save(next);
    set({ open: next });
  },
  setOpen: (id, open) => {
    if (get().open[id] === open) return;
    const next = { ...get().open, [id]: open };
    save(next);
    set({ open: next });
  },
}));
