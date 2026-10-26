# Decisions

Choices between alternatives with lasting consequences. Each records the
context, the decision, and what would reopen it. Decisions are corrected in
place; a reversal is a new entry that supersedes the old one.

---

## D1 — The web app is the product; the native PowerPoint add-in is parked

**Date:** 2026-10-23 · **Owner directive**

**Context.** PowerRoadmap was built as two implementations of one spec: a React
web app and a think-cell-style on-slide C++ COM add-in for PowerPoint. Roughly
two thirds of the plan's history is native work. The owner's company does not
use PowerPoint, so the owner no longer uses it either: *"the PowerPoint version
now becomes second prio... we really need that HTML web interface instead."*

**Decision.** The web app is the product. It ships as a single-file HTML
application in PowerScroll's release format. The native add-in and the Office.js
task pane are parked: present in the tree, excluded from scheduling. Native
iterations keep their open boxes in `PLAN.md` under the parked major `v2.14`
rather than being flattened into the backlog, because their task text carries
root-cause analysis a one-line backlog entry would destroy.

**Alternatives rejected.** Deleting `native/` (throws away a working add-in and
its audited SRS for a reversible priority change). Keeping native iterations
open at lower priority (they keep reporting as schedulable — that is the drift
that put two iterations in ACTIVE simultaneously).

**What would reopen it.** The owner needing on-slide editing again, or a user
base that does.

---

## D2 — The SRS stays in `spec/`, not `docs/srs/`

**Date:** 2026-10-23

**Context.** PowerFlow canon is `docs/srs/SRS-<feature>.md` with
`docs/srs/README.md` as the tag index. This project has `spec/srs/`,
`spec/srs-web/` and `spec/srs-native/`, already using the canonical
`SRS-<FEAT>-NNN` ID scheme with the canonical five columns, and already through
a two-round independent ASPICE audit. `spec/` also holds the layer the SRS sits
above: `data-model.md`, `layout.md`, `interaction.md`, the JSON Schema, and the
conformance fixtures both implementations trace to.

**Decision.** Keep `spec/`. PowerFlow §10 explicitly leaves a live ID scheme to
the project as a decision rather than a lint fix, and only the directory differs
from canon. `audit.py` will keep reporting the location and scheme findings;
that is expected and is not drift.

**What would reopen it.** Tooling that assumes `docs/srs/` by path, or a second
project in this family adopting canon so that consistency across repos starts
to pay more than the migration costs.

---

## D3 — The showcase artifact is a required close-the-loop gate

**Date:** 2026-10-23 · **Owner directive**

**Context.** The owner asked for PowerScroll's design demos by name: *"I really
like the design demos that we do in PowerScroll. I think we should include them
here."* PowerFlow has the hook already (`showcase: required`, D8); PowerScroll
has the detailed recipe. This project had the capture machinery
(`scripts/shot.mjs`, `record-web.mjs`, `docs/media/gallery-*.png`) and an
interactive design mockup, but neither was a gate.

**Decision.** Two artifacts, both required. A **design demo** — an interactive
prototype — before a UI direction is chosen, so the owner picks from something
real. A **showcase artifact** after the work, built from element-clipped 2x
screenshots of the running app, with the defect stated plainly and a
verification section carrying the real numbers including failures. Mocked-up
screenshots are not evidence. Rules in `AGENTS.md`.

**What would reopen it.** Nothing short of the owner withdrawing it; this is the
project's definition of delivery.

---

## D4 — OPEN: does the web app still need to put charts into PowerPoint decks?

**Date parked:** 2026-10-23 · **Status: open, parked by the owner**

**Context.** D1 removes the owner's own PowerPoint usage, but colleagues still
receive decks, and the product's one-line promise is "calendar-true Gantt charts
you can actually present". If charts must reach other people's slides, the web
app needs an image or `.pptx` export path — a different thing from the parked
on-slide add-in. The owner chose to park the question rather than answer it
before it bites.

**Decision.** Deliberately deferred. Do not build export speculatively, and do
not assume HTML-only either. Raise it when a real need appears.

**What would decide it.** The owner needing to hand a chart to someone who works
in PowerPoint.

---

## D5 — Release format mirrors PowerScroll

