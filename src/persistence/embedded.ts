import type { GanttDocument } from '../types/document';
import { validateDocument } from './schema';

const EMBED_ID = 'powerplanner-data';

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
 * Returns the current HTML source with the document JSON embedded inside.
 * The page must contain a `<script type="application/json" id="powerplanner-data">` tag.
 * If missing, one is injected just before `</body>`.
 */
export function buildEmbeddedHtml(doc: GanttDocument): string {
  const html = document.documentElement.outerHTML;
  const json = JSON.stringify(doc, null, 2);
  const newTag = `<script type="application/json" id="${EMBED_ID}">\n${escapeForScript(json)}\n</script>`;
  const fullHtml = `<!doctype html>\n<html>${html.replace(/^<html[^>]*>/, '').replace(/<\/html>$/, '')}</html>`;

  // Try to replace existing tag first
  const tagRegex = new RegExp(`<script[^>]*id=["']${EMBED_ID}["'][^>]*>[\\s\\S]*?<\\/script>`, 'i');
  if (tagRegex.test(fullHtml)) {
    return fullHtml.replace(tagRegex, newTag);
  }
  // Otherwise insert before </body>
  return fullHtml.replace(/<\/body>/i, `${newTag}\n</body>`);
}

function escapeForScript(s: string): string {
  // Prevent the closing </script> sequence from breaking out of the tag
  return s.replace(/<\/(script)/gi, '<\\/$1');
}
