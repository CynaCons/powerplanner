#include "pch.h"
#include "Overlay.h"
#include "GanttBuilder.h"
#include "GanttJson.h"
#include "GanttOps.h"
#include "GanttHitTest.h"
#include "GanttLayout.h"
// GDI+ headers need the min/max macros that pch.h's NOMINMAX removes. Define
// them only for the header, then drop them so std::min/std::max keep working.
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define PP_OVERLAY_UNDEF_MIN
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define PP_OVERLAY_UNDEF_MAX
#endif
#include <gdiplus.h>
#ifdef PP_OVERLAY_UNDEF_MIN
#undef min
#undef PP_OVERLAY_UNDEF_MIN
#endif
#ifdef PP_OVERLAY_UNDEF_MAX
#undef max
#undef PP_OVERLAY_UNDEF_MAX
#endif
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// ---- module state ----------------------------------------------------------

namespace {
const wchar_t* kClass = L"PowerPlannerOverlay";
const wchar_t* kEditorClass = L"PowerPlannerInlineEditor";
const COLORREF ACCENT = RGB(26, 115, 232); // Material primary blue
const COLORREF HANDLE_INNER = RGB(138, 180, 248);
const COLORREF SURFACE = RGB(255, 255, 255);
const COLORREF SURFACE_VARIANT = RGB(241, 243, 244);
const COLORREF TEXT = RGB(60, 64, 67);
// ---- DPI-scaled chrome metrics ---------------------------------------------
// All chrome pixel constants below are expressed at the 96-DPI (100% Windows
// scaling) baseline and scaled per-tick through HtScalePx (MulDiv(base,dpi,96)
// — see GanttHitTest.h/.cpp, which is COM-free so it's shared with the ops
// harness). The overlay runs IN-PROCESS in POWERPNT.EXE, a per-monitor-DPI-
// aware process, so COM coordinates (PointsToScreenPixels, GetCursorPos)
// already come back in PowerPoint's DPI context and need no scaling — only
// OUR chrome's hardcoded pixel metrics do, or they stay 96-DPI-sized (a few mm
// on screen) and become unusably small at 150-200%.
const int kBaseInfl = 5;                    // frame inset from shape edge (px)
const int kBaseBadgeH = 20;                 // badge strip height (px)
const int kBaseToolbarH = 28;               // floating action toolbar height (px)
const int BUTTON_COUNT = 4;                 // not a pixel metric: unscaled
const int kBaseRowInsertButton = 16;
const int kBaseButtonW = 32;
const int kBaseButtonH = 20;
const int kBaseButtonGap = 4;
const int kBaseGripSize = 16;
const int kBaseDragThresholdPx = 4;
const int kBaseTooltipPad = 5;
const Gdiplus::REAL kBaseTooltipFontPx = 10.0f;
const Gdiplus::REAL kBaseBadgeFontPx = 11.0f;
const Gdiplus::REAL kBaseButtonFontPx = 12.0f;
const BYTE HOVER_WASH_ALPHA = 28;           // translucent accent wash over hovered row

// ---- floating card editor metrics (96-DPI baseline, scaled via Scale()) ----
const int kBaseCardW = 260;
const int kBaseCardPad = 10;
const int kBaseCardRowH = 22;
const int kBaseCardRowGap = 6;
const int kBaseCardLabelW = 60;   // field-label column width
const int kBaseCardSwatchSize = 22;
const int kBaseCardSwatchGap = 6;
const int kBaseCardOkW = 60;
const int kBaseCardOkH = 24;

// Current DPI-scaled values, recomputed by UpdateDpiScaledMetrics() whenever
// the overlay's window DPI changes (or on first use). Default to the 96-DPI
// (100%) baseline so any code path that runs before the first DPI probe still
// behaves exactly as before this unit.
int g_dpi = 96;
int INFL = kBaseInfl;
int BADGE_H = kBaseBadgeH;
int TOOLBAR_H = kBaseToolbarH;
int ROW_INSERT_BUTTON = kBaseRowInsertButton;
int g_buttonW = kBaseButtonW;
int g_buttonH = kBaseButtonH;
int g_buttonGap = kBaseButtonGap;
int GRIP_SIZE = kBaseGripSize;
int kDragThresholdPx = kBaseDragThresholdPx;
int g_tooltipPad = kBaseTooltipPad;
Gdiplus::REAL g_tooltipFontPx = kBaseTooltipFontPx;
Gdiplus::REAL g_badgeFontPx = kBaseBadgeFontPx;
Gdiplus::REAL g_buttonFontPx = kBaseButtonFontPx;

PowerPoint::_ApplicationPtr g_app;
HWND     g_hwnd = NULL;
HINSTANCE g_inst = NULL;
UINT_PTR g_timer = 0;
bool     g_shown = false;
bool     g_mutating = false;
bool     g_inTick = false;
ULONG_PTR g_gdiplusToken = 0;
// PowerPoint's main window, cached each Tick() from the Application COM object.
// Used to forward input (e.g. mouse wheel) that the overlay captures but does
// not itself handle, so Ctrl+wheel zoom/scroll still reaches PowerPoint.
HWND     g_pptHwnd = NULL;

// 32-bpp premultiplied-alpha back buffer, pushed via UpdateLayeredWindow.
HDC     g_bufDc = NULL;
HBITMAP g_bufBmp = NULL;
HGDIOBJ g_bufOld = NULL;
void*   g_bufBits = nullptr;
int     g_bufW = 0;
int     g_bufH = 0;
std::string g_lastKind;       // last shown PP_KIND (to log on change)
std::string g_selId;          // current selected PowerPlanner PP_ID
std::string g_selKind;        // current selected PowerPlanner PP_KIND
std::string g_chartProj;      // current CHART_ROOT PP_PROJ payload
// Most recent PowerPoint-native selection of a chart child that Tick()
// suppressed (Unselect()'d). Mirrored into the internal selection model
// below, then cleared.
std::string g_suppressedKind;
std::string g_suppressedId;

// ---- internal (own) selection model ----------------------------------------
// The source of truth for "what is selected" no longer needs PowerPoint's
// native Selection object. Kind is one of "TASK", "MILESTONE", "ROW" (row
// band), or empty (nothing selected). g_selId/g_selKind/g_hasSelectionChrome/
// g_selScreenRect (above) are still what PaintOverlay/HandleToolbarButton
// read, but they are now derived FROM this internal state each tick instead
// of from PowerPoint's Selection.
std::string g_ownSelKind;
std::string g_ownSelId;

// Left-button gesture tracking (down -> up), used to distinguish a click
// (select) from a drag and to own mouse capture for the duration of the
// gesture.
bool g_captureActive = false;
POINT g_mouseDownPt = {};
// kDragThresholdPx (DPI-scaled) is declared with the other chrome metrics above.

// ---- drag-move-resize gesture state -----------------------------------------
// A gesture is anchored on WM_LBUTTONDOWN over a draggable hit zone (task body
// or edge, or a milestone). g_dragActive latches true once the pointer moves
// beyond kDragThresholdPx (never re-derived from up-vs-down endpoints — see
// WM_MOUSEMOVE). g_gestureActive is broader: true for the whole capture
// lifetime of a draggable-zone gesture (from LBUTTONDOWN, even before the
// threshold is crossed) so Tick() knows to suppress selection-chrome sync
// without ever early-returning (the ghost still needs to paint every tick).
bool g_gestureActive = false;
bool g_dragActive = false;
enum class DragKind { None, TaskBody, TaskEdgeL, TaskEdgeR, Milestone, Create };
DragKind g_dragKind = DragKind::None;
std::string g_dragId;          // task or milestone PP_ID being dragged
RECT g_dragAnchorRect = {};    // original screen rect of the dragged item at gesture start
std::string g_dragOrigStart;   // original ISO start date (task) or date (milestone)
std::string g_dragOrigEnd;     // original ISO end date (task); empty for milestones
float g_dragPxPerDay = 0.0f;   // SCREEN pixels-per-day for this gesture (see ComputeDragPxPerDay)
long g_dragDeltaDays = 0;      // current candidate day delta (updated on move)
POINT g_dragLastPt = {};       // last client point seen during the drag

// Row-reassign (vertical) part of a TaskBody drag: the row band id under the
// current drag point, and its screen rect. Recomputed every WM_MOUSEMOVE from
// the (per-tick-cached) g_rowBands; empty g_dragTargetRowId means "no row
// band under the pointer right now" (e.g. above/below the chart), in which
// case the vertical retarget is simply skipped for that move (horizontal
// ghost/date math is unaffected). Only meaningful when g_dragKind ==
// DragKind::TaskBody; edges/milestones never retarget rows.
std::string g_dragOrigRowId;    // task's row at gesture start
std::string g_dragTargetRowId;  // row band currently under the pointer (may equal orig)
RECT g_dragTargetRowRect = {};  // screen rect of that row band (for the ghost's y-position)

// ---- create-on-EmptyCell gesture state --------------------------------------
// Started on WM_LBUTTONDOWN over an EmptyCell(rowId) hit. Reuses g_dragKind ==
// DragKind::Create / g_gestureActive / g_dragActive / g_dragLastPt (the
// existing threshold-latch + repaint-on-move plumbing in UpdateDragGesture)
// but tracks its own anchor/target day pair rather than a day DELTA, because a
// create-drag has no "original" task to offset — it defines a brand new span
// from the down point to the current point (and can extend either direction).
std::string g_createRowId;     // row band the create gesture is anchored in
long g_createAnchorDay = 0;    // ISO day number under the down-point
long g_createCurrentDay = 0;   // ISO day number under the current point (updated on move)
std::wstring g_badge = L"PowerPlanner";
RECT g_buttonRects[BUTTON_COUNT] = {};
RECT g_frameRect = {};
RECT g_chartScreenRect = {};
RECT g_selScreenRect = {};
int g_windowOriginX = 0;
int g_windowOriginY = 0;
bool g_buttonsValid = false;
bool g_hasSelectionChrome = false;

struct RowBand {
	std::string rowId;
	RECT screenRect;
	int screenLeftGutter;
};

std::vector<RowBand> g_rowBands;

// Semantic hit-test snapshot (pure, COM-free — see GanttHitTest.h). Rebuilt by
// BuildRowBands' child walk; reused across ticks while the chart screen rect
// and child count are unchanged so the per-tick COM cost stays bounded.
HtSnapshot g_hitSnapshot;
RECT g_hitCacheChartRect = {};
long g_hitCacheChildCount = -1;
// Last semantic hit from a mouse-down on the overlay. The selection-model unit
// will consume this; for now it is only stored (and logged on change).
HtHit g_lastHit;

// 'Move chart' grip in the chrome (top-right): clicking it selects the
// CHART_ROOT group via COM so the user can still move/delete the whole chart
// natively even though the overlay now captures every chart click.
RECT g_gripRect = {};
bool g_gripValid = false;
// GRIP_SIZE (DPI-scaled) is declared with the other chrome metrics above.

std::string g_hoverRowId;
RECT g_hoverBandRect = {};
RECT g_hoverInsertRect = {};
bool g_hoverInsertValid = false;
bool g_lastLeftButtonDown = false;

struct EditRegion {
	std::string kind;
	std::string id;
	RECT screenRect;
};

std::vector<EditRegion> g_editRegions;
HWND g_editorHwnd = NULL;
HWND g_editHwnd = NULL;
WNDPROC g_oldEditProc = NULL;
bool g_editClosing = false;
std::string g_editKind;
std::string g_editId;

// ---- floating card editor (double-click on TaskBody/Milestone) -------------
// A second, richer top-level focusable window (same "separate top-level
// window hosting real Win32 child controls" pattern as the inline title/row-
// label editor above — the NOACTIVATE overlay itself can never host a
// focused control). Unlike the inline editor (single EDIT box, commits on
// kill-focus), the card holds several fields + 8 color swatches and commits
// them all in ONE undo entry when the user hits Enter/OK; losing focus or
// Esc CANCELS instead (per the task spec) since a stray click on another
// field mid-edit should not silently commit half-typed values.
const wchar_t* kCardClass = L"PowerPlannerCardEditor";
const wchar_t* kCardSwatchClass = L"PowerPlannerCardSwatch";

// Fixed child control ids (GetDlgItem/EnumChildWindows-friendly; also used by
// the harness's EDITOR stage to locate the start-date field deterministically
// without walking children by class+z-order).
enum CardControlId {
	CARD_ID_LABEL = 101,
	CARD_ID_START = 102,
	CARD_ID_END = 103,
	CARD_ID_PERCENT = 104,
	CARD_ID_OK = 105,
	CARD_ID_SWATCH_BASE = 110, // 110..117 for the 8 color swatches
};
constexpr int kCardSwatchCount = 8;

// 8-swatch palette (RGB). Reuses Scene.h's Material primary/milestone tokens
// as the first two entries (so the default task/milestone colors are always
// one click away) and rounds out the rest with a small, calm Material-ish set
// distinct enough to tell apart at swatch size. Scene.h itself has no
// ready-made 8-color list (MaterialLight() is a themed token STRUCT, not a
// palette array), so these are otherwise-unused literal hex values.
const COLORREF kCardSwatches[kCardSwatchCount] = {
	RGB(0x1A, 0x73, 0xE8), // Material blue (primary/task default)
	RGB(0xF9, 0xAB, 0x00), // amber (milestone default)
	RGB(0xD9, 0x30, 0x25), // red
	RGB(0x18, 0x8E, 0x3C), // green
	RGB(0x9C, 0x27, 0xB0), // purple
	RGB(0x00, 0x97, 0xA7), // teal
	RGB(0xF4, 0x71, 0x1B), // orange
	RGB(0x5F, 0x63, 0x68), // neutral grey
};

HWND g_cardHwnd = NULL;         // top-level card window (WS_EX_TOOLWINDOW)
HWND g_cardLabelHwnd = NULL;
HWND g_cardStartHwnd = NULL;
HWND g_cardEndHwnd = NULL;
HWND g_cardPercentHwnd = NULL;
HWND g_cardOkHwnd = NULL;
HWND g_cardSwatchHwnd[kCardSwatchCount] = {};
WNDPROC g_oldCardFieldProc = NULL; // shared subclass proc for the 4 EDIT children
bool g_cardClosing = false;
std::string g_cardKind;   // "TASK" | "MILESTONE"
std::string g_cardId;
int g_cardSelectedSwatch = -1; // index into kCardSwatches, or -1 = no change
std::string g_cardOrigColor;   // color value read at open time (for "no change" baseline)
bool g_cardInvalid = false;    // true while the red-border/invalid state is shown

enum OverlayButton {
	BTN_ADD = 0,
	BTN_DEL = 1,
	BTN_PCT_MINUS = 2,
	BTN_PCT_PLUS = 3
};

// ---- keyboard hotkeys (Delete/Left/Right (+/-1 day), Shift+Left/Right (+/-7))
// -----------------------------------------------------------------------------
// PROBE VERDICT (native/build/keys-probe.txt, OPTION C): RegisterHotKey
// delivers WM_HOTKEY to this NOACTIVATE overlay even though it never has
// keyboard focus, but registered keys are stolen SYSTEM-WIDE for as long as
// they're registered (every process, not just PowerPoint) and cleanly return
// to normal on UnregisterHotKey. So registration is scoped as tightly as
// possible: only while (a) the internal selection is a TASK/MILESTONE (there
// is something to Delete/Nudge) AND (b) PowerPoint itself is the foreground
// app (so the theft window is limited to "user is actually looking at
// PowerPoint") AND (c) no gesture/mutation/inline-edit is in progress
// (mid-drag Delete would be nonsensical, and re-registering while g_mutating
// could race the commit). Tick() re-evaluates this every 150ms tick and
// registers/unregisters on the true<->false TRANSITION only (not every tick)
// so a steady selected-task+foreground state doesn't thrash RegisterHotKey.
// NOTE: these values are mirrored in Overlay.h's OverlayHotkeyIdForTest for
// the harness's KEYS stage (which posts WM_HOTKEY directly to the overlay
// hwnd, bypassing RegisterHotKey, to test this handler in isolation) — keep
// both enums in sync if either changes.
enum OverlayHotkeyId {
	HOTKEY_DELETE = 1,
	HOTKEY_LEFT = 2,
	HOTKEY_RIGHT = 3,
	HOTKEY_SHIFT_LEFT = 4,
	HOTKEY_SHIFT_RIGHT = 5,
};

struct HotkeySpec {
	int id;
	UINT mods;
	UINT vk;
	const wchar_t* name; // for OvLog on registration failure
};

// fsModifiers deliberately omits MOD_NOREPEAT: held Left/Right should repeat
// (matching PowerPoint's own arrow-key nudge feel) exactly like the existing
// GetAsyncKeyState-polled Esc handling has no repeat-suppression either.
const HotkeySpec kHotkeySpecs[] = {
	{ HOTKEY_DELETE,       0,          VK_DELETE, L"Delete" },
	{ HOTKEY_LEFT,         0,          VK_LEFT,   L"Left" },
	{ HOTKEY_RIGHT,        0,          VK_RIGHT,  L"Right" },
	{ HOTKEY_SHIFT_LEFT,   MOD_SHIFT,  VK_LEFT,   L"Shift+Left" },
	{ HOTKEY_SHIFT_RIGHT,  MOD_SHIFT,  VK_RIGHT,  L"Shift+Right" },
};
constexpr int kHotkeyCount = sizeof(kHotkeySpecs) / sizeof(kHotkeySpecs[0]);

// Whether each hotkey id is CURRENTLY registered (per-key, since an
// individual RegisterHotKey call can fail — e.g. another app already owns
// that combo — and the rest should still work per the task spec's "degrade
// gracefully" requirement).
bool g_hotkeysRegistered[kHotkeyCount] = {};
// Whether the group as a whole is in the "registered" state, i.e. the last
// evaluated shouldRegister was true. Tracked separately from
// g_hotkeysRegistered (which is per-key) so Tick() only calls Register/
// UnregisterHotKey on an actual state TRANSITION.
bool g_hotkeysActive = false;

// Forward
void PaintOverlay(Gdiplus::Graphics& g, int W, int H);
void RenderOverlay();
void RequestOverlayRepaint();
void HandleToolbarButton(int button);
void HandleHoverInsertRow();
void RebuildChart(PpDocument& doc, const std::string& selectId);
void OvLog(const wchar_t* msg);
void StartNewUndoEntryIfPossible();
void OpenInlineEditor(const EditRegion& region);
void CommitInlineEdit();
void CancelInlineEdit();
void OpenCardEditor(const std::string& kind, const std::string& id, const RECT& anchorScreenRect);
void CommitCardEdit();
void CancelCardEdit();
void CloseCardEditor();
bool IsEditSessionActive();
void CancelDragGesture();
void CommitDragGesture(DragKind kind, const std::string& id, long deltaDays, const std::string& targetRowId);
void CommitCreateGesture(const std::string& rowId, long startDay, long endDay);
float ComputeEmptyCellPxPerDay();
void ShowContextMenuForHit(const HtHit& hit, POINT clientPt);
LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK InlineEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK CardWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK CardFieldProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
void UpdateHotkeyRegistration();
void UnregisterAllHotkeys();
void HandleHotkeyDelete();
void HandleHotkeyNudge(long deltaDays);

std::string Narrow(const wchar_t* w) {
	if (!w || !*w) return "";
	int len = (int)::wcslen(w);
	int n = (int)::WideCharToMultiByte(CP_UTF8, 0, w, len, NULL, 0, NULL, NULL);
	std::string s(n, '\0');
	if (n > 0) ::WideCharToMultiByte(CP_UTF8, 0, w, len, &s[0], n, NULL, NULL);
	return s;
}

std::wstring Widen(const std::string& s) {
	if (s.empty()) return L"";
	int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
	std::wstring w(n, L'\0');
	if (n > 0) ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
	return w;
}

const PpTask* FindTask(const PpDocument& doc, const std::string& id) {
	for (const auto& task : doc.tasks) {
		if (task.id == id) return &task;
	}
	return nullptr;
}

std::string FirstRowId(const PpDocument& doc) {
	return doc.rows.empty() ? "" : doc.rows.front().id;
}

std::string RowForSelection(const PpDocument& doc, const std::string& kind, const std::string& id) {
	if (kind == "TASK" || kind == "TASK_PROGRESS") {
		if (const PpTask* task = FindTask(doc, id)) return task->rowId;
	}
	if (kind == "ROW_LABEL") {
		for (const auto& row : doc.rows) {
			if (row.id == id) return row.id;
		}
	}
	return FirstRowId(doc);
}

void DefaultTaskDates(const PpDocument& doc, const std::string& rowId, const std::string& selectedTaskId, std::string& start, std::string& end) {
	if (const PpTask* selected = FindTask(doc, selectedTaskId)) {
		start = selected->start;
		end = selected->end;
		return;
	}
	for (const auto& task : doc.tasks) {
		if (task.rowId == rowId) {
			start = task.start;
			end = task.end;
			return;
		}
	}
	if (!doc.tasks.empty()) {
		start = doc.tasks.front().start;
		end = doc.tasks.front().end;
		return;
	}
	start = "2026-01-01";
	end = "2026-01-08";
}

bool IsTaskKind(const std::string& kind) {
	return kind == "TASK" || kind == "TASK_PROGRESS";
}

void ClearSelectionState() {
	g_selId.clear();
	g_selKind.clear();
	g_hasSelectionChrome = false;
	g_buttonsValid = false;
	::SetRectEmpty(&g_selScreenRect);
	::SetRectEmpty(&g_frameRect);
}

void ClearOwnSelection() {
	g_ownSelKind.clear();
	g_ownSelId.clear();
}

void SetOwnSelection(const std::string& kind, const std::string& id) {
	if (id.empty()) { ClearOwnSelection(); return; }
	g_ownSelKind = kind;
	g_ownSelId = id;
}

bool SameRect(const RECT& a, const RECT& b) {
	return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

bool SameSelectionState(bool hasSelection, const RECT& selRect, const std::string& selId, const std::string& selKind) {
	return g_hasSelectionChrome == hasSelection && SameRect(g_selScreenRect, selRect) && g_selId == selId && g_selKind == selKind;
}

HtRect ToHtRect(const RECT& r) {
	return { r.left, r.top, r.right, r.bottom };
}

void InvalidateHitSnapshot() {
	g_hitSnapshot = HtSnapshot{};
	::SetRectEmpty(&g_hitCacheChartRect);
	g_hitCacheChildCount = -1;
}

// Scale a 96-DPI ("100%") chrome pixel constant to the overlay's current DPI
// (g_dpi). Thin wrapper around GanttHitTest.h's HtScalePx (MulDiv(base,dpi,96)
// — COM-free, shared with the ops harness) so every chrome metric below goes
// through the same rounding rule.
int Scale(int basePx) {
	return HtScalePx(basePx, g_dpi);
}

Gdiplus::REAL ScaleF(Gdiplus::REAL basePx) {
	return (Gdiplus::REAL)HtScalePx((int)std::lround((double)basePx * 4.0), g_dpi) / 4.0f;
}

// Recompute every DPI-scaled chrome metric from g_dpi. Called whenever g_dpi
// changes (see UpdateDpiForWindow) so LayoutToolbarButtons/LayoutGrip/
// LayoutHoverInsertHotspot/PaintOverlay/ShowOverlayForChartRect and the drag
// threshold all pick up the new scale together, and the hit snapshot (whose
// edgeBandPx mirrors kDragThresholdPx's underlying kHtEdgePx scale) is
// invalidated so the next tick's BuildRowBands rebuilds it at the new DPI.
void UpdateDpiScaledMetrics() {
	INFL = Scale(kBaseInfl);
	BADGE_H = Scale(kBaseBadgeH);
	TOOLBAR_H = Scale(kBaseToolbarH);
	ROW_INSERT_BUTTON = Scale(kBaseRowInsertButton);
	g_buttonW = Scale(kBaseButtonW);
	g_buttonH = Scale(kBaseButtonH);
	g_buttonGap = Scale(kBaseButtonGap);
	GRIP_SIZE = Scale(kBaseGripSize);
	kDragThresholdPx = Scale(kBaseDragThresholdPx);
	g_tooltipPad = Scale(kBaseTooltipPad);
	g_tooltipFontPx = ScaleF(kBaseTooltipFontPx);
	g_badgeFontPx = ScaleF(kBaseBadgeFontPx);
	g_buttonFontPx = ScaleF(kBaseButtonFontPx);
	g_hitSnapshot.edgeBandPx = Scale((int)kHtEdgePx);
	InvalidateHitSnapshot();
	g_buttonsValid = false;
}

// Probe hwnd's current DPI (per-monitor: differs across monitors and can
// change mid-session if the window is dragged to a different-DPI monitor or
// the user changes scaling live). Falls back to 96 if GetDpiForWindow is
// unavailable (older Windows) — dynamically resolved since the SDK/import lib
// used here may predate it on some build machines. Returns true if g_dpi
// changed (caller then calls UpdateDpiScaledMetrics() + repaints/relayouts).
bool UpdateDpiForWindow(HWND hwnd) {
	int newDpi = 96;
	if (hwnd) {
		static UINT(WINAPI * pGetDpiForWindow)(HWND) = []() {
			HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
			return user32 ? (UINT(WINAPI*)(HWND))::GetProcAddress(user32, "GetDpiForWindow") : nullptr;
		}();
		if (pGetDpiForWindow) {
			UINT dpi = pGetDpiForWindow(hwnd);
			if (dpi > 0) newDpi = (int)dpi;
		}
	}
	if (newDpi != g_dpi) {
		g_dpi = newDpi;
		return true;
	}
	return false;
}

void ClearHoverState() {
	g_hoverRowId.clear();
	::SetRectEmpty(&g_hoverBandRect);
	::SetRectEmpty(&g_hoverInsertRect);
	g_hoverInsertValid = false;
}

const EditRegion* EditRegionFromScreenPoint(POINT pt) {
	for (const auto& region : g_editRegions) {
		if (::PtInRect(&region.screenRect, pt)) return &region;
	}
	return nullptr;
}

const EditRegion* EditRegionFromClientPoint(POINT pt) {
	POINT screenPt = { pt.x + g_windowOriginX, pt.y + g_windowOriginY };
	return EditRegionFromScreenPoint(screenPt);
}

void LayoutToolbarButtons(int width, int height) {
	g_buttonsValid = false;
	for (int i = 0; i < BUTTON_COUNT; ++i) ::SetRectEmpty(&g_buttonRects[i]);
	// The mini-toolbar binds to the internal selection: visible for a task or
	// milestone. ROW selection (new: a row band with no task/milestone under
	// it) does not show the toolbar. The CHART_ROOT chrome path is unrelated
	// to the internal model and keeps its pre-existing layout behavior.
	bool toolbarEligible = g_hasSelectionChrome && g_selKind != "ROW";
	if (!toolbarEligible || ::IsRectEmpty(&g_frameRect) || width <= 0 || height <= 0) return;

	const int buttonW = g_buttonW;
	const int buttonH = g_buttonH;
	const int gap = g_buttonGap;
	const int totalW = BUTTON_COUNT * buttonW + (BUTTON_COUNT - 1) * gap;
	const int pad6 = Scale(6);
	const int pad3 = Scale(3);
	int x = g_frameRect.left + pad6;
	int y = g_frameRect.bottom + INFL;
	if (x + totalW + pad6 > width) x = std::max(INFL, width - totalW - INFL - pad6);
	if (x < INFL) x = INFL;
	if (y + buttonH + pad3 > height) y = height - TOOLBAR_H + (TOOLBAR_H - buttonH) / 2;
	if (y < INFL) y = INFL;
	for (int i = 0; i < BUTTON_COUNT; ++i) {
		g_buttonRects[i] = { x + i * (buttonW + gap), y, x + i * (buttonW + gap) + buttonW, y + buttonH };
	}
	g_buttonsValid = true;
}

int ButtonFromClientPoint(POINT pt) {
	if (!g_buttonsValid) return -1;
	for (int i = 0; i < BUTTON_COUNT; ++i) {
		if (::PtInRect(&g_buttonRects[i], pt)) return i;
	}
	return -1;
}

// 'Move chart' grip: pinned to the window's top-RIGHT, inside the badge strip
// (the strip above the chart), so it is always reachable. Top-right (rather
// than top-left) avoids colliding with the "PowerPlanner" badge chip, which
// is drawn at the frame's top-left and can extend close to frame.left.
void LayoutGrip(int width, int height) {
	g_gripValid = false;
	::SetRectEmpty(&g_gripRect);
	const int pad2 = Scale(2);
	if (width < GRIP_SIZE + INFL * 2 || height < BADGE_H) return;
	g_gripRect = { width - INFL - 1 - GRIP_SIZE, pad2, width - INFL - 1, pad2 + GRIP_SIZE };
	g_gripValid = true;
}

bool GripFromClientPoint(POINT pt) {
	return g_gripValid && ::PtInRect(&g_gripRect, pt);
}

void LayoutHoverInsertHotspot() {
	g_hoverInsertValid = false;
	::SetRectEmpty(&g_hoverInsertRect);
	if (g_hoverRowId.empty() || ::IsRectEmpty(&g_hoverBandRect)) return;
	if (::GetKeyState(VK_LBUTTON) & 0x8000) return;

	int bandTop = g_hoverBandRect.top - g_windowOriginY;
	int bandBottom = g_hoverBandRect.bottom - g_windowOriginY;
	int cy = (bandTop + bandBottom) / 2;
	const int pad6 = Scale(6);
	const int pad4 = Scale(4);
	int left = g_chartScreenRect.left - g_windowOriginX + pad6;
	for (const auto& band : g_rowBands) {
		if (band.rowId == g_hoverRowId) {
			left = std::max((int)(g_chartScreenRect.left - g_windowOriginX + pad4), band.screenLeftGutter - g_windowOriginX - ROW_INSERT_BUTTON - pad4);
			break;
		}
	}
	g_hoverInsertRect = { left, cy - ROW_INSERT_BUTTON / 2, left + ROW_INSERT_BUTTON, cy + ROW_INSERT_BUTTON / 2 };
	g_hoverInsertValid = true;
}

bool HoverInsertFromClientPoint(POINT pt) {
	if (!g_hoverInsertValid) LayoutHoverInsertHotspot();
	return g_hoverInsertValid && ::PtInRect(&g_hoverInsertRect, pt);
}

void NormalizeRect(RECT& rc) {
	if (rc.left > rc.right) std::swap(rc.left, rc.right);
	if (rc.top > rc.bottom) std::swap(rc.top, rc.bottom);
}

void UpdateSelectionFrameFromScreen() {
	::SetRectEmpty(&g_frameRect);
	g_buttonsValid = false;
	if (!g_hasSelectionChrome || ::IsRectEmpty(&g_selScreenRect)) return;
	g_frameRect = {
		g_selScreenRect.left - g_windowOriginX,
		g_selScreenRect.top - g_windowOriginY,
		g_selScreenRect.right - g_windowOriginX,
		g_selScreenRect.bottom - g_windowOriginY
	};
	if (g_frameRect.right < g_frameRect.left + 8) g_frameRect.right = g_frameRect.left + 8;
	if (g_frameRect.bottom < g_frameRect.top + 8) g_frameRect.bottom = g_frameRect.top + 8;
}

PowerPoint::ShapePtr FindChartRoot(PowerPoint::_SlidePtr slide) {
	if (!slide) return nullptr;
	PowerPoint::ShapesPtr shapes = slide->GetShapes();
	if (!shapes) return nullptr;
	long n = shapes->GetCount();
	for (long i = 1; i <= n; ++i) {
		PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
		_bstr_t kind = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
		if (kind.length() && Narrow((const wchar_t*)kind) == "CHART_ROOT") return sh;
	}
	return nullptr;
}

// Single per-tick child walk: builds the row bands, inline-edit regions AND
// the semantic hit-test snapshot. When the chart screen rect and child count
// are unchanged since the last walk, the cached results are reused so the
// per-tick COM cost stays bounded (one GetGroupItems + GetCount call).
void BuildRowBands(PowerPoint::ShapePtr chart, PowerPoint::DocumentWindowPtr win) {
	if (!chart || !win) {
		g_rowBands.clear();
		g_editRegions.clear();
		InvalidateHitSnapshot();
		return;
	}
	try {
		PowerPoint::GroupShapesPtr items = chart->GetGroupItems();
		if (!items) {
			g_rowBands.clear();
			g_editRegions.clear();
			InvalidateHitSnapshot();
			return;
		}
		long n = items->GetCount();
		if (n == g_hitCacheChildCount && SameRect(g_chartScreenRect, g_hitCacheChartRect)) {
			return; // cache hit: reuse g_rowBands / g_editRegions / g_hitSnapshot
		}
		g_rowBands.clear();
		g_editRegions.clear();
		g_hitSnapshot = HtSnapshot{};
		g_hitSnapshot.chartRect = ToHtRect(g_chartScreenRect);
		for (long i = 1; i <= n; ++i) {
			PowerPoint::ShapePtr ch = items->Item(_variant_t(i));
			_bstr_t kind = ch->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (!kind.length()) continue;
			std::string kindStr = Narrow((const wchar_t*)kind);
			// TASK_PROGRESS is the inner fill of a TASK bar — the TASK rect
			// already covers it, so it does not participate in hit-testing.
			if (kindStr != "ROW_LABEL" && kindStr != "TITLE" && kindStr != "TASK" && kindStr != "MILESTONE") continue;
			float left = ch->GetLeft(), top = ch->GetTop(), w = ch->GetWidth(), h = ch->GetHeight();
			RECT rr = {
				win->PointsToScreenPixelsX(left),
				win->PointsToScreenPixelsY(top),
				win->PointsToScreenPixelsX(left + w),
				win->PointsToScreenPixelsY(top + h)
			};
			NormalizeRect(rr);
			if (rr.bottom <= rr.top) continue;
			if (kindStr == "TITLE") {
				g_editRegions.push_back({ "TITLE", "", rr });
				g_hitSnapshot.items.push_back({ HtItemKind::Title, "", ToHtRect(rr) });
				continue;
			}
			_bstr_t id = ch->GetTags()->Item(_bstr_t(L"PP_ID"));
			std::string idStr = Narrow((const wchar_t*)id);
			if (idStr.empty()) continue;
			if (kindStr == "TASK") {
				g_hitSnapshot.items.push_back({ HtItemKind::Task, idStr, ToHtRect(rr) });
				continue;
			}
			if (kindStr == "MILESTONE") {
				g_hitSnapshot.items.push_back({ HtItemKind::Milestone, idStr, ToHtRect(rr) });
				continue;
			}
			g_rowBands.push_back({ idStr, { g_chartScreenRect.left, rr.top, g_chartScreenRect.right, rr.bottom }, rr.left });
			g_editRegions.push_back({ "ROW_LABEL", idStr, rr });
			g_hitSnapshot.items.push_back({ HtItemKind::RowLabel, idStr, ToHtRect(rr) });
		}
		std::sort(g_rowBands.begin(), g_rowBands.end(), [](const RowBand& a, const RowBand& b) {
			return a.screenRect.top < b.screenRect.top;
		});
		g_hitSnapshot.rowBands.reserve(g_rowBands.size());
		for (const auto& band : g_rowBands) {
			g_hitSnapshot.rowBands.push_back({ band.rowId, band.screenRect.top, band.screenRect.bottom });
		}
		g_hitCacheChartRect = g_chartScreenRect;
		g_hitCacheChildCount = n;
	}
	catch (const _com_error&) {
		g_rowBands.clear();
		g_editRegions.clear();
		InvalidateHitSnapshot();
	}
}

// Recompute the chrome-facing selection state (g_selId/g_selKind/
// g_hasSelectionChrome/g_selScreenRect) FROM the internal selection model
// (g_ownSelKind/g_ownSelId) plus the current hit-test snapshot / row bands.
// If the internally-selected element is no longer present in the snapshot
// (deleted, or off a since-rebuilt chart), the internal selection itself is
// cleared — there is nothing to draw chrome around.
void SyncSelectionChromeFromOwnSelection() {
	ClearSelectionState();
	if (g_ownSelKind.empty() || g_ownSelId.empty()) return;

	if (g_ownSelKind == "ROW") {
		for (const auto& band : g_rowBands) {
			if (band.rowId == g_ownSelId) {
				g_selKind = "ROW";
				g_selId = g_ownSelId;
				g_selScreenRect = band.screenRect;
				g_hasSelectionChrome = true;
				return;
			}
		}
		ClearOwnSelection();
		return;
	}

	HtItemKind wantKind = (g_ownSelKind == "MILESTONE") ? HtItemKind::Milestone : HtItemKind::Task;
	for (const auto& item : g_hitSnapshot.items) {
		if (item.kind == wantKind && item.id == g_ownSelId) {
			g_selKind = g_ownSelKind;
			g_selId = g_ownSelId;
			g_selScreenRect = { item.rect.left, item.rect.top, item.rect.right, item.rect.bottom };
			g_hasSelectionChrome = true;
			return;
		}
	}
	ClearOwnSelection();
}

bool UpdateHoverFromCursor() {
	std::string oldId = g_hoverRowId;
	RECT oldRect = g_hoverBandRect;
	ClearHoverState();

	POINT pt = {};
	if (!::GetCursorPos(&pt) || !::PtInRect(&g_chartScreenRect, pt)) {
		return oldId != g_hoverRowId || !SameRect(oldRect, g_hoverBandRect);
	}

	for (const auto& band : g_rowBands) {
		if (pt.y >= band.screenRect.top && pt.y <= band.screenRect.bottom) {
			g_hoverRowId = band.rowId;
			g_hoverBandRect = band.screenRect;
			break;
		}
	}
	LayoutHoverInsertHotspot();
	return oldId != g_hoverRowId || !SameRect(oldRect, g_hoverBandRect);
}

// Single repaint entry point: everything that wants pixels on screen goes
// through RenderOverlay(). Later units (drag ghosts etc.) should add their
// drawing inside PaintOverlay so it flows through the same path.
void RequestOverlayRepaint() {
	if (!g_hwnd) return;
	RenderOverlay();
}

std::string CurrentEditText(const EditRegion& region) {
	std::string json = ReadGanttFromSlide(g_app);
	if (json.empty()) return "";
	PpDocument doc = DocumentFromJson(json);
	if (region.kind == "TITLE") return doc.title;
	if (region.kind == "ROW_LABEL") {
		for (const auto& row : doc.rows) {
			if (row.id == region.id) return row.label;
		}
	}
	return "";
}

void FocusPowerPoint() {
	try {
		if (!g_app) return;
		HWND ppt = (HWND)(INT_PTR)g_app->GetHWND();
		if (ppt) ::SetForegroundWindow(ppt);
	}
	catch (...) {
	}
}

void ClearEditTarget() {
	g_editKind.clear();
	g_editId.clear();
}

void HideInlineEditor() {
	g_editClosing = true;
	if (g_editorHwnd) ::ShowWindow(g_editorHwnd, SW_HIDE);
	g_editClosing = false;
}

void DestroyInlineEditor() {
	g_editClosing = true;
	if (g_editHwnd && g_oldEditProc) {
		::SetWindowLongPtrW(g_editHwnd, GWLP_WNDPROC, (LONG_PTR)g_oldEditProc);
		g_oldEditProc = NULL;
	}
	if (g_editorHwnd) {
		::DestroyWindow(g_editorHwnd);
		g_editorHwnd = NULL;
	}
	g_editHwnd = NULL;
	g_editClosing = false;
	ClearEditTarget();
}

void EnsureEditorWindow() {
	if (g_editorHwnd && g_editHwnd) return;

	WNDCLASSEXW wc = { sizeof(wc) };
	wc.lpfnWndProc = EditorWndProc;
	wc.hInstance = g_inst;
	wc.hCursor = ::LoadCursor(NULL, IDC_IBEAM);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszClassName = kEditorClass;
	::RegisterClassExW(&wc);

	g_editorHwnd = ::CreateWindowExW(
		WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
		kEditorClass, L"", WS_POPUP | WS_BORDER,
		0, 0, 100, 24, NULL, NULL, g_inst, NULL);
	if (!g_editorHwnd) return;

	g_editHwnd = ::CreateWindowExW(
		0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
		0, 0, 100, 24, g_editorHwnd, (HMENU)1, g_inst, NULL);
	if (g_editHwnd) {
		::SendMessageW(g_editHwnd, WM_SETFONT, (WPARAM)::GetStockObject(DEFAULT_GUI_FONT), TRUE);
		g_oldEditProc = (WNDPROC)::SetWindowLongPtrW(g_editHwnd, GWLP_WNDPROC, (LONG_PTR)InlineEditProc);
	}
}

void OpenInlineEditor(const EditRegion& region) {
	if (!g_app || g_mutating) return;
	// Only one editor at a time: opening the simple inline box closes any
	// open card editor first (CloseCardEditor discards uncommitted card
	// edits, same as clicking away from the card would via WM_ACTIVATE).
	CloseCardEditor();
	try {
		std::string value = CurrentEditText(region);
		EnsureEditorWindow();
		if (!g_editorHwnd || !g_editHwnd) return;

		RECT rc = region.screenRect;
		NormalizeRect(rc);
		int w = std::max(80, (int)(rc.right - rc.left));
		int h = std::max(22, (int)(rc.bottom - rc.top));
		::SetWindowPos(g_editorHwnd, HWND_TOPMOST, rc.left, rc.top, w, h, SWP_SHOWWINDOW);
		::MoveWindow(g_editHwnd, 0, 0, w, h, TRUE);
		std::wstring text = Widen(value);
		::SetWindowTextW(g_editHwnd, text.c_str());
		g_editKind = region.kind;
		g_editId = region.id;
		::SendMessageW(g_editHwnd, EM_SETSEL, 0, -1);
		::SetForegroundWindow(g_editorHwnd);
		::SetFocus(g_editHwnd);
	}
	catch (const _com_error&) {
		OvLog(L"COM error opening inline editor");
		HideInlineEditor();
		ClearEditTarget();
	}
	catch (const std::exception&) {
		OvLog(L"document error opening inline editor");
		HideInlineEditor();
		ClearEditTarget();
	}
	catch (...) {
		OvLog(L"unknown error opening inline editor");
		HideInlineEditor();
		ClearEditTarget();
	}
}

void CommitInlineEdit() {
	if (!g_editHwnd || g_editKind.empty() || g_mutating) return;
	std::string kind = g_editKind;
	std::string id = g_editId;
	try {
		int len = ::GetWindowTextLengthW(g_editHwnd);
		std::vector<wchar_t> buf((size_t)len + 1);
		::GetWindowTextW(g_editHwnd, buf.data(), len + 1);
		std::string text = Narrow(buf.data());

		HideInlineEditor();
		ClearEditTarget();

		g_mutating = true;
		try {
			std::string json = ReadGanttFromSlide(g_app);
			if (!json.empty()) {
				PpDocument doc = DocumentFromJson(json);
				bool changed = (kind == "TITLE") ? SetTitle(doc, text) : SetEntityLabel(doc, id, text);
				if (changed) RebuildChart(doc, kind == "ROW_LABEL" ? id : "");
			}
		}
		catch (const _com_error&) {
			OvLog(L"COM error committing inline edit");
		}
		catch (const std::exception&) {
			OvLog(L"document error committing inline edit");
		}
		catch (...) {
			OvLog(L"unknown error committing inline edit");
		}
		g_mutating = false;
		FocusPowerPoint();
	}
	catch (...) {
		g_mutating = false;
		HideInlineEditor();
		ClearEditTarget();
		FocusPowerPoint();
		OvLog(L"inline edit commit failed");
	}
}

void CancelInlineEdit() {
	HideInlineEditor();
	ClearEditTarget();
	FocusPowerPoint();
}

// ---- floating card editor ---------------------------------------------------

const PpMilestone* FindMilestone(const PpDocument& doc, const std::string& id) {
	for (const auto& ms : doc.milestones) {
		if (ms.id == id) return &ms;
	}
	return nullptr;
}

// True while EITHER editor (simple inline title/row-label box, or the richer
// card) owns keyboard focus / is mid-edit. Single choke point so hotkey
// registration (UpdateHotkeyRegistration) and native-selection suppression
// (Tick()) only need one check instead of two ad hoc ones, and so a future
// third editor kind only has to extend this function.
bool IsEditSessionActive() {
	return (g_editHwnd && !g_editKind.empty()) || (g_cardHwnd != NULL);
}

std::wstring FormatSwatchLabel(int idx) {
	wchar_t buf[8];
	::swprintf_s(buf, 8, L"%d", idx + 1);
	return buf;
}

// Read a single wide EDIT control's text as a narrow UTF-8 string.
std::string GetEditText(HWND hwnd) {
	if (!hwnd) return "";
	int len = ::GetWindowTextLengthW(hwnd);
	std::vector<wchar_t> buf((size_t)len + 1);
	::GetWindowTextW(hwnd, buf.data(), len + 1);
	return Narrow(buf.data());
}

void SetEditText(HWND hwnd, const std::string& value) {
	if (!hwnd) return;
	::SetWindowTextW(hwnd, Widen(value).c_str());
}

// Strict ISO (YYYY-MM-DD) parse: DateToDays() silently returns 0 for garbage
// input (sscanf_s just leaves unmatched fields as 0), so round-trip through
// DaysToDate() and require an EXACT string match — this also rejects
// civil-calendar nonsense like day 40 or month 13, which DaysFromCivil would
// otherwise silently normalize into some other (wrong) date instead of failing.
bool ParseIsoDateStrict(const std::string& text, long& outDays) {
	int y = 0, m = 0, d = 0;
	if (::sscanf_s(text.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return false;
	if (text.size() != 10 || text[4] != '-' || text[7] != '-') return false;
	if (m < 1 || m > 12 || d < 1 || d > 31 || y < 1) return false;
	long days = DateToDays(text);
	if (DaysToDate(days) != text) return false;
	outDays = days;
	return true;
}

// Toggle the "invalid" visual (red border) by forcing the affected field(s)
// to repaint via a class-level custom draw is more machinery than this needs;
// a simple, robust cross-Windows-version approach is to tint the control's
// background through WM_CTLCOLOREDIT (see CardWndProc) driven by g_cardInvalid,
// plus MessageBeep so the failure is audible even if the card is off-screen
// in a screenshot-less headless run (matches the task spec's "red border or
// MessageBeep").
void SetCardInvalid(bool invalid) {
	g_cardInvalid = invalid;
	if (g_cardStartHwnd) ::InvalidateRect(g_cardStartHwnd, NULL, TRUE);
	if (g_cardEndHwnd) ::InvalidateRect(g_cardEndHwnd, NULL, TRUE);
	if (invalid) ::MessageBeep(MB_ICONWARNING);
}

// Position + size every child control inside the card's client area. Called
// once at open time (LayoutCardControls is idempotent, so it is safe to call
// again if the card were ever resized, though the card is currently
// fixed-size). isMilestone hides the End-date and Percent rows entirely
// (milestones have neither) and shrinks the window accordingly.
void LayoutCardControls(bool isMilestone) {
	if (!g_cardHwnd) return;
	const int pad = Scale(kBaseCardPad);
	const int rowH = Scale(kBaseCardRowH);
	const int rowGap = Scale(kBaseCardRowGap);
	const int labelW = Scale(kBaseCardLabelW);
	const int cardW = Scale(kBaseCardW);
	const int fieldW = cardW - pad * 2 - labelW;
	const int swatchSize = Scale(kBaseCardSwatchSize);
	const int swatchGap = Scale(kBaseCardSwatchGap);

	int y = pad;
	auto placeRow = [&](HWND field, bool visible) {
		if (!field) return;
		::ShowWindow(field, visible ? SW_SHOW : SW_HIDE);
		if (!visible) return;
		::MoveWindow(field, pad + labelW, y, fieldW, rowH, TRUE);
		y += rowH + rowGap;
	};

	placeRow(g_cardLabelHwnd, true);
	placeRow(g_cardStartHwnd, true);
	placeRow(g_cardEndHwnd, !isMilestone);
	placeRow(g_cardPercentHwnd, !isMilestone);

	// Swatch row: 8 small square buttons in a single row.
	int swatchesW = kCardSwatchCount * swatchSize + (kCardSwatchCount - 1) * swatchGap;
	int sx = pad + std::max(0, (cardW - pad * 2 - swatchesW) / 2);
	for (int i = 0; i < kCardSwatchCount; ++i) {
		if (!g_cardSwatchHwnd[i]) continue;
		::MoveWindow(g_cardSwatchHwnd[i], sx + i * (swatchSize + swatchGap), y, swatchSize, swatchSize, TRUE);
	}
	y += swatchSize + rowGap;

	// OK button, right-aligned.
	const int okW = Scale(kBaseCardOkW);
	const int okH = Scale(kBaseCardOkH);
	if (g_cardOkHwnd) {
		::MoveWindow(g_cardOkHwnd, cardW - pad - okW, y, okW, okH, TRUE);
	}
	y += okH + pad;

	::SetWindowPos(g_cardHwnd, NULL, 0, 0, cardW, y, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Create the card's top-level window + every child control, once. Idempotent
// (like EnsureEditorWindow): a re-open just repositions/repopulates.
void EnsureCardWindow() {
	if (g_cardHwnd) return;

	WNDCLASSEXW wc = { sizeof(wc) };
	wc.lpfnWndProc = CardWndProc;
	wc.hInstance = g_inst;
	wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = kCardClass;
	::RegisterClassExW(&wc);

	g_cardHwnd = ::CreateWindowExW(
		WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
		kCardClass, L"Edit", WS_POPUP | WS_BORDER,
		0, 0, Scale(kBaseCardW), 200, NULL, NULL, g_inst, NULL);
	if (!g_cardHwnd) return;

	HFONT font = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
	auto makeEdit = [&](int id, DWORD extraStyle) {
		HWND h = ::CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | extraStyle,
			0, 0, 10, 10, g_cardHwnd, (HMENU)(INT_PTR)id, g_inst, NULL);
		if (h) {
			::SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
			g_oldCardFieldProc = (WNDPROC)::SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)CardFieldProc);
		}
		return h;
	};
	g_cardLabelHwnd = makeEdit(CARD_ID_LABEL, 0);
	g_cardStartHwnd = makeEdit(CARD_ID_START, 0);
	g_cardEndHwnd = makeEdit(CARD_ID_END, 0);
	g_cardPercentHwnd = makeEdit(CARD_ID_PERCENT, ES_NUMBER);

	for (int i = 0; i < kCardSwatchCount; ++i) {
		g_cardSwatchHwnd[i] = ::CreateWindowExW(0, L"BUTTON", FormatSwatchLabel(i).c_str(),
			WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
			0, 0, 10, 10, g_cardHwnd, (HMENU)(INT_PTR)(CARD_ID_SWATCH_BASE + i), g_inst, NULL);
		if (g_cardSwatchHwnd[i]) ::SendMessageW(g_cardSwatchHwnd[i], WM_SETFONT, (WPARAM)font, TRUE);
	}

	g_cardOkHwnd = ::CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_DEFPUSHBUTTON,
		0, 0, 10, 10, g_cardHwnd, (HMENU)(INT_PTR)CARD_ID_OK, g_inst, NULL);
	if (g_cardOkHwnd) ::SendMessageW(g_cardOkHwnd, WM_SETFONT, (WPARAM)font, TRUE);
}

// Destroy the card window + children and clear every bit of card state. Safe
// to call whether or not a card is currently open (mirrors DestroyInlineEditor).
void CloseCardEditor() {
	g_cardClosing = true;
	if (g_cardHwnd) {
		::DestroyWindow(g_cardHwnd);
		g_cardHwnd = NULL;
	}
	g_cardLabelHwnd = g_cardStartHwnd = g_cardEndHwnd = g_cardPercentHwnd = g_cardOkHwnd = NULL;
	for (int i = 0; i < kCardSwatchCount; ++i) g_cardSwatchHwnd[i] = NULL;
	g_oldCardFieldProc = NULL;
	g_cardKind.clear();
	g_cardId.clear();
	g_cardSelectedSwatch = -1;
	g_cardOrigColor.clear();
	g_cardInvalid = false;
	g_cardClosing = false;
}

// Open (or re-open, replacing any existing card per "only one editor window at
// a time") the card for a TASK or MILESTONE hit. anchorScreenRect is the
// item's current on-screen rect (from g_hitSnapshot), used to position the
// card just below-right of the bar, DPI-scaled and clamped so it never opens
// fully off whichever monitor the bar is on.
void OpenCardEditor(const std::string& kind, const std::string& id, const RECT& anchorScreenRect) {
	if (!g_app || g_mutating) return;
	// "Opening a new one closes the previous" — also covers double-clicking a
	// DIFFERENT item while a card is already open for another.
	CloseCardEditor();
	CancelInlineEdit(); // the two editors are mutually exclusive at all times

	try {
		std::string json = ReadGanttFromSlide(g_app);
		if (json.empty()) return;
		PpDocument doc = DocumentFromJson(json);

		bool isMilestone = (kind == "MILESTONE");
		std::string label, start, end, color;
		int percent = 0;
		if (isMilestone) {
			const PpMilestone* ms = FindMilestone(doc, id);
			if (!ms) return;
			label = ms->label;
			start = ms->date;
			color = ms->color;
		} else {
			const PpTask* task = FindTask(doc, id);
			if (!task) return;
			label = task->label;
			start = task->start;
			end = task->end;
			percent = task->percent;
			color = task->color;
		}

		EnsureCardWindow();
		if (!g_cardHwnd) return;

		g_cardKind = kind;
		g_cardId = id;
		g_cardOrigColor = color;
		g_cardSelectedSwatch = -1;
		for (int i = 0; i < kCardSwatchCount; ++i) {
			wchar_t hex[8];
			::swprintf_s(hex, 8, L"#%02x%02x%02x", GetRValue(kCardSwatches[i]), GetGValue(kCardSwatches[i]), GetBValue(kCardSwatches[i]));
			if (Narrow(hex) == color) { g_cardSelectedSwatch = i; break; }
		}

		SetEditText(g_cardLabelHwnd, label);
		SetEditText(g_cardStartHwnd, start);
		SetEditText(g_cardEndHwnd, end);
		if (!isMilestone) SetEditText(g_cardPercentHwnd, std::to_string(percent));

		LayoutCardControls(isMilestone);
		SetCardInvalid(false);

		RECT rc = anchorScreenRect;
		NormalizeRect(rc);
		RECT cardRect = {};
		::GetWindowRect(g_cardHwnd, &cardRect);
		int cardW = cardRect.right - cardRect.left;
		int cardH = cardRect.bottom - cardRect.top;
		int x = rc.left;
		int y = rc.bottom + Scale(4);

		HMONITOR mon = ::MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { sizeof(mi) };
		if (::GetMonitorInfoW(mon, &mi)) {
			if (x + cardW > mi.rcWork.right) x = mi.rcWork.right - cardW;
			if (x < mi.rcWork.left) x = mi.rcWork.left;
			if (y + cardH > mi.rcWork.bottom) y = rc.top - cardH - Scale(4);
			if (y < mi.rcWork.top) y = mi.rcWork.top;
		}

		::SetWindowPos(g_cardHwnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
		::SetForegroundWindow(g_cardHwnd);
		::SetFocus(g_cardLabelHwnd);
		::SendMessageW(g_cardLabelHwnd, EM_SETSEL, 0, -1);
	}
	catch (const _com_error&) {
		OvLog(L"COM error opening card editor");
		CloseCardEditor();
	}
	catch (const std::exception&) {
		OvLog(L"document error opening card editor");
		CloseCardEditor();
	}
	catch (...) {
		OvLog(L"unknown error opening card editor");
		CloseCardEditor();
	}
}

// Validate + commit every field of the open card in ONE undo entry. Returns
// without closing the card if validation fails (invalid dates: parse failure
// or end < start) — the task spec requires the editor to STAY OPEN with a
// red-border/beep cue rather than silently discarding the user's edits.
void CommitCardEdit() {
	if (!g_cardHwnd || g_cardKind.empty() || g_mutating) return;
	std::string kind = g_cardKind;
	std::string id = g_cardId;
	bool isMilestone = (kind == "MILESTONE");

	std::string label = GetEditText(g_cardLabelHwnd);
	std::string startText = GetEditText(g_cardStartHwnd);
	std::string endText = isMilestone ? startText : GetEditText(g_cardEndHwnd);
	std::string percentText = isMilestone ? "" : GetEditText(g_cardPercentHwnd);

	long startDays = 0, endDays = 0;
	if (!ParseIsoDateStrict(startText, startDays)) {
		SetCardInvalid(true);
		return;
	}
	if (!isMilestone) {
		if (!ParseIsoDateStrict(endText, endDays)) {
			SetCardInvalid(true);
			return;
		}
		if (endDays < startDays) {
			SetCardInvalid(true);
			return;
		}
	}
	SetCardInvalid(false);

	int percent = 0;
	if (!isMilestone) {
		try { percent = std::stoi(percentText); } catch (...) { percent = 0; }
		percent = std::max(0, std::min(100, percent));
	}

	std::string color;
	if (g_cardSelectedSwatch >= 0 && g_cardSelectedSwatch < kCardSwatchCount) {
		wchar_t hex[8];
		::swprintf_s(hex, 8, L"#%02x%02x%02x", GetRValue(kCardSwatches[g_cardSelectedSwatch]), GetGValue(kCardSwatches[g_cardSelectedSwatch]), GetBValue(kCardSwatches[g_cardSelectedSwatch]));
		color = Narrow(hex);
	} else {
		color = g_cardOrigColor; // no swatch clicked: leave color unchanged
	}

	CloseCardEditor();

	g_mutating = true;
	try {
		std::string json = ReadGanttFromSlide(g_app);
		if (!json.empty()) {
			PpDocument doc = DocumentFromJson(json);
			bool changed = false;
			changed = SetEntityLabel(doc, id, label) || changed;
			if (isMilestone) {
				// Milestones only have a single date field (Start doubles as
				// the milestone's date) and no percent/color-via-task op yet
				// (PpMilestone.color exists but GanttOps has no milestone
				// color setter — out of scope per the task brief, which only
				// asked for SetTaskColor). Date-only commit for milestones.
				for (auto& ms : doc.milestones) {
					if (ms.id == id) {
						std::string newDate = DaysToDate(startDays);
						if (ms.date != newDate) { ms.date = newDate; changed = true; }
						break;
					}
				}
			} else {
				const PpTask* before = FindTask(doc, id);
				std::string prevStart = before ? before->start : "";
				std::string prevEnd = before ? before->end : "";
				std::string newStart = DaysToDate(startDays);
				std::string newEnd = DaysToDate(endDays);
				if (newStart != prevStart || newEnd != prevEnd) {
					changed = SetTaskDates(doc, id, newStart, newEnd) || changed;
				}
				int prevPercent = before ? before->percent : -1;
				if (percent != prevPercent) {
					changed = SetTaskPercentValue(doc, id, percent) || changed;
				}
				std::string prevColor = before ? before->color : "";
				if (color != prevColor) {
					changed = SetTaskColor(doc, id, color) || changed;
				}
			}
			if (changed) RebuildChart(doc, id);
		}
	}
	catch (const _com_error&) {
		OvLog(L"COM error committing card edit");
	}
	catch (const std::exception&) {
		OvLog(L"document error committing card edit");
	}
	catch (...) {
		OvLog(L"unknown error committing card edit");
	}
	g_mutating = false;
	SetOwnSelection(kind, id);
	FocusPowerPoint();
}

void CancelCardEdit() {
	CloseCardEditor();
	FocusPowerPoint();
}

// Late-bound Application.StartNewUndoEntry (idiom copied from
// native/render/undo-probe.cpp's CallStartNewUndoEntry/InvokeName): called at
// the START of a user-gesture commit, before any mutation, so every COM edit
// that follows — including a delete+recreate ungroup/regroup — collapses into
// ONE undo entry (VERDICT: GROUPING_WORKS in native/build/undo-probe.txt; no
// trailing call needed). Best-effort: swallows failure, just logs the HRESULT
// from GetIDsOfNames/Invoke so a missing/broken API on some PowerPoint build
// degrades to "no grouping" rather than a crash.
void StartNewUndoEntryIfPossible() {
	if (!g_app) return;
	try {
		IDispatch* appDisp = g_app;
		DISPID dispid = 0;
		LPOLESTR name = const_cast<LPOLESTR>(L"StartNewUndoEntry");
		HRESULT hrIds = appDisp->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &dispid);
		if (FAILED(hrIds)) {
			wchar_t buf[80];
			::swprintf_s(buf, 80, L"StartNewUndoEntry: GetIDsOfNames failed hr=0x%08lX", (unsigned long)hrIds);
			OvLog(buf);
			return;
		}
		DISPPARAMS dp = {};
		EXCEPINFO ei = {};
		UINT argErr = 0;
		HRESULT hrInv = appDisp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, NULL, &ei, &argErr);
		if (FAILED(hrInv)) {
			wchar_t buf[80];
			::swprintf_s(buf, 80, L"StartNewUndoEntry: Invoke failed hr=0x%08lX", (unsigned long)hrInv);
			OvLog(buf);
		}
		if (ei.bstrDescription) ::SysFreeString(ei.bstrDescription);
		if (ei.bstrSource) ::SysFreeString(ei.bstrSource);
		if (ei.bstrHelpFile) ::SysFreeString(ei.bstrHelpFile);
	}
	catch (const _com_error& e) {
		wchar_t buf[80];
		::swprintf_s(buf, 80, L"StartNewUndoEntry: COM error 0x%08lX", (unsigned long)e.Error());
		OvLog(buf);
	}
	catch (...) {
		OvLog(L"StartNewUndoEntry: unknown error");
	}
}

// Append a debug line (shared with Connect's log).
void OvLog(const wchar_t* msg) {
	wchar_t path[MAX_PATH];
	DWORD n = ::GetTempPathW(MAX_PATH, path);
	if (!n || n > MAX_PATH) return;
	::wcscat_s(path, MAX_PATH, L"powerplanner-addin.log");
	HANDLE h = ::CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return;
	wchar_t pidBuf[48];
	::swprintf_s(pidBuf, 48, L"[overlay %lu @%lu] ", ::GetCurrentProcessId(), ::GetTickCount());
	std::wstring line = std::wstring(pidBuf) + msg + L"\r\n";
	DWORD w = 0; ::WriteFile(h, line.c_str(), (DWORD)(line.size() * sizeof(wchar_t)), &w, NULL);
	::CloseHandle(h);
}

// Select the CHART_ROOT group natively (used by the 'move chart' grip) so the
// user can still move/delete the whole chart even though the overlay captures
// every chart click.
void SelectChartRoot() {
	if (!g_app || g_mutating) return;
	try {
		PowerPoint::DocumentWindowPtr win = g_app->GetActiveWindow();
		if (!win) return;
		PowerPoint::_SlidePtr slide = win->GetView()->GetSlide();
		PowerPoint::ShapePtr chart = FindChartRoot(slide);
		if (!chart) return;
		chart->Select(Office::msoTrue);
		FocusPowerPoint();
		OvLog(L"move-chart grip: selected CHART_ROOT");
	}
	catch (const _com_error&) {
		OvLog(L"COM error selecting chart root");
	}
	catch (const std::exception&) {
		OvLog(L"error selecting chart root");
	}
	catch (...) {
		OvLog(L"unknown error selecting chart root");
	}
}

// Semantic hit test for a client-space point (pure — no COM).
HtHit HitTestClientPoint(POINT pt) {
	return GanttHitTestPoint(g_hitSnapshot, pt.x + g_windowOriginX, pt.y + g_windowOriginY);
}

// Translate a pure HtCursor into a real HCURSOR for ::SetCursor. This is the
// ONLY place HCURSOR appears — GanttHitTest.h/.cpp stay COM/Windows-free so
// GanttCursorForZone can be unit-tested from the ops harness.
HCURSOR HCursorForHtCursor(HtCursor c) {
	switch (c) {
	case HtCursor::SizeAll: return ::LoadCursor(NULL, IDC_SIZEALL);
	case HtCursor::SizeWE:  return ::LoadCursor(NULL, IDC_SIZEWE);
	case HtCursor::Cross:   return ::LoadCursor(NULL, IDC_CROSS);
	case HtCursor::Hand:    return ::LoadCursor(NULL, IDC_HAND);
	case HtCursor::Arrow:   return ::LoadCursor(NULL, IDC_ARROW);
	case HtCursor::Default:
	default:
		return NULL; // let DefWindowProcW pick the default (class) cursor
	}
}

// WM_SETCURSOR handler body: resolve the client point under the cursor to a
// chrome-widget hit first (toolbar button / hover-insert '+' / move grip all
// win as Hand, matching WM_NCHITTEST's own widget-vs-chart precedence), then
// fall back to the semantic chart hit test. Returns true if a cursor was set
// (caller returns TRUE from WM_SETCURSOR to suppress the default handling);
// false lets DefWindowProcW apply the class cursor (used outside the chart
// and off any chrome widget).
bool HandleSetCursor(HWND hwnd) {
	POINT screenPt = {};
	if (!::GetCursorPos(&screenPt)) return false;
	POINT pt = screenPt;
	::ScreenToClient(hwnd, &pt);

	bool overChromeWidget = ButtonFromClientPoint(pt) >= 0 || HoverInsertFromClientPoint(pt) || GripFromClientPoint(pt);
	HtZone zone = HtZone::Outside;
	if (!overChromeWidget) {
		if (!::PtInRect(&g_chartScreenRect, screenPt)) return false; // truly outside: default cursor
		zone = HitTestClientPoint(pt).zone;
	}
	HtCursor cur = GanttCursorForZone(zone, overChromeWidget);
	HCURSOR hc = HCursorForHtCursor(cur);
	if (!hc) return false;
	::SetCursor(hc);
	return true;
}

// Apply the selection effect of a completed CLICK gesture (down+up within the
// drag threshold, same point). TaskBody/edges and Milestone select the item;
// a RowBand hit with a row id selects the row; EmptyCell/background-RowBand
// (empty row id)/Outside all clear the selection. Chrome is re-synced by the
// caller's next Tick() (or immediately, for the harness's benefit, by calling
// SyncSelectionChromeFromOwnSelection after this).
void ApplyClickSelection(const HtHit& hit) {
	switch (hit.zone) {
	case HtZone::TaskBody:
	case HtZone::TaskEdgeL:
	case HtZone::TaskEdgeR:
		SetOwnSelection("TASK", hit.id);
		return;
	case HtZone::Milestone:
		SetOwnSelection("MILESTONE", hit.id);
		return;
	case HtZone::RowBand:
		if (!hit.rowId.empty()) {
			SetOwnSelection("ROW", hit.rowId);
		} else {
			ClearOwnSelection(); // empty rowId = chart background
		}
		return;
	case HtZone::Label:
		// Labels are handled by double-click-to-edit; a single click on a
		// label does not change selection either way today.
		return;
	case HtZone::EmptyCell:
	case HtZone::Outside:
	default:
		ClearOwnSelection();
		return;
	}
}

// Reset all drag-gesture state to idle. Idempotent — safe to call from
// WM_CAPTURECHANGED (including our own ReleaseCapture, which delivers it) as
// well as from an explicit cancel.
void ResetDragGestureState() {
	g_gestureActive = false;
	g_dragActive = false;
	g_dragKind = DragKind::None;
	g_dragId.clear();
	::SetRectEmpty(&g_dragAnchorRect);
	g_dragOrigStart.clear();
	g_dragOrigEnd.clear();
	g_dragPxPerDay = 0.0f;
	g_dragDeltaDays = 0;
	g_dragLastPt = {};
	g_dragOrigRowId.clear();
	g_dragTargetRowId.clear();
	::SetRectEmpty(&g_dragTargetRowRect);
	g_createRowId.clear();
	g_createAnchorDay = 0;
	g_createCurrentDay = 0;
}

// SCREEN-PIXELS-per-day for the gesture. WM_MOUSEMOVE deltas are screen
// pixels and must never be mixed with PP_PROJ's ptPerDay field, which is in
// slide POINTS, without a COM-derived points->pixels zoom factor — which
// WM_MOUSEMOVE handling deliberately avoids (per A4, no COM on every move).
//
// The hit snapshot's item rects (g_hitSnapshot, built each Tick from
// PointsToScreenPixelsX/Y) are already in screen pixels, so deriving
// px/day from ANY task's rect-width / day-span in the current snapshot
// gives the exact same axis scale as the anchor item (all tasks share one
// time axis) and needs no COM call. This also covers the milestone case
// (a milestone has no day-span of its own): fall back to the first task
// found in the snapshot when the anchor itself isn't a task with a usable
// span (anchorSpanDays <= 0).
float ComputeDragPxPerDay(const RECT& anchorRect, long anchorSpanDays) {
	if (anchorSpanDays > 0) {
		float widthPx = (float)(anchorRect.right - anchorRect.left);
		if (widthPx > 0.0f) return widthPx / (float)anchorSpanDays;
	}
	if (!g_app) return 0.0f;
	std::string json = ReadGanttFromSlide(g_app);
	if (json.empty()) return 0.0f;
	PpDocument doc = DocumentFromJson(json);
	for (const auto& item : g_hitSnapshot.items) {
		if (item.kind != HtItemKind::Task) continue;
		const PpTask* task = FindTask(doc, item.id);
		if (!task) continue;
		long span = DateToDays(task->end) - DateToDays(task->start) + 1;
		if (span <= 0) continue;
		float widthPx = (float)(item.rect.right - item.rect.left);
		if (widthPx > 0.0f) return widthPx / (float)span;
	}
	return 0.0f;
}

// Begin a drag-move-resize gesture anchored at a TaskBody/TaskEdgeL/
// TaskEdgeR/Milestone hit. Reads the document once (per A4's guidance to read
// doc dates at gesture start) so subsequent WM_MOUSEMOVE handling stays
// COM-free. No-ops (leaves gesture state untouched) if the doc/task/milestone
// can't be resolved — the gesture then behaves as a plain click-vs-drag-less
// capture (existing WM_LBUTTONUP click-select logic still applies on threshold-
// within release).
void StartDragGesture(const HtHit& hit, POINT downPt) {
	ResetDragGestureState();
	if (hit.zone != HtZone::TaskBody && hit.zone != HtZone::TaskEdgeL &&
		hit.zone != HtZone::TaskEdgeR && hit.zone != HtZone::Milestone) {
		return;
	}
	if (!g_app) return;
	std::string json = ReadGanttFromSlide(g_app);
	if (json.empty()) return;
	PpDocument doc = DocumentFromJson(json);

	RECT anchorRect = {};
	bool haveRect = false;
	for (const auto& item : g_hitSnapshot.items) {
		if (item.id == hit.id &&
			((hit.zone == HtZone::Milestone && item.kind == HtItemKind::Milestone) ||
			 (hit.zone != HtZone::Milestone && item.kind == HtItemKind::Task))) {
			anchorRect = { item.rect.left, item.rect.top, item.rect.right, item.rect.bottom };
			haveRect = true;
			break;
		}
	}
	if (!haveRect) return;

	if (hit.zone == HtZone::Milestone) {
		const PpMilestone* ms = nullptr;
		for (const auto& m : doc.milestones) if (m.id == hit.id) { ms = &m; break; }
		if (!ms) return;
		g_dragKind = DragKind::Milestone;
		g_dragId = hit.id;
		g_dragOrigStart = ms->date;
		g_dragOrigEnd.clear();
		g_dragPxPerDay = ComputeDragPxPerDay(anchorRect, 0);
	} else {
		const PpTask* task = FindTask(doc, hit.id);
		if (!task) return;
		g_dragKind = (hit.zone == HtZone::TaskEdgeL) ? DragKind::TaskEdgeL
			: (hit.zone == HtZone::TaskEdgeR) ? DragKind::TaskEdgeR : DragKind::TaskBody;
		g_dragId = hit.id;
		g_dragOrigStart = task->start;
		g_dragOrigEnd = task->end;
		long spanDays = DateToDays(task->end) - DateToDays(task->start) + 1;
		g_dragPxPerDay = ComputeDragPxPerDay(anchorRect, spanDays);
		if (g_dragKind == DragKind::TaskBody) {
			// Row-reassign only applies to whole-bar (body) drags, not edge
			// resizes: retarget the ghost's row using the row band under the
			// CURRENT drag point, tracked from WM_MOUSEMOVE (see
			// UpdateDragGesture). Seed both orig and target with the task's
			// row at gesture start so a drag that never leaves the row is a
			// no-op vertically.
			g_dragOrigRowId = task->rowId;
			g_dragTargetRowId = task->rowId;
			g_dragTargetRowRect = anchorRect; // refined below if a band is found
			for (const auto& band : g_rowBands) {
				if (band.rowId == task->rowId) { g_dragTargetRowRect = band.screenRect; break; }
			}
		}
	}
	if (g_dragPxPerDay <= 0.0f) { ResetDragGestureState(); return; }

	g_dragAnchorRect = anchorRect;
	g_dragLastPt = downPt;
	g_dragDeltaDays = 0;
	g_gestureActive = true;
}

// px-per-day for a CREATE gesture anchored on an EmptyCell (no task rect to
// derive scale from at the anchor point itself). Reuses ComputeDragPxPerDay's
// "scan the snapshot for any task with a usable day-span" fallback path by
// passing an empty anchor rect / zero span, so the scale always comes from an
// existing task's rect-width / day-span — the same axis all tasks share.
float ComputeEmptyCellPxPerDay() {
	RECT empty = {};
	return ComputeDragPxPerDay(empty, 0);
}

// Begin a create-on-EmptyCell gesture: anchors a brand-new task span at the
// day under the down point, in the row the EmptyCell hit reports. Mirrors
// StartDragGesture's "read doc once, everything else off the snapshot"
// pattern so WM_MOUSEMOVE handling stays COM-free.
void StartCreateGesture(const HtHit& hit, POINT downPt) {
	ResetDragGestureState();
	if (hit.zone != HtZone::EmptyCell || hit.rowId.empty()) return;
	if (!g_app) return;

	float pxPerDay = ComputeEmptyCellPxPerDay();
	if (pxPerDay <= 0.0f) return;

	// Derive the anchor day from the down-point x. There is no task rect at
	// an EmptyCell point to read a day directly from, so instead anchor
	// against a REFERENCE task already in the snapshot: its rect.left is a
	// known (day, screen-x) pair (day = task.start, screen-x = rect.left),
	// and every task shares the same time axis (pxPerDay), so the anchor day
	// is that reference day plus/minus the screen-pixel offset from the
	// reference, divided by pxPerDay. This is the documented choice for A4's
	// "derive px-per-day... or from any task rect in the snapshot" — using a
	// task rect as the (day, x) reference point avoids a second COM call
	// beyond the one doc read ComputeEmptyCellPxPerDay's fallback already
	// performs (that read is reused below via a fresh ReadGanttFromSlide,
	// which is cheap/idempotent and keeps this function self-contained).
	std::string json = ReadGanttFromSlide(g_app);
	if (json.empty()) return;
	PpDocument doc = DocumentFromJson(json);
	bool haveRef = false;
	long refDay = 0;
	long refScreenX = 0;
	for (const auto& item : g_hitSnapshot.items) {
		if (item.kind != HtItemKind::Task) continue;
		const PpTask* task = FindTask(doc, item.id);
		if (!task) continue;
		refDay = DateToDays(task->start);
		refScreenX = item.rect.left;
		haveRef = true;
		break;
	}
	if (!haveRef) return;

	long anchorDay = refDay + (long)::lround((double)(downPt.x + g_windowOriginX - refScreenX) / (double)pxPerDay);

	g_dragKind = DragKind::Create;
	g_createRowId = hit.rowId;
	g_createAnchorDay = anchorDay;
	g_createCurrentDay = anchorDay;
	g_dragPxPerDay = pxPerDay;
	for (const auto& band : g_rowBands) {
		if (band.rowId == hit.rowId) { g_dragTargetRowRect = band.screenRect; break; }
	}
	g_dragLastPt = downPt;
	g_gestureActive = true;
	// Note: g_dragActive latches later, in UpdateDragGesture, exactly like the
	// move/resize path — a create gesture that never crosses the threshold is
	// still a plain click (no task created), per ApplyClickSelection's
	// existing EmptyCell -> ClearOwnSelection handling.
}

// Row band (from the current per-tick g_rowBands) whose y-range contains the
// given SCREEN y, or nullptr if none (e.g. above/below the chart). Screen
// space to match g_rowBands' screenRect (see BuildRowBands).
const RowBand* RowBandAtScreenY(long screenY) {
	for (const auto& band : g_rowBands) {
		if (screenY >= band.screenRect.top && screenY < band.screenRect.bottom) return &band;
	}
	return nullptr;
}

// Recompute the candidate day delta from the total horizontal displacement
// since the gesture anchor (not incremental per-move deltas, so rounding
// never accumulates drift) and request a repaint if it changed the ghost.
// For a TaskBody drag, also retargets the row band under the CURRENT point
// (vertical reassignment) combined with the same horizontal day-shift. For a
// Create gesture, recomputes the candidate day under the current point
// (anchor day is fixed; current day tracks the pointer, so dragging left of
// the anchor is a valid "growing left" span exactly like the task spec asks).
void UpdateDragGesture(POINT pt) {
	if (!g_gestureActive) return;
	g_dragLastPt = pt;
	long dx = pt.x - g_mouseDownPt.x;
	long dy = pt.y - g_mouseDownPt.y;
	if (!g_dragActive) {
		if (dx < -kDragThresholdPx || dx > kDragThresholdPx || dy < -kDragThresholdPx || dy > kDragThresholdPx) {
			g_dragActive = true;
		} else {
			return; // still within click threshold: no ghost yet
		}
	}

	if (g_dragKind == DragKind::Create) {
		long newDay = g_createAnchorDay + (long)::lround((double)dx / (double)g_dragPxPerDay);
		g_createCurrentDay = newDay;
		RequestOverlayRepaint();
		return;
	}

	long newDelta = (g_dragPxPerDay > 0.0f) ? (long)::lround((double)dx / (double)g_dragPxPerDay) : 0;
	g_dragDeltaDays = newDelta;

	if (g_dragKind == DragKind::TaskBody) {
		long screenY = pt.y + g_windowOriginY;
		if (const RowBand* band = RowBandAtScreenY(screenY)) {
			g_dragTargetRowId = band->rowId;
			g_dragTargetRowRect = band->screenRect;
		}
		// If no band is under the pointer (above/below the chart), keep the
		// last valid target — the ghost stays at the last row it was over
		// rather than snapping away, and the horizontal shift still updates.
	}

	RequestOverlayRepaint(); // A2/task spec: repaint on each move (cheap)
}

// Compute the candidate dates for a gesture (original dates shifted by the
// day delta, per drag kind), clamping end >= start. Pure — takes the gesture
// facts as parameters rather than reading g_drag* globals, because the
// commit path (CommitDragGesture) calls this AFTER those globals have
// already been cleared by WM_CAPTURECHANGED's CancelDragGesture (see
// CommitDragGesture's comment). The live-globals convenience overload below
// is for PaintOverlay's mid-drag tooltip, where the globals are still valid.
void ComputeDragCandidateDates(DragKind kind, const std::string& origStart, const std::string& origEnd,
	long deltaDays, std::string& outStart, std::string& outEnd) {
	long startDay = DateToDays(origStart);
	long endDay = origEnd.empty() ? startDay : DateToDays(origEnd);
	switch (kind) {
	case DragKind::TaskEdgeL:
		startDay += deltaDays;
		if (startDay > endDay) startDay = endDay; // clamp end>=start
		break;
	case DragKind::TaskEdgeR:
		endDay += deltaDays;
		if (endDay < startDay) endDay = startDay;
		break;
	case DragKind::TaskBody:
	case DragKind::Milestone:
	default:
		startDay += deltaDays;
		endDay += deltaDays;
		break;
	}
	outStart = DaysToDate(startDay);
	outEnd = DaysToDate(endDay);
}

// Convenience overload for callers reading the CURRENT (live) gesture state
// (PaintOverlay's mid-drag tooltip) — NOT used by the commit path (see above).
void ComputeDragCandidateDates(std::string& outStart, std::string& outEnd) {
	ComputeDragCandidateDates(g_dragKind, g_dragOrigStart, g_dragOrigEnd, g_dragDeltaDays, outStart, outEnd);
}

// Cancel an in-progress gesture (Esc, or WM_CAPTURECHANGED losing capture):
// discard all gesture state and repaint so the ghost disappears. Selection is
// untouched.
void CancelDragGesture() {
	if (!g_gestureActive) return;
	ResetDragGestureState();
	RequestOverlayRepaint();
}

// Commit on WM_LBUTTONUP with an active drag: mutate the document via
// GanttOps and rebuild, then re-apply the internal selection (A2) so the
// dragged item stays selected instead of flickering to deselected. A
// zero-day delta AND an unchanged row is a no-op — no rebuild, no undo
// pollution (a pure row-reassign with zero horizontal shift still commits).
//
// Takes the gesture snapshot as PARAMETERS rather than reading the live
// g_drag* globals: the caller (WM_LBUTTONUP) must call ::ReleaseCapture()
// before this to release its own mouse capture, and — because capture is on
// our own hwnd — that delivers WM_CAPTURECHANGED synchronously, which calls
// CancelDragGesture() (to cover the externally-stolen-capture case) and
// would otherwise wipe g_gestureActive/g_dragActive/g_dragKind/etc. out from
// under this function before it ever ran.
//
// targetRowId is only meaningful for DragKind::TaskBody (row-reassign); pass
// "" for edge/milestone drags where no row retarget applies.
void CommitDragGesture(DragKind kind, const std::string& id, long deltaDays, const std::string& targetRowId) {
	bool rowChangeRequested = (kind == DragKind::TaskBody) && !targetRowId.empty();
	if (deltaDays == 0 && !rowChangeRequested) return;
	if (!g_app || g_mutating) return;

	g_mutating = true;
	try {
		std::string json = ReadGanttFromSlide(g_app);
		if (!json.empty()) {
			PpDocument doc = DocumentFromJson(json);
			bool changed = false;
			std::string selKind;
			if (kind == DragKind::Milestone) {
				// Milestones have no length; a date shift is a body-style nudge.
				changed = NudgeTask(doc, id, deltaDays); // no-op if id is not a task
				if (!changed) {
					for (auto& ms : doc.milestones) {
						if (ms.id == id) {
							ms.date = DaysToDate(DateToDays(ms.date) + deltaDays);
							changed = true;
							break;
						}
					}
				}
				selKind = "MILESTONE";
			} else if (kind == DragKind::TaskBody) {
				// Row-reassign first (only if the target row actually differs
				// from the task's CURRENT row — re-read fresh, not the gesture-
				// start g_dragOrigRowId, since nothing else mutates the doc
				// between gesture start and commit so they agree anyway), then
				// the horizontal day-shift, combined into one rebuild/commit.
				bool rowChanged = false;
				if (rowChangeRequested) {
					if (const PpTask* task = FindTask(doc, id)) {
						if (task->rowId != targetRowId) {
							rowChanged = MoveTaskToRow(doc, id, targetRowId);
						}
					}
				}
				bool dateChanged = (deltaDays != 0) && NudgeTask(doc, id, deltaDays);
				changed = rowChanged || dateChanged;
				selKind = "TASK";
			} else { // TaskEdgeL / TaskEdgeR
				// Candidate dates from the FRESHLY-read task (not the gesture-
				// start snapshot in g_dragOrigStart/End, which is unavailable
				// here — see this function's header comment): equivalent since
				// nothing else mutates the doc between gesture start and commit.
				if (const PpTask* task = FindTask(doc, id)) {
					std::string newStart, newEnd;
					ComputeDragCandidateDates(kind, task->start, task->end, deltaDays, newStart, newEnd);
					changed = SetTaskDates(doc, id, newStart, newEnd);
				}
				selKind = "TASK";
			}
			if (changed) {
				RebuildChart(doc, id);
				// A2: set the internal selection synchronously so it is never
				// momentarily empty (avoiding a deselect-then-reselect flicker).
				// Do NOT call SyncSelectionChromeFromOwnSelection() here: RebuildChart
				// just called InvalidateHitSnapshot(), so the hit snapshot is empty
				// until the next Tick()'s BuildRowBands walk repopulates it from the
				// freshly-rebuilt shapes — syncing against the stale/empty snapshot
				// right now would immediately clear the selection we just set (see
				// SyncSelectionChromeFromOwnSelection's "not found -> ClearOwnSelection"
				// fallback). The next Tick() calls it once the snapshot is valid again,
				// exactly like the existing HandleToolbarButton commit pattern.
				SetOwnSelection(selKind, id);
				RequestOverlayRepaint();
			}
		}
	}
	catch (const _com_error&) {
		OvLog(L"COM error committing drag gesture");
	}
	catch (const std::exception&) {
		OvLog(L"document error committing drag gesture");
	}
	catch (...) {
		OvLog(L"unknown error committing drag gesture");
	}
	g_mutating = false;
}

// Commit a CREATE gesture on WM_LBUTTONUP: adds a new task spanning
// [startDay, endDay] (inclusive, already normalized/clamped by the caller) in
// rowId, rebuilds, and selects the new task — mirrors CommitDragGesture's A2
// synchronous-selection pattern.
void CommitCreateGesture(const std::string& rowId, long startDay, long endDay) {
	if (rowId.empty() || !g_app || g_mutating) return;
	g_mutating = true;
	try {
		std::string json = ReadGanttFromSlide(g_app);
		if (!json.empty()) {
			PpDocument doc = DocumentFromJson(json);
			std::string startISO = DaysToDate(startDay);
			std::string endISO = DaysToDate(endDay);
			std::string newId = AddTask(doc, rowId, "New task", startISO, endISO);
			if (!newId.empty()) {
				RebuildChart(doc, newId);
				// A2, same reasoning as CommitDragGesture: set synchronously,
				// do not resync chrome here (stale/invalidated snapshot).
				SetOwnSelection("TASK", newId);
				RequestOverlayRepaint();
			}
		}
	}
	catch (const _com_error&) {
		OvLog(L"COM error committing create gesture");
	}
	catch (const std::exception&) {
		OvLog(L"document error committing create gesture");
	}
	catch (...) {
		OvLog(L"unknown error committing create gesture");
	}
	g_mutating = false;
}

// The overlay's HTCLIENT region swallows WM_MOUSEWHEEL/WM_MOUSEHWHEEL (they
// route to whichever window is under the cursor, which is us), so without
// this, Ctrl+wheel zoom and wheel-scroll are dead over the chart. Forward the
// message on to PowerPoint's main window unchanged. Falls back to a
// WindowFromPoint probe (excluding our own hwnd) if Tick() hasn't cached a
// PowerPoint hwnd yet, so the forward still works before the first tick.
void ForwardWheelToPowerPoint(HWND self, UINT msg, WPARAM wp, LPARAM lp) {
	HWND target = g_pptHwnd;
	if (!target || target == self) {
		POINT screenPt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		HWND under = ::WindowFromPoint(screenPt);
		while (under && under == self) {
			under = ::GetParent(under);
		}
		target = under;
	}
	if (!target || target == self) return;
	::PostMessageW(target, msg, wp, lp);
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	// Blanket exception net: this is a raw WndProc invoked by the Windows
	// message loop, so any exception escaping it (a COM error from a
	// PowerPoint call made deep inside one of the handlers below, etc.) would
	// unwind through non-C++-aware Windows/COM frames — undefined behavior at
	// best, a hard crash of the host PowerPoint process at worst. Every case
	// below keeps its existing return-value semantics on the success path;
	// only an actual throw is redirected here, falling through to the same
	// DefWindowProcW passthrough used for unhandled messages.
	try {
	if (msg == WM_NCHITTEST) {
		// Alt = escape hatch: let PowerPoint see the mouse so the user can
		// natively select/move the whole group under the overlay.
		if (::GetKeyState(VK_MENU) < 0) return HTTRANSPARENT;
		POINT screenPt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		// The overlay captures ALL mouse input over the chart area; PowerPoint
		// never sees chart clicks. Outside the chart, only the chrome widgets
		// (toolbar, hover '+', move grip) are interactive.
		if (::PtInRect(&g_chartScreenRect, screenPt)) return HTCLIENT;
		POINT pt = screenPt;
		::ScreenToClient(hwnd, &pt);
		return (ButtonFromClientPoint(pt) >= 0 || HoverInsertFromClientPoint(pt) || GripFromClientPoint(pt)) ? HTCLIENT : HTTRANSPARENT;
	}
	if (msg == WM_MOUSEACTIVATE) {
		return MA_NOACTIVATE;
	}
	if (msg == WM_SETCURSOR) {
		// Only decide the cursor when it's actually over OUR client area
		// (LOWORD(lp) is the hit-test result WM_NCHITTEST just returned); for
		// any other case (e.g. HTERROR while the window is being torn down)
		// fall through to DefWindowProcW like every unhandled message here.
		if (LOWORD(lp) == HTCLIENT && HandleSetCursor(hwnd)) return TRUE;
		return ::DefWindowProcW(hwnd, msg, wp, lp);
	}
	if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) {
		// WM_MOUSEWHEEL/HWHEEL carry screen coordinates in lp already (unlike
		// most mouse messages), so no client-to-screen conversion is needed.
		ForwardWheelToPowerPoint(hwnd, msg, wp, lp);
		return 0;
	}
	if (msg == WM_LBUTTONDOWN) {
		// While an editor (inline or card) is open, the overlay itself must
		// not start a new gesture/selection-change underneath it — the
		// editor's own top-level window would lose activation anyway (which
		// cancels it), but bouncing a click through to a drag/create gesture
		// first is exactly the kind of "gesture while editor is open" the
		// task spec asks to suppress.
		if (IsEditSessionActive()) return 0;
		POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		if (ButtonFromClientPoint(pt) >= 0 || HoverInsertFromClientPoint(pt) || GripFromClientPoint(pt)) return 0;
		// Route everything else through the semantic hit test and stash the
		// result (kept for parity/debugging; selection itself is decided on
		// the up, from the ORIGINAL down-hit, once we know this was a click).
		g_lastHit = HitTestClientPoint(pt);
		g_mouseDownPt = pt;
		g_captureActive = true;
		::SetCapture(hwnd);
		// Anchor a potential drag gesture on a draggable hit zone. This does
		// NOT commit to a drag yet — WM_MOUSEMOVE latches g_dragActive only
		// once the pointer crosses the threshold; a plain click still selects
		// on WM_LBUTTONUP exactly as before.
		StartDragGesture(g_lastHit, pt);
		// EmptyCell (a row's open timeline area, non-empty rowId): anchor a
		// create-drag instead. StartDragGesture above already no-op'd (wrong
		// zone), so gesture state is still idle here.
		if (g_lastHit.zone == HtZone::EmptyCell && !g_lastHit.rowId.empty()) {
			StartCreateGesture(g_lastHit, pt);
		}
		return 0;
	}
	if (msg == WM_MOUSEMOVE) {
		// lp is CLIENT coordinates for WM_MOUSEMOVE (unlike WM_MOUSEWHEEL) —
		// see the LOWORD/HIWORD idiom used throughout this handler.
		if (g_gestureActive) {
			POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
			UpdateDragGesture(pt);
		}
		return 0;
	}
	if (msg == WM_CAPTURECHANGED) {
		// Some other window stole capture mid-gesture (including our own
		// ReleaseCapture on LBUTTONUP, which also delivers this message):
		// cancel any in-progress drag gesture idempotently. Selection is left
		// exactly as it was (no click-select on an aborted gesture).
		g_captureActive = false;
		CancelDragGesture();
		return 0;
	}
	if (msg == WM_LBUTTONUP) {
		POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		bool wasCaptureActive = g_captureActive;
		// Snapshot the gesture-in-progress state BEFORE ReleaseCapture: capture
		// release on our own hwnd delivers WM_CAPTURECHANGED synchronously
		// (self-inflicted, per A4), and that handler calls CancelDragGesture()
		// to cover externally-stolen capture — which would otherwise wipe
		// g_gestureActive/g_dragActive/g_dragKind/etc. out from under us right
		// here. Recompute the final delta from THIS up-point (rather than
		// trusting g_dragDeltaDays from the last MOUSEMOVE) so the commit is
		// exact even if the up-point moved fractionally since that move.
		bool wasGestureActive = g_gestureActive;
		bool wasDragActive = g_dragActive;
		DragKind dragKind = g_dragKind;
		std::string dragId = g_dragId;
		long dragDeltaDays = g_dragDeltaDays;
		std::string dragTargetRowId = g_dragTargetRowId;
		std::string createRowId = g_createRowId;
		long createAnchorDay = g_createAnchorDay;
		long createCurrentDay = g_createCurrentDay;
		float gesturePxPerDay = g_dragPxPerDay; // snapshot: ReleaseCapture below zeroes the live global (see CREATE branch's isClick check)
		long finalDx = pt.x - g_mouseDownPt.x;
		if (wasGestureActive && gesturePxPerDay > 0.0f) {
			if (dragKind == DragKind::Create) {
				createCurrentDay = createAnchorDay + (long)::lround((double)finalDx / (double)gesturePxPerDay);
			} else {
				dragDeltaDays = (long)::lround((double)finalDx / (double)gesturePxPerDay);
			}
		}
		if (g_captureActive) {
			g_captureActive = false;
			::ReleaseCapture();
		}
		if (GripFromClientPoint(pt)) {
			try {
				SelectChartRoot();
			} catch (...) {
				OvLog(L"move-chart grip click failed");
			}
			return 0;
		}
		if (HoverInsertFromClientPoint(pt)) {
			try {
				HandleHoverInsertRow();
			} catch (...) {
				OvLog(L"hover row insert failed");
			}
			return 0;
		}
		int button = ButtonFromClientPoint(pt);
		if (button >= 0) {
			try {
				HandleToolbarButton(button);
			} catch (...) {
				OvLog(L"toolbar click failed");
			}
			return 0;
		}
		// Click-vs-drag: only a gesture that started with our WM_LBUTTONDOWN
		// (capture was active) decides anything here. A drag that crossed the
		// threshold (g_dragActive, latched by WM_MOUSEMOVE — never re-derived
		// from these up/down endpoints) commits via GanttOps; otherwise this
		// is a plain click and selects exactly as before.
		if (wasCaptureActive) {
			if (wasGestureActive && wasDragActive && dragKind == DragKind::Create) {
				// Create-drag: span < 0.5 day is treated as a click (no task
				// created) per the task spec — existing EmptyCell click
				// semantics (ClearOwnSelection, via ApplyClickSelection) apply
				// instead. 0.5 day is compared against the RAW pixel
				// displacement's day-equivalent (createCurrentDay vs
				// createAnchorDay is already rounded to whole days, so the
				// comparison is done in pixels here to preserve the half-day
				// threshold).
				double dayDx = (gesturePxPerDay > 0.0f) ? (double)finalDx / (double)gesturePxPerDay : 0.0;
				bool isClick = (dayDx > -0.5 && dayDx < 0.5);
				ResetDragGestureState();
				if (isClick) {
					HtHit hit = HitTestClientPoint(g_mouseDownPt);
					ApplyClickSelection(hit);
					SyncSelectionChromeFromOwnSelection();
					RequestOverlayRepaint();
				} else {
					long startDay = createAnchorDay, endDay = createCurrentDay;
					if (endDay < startDay) std::swap(startDay, endDay); // allow dragging left
					try {
						CommitCreateGesture(createRowId, startDay, endDay);
					} catch (...) {
						OvLog(L"create gesture commit failed");
					}
				}
			} else if (wasGestureActive && wasDragActive) {
				try {
					CommitDragGesture(dragKind, dragId, dragDeltaDays, dragTargetRowId);
				} catch (...) {
					OvLog(L"drag gesture commit failed");
				}
				ResetDragGestureState();
			} else {
				ResetDragGestureState();
				long dx = pt.x - g_mouseDownPt.x;
				long dy = pt.y - g_mouseDownPt.y;
				bool isClick = (dx >= -kDragThresholdPx && dx <= kDragThresholdPx &&
					dy >= -kDragThresholdPx && dy <= kDragThresholdPx);
				if (isClick) {
					HtHit hit = HitTestClientPoint(g_mouseDownPt);
					ApplyClickSelection(hit);
					SyncSelectionChromeFromOwnSelection();
					RequestOverlayRepaint();
				}
			}
		} else {
			ResetDragGestureState();
		}
		return 0;
	}
	if (msg == WM_RBUTTONDOWN) {
		// Right-click owns the chart's context menu (V2): hit-test, set the
		// internal selection to the hit element (standard convention: a
		// right-click on an unselected item selects it first, same as
		// PowerPoint's own native behavior), and repaint so the chrome
		// reflects it before the popup appears. Actually showing/handling the
		// menu happens on the UP (see WM_RBUTTONUP) — TrackPopupMenuEx wants
		// TPM_RIGHTBUTTON with the button already released, matching the
		// standard Windows down-then-up-shows-menu right-click idiom.
		POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		HtHit hit = HitTestClientPoint(pt);
		ApplyClickSelection(hit);
		SyncSelectionChromeFromOwnSelection();
		RequestOverlayRepaint();
		return 0;
	}
	if (msg == WM_RBUTTONUP) {
		POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		HtHit hit = HitTestClientPoint(pt);
		try {
			ShowContextMenuForHit(hit, pt);
		} catch (...) {
			OvLog(L"context menu display/handling failed");
		}
		return 0;
	}
	if (msg == WM_LBUTTONDBLCLK) {
		POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		HtHit hit = HitTestClientPoint(pt);
		if (hit.zone == HtZone::TaskBody || hit.zone == HtZone::TaskEdgeL ||
			hit.zone == HtZone::TaskEdgeR || hit.zone == HtZone::Milestone) {
			// The floating card editor (V2): double-clicking a bar or
			// milestone opens the richer editor instead of the simpler
			// TITLE/ROW_LABEL inline box. Resolve the item's CURRENT screen
			// rect from the hit snapshot (same source g_lastHit/drag-start
			// use) so the card anchors to where the bar actually is now.
			const char* wantKindStr = (hit.zone == HtZone::Milestone) ? "MILESTONE" : "TASK";
			for (const auto& item : g_hitSnapshot.items) {
				bool kindMatches = (hit.zone == HtZone::Milestone) ? (item.kind == HtItemKind::Milestone) : (item.kind == HtItemKind::Task);
				if (kindMatches && item.id == hit.id) {
					RECT screenRect = { item.rect.left, item.rect.top, item.rect.right, item.rect.bottom };
					OpenCardEditor(wantKindStr, hit.id, screenRect);
					return 0;
				}
			}
			return 0;
		}
		if (hit.zone == HtZone::Label) {
			const char* kindStr = (hit.kind == HtItemKind::Title) ? "TITLE" : "ROW_LABEL";
			for (const auto& region : g_editRegions) {
				if (region.kind == kindStr && region.id == hit.id) {
					OpenInlineEditor(region);
					return 0;
				}
			}
		}
		// Fallback for edit regions not represented in the snapshot (none
		// today, but keeps behavior if the walk and regions ever diverge).
		if (const EditRegion* region = EditRegionFromClientPoint(pt)) {
			OpenInlineEditor(*region);
			return 0;
		}
		return 0;
	}
	if (msg == WM_PAINT) {
		// Content is pushed by UpdateLayeredWindow (RenderOverlay); WM_PAINT
		// only needs to validate the region so the queue stays quiet.
		::ValidateRect(hwnd, NULL);
		return 0;
	}
	if (msg == WM_HOTKEY) {
		// Delivered even though this NOACTIVATE window never has keyboard
		// focus (see kHotkeySpecs' header comment / keys-probe.txt OPTION C).
		// wp is the id passed to RegisterHotKey; dispatch on it directly
		// rather than re-deriving the key from lp's packed vk/mods.
		try {
			switch ((int)wp) {
			case HOTKEY_DELETE:
				HandleHotkeyDelete();
				break;
			case HOTKEY_LEFT:
				HandleHotkeyNudge(-1);
				break;
			case HOTKEY_RIGHT:
				HandleHotkeyNudge(1);
				break;
			case HOTKEY_SHIFT_LEFT:
				HandleHotkeyNudge(-7);
				break;
			case HOTKEY_SHIFT_RIGHT:
				HandleHotkeyNudge(7);
				break;
			default:
				break;
			}
		} catch (...) {
			OvLog(L"WM_HOTKEY handler failed");
		}
		return 0;
	}
	return ::DefWindowProcW(hwnd, msg, wp, lp);
	} catch (...) {
		OvLog(L"OverlayWndProc: exception caught, falling back to DefWindowProcW");
		return ::DefWindowProcW(hwnd, msg, wp, lp);
	}
}

LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_CLOSE) {
		CancelInlineEdit();
		return 0;
	}
	if (msg == WM_SIZE && g_editHwnd) {
		RECT rc;
		::GetClientRect(hwnd, &rc);
		::MoveWindow(g_editHwnd, 0, 0, rc.right - rc.left, rc.bottom - rc.top, TRUE);
		return 0;
	}
	if (msg == WM_DESTROY && hwnd == g_editorHwnd) {
		g_editorHwnd = NULL;
		g_editHwnd = NULL;
		g_oldEditProc = NULL;
	}
	return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK InlineEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_KEYDOWN) {
		if (wp == VK_RETURN) {
			CommitInlineEdit();
			return 0;
		}
		if (wp == VK_ESCAPE) {
			CancelInlineEdit();
			return 0;
		}
	}
	if (msg == WM_KILLFOCUS && !g_editClosing) {
		CommitInlineEdit();
		return 0;
	}
	return g_oldEditProc ? ::CallWindowProcW(g_oldEditProc, hwnd, msg, wp, lp) : ::DefWindowProcW(hwnd, msg, wp, lp);
}

// Subclass shared by the card's 4 EDIT children (label/start/end/percent).
// Enter commits the WHOLE card (not just this field — matches the task
// spec's "Enter ... commits ALL changed fields in ONE commit"); Esc cancels
// the whole card. Kill-focus does nothing here (unlike the inline editor):
// focus moving between the card's OWN child controls (Tab, or clicking
// another field/swatch) must NOT cancel or commit — only WM_ACTIVATE
// (focus leaving the card window entirely) does that; see CardWndProc.
LRESULT CALLBACK CardFieldProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_KEYDOWN) {
		if (wp == VK_RETURN) {
			CommitCardEdit();
			return 0;
		}
		if (wp == VK_ESCAPE) {
			CancelCardEdit();
			return 0;
		}
	}
	// Typing into a field that was showing the invalid cue clears it
	// immediately (re-validation happens for real on the next commit
	// attempt) so the red border doesn't linger once the user starts fixing
	// their input.
	if (msg == WM_CHAR && g_cardInvalid) {
		SetCardInvalid(false);
	}
	return g_oldCardFieldProc ? ::CallWindowProcW(g_oldCardFieldProc, hwnd, msg, wp, lp) : ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK CardWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_CLOSE) {
		CancelCardEdit();
		return 0;
	}
	if (msg == WM_COMMAND) {
		int id = LOWORD(wp);
		int notify = HIWORD(wp);
		if (id == CARD_ID_OK && notify == BN_CLICKED) {
			CommitCardEdit();
			return 0;
		}
		if (id >= CARD_ID_SWATCH_BASE && id < CARD_ID_SWATCH_BASE + kCardSwatchCount && notify == BN_CLICKED) {
			g_cardSelectedSwatch = id - CARD_ID_SWATCH_BASE;
			return 0;
		}
	}
	if (msg == WM_CTLCOLOREDIT && g_cardInvalid) {
		HWND ctl = (HWND)lp;
		if (ctl == g_cardStartHwnd || ctl == g_cardEndHwnd) {
			HDC dc = (HDC)wp;
			::SetBkColor(dc, RGB(0xFD, 0xE7, 0xE9)); // pale red wash: the "red border" invalid cue
			static HBRUSH invalidBrush = ::CreateSolidBrush(RGB(0xFD, 0xE7, 0xE9));
			return (LRESULT)invalidBrush;
		}
	}
	// Losing activation (focus leaving the card entirely — clicking the
	// slide, Alt+Tab, etc.) cancels per the task spec. Activation moving
	// between the card's OWN child controls does NOT generate WM_ACTIVATE
	// on the top-level window (only WM_KILLFOCUS on the child, which the
	// subclass above deliberately ignores), so this only fires on a genuine
	// focus-loss.
	if (msg == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE && !g_cardClosing) {
		CancelCardEdit();
		return 0;
	}
	if (msg == WM_DESTROY && hwnd == g_cardHwnd) {
		g_cardHwnd = NULL;
	}
	return ::DefWindowProcW(hwnd, msg, wp, lp);
}

