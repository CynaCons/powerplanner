import { useEffect } from 'react';
import { resolve } from './keymap';
import { anyModalOpen } from '../stores/viewStore';

function isEditableTarget(t: EventTarget | null): boolean {
  if (!(t instanceof HTMLElement)) return false;
  const tag = t.tagName;
  return tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT' || t.isContentEditable;
}

/**
 * The whole global keyboard, in one dispatch (SRS-EDIT-040).
 *
 * Everything this hook knows is in `keymap.ts`. Here there are only the three
 * questions that are about the page rather than about a binding: is the user
 * typing into a field, does a dialog or sheet own the keyboard, and did any
 * binding claim this chord. An unclaimed chord runs nothing and — this is the
 * part that matters — preventDefaults nothing, so Ctrl+N is the browser's
 * "new" again and Alt+← is Back again.
 */
export function useKeyboardShortcuts() {
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      const binding = resolve(e);
      if (!binding) return;

      if (!binding.whileTyping && isEditableTarget(e.target)) return;

      // A dialog or a teaching sheet owns the keyboard while it is open; only
      // the chords declared 'always' (save, the palette, the sheet's own
      // toggle) get through. Without this, M inserted a milestone on row one
      // while the user was reading the shortcuts sheet that lists M.
      if (binding.when !== 'always' && anyModalOpen()) return;

      if (binding.preventDefault !== false) e.preventDefault();
      binding.run();
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, []);
}
