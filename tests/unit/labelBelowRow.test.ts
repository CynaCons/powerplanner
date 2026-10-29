import { describe, it, expect, beforeEach } from 'vitest';
import { createElement } from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { layoutDocument, ROW_HEIGHT, BAR_HEIGHT } from '../../src/layout/engine';
import { belowLabelY } from '../../src/renderer/TaskBars';
import { Milestones, anchorFor, anchorAttrFor } from '../../src/renderer/Milestones';
import { useViewportStore } from '../../src/stores/viewportStore';
import type { GanttDocument, LabelOffset } from '../../src/types/document';

/**
 * SRS-EDIT-053, SRS-EDIT-055, SRS-EDIT-056 — a label stays where it is put,
 * and where it is put is inside its own row.
 *
 * A below-placed label was drawn at `bandTop + bandHeight + 12`, five pixels
 * into the next row for a task and ten for a milestone; on the LAST row there
 * is no next row, so it fell past the sheet's bottom edge and the plot clip
 * removed it ("I don't see my label anymore. That's dumb.").
 *
 * The second half of the file is the owner's other symptom, which looked
 * unrelated and was not: a label the user had placed was forced to
 * `text-anchor: start`, so a press that pinned a centred label at its own
 * current position redrew it half a label-width to the right — "it teleports
 * away" — and the gesture that did the pinning was a click that never moved.
 *
 * Observed red on 79ccbdb: a task below-label at bandTop+34 against a row
 * floor of bandTop+29, and text-anchor flipping middle→start on a pin.
 */

/** How far an 11px glyph's descender reaches under its baseline. */
const DESCENDER_PX = 3;
/** Slack between the bar band and the row floor: all a below-label may use. */
const SLACK = (ROW_HEIGHT - BAR_HEIGHT) / 2;

function doc(milestones: GanttDocument['milestones']): GanttDocument {
  return {
    schemaVersion: 1,
    title: 'Labels',
    calendar: { scale: 'week', fiscalYearStart: 1, workingDays: [1, 2, 3, 4, 5], holidays: [] },
    rows: [
      { id: 'r1', label: 'Row 1', groupId: null },
      { id: 'r2', label: 'Row 2', groupId: null },
    ],
    tasks: [],
    milestones,
    brackets: [],
    dependencies: [],
    markers: [],
    style: { theme: 'light', preset: 'default' },
  };
}

describe('a below-placed label is drawn in its own row', () => {
  it('keeps a task label clear of the band under it', () => {
    // A bar band sits centred in its row, so the row floor is SLACK below it.
    const bandTop = 7;
    const baseline = belowLabelY(bandTop, BAR_HEIGHT);

    expect(baseline).toBeGreaterThan(bandTop + BAR_HEIGHT); // below the bar
    expect(baseline + DESCENDER_PX).toBeLessThanOrEqual(bandTop + BAR_HEIGHT + SLACK);
    // And nowhere near the next row's own band, which is what it used to land in.
    expect(baseline + DESCENDER_PX).toBeLessThan(bandTop + ROW_HEIGHT);
  });

  it('keeps a milestone label inside the marker\'s row', () => {
    // A milestone's y is the centre of its sub-row band, so the floor is half
    // a row below it.
    const { dx, dy } = anchorFor('below');
    expect(dx).toBe(0);
    expect(dy + DESCENDER_PX).toBeLessThanOrEqual(ROW_HEIGHT / 2);
    expect(dy).toBeGreaterThan(0);
  });

  it('survives the last row, where there is no next row to spill into', () => {
    const l = layoutDocument({
      doc: doc([{ id: 'm1', rowId: 'r2', label: 'Gate 3', date: '2026-06-10', labelPlacement: 'below' }]),
      viewStart: '2026-06-01',
      pxPerDay: 8,
    });
    const last = l.milestones[0];
    const baseline = last.y + anchorFor('below').dy;

    // The plot clip ends at the sheet bottom, which is the chart's own height.
    expect(baseline + DESCENDER_PX).toBeLessThanOrEqual(l.chartHeight);

    // Same question for a bar on the final row.
    const bandTop = l.chartHeight - ROW_HEIGHT + (ROW_HEIGHT - BAR_HEIGHT) / 2;
    expect(belowLabelY(bandTop, BAR_HEIGHT) + DESCENDER_PX).toBeLessThanOrEqual(l.chartHeight);
  });
});

describe('pinning a label where it already is does not move it', () => {
  beforeEach(() => {
    useViewportStore.setState({ startDate: '2026-06-01', pxPerDay: 8 });
  });

  function render(labelOffset: LabelOffset | null, placement: 'above' | 'below') {
    const d = doc([
      {
        id: 'm1',
        rowId: 'r1',
        label: 'Integration freeze',
        date: '2026-06-10',
        labelPlacement: placement,
        labelOffset,
      },
    ]);
    const layout = layoutDocument({ doc: d, viewStart: '2026-06-01', pxPerDay: 8 });
    return renderToStaticMarkup(createElement(Milestones, { layout }));
  }

  /** The label `<text>`'s own attributes, which carry x and the anchor. */
  function labelTag(markup: string): string {
    const m = /<text[^>]*data-pp-kind="label"[^>]*>/.exec(markup);
    expect(m, 'no milestone label was drawn').not.toBeNull();
    return m![0];
  }

  it.each(['above', 'below'] as const)('%s: the pinned label matches the automatic one', (placement) => {
    const auto = labelTag(render(null, placement));
    const pinned = labelTag(render(anchorFor(placement), placement));

    const xOf = (tag: string) => /\sx="([-\d.]+)"/.exec(tag)?.[1];
    const anchorOf = (tag: string) => /text-anchor="(\w+)"/.exec(tag)?.[1];

    expect(anchorOf(auto)).toBe('middle');
    expect(anchorOf(pinned)).toBe(anchorOf(auto));
    expect(xOf(pinned)).toBe(xOf(auto));
  });

  it('reads the anchor off the offset, with no separate rule for a pinned label', () => {
    expect(anchorAttrFor(anchorFor('above').dx)).toBe('middle');
    expect(anchorAttrFor(anchorFor('below').dx)).toBe('middle');
    expect(anchorAttrFor(anchorFor('right').dx)).toBe('start');
    expect(anchorAttrFor(anchorFor('left').dx)).toBe('end');
  });
});
