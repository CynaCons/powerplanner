# Native overlay input-loss analysis

Investigation of the intermittent "overlay stops receiving clicks" defect, plus the
"overlay floats over other applications" report.

Evidence base: two live PowerPoint recordings from the same DLL build
(`2.10.0-dev`, built `Jul 18 2026 17:27:18`), same PowerPoint process (PID 29472),
12 minutes apart:

- Session A (working): `native/records/20260718-174508-273-29472`
- Session B (broken):  `native/records/20260718-175725-552-29472`

Source read at the working-tree state of 2026-07-18. **No source file was modified
by this investigation.**

---

## 0. Correction to the premise

The brief states session B shows "mouse MOVEMENT but never mouse BUTTON messages",
and asks for an explanation of the move-survives/click-dies asymmetry.

**There is no such asymmetry.** The timeline shows two clean phases:

| Phase | Window | What the overlay received |
|---|---|---|
| 1 | t = 828 ms → 7531 ms | 60 `WM_MOUSEMOVE`, hover repaints firing normally |
| 2 | t = 7531 ms → 18812 ms (end) | **zero mouse messages of any kind** — no moves, no buttons |

The overlay did not lose *button* messages. It went **completely deaf to the mouse**
at t ≈ 7531 ms and stayed deaf for the remaining 11.3 seconds, while continuing to
tick, paint and emit snapshots at the normal ~150 ms cadence. The user's five clicks
all fall inside phase 2, which is why the recording contains no button messages —
not because buttons were filtered out.

This matters, because it eliminates every hypothesis that discriminates between
message types (`WS_EX_TRANSPARENT` toggling, `MA_NOACTIVATEANDEAT`, a low-level hook
eating button messages, capture theft). The failure is **wholesale mouse-input
routing**, and it flips at a single instant.

Also worth stating up front: `RecEmitInput` is the *first* statement in
`OverlayWndProc` (`Overlay.cpp:5031`), before any handler logic. Therefore any
`WM_LBUTTONDOWN` that reached the window would have been recorded regardless of how
it was subsequently handled. **The messages never arrived. This is a routing defect,
not a handler defect.** No handler in `OverlayWndProc` needs to be examined.

---

## 1. Ground truth established from the recordings

### 1.1 The overlay was alive, visible, on top, and host-active throughout phase 2

- `paint` and `snapshot` events continue at normal cadence to the end of session B
  (last paint t = 18812).
- Tick reaches the paint stage only after passing the host-scoping gate at
  `Overlay.cpp:9150-9163` (`if (!g_lastHostActive) { HideOverlay(); … return; }`) and
  the view gate at `9169-9176`. Continued paints therefore *prove*
  `IsHostActiveForOverlayChrome()` returned **true** — PowerPoint was foreground the
  whole time. The user did not switch apps during phase 2.
- Frame `frames/0002-sel.png`, captured at t = 10094 ms (mid phase 2), shows the
  overlay's REC indicator and the bottom app bar rendered on screen — the overlay
  window was visible and composited — **and simultaneously shows PowerPoint's own
  native selection handles around the chart group**.

So: the overlay was on top and painting, and PowerPoint received the click.

### 1.2 g_chartScreenRect never changed

Every snapshot in session B reports `chartRect = {470, 275, 1753, 773}`, from t = 16 ms
to t = 18656 ms. The last recorded move was at screen (621, 451) — comfortably inside
that rect. The chart geometry is not the variable.

### 1.3 Nothing else was swallowing the input

- App bar: `appBarRect = {796, 781, 1427, 825}` — entirely *below* the chart
  (chart bottom = 773), so it cannot cover the click points. Its WndProc is also
  recorder-tapped (`Overlay.cpp:7381`, surface `"appbar"`) and logged nothing.
- Theme menu / flyout: also recorder-tapped (surface `"menu"`, wired via
  `ThemeMenu_Init(..., RecEmitInput)` at `Overlay.cpp:5871`); zero `menu` events. Its
  `WH_MOUSE_LL` hook (`ThemeMenu.cpp:404-410`) unconditionally calls
  `CallNextHookEx` and never returns non-zero, so it cannot eat messages anyway.
