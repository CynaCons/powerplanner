# PowerPlanner Native PowerPoint Add-in (C++ COM)

A native COM add-in for PowerPoint desktop, in the architectural style of
think-cell: the chart lives **on the slide as native shapes**, and the editing
UI is drawn **contextually over the slide**, not in a side task pane.

This is a separate, Windows-only codebase under `native/`. It does not share
runtime code with the React web app — only concepts (the data model and the
date→coordinate layout math, ported to C++).

## Why C++ COM and not Office.js

The Office.js web add-in (`src/taskpane/`) already emits native shapes, but it
runs in a sandboxed task-pane webview. It **cannot** draw floating controls over
the slide canvas or hook PowerPoint's selection/mouse events — there is no web
API for it. think-cell is a C++ COM add-in for exactly this reason. To match
that interaction model we need an in-process COM add-in.

## Toolchain (verified on this machine)

- Visual Studio 2022 Community — MSVC 14.33, ATL, Windows SDK 10.0.19041
- 64-bit Office (PowerPoint desktop, Office16 / Click-to-Run)
- Type libraries (resolved by LIBID from the registry):
  - PowerPoint Object Library — `{91493440-5A91-11CF-8700-00AA0060263B}`
  - Microsoft Office (MSO) — `{2DF8D04C-5BFA-101B-BDE5-00AA0044DE52}`
  - Add-in Designer (`IDTExtensibility2`) — `{AC0714F2-3D04-11D1-AE7D-00A0C90F26F4}`

Because Office is 64-bit, the add-in DLL is built **x64**. Bitness must match
PowerPoint or it will not load.

## Architecture

In-process COM server (DLL) implementing:

- `IDTExtensibility2` — the classic Office add-in lifecycle (`OnConnection`
  caches the `PowerPoint.Application` pointer; `OnDisconnection` tears down).
- `IRibbonExtensibility` — `GetCustomUI` returns Fluent ribbon XML; ribbon
  callbacks (`onLoad`, button `onAction`) are routed through a hand-implemented
  `IDispatch` (`GetIDsOfNames` / `Invoke`) since there is no wizard-generated
  type library for the callbacks.

Registration is **per-user** (`AtlSetPerUserRegistration(true)`), so no admin
rights are needed: the COM class lands under `HKCU\Software\Classes` and the
load entry under `HKCU\Software\Microsoft\Office\PowerPoint\AddIns\PowerPlanner.Connect`
with `LoadBehavior=3`.

## Roadmap

| Phase | Deliverable |
|-------|-------------|
| **N1** | Loadable add-in: DLL loads in PowerPoint, "PowerPlanner" ribbon tab, button click reaches native code |
| **N2** | "Insert Gantt" emits native shapes (rectangles/diamonds/connectors/text) via the PowerPoint object model — C++ port of `src/taskpane/officeBridge.ts` |
| **N3** | C++ data model + JSON round-trip embedded in a shape tag (`PP_DOC`) |
| **N4** | On-slide overlay: layered child window over the slide-edit pane, selection/mouse hooks, contextual handles aligned to chart shapes |
| **N5** | Live "agents": watch shape edits and reflow the chart automatically |
| **N6** | WiX/MSI installer, signed registration, ribbon icons |

## Install / test on another PC

See [native-addin-install.md](native-addin-install.md) — build from source, or
copy the prebuilt DLL + `register.bat` to any 64-bit-PowerPoint machine (no admin,
no Visual Studio needed to register).

## Build & register

```powershell
# from native/
./build.ps1            # compiles x64 DLL into build/
./build.ps1 -Register  # build, then regsvr32 (per-user, no admin)
./build.ps1 -Unregister
```

Verify load: open PowerPoint → File → Options → Add-ins → COM Add-ins, or check
`HKCU\Software\Microsoft\Office\PowerPoint\AddIns\PowerPlanner.Connect`.

## Status

- [x] Toolchain verified, type libraries located, x64 target chosen
- [x] N1 — loadable skeleton + ribbon. Verified in PowerPoint desktop: "PowerPlanner"
      ribbon tab renders, "Insert Gantt" button click reaches native `DoInsertGantt`,
      and `OnConnection` logs from inside `POWERPNT.EXE`.

### Build notes (gotchas already solved)

- **Static CRT (`/MT`).** A `/MD` build loads fine via `regsvr32`/PowerShell but
  PowerPoint silently skips it (cannot resolve `vcruntime140.dll` in its load
  context). `dumpbin /dependents` must show only system DLLs.
- **No `named_guids` in `#import`.** Office and PowerPoint both declare an
  `Adjustments` interface; `named_guids` emits `IID_Adjustments` twice in one
  object → `LNK1179 duplicate COMDAT`. Use `__uuidof` + hand-defined LIBIDs.
- **No precompiled header** with these imports (interacts badly — same LNK1179).
- The `COMAddIns` collection from an OLE-automation instance does NOT reliably
  list a never-connected add-in. Verify load with an interactive launch (and the
  `%TEMP%\powerplanner-addin.log` written by `OnConnection`).
