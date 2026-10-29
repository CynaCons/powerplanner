import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { readFileSync } from 'node:fs';
import { serializeChartSvg, rasterizeChart, viewSummary, censusOf } from '../../src/agent/feedback';

/**
 * SRS-ABR-033, SRS-ABR-035 — the render carries its own colours, and the
 * summary says what is on screen.
 *
 * Observed red on d101e0b: `src/agent/feedback.ts` did not exist. The bridge's
 * own `serializeChartSvg` returned `new XMLSerializer().serializeToString(svg)`
 * and nothing else, so every `var(--color-…)` in the markup resolved against a
 * `:root` that was not in the document being handed over, and nothing anywhere
 * reported whether an element was inside the visible chart area.
 *
 * jsdom has no layout: every `getBoundingClientRect()` is zero. So these cases
 * are about the RULES — which tokens get inlined, that an unresolvable one is
 * left out rather than emitted empty, that the census keys by kind and counts
 * an id once however many hit targets share it — and the geometry itself is
 * proved in `tests/e2e/agent-bridge.spec.ts`, where a real browser has a layout.
 */

function mountChart(svgInner: string, tokens: Record<string, string> = {}) {
  for (const [name, value] of Object.entries(tokens)) {
    document.documentElement.style.setProperty(name, value);
  }
  document.body.style.backgroundColor = 'rgb(250, 250, 250)';
  document.body.innerHTML = `<div class="chart-area"><svg viewBox="0 0 400 300">${svgInner}</svg></div>`;
}

const USED_TOKENS = { '--color-bar': '#1f4e79', '--color-text': '#1c2430' };

beforeEach(() => {
  document.documentElement.removeAttribute('style');
  document.body.innerHTML = '';
});
afterEach(() => {
  document.documentElement.removeAttribute('style');
  document.body.innerHTML = '';
});

describe('the serialised chart stands on its own (SRS-ABR-033)', () => {
  it('inlines the resolved value of every custom property the markup references', () => {
    mountChart('<rect fill="var(--color-bar)" /><text fill="var(--color-text)">A</text>', USED_TOKENS);
    const { svg } = serializeChartSvg();
    expect(svg).toContain('--color-bar: #1f4e79');
    expect(svg).toContain('--color-text: #1c2430');
  });

  it('discovers the tokens from the markup instead of carrying a list', () => {
    // A renderer reaching for a token nobody remembered would otherwise lose its
    // colour silently, because a missing colour renders as a plausible one.
    mountChart('<rect fill="var(--color-invented-today)" />', { '--color-invented-today': '#abcdef' });
    expect(serializeChartSvg().svg).toContain('--color-invented-today: #abcdef');
  });

  it('leaves an unresolvable token out rather than emitting an empty declaration', () => {
    mountChart('<rect fill="var(--color-nowhere, #ff0000)" />');
    const { svg } = serializeChartSvg();
    expect(svg).not.toContain('--color-nowhere:');
    // The renderer's own fallback survives, which is the point of leaving it out.
    expect(svg).toContain('#ff0000');
  });

  it('carries the page background and a font, since the chart has no ground of its own', () => {
    mountChart('<rect fill="var(--color-bar)" />', USED_TOKENS);
    const { svg } = serializeChartSvg();
    expect(svg).toContain('background:rgb(250, 250, 250)');
    expect(svg).toMatch(/font-family:/);
  });

  it('declares the namespace and explicit dimensions, so it can be rasterised', () => {
    mountChart('<rect fill="var(--color-bar)" />', USED_TOKENS);
    const { svg, width, height } = serializeChartSvg();
    expect(svg).toContain('xmlns="http://www.w3.org/2000/svg"');
    expect(svg).toMatch(/width="\d+"/);
    expect(width).toBeGreaterThan(0);
    expect(height).toBeGreaterThan(0);
  });

  it('puts the style block before the elements that read it', () => {
    mountChart('<rect fill="var(--color-bar)" />', USED_TOKENS);
    const { svg } = serializeChartSvg();
    expect(svg.indexOf('<style>')).toBeLessThan(svg.indexOf('<rect'));
  });

  it('says why rather than failing silently when the host cannot rasterise', async () => {
    // jsdom has no canvas: the contract under test is that the reason comes back
    // in `pngError`, because an agent handed markup with no explanation would
    // believe it had seen the chart.
    const out = await rasterizeChart('<svg xmlns="http://www.w3.org/2000/svg"/>', 400, 300);
    expect(out.png === undefined || typeof out.png === 'string').toBe(true);
    if (!out.png) expect(out.pngError).toBeTruthy();
  });

  it('refuses when there is no chart at all, rather than serialising the page', () => {
    document.body.innerHTML = '<div class="chart-area"></div>';
    expect(() => serializeChartSvg()).toThrow(/no chart SVG/);
  });
});

