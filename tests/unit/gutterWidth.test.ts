import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { gutterWidthFor, GUTTER_W_MIN, GUTTER_W_MAX } from '../../src/layout/gutterWidth';
import type { Row } from '../../src/types/document';

/**
 * SRS-LAY-050, SRS-LAY-051 — the row panel sizes itself to its own names.
 *
 * The width was `ROW_GUTTER_W_BASE = 200` whatever was in the panel, so a long
 * package name was cut off mid-word and a plan of short names spent a quarter
 * of the window on empty panel. Owner report 2026-10-29: "the left panel
 * should auto-size to its text".
 *
 * Observed red on 79ccbdb: `src/layout/gutterWidth.ts` did not exist and the
 * width was a module constant with no input, so every case below failed on
 * import.
 *
 * jsdom has no 2d canvas context, so `canvasMeasure` falls through to its
 * per-character estimate here. These cases are therefore about the *rule* —
 * longer names mean a wider panel, depth costs indent, the clamp holds — not
 * about exact pixel advances, which only a real browser can give.
 */

const row = (id: string, label: string, groupId: string | null = null, collapsed?: boolean): Row => ({
  id,
  label,
  groupId,
  ...(collapsed === undefined ? {} : { collapsed }),
});

/** A measure with a known answer, so the arithmetic is checkable. */
const flat = (text: string) => text.length * 10;
const measure = (text: string) => flat(text);

describe('the row panel measures its own names (SRS-LAY-050)', () => {
  it('grows with the longest name', () => {
    const short = gutterWidthFor([row('r1', 'Spec')], measure);
    const long = gutterWidthFor([row('r1', 'Spec'), row('r2', 'Qualification campaign')], measure);
    expect(long).toBeGreaterThan(short);
  });

  it('places a top-level name at its own indent plus padding', () => {
    // x=12 for a top-level row, 16px of right padding.
    expect(gutterWidthFor([row('r1', 'abcdefghijklmnop')], measure)).toBe(12 + 160 + 16);
  });

  it('charges a child row for its indent', () => {
    const parent = row('g1', 'Phase');
    const child = row('r1', 'abcdefghij', 'g1');
    // x=30 for a child, and 'Phase' under x=38 at bold 12 is the shorter of the two.
    expect(gutterWidthFor([parent, child], measure)).toBe(30 + 100 + 16);
  });

  it('ignores the names of rows a collapsed group is hiding', () => {
    const rows = [
      row('g1', 'Phase', null, true),
      row('r1', 'a name far longer than the header', 'g1'),
    ];
    const collapsed = gutterWidthFor(rows, measure);
    const open = gutterWidthFor([row('g1', 'Phase'), rows[1]], measure);
    expect(collapsed).toBeLessThan(open);
    // Only the header is left to measure: x=38 for a group, 'Phase' is 5 chars.
    expect(collapsed).toBe(GUTTER_W_MIN);
  });

  it('never leaves the usable range, however long or short the names', () => {
    expect(gutterWidthFor([row('r1', 'x')], measure)).toBe(GUTTER_W_MIN);
    expect(gutterWidthFor([row('r1', 'y'.repeat(500))], measure)).toBe(GUTTER_W_MAX);
    expect(gutterWidthFor([], measure)).toBe(GUTTER_W_MIN);
  });

  it('agrees with the x offsets RowGutter actually draws', () => {
    // The two files carry the offsets separately and have to match; if the
    // renderer moves its text, this is what says so.
    const gutter = readFileSync('src/renderer/RowGutter.tsx', 'utf8');
    expect(gutter).toContain('x={38}');
    expect(gutter).toContain("x={row.groupId ? 30 : 12}");
    const widths = readFileSync('src/layout/gutterWidth.ts', 'utf8');
    expect(widths).toContain('X_GROUP = 38');
    expect(widths).toContain('X_CHILD = 30');
    expect(widths).toContain('X_TOP = 12');
  });
});

describe("a width the planner chose is not revised (SRS-LAY-051)", () => {
  it('holds the drag behind a nullable state, not behind the measured width', () => {
    // The measured width is a default, so the state has to be able to say
    // "nobody has chosen": a `number` initialised to the auto width could not
    // tell a stored 200 from a measured 200, and the panel would stop
    // following its names the first time they happened to agree.
    const chart = readFileSync('src/app/ChartArea.tsx', 'utf8');
    expect(chart).toContain('useState<number | null>');
    expect(chart).toContain('gutterW ?? autoGutterW');
  });

  it('does not store a width when no drag ever moved', () => {
    const chart = readFileSync('src/app/ChartArea.tsx', 'utf8');
    expect(chart).toContain('if (gutterW !== null)');
  });
});
