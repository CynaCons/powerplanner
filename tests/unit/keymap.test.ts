import { describe, it, expect } from 'vitest';
import { KEYMAP, chordOf, resolve, shadowedPairs, shortcutFor } from '../../src/app/keymap';

/**
 * SRS-EDIT-040 / SRS-EDIT-041 — invariants of the declaration itself.
 *
 * These are REGRESSION tests, not gates: `keymap.ts` did not exist before the
 * fix, so they could never have been observed failing against the old code.
 * The gate is `tests/unit/keymapBehaviour.test.ts`, which talks only to the
 * shipped hook and was watched going red first.
 */

describe('the table is the single declaration (SRS-EDIT-041)', () => {
  it('no two bindings claim the same chord', () => {
    expect(shadowedPairs(KEYMAP)).toEqual([]);
  });

  it('every declared chord is what chordOf would produce for it', () => {
    for (const b of KEYMAP) {
      for (const chord of [b.chord, ...(b.aliases ?? [])]) {
        const parts = chord.split('+');
        const key = parts[parts.length - 1];
        const synthetic = new KeyboardEvent('keydown', {
          key: key === 'plus' ? '+' : key === 'minus' ? '-' : key === 'slash' ? '?' : key,
          ctrlKey: parts.includes('mod'),
          altKey: parts.includes('alt'),
          shiftKey: parts.includes('shift'),
        });
        expect(chordOf(synthetic), `${b.id} declares ${chord}`).toBe(chord);
      }
    }
  });

  it('every binding carries a label and a group', () => {
    for (const b of KEYMAP) {
      expect(b.label.length, b.id).toBeGreaterThan(0);
      expect(b.group.length, b.id).toBeGreaterThan(0);
    }
  });

  it('modifier chords resolve to nothing unless declared', () => {
    const ev = (init: KeyboardEventInit) => new KeyboardEvent('keydown', init);
    expect(resolve(ev({ key: 'n', ctrlKey: true }))).toBeUndefined();
    expect(resolve(ev({ key: 'm', metaKey: true }))).toBeUndefined();
    expect(resolve(ev({ key: 'b', altKey: true }))).toBeUndefined();
    // Browser page zoom is the user's, not the chart's.
    expect(resolve(ev({ key: '=', ctrlKey: true }))).toBeUndefined();
    expect(resolve(ev({ key: 'M', shiftKey: true }))?.id).toBe('scale-month');
    expect(resolve(ev({ key: 'm' }))?.id).toBe('insert-milestone');
  });

  it('the palette names keys the keymap actually binds', () => {
    // The palette taught T for New task and Y for New milestone; both switch
    // tools. Every id it asks for must exist, or this throws.
    expect(shortcutFor('insert-task')).toBe('N');
    expect(shortcutFor('scale-month')).toBe('⇧M');
    expect(() => shortcutFor('no-such-binding')).toThrow();
  });
});
