# SRS — Web Agent Bridge

Requirements for the MCP surface that lets an agent drive the **live web app's**
Gantt document. Feature tag: `ABR`.

Traces up to: [`../../docs/web-agent-bridge-plan.md`](../../docs/web-agent-bridge-plan.md),
[`../data-model.md`](../data-model.md), [`../srs/SRS-persistence.md`](../srs/SRS-persistence.md).
Plan: `PLAN.md` → Phase 14 / v2.11.x.
Reference impl (planned): `src/agent/`, `src/stores/documentStore.ts`, `mcp/agent-bridge/`.

**Scope:** web only. The native add-in's agent surface is the harness driver and
session recorder and is out of scope here.

## Session & connection

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-ABR-001 | The bridge client shall connect only after the user explicitly enables it via a UI control, and shall disconnect when the user disables it. | The user grants remote control of their document deliberately; the same control is the stop button. | Test (e2e) | `src/agent/AgentBridge.ts`; B2a/B2e |
| SRS-ABR-002 | The bridge server shall bind its listening socket to the loopback interface only, never to a wildcard address. | A document-editing channel must not be reachable from the network. | Test (asserts bind address) | `mcp/agent-bridge/`; B3a |
| SRS-ABR-003 | The bridge client shall reconnect automatically with backoff after server restart, without user action and without losing document state. | Restarting the agent host is routine; it must not disturb the user's document. | Test (e2e) | B2a |
| SRS-ABR-004 | When no client is attached, every mutating tool shall fail with `NO_CLIENT` rather than succeeding vacuously. | A tool that reports success with nothing attached would let an agent believe it authored a chart that does not exist. | Test | B3f |
| SRS-ABR-005 | When more than one client is attached, mutating tools shall require an explicit client identifier and shall never broadcast to all clients. | Silently editing the wrong open document is unrecoverable from the user's point of view. | Test | B3e |

## Document mutation

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-ABR-010 | All agent mutations shall be applied through the existing document store actions and the canonical document model; the bridge shall not maintain a parallel document representation. | One source of truth is the property that makes agent and user edits incapable of desyncing. | Review + Test | `src/stores/documentStore.ts`; design §1 |
| SRS-ABR-011 | Agent mutation payloads shall be validated against the same schema path as file loading, and invalid payloads rejected with `VALIDATION_FAILED` without modifying the document. | An agent is an untrusted input source exactly like a file. | Test | `src/persistence/schema.ts`; B1e |
| SRS-ABR-012 | `apply_batch` shall apply an ordered sequence of operations as a single unit: if any operation is invalid, none shall be applied. | A half-applied plan is worse than a rejected one and is hard for either party to reason about. | Test | design F3; B1b |
| SRS-ABR-013 | `apply_batch` shall produce exactly one undo history frame and one store commit regardless of operation count. | One user keystroke must be able to revert an agent's entire turn. | Test (counts frames and `set` calls) | design §8; B1b/B4d |
| SRS-ABR-014 | Entity-creating tools shall return the identifier of each created entity, and shall accept a caller-supplied identifier. | Without returned ids an agent cannot wire dependencies to the tasks it just created. | Test | B3c |
| SRS-ABR-015 | Deleting a row shall cascade to its tasks and milestones, and deleting a task shall cascade to its dependencies, identically to user-initiated deletion. | Agent edits must not be able to leave dangling references that user edits cannot produce. | Test | `documentStore.ts`; B1e |
| SRS-ABR-016 | View operations (viewport, selection) shall not create undo history frames, and no document mutation shall move the viewport as a side effect. | Undo is for document changes; an agent must not fight a user who is scrolling. | Test | design §10 decision 3 |
| SRS-ABR-017 | The operation set shall include moving a row to a new position in the row order. | Row order is part of the document and was the one structural property no caller could change: it is array position, and no operation addressed it. | Test | `src/stores/docOps.ts`; v2.16.0 |
| SRS-ABR-018 | The operation set shall include setting the calendar's working days and its holiday list. | Weekend and holiday shading is derived from the calendar, so a plan authored entirely by an agent could not state when work does not happen. | Test | `src/stores/docOps.ts`; v2.16.0 |
| SRS-ABR-019 | Every document operation available to an agent shall also be reachable by a user through the UI, without the bridge. | Owner directive 2026-10-23. The bridge is a convenience, never the only way to author: an operation a person cannot perform is a dependency on an agent being available. | Review + Test (e2e) | v2.16.0 |