void EnsureWindow() {
	if (g_hwnd) return;
	WNDCLASSEXW wc = { sizeof(wc) };
	wc.lpfnWndProc = OverlayWndProc;
	wc.hInstance = g_inst;
	wc.style = CS_DBLCLKS;
	wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = kClass;
	::RegisterClassExW(&wc);

	// NOTE: no SetLayeredWindowAttributes here — the window is driven purely by
	// UpdateLayeredWindow (per-pixel premultiplied alpha). A layered window
	// without attributes stays invisible until the first RenderOverlay() push.
	g_hwnd = ::CreateWindowExW(
		WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
		kClass, L"", WS_POPUP,
		0, 0, 10, 10, NULL, NULL, g_inst, NULL);
}

// ---- premultiplied-alpha painting (GDI+) -----------------------------------

Gdiplus::Color GpColor(BYTE a, COLORREF c) {
	return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

void AddRoundRect(Gdiplus::GraphicsPath& path, Gdiplus::REAL x, Gdiplus::REAL y,
	Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL r) {
	if (w <= 0.0f || h <= 0.0f) return;
	Gdiplus::REAL maxR = (w < h ? w : h) / 2.0f;
	if (r > maxR) r = maxR;
	if (r <= 0.0f) {
		path.AddRectangle(Gdiplus::RectF(x, y, w, h));
		return;
	}
	Gdiplus::REAL d = r * 2.0f;
	path.StartFigure();
	path.AddArc(x, y, d, d, 180.0f, 90.0f);
	path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
	path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
	path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
	path.CloseFigure();
}

void DrawHandle(Gdiplus::Graphics& g, int cx, int cy, int r) {
	using namespace Gdiplus;
	GraphicsPath path;
	AddRoundRect(path, (REAL)(cx - r), (REAL)(cy - r), (REAL)(2 * r + 1), (REAL)(2 * r + 1), 1.5f);
	SolidBrush fill(Color(255, 255, 255, 255));
	g.FillPath(&fill, &path);
	GraphicsPath innerPath;
	AddRoundRect(innerPath, (REAL)(cx - r + 1), (REAL)(cy - r + 1), (REAL)(2 * r - 1), (REAL)(2 * r - 1), 1.0f);
	Pen inner(GpColor(255, HANDLE_INNER), 1.0f);
	g.DrawPath(&inner, &innerPath);
	Pen border(GpColor(255, ACCENT), 1.0f);
	g.DrawPath(&border, &path);
}

void PaintOverlay(Gdiplus::Graphics& g, int W, int H) {
	using namespace Gdiplus;
	// Background stays fully transparent (alpha 0) — the caller cleared it.

	LayoutToolbarButtons(W, H);
	LayoutHoverInsertHotspot();
	LayoutGrip(W, H);

	// 'Move chart' grip chip (top-right, in the badge strip): white chip with an
	// accent four-way move glyph. Clicking it selects the CHART_ROOT group.
	if (g_gripValid) {
		GraphicsPath gripPath;
		AddRoundRect(gripPath, (REAL)g_gripRect.left, (REAL)g_gripRect.top,
			(REAL)(g_gripRect.right - g_gripRect.left), (REAL)(g_gripRect.bottom - g_gripRect.top), 3.0f);
		SolidBrush gripFill(GpColor(255, SURFACE));
		g.FillPath(&gripFill, &gripPath);
		Pen gripBorder(GpColor(255, ACCENT), 1.0f);
		g.DrawPath(&gripBorder, &gripPath);

		int cx = (g_gripRect.left + g_gripRect.right) / 2;
		int cy = (g_gripRect.top + g_gripRect.bottom) / 2;
		int g4 = Scale(4), g2 = Scale(2);
		Pen glyph(GpColor(255, ACCENT), ScaleF(1.4f));
		g.DrawLine(&glyph, cx - g4, cy, cx + g4, cy);
		g.DrawLine(&glyph, cx, cy - g4, cx, cy + g4);
		g.DrawLine(&glyph, cx - g4, cy, cx - g2, cy - g2);
		g.DrawLine(&glyph, cx - g4, cy, cx - g2, cy + g2);
		g.DrawLine(&glyph, cx + g4, cy, cx + g2, cy - g2);
		g.DrawLine(&glyph, cx + g4, cy, cx + g2, cy + g2);
		g.DrawLine(&glyph, cx, cy - g4, cx - g2, cy - g2);
		g.DrawLine(&glyph, cx, cy - g4, cx + g2, cy - g2);
		g.DrawLine(&glyph, cx, cy + g4, cx - g2, cy + g2);
		g.DrawLine(&glyph, cx, cy + g4, cx + g2, cy + g2);
	}

	if (!g_hoverRowId.empty() && !::IsRectEmpty(&g_hoverBandRect) && !(::GetKeyState(VK_LBUTTON) & 0x8000)) {
		RECT band = {
			g_hoverBandRect.left - g_windowOriginX,
			g_hoverBandRect.top - g_windowOriginY,
			g_hoverBandRect.right - g_windowOriginX,
			g_hoverBandRect.bottom - g_windowOriginY
		};
		// Genuinely translucent accent wash over the whole hovered row band.
		SolidBrush wash(GpColor(HOVER_WASH_ALPHA, ACCENT));
		g.FillRectangle(&wash, (INT)band.left, (INT)band.top,
			(INT)(band.right - band.left), (INT)(band.bottom - band.top));
		// Soft accent edge lines top/bottom.
		Pen edge(GpColor(110, ACCENT), 1.0f);
		g.DrawLine(&edge, (INT)band.left, (INT)band.top, (INT)band.right, (INT)band.top);
		g.DrawLine(&edge, (INT)band.left, (INT)band.bottom, (INT)band.right, (INT)band.bottom);
		// Solid accent bar on the left edge.
		SolidBrush bar(GpColor(255, ACCENT));
		g.FillRectangle(&bar, (INT)band.left, (INT)band.top, Scale(3), (INT)(band.bottom - band.top));

		if (g_hoverInsertValid) {
			INT ex = (INT)g_hoverInsertRect.left, ey = (INT)g_hoverInsertRect.top;
			INT ew = (INT)(g_hoverInsertRect.right - g_hoverInsertRect.left);
			INT eh = (INT)(g_hoverInsertRect.bottom - g_hoverInsertRect.top);
			SolidBrush plusFill(GpColor(255, SURFACE));
			g.FillEllipse(&plusFill, ex, ey, ew, eh);
			Pen plusPen(GpColor(255, ACCENT), 1.0f);
			g.DrawEllipse(&plusPen, ex, ey, ew, eh);

			Pen glyphPen(GpColor(255, ACCENT), ScaleF(2.0f));
			int cx = (g_hoverInsertRect.left + g_hoverInsertRect.right) / 2;
			int cy = (g_hoverInsertRect.top + g_hoverInsertRect.bottom) / 2;
			int g4 = Scale(4);
			g.DrawLine(&glyphPen, cx - g4, cy, cx + g4, cy);
			g.DrawLine(&glyphPen, cx, cy - g4, cx, cy + g4);
		}
	}

	// Drag-move-resize / row-reassign / create ghost: a translucent preview of
	// where the dragged task/milestone will land (or the new task that will
	// be created), plus a small tooltip with the candidate dates (+ target
	// row for a row-reassign). Drawn from gesture state only — no COM here
	// (PaintOverlay runs off UpdateLayeredWindow's back-buffer path, not a
	// tick).
	if (g_gestureActive && g_dragActive && g_dragKind == DragKind::Create) {
		RECT band = {
			g_dragTargetRowRect.left - g_windowOriginX,
			g_dragTargetRowRect.top - g_windowOriginY,
			g_dragTargetRowRect.right - g_windowOriginX,
			g_dragTargetRowRect.bottom - g_windowOriginY
		};
		long lowDay = g_createAnchorDay, highDay = g_createCurrentDay;
		if (highDay < lowDay) std::swap(lowDay, highDay);
		// Reconstruct screen-x from day using the same reference the gesture
		// start anchored against: g_dragLastPt/g_mouseDownPt give us ONE
		// known (day,x) pair — the anchor itself — so every other day's x is
		// anchorScreenXAtDown + (day - anchorDay) * pxPerDay.
		long baseX = g_mouseDownPt.x;
		int ghostLeft = baseX + (int)::lround((double)(lowDay - g_createAnchorDay) * (double)g_dragPxPerDay);
		int ghostRight = baseX + (int)::lround((double)(highDay + 1 - g_createAnchorDay) * (double)g_dragPxPerDay);
		if (ghostRight < ghostLeft + 2) ghostRight = ghostLeft + 2;

		GraphicsPath ghostPath;
		AddRoundRect(ghostPath, (REAL)ghostLeft, (REAL)band.top,
			(REAL)(ghostRight - ghostLeft), (REAL)(band.bottom - band.top), 3.0f);
		SolidBrush ghostFill(GpColor(90, ACCENT));
		g.FillPath(&ghostFill, &ghostPath);
		Pen ghostPen(GpColor(200, ACCENT), 1.5f);
		g.DrawPath(&ghostPen, &ghostPath);

		std::wstring tip = Widen(DaysToDate(lowDay)) + L" → " + Widen(DaysToDate(highDay));
		Gdiplus::Font tipFont(L"Segoe UI", g_tooltipFontPx, FontStyleRegular, UnitPixel);
		RectF tipBounds;
		g.MeasureString(tip.c_str(), -1, &tipFont, PointF(0, 0), &tipBounds);
		int tipPad = g_tooltipPad;
		int tipOffset = Scale(14);
		int tipX = g_dragLastPt.x + tipOffset;
		int tipY = g_dragLastPt.y + tipOffset;
		int tipW = (int)tipBounds.Width + tipPad * 2;
		int tipH = (int)tipBounds.Height + tipPad * 2;
		GraphicsPath tipPath;
		AddRoundRect(tipPath, (REAL)tipX, (REAL)tipY, (REAL)tipW, (REAL)tipH, 3.0f);
		SolidBrush tipBg(GpColor(235, RGB(32, 33, 36)));
		g.FillPath(&tipBg, &tipPath);
		StringFormat tipFmt;
		tipFmt.SetAlignment(StringAlignmentCenter);
		tipFmt.SetLineAlignment(StringAlignmentCenter);
		SolidBrush tipText(Color(255, 255, 255, 255));
		g.DrawString(tip.c_str(), -1, &tipFont, RectF((REAL)tipX, (REAL)tipY, (REAL)tipW, (REAL)tipH), &tipFmt, &tipText);
	} else if (g_gestureActive && g_dragActive) {
		RECT anchor = {
			g_dragAnchorRect.left - g_windowOriginX,
			g_dragAnchorRect.top - g_windowOriginY,
			g_dragAnchorRect.right - g_windowOriginX,
			g_dragAnchorRect.bottom - g_windowOriginY
		};
		long shiftPx = (long)::lround((double)g_dragDeltaDays * (double)g_dragPxPerDay);
		RECT ghost = anchor;
		switch (g_dragKind) {
		case DragKind::TaskEdgeL:
			ghost.left = anchor.left + shiftPx;
			if (ghost.left > ghost.right - 2) ghost.left = ghost.right - 2; // keep end>=start visually too
			break;
		case DragKind::TaskEdgeR:
			ghost.right = anchor.right + shiftPx;
			if (ghost.right < ghost.left + 2) ghost.right = ghost.left + 2;
			break;
		case DragKind::TaskBody:
			ghost.left = anchor.left + shiftPx;
			ghost.right = anchor.right + shiftPx;
			if (!::IsRectEmpty(&g_dragTargetRowRect)) {
				// Retarget the ghost's y to the row band under the pointer
				// (vertical row-reassign), keeping the horizontal shift.
				int bandTop = g_dragTargetRowRect.top - g_windowOriginY;
				int bandBottom = g_dragTargetRowRect.bottom - g_windowOriginY;
				int h = anchor.bottom - anchor.top;
				int cy = (bandTop + bandBottom) / 2;
				ghost.top = cy - h / 2;
				ghost.bottom = ghost.top + h;
			}
			break;
		case DragKind::Milestone:
		default:
			ghost.left = anchor.left + shiftPx;
			ghost.right = anchor.right + shiftPx;
			break;
		}

		if (g_dragKind == DragKind::Milestone) {
			// Diamond shifted with the gesture, matching the milestone marker.
			int cx = (ghost.left + ghost.right) / 2;
			int cy = (anchor.top + anchor.bottom) / 2;
			int rx = (anchor.right - anchor.left) / 2;
			int ry = (anchor.bottom - anchor.top) / 2;
			GraphicsPath diamond;
			Gdiplus::Point pts[4] = {
				Gdiplus::Point(cx, cy - ry), Gdiplus::Point(cx + rx, cy),
				Gdiplus::Point(cx, cy + ry), Gdiplus::Point(cx - rx, cy)
			};
			diamond.AddPolygon(pts, 4);
			SolidBrush ghostFill(GpColor(140, ACCENT));
			g.FillPath(&ghostFill, &diamond);
			Pen ghostPen(GpColor(220, ACCENT), 1.5f);
			g.DrawPath(&ghostPen, &diamond);
		} else {
			GraphicsPath ghostPath;
			AddRoundRect(ghostPath, (REAL)ghost.left, (REAL)ghost.top,
				(REAL)(ghost.right - ghost.left), (REAL)(ghost.bottom - ghost.top), 3.0f);
			SolidBrush ghostFill(GpColor(110, ACCENT));
			g.FillPath(&ghostFill, &ghostPath);
			Pen ghostPen(GpColor(220, ACCENT), 1.5f);
			g.DrawPath(&ghostPen, &ghostPath);
		}

		// Tooltip: candidate dates near the cursor, plus the target row when
		// a TaskBody drag has retargeted to a different row than it started.
		std::string candStart, candEnd;
		ComputeDragCandidateDates(candStart, candEnd);
		std::wstring tip = Widen(candStart) + L" → " + Widen(candEnd);
		if (g_dragKind == DragKind::TaskBody && !g_dragTargetRowId.empty() && g_dragTargetRowId != g_dragOrigRowId) {
			tip += L"  (" + Widen(g_dragTargetRowId) + L")";
		}
		Gdiplus::Font tipFont(L"Segoe UI", g_tooltipFontPx, FontStyleRegular, UnitPixel);
		RectF tipBounds;
		g.MeasureString(tip.c_str(), -1, &tipFont, PointF(0, 0), &tipBounds);
		int tipPad = g_tooltipPad;
		int tipOffset = Scale(14);
		int tipX = g_dragLastPt.x + tipOffset;
		int tipY = g_dragLastPt.y + tipOffset;
		int tipW = (int)tipBounds.Width + tipPad * 2;
		int tipH = (int)tipBounds.Height + tipPad * 2;
		GraphicsPath tipPath;
		AddRoundRect(tipPath, (REAL)tipX, (REAL)tipY, (REAL)tipW, (REAL)tipH, 3.0f);
		SolidBrush tipBg(GpColor(235, RGB(32, 33, 36)));
		g.FillPath(&tipBg, &tipPath);
		StringFormat tipFmt;
		tipFmt.SetAlignment(StringAlignmentCenter);
		tipFmt.SetLineAlignment(StringAlignmentCenter);
		SolidBrush tipText(Color(255, 255, 255, 255));
		g.DrawString(tip.c_str(), -1, &tipFont, RectF((REAL)tipX, (REAL)tipY, (REAL)tipW, (REAL)tipH), &tipFmt, &tipText);
	}

	if (!g_hasSelectionChrome || ::IsRectEmpty(&g_frameRect)) return;

	RECT frame = g_frameRect;
	REAL fx = (REAL)frame.left, fy = (REAL)frame.top;
	REAL fw = (REAL)(frame.right - frame.left), fh = (REAL)(frame.bottom - frame.top);

	// Soft halo behind the frame, then the crisp accent frame itself. Stroke
	// widths and the halo's outward inflation are DPI-scaled so the chrome
	// stays proportioned (not hairline-thin) at high scale factors.
	REAL haloInfl = ScaleF(1.5f);
	REAL haloPenW = ScaleF(4.0f);
	REAL framePenW = ScaleF(2.0f);
	{
		GraphicsPath halo;
		AddRoundRect(halo, fx - haloInfl, fy - haloInfl, fw + haloInfl * 2.0f, fh + haloInfl * 2.0f, 4.5f);
		Pen haloPen(GpColor(56, ACCENT), haloPenW);
		g.DrawPath(&haloPen, &halo);

		GraphicsPath framePath;
		AddRoundRect(framePath, fx, fy, fw, fh, 3.0f);
		Pen framePen(GpColor(255, ACCENT), framePenW);
		g.DrawPath(&framePen, &framePath);
	}

	// 8 handles (white fill, accent border)
	int mx = (frame.left + frame.right) / 2, my = (frame.top + frame.bottom) / 2;
	int xs[3] = { (int)frame.left, mx, (int)frame.right };
	int ys[3] = { (int)frame.top, my, (int)frame.bottom };
	int handleR = Scale(3);
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j) {
			if (i == 1 && j == 1) continue;
			DrawHandle(g, xs[i], ys[j], handleR);
		}

	// badge: filled Material chip with white label at top-left
	int bw = Scale(96), bh = BADGE_H - Scale(4);
	int badgeTop = std::max(Scale(2), (int)frame.top - BADGE_H - Scale(3));
	{
		GraphicsPath badgePath;
		AddRoundRect(badgePath, fx, (REAL)badgeTop, (REAL)bw, (REAL)bh, 4.0f);
		SolidBrush badgeBrush(GpColor(255, ACCENT));
		g.FillPath(&badgeBrush, &badgePath);

		Gdiplus::Font badgeFont(L"Segoe UI", g_badgeFontPx, FontStyleRegular, UnitPixel);
		StringFormat sf;
		sf.SetAlignment(StringAlignmentCenter);
		sf.SetLineAlignment(StringAlignmentCenter);
		sf.SetFormatFlags(StringFormatFlagsNoWrap);
		SolidBrush textBrush(Color(255, 255, 255, 255));
		g.DrawString(g_badge.c_str(), -1, &badgeFont,
			RectF(fx, (REAL)badgeTop, (REAL)bw, (REAL)bh), &sf, &textBrush);
	}

	// floating Material mini-toolbar
	if (g_buttonsValid) {
		int bgPad6 = Scale(6), bgPad3 = Scale(3);
		RECT bg = g_buttonRects[0];
		bg.left -= bgPad6; bg.top -= bgPad3; bg.right = g_buttonRects[BUTTON_COUNT - 1].right + bgPad6; bg.bottom += bgPad3;
		GraphicsPath bgPath;
		AddRoundRect(bgPath, (REAL)bg.left, (REAL)bg.top,
			(REAL)(bg.right - bg.left), (REAL)(bg.bottom - bg.top), 5.0f);
		SolidBrush bgBrush(GpColor(255, SURFACE));
		g.FillPath(&bgBrush, &bgPath);
		Pen bgPen(GpColor(255, RGB(218, 220, 224)), 1.0f);
		g.DrawPath(&bgPen, &bgPath);

		Gdiplus::Font btnFont(L"Segoe UI", g_buttonFontPx, FontStyleBold, UnitPixel);
		StringFormat sf;
		sf.SetAlignment(StringAlignmentCenter);
		sf.SetLineAlignment(StringAlignmentCenter);
		sf.SetFormatFlags(StringFormatFlagsNoWrap);
		SolidBrush textBrush(GpColor(255, TEXT));
		const wchar_t* labels[BUTTON_COUNT] = { L"Add", L"Del", L"-", L"+" };
		for (int i = 0; i < BUTTON_COUNT; ++i) {
			const RECT& br = g_buttonRects[i];
			GraphicsPath btnPath;
			AddRoundRect(btnPath, (REAL)br.left, (REAL)br.top,
				(REAL)(br.right - br.left), (REAL)(br.bottom - br.top), 4.0f);
			SolidBrush btnBrush(GpColor(255, i == BTN_ADD ? RGB(232, 240, 254) : SURFACE_VARIANT));
			g.FillPath(&btnBrush, &btnPath);
			Pen btnPen(GpColor(255, i == BTN_ADD ? ACCENT : RGB(218, 220, 224)), 1.0f);
			g.DrawPath(&btnPen, &btnPath);
			g.DrawString(labels[i], -1, &btnFont,
				RectF((REAL)br.left, (REAL)br.top,
					(REAL)(br.right - br.left), (REAL)(br.bottom - br.top)),
				&sf, &textBrush);
		}
	}
}

