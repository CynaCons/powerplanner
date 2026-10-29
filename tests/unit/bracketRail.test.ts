import { describe, it, expect, beforeEach } from 'vitest';
import { createElement } from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { layoutDocument } from '../../src/layout/engine';
import { Brackets, bracketRail } from '../../src/renderer/Brackets';
import { useViewportStore } from '../../src/stores/viewportStore';
import type { GanttDocument } from '../../src/types/document';

/**
 * SRS-ELEM-031 — a bracket on the first row is a bracket you can see.
 *
 * The rail was drawn at `rowOffsets[topRow] − 12` and its label six pixels
 * above that. `chart-plot-clip` begins at the top of row 0, so on the first row
 * the rail, the label and both resize grips were all at a negative y and were
 * cut away; the only thing left was the 0.4-opacity bottom rule. That rule is
 * exactly what the owner reported after pressing the bracket button on
 * 2026-10-29 — "I created some sort of horizontal line. I have no idea" — and
 * the grips going with it is why the thing could then not be grabbed.
 *
 * Observed red on 79ccbdb: rail y = −12, label y = −18, grips y = −16.
 */

function doc(): GanttDocument {
  return {
    schemaVersion: 1,
    title: 'Brackets',
    calendar: { scale: 'week', fiscalYearStart: 1, workingDays: [1, 2, 3, 4, 5], holidays: [] },
    rows: [
      { id: 'r1', label: 'Row 1', groupId: null },
      { id: 'r2', label: 'Row 2', groupId: null },
    ],
    tasks: [
      { id: 't1', rowId: 'r1', label: 'Port', start: '2026-06-01', end: '2026-06-12', percentComplete: 0 },
    ],
    milestones: [],
    brackets: [{ id: 'br1', label: 'Phase one', start: '2026-06-01', end: '2026-06-12', rowIds: ['r1'] }],
    dependencies: [],
    markers: [],
    style: { theme: 'light', preset: 'default' },
  };
}

beforeEach(() => {
  useViewportStore.setState({ startDate: '2026-06-01', pxPerDay: 8 });
});

describe('the bracket rail stays inside the chart (SRS-ELEM-031)', () => {
  it('puts every y below the top edge of its topmost row', () => {
    const r = bracketRail(0, 0, 36);
    expect(r.railY).toBeGreaterThan(0);
    expect(r.chipTop).toBeGreaterThanOrEqual(0);
    expect(r.labelBaseline).toBeGreaterThan(0);
    expect(r.bottomY).toBeGreaterThan(r.railY);
    expect(r.bottomY).toBeLessThan(36);
  });

  it('follows its top row down the chart', () => {
    const first = bracketRail(0, 0, 36);
    const second = bracketRail(36, 36, 36);
    expect(second.railY - first.railY).toBe(36);
  });

  it('draws nothing above the sheet for a bracket on row 0', () => {
    const layout = layoutDocument({ doc: doc(), viewStart: '2026-06-01', pxPerDay: 8 });
    expect(layout.brackets[0].topRow).toBe(0);

    const markup = renderToStaticMarkup(createElement(Brackets, { layout }));
    const ys = [...markup.matchAll(/\s(?:y|y1|y2)="(-?[\d.]+)"/g)].map((m) => Number(m[1]));
    expect(ys.length).toBeGreaterThan(6);
    for (const y of ys) {
      expect(y).toBeGreaterThanOrEqual(0);
      expect(y).toBeLessThanOrEqual(layout.chartHeight);
    }
  });

  it('keeps the label and both grips, which the clip used to take', () => {
    const layout = layoutDocument({ doc: doc(), viewStart: '2026-06-01', pxPerDay: 8 });
    const markup = renderToStaticMarkup(createElement(Brackets, { layout }));
    expect(markup).toContain('Phase one');
    expect(markup).toContain('data-pp-sub="start"');
    expect(markup).toContain('data-pp-sub="end"');
  });
});
