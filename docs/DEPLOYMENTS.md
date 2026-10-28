# Deployment ledger

**Observed** state of every PowerRoadmap deployment surface. Update it from what
you actually saw — a query, a fetch, a page you opened — never from what a
workflow's exit text implied or from what you expect to be true. The runbook is
[`RELEASING.md`](RELEASING.md).

Repository: `CynaCons/powerroadmap` — renamed from `CynaCons/powerplanner` on
2026-10-28. Observations recorded before that date name the old repository and
are left as they were observed; the old name still resolves.

## Current state — observed 2026-10-26

| Surface | State | Observed |
|---|---|---|
| GitHub release (latest) | `v2.19.0`, non-draft, published 2026-10-26 | `gh release view v2.19.0` |
| Release assets | `PowerRoadmap.html`, `PowerGantt.html`, `PowerPlanner.html` — byte-identical, 367,548 bytes each | `gh release view --json assets` |
| GitHub Pages | `status: built`, from `main` `/`, HTTPS enforced | `gh api repos/CynaCons/powerplanner/pages` |
| Pages URL | https://cynacons.github.io/powerplanner/ | same |
| Pages content | landing site from `site/`; no live app demo | `deploy-site.yml` |
| `src/version.ts` | `2.19.0` | repo |
| `package.json` | `2.19.0` — matches | repo |
| `dist-template/index.html` | tracked, rebuilt at the tag, 355,051 bytes | repo |
| npm | not a surface — nothing published | DECISIONS.md D5 |
| MCP Registry | not a surface — nothing published | DECISIONS.md D5 |

## Asset names, and why all three ship

The app has been renamed twice in a day. The updater lives inside each saved
file and only knows the asset name that existed when it shipped, so every
release carries a byte-identical copy under all three names. Dropping one
strands every build still using it, and those builds cannot be repaired
remotely.

| A build on | looks for | v2.19.0 ships it |
|---|---|---|
| 2.17.x | `PowerPlanner.html` | yes |
| 2.18.x | `PowerGantt.html` | yes |
| 2.19.0+ | `PowerRoadmap.html` | yes |

Verified against the live API on 2026-10-26, not inferred.

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
| 2026-08-07 | GitHub release | v2.11.0 | Web agent bridge. No assets attached. |
| 2026-05-27 | GitHub release | v2.3.0 | No assets attached. |
| 2026-05-27 | GitHub release | v2.0.0 | No assets attached. |
| 2026-05-27 | GitHub release | v1.0.1 | No assets attached. |
| 2026-05-27 | GitHub release | v1.0.0 | No assets attached. |
