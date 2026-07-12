// N4: on-slide contextual overlay (think-cell style).
//
// A layered, click-through top-level window drawn OVER PowerPoint's slide-edit
// pane. Pixels are composed with GDI+ into a 32-bpp premultiplied-alpha DIB
// back buffer and pushed with UpdateLayeredWindow (per-pixel alpha — no color
// key), so washes/chrome can be genuinely translucent. A polling timer finds
// the CHART_ROOT on the active slide each tick and positions the overlay over
// the whole chart via DocumentWindow::PointsToScreenPixelsX/Y (which accounts
// for zoom + scroll). Selection chrome is painted only when a PowerPlanner
// shape is selected; the overlay hides only when no chart root is present.
#pragma once

#include <windows.h>

// Start the overlay: register the window class, create the (hidden) overlay,
// and begin the polling timer. `app` is the PowerPoint.Application (IDispatch).
void OverlayStart(IDispatch* app);

// Stop + tear down (kill timer, destroy window, release the app reference).
void OverlayStop();

// The overlay window handle (for tests/screen capture); NULL if not started.
HWND OverlayHwnd();

// ---- shared native-selection-change handler (SR-SMO-05 / ARC-07) -----------
// Result of Overlay_OnNativeSelectionChanged: what COM action the CALLER must
// take after the handler has updated the internal selection model. The handler
// is COM-free so it can be shared by BOTH the 150ms Tick poll (watchdog) and
// the Connect.cpp WindowSelectionChange COM sink (instant path). The caller
// resolves the current native selection via COM and performs any Unselect()
// itself. See the SINGLE-SELECTION CONTRACT comment in Overlay.cpp.
enum OverlayNativeSelAction {
	OVERLAY_NATIVE_SEL_NONE = 0,            // no COM action (CHART_ROOT / empty / foreign)
	OVERLAY_NATIVE_SEL_SUPPRESS_CHILD = 1,  // caller should Unselect() a chart child
};

// Resolve a native-selection observation into internal-model updates.
//   firstShapeKind    : PP_KIND of the FIRST selected shape, or "" when the
//                       selection is empty OR the first shape has no PP_KIND
//                       (a foreign, non-chart shape).
//   firstShapeId      : PP_ID of that shape ("" if none).
//   hasShapeSelection : true iff the native selection is a non-empty shape
//                       selection (ppSelectionShapes, count >= 1).
// Mirrors a TASK/MILESTONE child pick into the overlay's ownSel, clears ownSel
// when a foreign shape becomes selected (feeds UF-07 context reset + B1), and
// records the observed kind for the harness. Returns an OverlayNativeSelAction.
int Overlay_OnNativeSelectionChanged(const char* firstShapeKind, const char* firstShapeId, bool hasShapeSelection);

// Debug/test hook: the internally-selected element's id (task/milestone/row),
// or empty if nothing is internally selected. Drives the harness's OWNSEL
// stage assertion. Never touches COM.
const char* Overlay_GetSelectedIdForTest();

// Debug/test hook: the internally-selected element's KIND, as a stable int
// (the id-only hook above cannot disambiguate — e.g. a TASK and a TEXT could
// share the same id shape across independent tests). Mirrors g_ownSelKind's
// string values ("TASK"/"MILESTONE"/"ROW"/"MARKER"/"TEXT"/empty) without
// exposing the string itself, since this is consumed by the harness's
// TEXTELEM stage to assert the internal selection model resolved to "text"
// (not "task"/"milestone"/etc.) after a click on a PpText annotation. Never
// touches COM.
enum OverlaySelectedKindForTest {
	OVERLAY_SELKIND_NONE_FOR_TEST = 0,
	OVERLAY_SELKIND_TASK_FOR_TEST = 1,
	OVERLAY_SELKIND_MILESTONE_FOR_TEST = 2,
	OVERLAY_SELKIND_ROW_FOR_TEST = 3,
	OVERLAY_SELKIND_MARKER_FOR_TEST = 4,
	OVERLAY_SELKIND_TEXT_FOR_TEST = 5,
};
int Overlay_GetSelectedKindForTest();

