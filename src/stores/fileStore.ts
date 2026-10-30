import { create } from 'zustand';

/**
 * Which file the plan is bound to, and how that file is doing.
 *
 * The binding was module state in `fileIo` and the footer read it at render
 * time, which worked only because every event that changed it also changed
 * the document. Restoring a remembered handle on startup does not: it lands
 * an indeterminate time after mount, with the document untouched. So the
 * binding has to be something a component can subscribe to.
 *
 * `state` is the second half of the answer. "Which file" without "did the
 * bytes get there" is the half that reads reassuringly while a write is being
 * refused — a handle recalled from a previous session always starts without
 * permission, and the planner has to be told that, not left looking at a
 * filename that implies it is safe.
 */

export type SaveState =
  | 'none'
  | 'saving'
  | 'saved'
  /** The file is known, but this browser has not been allowed to write it. */
  | 'needs-permission'
  | 'error';

interface FileState {
  name: string | null;
  state: SaveState;
  /** Why, when the state alone does not say it. */
  detail: string | null;
  bind: (name: string | null) => void;
  setState: (state: SaveState, detail?: string | null) => void;
}

export const useFileStore = create<FileState>((set) => ({
  name: null,
  state: 'none',
  detail: null,
  bind: (name) => set({ name, state: name ? 'saved' : 'none', detail: null }),
  setState: (state, detail = null) => set({ state, detail }),
}));

/** Non-reactive setters, for the persistence layer, which is not a component. */
export function bindFile(name: string | null): void {
  useFileStore.getState().bind(name);
}

export function setSaveState(state: SaveState, detail: string | null = null): void {
  useFileStore.getState().setState(state, detail);
}

/** What the footer says about the file's state, next to its name. */
export function saveStateLabel(state: SaveState, detail: string | null): string | null {
  switch (state) {
    case 'saving':
      return 'saving…';
    case 'needs-permission':
      return 'not allowed to write — press Ctrl+S';
    case 'error':
      return detail ? `not saved: ${detail}` : 'not saved';
    default:
      return null;
  }
}
