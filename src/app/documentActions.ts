import { useDocumentStore } from '../stores/documentStore';
import { useSelectionStore } from '../stores/selectionStore';
import { useViewStore } from '../stores/viewStore';
import { detachFile, openFile, saveAs, saveToFile } from '../persistence/fileIo';
import type { GanttDocument } from '../types/document';

/**
 * The rules that apply whenever the document is saved or replaced, in one
 * place (SRS-EDIT-018, SRS-PERS-022).
 *
 * They used to live inline in the File menu, and the command palette — which
 * offers the same four actions — had none of them: its Open replaced unsaved
 * work without asking, its Save never marked the document clean, and its
 * template command left the previous plan's file bound as the save target.
 * Two surfaces, two sets of rules, is how that happens; this is one set.
 */

function suggestedName(title: string): string {
  const slug = title.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/(^-|-$)/g, '');
  return `${slug || 'plan'}.html`;
}

/**
 * Ask before throwing away work that is not in a file. Returns false when the
 * user says no, and true when there was nothing to lose.
 */
export function confirmDiscard(whatHappensNext: string): boolean {
  const { doc, revision, savedRevision } = useDocumentStore.getState();
  if (revision === savedRevision) return true;
  return window.confirm(
    `“${doc.title}” has changes that are not in a file. ${whatHappensNext} Continue?`,
  );
}

/**
 * Save, and mark the document clean ONLY if the bytes reached the file the
 * user picked. A cancelled picker falls back to a download, which is not the
 * file they asked for — marking that clean silenced both guards above.
 */
export async function saveCurrent(): Promise<void> {
  const store = useDocumentStore.getState();
  const { written } = await saveToFile(store.doc, suggestedName(store.doc.title));
  if (written) useDocumentStore.getState().markSaved();
}

export async function saveCurrentAs(): Promise<void> {
  const store = useDocumentStore.getState();
  const { written } = await saveAs(store.doc, suggestedName(store.doc.title));
  if (written) useDocumentStore.getState().markSaved();
}

/** Open a file into the app, after asking if that would lose anything. */
export async function openIntoApp(): Promise<void> {
  if (!confirmDiscard('Opening another roadmap replaces it.')) return;
  const next = await openFile();
  if (next) {
    useSelectionStore.getState().clear();
    useViewStore.getState().setDetailsOpen(false);
    useDocumentStore.getState().setDocument(next);
  }
}

/**
 * Start a different document.
 *
 * `detachFile` first, and always: the save target is module state that
 * outlives the document, so without it the next Ctrl+S writes this document
 * over the file the previous one came from.
 */
export function startDocument(doc: GanttDocument): void {
  detachFile();
  useSelectionStore.getState().clear();
  useViewStore.getState().setDetailsOpen(false);
  useDocumentStore.getState().newDocument(doc);
}
