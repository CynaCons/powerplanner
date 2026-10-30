import type { GanttDocument } from '../types/document';
import { validateDocument } from './schema';

const EMBED_ID = 'powerplanner-data';

export function readEmbeddedDocument(): GanttDocument | null {
  const el = document.getElementById(EMBED_ID);
  if (!el) return null;
  const txt = el.textContent?.trim();
  // The page ships the tag empty, so "no text" is a fresh build, not a fault.
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
 * Put the document into a page, working on NODES rather than on text
 * (SRS-PERS-031, SRS-PERS-032).
 *
 * Every textual anchor this used to reach for — `</body>`, `</head>`,
 * `<div id="root">`, the data tag itself — also occurs INSIDE the app's own
 * JavaScript bundle, because the bundle contains the source of these very
 * functions. Measured on the v2.28.0 build: the first `</body>` in the file
 * is at byte 268,929, in the minified source of this function, 137 KB before
 * the real one. So every save spliced the plan into the middle of the bundle;
 * the script then died with "Unexpected end of input" and the reopened file
 * showed the rendered DOM that `outerHTML` had captured — a picture of the
 * chart, holding the document from BEFORE the save, with no app behind it.
 *
 * `querySelector` cannot be fooled by text that merely looks like a tag, so
 * the rule here is: never pattern-match the document, always address it.
 */
function writeInto(root: HTMLElement, doc: GanttDocument): string {
  // A saved file carries data, never a snapshot. Leaving the rendered chart in
  // place is what let a dead file look like a working one.
  root.querySelector('#root')?.replaceChildren();

  let tag = root.querySelector(`script#${EMBED_ID}`);
  if (!tag) {
    const owner = root.ownerDocument;
    tag = owner.createElement('script');
    tag.setAttribute('type', 'application/json');
    tag.id = EMBED_ID;
    (root.querySelector('head') ?? root).appendChild(tag);
  }
  // textContent, so nothing a planner types can close the tag early.
  tag.textContent = escapeForScript(JSON.stringify(doc, null, 2));

  return `<!doctype html>\n${root.outerHTML}`;
}

/**
 * Inject a document into ANY PowerRoadmap HTML, not just the live page.
 *
 * The updater hands this a build it has just downloaded. Parsing it rather
 * than rewriting its text costs one pass and removes the whole class of
 * defect above — a downloaded template contains the same bundle, so the same
 * false anchors.
 */
export function injectDocumentInto(templateHtml: string, doc: GanttDocument): string {
  const parsed = new DOMParser().parseFromString(templateHtml, 'text/html');
  return writeInto(parsed.documentElement, doc);
}

/**
 * The current page, with the document embedded in it.
 *
 * In a built single-file app the page IS the bundle, so a clone of it is a
 * complete program. Under the dev server it is not — the page's only script
 * is `/src/main.tsx`, which resolves to nothing once the file is on disk — so
 * the caller must supply the built template instead. `prepareTemplate` in
 * fileIo does that; this function is the live-page half.
 */
export function buildEmbeddedHtml(doc: GanttDocument): string {
  return writeInto(document.documentElement.cloneNode(true) as HTMLElement, doc);
}

function escapeForScript(s: string): string {
  // `<\/script` is a valid JSON escape for `</script`, so this survives the
  // round trip through JSON.parse unchanged.
  return s.replace(/<\/(script)/gi, '<\\/$1');
}
