# Web Agent Bridge (MCP) — Design Plan

**Scope: the web app only.** Nothing in this document touches `native/`, the
PowerPoint COM add-in, or `src/taskpane/`. The native on-slide editor gets its
agent surface from the harness driver and session recorder; this is the
independent web-side equivalent.

**Goal:** an agent, from its own console, drives the live PowerGantt web app
and builds a Gantt chart in front of the user — bars appearing as the agent
works, with the user able to watch, interrupt, and undo.

Status: **design — awaiting design review (v2.11.0)**
Plan: `PLAN.md` → v2.11 Web Agent Bridge

---

## 1. Why a store bridge and not browser automation

Three candidate architectures were considered (see §9 for the rejected two).

The web app already has the seam this needs:

- `src/stores/documentStore.ts` — one Zustand store holding one `GanttDocument`,
  with an action surface (`addTask`, `updateTask`, `addDependency`, `addRow`,
  `setScale`, `undo`, `redo`, …) that is already the vocabulary an agent wants.
- `src/types/document.ts` — `GanttDocument` is flat, plain JSON. No class
  instances, no geometry, ISO date strings, `schemaVersion: 1`.
- `src/layout/engine.ts` — **layout is derived.** Pixel geometry is computed
  from dates. An agent never places a bar; it states that a task runs
  2026-03-03 → 2026-03-21 on row `row-2`, and the renderer does the rest.
- React + Zustand is reactive, so a store mutation *is* a live UI update.

So the design is: **expose the store actions as MCP tools.** No DOM clicks, no
drag simulation, no coordinate math, no dependence on component structure.

The decisive property: because there is exactly one store and one document,
agent edits and user edits cannot desync. They are the same edits.

---

## 2. Topology

Three participants, two processes beyond the browser:

```
┌────────────────────┐   stdio    ┌──────────────────────────┐   ws    ┌──────────────────┐
│  Agent host        │◄──────────►│  bridge MCP server       │◄───────►│  browser page    │
│  (Claude Code)     │  MCP JSON  │  (node, stdio + ws host) │  local  │  AgentBridge +   │
└────────────────────┘            │  127.0.0.1:5199          │         │  documentStore   │
                                  └──────────────────────────┘         └──────────────────┘
```

**One Node process wears two faces**: an MCP stdio server (spawned by the agent
host via `.mcp.json`) that also hosts a WebSocket server bound to loopback. The
browser page connects *out* to it.

Why the MCP server hosts the socket rather than the Vite dev server:

- The MCP process is already lifecycle-managed by the agent host, so the socket
  is up exactly when an agent is present.
- The page reconnects with backoff, so restarting either side is a non-event.
- The bridge is not coupled to the dev server's internals or plugin API.

The cost is one extra port and an origin/token story (§6), both handled
explicitly.

> **Corrected by design review (F4).** An earlier draft of this section also
> claimed the topology lets the bridge work against `npm run preview` and served
> portable builds. That is false as designed: §6/T3 makes the client
> `import.meta.env.DEV`-only, so it does not exist in those builds at all. v1 is
> **dev-only**, deliberately. The remaining reasons above still hold.

---

## 3. Protocol

Line-delimited JSON over the WebSocket. Deliberately small.

```jsonc
// browser → server, on connect
{ "type": "hello", "clientId": "…", "token": "…", "href": "…", "revision": 12 }

// server → browser  (opId present on every mutating call; see F2)
{ "type": "call", "id": 7, "opId": "op-a3f…", "method": "add_task", "params": { … } }

// browser → server
{ "type": "result", "id": 7, "ok": true,  "data": { "id": "task-x9" },
  "revision": 13, "userEditedSince": false, "replayed": false }
{ "type": "result", "id": 7, "ok": false, "error": { "code": "…", "message": "…" } }

// browser → server, unsolicited
{ "type": "event", "kind": "doc-changed", "revision": 14, "source": "user" }
{ "type": "event", "kind": "bye" }
```

Error codes: `NO_CLIENT`, `REVISION_CONFLICT`, `VALIDATION_FAILED`,
`UNKNOWN_ID`, `TIMEOUT`, `BRIDGE_DISABLED`.

