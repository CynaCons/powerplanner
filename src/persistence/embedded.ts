import type { GanttDocument } from '../types/document';
import { validateDocument } from './schema';

const EMBED_ID = 'powerplanner-data';

/**
 * A literal regex, deliberately, rather than `new RegExp(\`...\`)`.
 *
 * Built from a template literal, `[\s\S]` is read as the escapes `\s` and
 * `\S` — which a template literal collapses to `s` and `S` — so the pattern
 * silently became `[sS]*?` and matched nothing but the letter S. The embedded
 * tag was then appended rather than replaced on every save, and the file grew
 * a second copy of the document each time. A literal has no such hazard.
 */
const EMBED_TAG_RE = /<script[^>]*id=["']powerplanner-data["'][^>]*>[\s\S]*?<\/script>/i;

export function readEmbeddedDocument(): GanttDocument | null {
  const el = document.getElementById(EMBED_ID);
  if (!el) return null;
  const txt = el.textContent?.trim();
  if (!txt) return null;
  try {
    const parsed = JSON.parse(txt);
    return validateDocument(parsed);
  } catch (e) {
    console.warn('Failed to read embedded document', e);
    return null;
  }
}

/**
 * Inject a document into ANY PowerGantt HTML, not just the live page.
 *
 * Pure, so the updater can hand it a template it has just downloaded, and so
 * the behaviour is testable without a DOM. `buildEmbeddedHtml` is the
 * live-page caller; the updater is the other one.
 */
export function injectDocumentInto(templateHtml: string, doc: GanttDocument): string {
  const json = JSON.stringify(doc, null, 2);
  const newTag =
    `<script type="application/json" id="${EMBED_ID}">\n${escapeForScript(json)}\n</script>`;

  if (EMBED_TAG_RE.test(templateHtml)) {
    return templateHtml.replace(EMBED_TAG_RE, newTag);
  }
  if (/<\/body>/i.test(templateHtml)) {
    return templateHtml.replace(/<\/body>/i, `${newTag}\n</body>`);
  }
  return templateHtml + newTag;
}

/**
 * Returns the current HTML source with the document JSON embedded inside.
 * The page must contain a `<script type="application/json" id="powerplanner-data">` tag.
 * If missing, one is injected just before `</body>`.
 */
export function buildEmbeddedHtml(doc: GanttDocument): string {
  const html = document.documentElement.outerHTML;
  const fullHtml =
    `<!doctype html>\n<html>${html.replace(/^<html[^>]*>/, '').replace(/<\/html>$/, '')}</html>`;
  return injectDocumentInto(fullHtml, doc);
}

function escapeForScript(s: string): string {
  // Prevent the closing </script> sequence from breaking out of the tag
  return s.replace(/<\/(script)/gi, '<\\/$1');
}
