import { create } from 'zustand';

interface EditState {
  editingTaskId: string | null;
  snapEnabled: boolean;
  beginEdit: (id: string) => void;
  endEdit: () => void;
  setSnap: (on: boolean) => void;
}

export const useEditStore = create<EditState>((set) => ({
  editingTaskId: null,
  snapEnabled: true,
  beginEdit: (id) => set({ editingTaskId: id }),
  endEdit: () => set({ editingTaskId: null }),
  setSnap: (on) => set({ snapEnabled: on }),
}));
