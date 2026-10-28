import { create } from 'zustand';
import type { Selectable } from './selectionStore';

interface ViewState {
  /**
   * The new-roadmap chooser. It lives here rather than in the File menu's own
   * state because two places open it — the menu and the command palette — and
   * threading a callback through the header for one boolean was worse.
   */
  newRoadmapOpen: boolean;
  setNewRoadmapOpen: (v: boolean) => void;
  /**
   * The details editor, and WHAT it is editing (SRS-EDIT-015).
   *
   * The subject is carried explicitly rather than inferred from the selection.
   * Inferring it meant the Row details button opened Task details for the
   * first task on that row — selecting a row selects everything on it — so the
   * name field a user typed into renamed the wrong object.
   */
  detailsOpen: boolean;
  detailsTarget: Selectable | null;
  openDetails: (target: Selectable) => void;
  closeDetails: () => void;
  /** Kept for the places that only need to close it. */
  setDetailsOpen: (v: boolean) => void;
  showCriticalPath: boolean;
  showBaseline: boolean;
  showMinimap: boolean;
  setShowCriticalPath: (v: boolean) => void;
  setShowBaseline: (v: boolean) => void;
  setShowMinimap: (v: boolean) => void;
  toggle: (key: 'showCriticalPath' | 'showBaseline' | 'showMinimap') => void;
}

export const useViewStore = create<ViewState>((set, get) => ({
  newRoadmapOpen: false,
  setNewRoadmapOpen: (v) => set({ newRoadmapOpen: v }),
  detailsOpen: false,
  detailsTarget: null,
  openDetails: (target) => set({ detailsOpen: true, detailsTarget: target }),
  closeDetails: () => set({ detailsOpen: false, detailsTarget: null }),
  setDetailsOpen: (v) => set(v ? { detailsOpen: true } : { detailsOpen: false, detailsTarget: null }),
  showCriticalPath: false,
  showBaseline: false,
  showMinimap: true,
  setShowCriticalPath: (v) => set({ showCriticalPath: v }),
  setShowBaseline: (v) => set({ showBaseline: v }),
  setShowMinimap: (v) => set({ showMinimap: v }),
  toggle: (key) => set({ [key]: !get()[key] } as Partial<ViewState>),
}));
