# Headless render without PowerPoint (NTS-03 / Phase 13 v2.8.4)

## Goal

CI and pure unit tests that assert scene geometry / visual primitives without
launching POWERPNT.EXE.

## Options

1. **Scene dump JSON** (recommended MVP): `BuildProjectedScene` / pure layout
   already emit prims; write a text/JSON dump harness (extends ops-test /
   ppunit) and snapshot prim kind:id:rect sets.
2. **GDI+ offscreen bitmap**: paint a subset of chrome/chart from pure scene
   data into a PNG under `native/build/` (no COM).
3. **SVG backend**: port web `GanttRenderer` concepts to C++ or share fixture
   expected JSON only (already have layout conformance).

## MVP status (2026-07-17)

- **Design + ticket only** this slice: layout conformance (`ppconf`) and
  ops-test already cover pure model/layout without PPT.
- Full GDI+/SVG headless renderer deferred as productized follow-up; not
  required for Phase 13 exit if pure gates green.

## Ticket

- Title: Headless scene-dump or GDI+ PNG backend for native CI
- Depends on: stable prim schema from GanttScene
- Acceptance: `ppunit` or new exe runs on GHA without Office installed
