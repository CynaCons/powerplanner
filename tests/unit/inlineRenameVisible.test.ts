import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { createElement } from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { InlineLabel } from '../../src/app/InlineLabel';

/**
 * SRS-EDIT-050 — the name you are typing is visible while you type it.
 *
 * `InlineLabel` carried `color = 'white'` as a prop default and neither caller
 * passed anything else, over a field that is `rgba(255,255,255,0.92)` in the
 * `:root`, `light` and `print` themes. White on white: the name appeared only
 * on Enter, when the SVG text underneath took the value back. The caret was
 * white too. It read as correct for one instant because the initial `select()`
 * puts the browser's selection highlight behind the glyphs, which is why the
 * defect survived a session of use before it was named.
 *
 * Observed red on 79ccbdb: contrast 1.00:1 in `:root`, `light` and `print`.
 *
 * The check is run against the stylesheet rather than through
 * `getComputedStyle`, because jsdom does not substitute `var()`. Resolving the
 * tokens here is also what makes it a gate on a FUTURE theme: adding a fifth
 * block with a pale editor background fails this test, not the owner's eyes.
 */

const CSS = readFileSync(resolve(__dirname, '../../src/styles/global.css'), 'utf8');

/** Every theme the app ships: the defaults, plus one block per `ThemeName`. */
const THEMES = [':root', "[data-theme='light']", "[data-theme='dark']", "[data-theme='print']"];

/** The declarations of one top-level rule, by selector. */
function block(selector: string): string {
  const at = CSS.indexOf(`${selector} {`);
  expect(at, `${selector} is missing from global.css`).toBeGreaterThan(-1);
  return CSS.slice(at, CSS.indexOf('\n}', at));
}

function token(selector: string, name: string): string {
  const m = new RegExp(`${name}:\\s*([^;]+);`).exec(block(selector));
  expect(m, `${name} is missing from ${selector}`).not.toBeNull();
  return m![1].trim();
}

type Rgba = { r: number; g: number; b: number; a: number };

function parseColor(value: string): Rgba {
  const hex = /^#([0-9a-f]{6})$/i.exec(value);
  if (hex) {
    const n = parseInt(hex[1], 16);
    return { r: (n >> 16) & 255, g: (n >> 8) & 255, b: n & 255, a: 1 };
  }
  const rgba = /^rgba?\(([^)]+)\)$/i.exec(value);
  if (rgba) {
    const p = rgba[1].split(',').map((s) => Number(s.trim()));
    return { r: p[0], g: p[1], b: p[2], a: p[3] ?? 1 };
  }
  throw new Error(`cannot parse colour: ${value}`);
}

/** `over` is what the field is painted on: the theme's own surface. */
function composite(top: Rgba, over: Rgba): Rgba {
  return {
    r: top.r * top.a + over.r * (1 - top.a),
    g: top.g * top.a + over.g * (1 - top.a),
    b: top.b * top.a + over.b * (1 - top.a),
    a: 1,
  };
}

function relativeLuminance({ r, g, b }: Rgba): number {
  const lin = (c: number) => {
    const s = c / 255;
    return s <= 0.03928 ? s / 12.92 : Math.pow((s + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
}

function contrast(a: Rgba, b: Rgba): number {
  const la = relativeLuminance(a);
  const lb = relativeLuminance(b);
  return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
}

describe('the inline rename editor takes its colour from the theme', () => {
  it('declares text and caret colour in the stylesheet, not in the component', () => {
    const rule = block('.inline-editor');
    expect(rule).toMatch(/color:\s*var\(--color-text\)/);
    expect(rule).toMatch(/caret-color:\s*var\(--color-text\)/);
  });

  it('renders with that class and hard-codes no colour of its own', () => {
    const markup = renderToStaticMarkup(
      createElement(InlineLabel, {
        text: 'Integration freeze',
        x: 10,
        y: 10,
        width: 120,
        height: 18,
        onCommit: () => {},
        onCancel: () => {},
      }),
    );
    expect(markup).toContain('class="inline-editor"');
    // The whole defect in one assertion: no colour may be set inline, because
    // an inline colour cannot follow the theme.
    expect(markup).not.toMatch(/color:/);
    expect(markup).not.toMatch(/white/i);
  });

  it.each(THEMES)('reads at 4.5:1 or better in %s', (theme) => {
    const text = parseColor(token(theme, '--color-text'));
    const field = parseColor(token(theme, '--color-inline-editor-bg'));
    const surface = parseColor(token(theme, '--color-bg'));

    const ratio = contrast(text, composite(field, surface));
    expect(ratio, `${theme}: text on the editor field`).toBeGreaterThanOrEqual(4.5);
  });

  it.each(THEMES)('keeps the selection highlight readable in %s', (theme) => {
    // The default highlight was the accident that hid the defect; the rule
    // states both halves so the first keystroke changes nothing but the text.
    const rule = block('.inline-editor::selection');
    expect(rule).toMatch(/color:\s*var\(--color-text\)/);

    const text = parseColor(token(theme, '--color-text'));
    const field = parseColor(token(theme, '--color-inline-editor-bg'));
    const surface = parseColor(token(theme, '--color-bg'));
    const highlight = parseColor(token(theme, '--color-accent-soft'));

    const ratio = contrast(text, composite(highlight, composite(field, surface)));
    expect(ratio, `${theme}: text on its own selection highlight`).toBeGreaterThanOrEqual(4.5);
  });
});
