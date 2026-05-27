import { create } from 'zustand';

export type ContextTargetKind = 'background' | 'task' | 'milestone' | 'bracket' | 'dependency' | 'marker';

export interface ContextTarget {
  kind: ContextTargetKind;
  id?: string;
  /** For background context: the date and row clicked */
  date?: string;
  rowId?: string;
}

interface ContextMenuState {
  open: boolean;
  x: number;
  y: number;
  target: ContextTarget | null;
  show: (x: number, y: number, target: ContextTarget) => void;
  close: () => void;
}

export const useContextMenuStore = create<ContextMenuState>((set) => ({
  open: false,
  x: 0,
  y: 0,
  target: null,
  show: (x, y, target) => set({ open: true, x, y, target }),
  close: () => set({ open: false, target: null }),
}));