// ---- back buffer + UpdateLayeredWindow push --------------------------------

void FreeBackBuffer() {
	if (g_bufDc) {
		if (g_bufOld) ::SelectObject(g_bufDc, g_bufOld);
		::DeleteDC(g_bufDc);
		g_bufDc = NULL;
		g_bufOld = NULL;
	}
	if (g_bufBmp) {
		::DeleteObject(g_bufBmp);
		g_bufBmp = NULL;
	}
	g_bufBits = nullptr;
	g_bufW = 0;
	g_bufH = 0;
}

bool EnsureBackBuffer(int w, int h) {
	if (g_bufDc && g_bufBits && g_bufW == w && g_bufH == h) return true;
	FreeBackBuffer();
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;  // top-down so GDI+ stride is simply w*4
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HDC screen = ::GetDC(NULL);
	HBITMAP bmp = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
	HDC dc = ::CreateCompatibleDC(screen);
	::ReleaseDC(NULL, screen);
	if (!bmp || !dc || !bits) {
		if (dc) ::DeleteDC(dc);
		if (bmp) ::DeleteObject(bmp);
		return false;
	}
	g_bufOld = ::SelectObject(dc, bmp);
	g_bufDc = dc;
	g_bufBmp = bmp;
	g_bufBits = bits;
	g_bufW = w;
	g_bufH = h;
	return true;
}

