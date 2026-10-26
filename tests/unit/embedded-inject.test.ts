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

  it('round-trips a document through the injected JSON', () => {
    const out = injectDocumentInto('<html><body></body></html>', doc);
    const json = out.slice(out.indexOf('>', out.indexOf('powerplanner-data')) + 1, out.indexOf('</script>'));
    expect(JSON.parse(json).tasks).toHaveLength(doc.tasks.length);
  });
});
