# Deployment ledger

**Observed** state of every PowerRoadmap deployment surface. Update it from what
you actually saw — a query, a fetch, a page you opened — never from what a
workflow's exit text implied or from what you expect to be true. The runbook is
[`RELEASING.md`](RELEASING.md).

Repository: `CynaCons/powerroadmap` — renamed from `CynaCons/powerplanner` on
2026-10-28. Observations recorded before that date name the old repository and
are left as they were observed; the old name still resolves.

## Current state — observed 2026-10-29

| Surface | State | Observed |
|---|---|---|
| GitHub release (latest) | `v2.28.0`, non-draft, not prerelease, published 2026-10-29T17:15:20Z from `main` | `gh release view v2.28.0 --json` |
| Release assets | `PowerRoadmap.html` alone — 403,919 bytes, `state=uploaded`. One file, not three; see below. | `gh release view --json assets` |
| Asset integrity | downloaded and hashed: `873682e964bdeb81c42e14ca83ca8712be68eaa218103c905ac89f8cf1c70fe6`, identical to the raw update route and to the committed `dist-template/index.html` | `gh release download` + `sha256sum` |
| Published app runs | the downloaded asset was served and opened: 39 rows, 99 bars, status bar `v2.28.0`, zero console errors | browser |
| Build & Release workflow | `completed/success` for tag `v2.28.0`, 53s | `gh run list` |
| Web CI | `completed/success` on `main`, 50s | same |
| GitHub Pages | HTTP 200 at https://cynacons.github.io/powerroadmap/ | `curl` |
| Pages content | landing site from `site/`; no live app demo | `deploy-site.yml` |
| `src/version.ts` | `2.28.0` | repo |
| `package.json` | `2.28.0` — matches | repo |
| `dist-template/index.html` | tracked; the CI rebuild at the tag reproduced the committed bytes exactly, so the blob digest and the on-disk digest agree and `git status` is clean | repo |
| npm | not a surface — nothing published | DECISIONS.md D5 |
| MCP Registry | not a surface — nothing published | DECISIONS.md D5 |

### The update routes, re-measured at v2.28.0

All four fetched 2026-10-29 after the release, not inferred:

| Route | Observed |
|---|---|
| `raw…/CynaCons/powerroadmap/main/package.json` | `2.28.0` |
| `api.github.com/repos/CynaCons/powerroadmap/releases/latest` | `v2.28.0` |
| `api.github.com/repos/CynaCons/powerplanner/releases/latest` — the **old** name, which every build before v2.20.0 asks for | `v2.28.0`, through the rename redirect |
| `raw…/CynaCons/powerroadmap/v2.28.0/dist-template/index.html` | 403,919 bytes, digest `873682e9…` — identical to the published asset |

The third row is the one that matters: builds in the wild compiled the old
repository name in and cannot be corrected remotely. They still find this
release.

### A digest trap on Windows

`core.autocrlf=true` and no `.gitattributes`, so `dist-template/index.html` is
*checked out* with CRLF: at v2.20.0, 378,167 bytes on disk against a
377,854-byte blob. The blob is what the Linux runner checks out and publishes,
so hashing the local working copy gives a different digest and looks like a
failed release. Hash `git cat-file blob HEAD:dist-template/index.html`, not the
file on disk.

It depends on how the file got there, which is why it does not bite every time.
A file written by `npm run build:template` is LF and matches the blob — at
v2.21.0 both were 388,308 bytes and `git status` stayed clean after the rebuild.
A file restored by `git checkout --` is CRLF and does not. So the trap springs
after a checkout, not after a build. A `.gitattributes` pinning the template to
LF would remove the distinction; it is in the backlog.

## Asset names — one file, from v2.22.0

Releases up to and including v2.21.0 carried the single-file app three times,
as `PowerRoadmap.html`, `PowerGantt.html` and `PowerPlanner.html`, on the
reasoning that an installed build looks for the asset name it shipped with and
would be stranded without its own.