// Compose the full overlay into the 32-bpp premultiplied back buffer and push
// it to the screen with UpdateLayeredWindow. Never lets exceptions escape (it
// runs from the timer tick inside PowerPoint's process).
void RenderOverlay() {
	if (!g_hwnd || !g_gdiplusToken) return;
	RECT wr;
	if (!::GetWindowRect(g_hwnd, &wr)) return;
	int w = wr.right - wr.left, h = wr.bottom - wr.top;
	if (w <= 0 || h <= 0) return;
	if (!EnsureBackBuffer(w, h)) return;
	try {
		Gdiplus::Bitmap surface(w, h, w * 4, PixelFormat32bppPARGB, (BYTE*)g_bufBits);
		if (surface.GetLastStatus() != Gdiplus::Ok) {
			OvLog(L"overlay render skipped (bitmap status)");
			return;
		}
		Gdiplus::Graphics g(&surface);
		if (g.GetLastStatus() != Gdiplus::Ok) {
			OvLog(L"overlay render skipped (graphics status)");
			return;
		}
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
		g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
		g.Clear(Gdiplus::Color(0, 0, 0, 0));
		PaintOverlay(g, w, h);
		g.Flush(Gdiplus::FlushIntentionSync);
		if (g.GetLastStatus() != Gdiplus::Ok) {
			OvLog(L"overlay render skipped (paint status)");
			return;
		}

		POINT dst = { wr.left, wr.top };
		POINT src = { 0, 0 };
		SIZE  size = { w, h };
		BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
		::UpdateLayeredWindow(g_hwnd, NULL, &dst, &size, g_bufDc, &src, 0, &bf, ULW_ALPHA);
	}
	catch (const std::exception&) {
		OvLog(L"overlay render failed (std::exception)");
	}
	catch (...) {
		OvLog(L"overlay render failed");
	}
}