Per-call timeout 5 s, returning `TIMEOUT`. Retry is safe **only with the same
`opId`**: the browser keeps a bounded LRU of applied `opId`s and replays the
original result rather than applying the mutation twice (`replayed: true`).
Without that key, a blind retry could double-apply — which is why F2 made
`opId` mandatory on mutating calls.

**Multiple tabs:** the server tracks connected clients and routes to the most
recently active one. `get_status` reports the count; if more than one is
attached, mutating tools require an explicit `clientId`. No implicit broadcast —
an agent silently editing two documents is a footgun.

---

## 4. Store foundations (the only changes to existing app code)

Four additions to `documentStore.ts`. Everything else in `src/` is untouched.

**4.1 Revision counter.** A monotonic `revision: number` bumped by every
mutation. This is how the agent detects that the user edited underneath it.
Threading it through the existing `mutate()` helper covers every action at once.

**4.2 `applyBatch(ops: DocOp[])` — one history frame.** Today `mutate()` calls
`pushHistory` on *every* action (`documentStore.ts:58`). An agent adding 30
tasks would create 30 undo entries and 30 renders. `applyBatch` reduces the ops
against the document and pushes exactly **one** history frame, so the user
undoes the agent's whole turn with a single Ctrl+Z. This is the single most
important addition; the tool surface is built around it.

**4.3 Agent-activity flag.** `agentActive: boolean` + `lastAgentEditAt`, so
`StatusBar` can show an unobtrusive indicator. The user must be able to see that
something other than their own hands is editing the document.

**4.4 `DocOp` union + reducer.** A discriminated union (`{op:'add_task', …}`)
with a pure `applyOp(doc, op): GanttDocument`. Pure, exhaustively switched,
unit-testable without a browser — and it becomes the shared vocabulary for both
the individual tools and `applyBatch`.

These four are independently useful and land first, behind `npm test`, with no
bridge in the tree yet.

---

## 5. Tool surface (v1)

All payloads validated against the existing `validateDocument` /
`schema.ts` path — **no second schema.** Mutating tools accept an optional
`expectedRevision` and return the new `revision`.

**Session & read**
| Tool | Returns |
|---|---|
| `get_status` | bridge up, clients attached, title, revision, entity counts |
| `get_document` | full `GanttDocument` JSON |
| `get_view` | viewport, scale, current selection |
| `get_render` | chart as SVG string (evidence to show the user) |

**Document mutation**
| Tool | Notes |
|---|---|
| `new_document` | via `blankDocument()` (`src/embed/index.ts:34`) |
| `add_row` / `update_row` / `delete_row` | delete cascades to tasks + milestones |
| `add_task` / `update_task` / `delete_task` | delete cascades to dependencies |
| `add_milestone` / `update_milestone` / `delete_milestone` | |
| `add_dependency` / `delete_dependency` | validates both endpoints exist |
| `add_marker` / `add_bracket` | |
| `set_calendar` | scale, fiscalYearStart, workingDays, holidays |
| `set_style` | theme, preset |
| `capture_baseline` | |
| **`apply_batch`** | ordered `DocOp[]`, one undo frame — the workhorse |
| `undo` / `redo` | |

**View (no history frame)**
`set_viewport`, `select`, `fit_to_tasks`.

Two conventions that matter:

- **Creation tools return the created id.** Without this an agent cannot wire
  dependencies after creating tasks. Caller-supplied ids are also accepted so a
  batch can be authored in one shot.
- **Read is the document, not a screenshot.** The agent reasons over JSON.
  `get_render` exists to *show the user*, not to inform the agent.

---

## 6. Security — a loopback socket that rewrites the user's document

This is a remote-control channel into a live document and is treated as one.

**Threat model, stated honestly (design review F6).** The defences below stop a
**web-origin** attacker: a malicious or compromised page open in the user's
browser that tries to reach the loopback socket. That is the realistic threat
for a dev-time tool. They do **not** stop a local attacker already running code
as the user — such a process can read the token file directly and forge any
`Origin` header. Local code execution is explicitly out of scope; claiming
otherwise would be security theatre.

**T1 — any web page can reach loopback WebSockets.** WebSocket handshakes are
*not* subject to CORS. Any site open in the user's browser could connect to
`ws://127.0.0.1:5199` and start issuing edits. Two mitigations, both required:
1. **Origin allowlist** — reject the handshake unless `Origin` is
   `http://localhost:5180` / `http://127.0.0.1:5180` (plus configured preview
   origins).