That reasoning was wrong, and checking it is what retired the aliases. The
updater does not read the release asset at all: `fetchBuild()` in
`src/utils/updateChecker.ts` fetches
`raw.githubusercontent.com/<repo>/<tag>/dist-template/index.html` and falls back
to the asset only in case the CORS situation ever changes — which it has not,
because release-asset downloads carry no `access-control-allow-origin` header
and a page therefore cannot read one. Every build back to v2.17.0 takes the raw
route. Dropping the aliases strands nothing.

Owner report 2026-10-29, on opening the v2.21.0 release page: three HTML files
that are the same file, and no way to tell which to download. From v2.22.0 the
release publishes `PowerRoadmap.html` only.

`RELEASE_ASSET_NAMES` still lists all three names, newest first, and should
stay that way: the matcher runs in an old build reading a *new* release, and
has to recognise at least one name it knows.

What cannot be dropped is the **old repository name**. `api.github.com/repos/
CynaCons/powerplanner/releases/latest` is still what every build before v2.20.0
asks for, and it resolves only because GitHub redirects a renamed repository.
That redirect is the single point of failure for those builds; re-measure it at
every release. At v2.28.0 it returned `v2.28.0`.

GitHub also attaches a source zip and tarball to every release automatically.
They cannot be removed, and they are not ours.

## The rename — done 2026-10-28

`CynaCons/powerplanner` → `CynaCons/powerroadmap`, on the owner's approval. It
had been held back because the updater ships inside every saved file and cannot
be fixed retroactively, so a rename that broke either route would strand every
build in the wild.

The prediction, measured 2026-10-27 against `facebook/jest` as a proxy, and the
outcome, measured against this repository within minutes of renaming it:

| Route, asked under the OLD name | Predicted | Observed 2026-10-28 |
|---|---|---|
| `api.github.com/repos/<old>/releases/latest` | 301 with CORS on the redirect | **301** to `/repositories/1250571066/releases/latest`, `access-control-allow-origin: *` on the 301 **and** on the followed 200 |
| `raw.githubusercontent.com/<old>/main/package.json` | 200, no redirect | **200**, `access-control-allow-origin: *`, no redirect |
| `github.com/<old>/releases/latest/download/PowerRoadmap.html` | not measured | **200** after redirects, resolves to `release-assets.githubusercontent.com` |

So every build in the wild keeps updating through the old name, and the site's
download buttons keep working from old links. `GITHUB_REPO` in
`src/utils/updateChecker.ts` moved to the new name for builds from v2.20.0 on.

### What the rename did break: the old Pages URL

| URL | Observed 2026-10-28 |
|---|---|
| https://cynacons.github.io/powerplanner/ | **404**, with no `Location` — GitHub issues no Pages redirect for a renamed project site |
| https://cynacons.github.io/powerroadmap/ | 200 |

Anyone holding a link to the old landing page gets a 404. Nothing in the app
depends on it — the updater never touches Pages — so no build is stranded, but
the link is dead and only a placeholder repository at the old name, serving a
redirect page, would revive it. Not done; it is a new public surface and the
owner's call.

## Which releases can update themselves

| Release | Carries the updater | Note |
|---|---|---|
| v2.17.0 | **yes** | first build that can update itself |
| v2.16.0 | no | tagged at `b900c6f`, before the updater landed in `842ee92` |
| v2.11.0 and earlier | no | no assets at all |

Verified against the published build, not the repo: fetching
`v2.17.0/dist-template/index.html` returns 363,694 bytes containing
`Check for updates`, `raw.githubusercontent` and `powerplanner-update-check`;
the same fetch at `v2.16.0` contains none of them.

A build without an updater has to be replaced by hand — the updater ships
inside each saved file, so it cannot be delivered retroactively.

### Observed: a real v2.19.0 build finding v2.20.0 across the rename