## Concurrency & idempotency

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-ABR-020 | Every mutation result shall carry the resulting document revision and a flag indicating whether the user has edited since the agent's previous call. | The agent needs to notice that the document moved under it without being blocked every time it does. | Test | design F1 |
| SRS-ABR-021 | Revision checking shall be opt-in per call; when a caller supplies an expected revision and it does not match, the call shall fail with `REVISION_CONFLICT` and apply nothing. | Mandatory whole-document checking would reject unrelated concurrent user edits and make live sessions unusable. | Test | design F1; B2d |
| SRS-ABR-022 | A conflict shall never be resolved by overwriting the user's state; the agent is informed and the user's document stands. | The user's direct manipulation outranks the agent's plan. | Test + Review | B2d |
| SRS-ABR-023 | Every mutating call shall carry a caller-generated operation id; a repeated call with an already-applied operation id shall return the original result and apply nothing further. | Without an idempotency key, retrying after a timeout can double-apply, and not retrying leaves the agent stuck. | Test | design F2; B3f |
| SRS-ABR-024 | A call that exceeds the per-call timeout shall fail with `TIMEOUT`; retry shall be permitted only with the same operation id. | Bounds a hung page while keeping retry safe under SRS-ABR-023. | Test | B4a |

## Read & evidence

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-ABR-030 | The bridge shall expose the current document as canonical JSON. | The agent reasons over the model, not over pixels. | Test | B3b |
| SRS-ABR-031 | The bridge shall expose the rendered chart as SVG markup for the purpose of showing the user, without triggering a file download. | Evidence the user can look at; the existing export path downloads a file and is unsuitable. | Test | design F7; B0f/B3b |
| SRS-ABR-032 | Bridge status shall report whether a client is attached, how many, the document title, and the current revision. | An agent must be able to tell "not connected" from "connected to an empty document". | Test | B3b |

## Security

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-ABR-040 | The bridge server shall reject WebSocket handshakes whose `Origin` is not in a configured allowlist. | WebSocket handshakes are not subject to CORS, so any page the user has open could otherwise connect. | Test | design §6/T1; B3d |
| SRS-ABR-041 | The bridge server shall require a per-session token, generated at server start, and reject handshakes presenting an absent or incorrect token. | Defence in depth behind the origin check; a foreign origin cannot read a same-origin-served token. | Test | B2c/B3d |
| SRS-ABR-042 | The session token shall be delivered to the page over the same origin and shall not be committed to version control. | Removes the copy-paste step without turning the token into a repository secret. | Test + Review (gitignore) | B2c |
| SRS-ABR-043 | The threat model shall be documented as covering web-origin attackers and explicitly **not** covering code already executing locally as the user. | Overstating the guarantee is worse than stating a bounded one. | Review | design §6 |

## Build isolation

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-ABR-050 | No bridge code shall be present in the portable single-file HTML build output. | The portable build is distributed to third parties; a remote-control channel must not travel with it. | Test (greps built `dist-template/PowerGantt.html`) | design §6/T3; B4c |
| SRS-ABR-051 | Introducing the bridge shall not change portable HTML, JSON, or YAML round-trip behaviour. | Persistence is the product's core promise and is not in this feature's scope. | Test | `../srs/SRS-persistence.md` SRS-PERS-013; B4f |

## Visibility & user control

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-ABR-060 | While a bridge session is active, the app shall display a persistent indication that an agent may be editing. | The user must never be unable to explain why their document is changing. | Demo (live gate) | B1d |
| SRS-ABR-061 | The user shall be able to end a bridge session at any time from the UI, taking effect without a page reload. | Withdrawal of consent must be immediate and local. | Test (e2e) | B2e |
| SRS-ABR-062 | A live session in which an agent authors a plan while the user watches shall be reviewed for interaction quality, and that review shall be a required gate. | Unit, protocol, and e2e layers cannot observe whether the experience is good; only watching it can. | Demo (user gate) | B4e; design §7 |

## Open items

- SRS-ABR-062 is a user gate and cannot be self-closed by an agent, consistent
  with the user-milestone-gate convention in `PLAN.md`.
- Deferred (design F9): attaching an agent-supplied caption to the indicator of
  SRS-ABR-060, so the user sees stated intent rather than only moving bars.
- No requirement yet covers bridge behaviour outside dev builds, because v1 is
  deliberately dev-only (design F4/F5). Extending past dev requires a token
  delivery mechanism that does not depend on the Vite dev server.