void HideOverlay() {
	if (g_shown && g_hwnd) { ::ShowWindow(g_hwnd, SW_HIDE); g_shown = false; }
	g_buttonsValid = false;
	ClearSelectionState();
	ClearOwnSelection();
	CloseCardEditor(); // chart/slide went away (or is hidden): no editor can stay meaningfully open
	UnregisterAllHotkeys(); // no selection left to Delete/Nudge; release any stolen keys

	if (g_captureActive) {
		g_captureActive = false;
		if (g_hwnd && ::GetCapture() == g_hwnd) ::ReleaseCapture();
	}
	ResetDragGestureState();
	ClearHoverState();
	g_lastLeftButtonDown = false;
	g_rowBands.clear();
	g_editRegions.clear();
	InvalidateHitSnapshot();
	g_lastHit = HtHit{};
	g_gripValid = false;
	::SetRectEmpty(&g_gripRect);
	::SetRectEmpty(&g_chartScreenRect);
	g_chartProj.clear();
}

void ShowOverlayForChartRect(const RECT& chart) {
	EnsureWindow();
	if (!g_hwnd) return;
	// Re-probe DPI on every (re)position: the overlay can be dragged to a
	// different-DPI monitor, or the user can change scaling live, between
	// ticks. A change invalidates the hit snapshot + forces a relayout/repaint
	// (via UpdateDpiScaledMetrics) so chrome and hit zones never lag behind.
	bool dpiChanged = UpdateDpiForWindow(g_hwnd);
	if (dpiChanged) UpdateDpiScaledMetrics();
	int chartW = chart.right - chart.left;
	int chartH = chart.bottom - chart.top;
	int wx = chart.left - INFL, wy = chart.top - INFL - BADGE_H;
	int ww = chartW + INFL * 2, wh = chartH + INFL * 2 + BADGE_H + TOOLBAR_H;
	g_windowOriginX = wx;
	g_windowOriginY = wy;
	UpdateSelectionFrameFromScreen();
	const int toolbarMinW = 2 * (INFL + Scale(6)) + BUTTON_COUNT * g_buttonW + (BUTTON_COUNT - 1) * g_buttonGap;
	if (ww < toolbarMinW) ww = toolbarMinW;
	if (wh < BADGE_H + TOOLBAR_H + INFL * 2 + Scale(8)) wh = BADGE_H + TOOLBAR_H + INFL * 2 + Scale(8);
	LayoutToolbarButtons(ww, wh);
	LayoutGrip(ww, wh);
	RECT oldWindow = {};
	bool wasShown = g_shown;
	bool hadWindow = ::GetWindowRect(g_hwnd, &oldWindow) != FALSE;
	::SetWindowPos(g_hwnd, HWND_TOPMOST, wx, wy, ww, wh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	g_shown = true;
	if (dpiChanged || !wasShown || !hadWindow || oldWindow.left != wx || oldWindow.top != wy || oldWindow.right - oldWindow.left != ww || oldWindow.bottom - oldWindow.top != wh) {
		RequestOverlayRepaint();
	}
}

// Single choke point for every user-gesture commit (drag, create, toolbar
// button, hover-insert row, inline-edit) — see CommitDragGesture/
// CommitCreateGesture/HandleToolbarButton/HandleHoverInsertRow/
// CommitInlineEdit, all of which call this. StartNewUndoEntryIfPossible()
// MUST run before the mutation is applied to PowerPoint so the whole gesture
// (including UpdateGantt's occasional ungroup/regroup on structural edits)
// collapses into one undo entry. UpdateGantt reconciles the existing
// CHART_ROOT in place when possible (pure move/resize/retext never deletes
// or regroups anything) and only falls back to a full delete+re-emit
// (InsertGantt) internally if reconciliation itself fails.
void RebuildChart(PpDocument& doc, const std::string& selectId) {
	StartNewUndoEntryIfPossible();
	InvalidateHitSnapshot();
	HRESULT hr = UpdateGantt(g_app, doc, selectId);
	if (FAILED(hr)) OvLog(L"UpdateGantt failed after gesture commit");
}

// ---- keyboard hotkey handlers ------------------------------------------------
// Both follow the standard commit pattern used throughout this file (see
// RebuildChart's header comment): read PP_DOC, mutate via GanttOps, rebuild,
// SetOwnSelection synchronously (RebuildChart just invalidated the hit
// snapshot, so — same reasoning as CommitDragGesture/HandleContextMenuCommand
// — SyncSelectionChromeFromOwnSelection must wait for the next Tick()).

// WM_HOTKEY(Delete): delete the selected task/milestone and clear selection.
void HandleHotkeyDelete() {
	if (!g_app || g_mutating) return;
	const std::string selId = g_ownSelId;
	const std::string selKind = g_ownSelKind;
	if (selId.empty() || (selKind != "TASK" && selKind != "MILESTONE")) return;

	g_mutating = true;
	try {
		std::string json = ReadGanttFromSlide(g_app);
		if (!json.empty()) {
			PpDocument doc = DocumentFromJson(json);
			if (DeleteById(doc, selId)) {
				RebuildChart(doc, "");
				ClearOwnSelection();
				RequestOverlayRepaint();
				wchar_t buf[128];
				::swprintf_s(buf, 128, L"hotkey Delete applied to %hs/%hs", selKind.c_str(), selId.c_str());
				OvLog(buf);
			}
		}
	}
	catch (const _com_error&) {
		OvLog(L"COM error during hotkey delete");
	}
	catch (const std::exception&) {
		OvLog(L"document error during hotkey delete");
	}
	catch (...) {
		OvLog(L"unknown error during hotkey delete");
	}
	g_mutating = false;
}

// WM_HOTKEY(Left/Right, +-Shift): nudge the selected task/milestone by
// deltaDays (+-1 for plain Left/Right, +-7 for the Shift variants per the
// task spec), keeping the selection on the same item.
void HandleHotkeyNudge(long deltaDays) {
	if (!g_app || g_mutating) return;
	const std::string selId = g_ownSelId;
	const std::string selKind = g_ownSelKind;
	if (selId.empty() || (selKind != "TASK" && selKind != "MILESTONE")) return;

	g_mutating = true;
	try {
		std::string json = ReadGanttFromSlide(g_app);
		if (!json.empty()) {
			PpDocument doc = DocumentFromJson(json);
			bool changed = NudgeTask(doc, selId, deltaDays); // no-op if selId is not a task
			if (!changed && selKind == "MILESTONE") {
				for (auto& ms : doc.milestones) {
					if (ms.id == selId) {
						ms.date = DaysToDate(DateToDays(ms.date) + deltaDays);
						changed = true;
						break;
					}
				}
			}
			if (changed) {
				RebuildChart(doc, selId);
				SetOwnSelection(selKind, selId);
				RequestOverlayRepaint();
				wchar_t buf[128];
				::swprintf_s(buf, 128, L"hotkey Nudge(%ld) applied to %hs/%hs", deltaDays, selKind.c_str(), selId.c_str());
				OvLog(buf);
			}
		}
	}
	catch (const _com_error&) {
		OvLog(L"COM error during hotkey nudge");
	}
	catch (const std::exception&) {
		OvLog(L"document error during hotkey nudge");
	}
	catch (...) {
		OvLog(L"unknown error during hotkey nudge");
	}
	g_mutating = false;
}

// Unregister every hotkey id that is currently registered (per-key state in
// g_hotkeysRegistered, so this is safe to call even after a partial-failure
// registration). Called on the shouldRegister true->false transition, and
// from HideOverlay/OverlayStop so a hidden/stopped overlay never leaves keys
// stolen system-wide.
void UnregisterAllHotkeys() {
	for (int i = 0; i < kHotkeyCount; ++i) {
		if (g_hotkeysRegistered[i]) {
			::UnregisterHotKey(g_hwnd, kHotkeySpecs[i].id);
			g_hotkeysRegistered[i] = false;
		}
	}
	g_hotkeysActive = false;
}

// Register every hotkey (best-effort per-key: a single key's RegisterHotKey
// failing — e.g. another app already owns that combo — is logged and does
// NOT prevent the other keys from registering, per the task spec's "degrade
// gracefully" requirement). Called on the shouldRegister false->true
// transition.
void RegisterAllHotkeys() {
	if (!g_hwnd) return;
	for (int i = 0; i < kHotkeyCount; ++i) {
		const HotkeySpec& spec = kHotkeySpecs[i];
		BOOL registered = ::RegisterHotKey(g_hwnd, spec.id, spec.mods, spec.vk);
		g_hotkeysRegistered[i] = (registered != FALSE);
		if (!registered) {
			wchar_t buf[96];
			::swprintf_s(buf, 96, L"RegisterHotKey failed for %s (err=%lu)", spec.name, (unsigned long)::GetLastError());
			OvLog(buf);
		}
	}
	g_hotkeysActive = true;
}

// Evaluate shouldRegister = (internal selection is TASK/MILESTONE) AND
// (PowerPoint is the foreground app) AND (not mid-gesture/mutation/inline-
// edit) and register/unregister on a TRANSITION only. Called every Tick().
// PowerPoint-foreground is checked by comparing the foreground window's owning
// process id against our OWN process id (GetCurrentProcessId): the overlay
// DLL runs IN-PROCESS inside POWERPNT.EXE (per the task brief), so "foreground
// window belongs to PowerPoint's process" and "foreground window belongs to
// OUR process" are the same check here — no need to resolve g_pptHwnd's PID
// separately, and this also naturally covers the overlay's OWN hwnd (and the
// inline editor) being foreground, which are legitimately "PowerPoint is
// active" states for this purpose (e.g. right after a click before focus
// returns to the main frame).
void UpdateHotkeyRegistration() {
	if (!g_hwnd) return;

	bool selectionIsTaskOrMilestone = (g_ownSelKind == "TASK" || g_ownSelKind == "MILESTONE") && !g_ownSelId.empty();

	bool foregroundIsOurs = false;
	HWND fg = ::GetForegroundWindow();
	if (fg) {
		DWORD fgPid = 0;
		::GetWindowThreadProcessId(fg, &fgPid);
		foregroundIsOurs = (fgPid == ::GetCurrentProcessId());
	}

	bool notMidGesture = !g_gestureActive && !g_mutating && !IsEditSessionActive();

	bool shouldRegister = selectionIsTaskOrMilestone && foregroundIsOurs && notMidGesture;

	if (shouldRegister && !g_hotkeysActive) {
		RegisterAllHotkeys();
	} else if (!shouldRegister && g_hotkeysActive) {
		UnregisterAllHotkeys();
	}
}

void HandleToolbarButton(int button) {
	if (!g_app || g_mutating) return;
	const std::string selId = g_selId;
	const std::string selKind = g_selKind;
	if (button != BTN_ADD && !IsTaskKind(selKind)) return;
	if (button != BTN_ADD && selId.empty()) return;

	g_mutating = true;
	try {
		std::string json = ReadGanttFromSlide(g_app);
		if (json.empty()) { g_mutating = false; return; }

		PpDocument doc = DocumentFromJson(json);
		std::string selectId;
		bool changed = false;
		if (button == BTN_ADD) {
			std::string rowId = RowForSelection(doc, selKind, selId);
			if (!rowId.empty()) {
				std::string start, end;
				DefaultTaskDates(doc, rowId, IsTaskKind(selKind) ? selId : "", start, end);
				selectId = AddTask(doc, rowId, "New Task", start, end);
				changed = !selectId.empty();
			}
		} else if (button == BTN_DEL) {
			selectId.clear();
			changed = DeleteById(doc, selId);
		} else if (button == BTN_PCT_MINUS || button == BTN_PCT_PLUS) {
			if (const PpTask* task = FindTask(doc, selId)) {
				int delta = (button == BTN_PCT_PLUS) ? 10 : -10;
				int newPct = task->percent + delta;
				if (newPct < 0) newPct = 0;
				if (newPct > 100) newPct = 100;
				if (newPct != task->percent) {
					changed = SetTaskPercent(doc, selId, newPct);
					if (changed) selectId = selId;
				}
			}
		}

		if (changed) {
			RebuildChart(doc, selectId);
			wchar_t buf[128];
			::swprintf_s(buf, 128, L"toolbar button %d applied to %hs/%hs", button, selKind.c_str(), selId.c_str());
			OvLog(buf);
		}
	}
	catch (const _com_error&) {
		OvLog(L"COM error during toolbar edit");
	}
	catch (const std::exception&) {
		OvLog(L"document error during toolbar edit");
	}
	catch (...) {
		OvLog(L"unknown error during toolbar edit");
	}
	g_mutating = false;
}

void HandleHoverInsertRow() {
	if (!g_app || g_mutating || g_hoverRowId.empty()) return;
	const std::string afterRowId = g_hoverRowId;

	g_mutating = true;
	try {
		std::string json = ReadGanttFromSlide(g_app);
		if (json.empty()) { g_mutating = false; return; }

		PpDocument doc = DocumentFromJson(json);
		std::string rowId = AddRow(doc, "New Row", afterRowId);
		if (!rowId.empty()) {
			RebuildChart(doc, "");
			wchar_t buf[128];
			::swprintf_s(buf, 128, L"hover insert row after %hs", afterRowId.c_str());
			OvLog(buf);
		}
	}
	catch (const _com_error&) {
		OvLog(L"COM error during hover row insert");
	}
	catch (const std::exception&) {
		OvLog(L"document error during hover row insert");
	}
	catch (...) {
		OvLog(L"unknown error during hover row insert");
	}
	g_mutating = false;
}

// Execute the operation MapMenuCommand described for a chosen right-click
// menu command, given the HtHit that produced the menu (supplies whichever of
// rowId/taskId the op needs) and, for HtOpKind::AddTaskAtPoint, the client
// point the menu was invoked at (to derive the new task's anchor day, mirroring
// StartCreateGesture's EmptyCell math). Follows the standard commit pattern:
// read PP_DOC, apply the GanttOps mutation, RebuildChart (which itself calls
// StartNewUndoEntryIfPossible before mutating), then synchronously
// SetOwnSelection so the edited/created item is selected without a
// deselect-then-reselect flicker (same reasoning as CommitDragGesture/
// CommitCreateGesture — the hit snapshot is invalidated by RebuildChart, so
// SyncSelectionChromeFromOwnSelection must wait for the next Tick()).
void HandleContextMenuCommand(const HtMenuOp& op, const HtHit& hit, POINT clientPt) {
	if (op.opKind == HtOpKind::None) return;
	if (!g_app || g_mutating) return;

	g_mutating = true;
	try {
		std::string json = ReadGanttFromSlide(g_app);
		if (json.empty()) { g_mutating = false; return; }
		PpDocument doc = DocumentFromJson(json);

		std::string selectId;
		std::string selectKind;
		bool changed = false;

		switch (op.opKind) {
		case HtOpKind::AddTask: {
			// hit.id is the task/milestone id for TaskBody/Edge/Milestone
			// zones; hit.rowId (via hit.id, since RowBand/Label report the
			// row id in HtHit::id too) covers the row-oriented zones.
			std::string rowId = !hit.rowId.empty() ? hit.rowId : RowForSelection(doc, hit.kind == HtItemKind::Milestone ? "MILESTONE" : "TASK", hit.id);
			if (!rowId.empty()) {
				std::string start, end;
				DefaultTaskDates(doc, rowId, hit.id, start, end);
				selectId = AddTask(doc, rowId, "New Task", start, end);
				changed = !selectId.empty();
				selectKind = "TASK";
			}
			break;
		}
		case HtOpKind::Delete: {
			selectId.clear();
			changed = DeleteById(doc, hit.id);
			break;
		}
		case HtOpKind::DeleteRow: {
			selectId.clear();
			changed = DeleteById(doc, hit.rowId);
			break;
		}
		case HtOpKind::Nudge: {
			changed = NudgeTask(doc, hit.id, op.nudgeDays);
			if (!changed) {
				// Milestones have no length; a "nudge" shifts the date directly
				// (mirrors CommitDragGesture's milestone-drag fallback).
				for (auto& ms : doc.milestones) {
					if (ms.id == hit.id) {
						ms.date = DaysToDate(DateToDays(ms.date) + op.nudgeDays);
						changed = true;
						break;
					}
				}
				selectKind = "MILESTONE";
			} else {
				selectKind = "TASK";
			}
			if (changed) selectId = hit.id;
			break;
		}
		case HtOpKind::Percent: {
			if (const PpTask* task = FindTask(doc, hit.id)) {
				int newPct = task->percent + op.percentDelta;
				if (newPct < 0) newPct = 0;
				if (newPct > 100) newPct = 100;
				if (newPct != task->percent) {
					changed = SetTaskPercent(doc, hit.id, newPct);
					if (changed) { selectId = hit.id; selectKind = "TASK"; }
				}
			}
			break;
		}
		case HtOpKind::SetScale: {
			changed = SetScale(doc, op.scale);
			break;
		}
		case HtOpKind::AddRow: {
			// needsRowId => "Add Row Below" (afterRowId = hit.rowId); the
			// background "Add Row" command has needsRowId == false and
			// appends (AddRow's empty-afterRowId behavior).
			std::string afterRowId = op.needsRowId ? hit.rowId : "";
			std::string rowId = AddRow(doc, "New Row", afterRowId);
			changed = !rowId.empty();
			break;
		}
		case HtOpKind::AddTaskAtPoint: {
			// Mirrors StartCreateGesture's EmptyCell anchor-day derivation: no
			// task rect at this point to read a day from directly, so anchor
			// against a reference task already in the (still-valid, since we
			// have not rebuilt yet) hit snapshot. Falls back to a 7-day span.
			float pxPerDay = ComputeEmptyCellPxPerDay();
			if (pxPerDay > 0.0f) {
				bool haveRef = false;
				long refDay = 0;
				long refScreenX = 0;
				for (const auto& item : g_hitSnapshot.items) {
					if (item.kind != HtItemKind::Task) continue;
					const PpTask* task = FindTask(doc, item.id);
					if (!task) continue;
					refDay = DateToDays(task->start);
					refScreenX = item.rect.left;
					haveRef = true;
					break;
				}
				if (haveRef) {
					long screenX = clientPt.x + g_windowOriginX;
					long anchorDay = refDay + (long)::lround((double)(screenX - refScreenX) / (double)pxPerDay);
					std::string startISO = DaysToDate(anchorDay);
					std::string endISO = DaysToDate(anchorDay + 6); // default ~1 week span
					selectId = AddTask(doc, hit.rowId, "New task", startISO, endISO);
					changed = !selectId.empty();
					selectKind = "TASK";
				}
			}
			break;
		}
		case HtOpKind::None:
		default:
			break;
		}

		if (changed) {
			RebuildChart(doc, selectId);
			if (!selectId.empty() && !selectKind.empty()) {
				SetOwnSelection(selectKind, selectId);
			}
			RequestOverlayRepaint();
			wchar_t buf[160];
			::swprintf_s(buf, 160, L"context menu op %d applied (id=%hs)", (int)op.opKind, hit.id.c_str());
			OvLog(buf);
		}
	}
	catch (const _com_error&) {
		OvLog(L"COM error during context menu command");
	}
	catch (const std::exception&) {
		OvLog(L"document error during context menu command");
	}
	catch (...) {
		OvLog(L"unknown error during context menu command");
	}
	g_mutating = false;
}

// Build a real Win32 popup menu from BuildMenuForZone(hit's zone) and show it
// via TrackPopupMenuEx, then execute whatever command the user picked. NOACTIVATE
// windows (WS_EX_NOACTIVATE, like this overlay) do not become the foreground
// window on their own, and Win32 popup menus need SOME window to be foreground
// to behave correctly (dismiss on outside click, keyboard nav, etc.) — this is
// the documented workaround: SetForegroundWindow(g_hwnd) immediately before
// TrackPopupMenuEx, and PostMessage(g_hwnd, WM_NULL, 0, 0) immediately after,
// per the well-known "NOACTIVATE + TrackPopupMenu" idiom (the WM_NULL nudges
// the message queue so the menu's internal modal loop reliably tears down).
// TrackPopupMenu is a genuinely modal call — it pumps its own message loop
// until dismissed, so this whole function only returns after the user has
// picked an item (or cancelled). Skipped entirely under the ops harness (env
// var PP_OVERLAY_NO_MENU set): posting WM_RBUTTONDOWN/UP in that environment
// still exercises hit-testing + selection without blocking on a real modal menu.
void ShowContextMenuForHit(const HtHit& hit, POINT clientPt) {
	if (hit.zone == HtZone::Outside) return;
	bool hasRowId = !hit.rowId.empty();
	std::vector<HtMenuItem> items = BuildMenuForZone(hit.zone, hit.kind, hasRowId);
	if (items.empty()) return;

	// Under the ops/harness smoke check, skip showing the actual modal popup
	// (it cannot be automated in-process) — selection is already set by the
	// caller before this function runs, which is all that check verifies.
	if (::GetEnvironmentVariableW(L"PP_OVERLAY_NO_MENU", NULL, 0) > 0) return;

	HMENU menu = ::CreatePopupMenu();
	if (!menu) return;
	// name -> submenu HMENU, created + attached to the top-level menu (as an
	// MF_POPUP entry) lazily the first time an item references it.
	std::vector<std::pair<std::string, HMENU>> submenus;
	auto submenuFor = [&](const std::string& name) -> HMENU {
		for (auto& p : submenus) if (p.first == name) return p.second;
		HMENU sub = ::CreatePopupMenu();
		submenus.push_back({ name, sub });
		::AppendMenuW(menu, MF_STRING | MF_POPUP, (UINT_PTR)sub, Widen(name).c_str());
		return sub;
	};

	for (const auto& item : items) {
		bool inSubmenu = item.submenu && item.submenu[0] != '\0';
		// The "Change Scale" HEADER entry (HtCmd_None, submenu=="") only
		// exists in BuildMenuForZone's list to carry separatorBefore for the
		// header itself; submenuFor() below appends the actual MF_POPUP
		// header when the first Day/Week/Month leaf is seen, so this entry
		// contributes nothing here beyond the separator (handled next).
		if (item.cmdId == HtCmd_None && !inSubmenu) {
			if (item.separatorBefore) ::AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
			continue;
		}
		if (inSubmenu) {
			HMENU sub = submenuFor(item.submenu);
			::AppendMenuW(sub, MF_STRING, (UINT_PTR)item.cmdId, Widen(item.label).c_str());
			continue;
		}
		if (item.separatorBefore) ::AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
		::AppendMenuW(menu, MF_STRING, (UINT_PTR)item.cmdId, Widen(item.label).c_str());
	}

	POINT screenPt = { clientPt.x + g_windowOriginX, clientPt.y + g_windowOriginY };

	// NOACTIVATE idiom: force foreground before, nudge the queue after.
	::SetForegroundWindow(g_hwnd);
	int cmd = ::TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
		screenPt.x, screenPt.y, g_hwnd, NULL);
	::PostMessageW(g_hwnd, WM_NULL, 0, 0);

	::DestroyMenu(menu); // destroys owned submenus recursively

	if (cmd > 0) {
		HtMenuOp op = MapMenuCommand(hit.zone, cmd, hit.kind, hasRowId);
		try {
			HandleContextMenuCommand(op, hit, clientPt);
		} catch (...) {
			OvLog(L"context menu command execution failed");
		}
	}
}