**Date:** 2026-10-23 · **Owner directive** (*"same format as PowerScroll,
basically, same release and so on"*)

**Context.** PowerScroll publishes a single-file HTML app as a GitHub release
asset, serves a live demo from GitHub Pages, keeps one version source in
`src/version.ts`, and carries a runbook (`docs/RELEASING.md`) plus an
observed-state ledger (`docs/DEPLOYMENTS.md`). PowerRoadmap had
`dist-template/PowerRoadmap.html` and a Pages site but no version source, no
release workflow, no runbook and no ledger — so the v2.12 and v2.13 web work sat
unreleased behind tag v2.11.0 with a stale template.

**Decision.** Adopt the format: `src/version.ts` as the single version source
with `package.json` mirroring it; `.github/workflows/release.yml` building and
attaching `PowerRoadmap.html` on a tag; `docs/RELEASING.md` as the runbook;
`docs/DEPLOYMENTS.md` as the ledger, updated from observed production state
rather than inferred. Not adopted: the npm and MCP Registry surfaces, which
PowerScroll needs for `powerscroll-mcp` and PowerRoadmap does not yet have an
equivalent of.

**What would reopen it.** Shipping the agent bridge as an installable package,
which would add the npm and Registry surfaces to the runbook.

---

## D6 — One hue per discipline, not colour reserved for risk

**Date:** 2026-10-23 · **Owner choice between two built alternatives**

**Context.** Two colour strategies were drawn against the same 34-row AUTOSAR
plan: a hue per workstream, and a graphite field with colour reserved for the
two regulated streams (ISO 26262, ISO 21434).

**Decision.** A hue per discipline. The stream palette lives in
`global.css` as `--stream-1..8` and in `sample.ts`; the two must stay in step.

**Known cost, accepted.** Eight hues cannot all separate in greyscale —
prussian, plum and rust sit close in lightness — so a mono print or a poor
projector will merge some streams. The risk palette did not have this problem.
If printing becomes a real use, revisit.

**What would reopen it.** Printing in mono mattering, or a plan whose stream
count grows past what eight hues can carry.

---

## D7 — Inserting does not live in the header

**Date:** 2026-10-23 · **Delegated to Claude by the owner**

**Context.** After C2 moved selection editing into the command bar, the header
still carried Task / Milestone / Bracket / Deadline / Row, duplicating the
bar's INSERT group. The question was whether always-available insert is worth
the duplication, given the bar shows INSERT only when nothing is selected.

**Decision.** Remove them. Three facts decided it:

1. The header's buttons were the **worst** of the insert paths: they dropped
   every new item on `rows[0]` at today's date, leaving the user to drag it
   into place. The chart's right-click menu inserts at the row and date you
   actually clicked.
2. Insert remains reachable three ways — the bar's INSERT group (visible
   exactly when nothing is selected, which is the state you are in when
   adding), the right-click menu, and `T` / `Y` / `B`.
3. On first load nothing is selected, so a new user sees INSERT in the bar
   immediately. Discoverability does not depend on the header.

**One capability genuinely moved rather than being deleted:** bracket-from-
selection. It cannot live in an idle-only INSERT group, because it needs a
selection to span, so it is now a button in the selection group and still on
`B`.

**The cost, accepted.** Adding something while an item is selected needs an
Escape first, or the right-click path. This is the same trade the owner
already accepted for tools in C1c.

**What would reopen it.** Evidence that people hunt the header for insert and
do not find the bar.

---

## D8 — The command bar floats, and the header stays

**Date:** 2026-10-23 · Closes the open questions in PLAN.md v2.13.4 (C4)

**C4a — floating bar or its own grid row?** Floating, as the owner chose from
the interactive prototype in C1e. The plan's original proposal was a grid row;
the prototype changed the owner's mind, and nothing since has argued against
it. The known cost is that the bar occludes a band of chart, which is mild
because rows lay out from the top.

**C4b — does the header still earn its height now insert has left it?** Yes.
What remains is not duplicated anywhere: the brand, the document title, snap,
undo/redo, shortcuts, the agent toggle and the File menu. Undo/redo in
particular wants a fixed, always-visible home rather than a bar whose contents
change with the selection.

**What would reopen it.** The header losing another cluster, at which point
what is left may be better merged into the bar.

---

## D9 — The built template is committed, because updating depends on it

**Date:** 2026-10-26 · **Owner directive** (*"exactly like PowerScroll"*)

**Context.** PowerRoadmap ignored `dist-template/` entirely. PowerScroll
commits it, with a one-line comment in its `.gitignore` explaining why, and
that comment turns out to be load-bearing.

GitHub does **not** send `Access-Control-Allow-Origin` on release-asset
downloads. Both `browser_download_url` and the API's octet-stream asset
endpoint 302 to `objects.githubusercontent.com`, and neither response carries
the header. From a page — which is all a single-file app ever is — both fail
with a bare `TypeError: Failed to fetch`. `raw.githubusercontent.com` *does*
send it. So the only route an in-app updater can actually use is the built
file committed in the repository, fetched at the release tag.

**Decision.** `dist-template/index.html` is tracked and rebuilt at every
release tag. `PowerRoadmap.html` stays ignored: it is a byte-identical copy the
release workflow makes from `index.html`, and committing both would double the
weight for nothing.

**Pinned to the tag, never to main.** An update must install the version it
just told the user about, and `main` can be ahead of the newest release.

**The trap this leaves.** The updater ships *inside* each saved file, so a
build released with a broken download route cannot repair itself — users on it
must fetch the new build by hand once. That is why this route is settled before
the first release rather than after.

**What would reopen it.** GitHub serving CORS headers on release assets.
