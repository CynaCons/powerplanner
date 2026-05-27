import { create } from 'zustand';

interface ViewState {
  showCriticalPath: boolean;
  showBaseline: boolean;
  showMinimap: boolean;
  setShowCriticalPath: (v: boolean) => void;
  setShowBaseline: (v: boolean) => void;
  setShowMinimap: (v: boolean) => void;
  toggle: (key: 'showCriticalPath' | 'showBaseline' | 'showMinimap') => void;
}

export const useViewStore = create<ViewState>((set, get) => ({
  showCriticalPath: false,
  showBaseline: false,
  showMinimap: true,
  setShowCriticalPath: (v) => set({ showCriticalPath: v }),
  setShowBaseline: (v) => set({ showBaseline: v }),
  setShowMinimap: (v) => set({ showMinimap: v }),
  toggle: (key) => set({ [key]: !get()[key] } as Partial<ViewState>),
}));