- Card window: recorder-tapped (surface `"card"`, `Overlay.cpp:5667`); zero events.
- Duplicate overlay windows: both sessions ran in the same PID with a single DLL
  load, and `EnsureWindow` is idempotent on `g_hwnd` (`Overlay.cpp:5853`). Ruled out.

### 1.4 A distractor I checked and dismissed

Every phase-1 hit annotation reports `zone: RowBand` or `Label`, never `TaskBar` —
which initially looks like a broken hit model for session B's chart (whose ids are
`r_research`/`t1`, versus session A's `row-2`/`task-1`). Mapping the move coordinates
against `frames/0002-sel.png` (crop origin 462, 267) shows the pointer at screen
x = 508…621 → image x = 46…159, i.e. the **row-label rail**, left of the plot area
which starts at image x ≈ 210. The hit results are correct. This is not a defect.

---

## 2. Root cause (symptom 1: lost input)

### 2.1 The mechanism

`OverlayWndProc`'s `WM_NCHITTEST` handler, `Overlay.cpp:5032-5051`:

```cpp
if (msg == WM_NCHITTEST) {
    bool altDown = g_cursorOverrideEnabled ? g_cursorOverrideAltDown
                                           : (::GetKeyState(VK_MENU) < 0);
    if (altDown) return HTTRANSPARENT;                        // 5040-5041
    POINT screenPt = { ... };
    if (::PtInRect(&g_chartScreenRect, screenPt)) return HTCLIENT;   // 5046
    ...
}
```

Returning `HTTRANSPARENT` makes USER32 skip this window and continue hit-testing
**windows below it in the same thread**. The add-in is in-process and `EnsureWindow`
runs on PowerPoint's main UI thread, so the window below is PowerPoint's slide canvas
— this is the intended "Alt = escape hatch, let PowerPoint have the mouse" design. It
suppresses *all* mouse messages, moves included, which is exactly the observed
signature: overlay visible and topmost, zero mouse messages, PowerPoint gets the
clicks and selects `CHART_ROOT`.

Given §1.2 (chart rect constant, cursor inside it), `altDown` is the **only** input
in that function that can change the return value. `g_cursorOverrideEnabled` is false
in production (harness-only seam). Therefore the flip at t = 7531 ms was
`::GetKeyState(VK_MENU) < 0` becoming true and staying true.

### 2.2 Why `GetKeyState` latches

`GetKeyState` does **not** return live hardware state. It returns the key state as of
the last message the calling thread *retrieved from its own queue*. `WM_NCHITTEST` is
delivered by `SendMessage` from inside USER32's input dispatch, not retrieved through
`GetMessage`, so the snapshot it reads can be arbitrarily stale. Alt gets latched
"down" whenever the matching `WM_SYSKEYUP` for `VK_MENU` is never retrieved by
PowerPoint's main UI thread. Real ways that happens:

- **Alt+Tab.** The shell's task switcher (a different process) consumes the Alt-up.
  PowerPoint's thread never retrieves it. If the user then only uses the mouse, no
  further keyboard message is ever retrieved and the stale "Alt down" persists
  indefinitely.
- **Office KeyTips / ribbon accelerators.** A tap of Alt is consumed by ribbon UI,
  which in modern Office may live on a different UI thread.
- **A nested modal loop** (COM modal wait, PowerPoint drag tracking) during which the
  thread is not pumping keyboard messages.

Contrast with the rest of the file, which uses the correct API for real-time state:
`::GetAsyncKeyState(VK_ESCAPE)` at `Overlay.cpp:9063` and `9066`. The same stale-read
bug also exists at `Overlay.cpp:5505` (`GetKeyState(VK_MENU) & 0x8000` deciding
milestone-vs-task insertion), though its blast radius is one command, not all input.

### 2.3 Why it is intermittent, and why session A worked

The state that flips it is per-thread, invisible, and sticky:

- Session A (17:45:08) — the user started recording and worked continuously with the
  mouse; the recording shows 6 `WM_LBUTTONDOWN`/`WM_LBUTTONUP` pairs, 9 gesture
  events, 2 successful commits. No Alt latch.