// WM_HOTKEY ids (wParam values) the overlay registers via RegisterHotKey and
// dispatches on in its WM_HOTKEY handler (see Overlay.cpp's kHotkeySpecs /
// OverlayHotkeyId). Exposed here so the harness's KEYS stage can post
// WM_HOTKEY directly to the overlay hwnd with the matching id, bypassing the
// OS RegisterHotKey layer (which keys-probe.txt already proved delivers WM_
// HOTKEY to this NOACTIVATE overlay) to test the HANDLER in isolation.
enum OverlayHotkeyIdForTest {
	OVERLAY_HOTKEY_DELETE_FOR_TEST = 1,
	OVERLAY_HOTKEY_LEFT_FOR_TEST = 2,
	OVERLAY_HOTKEY_RIGHT_FOR_TEST = 3,
	OVERLAY_HOTKEY_SHIFT_LEFT_FOR_TEST = 4,
	OVERLAY_HOTKEY_SHIFT_RIGHT_FOR_TEST = 5,
};

// ---- input-neutral harness test seams --------------------------------------
// Production behavior is UNCHANGED (both are no-ops until a harness calls the
// setter below): every place Overlay.cpp reads the PHYSICAL cursor position
// (GetCursorPos for hover/WM_SETCURSOR) or the physical Alt keystate
// (WM_NCHITTEST's escape-hatch check) consults this override first. This lets
// the overlay-test.cpp harness fully define a gesture via POSTED WM_MOUSE*
// messages instead of also moving the real OS cursor / synthesizing real key
// events (which used to steal the user's mouse/keyboard while gates ran).
//
// `enabled=false` restores normal GetCursorPos/GetKeyState(VK_MENU) behavior.
// `altDown` only matters while `enabled` is true.
void Overlay_SetCursorPosOverrideForTest(bool enabled, POINT screenPt, bool altDown = false);

// Foreground/host-active override consulted by IsHostActiveForOverlayChrome,
// the hotkey registration's foreground-scoping check, and the Esc-clear fgPid
// check. mode: -1 = off (use the real GetForegroundWindow-based logic), 0 =
// force "host inactive" (as if some other app were foreground), 1 = force
// "host active" (as if PowerPoint/the overlay were foreground) — lets the
// harness exercise host-scoping (the SCOPE stage) without ever calling
// SetForegroundWindow on a real window.
void Overlay_SetHostActiveOverrideForTest(int mode);

// B1 (v2.6.1): keyboard-focus scope override for hotkey registration. mode:
// -1 = off (use the real GetGUIThreadInfo/GetFocus class-name check); 0 = force
// "focus NOT in the slide view" (as if the user were typing in the Notes pane /
// outline / ribbon); 1 = force "focus in the slide view". Lets the hotkey-scope
// harness prove that Delete/arrows are NOT registered (and the handlers no-op)
// when focus is elsewhere — without sending real keystrokes. Defaults to -1;
// while a host-active override is in effect and this is -1, focus is treated as
// satisfied so existing poll-only stages are unaffected.
void Overlay_SetSlideFocusOverrideForTest(int mode);

// Test hook: force the internal selection so a screenshot/visual harness can
// capture each app-bar context without simulating a click. kind is one of
// "TASK"/"MILESTONE"/"ROW"/"MARKER"/"TEXT"; a NULL/empty kind clears it. COM-free.
void Overlay_SelectForTest(const char* kind, const char* id);

// The bottom app-bar window handle (second layered chrome window); NULL if not
// started. Same lifecycle/visibility as the overlay (Ground Rule 9 / G4).
HWND OverlayAppBarHwnd();

// Test hook: fill *outScreenRect with the SCREEN rect of the app-bar button
// whose command id == cmd (an HtMenuCmd value). Returns false if no such button
// is currently laid out. Lets the harness post an input-neutral click at a
// button without hard-coded coordinates. Never touches COM.
bool OverlayAppBarButtonRectForTest(int cmd, RECT* outScreenRect);

// Test hook: last measured app-bar content width and clamped window width (pixels).
void OverlayAppBarContentWidthForTest(int* content, int* window);

// Test hook: mark the app-bar model/geometry dirty so the next ShowAppBar (or an
// immediate RenderAppBar if the bar is already shown) rebuilds/repaints after
// Overlay_SelectForTest drives a new selection context. Never touches COM.
void Overlay_InvalidateAppBarForTest();

