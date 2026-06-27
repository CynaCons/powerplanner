import { describe, it, expect } from 'vitest';
import { layoutDocument, ROW_HEIGHT } from '../../src/layout/engine';
import { validateDocument } from '../../src/persistence/schema';
import fixture from '../../spec/fixtures/basic-chart.json';
import expected from '../../spec/fixtures/basic-chart.expected.json';
import type { GanttDocument } from '../../src/types/document';

// Conformance: the WEB implementation (src/) must reproduce the abstract layout
// recorded in spec/fixtures. The PowerPoint implementation (native/) runs the
// same fixtures against its C++ layout port. Same inputs -> same abstract output
// keeps the two implementations locked to the one concept in spec/.

describe('spec conformance — basic-chart', () => {
  const doc = fixture.document as unknown as GanttDocument;
  const viewStart = fixture.view.viewStart;

  it('the fixture document is valid against the schema', () => {
    expect(() => validateDocument(doc)).not.toThrow();
  });

  it('the web engine reproduces the expected abstract layout (scale = 1)', () => {
    const r = layoutDocument({ doc, viewStart, pxPerDay: 1 });

    const abstract = {
      visibleRowIds: r.visibleRows.map((row) => row.id),
      rowSlots: r.rowHeights.map((h) => h / ROW_HEIGHT),
      rowOffsets: r.rowOffsets.map((o) => o / ROW_HEIGHT),
      chartRows: r.chartHeight / ROW_HEIGHT,
      tasks: r.tasks.map((t) => ({
        id: t.task.id,
        rowIndex: t.rowIndex,
        subRow: t.subRow,
        xDay: t.x,
        widthDays: t.width,
      })),
      milestones: r.milestones.map((m) => ({
        id: m.milestone.id,
        rowIndex: m.rowIndex,
        xDay: m.x,
      })),
      summaries: r.summaries.map((s) => ({
        rowId: s.rowId,
        rowIndex: s.rowIndex,
        xDay: s.x,
        widthDays: s.width,
      })),
      brackets: r.brackets.map((b) => ({
        id: b.bracket.id,
        xDay: b.x,
        widthDays: b.width,
        topRow: b.topRow,
        bottomRow: b.bottomRow,
      })),
      dependencies: r.dependencies.map((d) => ({
        id: d.dependency.id,
        fromXDay: d.fromX,
        toXDay: d.toX,
      })),
    };

    const { _note, ...want } = expected as typeof expected & { _note?: string };
    expect(abstract).toEqual(want);
  });
});
