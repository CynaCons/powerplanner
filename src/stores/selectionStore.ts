import { create } from 'zustand';

export type SelectableKind = 'task' | 'milestone' | 'bracket' | 'dependency' | 'marker';

export interface Selectable {
  kind: SelectableKind;
  id: string;
}

interface SelectionState {
  items: Selectable[];
  setSelection: (items: Selectable[]) => void;
  add: (item: Selectable) => void;
  remove: (id: string) => void;
  clear: () => void;
  isSelected: (id: string) => boolean;
}

export const useSelectionStore = create<SelectionState>((set, get) => ({
  items: [],
  setSelection: (items) => set({ items }),
  add: (item) => {
    if (get().isSelected(item.id)) return;
    set({ items: [...get().items, item] });
  },
  remove: (id) => set({ items: get().items.filter((i) => i.id !== id) }),
  clear: () => set({ items: [] }),
  isSelected: (id) => get().items.some((i) => i.id === id),
}));
