import { create } from 'zustand';

/**
 * What the app has just done, said once, in one place (SRS-PERS-025).
 *
 * Before this store there was no sink at all: `saveToFile` reported honestly
 * to a caller that used the answer only to gate `markSaved` and then threw it
 * away, six throwing paths reached callers that neither awaited nor caught, so
 * a failed save was an unhandled rejection and nothing else, and the entire
 * user-facing vocabulary was two `window.confirm`s and one `alert` that Chrome
 * never reaches. Saved, saved-somewhere-else and failed were one silence.
 */

export type Tone = 'done' | 'warn' | 'fail';

export interface Notice {
  id: string;
  text: string;
  tone: Tone;
  /** How many times this same text arrived in a row. */
  count: number;
  at: number;
}

interface NoticeState {
  items: Notice[];
  notify: (n: { text: string; tone?: Tone }) => void;
  dismiss: (id: string) => void;
  clear: () => void;
}

/** A 'done' notice is news for a moment; a failure stays until it is read. */
const DONE_MS = 6000;
const MAX_ITEMS = 3;

let seq = 0;

export const useNoticeStore = create<NoticeState>((set, get) => ({
  items: [],
  notify: ({ text, tone = 'done' }) => {
    const items = get().items;
    const newest = items[items.length - 1];
    // The autosave failure path fires every 800ms. Without this, one full
    // localStorage would pin the region and bury every other message.
    if (newest && newest.text === text && newest.tone === tone) {
      set({
        items: [...items.slice(0, -1), { ...newest, count: newest.count + 1, at: Date.now() }],
      });
      return;
    }
    const id = `n${++seq}`;
    set({ items: [...items, { id, text, tone, count: 1, at: Date.now() }].slice(-MAX_ITEMS) });
    if (tone === 'done') {
      setTimeout(() => get().dismiss(id), DONE_MS);
    }
  },
  dismiss: (id) => set({ items: get().items.filter((n) => n.id !== id) }),
  clear: () => set({ items: [] }),
}));

/** The one call every outcome goes through. */
export function notify(text: string, tone: Tone = 'done'): void {
  useNoticeStore.getState().notify({ text, tone });
}
