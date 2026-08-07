# Web-Specific Software Requirements Specification (SRS)

Requirements that apply **only to the web implementation** (`src/`, Vite build,
browser runtime). Same ASPICE-style table format as [`../srs/`](../srs/README.md)
and [`../srs-native/`](../srs-native/README.md) — see the shared layer's README
for the ID scheme, column meanings, and authoring rules.

This layer exists because some requirements have no meaningful native
counterpart: browser security posture, dev-server coupling, build-output
isolation, and the DOM/Zustand runtime.

## When a requirement belongs here

| Put it in | When |
|---|---|
| [`../srs/`](../srs/README.md) | Both implementations must satisfy it (data model, layout, persistence semantics). |
| [`../srs-native/`](../srs-native/README.md) | Native-only: overlay lifecycle, app bar, COM shape protection, on-slide selection. |
| **`srs-web/`** (here) | Web-only: browser runtime, origin/token security, Vite build isolation, web agent surface. |

A requirement that reads naturally for both implementations belongs in the
shared layer even if only one currently implements it.

## Specifications

| File | Feature tag | Covers |
|------|-------------|--------|
| [SRS-agent-bridge.md](SRS-agent-bridge.md) | `ABR` | The MCP agent bridge: an agent driving the live web app's document, and the safety properties that must hold while it does. |

## Trace

Requirements here trace **up** to the design specs and plan, and **down** to
implementation files and tests. Every requirement must name a verification
method that can actually observe the behaviour it asserts — a gate that cannot
see its own failure mode is treated as a defect in this project, not as a pass
(see the audit register at the foot of [`../../PLAN.md`](../../PLAN.md)).