- Session B (17:57:25) — the user was operating the app alongside an agent/terminal,
  i.e. Alt+Tabbing. Phase 1 (mouse-only hovering, 7.5 s) works; the first Alt+Tab
  round-trip latches the flag; phase 2 is dead forever.

Nothing in the recorder can see this: the snapshot chrome block has no `altDown`,
no `lastNcHitTest`, no `hostActive`, no `shown` field. That blind spot is why the
recordings look like "button messages vanished" instead of "hit test went
transparent".

**Falsifiable live prediction.** `AppBarWndProc` returns `HTCLIENT` unconditionally
with no Alt check (`Overlay.cpp:7382`). So in the broken state the **bottom app bar
must still respond to clicks while the chart body is completely dead**. If a live
repro shows the app bar dead too, this root cause is wrong.

### 2.4 Confidence

- Failure is routing (`WM_NCHITTEST`), not handling: **very high (~0.95)** —
  the recorder taps above all handlers.
- The overlay returned `HTTRANSPARENT` while visibly on top: **high (~0.85)** —
  frame 0002 plus the same-thread fall-through design leave no other path by which
  PowerPoint gets a click through a visible topmost overlay.
- The specific trigger is a latched `GetKeyState(VK_MENU)` at `Overlay.cpp:5040`:
  **moderate-high (~0.7)** — it is the only sticky global on that path, but the
  recordings contain no direct observation of Alt state.

---

## 3. Root cause (symptom 2: overlay over foreign windows)

**This is a separate defect. It does not share a code cause with symptom 1.** The
brief's unified hypothesis is not supported by the evidence, and I am not going to
force it.

`Overlay.cpp:5865-5868`:

```cpp
g_hwnd = ::CreateWindowExW(
    WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
    kClass, L"", WS_POPUP,
    0, 0, 10, 10, NULL, NULL, g_inst, NULL);
//                    ^^^^ hWndParent = NULL  → unowned top-level window
```

Same shape for the app bar (`7449-7450`) and the theme menu (`ThemeMenu.cpp:538-541`).
The card and inline editor are `WS_EX_TOOLWINDOW | WS_EX_TOPMOST`, also unowned
(`2092-2093`, `2333-2334`).

Consequences:

1. **No owner.** An unowned top-level window has no z-order, minimize, or activation
   relationship with PowerPoint. It will not follow the host down.
