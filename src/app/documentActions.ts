import { useDocumentStore } from '../stores/documentStore';
import { useSelectionStore } from '../stores/selectionStore';
import { useViewStore } from '../stores/viewStore';
import { detachFile, openFile, reloadFromFile, saveAs, saveToFile, type SaveResult } from '../persistence/fileIo';
import { notify } from '../stores/noticeStore';
import { useFileStore } from '../stores/fileStore';
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
 * Mark the document clean ONLY if the bytes reached the file the user picked,
 * and say which of the four things happened. A cancelled picker falls back to
 * a download, which is not the file they asked for — marking that clean
 * silenced both guards above, and saying nothing made it indistinguishable
 * from a save that worked.
 */
function reportSave(result: SaveResult): void {
  switch (result.outcome) {
    case 'written':
      useDocumentStore.getState().markSaved();
      notify(`Saved to ${result.name}.`);
      return;
    case 'downloaded-cancelled':
      notify(`You cancelled the save, so ${result.name} went to your downloads instead. The plan is still unsaved.`, 'warn');
      return;
    case 'downloaded-no-picker':
      notify(`This browser cannot save in place, so ${result.name} went to your downloads. The plan is still unsaved.`, 'warn');
      return;
    case 'permission-denied':
      notify(`This browser will not let the app write ${result.name}. Use Save as to choose it again.`, 'fail');
      return;
    case 'failed':
      notify(`${result.name} could not be saved: ${result.error ?? 'the write failed'}.`, 'fail');
  }
}

export async function saveCurrent(): Promise<void> {
  const store = useDocumentStore.getState();
  reportSave(await saveToFile(store.doc, suggestedName(store.doc.title)));
}

export async function saveCurrentAs(): Promise<void> {
  const store = useDocumentStore.getState();
  reportSave(await saveAs(store.doc, suggestedName(store.doc.title)));
}

/** Open a file into the app, after asking if that would lose anything. */
export async function openIntoApp(): Promise<void> {
  if (!confirmDiscard('Opening another roadmap replaces it.')) return;
  const result = await openFile();
  if ('error' in result) {
    if (result.error === 'cancelled') return;
    notify(`${result.name ?? 'That file'} is not a PowerRoadmap file: ${result.message ?? 'it holds no plan'}.`, 'fail');
    return;
  }
  useSelectionStore.getState().clear();
  useViewStore.getState().setDetailsOpen(false);
  useDocumentStore.getState().setDocument(result.doc);
  notify(`Opened ${result.name}.`);
}

/**
 * Throw away what is in memory and read the bound file again.
 *
 * Asks first, because this is the one command whose whole purpose is to lose
 * work — and it is reachable while an autosave may already have written some
 * of that work to the file, so "nothing to lose" is never assumed here.
 */
export async function revertToFile(): Promise<void> {
  const { name } = useFileStore.getState();
  if (!name) {
    notify('This plan is in no file, so there is nothing to go back to.', 'warn');
    return;
  }
  if (!window.confirm(`Discard the changes on screen and reload ${name} from disk?`)) return;
  const result = await reloadFromFile();
  if ('error' in result) {
    if (result.error === 'cancelled') return;
    notify(`${name} could not be read: ${result.message ?? 'it holds no plan'}.`, 'fail');
    return;
  }
  useSelectionStore.getState().clear();
  useViewStore.getState().setDetailsOpen(false);
  useDocumentStore.getState().setDocument(result.doc);
  notify(`Reloaded ${result.name}.`);
}

/**
 * Start a different document.
 *
 * `detachFile` first, and always: the save target is module state that
 * outlives the document, so without it the next Ctrl+S writes this document
 * over the file the previous one came from.
 */
export function startDocument(doc: GanttDocument, what = doc.title): void {
  detachFile();
  useSelectionStore.getState().clear();
  useViewStore.getState().setDetailsOpen(false);
  useDocumentStore.getState().newDocument(doc);
  // Releasing the save target is invisible, and the next Ctrl+S opens a picker
  // with no explanation unless it is said here.
  notify(`Started “${what}”. It is not saved to a file yet.`);
}

/**
 * Delete everything selected, and say what went.
 *
 * Three surfaces looped per-item store calls with their own copy of this
 * (the Delete key, the palette, the selection bar), so the rules could drift
 * and none of them reported. One function, one announcement.
 */
export function deleteSelection(): number {
  const sel = useSelectionStore.getState();
  const items = sel.items.filter((i) => i.kind !== 'row');
  if (items.length === 0) return 0;
  const s = useDocumentStore.getState();
  // One undo frame for the whole command. Deleting five bars used to cost
  // five Ctrl+Z, with the items reappearing one at a time.
  s.transact(() => {
    for (const item of items) {
      if (item.kind === 'task') s.deleteTask(item.id);
      else if (item.kind === 'milestone') s.deleteMilestone(item.id);
      else if (item.kind === 'bracket') s.deleteBracket(item.id);
      else if (item.kind === 'dependency') s.deleteDependency(item.id);
      else if (item.kind === 'marker') s.deleteMarker(item.id);
    }
  });
  sel.clear();
  const noun = items.length === 1 ? nounFor(items[0].kind) : 'items';
  notify(`Deleted ${items.length} ${noun}. Undo brings ${items.length === 1 ? 'it' : 'them'} back.`, 'warn');
  return items.length;
}

function nounFor(kind: string): string {
  return kind === 'dependency' ? 'dependency' : kind;
}