2. **Per-session token** — the MCP server generates a random token at startup
   and writes it to gitignored `.powerplanner-bridge.json`. A dev-only Vite
   middleware (`/__agent-token`) serves it to the page on the same origin. No
   copy-paste, no `define`, and a foreign origin cannot read it.

**T2 — exposure beyond the machine.** Bind `127.0.0.1` explicitly, never
`0.0.0.0`. Asserted by a test.

**T3 — shipping the bridge.** The bridge must not exist in the portable build.
Gated behind `import.meta.env.DEV` **and** an explicit opt-in, so Vite's
dead-code elimination strips it from `vite.export.config.ts` output. Gate:
**a test greps the built `dist-template/PowerGantt.html` and fails if any
bridge symbol appears.** Offline-first portable HTML behaviour is unchanged.

**T4 — the user losing control.** The user's own edits always win a conflict
(`REVISION_CONFLICT` is returned to the agent, never resolved by overwriting).
`applyBatch` keeps undo a single keystroke. The status indicator makes agent
activity visible. A disconnect toggle stops the session immediately.

---

## 7. Verification strategy

Layered, cheapest first — and deliberately arranged so that no layer can report
green about a failure mode it cannot observe.

1. **Pure unit (`npm test`)** — `applyOp` reducer, batch reduction, revision
   monotonicity, cascade deletes, schema rejection of malformed payloads.
2. **Protocol unit** — server framing, timeout, revision conflict, origin
   rejection, token rejection, `NO_CLIENT`.
3. **Playwright e2e (`npm run test:e2e`)** — real browser + real bridge: connect,
   `apply_batch` of a 12-task plan, assert the rendered SVG contains 12 bars,
   assert one Ctrl+Z reverts the whole batch, assert a user edit mid-session
   produces `REVISION_CONFLICT`.
4. **Build-output gate** — bridge symbols absent from the portable HTML.
5. **Live demo** — the agent builds a real plan while the user watches, with a
   `get_render` SVG captured as the artifact.

Explicit blind-spot note, per this project's history: layers 1–2 cannot detect a
bad *experience* (bars appearing in a jarring order, viewport not following,
indicator invisible). Only layer 5 answers that. It is a required gate, not a
nice-to-have.

---

## 8. Performance targets

| Quantity | Target | Kind |
|---|---|---|
| `apply_batch` of 50 ops → **1 history frame, 1 store `set`** | exact | **correctness gate** |
| Single tool call, round trip | < 100 ms | budget |
| `apply_batch` of 50 ops | < 300 ms | budget |
| Reconnect after server restart | < 2 s, automatic | budget |

Only the first row is a pass/fail gate, and it is stated in terms that a unit
test can observe directly. An earlier draft gated on "exactly 1 render commit",
which React 19 batching makes unobservable without instrumenting the renderer —
precisely the kind of gate that reports green about something it cannot see
(design review F8; see the audit register at the foot of `PLAN.md` for why this
project is strict about it).

The batch behaviour is the meaningful one regardless: it is the difference
between a chart that materialises and a chart that stutters into place, and
between one Ctrl+Z and fifty.

---

## 9. Alternatives considered and rejected

**A — File sync (agent writes YAML/JSON, app imports).** Cheap, headless,
portable; the persistence layer already round-trips it
(`exportYaml` / `parseFileContents` in `src/persistence/fileIo.ts`). Rejected as
the *primary* mechanism: it is whole-document replace, the user sees nothing
until reload, and there is no live authoring. **Retained as the export path** —
same schema, so live-authored charts still save, load, and share as files.

**C — Browser automation (chrome-devtools MCP / DOM driving).** Zero new code.
Rejected for authoring: DOM clicks and simulated drags are brittle against UI
refactors, carry no semantic vocabulary, and are slow across dozens of tasks.
**Retained for verification** — screenshots and console errors during review.

**Vite-plugin-hosted socket** (bridge lives in the dev server, MCP posts to it
over HTTP). Attractive because it inherits the dev server's origin and needs no
extra port or token. Rejected because it couples the bridge to `npm run dev`
and breaks under `preview` and portable serving. The token/origin work in §6 is
the price of that independence, and it is bounded.

---

## 10. Decisions (design review closed 2026-08-07)

