import type { GanttDocument } from '../types/document';
import { injectDocumentInto, buildEmbeddedHtml } from '../persistence/embedded';
import {
  downloadFile,
  getCurrentHandle,
  verifyWritePermission,
  writeHtmlToHandle,
  type FileSystemFileHandleLike,
} from '../persistence/fileIo';

/**
 * In-app update, ported from PowerScroll (PLAN.md v2.17.0).
 *
 * The shape is PowerScroll's one-to-one, including the orderings that look
 * arbitrary and are not — each is a scar. See DECISIONS.md D9.
 */

export const GITHUB_REPO = 'CynaCons/powerplanner';
export const RELEASE_ASSET_NAME = 'PowerPlanner.html';

/**
 * How long a check is trusted before asking GitHub again.
 *
 * api.github.com allows 60 unauthenticated requests an hour PER IP, and the
 * check fires on page load. Without a cache, opening a handful of plans — or a
 * test suite that loads the app hundreds of times — exhausts the quota for
 * everything else on that IP and the user sees 403.
 */
const CHECK_CACHE_KEY = 'powerplanner-update-check';
const CHECK_CACHE_MS = 6 * 60 * 60 * 1000;

export interface UpdateInfo {
  available: boolean;
  latestVersion?: string;
  downloadUrl?: string;
  releaseUrl?: string;
  /** Release tag, e.g. "v2.16.0". The download is pinned to it — see fetchBuild. */
  tag?: string;
}

export type UpdateMode = 'live-swap' | 'download';
export type PerformUpdateResult = { ok: true; mode: UpdateMode } | { ok: false };

/** Compare dotted numeric versions. Returns >0 when `a` is newer than `b`. */
export function compareVersions(a: string, b: string): number {
  const parts = (v: string) => v.split('.').map((p) => Number.parseInt(p, 10) || 0);
  const av = parts(a);
  const bv = parts(b);
  for (let i = 0; i < Math.max(av.length, bv.length); i++) {
    const diff = (av[i] ?? 0) - (bv[i] ?? 0);
    if (diff !== 0) return diff;
  }
  return 0;
}

function readCachedCheck(currentVersion: string): UpdateInfo | null {
  try {
    const raw = localStorage.getItem(CHECK_CACHE_KEY);
    if (!raw) return null;
    const entry = JSON.parse(raw);
    if (entry.forVersion !== currentVersion) return null;
    if (Date.now() - entry.at > CHECK_CACHE_MS) return null;
    return entry.info as UpdateInfo;
  } catch {
    return null;
  }
}

function writeCachedCheck(currentVersion: string, info: UpdateInfo): void {
  try {
    localStorage.setItem(
      CHECK_CACHE_KEY,
      JSON.stringify({ at: Date.now(), forVersion: currentVersion, info }),
    );
  } catch {
    // A full or blocked localStorage costs the cache, not the check.
  }
}

/**
 * Automation loads the app over and over, and each load would spend one of the
 * hour's 60 requests. `navigator.webdriver` is set by every driver, so this
 * needs no cooperation from the tests.
 */
function isAutomated(): boolean {
  return typeof navigator !== 'undefined' && navigator.webdriver === true;
}

/** The version on main, from a host that is not API-rate-limited. */
async function latestVersionFromRaw(): Promise<string | null> {
  try {
    const resp = await fetch(
      `https://raw.githubusercontent.com/${GITHUB_REPO}/main/package.json`,
      { cache: 'no-cache' },
    );
    if (!resp.ok) return null;
    const pkg = await resp.json();
    return typeof pkg.version === 'string' ? pkg.version : null;
  } catch {
    return null;
  }
}

/** Check GitHub for a newer release. Returns null when the check itself fails. */
export async function checkForUpdate(
  currentVersion: string,
  options: { force?: boolean } = {},
): Promise<UpdateInfo | null> {
  if (isAutomated() && !options.force) return null;

  if (!options.force) {
    const cached = readCachedCheck(currentVersion);
    if (cached) return cached;
  }

  try {
    const resp = await fetch(`https://api.github.com/repos/${GITHUB_REPO}/releases/latest`, {
      headers: { Accept: 'application/vnd.github.v3+json' },
    });

    if (!resp.ok) {
      if (resp.status === 403) {
        // Rate limited. raw.githubusercontent.com has no API quota, and the
        // release flow bumps the version and tags together, so main's version
        // is the newest release's version.
        const raw = await latestVersionFromRaw();
        if (raw) {
          const info: UpdateInfo =
            compareVersions(raw, currentVersion) > 0
              ? { available: true, latestVersion: raw, tag: `v${raw}` }
              : { available: false, latestVersion: raw };
          writeCachedCheck(currentVersion, info);
          return info;
        }
      }
      return null;
    }

    const data = await resp.json();
    const latest = (data.tag_name || '').replace(/^v/, '');
    if (!latest) return null;

    // Only a strictly NEWER release counts. Plain inequality would treat a
    // local build running ahead of the published tag as an available update
    // and offer to install an older one over it.
    if (compareVersions(latest, currentVersion) <= 0) {
      const upToDate: UpdateInfo = { available: false, latestVersion: latest };
      writeCachedCheck(currentVersion, upToDate);
      return upToDate;
    }

    const asset = data.assets?.find((a: { name?: string }) => a.name === RELEASE_ASSET_NAME);
    const info: UpdateInfo = {
      available: true,
      latestVersion: latest,
      downloadUrl: asset?.browser_download_url,
      releaseUrl: data.html_url,
      tag: data.tag_name,
    };
    writeCachedCheck(currentVersion, info);
    return info;
  } catch {
    return null;
  }
}

