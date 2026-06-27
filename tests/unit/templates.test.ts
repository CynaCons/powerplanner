import { describe, it, expect } from 'vitest';
import { TEMPLATES } from '../../src/utils/templates';
import { sampleDocument } from '../../src/utils/sample';
import { blankDocument } from '../../src/embed';
import { validateDocument } from '../../src/persistence/schema';

describe('templates', () => {
  it('has 5 templates (4 starters + blank)', () => {
    expect(TEMPLATES).toHaveLength(5);
    expect(TEMPLATES.find((t) => t.id === 'blank')).toBeTruthy();
  });

  it.each(TEMPLATES.map((t) => [t.id, t]))(
    '%s passes schema validation',
    (_id, tpl) => {
      expect(() => validateDocument(tpl.doc)).not.toThrow();
    },
  );

  it('defaults sample, blank, and template documents to light theme', () => {
    expect(sampleDocument().style.theme).toBe('light');
    expect(blankDocument().style.theme).toBe('light');
    for (const tpl of TEMPLATES) {
      expect(tpl.doc.style.theme, `${tpl.id} should default to light`).toBe('light');
    }
  });

  it('every task references a known row', () => {
    for (const tpl of TEMPLATES) {
      const rowIds = new Set(tpl.doc.rows.map((r) => r.id));
      for (const t of tpl.doc.tasks) {
        expect(rowIds.has(t.rowId), `${tpl.id}: task ${t.id} references unknown row ${t.rowId}`).toBe(true);
      }
      for (const m of tpl.doc.milestones) {
        expect(rowIds.has(m.rowId), `${tpl.id}: milestone ${m.id} references unknown row ${m.rowId}`).toBe(true);
      }
      for (const b of tpl.doc.brackets) {
        for (const rid of b.rowIds) {
          expect(rowIds.has(rid), `${tpl.id}: bracket ${b.id} references unknown row ${rid}`).toBe(true);
        }
      }
    }
  });

  it('every dependency references known tasks', () => {
    for (const tpl of TEMPLATES) {
      const taskIds = new Set(tpl.doc.tasks.map((t) => t.id));
      for (const d of tpl.doc.dependencies) {
        expect(taskIds.has(d.from), `${tpl.id}: dep ${d.id} unknown 'from' ${d.from}`).toBe(true);
        expect(taskIds.has(d.to), `${tpl.id}: dep ${d.id} unknown 'to' ${d.to}`).toBe(true);
      }
    }
  });

  it('tasks have start ≤ end', () => {
    for (const tpl of TEMPLATES) {
      for (const t of tpl.doc.tasks) {
        expect(t.start <= t.end, `${tpl.id}: task ${t.id} has start > end`).toBe(true);
      }
    }
  });
});
