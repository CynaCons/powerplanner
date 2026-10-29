import { describe, it, expect, beforeEach } from 'vitest';
import { createElement } from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { CommandBar, OVERLAP } from '../../src/app/CommandBar';
import { useDocumentStore } from '../../src/stores/documentStore';
import { useSelectionStore } from '../../src/stores/selectionStore';
import { sampleDocument } from '../../src/utils/sample';

/**
 * SRS-ELEM-061 and SRS-LAY-025 — what the command bar says.
 *
 * Two owner reports from 2026-10-29, both about words rather than behaviour:
 *
 *  - "Select for quick actions · right-click for the rest — this is dumb.
 *    Remove this." The hint was printed whenever nothing was selected, which
 *    is the one moment there is nothing to apply either gesture to, and it
 *    took the width the bar's own controls need on a narrow window.
 *  - "I can change whether the bars are stacked or overlapped. I don't really
 *    understand that." The control read "Stack | Overlap" — the names of the
 *    two layout modes — with the explanation hidden in a tooltip.
 *
 * Observed red on 79ccbdb.
 */

beforeEach(() => {
  useSelectionStore.setState({ items: [] });
  useDocumentStore.getState().setDocument(sampleDocument());
});

describe('the bar teaches nothing it does not have to (SRS-ELEM-061)', () => {
  it('prints no standing hint with an empty selection', () => {
    const markup = renderToStaticMarkup(createElement(CommandBar));
    expect(markup).not.toContain('quick actions');
    expect(markup).not.toContain('cmdbar__hint');
    // The bar is still the bar: tools and the insert group are there.
    expect(markup).toContain('Insert');
  });
});

describe('the stacking control explains itself (SRS-LAY-025)', () => {
  it('names the outcome instead of the layout mode', () => {
    const labels = OVERLAP.map((o) => o.label);
    expect(labels).not.toContain('Stack');
    expect(labels).not.toContain('Overlap');
    for (const o of OVERLAP) {
      expect(o.label).toMatch(/line/i);
    }
  });

  it('states the collision case in each description', () => {
    for (const o of OVERLAP) {
      expect(o.title).toContain('same dates');
      expect(o.title.length).toBeGreaterThan(30);
    }
    expect(OVERLAP.find((o) => o.value === false)!.title).toMatch(/own line/i);
    expect(OVERLAP.find((o) => o.value === true)!.title).toMatch(/on top of each other/i);
  });
});
