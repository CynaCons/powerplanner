/**
 * Where the bound file survives a reload (SRS-PERS-035).
 *
 * The binding used to be a module variable, so it lasted exactly as long as
 * the tab. Reopening a plan — the ordinary thing, since the plan IS an HTML
 * file you double-click — left the app with no idea which file it came from:
 * the first Ctrl+S opened a picker, and saving over the original took a
 * confirm dialog the planner had to read carefully.
 *
 * A handle is not JSON, so localStorage cannot hold one. IndexedDB stores it
 * structured-cloned and hands back a live handle after a reload, still
 * pointing at the same file on disk. Permission does not survive — that is
 * the browser's rule, not ours — so a restored handle starts in `prompt` and
 * the first write asks once.
 */

const DB_NAME = 'powerplanner-handles';
const DB_VERSION = 1;
const STORE = 'kv';
const CURRENT_KEY = 'current';

export interface StoredFileHandle {
  name: string;
  createWritable: () => Promise<{ write: (d: string) => Promise<void>; close: () => Promise<void> }>;
  getFile: () => Promise<File>;
  queryPermission?: (d: { mode: string }) => Promise<PermissionState>;
  requestPermission?: (d: { mode: string }) => Promise<PermissionState>;
  isSameEntry?: (other: unknown) => Promise<boolean>;
}

function openDb(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    if (typeof indexedDB === 'undefined') {
      reject(new Error('IndexedDB is not available'));
      return;
    }
    const req = indexedDB.open(DB_NAME, DB_VERSION);
    req.onupgradeneeded = () => {
      const db = req.result;
      if (!db.objectStoreNames.contains(STORE)) db.createObjectStore(STORE);
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

async function idbGet<T>(key: string): Promise<T | null> {
  try {
    const db = await openDb();
    return await new Promise<T | null>((resolve, reject) => {
      const tx = db.transaction(STORE, 'readonly');
      const req = tx.objectStore(STORE).get(key);
      req.onsuccess = () => resolve((req.result as T) ?? null);
      req.onerror = () => reject(req.error);
    });
  } catch {
    // A browser with storage switched off still has to save; it just forgets
    // the binding between sessions.
    return null;
  }
}

async function idbPut(key: string, value: unknown): Promise<void> {
  try {
    const db = await openDb();
    await new Promise<void>((resolve, reject) => {
      const tx = db.transaction(STORE, 'readwrite');
      tx.objectStore(STORE).put(value, key);
      tx.oncomplete = () => resolve();
      tx.onerror = () => reject(tx.error);
    });
  } catch {
    // ignored — see idbGet
  }
}

async function idbDelete(key: string): Promise<void> {
  try {
    const db = await openDb();
    await new Promise<void>((resolve, reject) => {
      const tx = db.transaction(STORE, 'readwrite');
      tx.objectStore(STORE).delete(key);
      tx.oncomplete = () => resolve();
      tx.onerror = () => reject(tx.error);
    });
  } catch {
    // ignored — see idbGet
  }
}

export async function rememberHandle(handle: StoredFileHandle): Promise<void> {
  await idbPut(CURRENT_KEY, handle);
}

export async function recallHandle(): Promise<StoredFileHandle | null> {
  return idbGet<StoredFileHandle>(CURRENT_KEY);
}

export async function forgetHandle(): Promise<void> {
  await idbDelete(CURRENT_KEY);
}

/**
 * May this document write that handle?
 *
 * Opened from disk, the HTML file IS the plan — and a copy of it carries the
 * same remembered binding, pointing at the original. Without this, opening
 * `plan (copy).html` and pressing Ctrl+S would write it over `plan.html`,
 * silently, with no picker and no undo. On a served page there is no such
 * ambiguity: the binding is the only file in play.
 */
export interface PageLocation {
  protocol: string;
  pathname: string;
}

/**
 * May the app write this file without being asked to (SRS-PERS-037)?
 *
 * Stated as a rule rather than buried in the writer, because all three of its
 * clauses are ways to lose a file quietly: no binding at all, a binding that
 * belongs to a different file, and a permission that has lapsed. `prompt` is
 * the ordinary state of a handle recalled from a previous session, and the
 * answer there is no — raising the dialog needs a gesture a timer does not
 * have, so asking would be refused with nothing shown.
 */
export function mayAutosave(
  handle: { name: string } | null,
  permission: PermissionState | 'unknown',
  at?: PageLocation,
): boolean {
  if (!handle) return false;
  if (!isHandleForThisDocument(handle, at)) return false;
  return permission === 'granted';
}

export function isHandleForThisDocument(
  handle: { name: string },
  // Taken as an argument so the rule can be stated and tested without a
  // navigation: jsdom will not let `location.protocol` be redefined.
  at: PageLocation = typeof window === 'undefined'
    ? { protocol: 'http:', pathname: '/' }
    : window.location,
): boolean {
  if (at.protocol !== 'file:') return true;
  const last = at.pathname.split('/').pop() ?? '';
  let own = last;
  try {
    own = decodeURIComponent(last);
  } catch {
    // keep the raw segment
  }
  return handle.name === own;
}