| # | Question | Decision |
|---|---|---|
| 1 | Bridge port | **5199**, overridable via `POWERPLANNER_BRIDGE_PORT`. Clear of the 5180/5181/5190 convention in `.claude/launch.json`. |
| 2 | Opt-in shape | **Explicit UI toggle.** Not auto-connect. The user turns the bridge on deliberately; the toggle doubles as the stop control. |
| 3 | Agent viewport control | **Allowed but never automatic.** `set_viewport` / `fit_to_tasks` exist as tools; nothing moves the viewport as a side effect of an edit. |
| 4 | Requirements location | **Create `spec/srs-web/`.** First occupant of the layer `AGENTS.md` anticipates. |
| 5 | v1 tool surface | **Full surface** — rows, tasks, milestones, dependencies, markers, brackets, calendar, style, baseline, undo/redo, `apply_batch`. One reducer switch makes the marginal ops cheap. |

---

## 11. Design review findings (B0e, 2026-08-07)

Adversarial pass against this document. Ten findings; six changed the design.

**F1 — document-level revision checking would misfire constantly.**
`expectedRevision` as originally specified rejects a call whenever *anything*
changed, including a user renaming an unrelated row. In a live session that is
most of the time, so agents would either fail continuously or blind-retry and
defeat the purpose. **Changed:** `expectedRevision` is now **opt-in per call**
(absent = no check). Every result carries `revision` and a `userEditedSince`
flag so the agent can notice drift without being blocked by it. Hard conflict
checking is reserved for `apply_batch`, where atomicity is the point.

**F2 — timeout without idempotency leaves an unrecoverable state.** §3 says a
timed-out mutation is never retried, because the agent cannot know whether it
landed. That is correct but leaves the agent stuck. **Changed:** every mutating
call carries a client-generated `opId`; the browser keeps a bounded LRU of
applied `opId`s and returns the *original* result on replay. Retry becomes safe,
so the no-auto-retry rule can relax to "retry with the same `opId`".

**F3 — `apply_batch` atomicity was claimed but never specified.** What happens
when op 7 of 20 fails validation? A half-applied document is worse than a
rejected one. **Changed:** the batch is reduced into a *draft* copy and
validated as a whole before anything is committed — all-or-nothing. The pure
reducer (B1a) makes this nearly free, which is a second reason to build it.

**F4 — §2 contradicted §6.** §2 sold the standalone-socket topology partly on
working against `preview` and portable builds; §6/T3 makes the client
`import.meta.env.DEV`-only, so it cannot. **Changed:** claim removed, v1
declared dev-only. Recorded inline in §2 rather than quietly deleted.

**F5 — token delivery has no non-dev story.** The `/__agent-token` middleware is
a Vite dev-server feature. Consistent with F4 for v1; recorded as a known limit
if the bridge is ever extended past dev.

**F6 — the security section overclaimed.** Origin + token defend against a
web-origin attacker, not against local code running as the user. **Changed:**
threat model stated explicitly at the top of §6.

**F7 — `get_render` assumed reuse that does not exist.** `exportSvg()` in
`src/persistence/fileIo.ts` triggers a *download*; it does not return a string.
**Changed:** B3 must add a small pure serializer that returns the SVG markup;
the download path stays as-is.

**F8 — "exactly 1 render commit" is not a measurable gate.** React 19 batches on
its own, so the stated perf target cannot be observed without instrumenting the
renderer — and this repo has a documented history of gates that measure
something other than what they claim (see the audit register at the foot of
`PLAN.md`). **Changed:** the observable proxy is **one history frame and one
store `set` call per batch**, both directly assertable in a unit test. Wall-clock
targets stay, but they are budgets, not correctness gates.

**F9 — a bare status indicator is thin.** The user sees bars appear but not why.
Optional, deferred: let a tool attach a short "what I'm doing" caption shown in
the indicator. Recorded, not scheduled.

**F10 — multi-tab routing.** Reviewed and kept as designed: explicit `clientId`
required whenever more than one client is attached, never an implicit broadcast.

**Not changed but worth stating:** the bridge deliberately has no server-side
document model. The browser store remains the single source of truth, and the
MCP server is a relay with schemas. Any future temptation to cache the document
server-side reintroduces exactly the desync this design exists to avoid.
