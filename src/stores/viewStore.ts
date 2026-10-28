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
   * The teaching surfaces and the palette.
   *
   * These were three separate `useState` flags — two in App, one in Header —
   * so the one place that asks "does a modal own the keyboard?" could not see
   * them. Pressing M while reading the shortcuts sheet inserted a milestone on
   * row one, and Del deleted the selection behind the scrim.
   */
  shortcutsOpen: boolean;
  paletteOpen: boolean;
  settingsOpen: boolean;
  setShortcutsOpen: (v: boolean) => void;
  setPaletteOpen: (v: boolean) => void;
  setSettingsOpen: (v: boolean) => void;
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

/** True while anything owns the keyboard: a dialog, a sheet, the palette. */
export function anyModalOpen(): boolean {
  const v = useViewStore.getState();
  return v.detailsOpen || v.newRoadmapOpen || v.shortcutsOpen || v.paletteOpen || v.settingsOpen;
}

export const useViewStore = create<ViewState>((set, get) => ({
  newRoadmapOpen: false,
  setNewRoadmapOpen: (v) => set({ newRoadmapOpen: v }),
  shortcutsOpen: false,
  paletteOpen: false,
  settingsOpen: false,
  setShortcutsOpen: (v) => set({ shortcutsOpen: v }),
  setPaletteOpen: (v) => set({ paletteOpen: v }),
  setSettingsOpen: (v) => set({ settingsOpen: v }),
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
