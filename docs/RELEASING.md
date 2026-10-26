# Releasing PowerRoadmap

The canonical runbook for the PowerRoadmap web application and its GitHub Pages
site. Read it before changing a version, creating a tag, or diagnosing a
deployment. After a deployment, update [`DEPLOYMENTS.md`](DEPLOYMENTS.md) from
**observed** production state, never from what you expect to be true.

The format mirrors PowerScroll's, by owner directive — see
[`../DECISIONS.md`](../DECISIONS.md) D5.

## Release surfaces

| Change | GitHub app release | GitHub Pages |
|---|---:|---:|
| Application code in `src/` | Yes | Yes, from `main` |
| Single-file build config (`vite.export.config.ts`) | Yes | Yes |
| Landing site content in `site/` | No | Yes, from `main` |
| Root-only dev or build dependency | Usually no | Yes, after push |
| Documentation only | No | Only if the site text changes |
| Anything under `native/` | No | No — parked, DECISIONS.md D1 |

Do not release merely because a version-looking file changed. First work out
which deployed surface consumes it.

**Not surfaces yet.** PowerRoadmap publishes nothing to npm and nothing to the
MCP Registry. The `powerplanner-web` agent bridge is dev-only by construction
and is stripped from production and portable builds. If the bridge ever ships as
an installable package, this runbook gains those two surfaces and their
ordering rules — see PowerScroll's runbook for the shape.

## Invariants

- **One version source.** `src/version.ts` holds `APP_VERSION`; `package.json`
  mirrors it. `tests/unit/version.test.ts` fails on drift, and `release.yml`
  runs the unit suite before building, so a mismatched version cannot reach a
  published asset.
- **`dist-template/` is rebuilt and committed at every release tag.** The
  committed template is what the tag describes; a stale one makes the release
  unattributable. It was stale from 2026-08-10 to 2026-10-23 and this is why.
- **Never move a tag that published.** Correct forward: ship `+1`.
- **Never release from an open iteration.** Close it first — the release notes
  *are* the closed iterations.
- **Never release with an owner report awaiting its tick.** The owner ticks
  those boxes, not you.
- **Check `gh run list` before dispatching a workflow.** A tag pushed from the
  owner's credentials has already started the build; a second run races it.
- **Never publish bytes that have not been reviewed, tested and committed.**
  Never amend a release commit.
- An instruction to *prepare* a release is not authorization to push a tag.
  Ask.

## Preflight

1. Confirm the surface and the version with the owner.
2. Read `PLAN.md`, the relevant `spec/srs*` rows, `AGENTS.md` and this file.
   Close or explicitly carry every scoped item.
3. `git status --short` — preserve unrelated local changes.
4. Compare local state against [`DEPLOYMENTS.md`](DEPLOYMENTS.md) *and* the live
   services. Do not assume the ledger is current.
5. Review the diff and the dependency delta.

## Application release

1. Finish the iteration: implementation, SRS traceability, tests, and the
   required showcase artifact (`AGENTS.md`).
2. Set `APP_VERSION` in `src/version.ts`, then mirror it:

   ```bash
   npm version <version> --no-git-tag-version
   ```

3. Run the gates and record honest totals — failures, flakes and all:

   ```bash
   npm run lint
   npm test
   npm run test:e2e
   npm run build:template
   ```

4. Smoke-test **both** the dev app and the built template: the chart renders and
   the browser console is clean. Open `dist-template/PowerRoadmap.html` directly
   from the filesystem — the portable build has to work with no server, and that
   is a different code path from `npm run dev`.
5. Verify a round-trip on the built template: save a chart, reopen the saved
   file, confirm the content and the version survive. Persistence covers
   `.html`, `.json` and `.yaml`.
6. Commit the reviewed source together with `dist-template/`, the version
   metadata, `PLAN.md` closures, SRS updates and this ledger. Push only when
   the owner asks.
7. Create and push the annotated tag only when the owner asks.
   `.github/workflows/release.yml` builds and attaches `PowerRoadmap.html`.
8. Verify rather than trusting the workflow's exit text: the release is
   non-draft, `PowerRoadmap.html` is attached with a digest, the asset opens in
   a browser, and Pages completed from the expected commit.
9. Update [`DEPLOYMENTS.md`](DEPLOYMENTS.md) from what you observed.

## GitHub Pages

`deploy-site.yml` publishes `site/` on any push to `main` that touches it. The
site is a landing page; it does not currently host a live demo of the app.

PowerScroll seeds its Pages demo by injecting a workspace JSON into the
single-file template (`scripts/build-pages-demo.mjs` + `site/demo-workspace.json`).
Doing the same here — a Pages demo opening on a real sample chart — is the
obvious next step for this surface and is registered in the backlog.

## When a publish fails

Do not overwrite, retag, or guess. Record which surfaces succeeded, correct
forward with a new version where required, and resume from the first incomplete
surface. Record the new failure mode here and as a memory in
`docs/agents/memories/`.
