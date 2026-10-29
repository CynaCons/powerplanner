import { useCallback } from 'react';
import { create } from 'zustand';

export type SelectableKind = 'task' | 'milestone' | 'bracket' | 'dependency' | 'marker' | 'row';

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

/**
 * Ask whether something is selected, from a component that must repaint when
 * the answer changes (SRS-EDIT-023).
 *
 * `useSelectionStore((s) => s.isSelected)` reads correctly and subscribes to
 * nothing: the selector returns the function `create()` built once, its
 * reference never changes, and Zustand's equality check therefore passes on
 * every update. Five chart renderers did exactly that, so the selection chrome
 * was computed from live state inside a component React had no reason to
 * re-render — it painted only when something unrelated forced a pass. The
 * status bar and the command bar read `items` and updated, which is why the
 * footer answered a click the chart ignored.
 *
 * Subscribing to `items` is the whole fix. The returned predicate is memoised
 * on that array so it stays stable between selection changes.
 */
export function useIsSelected(): (id: string) => boolean {
  const items = useSelectionStore((s) => s.items);
  return useCallback((id: string) => items.some((i) => i.id === id), [items]);
}