/**
 * Download the new build.
 *
 * The order is not the obvious one. GitHub sends NO CORS header on
 * release-asset downloads: `browser_download_url` and the API's octet-stream
 * endpoint both 302 to objects.githubusercontent.com, and neither response
 * carries `Access-Control-Allow-Origin`. From a page — which is all this app
 * ever is — both fail with a bare "TypeError: Failed to fetch".
 *
 * raw.githubusercontent.com does send it, and the built single file is
 * committed at dist-template/index.html, so fetching it AT THE RELEASE TAG is
 * the route that works. Pinning to the tag rather than main matters: main can
 * be ahead of the newest release, and an update must install the version it
 * just told the user about.
 */
export async function fetchBuild(downloadUrl?: string, tag?: string): Promise<string | null> {
  const looksRight = (text: string) => text.includes('<div id="root">');

  if (tag) {
    try {
      const resp = await fetch(
        `https://raw.githubusercontent.com/${GITHUB_REPO}/${tag}/dist-template/index.html`,
      );
      if (resp.ok) {
        const text = await resp.text();
        if (looksRight(text)) return text;
      }
    } catch {
      // Fall through.
    }
  }

  // The release asset. Expected to fail on CORS; kept in case that changes.
  if (downloadUrl) {
    try {
      const resp = await fetch(downloadUrl);
      if (resp.ok) {
        const text = await resp.text();
        if (looksRight(text)) return text;
      }
    } catch {
      // Expected.
    }
  }

  // main, last resort: it can be ahead of the newest release, so this may
  // install something that was never released.
  try {
    const resp = await fetch(
      `https://raw.githubusercontent.com/${GITHUB_REPO}/main/dist-template/index.html`,
    );
    if (resp.ok) {
      const text = await resp.text();
      if (looksRight(text)) return text;
    }
  } catch {
    // Give up below.
  }

  return null;
}

export interface PerformUpdateDeps {
  fetchTemplate?: (downloadUrl?: string, tag?: string) => Promise<string | null>;
  getHandle?: () => FileSystemFileHandleLike | null;
  writeHandle?: (handle: FileSystemFileHandleLike, html: string) => Promise<boolean>;
  verifyWrite?: (handle: FileSystemFileHandleLike) => Promise<boolean>;
  reload?: () => void;
  download?: (name: string, content: string, mime?: string) => void;
  buildBackupHtml?: (doc: GanttDocument) => string;
}

/**
 * Update in place when the browser allows it, otherwise download.
 *
 *   1. Settle the write target BEFORE fetching anything.
 *   2. Fetch the new build and inject the current plan into it.
 *   3. Write to the open file and reload, or download the new file.
 */
export async function performUpdate(
  doc: GanttDocument,
  currentVersion: string,
  newVersion: string,
  tag?: string,
  downloadUrl?: string,
  deps: PerformUpdateDeps = {},
): Promise<PerformUpdateResult> {
  const fetchTemplate = deps.fetchTemplate ?? fetchBuild;
  const handleOf = deps.getHandle ?? getCurrentHandle;
  const write = deps.writeHandle ?? writeHtmlToHandle;
  const verify = deps.verifyWrite ?? verifyWritePermission;
  const reload = deps.reload ?? (() => window.location.reload());
  const download = deps.download ?? downloadFile;
  const buildBackup = deps.buildBackupHtml ?? buildEmbeddedHtml;

  try {
    const safeName = (doc.title || 'plan').replace(/[^a-zA-Z0-9_\- ]/g, '_');

    // Settle the write target FIRST. `verify` can call requestPermission(),
    // which needs the transient activation from the click that started this.
    // The build is hundreds of KB and takes seconds, so asking afterwards means
    // the activation has expired and the request is denied with no prompt —
    // the update then silently downgrades to "download a copy", which reads as
    // the updater refusing to update the file you already have open.
    let target: FileSystemFileHandleLike | null = null;
    const handle = handleOf();
    if (handle && (await verify(handle))) target = handle;

    const newHtml = await fetchTemplate(downloadUrl, tag ?? `v${newVersion}`);
    if (!newHtml) return { ok: false };

    const finalHtml = injectDocumentInto(newHtml, doc);

    if (target) {
      // A safety copy before overwriting the file in place.
      download(`${safeName} (v${currentVersion} backup).html`, buildBackup(doc), 'text/html');
      if (await write(target, finalHtml)) {
        reload();
        return { ok: true, mode: 'live-swap' };
      }
      // Fall through to the download path if the write failed.
    }

    // No backup here, deliberately: nothing has been overwritten on this path,
    // so the file on disk IS the backup. Downloading one anyway was worse than
    // redundant in PowerScroll — browsers block a second programmatic download
    // from one gesture, so the backup went through and the actual new file was
    // silently dropped, which read as "update does nothing".
    download(`${safeName} (v${newVersion}).html`, finalHtml, 'text/html');
    return { ok: true, mode: 'download' };
  } catch {
    return { ok: false };
  }
}
