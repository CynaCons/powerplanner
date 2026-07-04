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

// Debug/test hook: the internally-selected element's id (task/milestone/row),
// or empty if nothing is internally selected. Drives the harness's OWNSEL
// stage assertion. Never touches COM.
const char* Overlay_GetSelectedIdForTest();

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

// ---- floating card editor (double-click TaskBody/Milestone) test hooks ----
// The card is a real top-level window (WS_EX_TOOLWINDOW), registered under
// this class name (mirrors Overlay.cpp's private kCardClass, kept in sync by
// hand since the class name itself has no other reason to be shared) so the
// harness's EDITOR stage can find it via FindWindowW without any other
// exported hook. Child control ids mirror Overlay.cpp's CardControlId enum
// so the harness can resolve the start-date field via GetDlgItem instead of
// walking children by class+z-order.
#define PP_CARD_EDITOR_CLASS L"PowerPlannerCardEditor"
enum OverlayCardControlIdForTest {
	OVERLAY_CARD_ID_LABEL_FOR_TEST = 101,
	OVERLAY_CARD_ID_START_FOR_TEST = 102,
	OVERLAY_CARD_ID_END_FOR_TEST = 103,
	OVERLAY_CARD_ID_PERCENT_FOR_TEST = 104,
	OVERLAY_CARD_ID_OK_FOR_TEST = 105,
};