// Scope the overlay chrome to the host: think-cell-style add-ins only ever
// paint their floating chrome while their host app is the active
// (foreground) window. Without this check the overlay's WS_EX_TOPMOST style
// keeps the selection frame + Add/Del/-/+ toolbar visible ON TOP OF OTHER
// APPS once PowerPoint is minimized or loses focus, which is the bug this
// guards against.
//
// "PowerPoint is active" is true when the foreground window's root is
// PowerPoint's own root window, OR the foreground window is one of OUR
// top-level windows (the overlay itself, the inline editor, or the card
// editor — all legitimate "PowerPoint is active" states, e.g. right after a
// click before focus returns to the main frame), OR the foreground window
// belongs to the POWERPNT.EXE process (covers PowerPoint-owned dialogs and
// flyouts that are themselves separate top-level windows).
//
// PID TRAP: compare against the pid that OWNS ppRoot (via
// GetWindowThreadProcessId(ppRoot, &pid)), NOT GetCurrentProcessId(). In
// production the overlay DLL runs in-process inside POWERPNT.EXE so the two
// pids are identical, but this same Overlay.cpp is also linked into
// ppoverlay.exe for the harness, where PowerPoint is a SEPARATE process —
// GetCurrentProcessId() would always be wrong there.
//
// Also requires ppRoot to be non-iconic (not minimized) and visible.
bool IsHostActiveForOverlayChrome(HWND ppRoot) {
	if (!ppRoot) return false;
	if (::IsIconic(ppRoot)) return false;
	if (!::IsWindowVisible(ppRoot)) return false;

	HWND fg = ::GetForegroundWindow();
	if (!fg) return false;
	HWND fgRoot = ::GetAncestor(fg, GA_ROOT);

	if (fgRoot == ppRoot) return true;
	if (fg == g_hwnd || fg == g_editorHwnd || fg == g_cardHwnd) return true;
	if (fgRoot == g_hwnd || fgRoot == g_editorHwnd || fgRoot == g_cardHwnd) return true;

	DWORD ppPid = 0;
	::GetWindowThreadProcessId(ppRoot, &ppPid);
	DWORD fgPid = 0;
	::GetWindowThreadProcessId(fg, &fgPid);
	if (ppPid != 0 && fgPid == ppPid) return true;

	return false;
}