// Test hook: rebuild + reposition + repaint the app bar immediately from the
// current ownSel and chart rect (same path as Tick's ShowAppBar). Lets harness
// captures match g_appBar without waiting for the 150ms timer. Never touches COM.
void Overlay_SyncAppBarForTest();

// Test hooks for S5 link mode (B7.1). Never touches COM.
bool Overlay_IsLinkModeForTest();
void Overlay_CancelLinkModeForTest();

// Test seam: drive an app-bar command (e.g. HtCmd_AddRowBelow, HtCmd_Rename, HtCmd_Scale*) 
// exactly as if the user clicked the button. Used by trace harnesses to perform
// ops and observe before/immed/after states. Never touches real input.
void Overlay_PerformAppBarCommandForTest(int cmd);

// Test seam: drive the row-gutter "+" hover quick-add exactly as if the user
// hovered the row and clicked the chip -- sets the hover-row state
// HandleHoverQuickAddTask reads (g_hoverRowId) to rowId, then invokes that
// same handler (adds a 5-day task at the visible-range center of the row and
// selects it). Used by trace harnesses to exercise the action without a real
// hover + click gesture. Never touches real input.
void Overlay_PerformHoverQuickAddForTest(const char* rowId);

// W3 test seam: dispatch the REAL window commit (the same CommitWindowGesture
// the WM_LBUTTONUP port-drag release calls: SetTimeWindow -> RebuildChart with
// one undo entry, M4 hidden-selection reset, zero-delta no-op vs the current
// document window). Lets trace harnesses commit deterministic ISO dates
// without deriving them from drag pixels. Touches COM (document mutation).
void Overlay_CommitWindowGestureForTest(const char* startISO, const char* endISO);

// Rich state dump for agent feedback loop: returns a JSON string (static buffer)
// with current chrome state for reports: own selection, row bands (with rects),
// taskCount/milestoneCount (from the hit snapshot), drag/gesture state, app bar
// visibility, visible chrome elements, etc.
// Never touches COM. Safe for harnesses to call after steps.
const char* Overlay_DumpChromeStateForTest();

// Render/SWP counters and compact window-state dump for gate harnesses.
// Never touches COM.
void Overlay_GetRenderCountersForTest(long* overlayPaints, long* appBarPaints,
                                      long* overlaySwp, long* appBarSwp);
void Overlay_DumpWindowStateForTest(char* buf, int bufLen);

// Test seam: open the custom theme menu at overlay client coords without blocking
// (harness captures while visible, then ThemeMenu_Dismiss). Never touches COM.
void Overlay_ShowContextMenuAtClientForTest(int clientX, int clientY);

// Test seam: open the document-context Settings popover without blocking so
// the harness can capture its themed surface; caller dismisses via
// ThemeMenu_Dismiss. Never mutates the document.
void Overlay_ShowSettingsMenuForTest();

// ---- floating card editor (double-click TaskBody/Milestone/Text) test hooks
// The card is a real top-level window (WS_EX_TOOLWINDOW), registered under
// this class name (mirrors Overlay.cpp's private kCardClass, kept in sync by
// hand since the class name itself has no other reason to be shared) so the
// harness's EDITOR stage can find it via FindWindowW without any other
// exported hook. Child control ids mirror Overlay.cpp's CardControlId enum
// so the harness can resolve the start-date field via GetDlgItem instead of
// walking children by class+z-order. OVERLAY_CARD_ID_DELETE_FOR_TEST is only
// shown in TEXT mode (label field + delete button, no dates/percent/swatches).
#define PP_CARD_EDITOR_CLASS L"PowerPlannerCardEditor"
#define PP_THEME_MENU_CLASS L"PowerPlannerThemeMenu"
enum OverlayCardControlIdForTest {
	OVERLAY_CARD_ID_LABEL_FOR_TEST = 101,
	OVERLAY_CARD_ID_START_FOR_TEST = 102,
	OVERLAY_CARD_ID_END_FOR_TEST = 103,
	OVERLAY_CARD_ID_PERCENT_FOR_TEST = 104,
	OVERLAY_CARD_ID_OK_FOR_TEST = 105,
	OVERLAY_CARD_ID_DELETE_FOR_TEST = 106,
};