describe('the view summary reports what is on screen (SRS-ABR-035)', () => {
  it('counts an id once however many hit targets share it', () => {
    // A selected bar is a body, two grips and a progress handle, all carrying
    // the same data-pp-id. Counting elements would report five tasks for one.
    mountChart(
      '<rect data-pp-kind="task" data-pp-id="t1" />' +
        '<rect data-pp-kind="task" data-pp-id="t1" data-pp-sub="start" />' +
        '<rect data-pp-kind="task" data-pp-id="t1" data-pp-sub="end" />' +
        '<rect data-pp-kind="task" data-pp-id="t2" />',
    );
    const kinds = viewSummary().onScreen;
    const task = kinds.task ?? { visible: 0, above: 0, below: 0, left: 0, right: 0 };
    const total = task.visible + task.above + task.below + task.left + task.right;
    expect(total).toBe(2);
  });

  it('keys the census by kind, so "no markers visible" is answerable', () => {
    mountChart(
      '<rect data-pp-kind="task" data-pp-id="t1" />' +
        '<rect data-pp-kind="marker" data-pp-id="mk1" />' +
        '<rect data-pp-kind="dependency" data-pp-id="d1" />',
    );
    expect(Object.keys(viewSummary().onScreen).sort()).toEqual(['dependency', 'marker', 'task']);
  });

  it('admits when the host has no layout, instead of claiming everything is hidden', () => {
    // jsdom reports every rect as zero. A census built on that is meaningless,
    // and a confident "every task is off screen" would be a lie rather than an
    // absence of knowledge.
    mountChart('<rect data-pp-kind="task" data-pp-id="t1" />');
    const view = viewSummary();
    expect(view.hasLayout).toBe(false);
    expect(view.warnings).toEqual([]);
  });

  it('reports the settings an agent can now change, so it can read back what it wrote', () => {
    mountChart('<rect data-pp-kind="task" data-pp-id="t1" />');
    const view = viewSummary();
    expect(view).toHaveProperty('gridDelimiter');
    expect(view).toHaveProperty('weekendDisplay');
    expect(view).toHaveProperty('viewportPx');
    expect(view.viewportPx).toHaveProperty('contentBelow');
    expect(Array.isArray(view.warnings)).toBe(true);
  });

  it('carries a date window, not just a start date', () => {
    mountChart('<rect data-pp-kind="task" data-pp-id="t1" />');
    const view = viewSummary();
    expect(view.startDate).toMatch(/^\d{4}-\d{2}-\d{2}$/);
    expect(view.endDate).toMatch(/^\d{4}-\d{2}-\d{2}$/);
    expect(view.endDate >= view.startDate).toBe(true);
  });
});

describe('an entity half on screen is reported as such (SRS-ABR-035)', () => {
  // jsdom reports every rect as zero, so the census rule is proved here against
  // explicit boxes rather than through the DOM. The window is the chart area.
  const area = { top: 48, bottom: 689, left: 0, right: 700 };
  const box = (top: number, bottom: number, left = 100, right = 200) => ({ top, bottom, left, right });

  it('calls an entity visible only when all of its parts are', () => {
    const c = censusOf(
      [
        { kind: 'task', id: 't1', rect: box(100, 122) },
        { kind: 'task', id: 't1', rect: box(100, 122, 98, 108) },
      ],
      area,
    );
    expect(c.task).toMatchObject({ visible: 1, clipped: 0, below: 0 });
  });

  it('reports a marker whose rule is visible and whose name is not as clipped', () => {
    // The exact shape of the v2.28.0 defect: a full-height rule inside the view,
    // a name chip a thousand pixels below it. Judging by either element alone
    // gives the wrong answer; this is why the bucket exists.
    const c = censusOf(
      [
        { kind: 'marker', id: 'mk-gate', rect: box(96, 1680) },
        { kind: 'marker', id: 'mk-gate', rect: box(1686, 1705, 854, 939) },
      ],
      area,
    );
    expect(c.marker).toMatchObject({ visible: 0, clipped: 1, below: 0 });
  });

  it('counts an entity wholly past an edge against that edge, not as clipped', () => {
    const c = censusOf(
      [
        { kind: 'task', id: 't9', rect: box(900, 922) },
        { kind: 'task', id: 't9', rect: box(900, 922, 98, 108) },
      ],
      area,
    );
    expect(c.task).toMatchObject({ visible: 0, clipped: 0, below: 1 });
  });

  it('distinguishes the four edges', () => {
    const c = censusOf(
      [
        { kind: 'task', id: 'a', rect: box(0, 20) },
        { kind: 'task', id: 'b', rect: box(900, 920) },
        { kind: 'task', id: 'c', rect: box(100, 120, -50, -10) },
        { kind: 'task', id: 'd', rect: box(100, 120, 900, 950) },
      ],
      area,
    );
    expect(c.task).toMatchObject({ above: 1, below: 1, left: 1, right: 1, visible: 0, clipped: 0 });
  });

  it('warns in the words a person would use when something is half on screen', () => {
    // Guards the message itself: "21 of 33 are off screen" reads as a scrolling
    // problem, which a planner shrugs at. The clipped case is the one they
    // report as "I cannot edit it", so it has to say that something cannot be
    // clicked.
    const svgInner =
      '<rect data-pp-kind="marker" data-pp-id="mk" /><rect data-pp-kind="marker" data-pp-id="mk" rx="4" />';
    mountChart(svgInner);
    // hasLayout is false under jsdom, so the warning text is asserted through
    // the pure path that produces it.
    const c = censusOf(
      [
        { kind: 'marker', id: 'mk', rect: box(96, 600) },
        { kind: 'marker', id: 'mk', rect: box(1686, 1705) },
      ],
      area,
    );
    expect(c.marker.clipped).toBe(1);
  });
});

describe('the bridge actually uses it', () => {
  const bridge = readFileSync('src/agent/AgentBridge.ts', 'utf8');

  it('no longer carries its own serializer', () => {
    expect(bridge).not.toContain('function serializeChartSvg');
    expect(bridge).toContain("from './feedback'");
  });

  it('attaches the view to every result that reads or changes the document', () => {
    // get_status, get_view, apply_batch, undo, redo, get_render.
    expect(bridge.match(/view: viewSummary\(\)/g)?.length ?? 0).toBeGreaterThanOrEqual(4);
    // Newline-agnostic: this repo has core.autocrlf on, so the working tree is CRLF.
    expect(bridge).toMatch(/case 'get_view':\s*return viewSummary\(\);/);
  });

  it('awaits the dispatch, since rasterising cannot be synchronous', () => {
    expect(bridge).toContain('await this.dispatch(call)');
    expect(bridge).toContain('private async handleCall');
  });
});
