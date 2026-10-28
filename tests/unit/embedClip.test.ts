import { describe, it, expect } from 'vitest';
import { createElement } from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { GanttRenderer } from '../../src/embed';
import { sampleDocument } from '../../src/utils/sample';

/**
 * SRS-TAX-040, the embed half.
 *
 * An SVG `clip-path` resolves in the REFERENCING element's own post-transform
 * user space. The embed renderer wrote its clip rect in absolute coordinates
 * — `(gutterW, AXIS_H, chartW, …)` — and then hung it on the group carrying
 * `translate(gutterW, AXIS_H)`, so the window landed at twice the offset. On a
 * 920px figure that silently destroyed the left 180px of the plot and the top
 * one and a half rows, in every embed and in every committed spec/visual
 * figure generated from this component.
 *
 * Observed red on afe451b: effective clip origin (360, 112), wanted (180, 56).
 */

const WIDTH = 920;
const HEIGHT = 420;
/** The renderer's own rule: ~22% of width, clamped to [80, 180]. */
const GUTTER_W = Math.max(80, Math.min(180, Math.round(WIDTH * 0.22)));
const AXIS_H = 56;

function render() {
  return renderToStaticMarkup(
    createElement(GanttRenderer, {
      document: sampleDocument(),
      width: WIDTH,
      height: HEIGHT,
      options: { themeOverride: 'light', showToday: false },
    }),
  );
}

/** Every opening tag in the markup, as a raw string. */
function tags(markup: string): string[] {
  return markup.match(/<[a-zA-Z][^>]*>/g) ?? [];
}

function translateOf(tag: string): { x: number; y: number } {
  const m = /transform="translate\(([-\d.]+)[ ,]+([-\d.]+)\)"/.exec(tag);
  return m ? { x: Number(m[1]), y: Number(m[2]) } : { x: 0, y: 0 };
}

describe('embed clipping (SRS-TAX-040)', () => {
  it('no element carries a clip-path and a transform at once', () => {
    const offenders = tags(render()).filter((t) => t.includes('clip-path=') && t.includes('transform='));
    expect(offenders).toEqual([]);
  });

  it('the clip window is the plot area, in absolute coordinates', () => {
    const markup = render();

    const rect = /<clipPath id="[^"]*"><rect x="([-\d.]+)" y="([-\d.]+)" width="([-\d.]+)" height="([-\d.]+)"/.exec(markup);
    expect(rect, 'the renderer emits one clipPath holding one rect').not.toBeNull();
    const [cx, cy, cw] = [Number(rect![1]), Number(rect![2]), Number(rect![3])];

    const referrer = tags(markup).find((t) => t.includes('clip-path='));
    expect(referrer, 'something references the clip').toBeDefined();
    const t = translateOf(referrer!);

    // Effective window = the rect, displaced by the referrer's own transform.
    expect({ x: cx + t.x, y: cy + t.y, width: cw }).toEqual({
      x: GUTTER_W,
      y: AXIS_H,
      width: WIDTH - GUTTER_W,
    });
  });
});
