import { describe, it, expect } from 'vitest';
import { blankDocument, validateDocument } from '../../src/embed';

describe('embed barrel', () => {
  it('blankDocument is valid against the schema', () => {
    const doc = blankDocument();
    expect(() => validateDocument(doc)).not.toThrow();
    expect(doc.title).toBe('Untitled plan');
    expect(doc.tasks).toHaveLength(0);
    expect(doc.rows).toHaveLength(1);
  });

  it('blankDocument accepts a custom title', () => {
    const doc = blankDocument('My plan');
    expect(doc.title).toBe('My plan');
  });
});
