import { describe, it, expect } from 'vitest';
import { validateDocument, SchemaError } from '../../src/persistence/schema';
import { toYaml, fromYaml } from '../../src/persistence/yaml';
import { sampleDocument } from '../../src/utils/sample';

describe('persistence', () => {
  describe('schema validation', () => {
    it('accepts a valid sample document', () => {
      expect(() => validateDocument(sampleDocument())).not.toThrow();
    });

    it('rejects wrong schemaVersion', () => {
      const bad = { ...sampleDocument(), schemaVersion: 99 };
      expect(() => validateDocument(bad)).toThrow(SchemaError);
    });

    it('rejects missing title', () => {
      const bad = { ...sampleDocument() };
      delete (bad as { title?: string }).title;
      expect(() => validateDocument(bad)).toThrow(SchemaError);
    });

    it('rejects invalid date format', () => {
      const bad = { ...sampleDocument() };
      bad.tasks[0] = { ...bad.tasks[0], start: '2026/06/01' };
      expect(() => validateDocument(bad)).toThrow(SchemaError);
    });

    it('rejects percent complete out of range', () => {
      const bad = { ...sampleDocument() };
      bad.tasks[0] = { ...bad.tasks[0], percentComplete: 150 };
      expect(() => validateDocument(bad)).toThrow(SchemaError);
    });
  });

  describe('YAML roundtrip', () => {
    it('preserves the sample document through YAML', () => {
      const doc = sampleDocument();
      const yaml = toYaml(doc);
      const parsed = fromYaml(yaml);
      const validated = validateDocument(parsed);
      expect(validated.title).toBe(doc.title);
      expect(validated.tasks.length).toBe(doc.tasks.length);
      expect(validated.tasks[0].label).toBe(doc.tasks[0].label);
      expect(validated.tasks[0].start).toBe(doc.tasks[0].start);
      expect(validated.tasks[0].end).toBe(doc.tasks[0].end);
      expect(validated.milestones[0].label).toBe(doc.milestones[0].label);
      expect(validated.calendar.scale).toBe(doc.calendar.scale);
    });

    it('preserves a minimal document', () => {
      const doc = {
        schemaVersion: 1 as const,
        title: 'Mini',
        calendar: { scale: 'week' as const, fiscalYearStart: 1, workingDays: [1, 2, 3, 4, 5], holidays: [] },
        rows: [{ id: 'r1', label: 'Row 1', groupId: null }],
        tasks: [{ id: 't1', rowId: 'r1', label: 'Task', start: '2026-01-01', end: '2026-01-08' }],
        milestones: [],
        brackets: [],
        dependencies: [],
        markers: [],
        style: { theme: 'light' as const, preset: 'default' },
      };
      const yaml = toYaml(doc);
      const parsed = fromYaml(yaml);
      const validated = validateDocument(parsed);
      expect(validated.title).toBe('Mini');
      expect(validated.tasks[0].id).toBe('t1');
    });
  });
});
