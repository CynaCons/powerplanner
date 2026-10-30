import { describe, it, expect } from 'vitest';
import { parseFileContents } from '../../src/persistence/fileIo';
import { injectDocumentInto } from '../../src/persistence/embedded';
import { toYaml } from '../../src/persistence/yaml';
import { sampleDocument } from '../../src/utils/sample';

/**
 * SRS-PERS-002 — the one door every open goes through, and it had no test.
 *
 * `parseFileContents` picks a reader by extension for the three formats the
 * app writes. The YAML and JSON readers were each covered on their own; what
 * was not covered is this — which reader gets called, and what happens to a
 * file that is none of them. A round-trip is only verified when the file goes
 * out and comes back the way a planner's does.
 */

const doc = sampleDocument();

describe('parseFileContents', () => {
  it('reads a plan back out of a saved .html', () => {
    const html = injectDocumentInto('<html><head></head><body><div id="root"></div></body></html>', doc);
    const back = parseFileContents('plan.html', html);
    expect(back.title).toBe(doc.title);
    expect(back.tasks).toHaveLength(doc.tasks.length);
    expect(back.milestones).toHaveLength(doc.milestones.length);
  });

  it('says plainly when an HTML file holds no roadmap', () => {
    expect(() => parseFileContents('holiday-snaps.html', '<html><body>nothing here</body></html>')).toThrow(
      /No embedded PowerRoadmap data/,
    );
  });

  it('reads .json', () => {
    const back = parseFileContents('plan.json', JSON.stringify(doc));
    expect(back.tasks).toHaveLength(doc.tasks.length);
  });

  it('reads .yaml and .yml', () => {
    const yaml = toYaml(doc);
    expect(parseFileContents('plan.yaml', yaml).tasks).toHaveLength(doc.tasks.length);
    expect(parseFileContents('plan.yml', yaml).tasks).toHaveLength(doc.tasks.length);
  });

  it('tries both readers on a file with no extension it knows', () => {
    expect(parseFileContents('plan', JSON.stringify(doc)).title).toBe(doc.title);
    expect(parseFileContents('plan', toYaml(doc)).title).toBe(doc.title);
  });

  it('refuses a document that is structurally wrong, whatever its name', () => {
    expect(() => parseFileContents('plan.json', '{"tasks": "not an array"}')).toThrow();
  });

  it('carries the fields added in this release through the file', () => {
    // SRS-ELEM-070, SRS-ELEM-072, SRS-ELEM-073: a mark's place in its column,
    // a bar's outline and a label's colour are document state, so they travel
    // with the plan or they are not settings at all.
    const styled = {
      ...doc,
      tasks: doc.tasks.map((t, i) =>
        i === 0 ? { ...t, outline: 'dashed' as const, outlineColor: '#A8452B', textColor: '#6B3F7A' } : t,
      ),
      milestones: doc.milestones.map((m, i) => (i === 0 ? { ...m, anchor: 'end' as const } : m)),
    };
    const html = injectDocumentInto('<html><body><div id="root"></div></body></html>', styled);
    const back = parseFileContents('plan.html', html);
    expect(back.tasks[0].outline).toBe('dashed');
    expect(back.tasks[0].outlineColor).toBe('#A8452B');
    expect(back.tasks[0].textColor).toBe('#6B3F7A');
    expect(back.milestones[0].anchor).toBe('end');
  });
});