// Poll the slide; keep the overlay over the chart while selection chrome follows
// the selected PowerPlanner child shape.
void Tick() {
	if (g_mutating) return;
	// Re-entrancy guard: while an outgoing COM call inside Tick blocks, its COM
	// modal wait still dispatches WM_TIMER — without this guard those nested
	// ticks issue new COM calls on the same channel and can starve the outer
	// call's reply forever (observed as a permanent hang in the harness).
	if (g_inTick) return;
	struct TickGuard {
		TickGuard() { g_inTick = true; }
		~TickGuard() { g_inTick = false; }
	} tickGuard;
	if (!g_app) { HideOverlay(); return; }
	try {
		RECT oldChartRect = g_chartScreenRect;
		RECT oldSelRect = g_selScreenRect;
		bool oldHasSelectionChrome = g_hasSelectionChrome;
		std::string oldSelId = g_selId;
		std::string oldSelKind = g_selKind;
		bool leftButtonDown = (::GetKeyState(VK_LBUTTON) & 0x8000) != 0;
		bool mouseStateChanged = leftButtonDown != g_lastLeftButtonDown;
		g_lastLeftButtonDown = leftButtonDown;

		// VK_ESCAPE cancels an in-progress capture gesture (drag or pending
		// click) — it does NOT touch the internal selection itself. General
		// Esc (deselecting a made selection) is a later unit. Latency is
		// bounded by the 150ms tick period (by design — this is tick-polled,
		// not event-driven).
		if (g_captureActive && (::GetAsyncKeyState(VK_ESCAPE) & 0x8000)) {
			g_captureActive = false;
			CancelDragGesture();
			if (g_hwnd && ::GetCapture() == g_hwnd) ::ReleaseCapture();
		} else if (!g_gestureActive && !g_captureActive && !g_ownSelKind.empty() &&
			(::GetAsyncKeyState(VK_ESCAPE) & 0x8000)) {
			// No gesture active but something IS internally selected: Esc
			// clears the internal selection (repainted below via the existing
			// SameSelectionState diff). PowerPoint must be the foreground app —
			// same foreground-ownership check UpdateHotkeyRegistration() uses —
			// so a stray Esc typed into some OTHER foreground app while
			// PowerPoint merely happens to still hold the selection doesn't
			// clear it. Esc is deliberately NOT registered as a hotkey (per the
			// task brief) — it stays on this existing GetAsyncKeyState poll,
			// so latency is bounded by the 150ms tick period like the gesture-
			// cancel case above.
			HWND fg = ::GetForegroundWindow();
			DWORD fgPid = 0;
			if (fg) ::GetWindowThreadProcessId(fg, &fgPid);
			if (fg && fgPid == ::GetCurrentProcessId()) {
				ClearOwnSelection();
			}
		}

		PowerPoint::DocumentWindowPtr win = g_app->GetActiveWindow();
		if (!win) { HideOverlay(); return; }
		g_pptHwnd = (HWND)(INT_PTR)g_app->GetHWND();

		// Host-scoping: the overlay chrome (selection frame + toolbar) may be
		// visible ONLY while PowerPoint is the active app (see
		// IsHostActiveForOverlayChrome's comment above). If PowerPoint is not
		// active (minimized, or some unrelated app is foreground), hide the
		// overlay and both auxiliary editor windows and bail out early, same
		// as the no-chart path below. Latency is bounded by one 150ms tick,
		// which is acceptable per the task spec.
		HWND ppRoot = ::GetAncestor(g_pptHwnd, GA_ROOT);
		if (!IsHostActiveForOverlayChrome(ppRoot)) {
			HideOverlay();
			// The inline editor and card editor each already commit/cancel
			// themselves on their own focus-loss messages (WM_KILLFOCUS for
			// the inline editor, WM_ACTIVATE/WA_INACTIVE for the card), so by
			// the time this runs they are usually already closed and
			// possibly NULL. Only hide-if-still-visible here — never force a
			// second commit/cancel (that would double-fire the editors' own
			// teardown logic).
			if (g_editorHwnd && ::IsWindow(g_editorHwnd)) ::ShowWindow(g_editorHwnd, SW_HIDE);
			if (g_cardHwnd && ::IsWindow(g_cardHwnd)) ::ShowWindow(g_cardHwnd, SW_HIDE);
			return;
		}

		PowerPoint::_SlidePtr slide = win->GetView()->GetSlide();
		PowerPoint::ShapePtr chart = FindChartRoot(slide);
		if (!chart) {
			HideOverlay();
			return;
		}

		float chartLeft = chart->GetLeft(), chartTop = chart->GetTop();
		float chartWidth = chart->GetWidth(), chartHeight = chart->GetHeight();
		g_chartScreenRect = {
			win->PointsToScreenPixelsX(chartLeft),
			win->PointsToScreenPixelsY(chartTop),
			win->PointsToScreenPixelsX(chartLeft + chartWidth),
			win->PointsToScreenPixelsY(chartTop + chartHeight)
		};
		NormalizeRect(g_chartScreenRect);
		g_chartProj = Narrow((const wchar_t*)chart->GetTags()->Item(_bstr_t(L"PP_PROJ")));
		bool chartChanged = !SameRect(oldChartRect, g_chartScreenRect);
		BuildRowBands(chart, win);
		bool hoverChanged = UpdateHoverFromCursor();

		ClearSelectionState();
		bool chartRootNativelySelected = false;
		PowerPoint::SelectionPtr sel = win->GetSelection();
		if (sel && sel->GetType() == PowerPoint::ppSelectionShapes) {
			PowerPoint::ShapeRangePtr sr = sel->GetShapeRange();
			if (sr && sr->GetCount() >= 1) {
				PowerPoint::ShapePtr sh = sr->Item(_variant_t(1L));
				_bstr_t kind = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
				if (kind.length()) {
					std::string kindStr = Narrow((const wchar_t*)kind);
					// The CHART_ROOT group itself must stay natively selectable
					// (move grip, Alt+click escape hatch) — only chart CHILDREN
					// get their PowerPoint-native selection suppressed. Its
					// chrome is still driven directly from PowerPoint's
					// Selection (old path), independent of the internal model.
					bool isEditingThisShape = IsEditSessionActive();
					if (kindStr != "CHART_ROOT" && !g_mutating && !isEditingThisShape) {
						_bstr_t id = sh->GetTags()->Item(_bstr_t(L"PP_ID"));
						g_suppressedKind = kindStr;
						g_suppressedId = Narrow((const wchar_t*)id);
						try {
							win->GetSelection()->Unselect();
						} catch (const _com_error&) {
							OvLog(L"COM error unselecting suppressed chart child");
						}
						OvLog((L"suppressed native selection of chart child PP_KIND=" + Widen(kindStr)).c_str());
						// Mirror the suppressed native pick into the internal
						// selection model, then clear it (one-shot).
						std::string mirrorKind = IsTaskKind(g_suppressedKind) ? "TASK" : g_suppressedKind;
						if (mirrorKind == "TASK" || mirrorKind == "MILESTONE") {
							SetOwnSelection(mirrorKind, g_suppressedId);
						}
						g_suppressedKind.clear();
						g_suppressedId.clear();
					} else if (kindStr == "CHART_ROOT") {
						chartRootNativelySelected = true;
						g_selKind = kindStr;
						_bstr_t id = sh->GetTags()->Item(_bstr_t(L"PP_ID"));
						g_selId = Narrow((const wchar_t*)id);
						float left = sh->GetLeft(), top = sh->GetTop(), w = sh->GetWidth(), h = sh->GetHeight();
						g_selScreenRect = {
							win->PointsToScreenPixelsX(left),
							win->PointsToScreenPixelsY(top),
							win->PointsToScreenPixelsX(left + w),
							win->PointsToScreenPixelsY(top + h)
						};
						NormalizeRect(g_selScreenRect);
						g_hasSelectionChrome = true;
					}
				}
			}
		}

		// Chrome for a chart CHILD (task/milestone/row) no longer requires
		// PowerPoint selection — it is driven entirely from the internal
		// selection model, kept in sync with the freshly-rebuilt snapshot /
		// row bands above. Skipped when CHART_ROOT chrome (old path) already
		// claimed g_selKind/g_hasSelectionChrome this tick, OR while a drag
		// gesture is active (A1): the gesture owns the ghost/chrome for its
		// duration and re-deriving chrome mid-drag from a snapshot that is
		// about to be invalidated by the eventual commit's RebuildChart is
		// unnecessary churn — the commit path re-syncs explicitly (A2).
		if (!chartRootNativelySelected && !g_gestureActive) {
			SyncSelectionChromeFromOwnSelection();
		}

		// Re-evaluate hotkey registration every tick (transition-only —
		// UpdateHotkeyRegistration itself no-ops unless shouldRegister flipped)
		// AFTER the internal selection has been fully resolved for this tick
		// (suppression mirror + SyncSelectionChromeFromOwnSelection above), so
		// it sees the selection state a user actually observes on screen.
		UpdateHotkeyRegistration();

		// NOTE: the old auto-reflow-on-mouse-up-idle-tick block that lived
		// here is gone (A1). It predated the own-selection/drag-gesture model:
		// chart clicks never reach PowerPoint anymore (the overlay captures
		// them), so there is no native shape drag left for ReflowFromSlide to
		// pick up on an idle tick — the WM_LBUTTONUP drag-commit path (see
		// CommitDragGesture) is the only way task/milestone dates change now.
		// ReflowFromSlide itself is untouched and still used by the harness.

		ShowOverlayForChartRect(g_chartScreenRect);
		if (chartChanged || hoverChanged || mouseStateChanged || !SameSelectionState(oldHasSelectionChrome, oldSelRect, oldSelId, oldSelKind)) {
			RequestOverlayRepaint();
		}

		std::string k = g_selKind;
		if (k != g_lastKind) {
			g_lastKind = k;
			wchar_t buf[160];
			::swprintf_s(buf, 160, L"shown for chart (%ld,%ld)-(%ld,%ld), selection PP_KIND=%hs",
				g_chartScreenRect.left, g_chartScreenRect.top, g_chartScreenRect.right, g_chartScreenRect.bottom, k.c_str());
			OvLog(buf);
		}
	} catch (const _com_error&) {
		HideOverlay();
	} catch (const std::exception&) {
		HideOverlay();
	} catch (...) {
		HideOverlay();
	}
}

void CALLBACK TimerProc(HWND, UINT, UINT_PTR, DWORD) { Tick(); }

} // namespace

void OverlayStart(IDispatch* app) {
	g_inst = (HINSTANCE)::GetModuleHandleW(NULL);
	g_app = app;  // QI to _Application
	// GDI+ is started here (and shut down in OverlayStop) — never in DllMain.
	if (!g_gdiplusToken) {
		Gdiplus::GdiplusStartupInput gsi;
		if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gsi, NULL) != Gdiplus::Ok) {
			g_gdiplusToken = 0;
			OvLog(L"GdiplusStartup failed");
		}
	}
	EnsureWindow();
	g_timer = ::SetTimer(NULL, 0, 150, TimerProc);
	OvLog(L"started");
}

void OverlayStop() {
	if (g_timer) { ::KillTimer(NULL, g_timer); g_timer = 0; }
	DestroyInlineEditor();
	CloseCardEditor();
	if (g_captureActive) {
		g_captureActive = false;
		if (g_hwnd && ::GetCapture() == g_hwnd) ::ReleaseCapture();
	}
	ResetDragGestureState();
	UnregisterAllHotkeys(); // MUST run before DestroyWindow below (needs g_hwnd)
	if (g_hwnd) { ::DestroyWindow(g_hwnd); g_hwnd = NULL; }
	FreeBackBuffer();
	if (g_gdiplusToken) {
		Gdiplus::GdiplusShutdown(g_gdiplusToken);
		g_gdiplusToken = 0;
	}
	g_app = nullptr;
	g_pptHwnd = NULL;
	g_shown = false;
	g_lastKind.clear();
	ClearSelectionState();
	ClearOwnSelection();
	ClearHoverState();
	g_buttonsValid = false;
	g_rowBands.clear();
	g_editRegions.clear();
	InvalidateHitSnapshot();
	g_lastHit = HtHit{};
	g_gripValid = false;
	::SetRectEmpty(&g_gripRect);
	::SetRectEmpty(&g_frameRect);
	::SetRectEmpty(&g_chartScreenRect);
	// Reset DPI state to the 96-DPI (100%) baseline so a fresh OverlayStart()
	// (e.g. the next harness run in the same process) re-probes rather than
	// carrying over a stale scale factor.
	g_dpi = 96;
	UpdateDpiScaledMetrics();
}

HWND OverlayHwnd() { return g_hwnd; }

const char* Overlay_GetSelectedIdForTest() { return g_ownSelId.c_str(); }