2. **Permanently topmost in production.** The only `HWND_NOTOPMOST` paths
   (`6784`, `7511`, `2122`, `2489`) are gated on `g_hostActiveOverrideMode >= 0`,
   which is harness-only. Live, `WS_EX_TOPMOST` is never removed. (This also refutes
   a "lost topmost, fell behind PowerPoint" explanation for symptom 1 — the overlay
   *cannot* fall below PowerPoint's non-topmost frame.)
3. **The only thing keeping it off other apps is a 150 ms poll.** `Tick()` →
   `IsHostActiveForOverlayChrome()` → `HideOverlay()` at `9150-9163`. There is **no**
   `WM_ACTIVATEAPP` handler and no foreground WinEvent hook anywhere in
   `native/PowerPlannerAddin` (grep: zero hits). Nothing is event-driven.
4. **`SetWindowPos` is not re-issued on a steady chart.** `6777` computes
   `needUpdate = dpiChanged || !wasShown || !hadWindow || moved || resized`; with a
   stationary chart, z-order is never reasserted. Not the cause here, but it means
   the window's placement is only ever corrected reactively.

So the overlay floats over other applications during **any window in which `Tick()`
does not run**:

- `if (g_mutating) return;` (`9040`) and `if (g_inTick) return;` (`9045`).
- Any blocking outgoing COM call, PowerPoint modal dialog, or native menu/drag
  tracking loop that starves `WM_TIMER`.
- The unavoidable ≤150 ms latency on every single app switch.
- **Permanently**, if `g_mutating` is ever left stuck true. It is set and cleared by
  hand at ~20 call sites (`2163/2184`, `2534/2550`, `4117/4157`, `4756/4891`,
  `6833/6874`, `7916/7956`, `8396/…`, …) with no RAII guard, unlike `g_inTick` which
  *is* guarded (`TickGuard`, `9046-9049`). One escaping exception on any of those
  paths disables the host-scoping poll for the life of the process.

The defensive comments at `6701-6705` and `6779-6783` show this class of bug has
already bitten twice in production ("overlay remained over a fullscreen game",
"app bar over a fullscreen game"). Both fixes were patches to the polling path, not
to the ownership model.

**Shared cause?** No shared code. **Shared trigger, yes:** Alt+Tab / foreground
switching exercises both defects at once, which is why the user experiences them
together and why they were reasonably suspected to be one bug.

Confidence on this mechanism: **high (~0.9)** — it is directly readable from the
source and requires no inference about runtime state.

---

## 4. Proposed fixes

### 4.1 Symptom 1 — Alt escape hatch

**Option A (minimal): swap the API.** Replace `::GetKeyState(VK_MENU) < 0` at
`Overlay.cpp:5040` with `(::GetAsyncKeyState(VK_MENU) & 0x8000) != 0`.
`GetAsyncKeyState` reads real hardware state and cannot latch.
*Pros:* one line, zero behaviour change when Alt genuinely is down.
*Cons:* keeps a global, invisible, all-or-nothing input kill-switch on the hottest
path in the add-in; `GetAsyncKeyState` is also slightly more expensive and is called
on every hit test (which is every mouse move). Also does not fix `5505`.

**Option B (preferred): take Alt off the hit-test path entirely.** Always return
`HTCLIENT` inside the chart rect. Decide click-through at *button-down* time in
`WM_LBUTTONDOWN` (`5069`) and forward the click to PowerPoint explicitly — the file
already has exactly this pattern for the wheel (`ForwardWheelToPowerPoint`, `5066`).
*Pros:* a stale modifier can then cost at most one click, never a permanently dead
chart; hover/moves keep working so the failure is self-evident and self-healing.
*Cons:* forwarding a synthetic button-down into PowerPoint's canvas is fiddlier than
letting USER32 route it (coordinate space, capture, double-click timing), and it is
a behaviour change to the documented Alt escape hatch. Needs a live check that
Alt+drag of the whole group still works.

**Option C: watchdog.** In `Tick()`, if `altDown` reads true but
`GetAsyncKeyState(VK_MENU)` reads false, force a keyboard-state resync. Cheap, but it
is a band-aid over Option A.

Recommendation: **A + B** — do A immediately as it is trivially safe, then B as the
structural fix. Fix `5505` with the same API swap while in the area.

### 4.2 Symptom 2 — window ownership

**Option A (preferred): give the overlay an owner and drop `WS_EX_TOPMOST`.** Pass
PowerPoint's frame HWND as `hWndParent` to `CreateWindowExW` for a `WS_POPUP` window
— that makes it an *owned* popup. Owned popups are always above their owner, are
hidden when the owner is minimized, and follow the owner in z-order, so they never
appear over an unrelated app. The polling gate then becomes a belt-and-braces
refinement rather than the sole defence.
*Pros:* eliminates the entire class, including the ≤150 ms latency window and the
stuck-`g_mutating` case.
*Cons:* the owner must be known at creation time; `EnsureWindow` (`5852`) currently
runs before `g_pptHwnd` is necessarily resolved, so creation must be deferred or the
window recreated when the host window changes (multiple document windows, monitor
moves). Owner and owned window must be on the same thread — they are, but this must
be asserted. Apply the same change to the app bar, editor, card, and theme menu.

**Option B: event-driven foreground tracking.** `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)`
to hide/show immediately instead of polling.
*Pros:* removes the latency window; smaller change than re-parenting.
*Cons:* still depends on the add-in's thread pumping messages, so it does not fix the
starved-`WM_TIMER` or stuck-`g_mutating` cases. Strictly weaker than Option A.

**Option C (do regardless): make `g_mutating` RAII.** Introduce a scope guard mirroring
`TickGuard` (`9046-9049`) and convert all ~20 manual set/clear pairs. This removes the
permanent-freeze failure mode independent of which option above is chosen.

---

## 5. How to verify — live only

The harness cannot see this bug. `ppappbarshot --trace` drives the overlay through
test seams in its own process; it never performs a real `WM_NCHITTEST`, never has a
real foreground window, and — decisively — it sets `g_cursorOverrideEnabled`, which
makes `Overlay.cpp:5040` read `g_cursorOverrideAltDown` instead of the buggy
`GetKeyState` call. **The harness executes a different line of code than production
on the exact expression that is at fault.** A green harness run is not evidence here.
(See `docs/session-recorder-spec.md` §1.)

### Step 0 — instrument the recorder first (no behaviour change)

Add to the snapshot `chrome` block emitted by `RecEmitSnapshot` (`Overlay.cpp:2855`):

- `altDownGetKeyState` — `::GetKeyState(VK_MENU) < 0`
- `altDownAsync` — `(::GetAsyncKeyState(VK_MENU) & 0x8000) != 0`
- `lastNcHitResult` — cached result of the last `WM_NCHITTEST` (`HTCLIENT` / `HTTRANSPARENT`)
- `hostActive` (`g_lastHostActive`), `shown` (`g_shown`), `overlaySwpCount`, `mutating`

Ship this before the fix. It converts the whole question into a single readable field.

### Step 1 — reproduce on the current build

1. Start a recording in live PowerPoint with a chart selected.
2. Hover the chart; confirm `WM_MOUSEMOVE` events appear.
3. **Alt+Tab to another app, then Alt+Tab back to PowerPoint.**
4. Without touching the keyboard again, click and drag a task bar three times.
5. Click a button on the bottom app bar.

Pass criteria for the diagnosis: after step 3, snapshots show
`altDownGetKeyState = true` while `altDownAsync = false`, `lastNcHitResult = HTTRANSPARENT`,
and `hostActive = true`; steps 4 produce zero `overlay` input events while step 5
still produces `appbar` input events. If the app bar is *also* dead, the root cause
in §2 is wrong — re-open the investigation at the z-order/ownership angle.

### Step 2 — verify the fix

Repeat the identical sequence on the patched build. Required evidence in
`events.jsonl`:

- `altDownGetKeyState` may still read true, but `lastNcHitResult` stays `HTCLIENT`.
- Each of the three drags produces `WM_LBUTTONDOWN` → `WM_MOUSEMOVE`* →
  `WM_LBUTTONUP`, a `gesture` start/update/commit triple, an `ownSel` event, and a
  changed `docDatesSignature` on the closing `doc` event.
- Repeat the whole sequence 5× in one session and 3× across cold PowerPoint restarts,
  since the bug is intermittent. Zero deaf phases required.

### Step 3 — verify symptom 2

1. With the overlay visible, Alt+Tab to a maximized window; screenshot immediately
   (within ~100 ms — a delayed screenshot hides the polling latency).
2. Minimize PowerPoint; screenshot.
3. Open a PowerPoint modal dialog (e.g. File → Options), Alt+Tab away, screenshot —
   this is the `WM_TIMER`-starved case the poll cannot cover.

Pass: no overlay chrome or app bar visible over the foreign window in any of the
three, including case 3.

---

## 6. What I could not determine

- **The actual coordinates of the phase-2 clicks.** There is no recorder channel for
  messages that never reach the WndProc, so I cannot rule out with certainty that the
  user clicked outside `g_chartScreenRect`. The last recorded move at (621, 451) was
  well inside it, and the user reports dragging task bars (which are inside it), so
  this is unlikely — but it is unproven.
- **Whether Alt was physically involved.** No keyboard state is recorded. §2.1 shows
  `altDown` is the only variable that can produce the observed routing; it does not
  independently confirm the value.
- **Which of the Alt-latch pathways** (Alt+Tab, KeyTips, modal loop) fired at
  t ≈ 7531 ms.
- **Whether symptom 2 was occurring during session B.** `hostActive` stayed true the
  whole time (§1.1), so the two symptoms were not simultaneously visible in this
  recording; the shared-trigger claim in §3 rests on source reading plus the user's
  separate report, not on a recording that shows both at once.
- **The two prior production incidents** referenced in the comments at `6701-6705`
  and `6779-6783` were not re-derived; I took the comments at face value as evidence
  that this class recurs.
