import { describe, it, expect } from 'vitest';
import { layoutDocument } from '../../src/layout/engine';
import { blankDocument } from '../../src/embed';
import { validateDocument, SchemaError } from '../../src/persistence/schema';
import { toYaml, fromYaml } from '../../src/persistence/yaml';
import type { GanttDocument } from '../../src/types/document';

/**
 * SRS-LAY-024 — overlapping bars on one row, as a document setting.
 *
 * `layoutDocument` has taken an `allowOverlap` argument since the lane packer
 * was written, and no caller has ever passed it: grep over `src/` found the
 * parameter's declaration and nothing else. The capability a planner would
 * call think-cell overlap was therefore dead code, reachable only from a test.
 *
 * Observed red on 340ca5a: with `style.allowOverlap` true the engine still
 * packed two overlapping bars into two lanes — chartHeight 72 for one row.
 */

function twoOverlappingBars(allowOverlap?: boolean): GanttDocument {
  const doc = blankDocument('Overlap');
  const rowId = doc.rows[0].id;
  doc.tasks = [
    { id: 'a', rowId, label: 'Design', start: '2026-01-01', end: '2026-03-31', percentComplete: 0 },
    { id: 'b', rowId, label: 'Build', start: '2026-02-01', end: '2026-05-31', percentComplete: 0 },
  ];
  if (allowOverlap !== undefined) {
    (doc.style as { allowOverlap?: boolean }).allowOverlap = allowOverlap;
  }
  return doc;
}

function layout(doc: GanttDocument) {
  return layoutDocument({ doc, viewStart: '2026-01-01', pxPerDay: 2 });
}

describe('overlapping bars on one row', () => {
  it('stacks into lanes by default, as it always has', () => {
    const result = layout(twoOverlappingBars());
    expect(result.rowHeights[0]).toBe(72);
    expect(result.tasks[0].y).not.toBe(result.tasks[1].y);
  });

  it('shares the band when the document asks for it', () => {
    const result = layout(twoOverlappingBars(true));
    expect(result.rowHeights[0]).toBe(36);
    expect(result.tasks[0].y).toBe(result.tasks[1].y);
    // And the bars genuinely cover each other in time, which is the point.
    expect(result.tasks[1].x).toBeLessThan(result.tasks[0].x + result.tasks[0].width);
  });

  it('an explicit argument still wins, for callers that are not a document', () => {
    const packed = layoutDocument({ doc: twoOverlappingBars(true), viewStart: '2026-01-01', pxPerDay: 2, allowOverlap: false });
    expect(packed.rowHeights[0]).toBe(72);
  });

  it('survives the save format, and absence still means stacked', () => {
    const doc = twoOverlappingBars(true);
    expect(validateDocument(JSON.parse(JSON.stringify(doc))).style.allowOverlap).toBe(true);
    expect(validateDocument(fromYaml(toYaml(doc))).style.allowOverlap).toBe(true);

    const old = twoOverlappingBars();
    expect('allowOverlap' in old.style).toBe(false);
    expect(validateDocument(JSON.parse(JSON.stringify(old))).style.allowOverlap).toBeUndefined();
  });

  it('refuses a setting that is not a boolean', () => {
    const bad = twoOverlappingBars();
    (bad.style as { allowOverlap?: unknown }).allowOverlap = 'yes';
    expect(() => validateDocument(bad)).toThrow(SchemaError);
  });
});
