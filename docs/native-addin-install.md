# Native Add-in — Build & Install (test on any Windows PC)

How to get the PowerPlanner native add-in running in PowerPoint, either by
building from source or by copying a prebuilt DLL to another machine.

The add-in is a 64-bit, **statically-linked** COM DLL — it depends only on
Windows + an installed **64-bit PowerPoint desktop**. No Visual Studio, no VC++
redistributable, and **no administrator rights** are needed to *register* it.

---

## Option A — Quick install on another PC (prebuilt DLL)

Use this to test on a machine that doesn't have the build tools.

1. Copy two files to the target PC (any folder), side by side:
   - `PowerPlannerAddin.dll` (from `native/build/` after a build, or a release)
   - `native/register.bat`
2. Double-click **`register.bat`** (or run it in a terminal). It runs
   `regsvr32` per-user — no admin prompt.
3. Start **PowerPoint** → a **PowerPlanner** tab appears on the ribbon.
4. New slide → **Insert Gantt**. Select the chart to see the on-slide overlay;
   move a bar and click **Reflow**; **Pull from slide** reads it back.

To remove it: run **`unregister.bat`**.

> Requirements on the target PC: 64-bit PowerPoint (Microsoft 365 / 2016+).
> The DLL is `/MT` (static CRT), so nothing else needs installing.

---

## Option B — Build from source

### Prerequisites
- **Visual Studio 2022** (Community is fine) with the **Desktop development with
  C++** workload, which provides: MSVC v14.3x toolset, **ATL**, and the
  **Windows 10/11 SDK**.
- 64-bit PowerPoint desktop installed (the build resolves the Office/PowerPoint
  type libraries from the registry at compile time).

### Build
From the repo root:

```bat
native\build.bat
```

This compiles the x64 DLL to `native\build\PowerPlannerAddin.dll`. Then register
it for the current user:

```bat
native\register.bat
```

(or `powershell -File native\build.ps1 -Register` to build + register in one step.)

### Verify it loaded
- PowerPoint → File → Options → Add-ins → *Manage: COM Add-ins* → **Go…** —
  "PowerPlanner" should be listed and checked.
- Or check the registry key
  `HKCU\Software\Microsoft\Office\PowerPoint\AddIns\PowerPlanner.Connect`
  (`LoadBehavior` = 3).
- The add-in also writes a log to `%TEMP%\powerplanner-addin.log` on connect.

---

## What you can test today

| Ribbon button   | What it does |
|-----------------|--------------|
| **Insert Gantt** | Emits a sample chart as native PowerPoint shapes (grouped, tagged). |
| **Pull from slide** | Reads the embedded document (`PP_DOC`) back and reports its contents. |
| **Reflow** | Reads moved/resized bars back into dates and rebuilds the chart (dependencies, summary, and embedded data stay in sync). |

Selecting the chart shows the on-slide overlay (selection frame + handles + a
"PowerPlanner" badge), which tracks zoom/scroll.

---

## Troubleshooting

- **No PowerPlanner tab:** ensure you registered the **x64** DLL and PowerPoint
  is **64-bit** (bitness must match). Re-run `register.bat`. Check that
  `LoadBehavior` is 3 (PowerPoint sets it to 2 if a load failed).
- **`regsvr32` says it can't find entry point / module:** the DLL bitness
  doesn't match, or the file is blocked — right-click the DLL → Properties →
  **Unblock**, then re-register.
- **Tab appears but buttons error:** open a slide in **Normal** view first.
- **Build fails resolving `MSPPT.OLB` / Office libs:** PowerPoint isn't
  installed, or is 32-bit while you're building x64.

## Uninstall

```bat
native\unregister.bat
```

This removes the per-user registration; delete the DLL to finish.
