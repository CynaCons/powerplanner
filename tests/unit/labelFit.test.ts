import { describe, it, expect } from 'vitest';
import { fitLabel, computeSpaceRight, textWidth, truncateToWidth } from '../../src/renderer/labelFit';

/**
 * Gates for the defect the user reported on 2026-08-07: a 32-task roadmap
 * rendered with every label spilling right onto its neighbour's label.
 *
 * The old renderer fell back from `on-bar` to `right` whenever text was wider
 * than the bar, with no check that the space to the right was free. The
 * decisive assertions below are the ones that constrain that fallback.
 */

describe('fitLabel — placement under space pressure', () => {
  it('keeps a label on the bar when it fits inside', () => {
    const r = fitLabel({ text: 'Rev B', requested: 'on-bar', barWidth: 200, spaceRight: 0 });
    expect(r.placement).toBe('on-bar');
    expect(r.text).toBe('Rev B');
    expect(r.truncated).toBe(false);
  });

  it('spills right only when the space to the right is actually free', () => {
    const text = 'Rev A bring-up & debug';
    const wide = fitLabel({ text, requested: 'on-bar', barWidth: 20, spaceRight: 400 });
    expect(wide.placement).toBe('right');
    expect(wide.text).toBe(text);
  });

  it('does NOT spill right onto a neighbouring bar — the reported defect', () => {
    // Sliver bar, neighbour 8px away. The old renderer placed the full label
    // here regardless, producing the overlap the user saw.
    const r = fitLabel({
      text: 'Rev A bring-up & debug',
      requested: 'on-bar',
      barWidth: 6,
      spaceRight: 8,
    });
    expect(r.placement).not.toBe('right');
    expect(r.placement).toBe('hidden');
  });

  it('truncates rather than hiding when there is partial room', () => {
    const r = fitLabel({
      text: 'Buffering & flow control at full bus load',
      requested: 'on-bar',
      barWidth: 120,
      spaceRight: 0,
    });
    expect(r.placement).toBe('on-bar');
    expect(r.truncated).toBe(true);
    expect(r.text.endsWith('…')).toBe(true);
    expect(textWidth(r.text)).toBeLessThanOrEqual(120 - 16);
  });

  it('hides when nothing legible fits anywhere', () => {
    const r = fitLabel({ text: 'Timestamp accuracy', requested: 'on-bar', barWidth: 4, spaceRight: 3 });
    expect(r.placement).toBe('hidden');
    expect(r.text).toBe('');
  });

  it('treats the last bar in a lane as having open space', () => {
    const r = fitLabel({
      text: 'Isolation & safety certification',
      requested: 'on-bar',
      barWidth: 10,
      spaceRight: Infinity,
    });
    expect(r.placement).toBe('right');
    expect(r.truncated).toBe(false);
  });

  it('honours an explicit placement rather than second-guessing the user', () => {
    // A deliberate choice in the document is not a guess to be overridden.
    for (const requested of ['left', 'right', 'above', 'below'] as const) {
      const r = fitLabel({ text: 'A very long deliberate label', requested, barWidth: 4, spaceRight: 0 });
      expect(r.placement).toBe(requested);
      expect(r.text).toBe('A very long deliberate label');
    }
  });

  it('respects hidden and empty text', () => {
    expect(fitLabel({ text: 'x', requested: 'hidden', barWidth: 999, spaceRight: 999 }).placement).toBe('hidden');
    expect(fitLabel({ text: '', requested: 'on-bar', barWidth: 999, spaceRight: 999 }).placement).toBe('hidden');
  });
});

describe('truncateToWidth', () => {
  it('returns the original when it already fits', () => {
    expect(truncateToWidth('short', 500)).toBe('short');
  });

  it('returns null below the legibility floor', () => {
    expect(truncateToWidth('anything', 10)).toBeNull();
  });

  it('always produces something narrower than the budget', () => {
    const out = truncateToWidth('Production tooling & ICT fixtures', 60);
    expect(out).not.toBeNull();
    expect(textWidth(out as string)).toBeLessThanOrEqual(60);
  });
});

describe('computeSpaceRight — per-lane neighbours', () => {
  it('measures the gap to the next item in the same lane', () => {
    const a = { x: 0, right: 50, lane: 'r1#0' };
    const b = { x: 80, right: 120, lane: 'r1#0' };
    const spaces = computeSpaceRight([a, b]);
    expect(spaces.get(a)).toBe(30);
    expect(spaces.get(b)).toBe(Infinity);
  });

  it('does not let a different lane constrain a label', () => {
    // Two bars at the same x in different sub-rows are not neighbours on screen.
    const a = { x: 0, right: 50, lane: 'r1#0' };
    const other = { x: 55, right: 90, lane: 'r1#1' };
    const spaces = computeSpaceRight([a, other]);
    expect(spaces.get(a)).toBe(Infinity);
  });

  it('sorts by x rather than trusting input order', () => {
    const later = { x: 200, right: 240, lane: 'r1#0' };
    const earlier = { x: 0, right: 50, lane: 'r1#0' };
    const spaces = computeSpaceRight([later, earlier]);
    expect(spaces.get(earlier)).toBe(150);
    expect(spaces.get(later)).toBe(Infinity);
  });

  it('clamps overlapping items to zero rather than a negative gap', () => {
    const a = { x: 0, right: 100, lane: 'r1#0' };
    const b = { x: 60, right: 160, lane: 'r1#0' };
    const spaces = computeSpaceRight([a, b]);
    expect(spaces.get(a)).toBe(0);
  });
});
