import { describe, it, expect } from 'vitest';
import { allocateLabels, CHAR_PX, type LabelCandidate } from '../../src/renderer/labelFit';

// Covers the owner report of 2026-10-23: milestone labels collide, and a
// blocked label should step above or below rather than vanish.

const lane = 'row-1#0';
const cand = (id: string, x: number, text: string, requested?: LabelCandidate['requested']): LabelCandidate =>
  ({ id, x, text, lane, requested });

describe('allocateLabels', () => {
  it('puts a lone label to the right', () => {
    const out = allocateLabels([cand('a', 100, 'Design freeze')]);
    expect(out.get('a')?.placement).toBe('right');
  });

  it('steps a colliding label above instead of dropping it', () => {
    // Two diamonds close enough that both labels cannot sit to the right.
    const out = allocateLabels([cand('a', 100, 'Design freeze'), cand('b', 140, 'B-sample')]);
    expect(out.get('a')?.placement).toBe('right');
    expect(out.get('b')?.placement).not.toBe('hidden');
    expect(out.get('b')?.placement).not.toBe('right');
  });

  it('uses all four levels before giving up', () => {
    const items = [0, 1, 2, 3].map((i) => cand(`m${i}`, 100 + i * 8, 'A fairly long milestone label'));
    const out = allocateLabels(items);
    const placements = items.map((i) => out.get(i.id)?.placement);
    expect(new Set(placements).size).toBe(4);
    expect(placements).not.toContain('hidden');
  });

  it('hides only when every level in the lane is taken', () => {
    const items = [0, 1, 2, 3, 4].map((i) => cand(`m${i}`, 100 + i * 4, 'A fairly long milestone label'));
    const out = allocateLabels(items);
    expect(items.map((i) => out.get(i.id)?.placement)).toContain('hidden');
  });

  it('does not move an explicit placement, and routes others around it', () => {
    const out = allocateLabels([
      cand('fixed', 100, 'Pinned above', 'above'),
      cand('auto', 100, 'Pinned above', undefined),
    ]);
    expect(out.get('fixed')?.placement).toBe('above');
    // The automatic one must not land on the occupied level.
    expect(out.get('auto')?.placement).not.toBe('above');
  });

  it('honours an explicit hidden', () => {
    const out = allocateLabels([cand('a', 100, 'Quiet', 'hidden')]);
    expect(out.get('a')?.placement).toBe('hidden');
  });

  it('keeps labels in different lanes independent', () => {
    const out = allocateLabels([
      { id: 'a', x: 100, text: 'Design freeze', lane: 'row-1#0' },
      { id: 'b', x: 100, text: 'Design freeze', lane: 'row-2#0' },
    ]);
    expect(out.get('a')?.placement).toBe('right');
    expect(out.get('b')?.placement).toBe('right');
  });

  it('measures width from the shared glyph metric', () => {
    expect(CHAR_PX).toBeGreaterThan(0);
  });
});
