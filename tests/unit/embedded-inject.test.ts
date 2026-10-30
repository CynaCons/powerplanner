import { describe, it, expect } from 'vitest';
import { injectDocumentInto } from '../../src/persistence/embedded';
import { sampleDocument } from '../../src/utils/sample';

/**
 * The injector is what the updater uses to carry a plan into a freshly
 * downloaded build, so "replaces rather than appends" is load-bearing: if it
 * appends, every update doubles the embedded document.
 */
const doc = sampleDocument();
const TAG = /<script[^>]*id="powerplanner-data"[^>]*>/g;

describe('injectDocumentInto', () => {
  it('REPLACES an existing embedded document rather than appending one', () => {
    const template =
      '<html><body><div id="root"></div>\n' +
      '<script type="application/json" id="powerplanner-data">\n{"old":true}\n</script>\n' +
      '</body></html>';
    const out = injectDocumentInto(template, doc);
    expect(out.match(TAG)?.length).toBe(1);
    expect(out).not.toContain('"old"');
    expect(out).toContain(doc.title);
  });

  it('handles a multi-line existing tag, which a broken regex would miss', () => {
    const template =
      '<html><body>\n<script type="application/json" id="powerplanner-data">\n' +
      '{\n  "deeply": {\n    "nested": true\n  }\n}\n</script>\n</body></html>';
    const out = injectDocumentInto(template, doc);
    expect(out.match(TAG)?.length).toBe(1);
    expect(out).not.toContain('nested');
  });

  it('inserts before </body> when the template has no tag yet', () => {
    const out = injectDocumentInto('<html><body><div id="root"></div></body></html>', doc);
    expect(out.match(TAG)?.length).toBe(1);
    expect(out.indexOf('powerplanner-data')).toBeLessThan(out.indexOf('</body>'));
  });

  /**
   * SRS-PERS-031 — the injector addresses the page, it does not search it.
   *
   * This is the shape of the defect that made every saved file unopenable.
   * A built PowerRoadmap inlines its own source, so the bundle text contains
   * `</body>`, `<div id="root">` and the data tag's own markup — and in the
   * v2.28.0 build the FIRST `</body>` was 137 KB before the real one, inside
   * the minified source of this very function. Observed red on 2dfe113: the
   * plan JSON landed in the middle of the script, which then died with
   * "Unexpected end of input", and the file reopened as the rendered DOM
   * `outerHTML` had captured — a picture of the chart with no app behind it.
   */
  const withFalseAnchors = () =>
    '<!doctype html><html><head>' +
    '<script type="module">const t=`${x}\\n</body>`;const r=s.includes(\'<div id="root">\');' +
    'const tag=`<script type="application/json" id="powerplanner-data">${j}<\\/script>`;</script>' +
    '</head><body><div id="root">rendered chart</div></body></html>';

  it('puts the plan where the page can read it, not inside the bundle', () => {
    const out = injectDocumentInto(withFalseAnchors(), doc);
    const reparsed = new DOMParser().parseFromString(out, 'text/html');
    // An element, not text that happens to look like one: spliced into the
    // script, the tag is part of its source and this finds nothing.
    const tags = reparsed.querySelectorAll('script#powerplanner-data');
    expect(tags).toHaveLength(1);
    expect(JSON.parse(tags[0].textContent ?? '{}').title).toBe(doc.title);
  });

  it('leaves the bundle able to run', () => {
    const out = injectDocumentInto(withFalseAnchors(), doc);
    const reparsed = new DOMParser().parseFromString(out, 'text/html');
    const bundle = reparsed.querySelector('script[type="module"]');
    expect(bundle?.textContent).toContain('<div id="root">');
    expect(bundle?.textContent).toContain('</body>');
    expect(bundle?.textContent).not.toContain(doc.title);
  });

  it('writes an empty chart root, so a dead file cannot look alive', () => {
    const out = injectDocumentInto(withFalseAnchors(), doc);
    expect(out).not.toContain('rendered chart');
    expect(out).toContain('<div id="root"></div>');
  });

  it('round-trips a document through the injected JSON', () => {
    const out = injectDocumentInto('<html><body></body></html>', doc);
    const json = out.slice(out.indexOf('>', out.indexOf('powerplanner-data')) + 1, out.indexOf('</script>'));
    expect(JSON.parse(json).tasks).toHaveLength(doc.tasks.length);
  });
});