The point of the gate (PLAN.md v2.17.1) was to see an installed build update
itself rather than argue that it could. Done 2026-10-28, and made harder on
purpose by running it *after* the rename.

`PowerRoadmap.html` was downloaded from the published **v2.19.0** release — a
build with `GITHUB_REPO = 'CynaCons/powerplanner'`, the OLD name, compiled in
and unchangeable. Served over `http://localhost:5181` so the page had a real
origin, with the 6-hour check cache cleared first. Pressing **Check for
updates** produced:

```json
{"available":true,"latestVersion":"2.20.0",
 "downloadUrl":"https://github.com/CynaCons/powerroadmap/releases/download/v2.20.0/PowerRoadmap.html",
 "releaseUrl":"https://github.com/CynaCons/powerroadmap/releases/tag/v2.20.0",
 "tag":"v2.20.0"}
```

and the dialog offered "Update to 2.20.0". Note what the old build resolved:
it asked under the old name and GitHub handed back URLs carrying the **new**
one, so it follows the rename without knowing the name changed.

The fetch the update would then perform was run in that same page, against the
old name, and returned 200 with `<div id="root">` present — `fetchBuild`'s own
acceptance guard — carrying `2.20.0`.

**Not exercised:** the final write. Served over http there is no file handle,
so `performUpdate` takes the download branch, and that was left for the owner
to click. Everything up to the write is observed; the write itself is browser
plumbing and independent of the rename.

## Update route — the one the in-app updater uses

Verified live on 2026-10-26, not inferred:

```
curl -I https://raw.githubusercontent.com/CynaCons/powerplanner/v2.16.0/dist-template/index.html
  → HTTP 200, 355,051 bytes
  → access-control-allow-origin: *
```

That header is why this route exists. Release-asset downloads do not carry it,
so a page cannot fetch them (DECISIONS.md D9). If this route ever stops
serving, updating breaks for every build already in the wild.

## What this ledger made visible

**Fixed 2026-10-26.** Every tag from v1.0.0 to v2.11.0 had zero assets, so the
single-file HTML the product *is* had never been published. `v2.16.0` is the
first release to carry one.

Its first run failed, on its own tag: the SRS-ABR-050 portable-build gate read
`dist-template/PowerRoadmap.html`, which the same change had made an untracked
artifact, and `npm test` ran before `npm run build:template`. Nothing had
published — no release object, no assets — so the tag was recut rather than
burning a version. The gate now reads the committed `index.html` and the
workflow builds before testing.

**The published app is three iterations behind the code.** Tag `v2.11.0` predates
the v2.12 legibility work and the v2.13 command-bar work (C0 and C1, committed
2026-08-07 and 2026-08-10). `dist-template/` was last built 2026-08-10 and does
not include anything since.

**CI history is misleading.** The only workflow that ran on code pushes was
`native-pure`, and it failed on all five of its last runs — `ilammy/msvc-dev-cmd`
cannot find vcvars on the current `windows-latest` image. The web app had no CI
at all. As of 2026-10-23 `native-pure` is manual-dispatch only while native is
parked, and `web-ci.yml` covers lint, unit, build and the single-file build.

## History

| Date | Surface | Version | Note |
|---|---|---|---|
| 2026-10-29 | GitHub release | v2.28.0 | Six iterations from the owner's live walkthrough. First release to publish one asset instead of three. e2e not run before the tag, by the owner's instruction. |
| 2026-10-28 | GitHub release | v2.21.0 | Last release to carry the three byte-identical aliases. |
| 2026-08-07 | GitHub release | v2.11.0 | Web agent bridge. No assets attached. |
| 2026-05-27 | GitHub release | v2.3.0 | No assets attached. |
| 2026-05-27 | GitHub release | v2.0.0 | No assets attached. |
| 2026-05-27 | GitHub release | v1.0.1 | No assets attached. |
| 2026-05-27 | GitHub release | v1.0.0 | No assets attached. |
