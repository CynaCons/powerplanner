import { create } from 'zustand';

interface ViewState {
  /**
   * The new-roadmap chooser. It lives here rather than in the File menu's own
   * state because two places open it — the menu and the command palette — and
   * threading a callback through the header for one boolean was worse.
   */
  newRoadmapOpen: boolean;
  setNewRoadmapOpen: (v: boolean) => void;
  /** The details editor for whatever is selected (SRS-EDIT-015). */
  detailsOpen: boolean;
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
  setDetailsOpen: (v) => set({ detailsOpen: v }),
  showCriticalPath: false,
  showBaseline: false,
  showMinimap: true,
  setShowCriticalPath: (v) => set({ showCriticalPath: v }),
  setShowBaseline: (v) => set({ showBaseline: v }),
  setShowMinimap: (v) => set({ showMinimap: v }),
  toggle: (key) => set({ [key]: !get()[key] } as Partial<ViewState>),
}));
