# Deployment ledger

**Observed** state of every PowerRoadmap deployment surface. Update it from what
you actually saw — a query, a fetch, a page you opened — never from what a
workflow's exit text implied or from what you expect to be true. The runbook is
[`RELEASING.md`](RELEASING.md).

Repository: `CynaCons/powerplanner`

## Current state — observed 2026-10-26

| Surface | State | Observed |
|---|---|---|
| GitHub release (latest) | `v2.17.0`, non-draft, published 2026-10-26 | `gh release view v2.17.0` |
| Release assets | `PowerRoadmap.html` — 363,694 bytes on v2.17.0, 355,051 on v2.16.0 | `gh release view --json assets` |
| GitHub Pages | `status: built`, from `main` `/`, HTTPS enforced | `gh api repos/CynaCons/powerplanner/pages` |
| Pages URL | https://cynacons.github.io/powerplanner/ | same |
| Pages content | landing site from `site/`; no live app demo | `deploy-site.yml` |
| `src/version.ts` | `2.17.0` | repo |
| `package.json` | `2.17.0` — matches | repo |
| `dist-template/index.html` | tracked, rebuilt at the tag, 355,051 bytes | repo |
| npm | not a surface — nothing published | DECISIONS.md D5 |
| MCP Registry | not a surface — nothing published | DECISIONS.md D5 |

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
