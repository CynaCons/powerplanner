# Deployment ledger

**Observed** state of every PowerPlanner deployment surface. Update it from what
you actually saw — a query, a fetch, a page you opened — never from what a
workflow's exit text implied or from what you expect to be true. The runbook is
[`RELEASING.md`](RELEASING.md).

Repository: `CynaCons/powerplanner`

## Current state — observed 2026-10-23

| Surface | State | Observed |
|---|---|---|
| GitHub release (latest) | `v2.11.0` — "Web Agent Bridge (MCP)", non-draft, published 2026-08-07 | `gh release view v2.11.0` |
| Release assets | **none** — zero assets on every release | `gh release view v2.11.0 --json assets` → `[]` |
| GitHub Pages | `status: built`, from `main` `/`, HTTPS enforced | `gh api repos/CynaCons/powerplanner/pages` |
| Pages URL | https://cynacons.github.io/powerplanner/ | same |
| Pages content | landing site from `site/`; no live app demo | `deploy-site.yml` |
| `src/version.ts` | `2.11.0` | repo |
| `package.json` | `2.11.0` — matches | repo |
| `dist-template/PowerPlanner.html` | built 2026-08-10, **stale** vs `main` | filesystem |
| npm | not a surface — nothing published | DECISIONS.md D5 |
| MCP Registry | not a surface — nothing published | DECISIONS.md D5 |

## What this ledger made visible

**No release has ever carried a downloadable artifact.** Every tag from v1.0.0
to v2.11.0 has zero assets, so the single-file HTML the product *is* has never
been published. `release.yml` (added 2026-10-23) is the fix; it has not run yet,
because it only triggers on a new tag.

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
