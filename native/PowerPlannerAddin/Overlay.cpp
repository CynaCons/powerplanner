#include "pch.h"
#include "Overlay.h"
#include "GanttBuilder.h"
#include "GanttJson.h"
#include "GanttOps.h"
#include "GanttHitTest.h"
#include "EntityDump.h"
#include "GanttAxisLayout.h"
#include "GanttAppBar.h"
#include "GanttCommandRegistry.h"
#include "GanttTheme.h"
#include "ThemeMenu.h"
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
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "OverlayFormat.h"
#include "OverlayGeometry.h"
#include "OverlayMetrics.h"

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
// Mirrors GanttScene.h's AXIS_H without pulling that Win32-bound scene header
// into the overlay; both source the canonical theme axis-height token.
constexpr float kAxisHeaderHeightPt = gt::axis_height;
// ---- DPI-scaled chrome metrics ---------------------------------------------
// All chrome pixel constants below are expressed at the 96-DPI (100% Windows
// scaling) baseline and scaled per-tick through HtScalePx (MulDiv(base,dpi,96)
// — see GanttHitTest.h/.cpp, which is COM-free so it's shared with the ops
// harness). The overlay runs IN-PROCESS in POWERPNT.EXE, a per-monitor-DPI-
// aware process, so COM coordinates (PointsToScreenPixels, GetCursorPos)
// already come back in PowerPoint's DPI context and need no scaling — only
// OUR chrome's hardcoded pixel metrics do, or they stay 96-DPI-sized (a few mm
// on screen) and become unusably small at 150-200%. kBase* constants live in
// OverlayMetrics.h; scaled runtime values are below.
const int BUTTON_COUNT = 4;                 // not a pixel metric: unscaled

// Current DPI-scaled values, recomputed by UpdateDpiScaledMetrics() whenever
// the overlay's window DPI changes (or on first use). Default to the 96-DPI
// (100%) baseline so any code path that runs before the first DPI probe still
// behaves exactly as before this unit.
int g_dpi = 96;
int INFL = kBaseInfl;
int BADGE_H = kBaseBadgeH;
int TOOLBAR_H = kBaseToolbarH;
int ROW_INSERT_BUTTON = kBaseRowInsertButton;
int LINK_PORT_RADIUS = kBaseLinkPortRadius;
int LINK_PORT_GAP = kBaseLinkPortGap;
int g_buttonW = kBaseButtonW;
int g_buttonH = kBaseButtonH;
int g_buttonGap = kBaseButtonGap;
int GRIP_SIZE = kBaseGripSize;
int kDragThresholdPx = kBaseDragThresholdPx;
int g_tooltipPad = kBaseTooltipPad;
Gdiplus::REAL g_tooltipFontPx = kBaseTooltipFontPx;
Gdiplus::REAL g_badgeFontPx = kBaseBadgeFontPx;
Gdiplus::REAL g_buttonFontPx = kBaseButtonFontPx;

// ---- input-neutral harness test seams --------------------------------------
// Off (g_cursorOverrideEnabled == false) in production: every GetCursorPos
// call and the WM_NCHITTEST Alt-passthrough check behave exactly as before.
// The overlay-test.cpp harness enables this once at startup and updates
// g_cursorOverrideScreenPt/g_cursorOverrideAltDown in the same place it used
// to call the real SetCursorPos/keybd_event, so posted WM_MOUSE*/WM_HOTKEY
// messages fully define a gesture without touching the user's real mouse or
// keyboard. See Overlay_SetCursorPosOverrideForTest in Overlay.h.
bool  g_cursorOverrideEnabled = false;
POINT g_cursorOverrideScreenPt = {};
bool  g_cursorOverrideAltDown = false;

// Off (-1) in production: IsHostActiveForOverlayChrome, the hotkey
// registration's foreground-scoping check, and the Esc-clear fgPid check all
// fall back to the real GetForegroundWindow-based logic. 0/1 let the harness
// declare "host inactive"/"host active" without a real SetForegroundWindow
// call. See Overlay_SetHostActiveOverrideForTest in Overlay.h.
int g_hostActiveOverrideMode = -1;

// B1 (v2.6.1): keyboard-focus scope for hotkey REGISTRATION. -1 (production) =
// use the real GetGUIThreadInfo/GetFocus class-name check
// (IsSlideViewFocusedForHotkeys); 0 = force "focus NOT in slide view"; 1 =
// force "focus in slide view". The hotkey-scope harness flips this to 0 to
// prove the registration gate drops when focus leaves the slide view (Notes /
// outline / ribbon edit control) WITHOUT sending real keystrokes. Default -1
// keeps poll-only harnesses that force host-active behaving as before (see the
// host-active shortcut in IsSlideViewFocusedForHotkeys).
int g_slideFocusOverrideMode = -1;

// COM-free mirror (consumed by the component-shape-protection harness's dump)
// of the PowerPoint-native selection PP_KIND that Tick() / the COM sink most
// recently OBSERVED for the FIRST selected shape: "" (nothing selected, or a
// foreign non-chart shape), "CHART_ROOT", or a chart-child kind (e.g. "TASK").
// The dump surfaces it as "nativeSelKind" so no_child_shape_selected can assert
// that, once settled, no chart CHILD remains PowerPoint's real selection (only
// "" or CHART_ROOT are allowed). Written from the COM side; read COM-free.
std::string g_lastNativeSelKindForTest;

// Single choke point for "where is the physical cursor": everything in this
// file that used to call ::GetCursorPos directly now calls this instead, so
// the harness's cursor override (once enabled) is consulted EVERYWHERE, not
// just at whichever call site a future edit happens to touch.
bool OverlayGetCursorPos(POINT* pt) {
	if (g_cursorOverrideEnabled) {
		*pt = g_cursorOverrideScreenPt;
		return true;
	}
	return ::GetCursorPos(pt) != 0;
}

PowerPoint::_ApplicationPtr g_app;
HWND     g_hwnd = NULL;
HINSTANCE g_inst = NULL;
UINT_PTR g_timer = 0;
bool     g_shown = false;
bool     g_mutating = false;
bool     g_inTick = false;
static bool g_lastHostActive = true;
static bool g_lastViewOk = true;
static long g_overlayPaintCount = 0, g_appBarPaintCount = 0;
static long g_overlaySwpCount = 0, g_appBarSwpCount = 0;
// Phase 13 v2.8.1: continuous paint cadence sample (SR-SMO-09..12).
// Ring of paint timestamps (GetTickCount64 ms) while sampling is active.
static constexpr int kPaintTsCap = 512;
static ULONGLONG g_paintTsMs[kPaintTsCap];
static int g_paintTsCount = 0;
static int g_paintTsWrite = 0;
static bool g_paintSampleActive = false;
static ULONGLONG g_paintSampleStartMs = 0;
ULONG_PTR g_gdiplusToken = 0;

// ---- bottom app bar (second layered chrome window) -------------------------
HWND g_appBarHwnd = NULL;
const wchar_t* const kAppBarClass = L"PowerPlannerAppBar";
HDC g_abDc = NULL; HBITMAP g_abBmp = NULL; HGDIOBJ g_abOld = NULL;
void* g_abBits = nullptr; int g_abW = 0, g_abH = 0;
bool g_appBarShown = false;
RECT g_appBarLastRect = { 0, 0, 0, 0 };
bool g_appBarGeomValid = false;   // false => force a reposition+repaint
bool g_appBarModelDirty = true;   // true  => model changed, force repaint
AppBarModel g_appBar;
bool g_appBarValid = false;
std::string g_appBarSelKindBuilt, g_appBarSelIdBuilt;
bool g_appBarIsMultiBuilt = false;
std::string g_appBarDocSig;
std::string g_lastScale = "week";
int g_appBarHoverCmd = 0;
struct AppBarHitRect { int cmd; RECT rc; bool enabled; };
std::vector<AppBarHitRect> g_appBarHits;
int g_appBarContentW = 0, g_appBarContentH = 0;
int g_appBarLastMeasuredContentW = 0, g_appBarLastMeasuredContentH = 0;
AppBarModel g_appBarLayout;
std::vector<AppBarItem> g_appBarInsertPopupItems;

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
std::string g_chartRowY;      // current CHART_ROOT PP_ROWY payload
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
struct OwnSelEntry {
	std::string kind;
	std::string id;
};
std::vector<OwnSelEntry> g_ownSelExtra;

// ---- S5 link mode (B7.1) ----------------------------------------------------
// Active while the user is picking a dependency target after pressing Link on
// the app bar. g_linkFromId is the selected task/milestone id at entry.
bool g_linkMode = false;
std::string g_linkFromId;
std::string g_linkFromKind;
bool g_linkDragFromRight = false;
std::string g_linkDragHoverId;
std::string g_linkDragHoverKind;

// Left-button gesture tracking (down -> up), used to distinguish a click
// (select) from a drag and to own mouse capture for the duration of the
// gesture.
bool g_captureActive = false;
POINT g_mouseDownPt = {};
WPARAM g_mouseDownMk = 0;
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
enum class DragKind { None, TaskBody, TaskEdgeL, TaskEdgeR, TaskProgress, Milestone, Create, Marker, Text, LinkPort, WindowEdgeL, WindowEdgeR };
DragKind g_dragKind = DragKind::None;
std::string g_dragId;          // task, milestone, marker, or text PP_ID being dragged
RECT g_dragAnchorRect = {};    // original screen rect of the dragged item at gesture start
std::string g_dragOrigStart;   // original ISO start date (task) or date (milestone)
std::string g_dragOrigEnd;     // original ISO end date (task); empty for milestones
float g_dragPxPerDay = 0.0f;   // SCREEN pixels-per-day for this gesture (see ComputeDragPxPerDay)
long g_dragDeltaDays = 0;      // current candidate day delta (updated on move)
POINT g_dragLastPt = {};       // last client point seen during the drag
int g_dragOrigPercent = 0;     // task percent at gesture start (TaskProgress only)
int g_dragCandidatePercent = 0; // live percent preview (TaskProgress only)
std::string g_dragPillText;    // last date/% pill text (harness dump, SR-IXC-03/06)
RECT g_dragPreviewRect = {};   // screen rect of create/task ghost (harness dump)

// W2 time-window gesture state. This is deliberately preview-only: the
// candidate baseline and axis document are snapshotted on port-down and never
// mutate PP_DOC or PowerPoint shapes until W3 owns the commit path.
PpDocument g_windowPreviewDoc;
std::string g_windowCandidateStart;
std::string g_windowCandidateEnd;

// ---- text-move gesture state -------------------------------------------------
// A Text drag moves the PpText by a screen-pixel offset translated back into
// slide POINTS (unlike every other drag kind, which works in whole days) —
// text has no day-span of its own to derive a px/day scale from, and its
// (dx, dy) offset is stored in points, not days. g_dragPxPerPt is the
// SCREEN-pixels-per-POINT scale for this gesture, derived from the SAME
// px/day scale every other gesture already uses (g_dragPxPerDay) divided by
// PP_PROJ's ptPerDay (points per day): since PowerPoint's zoom is a single
// uniform factor (never a separate horizontal/vertical scale), one px/pt
// ratio is valid for BOTH dx and dy — no separate vertical (row-height-based)
// scale is needed. g_dragTextAnchored mirrors the PpText's anchorId-empty-ness
// at gesture start (read once, per A4) so UpdateDragGesture/CommitDragGesture
// never need to re-read the doc to know which commit path applies.
bool g_dragTextAnchored = false;
float g_dragOrigDx = 0.0f;     // text's dx/dy (points) at gesture start
float g_dragOrigDy = 0.0f;
float g_dragPxPerPt = 0.0f;    // SCREEN pixels-per-POINT for this gesture
float g_dragCandidateDx = 0.0f; // current candidate dx/dy (points), updated on move
float g_dragCandidateDy = 0.0f;

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
std::string g_lastCommittedDragTargetRowId; // last TaskBody/Text drop row (harness dump)

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

// SR-SMO-06: optimistic drag-commit echo — paint dropped geometry until the
// next tick repopulates the hit snapshot after RebuildChart.
struct DragCommitEcho {
	bool active = false;
	DragKind kind = DragKind::None;
	RECT anchorScreen = {};
	long deltaDays = 0;
	float pxPerDay = 0.0f;
	RECT targetRowScreen = {};
	bool textAnchored = false;
	float origDx = 0.0f;
	float origDy = 0.0f;
	float candidateDx = 0.0f;
	float candidateDy = 0.0f;
	float pxPerPt = 0.0f;
	// m7 (SR-WIN-25): under an explicit window the echo must never paint
	// pixels outside the window (the committed bar will re-render CLIPPED, or
	// not at all). Screen-px horizontal clip range, valid only when set.
	bool windowClipValid = false;
	long windowClipLeftPx = 0;
	long windowClipRightPx = 0;
};
DragCommitEcho g_dragCommitEcho;

std::wstring g_badge = L"PowerPlanner \x2014 click a bar to edit";
RECT g_buttonRects[BUTTON_COUNT] = {};
RECT g_frameRect = {};
RECT g_chartScreenRect = {};
// M1: there is no HEADER_BAND in the semantic child snapshot. BuildRowBands
// derives this screen rect from the first PP_ROWY lane top minus scaled AXIS_H.
RECT g_headerBandScreenRect = {};
bool g_windowHeaderHover = false;
// Which window port (if any) the pointer is over. Drives the emphasized paint
// state and the "click or drag to widen" hint; NOT a visibility gate.
HtZone g_windowPortHoverZone = HtZone::Outside;
static float g_chartLeftPt = 0.0f;
static float g_chartWidthPt = 0.0f;
RECT g_selScreenRect = {};
// Last item-sized selection chrome rect (TASK/MILESTONE/MARKER/TEXT). Used
// when the hit snapshot is empty post-RebuildChart so we never fall back to a
// stale CHART_ROOT-sized g_selScreenRect until the next Tick() repopulates.
RECT g_lastKnownItemSelRect = {};
int g_windowOriginX = 0;
int g_windowOriginY = 0;
bool g_buttonsValid = false;
bool g_hasSelectionChrome = false;

struct RowBand {
	std::string rowId;
	RECT screenRect;
	int screenLeftGutter;
	RECT screenRailRect;
};
const RowBand* RowBandAtScreenY(long screenY);

std::vector<RowBand> g_rowBands;

// Semantic hit-test snapshot (pure, COM-free — see GanttHitTest.h). Rebuilt by
// BuildRowBands' child walk; reused across ticks while the chart screen rect,
// child count, and PP_ROWY/PP_PROJ payloads are unchanged so the per-tick COM
// cost stays bounded.
HtSnapshot g_hitSnapshot;
RECT g_hitCacheChartRect = {};
long g_hitCacheChildCount = -1;
std::string g_hitCacheChartRowY;
std::string g_hitCacheChartProj;
// R1a entity dump (SR-ENT-05/08): rebuilt with the hit snapshot in BuildRowBands.
// Geometry/kind/id are cached; flags are filled at dump time from live globals.
// Scene signature reuses hit-cache key inputs for future snapshot dedupe (SR-ENT-06).
std::vector<PpEntity> g_entityCache;
struct RecTaskEntityBinding {
	size_t entityIndex = 0;
	PowerPoint::ShapePtr shape;
};
std::vector<RecTaskEntityBinding> g_recTaskEntityBindings;
ULONGLONG g_recLastTaskGeometryRefreshMs = 0;
std::string g_entityDumpJson = "{\"entities\":[]}";
std::string g_entitySceneSig;

// ---- session recorder (R1b-core + R1b-taps / SR-REC-01..14) -----------------
// Off by default (SR-REC-01): every hook checks g_recActive first. Structured
// sink is SEPARATE from powerplanner-addin.log (never write events there).
// Toggle UI is R1c; this slice adds input/gesture/op/paint/frame taps.
bool g_recActive = false;
OverlaySessionRecordStateChanged g_recStateChanged = nullptr;
void* g_recStateChangedContext = nullptr;
wchar_t g_recSessionDir[MAX_PATH] = {};
FILE* g_recEventsFile = nullptr;
char g_recEventsBuffer[64 * 1024] = {};
unsigned long long g_recSeq = 0;
ULONGLONG g_recT0 = 0;
ULONGLONG g_recLastFlushMs = 0;
std::string g_recLastSnapEntitySig; // entity-body dedupe key (SR-ENT-06)
std::string g_recLastNonEmptyEntityJson; // survives transient post-rebuild cache invalidation
std::string g_recLastNativeKey;     // nativeSel transition dedupe (Tick is 150ms)
// ChildShapeRange detail (Connect/Tick → nativeSel + entity flags, SR-REC-06).
bool g_recNativeHasChild = false;
std::string g_recNativeChildKind;
std::string g_recNativeChildId;
// Throttle / dedupe state for R1b taps (zero cost when !g_recActive).
ULONGLONG g_recLastInputMoveMs = 0;
std::string g_recLastInputHitKey; // zone|kind|id of last emitted move
ULONGLONG g_recLastGestureUpdateMs = 0;
ULONGLONG g_recLastPaintOverlayMs = 0;
ULONGLONG g_recLastPaintAppBarMs = 0;
ULONGLONG g_recLastFrameMs = 0;
ULONGLONG g_recLastIdleSnapshotMs = 0;
unsigned long long g_recFrameSeq = 0;
std::string g_recPendingFrameTrigger;
ULONGLONG g_recPendingFrameDueMs = 0;
wchar_t g_recActiveMarkerPath[MAX_PATH] = {};
// Monotonic gesture-instance id (SR-REC gesture lifecycle). Start allocates a
// new "g"; update/commit/cancel reuse it. Matching on kind/id alone is wrong:
// Create start id="" vs commit id=newId, WindowEdgeL start vs "WindowEdge" commit.
long g_recGestureSeq = 0;
long g_recCurGestureId = 0;
// LBUTTONUP sets this around ReleaseCapture so CAPTURECHANGED's CancelDragGesture
// does not emit a spurious "cancel" when a commit (or explicit cancel) follows.
bool g_recSkipGestureCancel = false;
bool g_recIndicatorPainted = false;

// Last semantic hit from a mouse-down on the overlay. The selection-model unit
// will consume this; for now it is only stored (and logged on change).
HtHit g_lastHit;

// 'Move chart' grip in the chrome (top-right): clicking it selects the
// CHART_ROOT group via COM so the user can still move/delete the whole chart
// natively even though the overlay now captures every chart click.
RECT g_gripRect = {};
bool g_gripValid = false;
// M6 (v2.6.1): true while the cursor hovers the move grip, so PaintOverlay can
// surface a "Move chart" tooltip (SR-IXC-15 — the move affordance must be
// discoverable without relying on the Alt+click escape hatch alone).
bool g_gripHover = false;
// GRIP_SIZE (DPI-scaled) is declared with the other chrome metrics above.

std::string g_hoverRowId;
RECT g_hoverBandRect = {};
// Task-level hover (v2.10.x): the task bar unit currently under the cursor and
// its SCREEN rect, so PaintOverlay can outline exactly the bar the pointer is
// over. Row hover (above) stays coarse — it washes the whole band — whereas
// this names ONE task. Set only from UpdateHoverFromCursor, only while no drag
// gesture is in flight, and cleared by ClearHoverState (which the hide path
// calls), so a hidden overlay never keeps a stale highlight.
// TASK_LABEL / TASK_PROGRESS / TASK_PCT hits resolve to HtZone::TaskBody with
// the task's own id (SR-TASK-UNIT-01), so hovering a label highlights its bar.
std::string g_hoverTaskId;
RECT g_hoverTaskRect = {};
RECT g_hoverInsertRect = {};
bool g_hoverInsertValid = false;
RECT g_rowBoundaryInsertRects[2] = {};
bool g_rowBoundaryInsertValid[2] = { false, false };
bool g_lastLeftButtonDown = false;

// Empty-cell hover discovery hint (SR-CRE-05) + creation-failure reason (SR-CRE-02).
bool g_emptyCellHoverActive = false;
DWORD g_emptyCellHoverSinceTick = 0;
bool g_emptyCellHintShownThisSession = false;
std::wstring g_creationFailHint;

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
	CARD_ID_DELETE = 106, // TEXT mode only: label field + this button, no dates/percent/swatches
	CARD_ID_SWATCH_BASE = 110, // 110..117 for the 8 color swatches
};
constexpr int kCardSwatchCount = 8;

// Cards deliberately share the app bar's canonical palette.  Keeping the
// values in kAppBarSwatches (rather than a second card-only RGB palette) means
// the two ways of changing a task colour always agree visually and in the
// serialized #RRGGBB value.

HWND g_cardHwnd = NULL;         // top-level card window (WS_EX_TOOLWINDOW)
HWND g_cardLabelHwnd = NULL;
HWND g_cardStartHwnd = NULL;
HWND g_cardEndHwnd = NULL;
HWND g_cardPercentHwnd = NULL;
HWND g_cardOkHwnd = NULL;
HWND g_cardDateErrorHwnd = NULL; // inline validation message (N1)
HWND g_cardDeleteHwnd = NULL;   // TEXT mode only: label field + this button
HWND g_cardSwatchHwnd[kCardSwatchCount] = {};
WNDPROC g_oldCardFieldProc = NULL; // shared subclass proc for the 4 EDIT children
bool g_cardClosing = false;
std::string g_cardKind;   // "TASK" | "MILESTONE" | "TEXT"
std::string g_cardId;
int g_cardSelectedSwatch = -1; // index into kAppBarSwatches, or -1 = no change
std::string g_cardOrigColor;   // color value read at open time (for "no change" baseline)
bool g_cardInvalid = false;    // true while the red-border/invalid state is shown
HFONT g_cardUiFont = NULL;
HFONT g_editorUiFont = NULL;
HWND g_cardFocusHwnd = NULL;

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
void HandleHoverQuickAddTask();
void RebuildChart(PpDocument& doc, const std::string& selectId);
void OvLog(const wchar_t* msg);
void RecEvent(const char* type, const std::string& payloadBody);
void RecError(const char* where, long hr, const char* msg);
void RecEmitSnapshot();
void RecEmitDoc();
void RecEmitInput(const char* surface, UINT msg, HWND hwnd, WPARAM wp, LPARAM lp);
void RecEmitGestureStart(const char* kind, const std::string& id, const std::string& rowId, POINT anchorClient);
void RecEmitGestureUpdate();
void RecEmitGestureCommit(const char* kind, const std::string& id, const char* result, long hr,
	const std::string& payloadExtra = {});
void RecEmitGestureCancel(const char* kind, const std::string& id);
void RecEmitPaint(const char* surface, long count);
void RecCaptureFrames(const char* trigger);
void RecCapturePendingFrame();
void SessionRecordStart();
void SessionRecordStop();
static const char* RecDragKindName(DragKind k);
static const char* RecHtZoneName(HtZone z);
static const char* RecHtItemKindName(HtItemKind k);
static std::string RecModsString(WPARAM mk = 0);
static std::string RecAppBarCmdLabel(int cmd);
void StartNewUndoEntryIfPossible();
void OpenInlineEditor(const EditRegion& region);
void CommitInlineEdit();
void CancelInlineEdit();
const PpMilestone* FindMilestone(const PpDocument& doc, const std::string& id);
const PpText* FindTextById(const PpDocument& doc, const std::string& id);
void OpenCardEditor(const std::string& kind, const std::string& id, const RECT& anchorScreenRect);
void CommitCardEdit();
void CommitCardDelete();
void CancelCardEdit();
void CloseCardEditor();
bool IsEditSessionActive();
void CancelDragGesture();
static bool DeleteOwnSelectionsInDocument();
void ComputeDragCandidateDates(DragKind kind, const std::string& origStart, const std::string& origEnd,
	long deltaDays, std::string& outStart, std::string& outEnd);
void CommitDragGesture(DragKind kind, const std::string& id, long deltaDays, const std::string& targetRowId,
	float candidateDx = 0.0f, float candidateDy = 0.0f);
void CommitCreateGesture(const std::string& rowId, long startDay, long endDay);
float ComputeEmptyCellPxPerDay();
void ShowContextMenuForHit(const HtHit& hit, POINT clientPt);
void HandleContextMenuCommand(const HtMenuOp& op, const HtHit& hit, POINT clientPt);
LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK InlineEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK CardWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK CardFieldProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
void UpdateHotkeyRegistration();
void UnregisterAllHotkeys(const wchar_t* caller = L"?");
bool IsSlideViewFocusedForHotkeys();
void HandleHotkeyDelete();
void HandleHotkeyNudge(long deltaDays);
void ClearLinkMode();
void CommitLinkTarget(const std::string& targetId);
HtHit HitTestClientPoint(POINT pt);
void CommitLinkPortDrop(const std::string& fromIdIn, const std::string& fromKindIn, const std::string& targetId);
static void StartLinkPortDrag(const std::string& id, const std::string& kind, bool fromRight, POINT downPt);
static void StartWindowEdgeDrag(HtZone zone, POINT downPt);
static void CommitWindowGesture(const std::string& startISO, const std::string& endISO,
	const std::string& baselineStartISO, const std::string& baselineEndISO);
void HandleHoverQuickAddRow(bool insertAbove);

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

const PpMarker* FindMarker(const PpDocument& doc, const std::string& id) {
	for (const auto& mk : doc.markers) {
		if (mk.id == id) return &mk;
	}
	return nullptr;
}

// originDay is the ISO day number rendered AT originXpx. Per the frozen
// PP_PROJ semantics {minDay,pad,ptPerDay,originX} (see GanttBuilder.cpp's
// emission/ReflowFromSlide pair: bar left = originX + (day - (minDay - pad))
// * ptPerDay), the day at originX is minDay - pad, NOT minDay — the window
// starts pad days before the earliest dated item.
struct ProjPx { bool ok; double pxPerDay; double originXpx; long originDay; };
static ProjPx ProjectionPx();
static long AnchorDayFromScreenX(long screenX);
static long DayAtVisibleCenter();

std::string DefaultMarkerDateAtVisibleCenter() {
	const long centerScreenX = (g_chartScreenRect.left + g_chartScreenRect.right) / 2;
	float pxPerDay = ComputeEmptyCellPxPerDay();
	if (pxPerDay > 0.0f && g_app) {
		try {
			std::string json = ReadGanttFromSlide(g_app);
			if (!json.empty()) {
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
				if (haveRef) {
					const long centerDay = refDay + (long)::lround((double)(centerScreenX - refScreenX) / (double)pxPerDay);
					return DaysToDate(centerDay);
				}
			}
		}
		catch (...) {}
	}
	ProjPx proj = ProjectionPx();
	if (proj.ok) {
		const long centerDay = proj.originDay + (long)::lround((centerScreenX - proj.originXpx) / proj.pxPerDay);
		return DaysToDate(centerDay);
	}
	return DaysToDate(0);
}

// Row + date for a free note at the visible chart center (background Note).
void DefaultFreeNoteCellAtVisibleCenter(std::string& outRowId, std::string& outDateISO) {
	outRowId.clear();
	outDateISO = DefaultMarkerDateAtVisibleCenter();
	const long centerY = (g_chartScreenRect.top + g_chartScreenRect.bottom) / 2;
	if (const RowBand* band = RowBandAtScreenY(centerY)) {
		outRowId = band->rowId;
		return;
	}
	if (!g_rowBands.empty()) {
		outRowId = g_rowBands[0].rowId;
		return;
	}
	if (!g_app) return;
	try {
		std::string json = ReadGanttFromSlide(g_app);
		if (json.empty()) return;
		PpDocument doc = DocumentFromJson(json);
		outRowId = FirstRowId(doc);
	}
	catch (...) {}
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
	const long centerScreenX = (g_chartScreenRect.left + g_chartScreenRect.right) / 2;
	ProjPx proj = ProjectionPx();
	if (proj.ok) {
		const long centerDay = proj.originDay + (long)::lround((centerScreenX - proj.originXpx) / proj.pxPerDay);
		start = DaysToDate(centerDay);
		end = DaysToDate(centerDay + 6);
		return;
	}
	start = DaysToDate(0);
	end = DaysToDate(6);
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
	const bool had = !g_ownSelKind.empty() || !g_ownSelId.empty() || !g_ownSelExtra.empty();
	g_ownSelKind.clear();
	g_ownSelId.clear();
	g_ownSelExtra.clear();
	if (had) {
		g_appBarValid = false;
		g_appBarModelDirty = true;
		g_appBarLastMeasuredContentW = 0;
		g_appBarLastMeasuredContentH = 0;
		g_appBarGeomValid = false;
		// R1b: ownSel transition at ClearOwnSelection choke point (SR-REC-07).
		if (g_recActive) {
			RecEvent("ownSel", "\"kind\":\"\",\"id\":\"\",\"reason\":\"ClearOwnSelection\"");
			RecEmitSnapshot();
			RecCaptureFrames("sel");
		}
	}
}

// Forward decls for the multi-select helpers below (definitions later in file).
bool IsItemOwnSelKind(const std::string& kind);
static bool TryPublishItemChromeFromSnapshot(const std::string& kind, const std::string& id);
void SetOwnSelection(const std::string& kind, const std::string& id);

static int OwnSelCount() {
	if (g_ownSelKind.empty() || g_ownSelId.empty()) return 0;
	return 1 + (int)g_ownSelExtra.size();
}

static bool HasMultiSelection() {
	return !g_ownSelExtra.empty();
}

static void InvalidateAppBarForSelectionChange() {
	g_appBarValid = false;
	g_appBarModelDirty = true;
	g_appBarLastMeasuredContentW = 0;
	g_appBarLastMeasuredContentH = 0;
	g_appBarGeomValid = false;
}

// Update primary ownSel without clearing g_ownSelExtra (multi-select paths).
static void SetOwnSelectionPrimary(const std::string& kind, const std::string& id) {
	if (id.empty()) {
		ClearOwnSelection();
		ClearLinkMode();
		return;
	}
	// M4 / SR-WIN-26: never install a selection the current scene does not
	// emit. Post-commit re-selects (drag/nudge/create) land here AFTER
	// RebuildChart recommitted the scene cache, so the cached doc IS the new
	// truth: a nudge that pushed the item outside an explicit window resets to
	// document context (UF-07) instead of leaving invisible live hotkeys.
	// Cache miss fails open (selection kept) — same trust model as the dump.
	{
		PpDocument cachedDoc;
		if (Gantt_TryPeekCachedDoc(&cachedDoc) && !TimeWindowEmitsItem(cachedDoc, kind, id)) {
			OvLog(L"M4: selection target hidden by time window - reset to document context");
			ClearOwnSelection();
			ClearLinkMode();
			return;
		}
	}
	if (g_linkMode && (kind != g_linkFromKind || id != g_linkFromId)) {
		ClearLinkMode();
	}
	const std::string prevKind = g_ownSelKind;
	const std::string prevId = g_ownSelId;
	const bool prevMulti = HasMultiSelection();
	g_ownSelKind = kind;
	g_ownSelId = id;
	const bool multiNow = HasMultiSelection();
	if (prevKind != kind || prevId != id || prevMulti != multiNow) {
		InvalidateAppBarForSelectionChange();
		// R1b: ownSel transition at SetOwnSelection choke point (SR-REC-07).
		if (g_recActive) {
			std::string body;
			body.reserve(64 + kind.size() + id.size());
			body += "\"kind\":\"";
			EntityJsonAppendEscaped(body, kind);
			body += "\",\"id\":\"";
			EntityJsonAppendEscaped(body, id);
			body += "\",\"reason\":\"SetOwnSelection\"";
			RecEvent("ownSel", body);
			RecEmitSnapshot();
			RecCaptureFrames("sel");
		}
	}

	if (IsItemOwnSelKind(kind)) {
		if (!::IsRectEmpty(&g_chartScreenRect) && SameRect(g_selScreenRect, g_chartScreenRect)) {
			::SetRectEmpty(&g_selScreenRect);
			::SetRectEmpty(&g_frameRect);
			g_hasSelectionChrome = false;
			g_selKind.clear();
			g_selId.clear();
			g_buttonsValid = false;
		}
		if (!TryPublishItemChromeFromSnapshot(kind, id) &&
			!::IsRectEmpty(&g_lastKnownItemSelRect) &&
			!SameRect(g_lastKnownItemSelRect, g_chartScreenRect)) {
			g_selKind = kind;
			g_selId = id;
			g_selScreenRect = g_lastKnownItemSelRect;
			g_hasSelectionChrome = true;
		}
	}
}

static void ToggleOwnSelectionMember(const std::string& kind, const std::string& id) {
	if (id.empty()) return;
	if (g_ownSelKind == kind && g_ownSelId == id) {
		if (!g_ownSelExtra.empty()) {
			const OwnSelEntry promoted = g_ownSelExtra.front();
			g_ownSelExtra.erase(g_ownSelExtra.begin());
			SetOwnSelectionPrimary(promoted.kind, promoted.id);
		} else {
			ClearOwnSelection();
		}
		return;
	}
	for (size_t i = 0; i < g_ownSelExtra.size(); ++i) {
		if (g_ownSelExtra[i].kind == kind && g_ownSelExtra[i].id == id) {
			g_ownSelExtra.erase(g_ownSelExtra.begin() + (ptrdiff_t)i);
			InvalidateAppBarForSelectionChange();
			return;
		}
	}
	if (!g_ownSelKind.empty() && !g_ownSelId.empty()) {
		g_ownSelExtra.push_back({ g_ownSelKind, g_ownSelId });
	}
	SetOwnSelectionPrimary(kind, id);
}

static void SelectRowRangeFromPrimary(const std::string& clickedRowId) {
	if (clickedRowId.empty()) return;
	if (g_ownSelKind != "ROW" || g_ownSelId.empty()) {
		SetOwnSelection("ROW", clickedRowId);
		return;
	}
	int iAnchor = -1, iClick = -1;
	for (size_t i = 0; i < g_rowBands.size(); ++i) {
		if (g_rowBands[i].rowId == g_ownSelId) iAnchor = (int)i;
		if (g_rowBands[i].rowId == clickedRowId) iClick = (int)i;
	}
	if (iAnchor < 0 || iClick < 0) {
		SetOwnSelection("ROW", clickedRowId);
		return;
	}
	const int lo = std::min(iAnchor, iClick);
	const int hi = std::max(iAnchor, iClick);
	std::vector<OwnSelEntry> extras;
	for (int i = lo; i <= hi; ++i) {
		const std::string& rid = g_rowBands[(size_t)i].rowId;
		if (rid == clickedRowId) continue;
		extras.push_back({ "ROW", rid });
	}
	g_ownSelExtra = std::move(extras);
	SetOwnSelectionPrimary("ROW", clickedRowId);
}

void ClearLinkMode() {
	g_linkMode = false;
	g_linkFromId.clear();
	g_linkFromKind.clear();
}

static bool IsItemOwnSelKind(const std::string& kind) {
	return kind == "TASK" || kind == "MILESTONE" || kind == "MARKER" || kind == "TEXT";
}

static HtItemKind OwnSelKindToHtItemKind(const std::string& kind) {
	if (kind == "MILESTONE") return HtItemKind::Milestone;
	if (kind == "MARKER") return HtItemKind::Marker;
	if (kind == "TEXT") return HtItemKind::Text;
	return HtItemKind::Task;
}

// Publish chrome for an item ownSel from the current hit snapshot when
// possible. Returns true when g_selScreenRect was set to an item-sized rect.
static bool TryPublishItemChromeFromSnapshot(const std::string& kind, const std::string& id) {
	if (!IsItemOwnSelKind(kind) || id.empty()) return false;
	const HtItemKind wantKind = OwnSelKindToHtItemKind(kind);
	for (const auto& item : g_hitSnapshot.items) {
		if (item.kind == wantKind && item.id == id) {
			g_selKind = kind;
			g_selId = id;
			g_selScreenRect = { item.rect.left, item.rect.top, item.rect.right, item.rect.bottom };
			g_hasSelectionChrome = true;
			g_lastKnownItemSelRect = g_selScreenRect;
			return true;
		}
	}
	return false;
}

void SetOwnSelection(const std::string& kind, const std::string& id) {
	g_ownSelExtra.clear();
	SetOwnSelectionPrimary(kind, id);
}

bool SameSelectionState(bool hasSelection, const RECT& selRect, const std::string& selId, const std::string& selKind) {
	return g_hasSelectionChrome == hasSelection && SameRect(g_selScreenRect, selRect) && g_selId == selId && g_selKind == selKind;
}

void InvalidateHitSnapshot() {
	g_hitSnapshot = HtSnapshot{};
	::SetRectEmpty(&g_hitCacheChartRect);
	g_hitCacheChildCount = -1;
	g_hitCacheChartRowY.clear();
	g_hitCacheChartProj.clear();
	g_entityCache.clear();
	g_recTaskEntityBindings.clear();
	g_recLastTaskGeometryRefreshMs = 0;
	g_entityDumpJson = "{\"entities\":[]}";
	g_entitySceneSig.clear();
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
	LINK_PORT_RADIUS = Scale(kBaseLinkPortRadius);
	LINK_PORT_GAP = Scale(kBaseLinkPortGap);
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
	g_hoverTaskId.clear();
	::SetRectEmpty(&g_hoverTaskRect);
	::SetRectEmpty(&g_hoverInsertRect);
	g_hoverInsertValid = false;
	for (int i = 0; i < 2; ++i) {
		::SetRectEmpty(&g_rowBoundaryInsertRects[i]);
		g_rowBoundaryInsertValid[i] = false;
	}
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
	// it) does not show the toolbar, nor does TEXT (Add/Del/+-% have no
	// meaning for a text annotation; it gets its own delete-only editor via
	// double-click instead). The CHART_ROOT chrome path is unrelated to the
	// internal model and keeps its pre-existing layout behavior.
	// V4: the bottom app bar (R8) is now the command surface; the old floating
	// mini-toolbar (Add/Del/-/+) is retired so it no longer double-renders next
	// to a selection. Keep the layout/hit code intact but never make it eligible.
	bool toolbarEligible = false;
	(void)width; (void)height;
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

static bool IsLinkableItemKind(HtItemKind kind) {
	return kind == HtItemKind::Task || kind == HtItemKind::Milestone;
}

static const char* HtItemKindToOwnSel(HtItemKind kind) {
	return (kind == HtItemKind::Milestone) ? "MILESTONE" : "TASK";
}

static POINT LinkPortScreenCenter(const HtRect& item, bool rightPort) {
	const int cx = rightPort ? item.right + LINK_PORT_GAP : item.left - LINK_PORT_GAP;
	const int cy = (item.top + item.bottom) / 2;
	return { cx, cy };
}

static RECT LinkPortHitRectScreen(const HtRect& item, bool rightPort) {
	POINT c = LinkPortScreenCenter(item, rightPort);
	const int r = LINK_PORT_RADIUS + Scale(2);
	return { c.x - r, c.y - r, c.x + r, c.y + r };
}

static bool ShouldShowLinkPortsOnItem(const HtItem& item, bool linkDragActive, const std::string& linkDragFromId) {
	if (!IsLinkableItemKind(item.kind)) return false;
	if (linkDragActive) return item.id != linkDragFromId;
	if (g_ownSelKind == "TASK" && item.kind == HtItemKind::Task && item.id == g_ownSelId) return true;
	if (g_ownSelKind == "MILESTONE" && item.kind == HtItemKind::Milestone && item.id == g_ownSelId) return true;
	return false;
}

static bool HitTestLinkPortAtClient(POINT clientPt, std::string* outId, std::string* outKind, bool* outRight) {
	if (g_linkMode || IsEditSessionActive()) return false;
	POINT screenPt = { clientPt.x + g_windowOriginX, clientPt.y + g_windowOriginY };
	const bool linkDragActive = g_gestureActive && g_dragKind == DragKind::LinkPort;
	for (const auto& item : g_hitSnapshot.items) {
		if (!ShouldShowLinkPortsOnItem(item, linkDragActive, g_dragId)) continue;
		for (int side = 0; side < 2; ++side) {
			const bool rightPort = (side != 0);
			RECT hr = LinkPortHitRectScreen(item.rect, rightPort);
			if (::PtInRect(&hr, screenPt)) {
				if (outId) *outId = item.id;
				if (outKind) *outKind = HtItemKindToOwnSel(item.kind);
				if (outRight) *outRight = rightPort;
				return true;
			}
		}
	}
	return false;
}

static void PaintLinkPortChip(Gdiplus::Graphics& g, POINT centerClient, bool highlighted) {
	const int d = LINK_PORT_RADIUS * 2;
	const int x = centerClient.x - LINK_PORT_RADIUS;
	const int y = centerClient.y - LINK_PORT_RADIUS;
	Gdiplus::GraphicsPath path;
	AddRoundRect(path, (Gdiplus::REAL)x, (Gdiplus::REAL)y, (Gdiplus::REAL)d, (Gdiplus::REAL)d, (Gdiplus::REAL)LINK_PORT_RADIUS);
	Gdiplus::SolidBrush fill(GpToken(highlighted ? 255 : 230, gt::surface));
	g.FillPath(&fill, &path);
	Gdiplus::Pen edge(GpToken(255, gt::primary), highlighted ? 2.0f : 1.5f);
	g.DrawPath(&edge, &path);
}

static bool IsWindowEdgeDragKind(DragKind kind) {
	return kind == DragKind::WindowEdgeL || kind == DragKind::WindowEdgeR;
}

static bool IsWindowEdgeDragActive() {
	return g_gestureActive && IsWindowEdgeDragKind(g_dragKind);
}

// Discoverability fix (2026-07-18). The ports USED to be gated on
// g_windowHeaderHover, i.e. on the pointer already being inside the axis header
// band. That band's PIXEL height is kAxisHeaderHeightPt * yScale, and
// yScale = chartHeightPt / PP_ROWY.naturalH (see BuildRowBands): every row the
// user adds grows naturalH while the fitted chart frame stays put, so the
// reveal target shrinks monotonically as the chart fills up. On an empty/near
// empty chart the band is tall enough to stumble into; after "placing tasks" it
// collapses to a few pixels and the affordance becomes unreachable — exactly
// the reported "couldn't find the arrow buttons". Live recordings agree: the
// port rects are {} in every snapshot of 9/10 sessions, and in the tenth they
// blink on for 2.7s (one header traversal) with the selection unchanged.
//
// The ports are now laid out and painted whenever the header band exists.
// Hover only promotes them from resting to emphasized; it no longer decides
// whether they exist at all.
static bool ShouldShowWindowPorts() {
	if (::IsRectEmpty(&g_headerBandScreenRect)) return false;
	if (g_linkMode || IsEditSessionActive()) return false;
	// A non-window gesture (task drag, create, link) owns the pointer; the
	// ports would only compete with its ghost/pill chrome.
	if (g_gestureActive && !IsWindowEdgeDragKind(g_dragKind)) return false;
	return true;
}

// Resting affordance geometry. The old target was a LINK_PORT_RADIUS circle
// (14x14 px in the recordings) pinned to the chart edge; that is below the
// threshold at which a control reads as a control. This is a chevron pill
// sized off the header band, floored so it never degrades with row count.
static RECT WindowPortHitRectScreen(bool rightPort) {
	if (!ShouldShowWindowPorts()) return {};
	const int w = std::max(Scale(20), LINK_PORT_RADIUS * 2 + Scale(8));
	const int bandH = g_headerBandScreenRect.bottom - g_headerBandScreenRect.top;
	const int h = std::max(Scale(20), std::min(w + Scale(6), bandH - Scale(2)));
	const int cx = rightPort
		? g_headerBandScreenRect.right - w / 2 - Scale(1)
		: g_headerBandScreenRect.left + w / 2 + Scale(1);
	const int cy = (g_headerBandScreenRect.top + g_headerBandScreenRect.bottom) / 2;
	RECT r = { cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2 };
	// Every pixel must stay inside the chart rect. The overlay is a layered
	// window hit-tested against its PER-PIXEL ALPHA, and only the chart rect
	// carries the capture-layer fill, so a port that spilled outside would be
	// click-through even while visibly painted.
	if (r.top < g_chartScreenRect.top) {
		r.bottom += g_chartScreenRect.top - r.top;
		r.top = g_chartScreenRect.top;
	}
	if (r.bottom > g_chartScreenRect.bottom) {
		r.top -= r.bottom - g_chartScreenRect.bottom;
		r.bottom = g_chartScreenRect.bottom;
	}
	if (r.left < g_chartScreenRect.left) {
		r.right += g_chartScreenRect.left - r.left;
		r.left = g_chartScreenRect.left;
	}
	if (r.right > g_chartScreenRect.right) {
		r.left -= r.right - g_chartScreenRect.right;
		r.right = g_chartScreenRect.right;
	}
	if (r.right <= r.left || r.bottom <= r.top) return {};
	return r;
}

// Pointer-over-port state, refreshed by UpdateHoverFromCursor so a hover
// transition is a repaint TRIGGER (SR-SMO-04) rather than something the paint
// path re-derives from the live cursor.
static HtZone WindowPortUnderPoint(POINT screenPt) {
	const RECT left = WindowPortHitRectScreen(false);
	const RECT right = WindowPortHitRectScreen(true);
	if (::PtInRect(&left, screenPt)) return HtZone::WindowPortL;
	if (::PtInRect(&right, screenPt)) return HtZone::WindowPortR;
	return HtZone::Outside;
}

static HtZone HitTestWindowPortAtClient(POINT clientPt) {
	if (!ShouldShowWindowPorts()) return HtZone::Outside;
	const POINT screenPt = { clientPt.x + g_windowOriginX, clientPt.y + g_windowOriginY };
	return WindowPortUnderPoint(screenPt);
}

// Resting chevron button. Painted at every repaint the ports are laid out for,
// so the control is FINDABLE: a bordered pill with an outward double chevron
// ("widen the visible range this way"), not an invisible 14px hotspot.
static void PaintWindowPort(Gdiplus::Graphics& g, const RECT& hitScreen, bool rightPort, bool highlighted) {
	if (::IsRectEmpty(&hitScreen)) return;
	const Gdiplus::REAL x = (Gdiplus::REAL)(hitScreen.left - g_windowOriginX);
	const Gdiplus::REAL y = (Gdiplus::REAL)(hitScreen.top - g_windowOriginY);
	const Gdiplus::REAL w = (Gdiplus::REAL)(hitScreen.right - hitScreen.left);
	const Gdiplus::REAL h = (Gdiplus::REAL)(hitScreen.bottom - hitScreen.top);
	Gdiplus::GraphicsPath path;
	AddRoundRect(path, x, y, w, h, ScaleF(3.0f));
	Gdiplus::SolidBrush fill(GpToken(highlighted ? 255 : 232, gt::surface));
	g.FillPath(&fill, &path);
	Gdiplus::Pen edge(GpToken(highlighted ? 255 : 200, gt::primary),
		highlighted ? ScaleF(2.0f) : ScaleF(1.0f));
	g.DrawPath(&edge, &path);

	const int cx = (hitScreen.left + hitScreen.right) / 2 - g_windowOriginX;
	const int cy = (hitScreen.top + hitScreen.bottom) / 2 - g_windowOriginY;
	const int arm = std::max(2, (int)(h / 5));
	const int gap = std::max(2, arm);
	const int dir = rightPort ? 1 : -1;
	Gdiplus::Pen glyph(GpToken(255, gt::primary), ScaleF(highlighted ? 2.0f : 1.6f));
	glyph.SetStartCap(Gdiplus::LineCapRound);
	glyph.SetEndCap(Gdiplus::LineCapRound);
	glyph.SetLineJoin(Gdiplus::LineJoinRound);
	for (int i = 0; i < 2; ++i) {
		const int tipX = cx + dir * (gap / 2 + i * gap);
		Gdiplus::Point chevron[3] = {
			{ tipX - dir * arm, cy - arm },
			{ tipX, cy },
			{ tipX - dir * arm, cy + arm }
		};
		g.DrawLines(&glyph, chevron, 3);
	}
}

// Hover hint. The affordance is now visible at rest, but "what does this do"
// still has to be answerable without a manual — so name the gesture the moment
// the pointer lands on it.
static void PaintWindowPortHint(Gdiplus::Graphics& g, const RECT& hitScreen, bool rightPort) {
	if (::IsRectEmpty(&hitScreen)) return;
	const wchar_t* text = L"Click or drag to widen";
	Gdiplus::Font font(L"Segoe UI", ScaleF(10.0f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::RectF measured;
	g.MeasureString(text, -1, &font, Gdiplus::PointF(0.0f, 0.0f), &measured);
	const Gdiplus::REAL padX = ScaleF(6.0f), padY = ScaleF(3.0f);
	const Gdiplus::REAL w = measured.Width + padX * 2.0f;
	const Gdiplus::REAL h = measured.Height + padY * 2.0f;
	Gdiplus::REAL x = rightPort
		? (Gdiplus::REAL)(hitScreen.left - g_windowOriginX) - w - ScaleF(4.0f)
		: (Gdiplus::REAL)(hitScreen.right - g_windowOriginX) + ScaleF(4.0f);
	const Gdiplus::REAL y = (Gdiplus::REAL)(hitScreen.top - g_windowOriginY);
	const Gdiplus::REAL chartLeft = (Gdiplus::REAL)(g_chartScreenRect.left - g_windowOriginX);
	const Gdiplus::REAL chartRight = (Gdiplus::REAL)(g_chartScreenRect.right - g_windowOriginX);
	if (x < chartLeft) x = chartLeft;
	if (x + w > chartRight) x = chartRight - w;
	if (x < chartLeft) return; // chart narrower than the hint: skip rather than spill
	Gdiplus::GraphicsPath path;
	AddRoundRect(path, x, y, w, h, ScaleF(3.0f));
	Gdiplus::SolidBrush fill(GpToken(240, gt::ink));
	g.FillPath(&fill, &path);
	Gdiplus::SolidBrush ink(GpToken(255, gt::surface));
	Gdiplus::StringFormat sf;
	sf.SetAlignment(Gdiplus::StringAlignmentCenter);
	sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
	sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
	g.DrawString(text, -1, &font, Gdiplus::RectF(x, y, w, h), &sf, &ink);
}

static void PaintWindowPorts(Gdiplus::Graphics& g) {
	if (!ShouldShowWindowPorts()) return;
	const RECT left = WindowPortHitRectScreen(false);
	const RECT right = WindowPortHitRectScreen(true);
	const bool dragL = IsWindowEdgeDragActive() && g_dragKind == DragKind::WindowEdgeL;
	const bool dragR = IsWindowEdgeDragActive() && g_dragKind == DragKind::WindowEdgeR;
	const bool hoverL = g_windowPortHoverZone == HtZone::WindowPortL;
	const bool hoverR = g_windowPortHoverZone == HtZone::WindowPortR;
	PaintWindowPort(g, left, false, dragL || hoverL);
	PaintWindowPort(g, right, true, dragR || hoverR);
	if (!IsWindowEdgeDragActive()) {
		if (hoverL) PaintWindowPortHint(g, left, false);
		else if (hoverR) PaintWindowPortHint(g, right, true);
	}
}

// W2's two-phase renderer: paint the candidate axis only into the header
// overlay strip. The source shapes and their PP_* tags are intentionally never
// touched on mouse move; W3 will own the one-shot document/rebuild commit.
static void PaintWindowAxisPreview(Gdiplus::Graphics& g) {
	if (!g_dragActive || !IsWindowEdgeDragActive() || g_windowCandidateStart.empty()
		|| g_windowCandidateEnd.empty() || ::IsRectEmpty(&g_headerBandScreenRect)) return;
	const int left = g_headerBandScreenRect.left - g_windowOriginX;
	const int top = g_headerBandScreenRect.top - g_windowOriginY;
	const int right = g_headerBandScreenRect.right - g_windowOriginX;
	const int bottom = g_headerBandScreenRect.bottom - g_windowOriginY;
	if (right <= left || bottom <= top) return;

	// Opaque strip clears the frozen native header pixels below before painting
	// the candidate. Timeline labels/ticks begin after the rail; the header
	// fill deliberately spans the whole band, matching HEADER_BAND.
	Gdiplus::SolidBrush fill(GpToken(255, gt::headerBand));
	g.FillRectangle(&fill, left, top, right - left, bottom - top);
	int plotLeft = g_hitSnapshot.railRightPx - g_windowOriginX;
	if (plotLeft <= left || plotLeft >= right) plotLeft = left;
	const long startDay = DateToDays(g_windowCandidateStart);
	const long endDay = DateToDays(g_windowCandidateEnd);
	const long visibleDays = std::max(1L, endDay - startDay + 1);
	const float previewPtPerDay = (float)(right - plotLeft) / (float)visibleDays;
	const float axisH = (float)(bottom - top);
	const float headMid = (float)top + axisH / 2.0f;
	const AxisTierLayout axis = ComputeAxisTierLayout(g_windowPreviewDoc,
		g_windowCandidateStart, g_windowCandidateEnd, 0, previewPtPerDay,
		(float)plotLeft, (float)right, (float)top, headMid, axisH, (float)bottom);

	auto paintLabels = [&](const std::vector<AxisTierLabel>& labels, unsigned long color) {
		Gdiplus::Font font(L"Segoe UI", std::max(7.0f, ScaleF(7.0f)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
		Gdiplus::SolidBrush brush(GpToken(255, color));
		for (const auto& label : labels) {
			Gdiplus::StringFormat format;
			format.SetAlignment(label.centered ? Gdiplus::StringAlignmentCenter : Gdiplus::StringAlignmentNear);
			format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
			format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
			const std::wstring text = Widen(label.label);
			g.DrawString(text.c_str(), -1, &font,
				Gdiplus::RectF(label.rect.left, label.rect.top,
					label.rect.right - label.rect.left, label.rect.bottom - label.rect.top),
				&format, &brush);
		}
	};
	paintLabels(axis.bottomLabels, gt::ink2);
	if (axis.hasBottomBand) {
		Gdiplus::Pen divider(GpToken(255, gt::outline), ScaleF(gt::hairline));
		g.DrawLine(&divider, (INT)axis.bandDivider.left, (INT)axis.bandDivider.top,
			(INT)axis.bandDivider.right, (INT)axis.bandDivider.bottom);
	}
	paintLabels(axis.topLabels, gt::ink3);
	auto paintTicks = [&](const std::vector<AxisTierTick>& ticks, unsigned long color, float weight) {
		for (const auto& tick : ticks) {
			Gdiplus::Pen pen(GpToken(255, color), ScaleF(weight));
			if (tick.dashed) pen.SetDashStyle(Gdiplus::DashStyleDot);
			g.DrawLine(&pen, (INT)tick.x, (INT)tick.top, (INT)tick.x, (INT)tick.bottom);
		}
	};
	paintTicks(axis.ticks, gt::outline, gt::hairline);
	paintTicks(axis.majorTicks, gt::outline2, gt::hairline_major);
}

static void PaintWindowPill(Gdiplus::Graphics& g) {
	if (!g_dragActive || !IsWindowEdgeDragActive() || g_dragPillText.empty()) return;
	const std::wstring text = Widen(g_dragPillText);
	Gdiplus::Font font(L"Segoe UI", g_tooltipFontPx, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::RectF bounds;
	g.MeasureString(text.c_str(), -1, &font, Gdiplus::PointF(0, 0), &bounds);
	const int w = (int)bounds.Width + g_tooltipPad * 2;
	const int h = (int)bounds.Height + g_tooltipPad * 2;
	int x = g_dragLastPt.x + Scale(14);
	int y = g_dragLastPt.y + Scale(14);
	if (x + w > g_headerBandScreenRect.right - g_windowOriginX) x = g_headerBandScreenRect.right - g_windowOriginX - w;
	if (x < INFL) x = INFL;
	Gdiplus::GraphicsPath path;
	AddRoundRect(path, (Gdiplus::REAL)x, (Gdiplus::REAL)y, (Gdiplus::REAL)w, (Gdiplus::REAL)h, 3.0f);
	Gdiplus::SolidBrush bg(GpToken(245, gt::ink));
	g.FillPath(&bg, &path);
	Gdiplus::StringFormat format;
	format.SetAlignment(Gdiplus::StringAlignmentCenter);
	format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
	Gdiplus::SolidBrush fg(GpToken(255, gt::surface));
	g.DrawString(text.c_str(), -1, &font, Gdiplus::RectF((Gdiplus::REAL)x, (Gdiplus::REAL)y,
		(Gdiplus::REAL)w, (Gdiplus::REAL)h), &format, &fg);
}

static void PaintLinkPortsForSnapshot(Gdiplus::Graphics& g) {
	const bool linkDragActive = g_gestureActive && g_dragKind == DragKind::LinkPort;
	for (const auto& item : g_hitSnapshot.items) {
		if (!ShouldShowLinkPortsOnItem(item, linkDragActive, g_dragId)) continue;
		for (int side = 0; side < 2; ++side) {
			const bool rightPort = (side != 0);
			POINT sc = LinkPortScreenCenter(item.rect, rightPort);
			POINT cc = { sc.x - g_windowOriginX, sc.y - g_windowOriginY };
			const bool hi = linkDragActive && !g_linkDragHoverId.empty() &&
				item.id == g_linkDragHoverId;
			PaintLinkPortChip(g, cc, hi);
		}
	}
}

static void UpdateLinkDragHover(POINT clientPt) {
	g_linkDragHoverId.clear();
	g_linkDragHoverKind.clear();
	if (g_dragKind != DragKind::LinkPort) return;
	HtHit hit = HitTestClientPoint(clientPt);
	if (hit.zone == HtZone::TaskBody || hit.zone == HtZone::TaskEdgeL ||
		hit.zone == HtZone::TaskEdgeR || hit.zone == HtZone::Milestone) {
		if (!hit.id.empty() && hit.id != g_dragId) {
			g_linkDragHoverId = hit.id;
			g_linkDragHoverKind = (hit.zone == HtZone::Milestone) ? "MILESTONE" : "TASK";
		}
	}
}

void LayoutHoverInsertHotspot() {
	g_hoverInsertValid = false;
	::SetRectEmpty(&g_hoverInsertRect);
	for (int i = 0; i < 2; ++i) {
		::SetRectEmpty(&g_rowBoundaryInsertRects[i]);
		g_rowBoundaryInsertValid[i] = false;
	}
	if (g_hoverRowId.empty() || ::IsRectEmpty(&g_hoverBandRect)) return;
	if (::GetKeyState(VK_LBUTTON) & 0x8000) return;

	int bandTop = g_hoverBandRect.top - g_windowOriginY;
	int bandBottom = g_hoverBandRect.bottom - g_windowOriginY;
	int cy = (bandTop + bandBottom) / 2;
	const int pad6 = Scale(6);
	const int pad4 = Scale(4);
	int railCenterX = g_chartScreenRect.left - g_windowOriginX + pad6;
	for (const auto& band : g_rowBands) {
		if (band.rowId == g_hoverRowId) {
			const int railLeft = band.screenLeftGutter - g_windowOriginX;
			const int railRight = ::IsRectEmpty(&band.screenRailRect)
				? (g_chartScreenRect.left - g_windowOriginX + Scale(40))
				: (band.screenRailRect.right - g_windowOriginX);
			railCenterX = (railLeft + railRight) / 2;
			const int chipHalf = ROW_INSERT_BUTTON / 2;
			// UF-06: row-adder chips on BOTH boundaries, centered on the boundary line.
			g_rowBoundaryInsertRects[0] = {
				railCenterX - chipHalf, bandTop - chipHalf,
				railCenterX + chipHalf, bandTop + chipHalf
			};
			g_rowBoundaryInsertRects[1] = {
				railCenterX - chipHalf, bandBottom - chipHalf,
				railCenterX + chipHalf, bandBottom + chipHalf
			};
			g_rowBoundaryInsertValid[0] = true;
			g_rowBoundaryInsertValid[1] = true;
			break;
		}
	}
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

static int RowBoundaryInsertFromClientPoint(POINT pt) {
	if (!g_hoverInsertValid) LayoutHoverInsertHotspot();
	for (int i = 0; i < 2; ++i) {
		if (g_rowBoundaryInsertValid[i] && ::PtInRect(&g_rowBoundaryInsertRects[i], pt)) return i;
	}
	return -1;
}

bool HoverInsertFromClientPoint(POINT pt) {
	if (!g_hoverInsertValid) LayoutHoverInsertHotspot();
	return g_hoverInsertValid && ::PtInRect(&g_hoverInsertRect, pt);
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
		try {
			PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
			_bstr_t kind = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (!kind.length() || Narrow((const wchar_t*)kind) != "CHART_ROOT") continue;
			// External undo (ExecuteMso) can leave a deleted-but-still-enumerable
			// zombie CHART_ROOT: PP_KIND still reads, but geometry / GroupItems
			// throw 0x800A01A8. Probe the accessors BuildRowBands needs and skip
			// dead candidates so the live restored group is found instead.
			(void)sh->GetLeft();
			PowerPoint::GroupShapesPtr items = sh->GetGroupItems();
			if (!items) continue;
			(void)items->GetCount();
			return sh;
		} catch (const _com_error&) {
			continue;
		}
	}
	return nullptr;
}

// Single per-tick child walk: builds the row bands, inline-edit regions AND
// the semantic hit-test snapshot. When the chart screen rect, child count, and
// PP_ROWY/PP_PROJ tag payloads are unchanged since the last walk, the cached
// results are reused so the per-tick COM cost stays bounded (one GetGroupItems
// + GetCount call). Tag fingerprints are part of the key because an external
// undo (ExecuteMso) can restore the same child count + screen rect while
// changing row geometry without going through RebuildChart's invalidation.
void BuildRowBands(PowerPoint::ShapePtr chart, PowerPoint::DocumentWindowPtr win) {
	if (!chart || !win) {
		g_rowBands.clear();
		g_editRegions.clear();
		::SetRectEmpty(&g_headerBandScreenRect);
		g_windowHeaderHover = false;
		g_windowPortHoverZone = HtZone::Outside;
		InvalidateHitSnapshot();
		return;
	}
	try {
		PowerPoint::GroupShapesPtr items = chart->GetGroupItems();
		if (!items) {
			g_rowBands.clear();
			g_editRegions.clear();
			::SetRectEmpty(&g_headerBandScreenRect);
			g_windowHeaderHover = false;
			g_windowPortHoverZone = HtZone::Outside;
			InvalidateHitSnapshot();
			return;
		}
		long n = items->GetCount();
		if (n == g_hitCacheChildCount && SameRect(g_chartScreenRect, g_hitCacheChartRect)
			&& g_chartRowY == g_hitCacheChartRowY && g_chartProj == g_hitCacheChartProj) {
			return; // cache hit: reuse g_rowBands / g_editRegions / g_hitSnapshot / g_entityCache
		}
		g_rowBands.clear();
		g_editRegions.clear();
		g_entityCache.clear();
		g_recTaskEntityBindings.clear();
		g_recLastTaskGeometryRefreshMs = 0;
		g_entityDumpJson = "{\"entities\":[]}";
		g_entitySceneSig.clear();
		::SetRectEmpty(&g_headerBandScreenRect);
		g_hitSnapshot = HtSnapshot{};
		g_hitSnapshot.chartRect = ToHtRect(g_chartScreenRect);
		g_hitSnapshot.edgeBandPx = Scale((int)kHtEdgePx);

		// Markers (TODAY_LINE/DEADLINE/CUSTOM_MARKER) are thin rendered lines —
		// their shape rect is near-zero width, unusable for hit-testing. Instead
		// synthesize each marker's hit band directly from PP_PROJ (read fresh
		// here, never cached across commits per fix-fit-persistence: the chart
		// frame is preserved across rebuilds, but ptPerDay/originX can still
		// shift on a rescale) + the marker's date from PP_DOC. Read once per
		// walk (cheap: one more Tags lookup on the already-open chart shape).
		PpProj proj;
		bool haveProj = ParseProj(g_chartProj, &proj);
		PpDocument chartDoc;
		bool haveDoc = false;
		if (haveProj) {
			_bstr_t docTag = chart->GetTags()->Item(_bstr_t(L"PP_DOC"));
			if (docTag.length()) {
				chartDoc = DocumentFromJson(Narrow((const wchar_t*)docTag));
				haveDoc = true;
			}
		}
		const std::vector<PpMarker>& markers = chartDoc.markers;
		// The builder cache is the semantic visual truth. Match its prims to the
		// live group children by stable (PP_KIND, PP_ID, ordinal), preserving the
		// COM-derived live rectangles while sourcing text/style/clip metadata from
		// the pure scene (SR-ENT-01..05).
		Scene cachedScene;
		const bool haveScene = Gantt_TryPeekCachedScene(&cachedScene);
		std::map<std::pair<std::string, std::string>, std::vector<const Prim*>> scenePrims;
		std::map<std::pair<std::string, std::string>, size_t> sceneOrdinals;
		if (haveScene) {
			for (const Prim& prim : cachedScene.prims)
				scenePrims[{ prim.tagKind, prim.tagId }].push_back(&prim);
		}

		for (long i = 1; i <= n; ++i) {
			PowerPoint::ShapePtr ch = items->Item(_variant_t(i));
			_bstr_t kind = ch->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (!kind.length()) continue;
			std::string kindStr = Narrow((const wchar_t*)kind);

			// R1a: collect a PpEntity for EVERY PP_KIND child (axis, rails, title,
			// markers, progress fills — not only hit-testable ones).
			_bstr_t idTag = ch->GetTags()->Item(_bstr_t(L"PP_ID"));
			std::string idStr = idTag.length() ? Narrow((const wchar_t*)idTag) : "";
			float left = ch->GetLeft(), top = ch->GetTop(), w = ch->GetWidth(), h = ch->GetHeight();
			RECT rr = {
				win->PointsToScreenPixelsX(left),
				win->PointsToScreenPixelsY(top),
				win->PointsToScreenPixelsX(left + w),
				win->PointsToScreenPixelsY(top + h)
			};
			NormalizeRect(rr);
			{
				PpEntity ent;
				ent.id = idStr;
				ent.kind = kindStr;
				ent.parentId = EntityParentId(kindStr, idStr);
				if (kindStr == "ROW_LABEL") {
					ent.rowId = idStr;
				} else if (haveDoc && !idStr.empty()) {
					for (const auto& t : chartDoc.tasks) {
						if (t.id == idStr) { ent.rowId = t.rowId; break; }
					}
					if (ent.rowId.empty()) {
						for (const auto& m : chartDoc.milestones) {
							if (m.id == idStr) { ent.rowId = m.rowId; break; }
						}
					}
				}
				ent.slideRect = { (double)left, (double)top, (double)w, (double)h };
				ent.screenRect = {
					(double)rr.left, (double)rr.top,
					(double)(rr.right - rr.left), (double)(rr.bottom - rr.top)
				};
				ent.z = (int)g_entityCache.size();
				const auto key = std::make_pair(kindStr, idStr);
				const size_t ordinal = sceneOrdinals[key]++;
				auto sceneIt = scenePrims.find(key);
				if (sceneIt != scenePrims.end() && ordinal < sceneIt->second.size()) {
					const Prim& prim = *sceneIt->second[ordinal];
					ent.text = prim.text.empty() ? "" : Narrow(prim.text.c_str());
					ent.style.fillArgb = prim.style.fill
						? EntityRgbFromBgr(prim.style.fillBgr) : 0;
					ent.style.strokeArgb = prim.style.line
						? EntityRgbFromBgr(prim.style.lineBgr)
						: EntityRgbFromBgr(prim.style.textBgr);
					ent.style.strokeW = prim.style.line ? prim.style.lineWeight : 0.0;
					if (prim.kind == PrimKind::Text && prim.style.fontSize > 0.0f) {
						const int x0 = win->PointsToScreenPixelsX(0.0f);
						const int xf = win->PointsToScreenPixelsX(prim.style.fontSize);
						ent.style.fontPx = (double)std::abs(xf - x0);
					}
					ent.flags.clipped = prim.clippedL || prim.clippedR;
				}
				ent.flags.visible = true;
				g_entityCache.push_back(std::move(ent));
				if (kindStr == "TASK" || kindStr == "TASK_PROGRESS" || kindStr == "TASK_LABEL")
					g_recTaskEntityBindings.push_back({ g_entityCache.size() - 1, ch });
			}

			// TASK_PROGRESS is the inner fill of a TASK bar — the TASK rect
			// already covers it, so it does not participate in hit-testing.
			// TODAY_LABEL/DEADLINE_LABEL/CUSTOM_MARKER_LABEL are the marker's
			// text chip, not the line itself — also excluded (the line's own
			// PP_KIND, handled below via the marker-band synthesis, is what
			// drives HtZone::Marker).
			bool isMarkerLine = kindStr == "TODAY_LINE" || kindStr == "DEADLINE" || kindStr == "CUSTOM_MARKER";
			bool isMarkerLabel = kindStr == "TODAY_LABEL" || kindStr == "DEADLINE_LABEL" || kindStr == "CUSTOM_MARKER_LABEL";
			bool isTaskLabel = kindStr == "TASK_LABEL" || kindStr == "RAIL_TASKLBL";
			bool isMilestoneLabel = kindStr == "MILESTONE_LABEL";
			if (kindStr != "ROW_LABEL" && kindStr != "TITLE" && kindStr != "TASK" && kindStr != "MILESTONE" && kindStr != "TEXT" && kindStr != "DEP"
				&& !isMarkerLine && !isMarkerLabel && !isTaskLabel && !isMilestoneLabel) continue;
			if (isMarkerLine) {
				// Ignore the rendered rect entirely (see comment above); derive
				// the band from PP_PROJ + this marker's date instead. If PP_PROJ
				// didn't parse or the marker's date isn't found in PP_DOC (doc/
				// tags out of sync mid-rebuild), skip rather than guess.
				if (!haveProj) continue;
				if (idStr.empty()) continue;
				const PpMarker* mk = nullptr;
				for (const auto& m : markers) if (m.id == idStr) { mk = &m; break; }
				if (!mk) continue;
				long dayIdx = DateToDays(mk->date) - proj.minDay + proj.pad;
				float xPt = proj.originX + (float)dayIdx * proj.ptPerDay;
				int screenX = win->PointsToScreenPixelsX(xPt);
				long halfBand = Scale((int)kHtEdgePx);
				RECT mr = { screenX - halfBand, g_chartScreenRect.top, screenX + halfBand, g_chartScreenRect.bottom };
				g_hitSnapshot.items.push_back({ HtItemKind::Marker, idStr, ToHtRect(mr) });
				continue;
			}
			if (rr.bottom <= rr.top) continue;
			if (kindStr == "TITLE") {
				g_editRegions.push_back({ "TITLE", "", rr });
				g_hitSnapshot.items.push_back({ HtItemKind::Title, "", ToHtRect(rr) });
				continue;
			}
			if (idStr.empty()) continue;
			if (isTaskLabel) {
				// Edit region (inline rename) + hit unit: TASK_LABEL is part of
				// the task bar, not a separate selectable object (SR-TASK-UNIT).
				// RAIL_TASKLBL stays edit-only so the rail still selects the ROW
				// via RowBand; only graph-side TASK_LABEL participates in hits.
				g_editRegions.push_back({ "TASK", idStr, rr });
				if (kindStr == "TASK_LABEL") {
					g_hitSnapshot.items.push_back({ HtItemKind::TaskLabel, idStr, ToHtRect(rr) });
				}
				continue;
			}
			if (isMilestoneLabel) {
				g_editRegions.push_back({ "MILESTONE", idStr, rr });
				continue;
			}
			if (isMarkerLabel) {
				g_editRegions.push_back({ "MARKER", idStr, rr });
				continue;
			}
			if (kindStr == "TASK") {
				HtItem item;
				item.kind = HtItemKind::Task;
				item.id = idStr;
				item.rect = ToHtRect(rr);
				if (haveDoc) {
					for (const auto& t : chartDoc.tasks) {
						if (t.id == idStr && t.percent > 0 && t.percent < 100) {
							item.progressPercent = t.percent;
							break;
						}
					}
				}
				g_hitSnapshot.items.push_back(item);
				continue;
			}
			if (kindStr == "MILESTONE") {
				g_hitSnapshot.items.push_back({ HtItemKind::Milestone, idStr, ToHtRect(rr) });
				continue;
			}
			if (kindStr == "DEP") {
				const int depPad = Scale(4);
				RECT hr = {
					rr.left - depPad, rr.top - depPad,
					rr.right + depPad, rr.bottom + depPad
				};
				g_hitSnapshot.items.push_back({ HtItemKind::Dependency, idStr, ToHtRect(hr) });
				continue;
			}
			if (kindStr == "TEXT") {
				g_editRegions.push_back({ "TEXT", idStr, rr });
				g_hitSnapshot.items.push_back({ HtItemKind::Text, idStr, ToHtRect(rr) });
				continue;
			}
			if (kindStr == "ROW_LABEL") {
				g_editRegions.push_back({ "ROW_LABEL", idStr, rr });
				g_hitSnapshot.items.push_back({ HtItemKind::RowLabel, idStr, ToHtRect(rr) });
				continue;
			}
		}

		// Row bands: prefer PP_ROWY (model-derived geometry, fixes rows with no
		// ROW_LABEL shape). Fall back to label-derived bands for older charts.
		PpRowY rowy;
		bool haveRowY = ParseRowY(g_chartRowY, &rowy);
		if (haveRowY) {
			float chartLeftPt = chart->GetLeft();
			float chartTopPt = chart->GetTop();
			float chartWPt = chart->GetWidth();
			float chartHPt = chart->GetHeight();
			float yScale = (rowy.naturalH > 0.0f) ? chartHPt / rowy.naturalH : 1.0f;
			float xScale = (rowy.naturalW > 0.0f) ? chartWPt / rowy.naturalW : 1.0f;
			int railLeftScreen = win->PointsToScreenPixelsX(chartLeftPt + rowy.railL * xScale);
			int railRightScreen = win->PointsToScreenPixelsX(chartLeftPt + rowy.railR * xScale);
			g_hitSnapshot.railLeftPx = railLeftScreen;
			g_hitSnapshot.railRightPx = railRightScreen;
			// M1: derive the header band from the model-derived first row lane.
			// HEADER_BAND itself remains out of the semantic item walk, avoiding a
			// synthetic header "item" in task/marker hit ordering. PP_ROWY is in
			// chart-local points; yScale maps it through any fitted chart frame.
			if (!rowy.rows.empty()) {
				float firstTop = rowy.rows.front().top;
				for (const auto& entry : rowy.rows) firstTop = std::min(firstTop, entry.top);
				const float headerTopPt = chartTopPt + (firstTop - kAxisHeaderHeightPt) * yScale;
				const float headerBottomPt = chartTopPt + firstTop * yScale;
				g_headerBandScreenRect = {
					g_chartScreenRect.left,
					win->PointsToScreenPixelsY(headerTopPt),
					g_chartScreenRect.right,
					win->PointsToScreenPixelsY(headerBottomPt)
				};
				NormalizeRect(g_headerBandScreenRect);
			}
			for (const auto& entry : rowy.rows) {
				float absTop = chartTopPt + entry.top * yScale;
				float absBot = chartTopPt + entry.bot * yScale;
				RECT bandRect = {
					g_chartScreenRect.left,
					win->PointsToScreenPixelsY(absTop),
					g_chartScreenRect.right,
					win->PointsToScreenPixelsY(absBot)
				};
				RECT railRect = {
					railLeftScreen,
					bandRect.top,
					railRightScreen,
					bandRect.bottom
				};
				NormalizeRect(bandRect);
				NormalizeRect(railRect);
				if (bandRect.bottom <= bandRect.top) continue;
				g_rowBands.push_back({ entry.id, bandRect, railLeftScreen, railRect });
			}
		} else {
			for (long i = 1; i <= n; ++i) {
				PowerPoint::ShapePtr ch = items->Item(_variant_t(i));
				_bstr_t kind = ch->GetTags()->Item(_bstr_t(L"PP_KIND"));
				if (!kind.length()) continue;
				std::string kindStr = Narrow((const wchar_t*)kind);
				if (kindStr != "ROW_LABEL") continue;
				_bstr_t id = ch->GetTags()->Item(_bstr_t(L"PP_ID"));
				std::string idStr = Narrow((const wchar_t*)id);
				if (idStr.empty()) continue;
				float left = ch->GetLeft(), top = ch->GetTop(), w = ch->GetWidth(), h = ch->GetHeight();
				RECT rr = {
					win->PointsToScreenPixelsX(left),
					win->PointsToScreenPixelsY(top),
					win->PointsToScreenPixelsX(left + w),
					win->PointsToScreenPixelsY(top + h)
				};
				NormalizeRect(rr);
				if (rr.bottom <= rr.top) continue;
				RECT railRect = { g_chartScreenRect.left, rr.top, rr.right, rr.bottom };
				g_rowBands.push_back({ idStr, { g_chartScreenRect.left, rr.top, g_chartScreenRect.right, rr.bottom }, rr.left, railRect });
			}
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
		g_hitCacheChartRowY = g_chartRowY;
		g_hitCacheChartProj = g_chartProj;
		// R1a: scene signature from the same inputs as the hit-snapshot cache key
		// (SR-ENT-05/06). Flags are re-applied at dump time; cache holds geometry.
		{
			char rectBuf[96];
			::snprintf(rectBuf, sizeof(rectBuf), "%ld,%ld,%ld,%ld|%ld",
				g_chartScreenRect.left, g_chartScreenRect.top,
				g_chartScreenRect.right, g_chartScreenRect.bottom, n);
			g_entitySceneSig = std::string(rectBuf) + "|" + g_chartRowY + "|" + g_chartProj;
		}
		g_entityDumpJson = EntityDumpToJson(g_entityCache);
		g_entitySceneSig = g_entityDumpJson;
		if (g_rowBands.empty()) {
			wchar_t buf[96];
			::swprintf_s(buf, 96, L"BuildRowBands: 0 bands (children=%ld rowY=%zu chars)", n, g_chartRowY.size());
			OvLog(buf);
		}
	}
	catch (const _com_error& e) {
		wchar_t buf[96];
		::swprintf_s(buf, 96, L"BuildRowBands: COM error 0x%08lX - cleared", (unsigned long)e.Error());
		OvLog(buf);
		g_rowBands.clear();
		g_editRegions.clear();
		::SetRectEmpty(&g_headerBandScreenRect);
		g_windowHeaderHover = false;
		g_windowPortHoverZone = HtZone::Outside;
		InvalidateHitSnapshot();
	}
	catch (const std::exception&) {
		// DocumentFromJson / ParseRowY inputs come from tags that can be torn
		// mid-rebuild — a parse failure must degrade like a COM failure, not
		// escape into Tick's catch-all (which hides the whole overlay).
		OvLog(L"BuildRowBands: parse error - cleared");
		g_rowBands.clear();
		g_editRegions.clear();
		::SetRectEmpty(&g_headerBandScreenRect);
		g_windowHeaderHover = false;
		g_windowPortHoverZone = HtZone::Outside;
		InvalidateHitSnapshot();
	}
}

// Recompute the chrome-facing selection state (g_selId/g_selKind/
// g_hasSelectionChrome/g_selScreenRect) FROM the internal selection model
// (g_ownSelKind/g_ownSelId) plus the current hit-test snapshot / row bands.
// When the snapshot is empty (post-RebuildChart, pre-next-Tick) ownSel is kept
// and chrome stays empty/last-known — never chart-sized. When the snapshot is
// populated but the item is gone, ownSel is cleared.
void SyncSelectionChromeFromOwnSelection() {
	ClearSelectionState();
	if (g_ownSelKind.empty() || g_ownSelId.empty()) return;

	if (g_ownSelKind == "ROW") {
		for (const auto& band : g_rowBands) {
			if (band.rowId == g_ownSelId) {
				g_selKind = "ROW";
				g_selId = g_ownSelId;
				g_selScreenRect = ::IsRectEmpty(&band.screenRailRect) ? band.screenRect : band.screenRailRect;
				g_hasSelectionChrome = true;
				return;
			}
		}
		ClearOwnSelection();
		return;
	}

	if (g_ownSelKind == "DEP") {
		RECT unionRect = {};
		bool any = false;
		for (const auto& item : g_hitSnapshot.items) {
			if (item.kind != HtItemKind::Dependency || item.id != g_ownSelId) continue;
			RECT r = { item.rect.left, item.rect.top, item.rect.right, item.rect.bottom };
			if (!any) {
				unionRect = r;
				any = true;
			} else {
				unionRect.left = std::min(unionRect.left, r.left);
				unionRect.top = std::min(unionRect.top, r.top);
				unionRect.right = std::max(unionRect.right, r.right);
				unionRect.bottom = std::max(unionRect.bottom, r.bottom);
			}
		}
		if (any) {
			g_selKind = "DEP";
			g_selId = g_ownSelId;
			g_selScreenRect = unionRect;
			g_hasSelectionChrome = true;
			return;
		}
		if (g_hitSnapshot.items.empty()) return;
		ClearOwnSelection();
		return;
	}

	if (TryPublishItemChromeFromSnapshot(g_ownSelKind, g_ownSelId)) return;
	// Snapshot invalidated (empty) but ownSel still names a live item — keep
	// ownSel, publish empty/last-known chrome, never chart-sized fallback.
	if (g_hitSnapshot.items.empty()) return;
	ClearOwnSelection();
}

bool UpdateHoverFromCursor() {
	std::string oldId = g_hoverRowId;
	RECT oldRect = g_hoverBandRect;
	const std::string oldTaskId = g_hoverTaskId;
	const RECT oldTaskRect = g_hoverTaskRect;
	const bool oldWindowHeaderHover = g_windowHeaderHover;
	const HtZone oldWindowPortHover = g_windowPortHoverZone;
	ClearHoverState();
	g_windowHeaderHover = false;
	g_windowPortHoverZone = HtZone::Outside;

	// SR-SMO-04: hover is a repaint TRIGGER, so "changed" must be exact —
	// returning true on an unchanged target would repaint on every WM_MOUSEMOVE.
	auto changed = [&]() {
		return oldId != g_hoverRowId || !SameRect(oldRect, g_hoverBandRect)
			|| oldTaskId != g_hoverTaskId || !SameRect(oldTaskRect, g_hoverTaskRect)
			|| oldWindowHeaderHover != g_windowHeaderHover
			|| oldWindowPortHover != g_windowPortHoverZone;
	};

	POINT pt = {};
	if (!OverlayGetCursorPos(&pt) || !::PtInRect(&g_chartScreenRect, pt)) {
		return changed();
	}
	if (!::IsRectEmpty(&g_headerBandScreenRect) && ::PtInRect(&g_headerBandScreenRect, pt))
		g_windowHeaderHover = true;
	g_windowPortHoverZone = ShouldShowWindowPorts() ? WindowPortUnderPoint(pt) : HtZone::Outside;

	for (const auto& band : g_rowBands) {
		if (pt.y >= band.screenRect.top && pt.y <= band.screenRect.bottom) {
			if (!::IsRectEmpty(&band.screenRailRect) && !::PtInRect(&band.screenRailRect, pt)) {
				continue;
			}
			g_hoverRowId = band.rowId;
			g_hoverBandRect = band.screenRect;
			break;
		}
	}

	// Task-bar hover. Suppressed for the whole duration of a drag gesture: a
	// highlight that tracked the pointer mid-drag would compete with the drag
	// ghost and force extra paints on the hottest path in the app.
	if (!g_gestureActive && !g_dragActive && g_dragKind == DragKind::None) {
		const HtHit hit = GanttHitTestPoint(g_hitSnapshot, pt.x, pt.y);
		if (hit.zone == HtZone::TaskBody && !hit.id.empty()) {
			// Resolve to the TASK body rect even when the pointer is over the
			// label/progress/pct geometry — those hit-test as TaskBody carrying
			// the task's id, and the bar is the thing we outline.
			for (const auto& item : g_hitSnapshot.items) {
				if (item.kind != HtItemKind::Task || item.id != hit.id) continue;
				g_hoverTaskId = hit.id;
				g_hoverTaskRect = { item.rect.left, item.rect.top, item.rect.right, item.rect.bottom };
				break;
			}
		}
	}

	LayoutHoverInsertHotspot();
	return changed();
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
	if (region.kind == "TASK") {
		if (const PpTask* task = FindTask(doc, region.id)) return task->label;
	}
	if (region.kind == "MILESTONE") {
		if (const PpMilestone* ms = FindMilestone(doc, region.id)) return ms->label;
	}
	if (region.kind == "MARKER") {
		for (const auto& mk : doc.markers) if (mk.id == region.id) return mk.label;
	}
	if (region.kind == "TEXT") {
		if (const PpText* txt = FindTextById(doc, region.id)) return txt->label;
	}
	return "";
}

bool TryOpenInlineRename(const std::string& kind, const std::string& id) {
	for (const auto& region : g_editRegions) {
		if (region.kind == kind && region.id == id) {
			OpenInlineEditor(region);
			return true;
		}
	}
	return false;
}

void ArmDragCommitEcho(DragKind kind, const RECT& anchorScreen, long deltaDays, float pxPerDay,
	const RECT& targetRowScreen, bool textAnchored, float origDx, float origDy,
	float candidateDx, float candidateDy, float pxPerPt) {
	g_dragCommitEcho.active = true;
	g_dragCommitEcho.kind = kind;
	g_dragCommitEcho.anchorScreen = anchorScreen;
	g_dragCommitEcho.deltaDays = deltaDays;
	g_dragCommitEcho.pxPerDay = pxPerDay;
	g_dragCommitEcho.targetRowScreen = targetRowScreen;
	g_dragCommitEcho.textAnchored = textAnchored;
	g_dragCommitEcho.origDx = origDx;
	g_dragCommitEcho.origDy = origDy;
	g_dragCommitEcho.candidateDx = candidateDx;
	g_dragCommitEcho.candidateDy = candidateDy;
	g_dragCommitEcho.pxPerPt = pxPerPt;
	// m7: freeze the explicit window's screen-px extent with the echo so the
	// paint pass can clip the ghost to it (and skip it entirely when the
	// committed result lands fully outside — i.e. becomes hidden).
	g_dragCommitEcho.windowClipValid = false;
	PpDocument cachedDoc;
	if (Gantt_TryPeekCachedDoc(&cachedDoc)
		&& !cachedDoc.windowStart.empty() && !cachedDoc.windowEnd.empty()) {
		ProjPx proj = ProjectionPx();
		if (proj.ok && proj.pxPerDay > 0.0) {
			const long ws = DateToDays(cachedDoc.windowStart);
			const long we = DateToDays(cachedDoc.windowEnd);
			g_dragCommitEcho.windowClipLeftPx =
				(long)::lround(proj.originXpx + (double)(ws - proj.originDay) * proj.pxPerDay);
			g_dragCommitEcho.windowClipRightPx =
				(long)::lround(proj.originXpx + (double)(we - proj.originDay + 1) * proj.pxPerDay);
			g_dragCommitEcho.windowClipValid =
				g_dragCommitEcho.windowClipRightPx > g_dragCommitEcho.windowClipLeftPx;
		}
	}
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

	if (!g_editorUiFont) {
		g_editorUiFont = ::CreateFontW(-Scale(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
	}

	WNDCLASSEXW wc = { sizeof(wc) };
	wc.lpfnWndProc = EditorWndProc;
	wc.hInstance = g_inst;
	wc.hCursor = ::LoadCursor(NULL, IDC_IBEAM);
	wc.hbrBackground = NULL;
	wc.lpszClassName = kEditorClass;
	::RegisterClassExW(&wc);

	g_editorHwnd = ::CreateWindowExW(
		WS_EX_TOOLWINDOW,
		kEditorClass, L"", WS_POPUP,
		0, 0, 100, 24, ::GetAncestor(g_pptHwnd, GA_ROOT), NULL, g_inst, NULL);
	if (!g_editorHwnd) return;

	g_editHwnd = ::CreateWindowExW(
		0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
		0, 0, 100, 24, g_editorHwnd, (HMENU)1, g_inst, NULL);
	if (g_editHwnd) {
		::SendMessageW(g_editHwnd, WM_SETFONT, (WPARAM)g_editorUiFont, TRUE);
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
		::SetWindowPos(g_editorHwnd, HWND_TOP,
			rc.left, rc.top, w, h, SWP_SHOWWINDOW);
		::MoveWindow(g_editHwnd, Scale(2), Scale(2), w - Scale(4), h - Scale(4), TRUE);
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
			PpDocument doc;
			if (ReadGanttDocFromSlide(g_app, &doc)) {
				bool changed = false;
				if (kind == "TITLE") changed = SetTitle(doc, text);
				else if (kind == "MARKER") changed = SetMarkerLabel(doc, id, text);
				else if (kind == "TEXT") changed = SetTextLabel(doc, id, text);
				else changed = SetEntityLabel(doc, id, text);
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

const PpText* FindTextById(const PpDocument& doc, const std::string& id) {
	for (const auto& t : doc.texts) {
		if (t.id == id) return &t;
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
	if (g_cardDateErrorHwnd) {
		::ShowWindow(g_cardDateErrorHwnd, invalid ? SW_SHOW : SW_HIDE);
	}
	if (invalid) ::MessageBeep(MB_ICONWARNING);
}

// Position + size every child control inside the card's client area. Called
// once at open time (LayoutCardControls is idempotent, so it is safe to call
// again if the card were ever resized, though the card is currently
// fixed-size). isMilestone hides the End-date and Percent rows entirely
// (milestones have neither) and shrinks the window accordingly. isText (TEXT
// mode: double-click on a PpText annotation) hides EVERY field except the
// label, plus the 8 color swatches, and shows a Delete button (left-aligned)
// alongside OK (right-aligned) instead — "label field only + delete button"
// per the task spec; OK still commits the (possibly edited) label via
// CommitCardEdit, Delete removes the text outright via CommitCardDelete.
// isText implies isMilestone's Start/End/Percent suppression (a superset), so
// callers pass both rather than this function re-deriving one from the other.
void LayoutCardControls(bool isMilestone, bool isText) {
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
	placeRow(g_cardStartHwnd, !isText);
	placeRow(g_cardEndHwnd, !isMilestone && !isText);
	placeRow(g_cardPercentHwnd, !isMilestone && !isText);

	// Swatch row: 8 small square buttons in a single row (hidden in TEXT mode
	// — PpText has no color field).
	int swatchesW = kCardSwatchCount * swatchSize + (kCardSwatchCount - 1) * swatchGap;
	int sx = pad + std::max(0, (cardW - pad * 2 - swatchesW) / 2);
	for (int i = 0; i < kCardSwatchCount; ++i) {
		if (!g_cardSwatchHwnd[i]) continue;
		::ShowWindow(g_cardSwatchHwnd[i], isText ? SW_HIDE : SW_SHOW);
		if (isText) continue;
		::MoveWindow(g_cardSwatchHwnd[i], sx + i * (swatchSize + swatchGap), y, swatchSize, swatchSize, TRUE);
	}
	if (!isText) y += swatchSize + rowGap;

	// Delete button (TEXT mode only), left-aligned; OK stays right-aligned.
	const int okW = Scale(kBaseCardOkW);
	const int okH = Scale(kBaseCardOkH);
	if (g_cardDeleteHwnd) {
		::ShowWindow(g_cardDeleteHwnd, isText ? SW_SHOW : SW_HIDE);
		if (isText) ::MoveWindow(g_cardDeleteHwnd, pad, y, okW, okH, TRUE);
	}
	if (g_cardOkHwnd) {
		::MoveWindow(g_cardOkHwnd, cardW - pad - okW, y, okW, okH, TRUE);
	}
	if (g_cardDateErrorHwnd) {
		::ShowWindow(g_cardDateErrorHwnd, g_cardInvalid ? SW_SHOW : SW_HIDE);
		if (!isText && !isMilestone) {
			::MoveWindow(g_cardDateErrorHwnd, pad, y - okH - rowGap, cardW - pad * 2, rowH, TRUE);
		}
	}
	y += okH + pad;

	::SetWindowPos(g_cardHwnd, NULL, 0, 0, cardW, y, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Create the card's top-level window + every child control, once. Idempotent
// (like EnsureEditorWindow): a re-open just repositions/repopulates.
void EnsureCardWindow() {
	if (g_cardHwnd) return;

	if (!g_cardUiFont) {
		g_cardUiFont = ::CreateFontW(-Scale(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
	}

	WNDCLASSEXW wc = { sizeof(wc) };
	wc.lpfnWndProc = CardWndProc;
	wc.hInstance = g_inst;
	wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.lpszClassName = kCardClass;
	::RegisterClassExW(&wc);

	g_cardHwnd = ::CreateWindowExW(
		WS_EX_TOOLWINDOW,
		kCardClass, L"", WS_POPUP | WS_CLIPCHILDREN,
		0, 0, Scale(kBaseCardW), 200, ::GetAncestor(g_pptHwnd, GA_ROOT), NULL, g_inst, NULL);
	if (!g_cardHwnd) return;

	auto makeEdit = [&](int id, DWORD extraStyle) {
		HWND h = ::CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | extraStyle,
			0, 0, 10, 10, g_cardHwnd, (HMENU)(INT_PTR)id, g_inst, NULL);
		if (h) {
			::SendMessageW(h, WM_SETFONT, (WPARAM)g_cardUiFont, TRUE);
			g_oldCardFieldProc = (WNDPROC)::SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)CardFieldProc);
		}
		return h;
	};
	g_cardLabelHwnd = makeEdit(CARD_ID_LABEL, 0);
	g_cardStartHwnd = makeEdit(CARD_ID_START, 0);
	g_cardEndHwnd = makeEdit(CARD_ID_END, 0);
	g_cardPercentHwnd = makeEdit(CARD_ID_PERCENT, ES_NUMBER);
#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER (0x1500 + 1) // ECM_FIRST + 1 (commctrl.h not in this TU's include set)
#endif
	::SendMessageW(g_cardStartHwnd, EM_SETCUEBANNER, TRUE, (LPARAM)L"YYYY-MM-DD");
	::SendMessageW(g_cardEndHwnd, EM_SETCUEBANNER, TRUE, (LPARAM)L"YYYY-MM-DD");

	g_cardDateErrorHwnd = ::CreateWindowExW(0, L"STATIC", L"Use YYYY-MM-DD date format",
		WS_CHILD | SS_LEFT, 0, 0, 10, 10, g_cardHwnd, (HMENU)(INT_PTR)CARD_ID_DELETE + 100, g_inst, NULL);
	if (g_cardDateErrorHwnd) {
		::SendMessageW(g_cardDateErrorHwnd, WM_SETFONT, (WPARAM)g_cardUiFont, TRUE);
		::ShowWindow(g_cardDateErrorHwnd, SW_HIDE);
	}

	for (int i = 0; i < kCardSwatchCount; ++i) {
		g_cardSwatchHwnd[i] = ::CreateWindowExW(0, L"BUTTON", L"",
			WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
			0, 0, 10, 10, g_cardHwnd, (HMENU)(INT_PTR)(CARD_ID_SWATCH_BASE + i), g_inst, NULL);
		if (g_cardSwatchHwnd[i]) ::SendMessageW(g_cardSwatchHwnd[i], WM_SETFONT, (WPARAM)g_cardUiFont, TRUE);
	}

	g_cardOkHwnd = ::CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
		0, 0, 10, 10, g_cardHwnd, (HMENU)(INT_PTR)CARD_ID_OK, g_inst, NULL);
	if (g_cardOkHwnd) ::SendMessageW(g_cardOkHwnd, WM_SETFONT, (WPARAM)g_cardUiFont, TRUE);

	// TEXT mode only (label field + this button, no dates/percent/swatches —
	// see LayoutCardControls' isText branch, which hides everything else).
	g_cardDeleteHwnd = ::CreateWindowExW(0, L"BUTTON", L"Delete", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
		0, 0, 10, 10, g_cardHwnd, (HMENU)(INT_PTR)CARD_ID_DELETE, g_inst, NULL);
	if (g_cardDeleteHwnd) ::SendMessageW(g_cardDeleteHwnd, WM_SETFONT, (WPARAM)g_cardUiFont, TRUE);
}

// Destroy the card window + children and clear every bit of card state. Safe
// to call whether or not a card is currently open (mirrors DestroyInlineEditor).
void CloseCardEditor() {
	g_cardClosing = true;
	if (g_cardHwnd) {
		::DestroyWindow(g_cardHwnd);
		g_cardHwnd = NULL;
	}
	g_cardLabelHwnd = g_cardStartHwnd = g_cardEndHwnd = g_cardPercentHwnd = g_cardOkHwnd = g_cardDeleteHwnd = NULL;
	g_cardDateErrorHwnd = NULL;
	for (int i = 0; i < kCardSwatchCount; ++i) g_cardSwatchHwnd[i] = NULL;
	g_oldCardFieldProc = NULL;
	g_cardKind.clear();
	g_cardId.clear();
	g_cardSelectedSwatch = -1;
	g_cardOrigColor.clear();
	g_cardInvalid = false;
	g_cardClosing = false;
	g_cardFocusHwnd = NULL;
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
		bool isMarker = (kind == "MARKER");
		bool isText = (kind == "TEXT");
		std::string label, start, end, color;
		int percent = 0;
		if (isText) {
			const PpText* txt = FindTextById(doc, id);
			if (!txt) return;
			label = txt->label;
		} else if (isMarker) {
			const PpMarker* mk = FindMarker(doc, id);
			if (!mk) return;
			label = mk->label;
			start = mk->date;
			color = mk->color;
		} else if (isMilestone) {
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
			if (AppBarColorEquals(kAppBarSwatches[i], color)) { g_cardSelectedSwatch = i; break; }
		}

		SetEditText(g_cardLabelHwnd, label);
		SetEditText(g_cardStartHwnd, start);
		SetEditText(g_cardEndHwnd, end);
		if (!isMilestone && !isMarker && !isText) SetEditText(g_cardPercentHwnd, std::to_string(percent));

		LayoutCardControls(isMilestone || isMarker, isText);
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

		::SetWindowPos(g_cardHwnd, HWND_TOP,
			x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
		::InvalidateRect(g_cardHwnd, NULL, TRUE);
		::SetForegroundWindow(g_cardHwnd);
		::SetFocus(g_cardLabelHwnd);
		g_cardFocusHwnd = g_cardLabelHwnd;
		::SendMessageW(g_cardLabelHwnd, EM_SETSEL, 0, -1);
	}
	catch (const _com_error& e) {
		OvLog(L"COM error opening card editor");
		RecError("OpenCardEditor", (long)e.Error(), "COM error");
		CloseCardEditor();
	}
	catch (const std::exception& e) {
		OvLog(L"document error opening card editor");
		RecError("OpenCardEditor", (long)E_FAIL, e.what());
		CloseCardEditor();
	}
	catch (...) {
		OvLog(L"unknown error opening card editor");
		RecError("OpenCardEditor", (long)E_FAIL, "unknown error");
		CloseCardEditor();
	}
}

// Validate + commit every field of the open card in ONE undo entry. Returns
// without closing the card if validation fails (invalid dates: parse failure
// or end < start) — the task spec requires the editor to STAY OPEN with a
// red-border/beep cue rather than silently discarding the user's edits. TEXT
// mode has no dates/percent/color to validate — it only ever commits the
// label (via SetTextLabel, not SetEntityLabel, which does not know about
// PpText ids).
void CommitCardEdit() {
	if (!g_cardHwnd || g_cardKind.empty() || g_mutating) return;
	std::string kind = g_cardKind;
	std::string id = g_cardId;
	bool isMilestone = (kind == "MILESTONE");
	bool isMarker = (kind == "MARKER");
	bool isText = (kind == "TEXT");

	std::string label = GetEditText(g_cardLabelHwnd);

	if (isText) {
		SetCardInvalid(false);
		CloseCardEditor();
		g_mutating = true;
		try {
			PpDocument doc;
			if (ReadGanttDocFromSlide(g_app, &doc)) {
				if (SetTextLabel(doc, id, label)) RebuildChart(doc, id);
			}
		}
		catch (const _com_error&) {
			OvLog(L"COM error committing text card edit");
		}
		catch (const std::exception&) {
			OvLog(L"document error committing text card edit");
		}
		catch (...) {
			OvLog(L"unknown error committing text card edit");
		}
		g_mutating = false;
		SetOwnSelection(kind, id);
		FocusPowerPoint();
		return;
	}

	std::string startText = GetEditText(g_cardStartHwnd);
	std::string endText = (isMilestone || isMarker) ? startText : GetEditText(g_cardEndHwnd);
	std::string percentText = (isMilestone || isMarker) ? "" : GetEditText(g_cardPercentHwnd);

	long startDays = 0, endDays = 0;
	if (!ParseIsoDateStrict(startText, startDays)) {
		SetCardInvalid(true);
		return;
	}
	if (!isMilestone && !isMarker) {
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
	if (!isMilestone && !isMarker) {
		try { percent = std::stoi(percentText); } catch (...) { percent = 0; }
		percent = std::max(0, std::min(100, percent));
	}

	std::string color;
	if (g_cardSelectedSwatch >= 0 && g_cardSelectedSwatch < kCardSwatchCount) {
		color = kAppBarSwatches[g_cardSelectedSwatch];
	} else {
		color = g_cardOrigColor; // no swatch clicked: leave color unchanged
	}

	CloseCardEditor();

	g_mutating = true;
	try {
		PpDocument doc;
		if (ReadGanttDocFromSlide(g_app, &doc)) {
			bool changed = false;
			if (isMarker) {
				changed = SetMarkerLabel(doc, id, label) || changed;
				changed = SetMarkerDate(doc, id, DaysToDate(startDays)) || changed;
			} else {
				changed = SetEntityLabel(doc, id, label) || changed;
			}
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
			} else if (!isMarker) {
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

// TEXT mode's Delete button: removes the text outright (mirrors
// HandleHotkeyDelete's TEXT branch) and closes the card — there is nothing
// left to keep editing once the text itself is gone.
void CommitCardDelete() {
	if (!g_cardHwnd || g_cardKind != "TEXT" || g_mutating) return;
	std::string id = g_cardId;
	CloseCardEditor();

	g_mutating = true;
	try {
		PpDocument doc;
		if (ReadGanttDocFromSlide(g_app, &doc)) {
			if (DeleteById(doc, id)) {
				RebuildChart(doc, "");
				ClearOwnSelection();
				RequestOverlayRepaint();
			}
		}
	}
	catch (const _com_error&) {
		OvLog(L"COM error committing card delete");
	}
	catch (const std::exception&) {
		OvLog(L"document error committing card delete");
	}
	catch (...) {
		OvLog(L"unknown error committing card delete");
	}
	g_mutating = false;
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
		// The DISPID for "StartNewUndoEntry" is stable for a given Application
		// object, so resolve it once per app instance and reuse it — saving a
		// GetIDsOfNames COM round-trip on every op dispatch (SR-SMO-02). Re-resolve
		// if the cached app pointer changes (host restart / new automation object).
		static IDispatch* s_undoDispApp = nullptr;
		static DISPID s_undoDispId = 0;
		DISPID dispid = s_undoDispId;
		if (appDisp != s_undoDispApp || s_undoDispId == 0) {
			LPOLESTR name = const_cast<LPOLESTR>(L"StartNewUndoEntry");
			HRESULT hrIds = appDisp->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &dispid);
			if (FAILED(hrIds)) {
				wchar_t buf[80];
				::swprintf_s(buf, 80, L"StartNewUndoEntry: GetIDsOfNames failed hr=0x%08lX", (unsigned long)hrIds);
				OvLog(buf);
				return;
			}
			s_undoDispApp = appDisp;
			s_undoDispId = dispid;
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
	// Keep the log handle open for the process lifetime: per-call
	// CreateFile/CloseHandle on a %TEMP% file costs ms and triggers AV scan
	// spikes inside the measured op-dispatch window (SR-SMO-02). FILE_APPEND_DATA
	// keeps multi-process appends atomic; the OS closes the handle at exit.
	static HANDLE s_log = INVALID_HANDLE_VALUE;
	if (s_log == INVALID_HANDLE_VALUE) {
		wchar_t path[MAX_PATH];
		DWORD n = ::GetTempPathW(MAX_PATH, path);
		if (!n || n > MAX_PATH) return;
		::wcscat_s(path, MAX_PATH, L"powerplanner-addin.log");
		s_log = ::CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (s_log == INVALID_HANDLE_VALUE) return;
	}
	wchar_t pidBuf[48];
	::swprintf_s(pidBuf, 48, L"[overlay %lu @%lu] ", ::GetCurrentProcessId(), ::GetTickCount());
	std::wstring line = std::wstring(pidBuf) + msg + L"\r\n";
	DWORD w = 0; ::WriteFile(s_log, line.c_str(), (DWORD)(line.size() * sizeof(wchar_t)), &w, NULL);
}

// ---- session recorder writer (R1b-core) ------------------------------------
// Structured JSONL sink held open like OvLog, but NEVER writes to
// powerplanner-addin.log. fflush every event for crash-robustness (v1).

void RecEvent(const char* type, const std::string& payloadBody) {
	if (!g_recActive || !g_recEventsFile || !type) return;
	const ULONGLONG t = ::GetTickCount64() - g_recT0;
	++g_recSeq;
	std::string line;
	line.reserve(payloadBody.size() + 80);
	line += "{\"t\":";
	line += std::to_string((unsigned long long)t);
	line += ",\"seq\":";
	line += std::to_string(g_recSeq);
	line += ",\"type\":\"";
	line += type;
	line += "\"";
	if (!payloadBody.empty()) {
		line += ",";
		line += payloadBody;
	}
	line += "}\n";
	::fwrite(line.data(), 1, line.size(), g_recEventsFile);
	// High-frequency input/gesture-update/paint rows stay buffered. Flush all
	// state transitions immediately and bound streaming loss to 250 ms, avoiding
	// dozens of stdio flushes per second during direct manipulation (SR-REC-15).
	const ULONGLONG now = ::GetTickCount64();
	const bool urgent = ::strcmp(type, "error") == 0
		|| ::strcmp(type, "snapshot") == 0 || ::strcmp(type, "doc") == 0
		|| ::strcmp(type, "frame") == 0 || ::strcmp(type, "nativeSel") == 0
		|| ::strcmp(type, "ownSel") == 0 || ::strcmp(type, "op") == 0
		|| (::strcmp(type, "gesture") == 0
			&& payloadBody.find("\"phase\":\"update\"") == std::string::npos);
	if (urgent || g_recLastFlushMs == 0 || now - g_recLastFlushMs >= 250) {
		::fflush(g_recEventsFile);
		g_recLastFlushMs = now;
	}
}

void RecError(const char* where, long hr, const char* msg) {
	if (!g_recActive) return;
	std::string body;
	body.reserve(128);
	body += "\"where\":\"";
	EntityJsonAppendEscaped(body, where ? where : "");
	body += "\",\"hr\":";
	body += std::to_string(hr);
	body += ",\"msg\":\"";
	EntityJsonAppendEscaped(body, msg ? msg : "");
	body += "\"";
	RecEvent("error", body);
}

void RecRefreshTaskEntityGeometry() {
	const ULONGLONG now = ::GetTickCount64();
	if (!g_app || g_dragKind != DragKind::None || g_recTaskEntityBindings.empty()
		|| (g_recLastTaskGeometryRefreshMs != 0 && now - g_recLastTaskGeometryRefreshMs < 250))
		return;
	g_recLastTaskGeometryRefreshMs = now;
	try {
		PowerPoint::DocumentWindowPtr win = g_app->GetActiveWindow();
		if (!win) return;
		for (const RecTaskEntityBinding& binding : g_recTaskEntityBindings) {
			if (!binding.shape || binding.entityIndex >= g_entityCache.size()) continue;
			try {
				const float left = binding.shape->GetLeft();
				const float top = binding.shape->GetTop();
				const float width = binding.shape->GetWidth();
				const float height = binding.shape->GetHeight();
				RECT rr = {
					win->PointsToScreenPixelsX(left),
					win->PointsToScreenPixelsY(top),
					win->PointsToScreenPixelsX(left + width),
					win->PointsToScreenPixelsY(top + height)
				};
				NormalizeRect(rr);
				PpEntity& ent = g_entityCache[binding.entityIndex];
				ent.slideRect = { (double)left, (double)top, (double)width, (double)height };
				ent.screenRect = {
					(double)rr.left, (double)rr.top,
					(double)(rr.right - rr.left), (double)(rr.bottom - rr.top)
				};
			} catch (const _com_error&) {
				// A rebuild can invalidate a cached child between ticks. The next
				// BuildRowBands refresh replaces all bindings.
			}
		}
	} catch (const _com_error&) {
		// No active window while PowerPoint is transitioning; keep last geometry.
	}
}

void RecEmitSnapshot() {
	if (!g_recActive) return;
	// Cached task-component handles make independent native child movement
	// visible without a full chart COM walk on every recorder snapshot.
	RecRefreshTaskEntityGeometry();
	// chrome = full DumpChromeState JSON object; entities = array or null when
	// scene signature unchanged since last snapshot (SR-REC-11 / SR-ENT-06).
	const char* chrome = Overlay_DumpChromeStateForTest();
	std::string body;
	body.reserve(4096);
	body += "\"chrome\":";
	body += chrome ? chrome : "{}";
	body += ",\"entities\":";
	const char* entFullRaw = Overlay_DumpEntitiesForTest();
	std::string entityJson = entFullRaw ? entFullRaw : "{\"entities\":[]}";
	if (entityJson == "{\"entities\":[]}" && !g_recLastNonEmptyEntityJson.empty()) {
		// RebuildChart invalidates the overlay cache before the next Tick repopulates
		// it. A chart with a valid CHART_ROOT cannot have zero scene prims, so keep
		// the prior graph rather than recording a false full-scene removal.
		entityJson = g_recLastNonEmptyEntityJson;
	} else if (entityJson != "{\"entities\":[]}") {
		g_recLastNonEmptyEntityJson = entityJson;
	}
	const char* entFull = entityJson.c_str();
	const bool dedupe = !g_recLastSnapEntitySig.empty()
		&& g_recLastSnapEntitySig == entityJson;
	if (dedupe) {
		body += "null";
	} else {
		if (entFull && entFull[0] == '{') {
			const char* arr = ::strchr(entFull, '[');
			if (arr) {
				const size_t n = ::strlen(arr);
				// Drop the wrapping object's final '}' so we keep only the array.
				if (n >= 2 && arr[n - 1] == '}')
					body.append(arr, n - 1);
				else
					body += arr;
			} else {
				body += "null";
			}
		} else {
			body += "null";
		}
		g_recLastSnapEntitySig = entityJson;
	}
	RecEvent("snapshot", body);
}

void RecEmitDoc() {
	if (!g_recActive) return;
	int taskCount = 0, rowCount = 0, milestoneCount = 0;
	std::string docDatesSignature;
	PpDocument cached;
	if (Gantt_TryPeekCachedDoc(&cached)) {
		taskCount = (int)cached.tasks.size();
		rowCount = (int)cached.rows.size();
		milestoneCount = (int)cached.milestones.size();
		for (const auto& task : cached.tasks)
			docDatesSignature += task.id + ":" + task.start + ":" + task.end + ";";
		for (const auto& ms : cached.milestones)
			docDatesSignature += ms.id + ":" + ms.date + ";";
	} else {
		rowCount = (int)g_rowBands.size();
	}
	std::string body;
	body.reserve(64 + docDatesSignature.size());
	body += "\"taskCount\":";
	body += std::to_string(taskCount);
	body += ",\"rowCount\":";
	body += std::to_string(rowCount);
	body += ",\"milestoneCount\":";
	body += std::to_string(milestoneCount);
	body += ",\"docDatesSignature\":\"";
	EntityJsonAppendEscaped(body, docDatesSignature);
	body += "\"";
	RecEvent("doc", body);
}

// ---- R1b taps: string tables, input/gesture/paint/frame emitters -----------

static const char* RecDragKindName(DragKind k) {
	switch (k) {
	case DragKind::TaskBody: return "TaskBody";
	case DragKind::TaskEdgeL: return "TaskEdgeL";
	case DragKind::TaskEdgeR: return "TaskEdgeR";
	case DragKind::TaskProgress: return "TaskProgress";
	case DragKind::Milestone: return "Milestone";
	case DragKind::Create: return "Create";
	case DragKind::Marker: return "Marker";
	case DragKind::Text: return "Text";
	case DragKind::LinkPort: return "LinkPort";
	case DragKind::WindowEdgeL: return "WindowEdgeL";
	case DragKind::WindowEdgeR: return "WindowEdgeR";
	default: return "None";
	}
}

static const char* RecHtZoneName(HtZone z) {
	switch (z) {
	case HtZone::Outside: return "Outside";
	case HtZone::WindowPortL: return "WindowPortL";
	case HtZone::WindowPortR: return "WindowPortR";
	case HtZone::TaskBody: return "TaskBody";
	case HtZone::TaskEdgeL: return "TaskEdgeL";
	case HtZone::TaskEdgeR: return "TaskEdgeR";
	case HtZone::TaskProgressEdge: return "TaskProgressEdge";
	case HtZone::Milestone: return "Milestone";
	case HtZone::Label: return "Label";
	case HtZone::Marker: return "Marker";
	case HtZone::Text: return "Text";
	case HtZone::Dependency: return "Dependency";
	case HtZone::RowBand: return "RowBand";
	case HtZone::EmptyCell: return "EmptyCell";
	default: return "Outside";
	}
}

static const char* RecHtItemKindName(HtItemKind k) {
	switch (k) {
	case HtItemKind::Task: return "TASK";
	case HtItemKind::TaskLabel: return "TASK_LABEL";
	case HtItemKind::Milestone: return "MILESTONE";
	case HtItemKind::RowLabel: return "ROW_LABEL";
	case HtItemKind::Title: return "TITLE";
	case HtItemKind::Marker: return "MARKER";
	case HtItemKind::Text: return "TEXT";
	case HtItemKind::Dependency: return "DEP";
	default: return "";
	}
}

static const char* RecMsgName(UINT msg) {
	switch (msg) {
	case WM_LBUTTONDOWN: return "WM_LBUTTONDOWN";
	case WM_LBUTTONUP: return "WM_LBUTTONUP";
	case WM_RBUTTONDOWN: return "WM_RBUTTONDOWN";
	case WM_RBUTTONUP: return "WM_RBUTTONUP";
	case WM_LBUTTONDBLCLK: return "WM_LBUTTONDBLCLK";
	case WM_MOUSEMOVE: return "WM_MOUSEMOVE";
	case WM_HOTKEY: return "WM_HOTKEY";
	default: return "WM_OTHER";
	}
}

static std::string RecModsJson(WPARAM mk) {
	// Prefer message key-state bits when present; fall back to GetAsyncKeyState
	// so HOTKEY / synthetic posts still get a useful mods string. Async, not
	// GetKeyState: this string is recorder ground truth, and GetKeyState only
	// reflects state as of the last message dequeued by this thread.
	const bool ctrl = ((mk & MK_CONTROL) != 0) || ((::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
	const bool shift = ((mk & MK_SHIFT) != 0) || ((::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
	const bool alt = ((::GetAsyncKeyState(VK_MENU) & 0x8000) != 0);
	std::string m = "[";
	bool first = true;
	auto append = [&](const char* name) {
		if (!first) m += ',';
		m += '\"'; m += name; m += '\"'; first = false;
	};
	if (ctrl) append("ctrl");
	if (shift) append("shift");
	if (alt) append("alt");
	m += ']';
	return m;
}

static std::string RecAppBarCmdLabel(int cmd) {
	if (cmd == 0) return {};
	for (const auto& group : g_appBarLayout.groups) {
		for (const auto& item : group.items) {
			if (item.cmd == cmd) return item.label;
		}
	}
	return {};
}

static int RecPngEncoderClsid(CLSID* clsid) {
	UINT num = 0, size = 0;
	Gdiplus::GetImageEncodersSize(&num, &size);
	if (size == 0) return -1;
	std::vector<BYTE> buf(size);
	auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
	Gdiplus::GetImageEncoders(num, size, codecs);
	for (UINT i = 0; i < num; ++i) {
		if (::wcscmp(codecs[i].MimeType, L"image/png") == 0) {
			*clsid = codecs[i].Clsid;
			return (int)i;
		}
	}
	return -1;
}

// Port of appbar-shot CaptureWindowToPng (PrintWindow + GDI+ PNG). Failures
// never throw; callers emit RecError. Layered ULW windows need PW_RENDERFULLCONTENT.
static bool RecCaptureWindowToPng(HWND hwnd, const wchar_t* path) {
	if (!hwnd || !::IsWindow(hwnd) || !path || !*path) return false;
	RECT r{};
	if (!::GetWindowRect(hwnd, &r)) return false;
	const int w = r.right - r.left, h = r.bottom - r.top;
	if (w <= 0 || h <= 0) return false;
	HDC screen = ::GetDC(NULL);
	if (!screen) return false;
	HDC mem = ::CreateCompatibleDC(screen);
	HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
	if (!mem || !bmp) {
		if (bmp) ::DeleteObject(bmp);
		if (mem) ::DeleteDC(mem);
		::ReleaseDC(NULL, screen);
		return false;
	}
	HGDIOBJ old = ::SelectObject(mem, bmp);
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
	const BOOL printed = ::PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT);
	::SelectObject(mem, old);
	bool ok = false;
	if (printed) {
		Gdiplus::Bitmap bitmap(bmp, NULL);
		CLSID pngClsid;
		if (RecPngEncoderClsid(&pngClsid) >= 0)
			ok = (bitmap.Save(path, &pngClsid, NULL) == Gdiplus::Ok);
	}
	::DeleteObject(bmp);
	::DeleteDC(mem);
	::ReleaseDC(NULL, screen);
	return ok;
}

static bool RecCaptureScreenRectToPng(const RECT& rect, const wchar_t* path) {
	if (!path || !*path) return false;
	const int w = rect.right - rect.left, h = rect.bottom - rect.top;
	if (w <= 0 || h <= 0) return false;
	HDC screen = ::GetDC(NULL);
	if (!screen) return false;
	HDC mem = ::CreateCompatibleDC(screen);
	HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
	if (!mem || !bmp) {
		if (bmp) ::DeleteObject(bmp);
		if (mem) ::DeleteDC(mem);
		::ReleaseDC(NULL, screen);
		return false;
	}
	HGDIOBJ old = ::SelectObject(mem, bmp);
	const BOOL copied = ::BitBlt(mem, 0, 0, w, h, screen, rect.left, rect.top,
		SRCCOPY | CAPTUREBLT);
	::SelectObject(mem, old);
	bool ok = false;
	if (copied) {
		Gdiplus::Bitmap bitmap(bmp, NULL);
		CLSID pngClsid;
		if (RecPngEncoderClsid(&pngClsid) >= 0)
			ok = (bitmap.Save(path, &pngClsid, NULL) == Gdiplus::Ok);
	}
	::DeleteObject(bmp);
	::DeleteDC(mem);
	::ReleaseDC(NULL, screen);
	return ok;
}

void RecEmitInput(const char* surface, UINT msg, HWND hwnd, WPARAM wp, LPARAM lp) {
	if (!g_recActive || !surface) return;
	// Only the SR-REC-05 message set; everything else is a no-op (cheap).
	switch (msg) {
	case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP:
	case WM_LBUTTONDBLCLK: case WM_MOUSEMOVE: case WM_HOTKEY:
		break;
	default:
		return;
	}

	POINT client = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
	POINT screen = client;
	if (msg == WM_HOTKEY) {
		// HOTKEY has no point payload — use current cursor (override-aware).
		POINT cur{};
		if (OverlayGetCursorPos(&cur)) {
			screen = cur;
			client = cur;
			if (hwnd) ::ScreenToClient(hwnd, &client);
		} else {
			screen = {};
			client = {};
		}
	} else if (hwnd) {
		::ClientToScreen(hwnd, &screen);
	}

	// Hit annotation (overlay: semantic hit-test; appbar: button cmd if cheap).
	std::string hitZone, hitKind, hitId;
	bool haveHit = false;
	if (::strcmp(surface, "overlay") == 0 && msg != WM_HOTKEY) {
		const HtHit hit = HitTestClientPoint(client);
		hitZone = RecHtZoneName(hit.zone);
		hitKind = RecHtItemKindName(hit.kind);
		hitId = hit.id.empty() ? hit.rowId : hit.id;
		haveHit = true;
	} else if (::strcmp(surface, "appbar") == 0 && msg != WM_HOTKEY) {
		int cmd = 0;
		for (const auto& h : g_appBarHits) {
			if (h.enabled && ::PtInRect(&h.rc, client)) { cmd = h.cmd; break; }
		}
		if (cmd != 0) {
			hitZone = "button";
			hitKind = "cmd";
			hitId = std::to_string(cmd);
			haveHit = true;
		}
	}

	if (msg == WM_MOUSEMOVE) {
		std::string hitKey;
		if (haveHit) {
			hitKey.reserve(hitZone.size() + hitKind.size() + hitId.size() + 2);
			hitKey += hitZone; hitKey += '|'; hitKey += hitKind; hitKey += '|'; hitKey += hitId;
		}
		const ULONGLONG now = ::GetTickCount64();
		const bool hitChanged = (hitKey != g_recLastInputHitKey);
		if (!hitChanged && (now - g_recLastInputMoveMs) < 50)
			return; // ~20 Hz + always-on-hit-change (SR-REC-05)
		g_recLastInputMoveMs = now;
		g_recLastInputHitKey = hitKey;
	}

	std::string body;
	body.reserve(160 + hitZone.size() + hitKind.size() + hitId.size());
	body += "\"surface\":\"";
	body += surface;
	body += "\",\"msg\":\"";
	body += RecMsgName(msg);
	body += "\",\"pt\":[";
	body += std::to_string(screen.x);
	body += ",";
	body += std::to_string(screen.y);
	body += "],\"client\":[";
	body += std::to_string(client.x);
	body += ",";
	body += std::to_string(client.y);
	body += "],\"mods\":";
	body += RecModsJson(wp);
	if (msg == WM_HOTKEY) {
		body += ",\"hotkeyId\":";
		body += std::to_string((int)wp);
	}
	if (haveHit) {
		body += ",\"hit\":{\"zone\":\"";
		EntityJsonAppendEscaped(body, hitZone);
		body += "\",\"kind\":\"";
		EntityJsonAppendEscaped(body, hitKind);
		body += "\",\"id\":\"";
		EntityJsonAppendEscaped(body, hitId);
		body += "\"}";
	}
	RecEvent("input", body);
}

void RecEmitGestureStart(const char* kind, const std::string& id, const std::string& rowId, POINT anchorClient) {
	if (!g_recActive || !kind) return;
	g_recLastGestureUpdateMs = 0;
	++g_recGestureSeq;
	g_recCurGestureId = g_recGestureSeq;
	std::string body;
	body.reserve(112 + id.size() + rowId.size());
	body += "\"phase\":\"start\",\"g\":";
	body += std::to_string(g_recCurGestureId);
	body += ",\"kind\":\"";
	EntityJsonAppendEscaped(body, kind);
	body += "\",\"id\":\"";
	EntityJsonAppendEscaped(body, id);
	body += "\",\"rowId\":\"";
	EntityJsonAppendEscaped(body, rowId);
	body += "\",\"anchor\":[";
	body += std::to_string(anchorClient.x);
	body += ",";
	body += std::to_string(anchorClient.y);
	body += "]";
	RecEvent("gesture", body);
	RecEmitSnapshot();
}

void RecEmitGestureUpdate() {
	if (!g_recActive || !g_gestureActive) return;
	const ULONGLONG now = ::GetTickCount64();
	if (g_recLastGestureUpdateMs != 0 && (now - g_recLastGestureUpdateMs) < 100)
		return; // 10 Hz max (SR-REC-08 update throttle)
	g_recLastGestureUpdateMs = now;

	std::string body;
	body.reserve(176);
	body += "\"phase\":\"update\",\"g\":";
	body += std::to_string(g_recCurGestureId);
	body += ",\"kind\":\"";
	EntityJsonAppendEscaped(body, RecDragKindName(g_dragKind));
	body += "\",\"id\":\"";
	EntityJsonAppendEscaped(body, g_dragId.empty() ? g_createRowId : g_dragId);
	body += "\"";
	if (g_dragKind == DragKind::Create) {
		body += ",\"rowId\":\"";
		EntityJsonAppendEscaped(body, g_createRowId);
		body += "\",\"anchorDay\":";
		body += std::to_string(g_createAnchorDay);
		body += ",\"currentDay\":";
		body += std::to_string(g_createCurrentDay);
	} else if (g_dragKind == DragKind::TaskProgress) {
		body += ",\"percent\":";
		body += std::to_string(g_dragCandidatePercent);
	} else if (IsWindowEdgeDragKind(g_dragKind)) {
		body += ",\"candidateStart\":\"";
		EntityJsonAppendEscaped(body, g_windowCandidateStart);
		body += "\",\"candidateEnd\":\"";
		EntityJsonAppendEscaped(body, g_windowCandidateEnd);
		body += "\"";
	} else {
		body += ",\"deltaDays\":";
		body += std::to_string(g_dragDeltaDays);
		if (!g_dragTargetRowId.empty()) {
			body += ",\"rowId\":\"";
			EntityJsonAppendEscaped(body, g_dragTargetRowId);
			body += "\"";
		}
		if (!g_dragOrigStart.empty()) {
			std::string cs, ce;
			ComputeDragCandidateDates(g_dragKind, g_dragOrigStart, g_dragOrigEnd, g_dragDeltaDays, cs, ce);
			body += ",\"candidateStart\":\"";
			EntityJsonAppendEscaped(body, cs);
			body += "\",\"candidateEnd\":\"";
			EntityJsonAppendEscaped(body, ce);
			body += "\"";
		}
	}
	RecEvent("gesture", body);
}

void RecEmitGestureCommit(const char* kind, const std::string& id, const char* result, long hr,
	const std::string& payloadExtra) {
	if (!g_recActive || !kind) return;
	std::string body;
	body.reserve(112 + id.size() + payloadExtra.size());
	body += "\"phase\":\"commit\",\"g\":";
	body += std::to_string(g_recCurGestureId);
	body += ",\"kind\":\"";
	EntityJsonAppendEscaped(body, kind);
	body += "\",\"id\":\"";
	EntityJsonAppendEscaped(body, id);
	body += "\",\"result\":\"";
	body += (result && *result) ? result : "ok";
	body += "\",\"hr\":";
	body += std::to_string(hr);
	if (!payloadExtra.empty()) {
		body += ",";
		body += payloadExtra;
	}
	RecEvent("gesture", body);
	RecEmitSnapshot();
	RecEmitDoc();
	RecCaptureFrames("commit");
}

void RecEmitGestureCancel(const char* kind, const std::string& id) {
	if (!g_recActive || !kind) return;
	std::string body;
	body.reserve(80 + id.size());
	body += "\"phase\":\"cancel\",\"g\":";
	body += std::to_string(g_recCurGestureId);
	body += ",\"kind\":\"";
	EntityJsonAppendEscaped(body, kind);
	body += "\",\"id\":\"";
	EntityJsonAppendEscaped(body, id);
	body += "\"";
	RecEvent("gesture", body);
	RecEmitSnapshot();
}

void RecEmitPaint(const char* surface, long count) {
	if (!g_recActive || !surface) return;
	const ULONGLONG now = ::GetTickCount64();
	ULONGLONG* last = nullptr;
	if (::strcmp(surface, "overlay") == 0) last = &g_recLastPaintOverlayMs;
	else if (::strcmp(surface, "appbar") == 0) last = &g_recLastPaintAppBarMs;
	else return;
	if (*last != 0 && (now - *last) < 250) return; // max 1/250ms (SR-REC-10)
	*last = now;
	std::string body;
	body.reserve(64);
	body += "\"surface\":\"";
	body += surface;
	body += "\",\"count\":";
	body += std::to_string(count);
	body += ",\"tMs\":";
	body += std::to_string((unsigned long long)(now - g_recT0));
	RecEvent("paint", body);
}

void RecCaptureFrames(const char* trigger) {
	if (!g_recActive || !trigger || g_recSessionDir[0] == L'\0') return;
	const ULONGLONG now = ::GetTickCount64();
	g_recPendingFrameTrigger = trigger;
	g_recPendingFrameDueMs = now + 75; // next settled paint, not pre-transition pixels
	if (g_recLastFrameMs != 0 && g_recPendingFrameDueMs < g_recLastFrameMs + 500)
		g_recPendingFrameDueMs = g_recLastFrameMs + 500;
}

void RecCapturePendingFrame() {
	if (!g_recActive || g_recPendingFrameTrigger.empty()) return;
	const ULONGLONG now = ::GetTickCount64();
	if (now < g_recPendingFrameDueMs) return;
	if (g_hwnd && ::IsWindow(g_hwnd)) ::UpdateWindow(g_hwnd);
	if (g_appBarHwnd && ::IsWindow(g_appBarHwnd)) ::UpdateWindow(g_appBarHwnd);

	RECT capture = g_chartScreenRect;
	if (::IsRectEmpty(&capture) && g_hwnd) ::GetWindowRect(g_hwnd, &capture);
	RECT appBar{};
	if (g_appBarHwnd && ::IsWindowVisible(g_appBarHwnd) && ::GetWindowRect(g_appBarHwnd, &appBar)) {
		capture.left = (std::min)(capture.left, appBar.left);
		capture.top = (std::min)(capture.top, appBar.top);
		capture.right = (std::max)(capture.right, appBar.right);
		capture.bottom = (std::max)(capture.bottom, appBar.bottom);
	}
	::InflateRect(&capture, 8, 8);
	const int vx = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
	const int vy = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
	const int vr = vx + ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
	const int vb = vy + ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
	capture.left = (std::max)(capture.left, (LONG)vx);
	capture.top = (std::max)(capture.top, (LONG)vy);
	capture.right = (std::min)(capture.right, (LONG)vr);
	capture.bottom = (std::min)(capture.bottom, (LONG)vb);

	++g_recFrameSeq;
	wchar_t absPath[MAX_PATH];
	::swprintf_s(absPath, MAX_PATH, L"%s\\frames\\%04llu-%hs.png", g_recSessionDir,
		(unsigned long long)g_recFrameSeq, g_recPendingFrameTrigger.c_str());
	bool ok = false;
	try { ok = RecCaptureScreenRectToPng(capture, absPath); }
	catch (...) { ok = false; }
	if (!ok) {
		RecError("RecCaptureFrames/composited", (long)E_FAIL,
			"screen composition PNG capture failed");
	} else {
		char rel[96];
		::snprintf(rel, sizeof(rel), "frames/%04llu-%s.png",
			(unsigned long long)g_recFrameSeq, g_recPendingFrameTrigger.c_str());
		std::string body = "\"file\":\"";
		EntityJsonAppendEscaped(body, rel);
		body += "\",\"surface\":\"composited\",\"trigger\":\"";
		EntityJsonAppendEscaped(body, g_recPendingFrameTrigger);
		body += "\",\"screenRect\":[" + std::to_string(capture.left) + "," +
			std::to_string(capture.top) + "," + std::to_string(capture.right) + "," +
			std::to_string(capture.bottom) + "]";
		RecEvent("frame", body);
		g_recLastFrameMs = now;
	}
	g_recPendingFrameTrigger.clear();
	g_recPendingFrameDueMs = 0;
}

void SessionRecordStop() {
	const bool wasActive = g_recActive;
	if (g_recEventsFile) {
		::fflush(g_recEventsFile);
		::fclose(g_recEventsFile);
		g_recEventsFile = nullptr;
	}
	g_recActive = false;
	g_recSeq = 0;
	g_recT0 = 0;
	g_recLastFlushMs = 0;
	g_recLastSnapEntitySig.clear();
	g_recLastNonEmptyEntityJson.clear();
	g_recLastNativeKey.clear();
	g_recLastInputMoveMs = 0;
	g_recLastInputHitKey.clear();
	g_recLastGestureUpdateMs = 0;
	g_recLastPaintOverlayMs = 0;
	g_recLastPaintAppBarMs = 0;
	g_recLastFrameMs = 0;
	g_recLastIdleSnapshotMs = 0;
	g_recFrameSeq = 0;
	g_recPendingFrameTrigger.clear();
	g_recPendingFrameDueMs = 0;
	g_recSkipGestureCancel = false;
	if (g_recActiveMarkerPath[0]) {
		::DeleteFileW(g_recActiveMarkerPath);
		g_recActiveMarkerPath[0] = L'\0';
	}
	if (wasActive && g_recStateChanged)
		g_recStateChanged(false, g_recStateChangedContext);
	// Keep g_recSessionDir so Overlay_GetSessionDirForTest can report the last session.
}

void SessionRecordStart() {
	if (g_recActive) SessionRecordStop();

	SYSTEMTIME local{};
	::GetLocalTime(&local);
	wchar_t stamp[40];
	::swprintf_s(stamp, 40, L"%04u%02u%02u-%02u%02u%02u-%03u-%lu",
		(unsigned)local.wYear, (unsigned)local.wMonth, (unsigned)local.wDay,
		(unsigned)local.wHour, (unsigned)local.wMinute, (unsigned)local.wSecond,
		(unsigned)local.wMilliseconds, ::GetCurrentProcessId());

	wchar_t sessionsRoot[MAX_PATH] = {};
	{
		std::filesystem::path root;
		wchar_t overridePath[MAX_PATH] = {};
		DWORD overrideLen = ::GetEnvironmentVariableW(
			L"POWERPLANNER_RECORDS_DIR", overridePath, MAX_PATH);
		if (overrideLen > 0 && overrideLen < MAX_PATH) {
			root = overridePath;
		} else {
			HMODULE module = NULL;
			wchar_t modulePath[MAX_PATH] = {};
			if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&SessionRecordStart), &module) &&
				::GetModuleFileNameW(module, modulePath, MAX_PATH)) {
				std::filesystem::path moduleDir = std::filesystem::path(modulePath).parent_path();
				if (_wcsicmp(moduleDir.filename().c_str(), L"build") == 0)
					root = moduleDir.parent_path() / L"records";
			}
			if (root.empty()) {
				wchar_t localAppData[MAX_PATH] = {};
				DWORD localLen = ::GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
				if (localLen > 0 && localLen < MAX_PATH)
					root = std::filesystem::path(localAppData) / L"PowerPlanner" / L"records";
			}
		}
		if (root.empty()) return;
		std::error_code ec;
		std::filesystem::create_directories(root, ec);
		if (ec || root.native().size() >= MAX_PATH) return;
		::wcscpy_s(sessionsRoot, root.c_str());
	}

	::swprintf_s(g_recSessionDir, MAX_PATH, L"%s\\%s", sessionsRoot, stamp);
	if (!::CreateDirectoryW(g_recSessionDir, NULL) && ::GetLastError() != ERROR_ALREADY_EXISTS) {
		g_recSessionDir[0] = L'\0';
		return;
	}
	wchar_t framesDir[MAX_PATH];
	::swprintf_s(framesDir, MAX_PATH, L"%s\\frames", g_recSessionDir);
	::CreateDirectoryW(framesDir, NULL);

	// meta.json — host context for agents (SR-REC-03).
	std::string pptxName;
	std::string chartId;
	if (g_app) {
		try {
			PowerPoint::_PresentationPtr pres = g_app->GetActivePresentation();
			if (pres) {
				_bstr_t name = pres->GetName();
				if (name.length()) pptxName = Narrow((const wchar_t*)name);
			}
			PowerPoint::DocumentWindowPtr win = g_app->GetActiveWindow();
			PowerPoint::_SlidePtr slide = win ? win->GetView()->GetSlide() : nullptr;
			PowerPoint::ShapePtr root = FindChartRoot(slide);
			if (root) chartId = std::to_string(root->GetId());
		} catch (...) {}
	}
	SYSTEMTIME utc{};
	::GetSystemTime(&utc);
	char isoStart[40];
	::snprintf(isoStart, sizeof(isoStart), "%04u-%02u-%02uT%02u:%02u:%02uZ",
		(unsigned)utc.wYear, (unsigned)utc.wMonth, (unsigned)utc.wDay,
		(unsigned)utc.wHour, (unsigned)utc.wMinute, (unsigned)utc.wSecond);
	const int screenW = ::GetSystemMetrics(SM_CXSCREEN);
	const int screenH = ::GetSystemMetrics(SM_CYSCREEN);

	std::string meta;
	meta.reserve(512);
	meta += "{\n";
	meta += "  \"dllVersion\":\"2.10.0-dev\",\n";
	meta += "  \"build\":\"" __DATE__ " " __TIME__ "\",\n";
	meta += "  \"pptxName\":\"";
	EntityJsonAppendEscaped(meta, pptxName);
	meta += "\",\n";
	meta += "  \"chartId\":\"";
	EntityJsonAppendEscaped(meta, chartId);
	meta += "\",\n";
	meta += "  \"startTime\":\"";
	meta += isoStart;
	meta += "\",\n";
	meta += "  \"screen\":{\"w\":";
	meta += std::to_string(screenW);
	meta += ",\"h\":";
	meta += std::to_string(screenH);
	meta += ",\"dpi\":";
	meta += std::to_string(g_dpi);
	meta += "},\n";
	meta += "  \"chartRect\":{\"left\":";
	meta += std::to_string(g_chartScreenRect.left);
	meta += ",\"top\":";
	meta += std::to_string(g_chartScreenRect.top);
	meta += ",\"right\":";
	meta += std::to_string(g_chartScreenRect.right);
	meta += ",\"bottom\":";
	meta += std::to_string(g_chartScreenRect.bottom);
	meta += "}\n";
	meta += "}\n";

	wchar_t metaPath[MAX_PATH];
	::swprintf_s(metaPath, MAX_PATH, L"%s\\meta.json", g_recSessionDir);
	FILE* metaFile = nullptr;
	if (::_wfopen_s(&metaFile, metaPath, L"wb") == 0 && metaFile) {
		::fwrite(meta.data(), 1, meta.size(), metaFile);
		::fclose(metaFile);
	}

	wchar_t eventsPath[MAX_PATH];
	::swprintf_s(eventsPath, MAX_PATH, L"%s\\events.jsonl", g_recSessionDir);
	FILE* eventsFile = nullptr;
	if (::_wfopen_s(&eventsFile, eventsPath, L"wb") != 0 || !eventsFile) {
		g_recSessionDir[0] = L'\0';
		return;
	}

	g_recEventsFile = eventsFile;
	::setvbuf(g_recEventsFile, g_recEventsBuffer, _IOFBF, sizeof(g_recEventsBuffer));
	g_recSeq = 0;
	g_recT0 = ::GetTickCount64();
	g_recLastFlushMs = g_recT0;
	g_recLastSnapEntitySig.clear();
	g_recLastNonEmptyEntityJson.clear();
	g_recLastNativeKey.clear();
	g_recLastIdleSnapshotMs = g_recT0;
	g_recActive = true;
	::swprintf_s(g_recActiveMarkerPath, MAX_PATH, L"%s\\.active", g_recSessionDir);
	FILE* activeFile = nullptr;
	if (::_wfopen_s(&activeFile, g_recActiveMarkerPath, L"wb") == 0 && activeFile) {
		::fprintf(activeFile, "pid=%lu\n", ::GetCurrentProcessId());
		::fclose(activeFile);
	}
	if (g_recStateChanged)
		g_recStateChanged(true, g_recStateChangedContext);

	// Initial doc + snapshot so a session is never an empty events file.
	RecEmitDoc();
	RecEmitSnapshot();
	OvLog(L"session recorder: started");
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
		ClearOwnSelection();  // avoid weird lingering row/task sel + container highlight while moving overall component
		chart->Select(Office::msoTrue);
		FocusPowerPoint();
		OvLog(L"move-chart grip: selected CHART_ROOT (own sel cleared)");
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

// Returns true only when the discovery hint pill's own shown/hidden state
// actually transitioned this tick (hidden->shown once hover has dwelled
// 600ms, or shown->hidden when the hover ends) -- mirrors UpdateHoverFromCursor's
// return-bool pattern so Tick() can request exactly one repaint per transition.
// Previously the "is it due to show" check was re-derived separately in Tick
// (from the raw fields) while the ACTUAL g_emptyCellHintShownThisSession
// mutation happened later, inside Paint; if a tick's repaint didn't go
// through (or the pill only visually flips on a later tick), the two could
// disagree and effectively re-arm, letting a stale hover position left by an
// earlier stage/gesture show then clear the pill across an otherwise-idle
// window. Owning both the transition and the flag here removes that split.
bool UpdateEmptyCellHoverHint() {
	const bool wasShown = g_emptyCellHintShownThisSession;
	if (g_gestureActive || g_dragActive || g_linkMode || IsEditSessionActive()) {
		g_emptyCellHoverActive = false;
		g_emptyCellHintShownThisSession = false;
		return wasShown != g_emptyCellHintShownThisSession;
	}
	POINT pt = {};
	if (!OverlayGetCursorPos(&pt) || !::PtInRect(&g_chartScreenRect, pt)) {
		g_emptyCellHoverActive = false;
		g_emptyCellHintShownThisSession = false;
		g_creationFailHint.clear();
		return wasShown != g_emptyCellHintShownThisSession;
	}
	POINT clientPt = { pt.x - g_windowOriginX, pt.y - g_windowOriginY };
	const HtHit hit = HitTestClientPoint(clientPt);
	if (hit.zone != HtZone::EmptyCell) {
		g_emptyCellHoverActive = false;
		g_emptyCellHintShownThisSession = false;
		g_creationFailHint.clear();
		return wasShown != g_emptyCellHintShownThisSession;
	}
	if (!g_emptyCellHoverActive) {
		g_emptyCellHoverActive = true;
		g_emptyCellHoverSinceTick = ::GetTickCount();
		g_emptyCellHintShownThisSession = false;
	} else if (!g_emptyCellHintShownThisSession &&
		(::GetTickCount() - g_emptyCellHoverSinceTick) >= 600) {
		g_emptyCellHintShownThisSession = true;
	}
	return wasShown != g_emptyCellHintShownThisSession;
}

void PaintPositionedHintPill(Gdiplus::Graphics& g, const wchar_t* hint) {
	if (!hint || !*hint) return;
	Gdiplus::Font hintFont(L"Segoe UI", (Gdiplus::REAL)g_tooltipFontPx, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::RectF hintBounds;
	g.MeasureString(hint, -1, &hintFont, Gdiplus::PointF(0, 0), &hintBounds);
	const int tipPad = g_tooltipPad;
	const int tipW = (int)hintBounds.Width + tipPad * 2;
	const int tipH = (int)hintBounds.Height + tipPad * 2;
	int tipX = 0;
	int tipY = 0;
	if (g_appBarGeomValid) {
		const int cx = (g_appBarLastRect.left + g_appBarLastRect.right) / 2 - g_windowOriginX;
		tipY = g_appBarLastRect.top - g_windowOriginY - tipH - Scale(8);
		tipX = cx - tipW / 2;
	} else if (g_chartScreenRect.right > g_chartScreenRect.left) {
		const int cx = (g_chartScreenRect.left + g_chartScreenRect.right) / 2 - g_windowOriginX;
		tipY = g_chartScreenRect.bottom - g_windowOriginY - tipH - Scale(12);
		tipX = cx - tipW / 2;
	} else {
		return;
	}
	Gdiplus::GraphicsPath hintPath;
	AddRoundRect(hintPath, (Gdiplus::REAL)tipX, (Gdiplus::REAL)tipY, (Gdiplus::REAL)tipW, (Gdiplus::REAL)tipH, 3.0f);
	Gdiplus::SolidBrush hintBg(GpToken(255, gt::ink));
	g.FillPath(&hintBg, &hintPath);
	Gdiplus::StringFormat hintFmt;
	hintFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
	hintFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
	Gdiplus::SolidBrush hintText(GpToken(255, gt::surface));
	g.DrawString(hint, -1, &hintFont, Gdiplus::RectF((Gdiplus::REAL)tipX, (Gdiplus::REAL)tipY, (Gdiplus::REAL)tipW, (Gdiplus::REAL)tipH), &hintFmt, &hintText);
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
	if (!OverlayGetCursorPos(&screenPt)) return false;
	POINT pt = screenPt;
	::ScreenToClient(hwnd, &pt);

	bool overChromeWidget = ButtonFromClientPoint(pt) >= 0 || HoverInsertFromClientPoint(pt) ||
		RowBoundaryInsertFromClientPoint(pt) >= 0 || GripFromClientPoint(pt);
	if ((g_linkMode || (g_gestureActive && g_dragKind == DragKind::LinkPort)) &&
		!overChromeWidget && ::PtInRect(&g_chartScreenRect, screenPt)) {
		HtHit hit = HitTestClientPoint(pt);
		if (hit.zone == HtZone::TaskBody || hit.zone == HtZone::TaskEdgeL ||
			hit.zone == HtZone::TaskEdgeR || hit.zone == HtZone::TaskProgressEdge ||
			hit.zone == HtZone::Milestone) {
			::SetCursor(::LoadCursor(NULL, IDC_CROSS));
			return true;
		}
	}
	HtZone zone = HtZone::Outside;
	if (!overChromeWidget) {
		if (!::PtInRect(&g_chartScreenRect, screenPt)) return false; // truly outside: default cursor
		// Match WM_LBUTTONDOWN precedence: a header port must not inherit a
		// marker's full-height semantic band and display the wrong cursor.
		zone = HitTestWindowPortAtClient(pt);
		if (zone == HtZone::Outside) zone = HitTestClientPoint(pt).zone;
	}
	HtCursor cur = GanttCursorForZone(zone, overChromeWidget);
	HCURSOR hc = HCursorForHtCursor(cur);
	if (!hc) return false;
	::SetCursor(hc);
	return true;
}

// Apply the selection effect of a completed CLICK gesture (down+up within the
// drag threshold, same point). TaskBody/edges, Milestone, and Marker select
// the item; a RowBand hit with a row id selects the row; EmptyCell/
// background-RowBand (empty row id)/Outside all clear the selection. Chrome
// is re-synced by the caller's next Tick() (or immediately, for the
// harness's benefit, by calling SyncSelectionChromeFromOwnSelection after
// this). Ctrl+click toggles membership; Shift+click on a row selects a
// contiguous row range from the primary row anchor.
void ApplyClickSelection(const HtHit& hit, bool ctrlDown, bool shiftDown) {
	auto selectItem = [&](const std::string& kind, const std::string& id) {
		if (ctrlDown) ToggleOwnSelectionMember(kind, id);
		else SetOwnSelection(kind, id);
	};

	switch (hit.zone) {
	case HtZone::TaskBody:
	case HtZone::TaskEdgeL:
	case HtZone::TaskEdgeR:
	case HtZone::TaskProgressEdge:
		if (shiftDown) return; // tasks join multi via Ctrl only
		selectItem("TASK", hit.id);
		return;
	case HtZone::Milestone:
		if (shiftDown) return;
		selectItem("MILESTONE", hit.id);
		return;
	case HtZone::Marker:
		if (shiftDown) return;
		selectItem("MARKER", hit.id);
		return;
	case HtZone::Text:
		if (shiftDown) return;
		selectItem("TEXT", hit.id);
		return;
	case HtZone::Dependency:
		if (ctrlDown || shiftDown) return;
		SetOwnSelection("DEP", hit.id);
		return;
	case HtZone::RowBand:
		if (!hit.rowId.empty()) {
			if (shiftDown) SelectRowRangeFromPrimary(hit.rowId);
			else if (ctrlDown) ToggleOwnSelectionMember("ROW", hit.rowId);
			else SetOwnSelection("ROW", hit.rowId);
		} else {
			ClearOwnSelection();
		}
		return;
	case HtZone::Label:
		if (hit.kind == HtItemKind::RowLabel && !hit.id.empty()) {
			if (shiftDown) SelectRowRangeFromPrimary(hit.id);
			else if (ctrlDown) ToggleOwnSelectionMember("ROW", hit.id);
			else SetOwnSelection("ROW", hit.id);
		}
		return;
	case HtZone::EmptyCell:
	case HtZone::Outside:
	default:
		ClearOwnSelection();
		return;
	}
	// v2.4.1 robust row selection: hover already lights the band; ensure click in row area selects the row
	// even if the HtHit zone resolution was conservative. First-class rows must be clickable.
	if (g_ownSelKind.empty() && !g_hoverRowId.empty()) {
		if (shiftDown) SelectRowRangeFromPrimary(g_hoverRowId);
		else if (ctrlDown) ToggleOwnSelectionMember("ROW", g_hoverRowId);
		else SetOwnSelection("ROW", g_hoverRowId);
	}
}

// Reset all drag-gesture state to idle. Idempotent — safe to call from
// WM_CAPTURECHANGED (including our own ReleaseCapture, which delivers it) as
// well as from an explicit cancel.
void ClearDragCommitEcho() {
	// Idle ticks are paint-free (SR-LIFE), so clearing the echo STATE without
	// a repaint leaves the ghost bar's pixels on the layered window forever
	// (v2.6.2: task bar kept floating over the axis after a drag). Repaint
	// exactly once when an active echo is cleared.
	const bool wasActive = g_dragCommitEcho.active;
	g_dragCommitEcho = {};
	if (wasActive) RequestOverlayRepaint();
}

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
	g_dragTextAnchored = false;
	g_dragOrigDx = 0.0f;
	g_dragOrigDy = 0.0f;
	g_dragPxPerPt = 0.0f;
	g_dragCandidateDx = 0.0f;
	g_dragCandidateDy = 0.0f;
	g_dragOrigPercent = 0;
	g_dragCandidatePercent = 0;
	g_dragPillText.clear();
	::SetRectEmpty(&g_dragPreviewRect);
	g_windowPreviewDoc = PpDocument{};
	g_windowCandidateStart.clear();
	g_windowCandidateEnd.clear();
	g_linkDragFromRight = false;
	g_linkDragHoverId.clear();
	g_linkDragHoverKind.clear();
}

// PP_PROJ-based screen-pixel day<->point projection (SR-CRE-01). Used as a
// fallback when no task shape can supply a px/day scale (empty charts).
static ProjPx ProjectionPx() {
	ProjPx r{};
	r.ok = false;
	if (g_chartScreenRect.right <= g_chartScreenRect.left) return r;
	if (g_chartWidthPt <= 0.0f) return r;
	PpProj proj;
	if (!ParseProj(g_chartProj, &proj) || proj.ptPerDay <= 0.0f) return r;
	const double chartScreenWidthPx = (double)(g_chartScreenRect.right - g_chartScreenRect.left);
	const double scale = chartScreenWidthPx / (double)g_chartWidthPt;
	r.pxPerDay = (double)proj.ptPerDay * scale;
	r.originXpx = (double)g_chartScreenRect.left + ((double)proj.originX - (double)g_chartLeftPt) * scale;
	r.originDay = proj.minDay - proj.pad; // day at originX (frozen PP_PROJ semantics)
	r.ok = r.pxPerDay > 0.0;
	return r;
}

// Bar height within a row band matches GanttScene ROW_HEIGHT/BAR_INSET (20/36).
static constexpr float kRowBarHeightFrac = (36.0f - 8.0f * 2.0f) / 36.0f;

static long ChartWindowLoDay() {
	// m4/UF-08: an explicit window is the exact clamp boundary (windowStart is
	// also PP_PROJ's minDay with pad=0, but the doc field is the declared truth).
	{
		PpDocument cachedDoc;
		if (Gantt_TryPeekCachedDoc(&cachedDoc) && !cachedDoc.windowStart.empty())
			return DateToDays(cachedDoc.windowStart);
	}
	ProjPx p = ProjectionPx();
	return p.ok ? p.originDay : 0;
}

static long ChartWindowHiDay() {
	// m4: clamp exactly to the explicit window's last day. The pixel-derived
	// bound below spans the whole chart rect, which includes the left rail
	// (ROW_GUTTER) — under an explicit window it overshoots windowEnd by
	// ~ROW_GUTTER/ptPerDay days.
	{
		PpDocument cachedDoc;
		if (Gantt_TryPeekCachedDoc(&cachedDoc) && !cachedDoc.windowEnd.empty())
			return DateToDays(cachedDoc.windowEnd);
	}
	ProjPx p = ProjectionPx();
	if (!p.ok) return LONG_MAX / 2;
	return p.originDay + (long)::lround((double)(g_chartScreenRect.right - g_chartScreenRect.left) / p.pxPerDay);
}

static long SnapDayToScale(long day, const std::string& scale) {
	if (scale.empty() || scale == "day") return day;
	if (scale == "week") {
		// Monday-aligned (1970-01-05 is Monday at day index 4).
		const long epochMonday = 4;
		long rel = day - epochMonday;
		long mod = ((rel % 7) + 7) % 7;
		if (mod <= 3) return day - mod;
		return day + (7 - mod);
	}
	if (scale == "month") {
		std::string iso = DaysToDate(day);
		if (iso.size() >= 10) {
			int y = std::stoi(iso.substr(0, 4));
			int m = std::stoi(iso.substr(5, 2));
			int d = std::stoi(iso.substr(8, 2));
			if (d <= 15) return DateToDays(iso.substr(0, 8) + "01");
			m++;
			if (m > 12) { m = 1; ++y; }
			char buf[16];
			::snprintf(buf, sizeof(buf), "%04d-%02d-01", y, m);
			return DateToDays(buf);
		}
		return day;
	}
	if (scale == "quarter") {
		std::string iso = DaysToDate(day);
		if (iso.size() >= 10) {
			int y = std::stoi(iso.substr(0, 4));
			int m = std::stoi(iso.substr(5, 2));
			int qStart = ((m - 1) / 3) * 3 + 1;
			char buf[16];
			::snprintf(buf, sizeof(buf), "%04d-%02d-01", y, qStart);
			long qDay = DateToDays(buf);
			m += 3;
			if (m > 12) { m -= 12; ++y; }
			::snprintf(buf, sizeof(buf), "%04d-%02d-01", y, m);
			long nextQ = DateToDays(buf);
			return (day - qDay <= 45) ? qDay : nextQ;
		}
		return day;
	}
	if (scale == "year") {
		std::string iso = DaysToDate(day);
		if (iso.size() >= 4) {
			int y = std::stoi(iso.substr(0, 4));
			long jan1 = DateToDays(std::to_string(y) + "-01-01");
			long nextJan = DateToDays(std::to_string(y + 1) + "-01-01");
			return (day - jan1 <= 182) ? jan1 : nextJan;
		}
		return day;
	}
	return day;
}

// Clamp a single moving edge while the opposite window edge remains fixed.
// The pure GanttOps bounds are reused so preview cannot show a commit that W3
// would reject. The binary search handles the calendar-dependent maximum span
// (month/year lengths) without a per-day walk across a multi-year drag.
static long ClampWindowEdgeDay(DragKind kind, long candidateDay, long startDay,
	long endDay, const std::string& scale) {
	if (kind == DragKind::WindowEdgeR) {
		const long minEnd = startDay + MinimumWindowSpanDays(DaysToDate(startDay), scale);
		const long maxEnd = MaximumWindowEndDay(DaysToDate(startDay), scale);
		return std::max(minEnd, std::min(maxEnd, candidateDay));
	}
	if (kind == DragKind::WindowEdgeL) {
		// Find the earliest start whose scale-dependent maximum reaches endDay.
		if (endDay > MaximumWindowEndDay(DaysToDate(candidateDay), scale)) {
			long lo = candidateDay, hi = endDay;
			while (lo < hi) {
				const long mid = lo + (hi - lo) / 2;
				if (endDay <= MaximumWindowEndDay(DaysToDate(mid), scale)) hi = mid;
				else lo = mid + 1;
			}
			candidateDay = lo;
		}
		// A calendar unit's day count depends on the candidate start. Move left
		// until the kept end is at least one complete visible unit away.
		while (candidateDay < endDay
			&& endDay - candidateDay < MinimumWindowSpanDays(DaysToDate(candidateDay), scale)) {
			--candidateDay;
		}
	}
	return candidateDay;
}

static void ComputeWindowEdgeCandidate(DragKind kind, const std::string& baselineStart,
	const std::string& baselineEnd, float pxPerDay, long dx, const std::string& scale,
	std::string* outStart, std::string* outEnd) {
	if (!outStart || !outEnd) return;
	long startDay = DateToDays(baselineStart);
	long endDay = DateToDays(baselineEnd);
	long candidateDay = (kind == DragKind::WindowEdgeL) ? startDay : endDay;
	if (pxPerDay > 0.0f) candidateDay += (long)::lround((double)dx / (double)pxPerDay);
	candidateDay = SnapDayToScale(candidateDay, scale);
	candidateDay = ClampWindowEdgeDay(kind, candidateDay, startDay, endDay, scale);
	if (kind == DragKind::WindowEdgeL) startDay = candidateDay;
	else if (kind == DragKind::WindowEdgeR) endDay = candidateDay;
	*outStart = DaysToDate(startDay);
	*outEnd = DaysToDate(endDay);
}

// Click-to-widen (2026-07-18 discoverability fix). Before this, a window port
// only did anything if the press turned into a threshold-crossing DRAG: a plain
// click fell through to the zero-delta no-op and the user got no feedback at
// all, which is indistinguishable from "this is not a button". A click now
// steps the pressed edge outward by one visible unit; drag remains the precise
// gesture. Snap/clamp go through the same pure helpers as the drag path so a
// click can never produce a window the commit would reject.
static void ComputeWindowExpandStep(DragKind kind, const std::string& baselineStart,
	const std::string& baselineEnd, const std::string& scale,
	std::string* outStart, std::string* outEnd) {
	if (!outStart || !outEnd) return;
	long startDay = DateToDays(baselineStart);
	long endDay = DateToDays(baselineEnd);
	const std::string& edgeIso = (kind == DragKind::WindowEdgeL) ? baselineStart : baselineEnd;
	long step = MinimumWindowSpanDays(edgeIso, scale);
	// At "day" scale one unit is one day — a step the user would read as
	// "nothing happened". A week is the smallest legible increment.
	if (step < 7) step = 7;
	long candidateDay = (kind == DragKind::WindowEdgeL) ? startDay - step : endDay + step;
	candidateDay = SnapDayToScale(candidateDay, scale);
	candidateDay = ClampWindowEdgeDay(kind, candidateDay, startDay, endDay, scale);
	if (kind == DragKind::WindowEdgeL) startDay = candidateDay;
	else if (kind == DragKind::WindowEdgeR) endDay = candidateDay;
	*outStart = DaysToDate(startDay);
	*outEnd = DaysToDate(endDay);
}

static void UpdateWindowEdgeDragCandidate(long dx) {
	std::string candidateStart, candidateEnd;
	ComputeWindowEdgeCandidate(g_dragKind, g_dragOrigStart, g_dragOrigEnd, g_dragPxPerDay,
		dx, g_windowPreviewDoc.scale, &candidateStart, &candidateEnd);
	g_windowCandidateStart = candidateStart;
	g_windowCandidateEnd = candidateEnd;
	const long oldSpan = DateToDays(g_dragOrigEnd) - DateToDays(g_dragOrigStart);
	const long newSpan = DateToDays(candidateEnd) - DateToDays(candidateStart);
	const long spanDelta = newSpan - oldSpan;
	const long oldEdge = (g_dragKind == DragKind::WindowEdgeL)
		? DateToDays(g_dragOrigStart) : DateToDays(g_dragOrigEnd);
	const long newEdge = (g_dragKind == DragKind::WindowEdgeL)
		? DateToDays(candidateStart) : DateToDays(candidateEnd);
	g_dragDeltaDays = newEdge - oldEdge;
	char pill[96];
	::snprintf(pill, sizeof(pill), "%s -> %s (%+ldd)", candidateStart.c_str(), candidateEnd.c_str(), spanDelta);
	g_dragPillText = pill;
}

static void StartWindowEdgeDrag(HtZone zone, POINT downPt) {
	if ((zone != HtZone::WindowPortL && zone != HtZone::WindowPortR) || !g_app) return;
	ResetDragGestureState();
	PpDocument doc;
	if (!ReadGanttDocFromSlide(g_app, &doc)) return;
	ProjPx projection = ProjectionPx();
	if (!projection.ok || projection.pxPerDay <= 0.0) return;

	std::string baselineStart, baselineEnd;
	if (!doc.windowStart.empty() && !doc.windowEnd.empty()) {
		baselineStart = doc.windowStart;
		baselineEnd = doc.windowEnd;
	} else {
		// D2: first drag materializes only an in-memory baseline matching the
		// current auto-fit projection. PP_PROJ has no maxDay, so reconstruct
		// the right edge from the document's true max date plus its current pad.
		PpProj proj;
		if (!ParseProj(g_chartProj, &proj)) return;
		std::string maxDate;
		auto considerMax = [&](const std::string& iso) {
			if (!iso.empty() && (maxDate.empty() || iso > maxDate)) maxDate = iso;
		};
		for (const auto& task : doc.tasks) { considerMax(task.start); considerMax(task.end); }
		for (const auto& milestone : doc.milestones) considerMax(milestone.date);
		if (maxDate.empty()) maxDate = DaysToDate(proj.minDay + 30);
		baselineStart = DaysToDate(proj.minDay - proj.pad);
		baselineEnd = DaysToDate(DateToDays(maxDate) + proj.pad);
	}
	if (baselineStart.empty() || baselineEnd.empty() || DateToDays(baselineEnd) <= DateToDays(baselineStart)) return;

	g_dragKind = (zone == HtZone::WindowPortL) ? DragKind::WindowEdgeL : DragKind::WindowEdgeR;
	g_dragOrigStart = baselineStart;
	g_dragOrigEnd = baselineEnd;
	g_windowCandidateStart = baselineStart;
	g_windowCandidateEnd = baselineEnd;
	g_windowPreviewDoc = doc;
	g_dragPxPerDay = (float)projection.pxPerDay;
	g_dragLastPt = downPt;
	g_dragAnchorRect = WindowPortHitRectScreen(zone == HtZone::WindowPortR);
	g_gestureActive = true;
	if (g_recActive)
		RecEmitGestureStart(RecDragKindName(g_dragKind), "", "", downPt);
}

// W3 real commit (SR-WIN-23/27/28). Reads ONLY its snapshot parameters — the
// WM_LBUTTONUP call site captures candidate + gesture baseline into locals
// BEFORE ReleaseCapture/ResetDragGestureState, so no g_drag*/g_window* global
// is consulted here (snapshot-locals rule). Standard commit shape (see
// CommitProgressGesture): read PP_DOC -> pure GanttOps mutation ->
// RebuildChart, which runs StartNewUndoEntryIfPossible BEFORE any COM write so
// the tag write + every shape write collapse into ONE undo entry.
// m9: a zero-delta release (candidate == gesture baseline, which covers the D2
// first-drag materialization where the doc window is still empty) is a pure
// no-op: no document write, no rebuild, no undo entry. Esc-cancel never gets
// here (W2 behavior kept: CancelDragGesture just clears preview + repaints).
static void CommitWindowGesture(const std::string& startISO, const std::string& endISO,
	const std::string& baselineStartISO, const std::string& baselineEndISO) {
	if (startISO.empty() || endISO.empty()) {
		RequestOverlayRepaint(); // preview state was already cleared — repaint it away
		return;
	}
	if (startISO == baselineStartISO && endISO == baselineEndISO) {
		OvLog(L"window commit: zero-delta release - no-op (m9)");
		if (g_recActive)
			RecEmitGestureCommit("WindowEdge", "", "ok", 0, "\"delta\":\"zero\"");
		RequestOverlayRepaint();
		return;
	}
	if (!g_app || g_mutating) {
		RequestOverlayRepaint();
		return;
	}
	g_mutating = true;
	const char* result = "fail";
	long hr = 0;
	try {
		PpDocument doc;
		if (ReadGanttDocFromSlide(g_app, &doc)) {
			// Belt-and-braces zero-delta vs the document itself (a stale
			// baseline must never force a no-change rebuild into undo).
			if (doc.windowStart == startISO && doc.windowEnd == endISO) {
				OvLog(L"window commit: candidate equals document window - no-op (m9)");
				result = "ok";
			} else if (SetTimeWindow(doc, startISO, endISO)) {
				wchar_t msg[160];
				::swprintf_s(msg, 160, L"window commit: %hs -> %hs", startISO.c_str(), endISO.c_str());
				OvLog(msg);
				// View-only op: selection is not re-targeted (selectId empty).
				// W1's cache-key/fast-path refusal makes this a full reconcile;
				// RebuildChart's M4 choke point resets a now-hidden selection.
				RebuildChart(doc, "");
				result = "ok";
			} else {
				OvLog(L"window commit: SetTimeWindow rejected candidate");
			}
		}
	}
	catch (const _com_error& e) {
		hr = (long)e.Error();
		OvLog(L"COM error committing window gesture");
		RecError("CommitWindowGesture", hr, "COM error");
	}
	catch (const std::exception& e) {
		hr = (long)E_FAIL;
		OvLog(L"document error committing window gesture");
		RecError("CommitWindowGesture", hr, e.what());
	}
	catch (...) {
		hr = (long)E_FAIL;
		OvLog(L"unknown error committing window gesture");
		RecError("CommitWindowGesture", hr, "unknown error");
	}
	g_mutating = false;
	if (g_recActive) {
		std::string extra = "\"start\":\"";
		EntityJsonAppendEscaped(extra, startISO);
		extra += "\",\"end\":\"";
		EntityJsonAppendEscaped(extra, endISO);
		extra += "\"";
		RecEmitGestureCommit("WindowEdge", "", result, hr, extra);
	}
	RequestOverlayRepaint();
}

static long SnapGestureDeltaDays(DragKind kind, const std::string& origStart, const std::string& origEnd,
	long deltaDays, const std::string& scale) {
	// UF-09 / SR-IXC-04: unit-snapping applies to MARKERS only (vertical date
	// lines). Task bars and milestones stay day-precise at every zoom — a
	// week/month-snapped task drag would make mid-week dates unreachable and
	// silently move the drop by days (caught by the INPLACE e2e stage).
	if (kind != DragKind::Marker) return deltaDays;
	std::string cs, ce;
	ComputeDragCandidateDates(kind, origStart, origEnd, deltaDays, cs, ce);
	long sd = SnapDayToScale(DateToDays(cs), scale);
	long ed = SnapDayToScale(DateToDays(ce), scale);
	long origStartDay = DateToDays(origStart);
	long origEndDay = origEnd.empty() ? origStartDay : DateToDays(origEnd);
	switch (kind) {
	case DragKind::TaskEdgeL:
		return sd - origStartDay;
	case DragKind::TaskEdgeR:
		return ed - origEndDay;
	default:
		return sd - origStartDay;
	}
}

static long ClampGestureDeltaDays(DragKind kind, const std::string& origStart, const std::string& origEnd, long deltaDays) {
	long lo = ChartWindowLoDay();
	long hi = ChartWindowHiDay();
	long startDay = DateToDays(origStart);
	long endDay = origEnd.empty() ? startDay : DateToDays(origEnd);
	switch (kind) {
	case DragKind::TaskEdgeL: {
		long ns = startDay + deltaDays;
		if (ns < lo) deltaDays -= (ns - lo);
		if (ns > endDay) deltaDays -= (ns - endDay);
		break;
	}
	case DragKind::TaskEdgeR: {
		long ne = endDay + deltaDays;
		if (ne > hi) deltaDays -= (ne - hi);
		if (ne < startDay) deltaDays -= (ne - startDay);
		break;
	}
	case DragKind::TaskBody:
	case DragKind::Milestone:
	case DragKind::Marker:
	default: {
		long span = endDay - startDay;
		long ns = startDay + deltaDays;
		long ne = endDay + deltaDays;
		if (ns < lo) deltaDays += (lo - ns);
		if (ne > hi) deltaDays -= (ne - hi);
		(void)span;
		break;
	}
	}
	return deltaDays;
}

static long AnchorDayFromScreenX(long screenX) {
	// C2 (SR-WIN-22): under an explicit window a bar's shape rect can be
	// CLIPPED, so "task rect.left == task start date" is false and the
	// reference-task calibration below misplaces the anchor by the clipped-off
	// span. PP_PROJ is rewritten on every window commit (pad=0, minDay ==
	// windowStart), so it is the authoritative px<->day map — use it outright.
	{
		PpDocument cachedDoc;
		if (Gantt_TryPeekCachedDoc(&cachedDoc)
			&& !cachedDoc.windowStart.empty() && !cachedDoc.windowEnd.empty()) {
			ProjPx winProj = ProjectionPx();
			if (winProj.ok) {
				return winProj.originDay + (long)::lround((screenX - winProj.originXpx) / winProj.pxPerDay);
			}
		}
	}
	float pxPerDay = ComputeEmptyCellPxPerDay();
	if (pxPerDay > 0.0f && g_app) {
		try {
			std::string json = ReadGanttFromSlide(g_app);
			if (!json.empty()) {
				PpDocument doc = DocumentFromJson(json);
				const bool docWindowed = !doc.windowStart.empty() && !doc.windowEnd.empty();
				for (const auto& item : g_hitSnapshot.items) {
					if (item.kind != HtItemKind::Task) continue;
					const PpTask* task = FindTask(doc, item.id);
					if (!task) continue;
					// C2 fallback guard (cache miss): a clipped shape is not a
					// calibration source — its rect no longer spans true dates.
					if (docWindowed && (DateToDays(task->start) < DateToDays(doc.windowStart)
						|| DateToDays(task->end) > DateToDays(doc.windowEnd))) continue;
					const long refDay = DateToDays(task->start);
					const long refScreenX = item.rect.left;
					return refDay + (long)::lround((double)(screenX - refScreenX) / (double)pxPerDay);
				}
			}
		}
		catch (...) {}
	}
	ProjPx proj = ProjectionPx();
	if (proj.ok) {
		return proj.originDay + (long)::lround((screenX - proj.originXpx) / proj.pxPerDay);
	}
	return 0;
}

static long DayAtVisibleCenter() {
	const long centerScreenX = (g_chartScreenRect.left + g_chartScreenRect.right) / 2;
	return AnchorDayFromScreenX(centerScreenX);
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
	// C2 (SR-WIN-22): under an explicit window ANY bar (the anchor included)
	// can be clipped at a window edge, so rect-width / true-day-span
	// understates px/day. PP_PROJ is rewritten on every window commit and is
	// exact under an explicit window (pad=0), so prefer it outright; the
	// rect-derived paths below stay the auto-fit behavior (where rects always
	// span true dates). ProjectionPx and the cache peek are both COM-free.
	{
		PpDocument cachedDoc;
		if (Gantt_TryPeekCachedDoc(&cachedDoc)
			&& !cachedDoc.windowStart.empty() && !cachedDoc.windowEnd.empty()) {
			ProjPx winProj = ProjectionPx();
			if (winProj.ok && winProj.pxPerDay > 0.0) return (float)winProj.pxPerDay;
		}
	}
	if (anchorSpanDays > 0) {
		float widthPx = (float)(anchorRect.right - anchorRect.left);
		if (widthPx > 0.0f) return widthPx / (float)anchorSpanDays;
	}
	if (!g_app) return 0.0f;
	std::string json = ReadGanttFromSlide(g_app);
	if (json.empty()) return 0.0f;
	PpDocument doc = DocumentFromJson(json);
	const bool docWindowed = !doc.windowStart.empty() && !doc.windowEnd.empty();
	for (const auto& item : g_hitSnapshot.items) {
		if (item.kind != HtItemKind::Task) continue;
		const PpTask* task = FindTask(doc, item.id);
		if (!task) continue;
		// C2 fallback guard (cache miss under a window): skip clipped shapes
		// as calibration sources — their rects no longer span true dates.
		if (docWindowed && (DateToDays(task->start) < DateToDays(doc.windowStart)
			|| DateToDays(task->end) > DateToDays(doc.windowEnd))) continue;
		long span = DateToDays(task->end) - DateToDays(task->start) + 1;
		if (span <= 0) continue;
		float widthPx = (float)(item.rect.right - item.rect.left);
		if (widthPx > 0.0f) return widthPx / (float)span;
	}
	ProjPx proj = ProjectionPx();
	if (proj.ok && proj.pxPerDay > 0.0) return (float)proj.pxPerDay;
	return 0.0f;
}

// Begin a drag-move-resize gesture anchored at a TaskBody/TaskEdgeL/
// TaskEdgeR/Milestone/Marker/Text hit. Reads the document once (per A4's
// guidance to read doc dates at gesture start) so subsequent WM_MOUSEMOVE
// handling stays COM-free. No-ops (leaves gesture state untouched) if the
// doc/task/milestone/marker/text can't be resolved — the gesture then
// behaves as a plain click-vs-drag-less capture (existing WM_LBUTTONUP
// click-select logic still applies on threshold-within release).
void StartDragGesture(const HtHit& hit, POINT downPt) {
	ResetDragGestureState();
	if (hit.zone != HtZone::TaskBody && hit.zone != HtZone::TaskEdgeL &&
		hit.zone != HtZone::TaskEdgeR && hit.zone != HtZone::TaskProgressEdge &&
		hit.zone != HtZone::Milestone &&
		hit.zone != HtZone::Marker && hit.zone != HtZone::Text) {
		return;
	}
	if (!g_app) return;
	std::string json = ReadGanttFromSlide(g_app);
	if (json.empty()) return;
	PpDocument doc = DocumentFromJson(json);

	HtItemKind wantKind = (hit.zone == HtZone::Milestone) ? HtItemKind::Milestone
		: (hit.zone == HtZone::Marker) ? HtItemKind::Marker
		: (hit.zone == HtZone::Text) ? HtItemKind::Text : HtItemKind::Task;
	RECT anchorRect = {};
	bool haveRect = false;
	for (const auto& item : g_hitSnapshot.items) {
		if (item.id == hit.id && item.kind == wantKind) {
			anchorRect = { item.rect.left, item.rect.top, item.rect.right, item.rect.bottom };
			haveRect = true;
			break;
		}
	}
	if (!haveRect) return;

	if (hit.zone == HtZone::Text) {
		const PpText* txt = nullptr;
		for (const auto& t : doc.texts) if (t.id == hit.id) { txt = &t; break; }
		if (!txt) return;
		g_dragKind = DragKind::Text;
		g_dragId = hit.id;
		g_dragTextAnchored = !txt->anchorId.empty();
		g_dragOrigDx = txt->dx;
		g_dragOrigDy = txt->dy;
		// For a FREE text, g_dragOrigStart carries its current cell date (like
		// Marker/Milestone) so the day-delta path below can compute its new
		// (rowId, date) cell exactly like a TaskBody/Marker drag; unused (left
		// empty) for an ANCHORED text, which only ever moves via dx/dy.
		g_dragOrigStart = txt->date;
		g_dragOrigEnd.clear();
		// A text's rect carries no day-span of its own to derive a px/day
		// scale from (like Milestone/Marker), so fall back to scanning the
		// snapshot for a task with a usable span (ComputeDragPxPerDay(rect,0)),
		// then convert that px/day scale to px/point via PP_PROJ's ptPerDay —
		// see g_dragPxPerPt's header comment for why one px/pt ratio covers
		// both axes. An ANCHORED text only needs the px/pt scale (dx/dy is a
		// continuous point offset); a FREE text needs BOTH: px/day for its
		// day-granularity (rowId,date) re-homing, and px/pt purely so the
		// mid-drag ghost/tooltip can preview the same continuous offset a
		// mouse-up would otherwise snap to whole days at (the residual dx/dy
		// is zeroed on commit either way — see CommitDragGesture).
		g_dragPxPerDay = ComputeDragPxPerDay(anchorRect, 0);
		PpProj proj;
		if (g_dragPxPerDay > 0.0f && ParseProj(g_chartProj, &proj) && proj.ptPerDay > 0.0f) {
			g_dragPxPerPt = g_dragPxPerDay / proj.ptPerDay;
		}
		if (!g_dragTextAnchored) {
			// Free text can be dragged to a different row, mirroring TaskBody's
			// row-reassign tracking (UpdateDragGesture/CommitDragGesture reuse
			// g_dragTargetRowId/g_dragTargetRowRect for this).
			g_dragOrigRowId = txt->rowId;
			g_dragTargetRowId = txt->rowId;
			g_dragTargetRowRect = anchorRect;
			for (const auto& band : g_rowBands) {
				if (band.rowId == txt->rowId) { g_dragTargetRowRect = band.screenRect; break; }
			}
		}
		if (g_dragPxPerPt <= 0.0f) { ResetDragGestureState(); return; }
		g_dragAnchorRect = anchorRect;
		g_dragLastPt = downPt;
		g_dragDeltaDays = 0;
		g_gestureActive = true;
		if (g_recActive)
			RecEmitGestureStart(RecDragKindName(g_dragKind), g_dragId, g_dragOrigRowId, downPt);
		return;
	}

	if (hit.zone == HtZone::Marker) {
		const PpMarker* mk = nullptr;
		for (const auto& m : doc.markers) if (m.id == hit.id) { mk = &m; break; }
		if (!mk) return;
		g_dragKind = DragKind::Marker;
		g_dragId = hit.id;
		g_dragOrigStart = mk->date;
		g_dragOrigEnd.clear();
		// A marker's synthesized hit rect is a fixed-width band (2*edgeBandPx)
		// independent of the day axis, so it carries no usable px/day scale of
		// its own (unlike a task's rect-width / day-span) — always fall back
		// to scanning the snapshot for a task with a usable span, exactly like
		// the Milestone case (ComputeDragPxPerDay(rect, 0) does this already).
		g_dragPxPerDay = ComputeDragPxPerDay(anchorRect, 0);
	} else if (hit.zone == HtZone::Milestone) {
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
		if (hit.zone == HtZone::TaskProgressEdge) {
			g_dragKind = DragKind::TaskProgress;
			g_dragId = hit.id;
			g_dragOrigStart = task->start;
			g_dragOrigEnd = task->end;
			g_dragOrigPercent = task->percent;
			g_dragCandidatePercent = task->percent;
			g_dragPxPerDay = ComputeDragPxPerDay(anchorRect, DateToDays(task->end) - DateToDays(task->start) + 1);
		} else {
			g_dragKind = (hit.zone == HtZone::TaskEdgeL) ? DragKind::TaskEdgeL
				: (hit.zone == HtZone::TaskEdgeR) ? DragKind::TaskEdgeR : DragKind::TaskBody;
			g_dragId = hit.id;
			g_dragOrigStart = task->start;
			g_dragOrigEnd = task->end;
			long spanDays = DateToDays(task->end) - DateToDays(task->start) + 1;
			g_dragPxPerDay = ComputeDragPxPerDay(anchorRect, spanDays);
			g_dragOrigPercent = task->percent;
			if (g_dragKind == DragKind::TaskBody) {
				g_dragOrigRowId = task->rowId;
				g_dragTargetRowId = task->rowId;
				g_dragTargetRowRect = anchorRect;
				for (const auto& band : g_rowBands) {
					if (band.rowId == task->rowId) { g_dragTargetRowRect = band.screenRect; break; }
				}
			}
		}
	}
	if (g_dragKind != DragKind::TaskProgress && g_dragPxPerDay <= 0.0f) { ResetDragGestureState(); return; }
	if (g_dragKind == DragKind::TaskProgress && (anchorRect.right - anchorRect.left) < 4) {
		ResetDragGestureState();
		return;
	}

	g_dragAnchorRect = anchorRect;
	g_dragLastPt = downPt;
	g_dragDeltaDays = 0;
	g_gestureActive = true;
	if (g_recActive)
		RecEmitGestureStart(RecDragKindName(g_dragKind), g_dragId, g_dragOrigRowId, downPt);
}

// px-per-day for a CREATE gesture anchored on an EmptyCell (no task rect to
// derive scale from at the anchor point itself). Reuses ComputeDragPxPerDay's
// task-scan path, then PP_PROJ projection fallback when the chart has zero tasks.
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
	if (pxPerDay <= 0.0f) {
		g_creationFailHint = L"Cannot create — chart scale unavailable";
		return;
	}

	const long screenX = downPt.x + g_windowOriginX;
	const long anchorDay = AnchorDayFromScreenX(screenX);
	g_creationFailHint.clear();

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
	if (g_recActive)
		RecEmitGestureStart("Create", "", g_createRowId, downPt);
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

static bool IsScreenYInRowBand(long screenY, const std::string& rowId) {
	if (rowId.empty()) return false;
	for (const auto& band : g_rowBands) {
		if (band.rowId == rowId && screenY >= band.screenRect.top && screenY < band.screenRect.bottom)
			return true;
	}
	return false;
}

// Group/summary rows (a row that is another row's groupId parent) are not
// valid drop targets for task body / free-text drags (v2.6.2-fix-round2 H1).
static bool IsValidTaskDropRow(const std::string& rowId) {
	if (rowId.empty()) return false;
	PpDocument doc;
	if (!Gantt_TryPeekCachedDoc(&doc)) return true;
	bool exists = false;
	for (const auto& r : doc.rows) {
		if (r.id == rowId) exists = true;
		if (r.groupId == rowId) return false; // group/summary row: invalid target
	}
	return exists;
}

static void LatchDragTargetRow(long screenY) {
	if (g_rowBands.empty()) return;
	if (!g_dragOrigRowId.empty() && IsScreenYInRowBand(screenY, g_dragOrigRowId)) {
		for (const auto& band : g_rowBands) {
			if (band.rowId == g_dragOrigRowId) {
				g_dragTargetRowId = g_dragOrigRowId;
				g_dragTargetRowRect = band.screenRect;
				return;
			}
		}
	}
	if (const RowBand* band = RowBandAtScreenY(screenY)) {
		if (IsValidTaskDropRow(band->rowId)) {
			g_dragTargetRowId = band->rowId;
			g_dragTargetRowRect = band->screenRect;
		}
	}
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
	if (IsWindowEdgeDragKind(g_dragKind)) {
		UpdateWindowEdgeDragCandidate(dx);
		RequestOverlayRepaint(); // preview-only: no document/shape write on move
		if (g_recActive) RecEmitGestureUpdate();
		return;
	}

	if (g_dragKind == DragKind::Create) {
		long newDay = g_createAnchorDay + (long)::lround((double)dx / (double)g_dragPxPerDay);
		g_createCurrentDay = newDay;
		RequestOverlayRepaint();
		if (g_recActive) RecEmitGestureUpdate();
		return;
	}

	if (g_dragKind == DragKind::LinkPort) {
		UpdateLinkDragHover(pt);
		RequestOverlayRepaint();
		if (g_recActive) RecEmitGestureUpdate();
		return;
	}

	if (g_dragKind == DragKind::TaskProgress) {
		int barW = g_dragAnchorRect.right - g_dragAnchorRect.left;
		if (barW >= 4) {
			long relX = (long)(pt.x + g_windowOriginX) - g_dragAnchorRect.left;
			int pct = (int)::lround(100.0 * (double)relX / (double)barW);
			g_dragCandidatePercent = std::max(0, std::min(100, pct));
		}
		RequestOverlayRepaint();
		if (g_recActive) RecEmitGestureUpdate();
		return;
	}

	if (g_dragKind == DragKind::Text) {
		// Points, not days: the total screen-pixel displacement since the
		// gesture anchor converted back to slide points via g_dragPxPerPt (not
		// incremental per-move deltas, so rounding never accumulates drift —
		// same reasoning as every other drag kind's day-delta). Used directly
		// as the new dx/dy for an ANCHORED text; only feeds the ghost preview
		// for a FREE text (whose commit re-homes to a whole-day cell instead —
		// see g_dragDeltaDays below).
		if (g_dragPxPerPt > 0.0f) {
			g_dragCandidateDx = g_dragOrigDx + (float)dx / g_dragPxPerPt;
			g_dragCandidateDy = g_dragOrigDy + (float)dy / g_dragPxPerPt;
		}
		if (!g_dragTextAnchored) {
			// Free text: day-delta (for its new date) + row retarget, exactly
			// like a TaskBody drag.
			g_dragDeltaDays = (g_dragPxPerDay > 0.0f) ? (long)::lround((double)dx / (double)g_dragPxPerDay) : 0;
			LatchDragTargetRow(pt.y + g_windowOriginY);
		}
		RequestOverlayRepaint();
		if (g_recActive) RecEmitGestureUpdate();
		return;
	}

	long newDelta = (g_dragPxPerDay > 0.0f) ? (long)::lround((double)dx / (double)g_dragPxPerDay) : 0;
	newDelta = ClampGestureDeltaDays(g_dragKind, g_dragOrigStart, g_dragOrigEnd, newDelta);
	newDelta = SnapGestureDeltaDays(g_dragKind, g_dragOrigStart, g_dragOrigEnd, newDelta, g_lastScale);
	g_dragDeltaDays = newDelta;

	if (g_dragKind == DragKind::TaskBody) {
		LatchDragTargetRow(pt.y + g_windowOriginY);
	}

	RequestOverlayRepaint(); // A2/task spec: repaint on each move (cheap)
	if (g_recActive) RecEmitGestureUpdate();
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
	case DragKind::Marker:
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
	if (g_recActive && !g_recSkipGestureCancel) {
		const char* kind = RecDragKindName(g_dragKind);
		const std::string id = g_dragId.empty() ? g_createRowId : g_dragId;
		RecEmitGestureCancel(kind, id);
	}
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
// targetRowId is only meaningful for DragKind::TaskBody/Text(free) (row-
// reassign); pass "" for edge/milestone/anchored-text drags where no row
// retarget applies. candidateDx/candidateDy are only meaningful for
// DragKind::Text (the final gesture-snapshot dx/dy in POINTS, see
// WM_LBUTTONUP's Text-specific recompute); every other kind ignores them.
void CommitDragGesture(DragKind kind, const std::string& id, long deltaDays, const std::string& targetRowId,
	float candidateDx, float candidateDy) {
	std::string commitTargetRowId = targetRowId;
	if (!commitTargetRowId.empty() && !IsValidTaskDropRow(commitTargetRowId))
		commitTargetRowId.clear();
	bool rowChangeRequested = (kind == DragKind::TaskBody || kind == DragKind::Text) && !commitTargetRowId.empty();
	if (kind != DragKind::Text && deltaDays == 0 && !rowChangeRequested) {
		if (g_recActive)
			RecEmitGestureCommit(RecDragKindName(kind), id, "ok", 0, "\"delta\":\"zero\"");
		return;
	}
	if (!g_app || g_mutating) return;

	g_mutating = true;
	const char* result = "fail";
	long hr = 0;
	try {
		PpDocument doc;
		if (ReadGanttDocFromSlide(g_app, &doc)) {
			bool changed = false;
			std::string selKind;
			if (kind == DragKind::Milestone) {
				// Milestones have no length; a date shift is a body-style nudge.
				changed = NudgeTask(doc, id, deltaDays); // no-op if id is not a task
				if (!changed) {
					for (auto& ms : doc.milestones) {
						if (ms.id == id) {
							// Milestones stay day-precise (unit snap is markers-only, UF-09).
							ms.date = DaysToDate(DateToDays(ms.date) + deltaDays);
							changed = true;
							break;
						}
					}
				}
				selKind = "MILESTONE";
			} else if (kind == DragKind::Marker) {
				// Markers have no length either (like milestones), but are NOT
				// tasks, so NudgeTask's task-id fallback does not apply here —
				// go straight to GanttOps' dedicated marker-date setter.
				for (const auto& mk : doc.markers) {
					if (mk.id == id) {
						long snappedDay = SnapDayToScale(DateToDays(mk.date) + deltaDays, doc.scale);
						std::string newDate = DaysToDate(snappedDay);
						changed = SetMarkerDate(doc, id, newDate);
						break;
					}
				}
				selKind = "MARKER";
			} else if (kind == DragKind::TaskBody) {
				// Row-reassign first (only if the target row actually differs
				// from the task's CURRENT row — re-read fresh, not the gesture-
				// start g_dragOrigRowId, since nothing else mutates the doc
				// between gesture start and commit so they agree anyway), then
				// the horizontal day-shift, combined into one rebuild/commit.
				bool rowChanged = false;
				if (rowChangeRequested) {
					if (const PpTask* task = FindTask(doc, id)) {
						if (task->rowId != commitTargetRowId) {
							rowChanged = MoveTaskToRow(doc, id, commitTargetRowId);
						}
					}
				}
				bool dateChanged = (deltaDays != 0) && NudgeTask(doc, id, deltaDays);
				changed = rowChanged || dateChanged;
				selKind = "TASK";
			} else if (kind == DragKind::Text) {
				// Re-read anchored-ness fresh (nothing else mutates the doc
				// between gesture start and commit, so this agrees with
				// g_dragTextAnchored anyway — same "re-read fresh" reasoning as
				// TaskBody's row-reassign above).
				const PpText* txt = nullptr;
				for (const auto& t : doc.texts) if (t.id == id) { txt = &t; break; }
				if (txt) {
					if (!txt->anchorId.empty()) {
						// Anchored: dx/dy only, no rowId/date re-homing. Suppress a
						// no-op commit (fix-percent-noop-undo's pattern: a gesture
						// that resolves to the SAME dx/dy — e.g. it never crossed
						// the click threshold in a way that changed px/pt rounding
						// — must not pollute undo).
						bool dxyChanged = (candidateDx != txt->dx) || (candidateDy != txt->dy);
						if (dxyChanged) changed = MoveText(doc, id, candidateDx, candidateDy);
					} else {
						// Free: re-home to the (possibly new) row + the day-shifted
						// date, with the residual dx/dy ZEROED (per the task spec —
						// the new position IS the cell origin, no leftover offset).
						std::string newDate = DaysToDate(DateToDays(txt->date) + deltaDays);
						std::string rowId = rowChangeRequested ? commitTargetRowId : txt->rowId;
						bool cellChanged = (rowId != txt->rowId) || (newDate != txt->date);
						bool residualChanged = (txt->dx != 0.0f) || (txt->dy != 0.0f);
						if (cellChanged || residualChanged) changed = MoveText(doc, id, 0.0f, 0.0f, rowId, newDate);
					}
				}
				selKind = "TEXT";
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
				if (kind == DragKind::TaskBody) {
					if (const PpTask* task = FindTask(doc, id))
						g_lastCommittedDragTargetRowId = task->rowId;
				} else if (kind == DragKind::Text) {
					for (const auto& t : doc.texts)
						if (t.id == id) { g_lastCommittedDragTargetRowId = t.rowId; break; }
				}
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
				result = "ok";
			} else {
				ClearDragCommitEcho();
				result = "ok"; // no-op mutation is not a failure
			}
		}
	}
	catch (const _com_error& e) {
		hr = (long)e.Error();
		OvLog(L"COM error committing drag gesture");
		RecError("CommitDragGesture", hr, "COM error");
	}
	catch (const std::exception& e) {
		hr = (long)E_FAIL;
		OvLog(L"document error committing drag gesture");
		RecError("CommitDragGesture", hr, e.what());
	}
	catch (...) {
		hr = (long)E_FAIL;
		OvLog(L"unknown error committing drag gesture");
		RecError("CommitDragGesture", hr, "unknown error");
	}
	g_mutating = false;
	if (g_recActive) {
		std::string extra = "\"deltaDays\":";
		extra += std::to_string(deltaDays);
		if (!commitTargetRowId.empty()) {
			extra += ",\"rowId\":\"";
			EntityJsonAppendEscaped(extra, commitTargetRowId);
			extra += "\"";
		}
		RecEmitGestureCommit(RecDragKindName(kind), id, result, hr, extra);
	}
}

void CommitProgressGesture(const std::string& id, int percent) {
	if (id.empty() || !g_app || g_mutating) return;
	percent = std::max(0, std::min(100, percent));
	g_mutating = true;
	const char* result = "fail";
	long hr = 0;
	try {
		PpDocument doc;
		if (ReadGanttDocFromSlide(g_app, &doc)) {
			if (SetTaskPercentValue(doc, id, percent)) {
				RebuildChart(doc, id);
				SetOwnSelection("TASK", id);
				RequestOverlayRepaint();
				result = "ok";
			}
		}
	}
	catch (const _com_error& e) {
		hr = (long)e.Error();
		OvLog(L"COM error committing progress drag");
		RecError("CommitProgressGesture", hr, "COM error");
	}
	catch (const std::exception& e) {
		hr = (long)E_FAIL;
		OvLog(L"document error committing progress drag");
		RecError("CommitProgressGesture", hr, e.what());
	}
	catch (...) {
		hr = (long)E_FAIL;
		OvLog(L"unknown error committing progress drag");
		RecError("CommitProgressGesture", hr, "unknown error");
	}
	g_mutating = false;
	if (g_recActive) {
		std::string extra = "\"percent\":";
		extra += std::to_string(percent);
		RecEmitGestureCommit("TaskProgress", id, result, hr, extra);
	}
}

// Commit a CREATE gesture on WM_LBUTTONUP: adds a new task spanning
// [startDay, endDay] (inclusive, already normalized/clamped by the caller) in
// rowId, rebuilds, and selects the new task — mirrors CommitDragGesture's A2
// synchronous-selection pattern.
void CommitCreateGesture(const std::string& rowId, long startDay, long endDay) {
	if (rowId.empty() || !g_app || g_mutating) return;
	g_mutating = true;
	const char* result = "fail";
	long hr = 0;
	std::string newId;
	try {
		PpDocument doc;
		if (ReadGanttDocFromSlide(g_app, &doc)) {
			std::string startISO = DaysToDate(startDay);
			std::string endISO = DaysToDate(endDay);
			newId = AddTask(doc, rowId, "New task", startISO, endISO);
			if (!newId.empty()) {
				RebuildChart(doc, newId);
				// A2, same reasoning as CommitDragGesture: set synchronously,
				// do not resync chrome here (stale/invalidated snapshot).
				SetOwnSelection("TASK", newId);
				g_creationFailHint.clear();
				RequestOverlayRepaint();
				result = "ok";
			} else {
				g_creationFailHint = L"Cannot create task";
			}
		}
	}
	catch (const _com_error& e) {
		hr = (long)e.Error();
		OvLog(L"COM error committing create gesture");
		RecError("CommitCreateGesture", hr, "COM error");
	}
	catch (const std::exception& e) {
		hr = (long)E_FAIL;
		OvLog(L"document error committing create gesture");
		RecError("CommitCreateGesture", hr, e.what());
	}
	catch (...) {
		hr = (long)E_FAIL;
		OvLog(L"unknown error committing create gesture");
		RecError("CommitCreateGesture", hr, "unknown error");
	}
	g_mutating = false;
	if (g_recActive) {
		std::string extra = "\"rowId\":\"";
		EntityJsonAppendEscaped(extra, rowId);
		extra += "\",\"startDay\":";
		extra += std::to_string(startDay);
		extra += ",\"endDay\":";
		extra += std::to_string(endDay);
		RecEmitGestureCommit("Create", newId, result, hr, extra);
	}
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
	// R1b: input tap at TOP (SR-REC-05). g_recActive fast-check inside.
	if (g_recActive) RecEmitInput("overlay", msg, hwnd, wp, lp);
	if (msg == WM_NCHITTEST) {
		// NOTE: there used to be an Alt escape hatch here returning
		// HTTRANSPARENT so PowerPoint could see the mouse. It was removed:
		// GetKeyState reports keyboard state as of the last message the thread
		// pulled off its queue, and WM_NCHITTEST arrives via SendMessage, so the
		// read could be arbitrarily stale. Alt+Tab latched it (the shell eats
		// the Alt-up, so the thread never retrieves it) and the overlay went
		// permanently transparent to input while still painting — a live
		// recording caught five clicks vanishing with zero events logged. See
		// docs/native-overlay-input-loss-analysis.md.
		POINT screenPt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		// The overlay captures ALL mouse input over the chart area; PowerPoint
		// never sees chart clicks. Outside the chart, only the chrome widgets
		// (toolbar, hover '+', move grip) are interactive.
		if (::PtInRect(&g_chartScreenRect, screenPt)) return HTCLIENT;
		POINT pt = screenPt;
		::ScreenToClient(hwnd, &pt);
		return (ButtonFromClientPoint(pt) >= 0 || HoverInsertFromClientPoint(pt) ||
			RowBoundaryInsertFromClientPoint(pt) >= 0 || GripFromClientPoint(pt)) ? HTCLIENT : HTTRANSPARENT;
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
		// Card click-away (SR-IXC-07): overlay is MA_NOACTIVATE so clicking the
		// chart does not steal focus from the card; commit explicitly instead
		// of relying on WM_ACTIVATE WA_INACTIVE (which never fires here).
		if (g_cardHwnd && ::IsWindowVisible(g_cardHwnd)) {
			CommitCardEdit();
			return 0;
		}
		// While an inline editor is open, the overlay itself must
		// not start a new gesture/selection-change underneath it — the
		// editor's own top-level window would lose activation anyway (which
		// cancels it), but bouncing a click through to a drag/create gesture
		// first is exactly the kind of "gesture while editor is open" the
		// task spec asks to suppress.
		if (IsEditSessionActive()) return 0;
		POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		if (ButtonFromClientPoint(pt) >= 0 || HoverInsertFromClientPoint(pt) ||
			RowBoundaryInsertFromClientPoint(pt) >= 0 || GripFromClientPoint(pt)) return 0;
		// M3: these header affordances win before semantic hit testing. Marker
		// bands intentionally span the full chart (including the header), so
		// changing their bands instead would regress marker selection elsewhere.
		const HtZone windowPort = HitTestWindowPortAtClient(pt);
		if (windowPort == HtZone::WindowPortL || windowPort == HtZone::WindowPortR) {
			OvLog(windowPort == HtZone::WindowPortL ? L"WINDOWPORT left down" : L"WINDOWPORT right down");
			g_lastHit = { windowPort };
			g_mouseDownPt = pt;
			g_mouseDownMk = wp;
			g_captureActive = true;
			::SetCapture(hwnd);
			StartWindowEdgeDrag(windowPort, pt);
			return 0;
		}
		std::string portId, portKind;
		bool portRight = false;
		if (HitTestLinkPortAtClient(pt, &portId, &portKind, &portRight)) {
			wchar_t pb[128];
			::swprintf_s(pb, 128, L"LINKPORT down id=%hs right=%d", portId.c_str(), portRight ? 1 : 0);
			OvLog(pb);
			g_lastHit = HitTestClientPoint(pt);
			g_mouseDownPt = pt;
			g_mouseDownMk = wp;
			g_captureActive = true;
			::SetCapture(hwnd);
			StartLinkPortDrag(portId, portKind, portRight, pt);
			return 0;
		}
		OvLog(L"LINKPORT down MISS (no port at point)");
		// Route everything else through the semantic hit test and stash the
		// result (kept for parity/debugging; selection itself is decided on
		// the up, from the ORIGINAL down-hit, once we know this was a click).
		g_lastHit = HitTestClientPoint(pt);
		g_mouseDownPt = pt;
		g_mouseDownMk = wp;
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
		POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		if (g_gestureActive) {
			UpdateDragGesture(pt);
		} else if (!(::GetKeyState(VK_LBUTTON) & 0x8000)) {
			// SR-SMO-04: repaint hover wash/affordances on target change only.
			// M6: track move-grip hover so PaintOverlay can surface the "Move
			// chart" tooltip (recomputed each move; also re-derived in Tick so
			// it clears when the pointer leaves the overlay entirely).
			bool overGrip = GripFromClientPoint(pt);
			if (overGrip != g_gripHover) { g_gripHover = overGrip; RequestOverlayRepaint(); }
			if (UpdateHoverFromCursor()) RequestOverlayRepaint();
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
		std::string dragOrigStart = g_dragOrigStart;
		std::string dragOrigEnd = g_dragOrigEnd;
		int dragOrigPercent = g_dragOrigPercent;
		int dragCandidatePercent = g_dragCandidatePercent;
		std::string dragTargetRowId = g_dragTargetRowId;
		std::string createRowId = g_createRowId;
		long createAnchorDay = g_createAnchorDay;
		long createCurrentDay = g_createCurrentDay;
		bool dragTextAnchored = g_dragTextAnchored;
		float dragOrigDx = g_dragOrigDx, dragOrigDy = g_dragOrigDy;
		float dragCandidateDx = g_dragCandidateDx, dragCandidateDy = g_dragCandidateDy;
		float gesturePxPerDay = g_dragPxPerDay; // snapshot: ReleaseCapture below zeroes the live global (see CREATE branch's isClick check)
		float gesturePxPerPt = g_dragPxPerPt;
		RECT dragAnchorRect = g_dragAnchorRect;
		RECT dragTargetRowRect = g_dragTargetRowRect;
		// Snapshot all W2 candidate values before ReleaseCapture. Capture release
		// synchronously routes WM_CAPTURECHANGED, which resets every g_drag*/
		// g_window* global; W3's real commit must keep this stale-global rule.
		std::string windowCandidateStart = g_windowCandidateStart;
		std::string windowCandidateEnd = g_windowCandidateEnd;
		std::string windowScale = g_windowPreviewDoc.scale;
		// Snapshot the link-drag hover target BEFORE any capture release path
		// can clear it (same reasoning as every other snapshot above), and
		// recompute from THIS up-point as the authority — the last MOUSEMOVE's
		// hover can be stale or already cleared.
		std::string linkDropTargetId = g_linkDragHoverId;
		std::string linkFromKind = g_linkFromKind;
		if (wasGestureActive && dragKind == DragKind::LinkPort) {
			HtHit upHit = HitTestClientPoint(pt);
			if ((upHit.zone == HtZone::TaskBody || upHit.zone == HtZone::TaskEdgeL ||
				upHit.zone == HtZone::TaskEdgeR || upHit.zone == HtZone::Milestone)
				&& !upHit.id.empty() && upHit.id != dragId) {
				linkDropTargetId = upHit.id;
			}
		}
		long finalDx = pt.x - g_mouseDownPt.x;
		long finalDy = pt.y - g_mouseDownPt.y;
		if (wasGestureActive && IsWindowEdgeDragKind(dragKind) && gesturePxPerDay > 0.0f) {
			ComputeWindowEdgeCandidate(dragKind, dragOrigStart, dragOrigEnd, gesturePxPerDay,
				finalDx, windowScale, &windowCandidateStart, &windowCandidateEnd);
		} else if (wasGestureActive && dragKind == DragKind::Text) {
			// Points, not days (see UpdateDragGesture's Text branch): recompute
			// the final candidate dx/dy from THIS up-point rather than trusting
			// the last MOUSEMOVE, same reasoning as every other kind's day-delta
			// recompute below.
			if (gesturePxPerPt > 0.0f) {
				dragCandidateDx = dragOrigDx + (float)finalDx / gesturePxPerPt;
				dragCandidateDy = dragOrigDy + (float)finalDy / gesturePxPerPt;
			}
			if (!dragTextAnchored && gesturePxPerDay > 0.0f) {
				dragDeltaDays = (long)::lround((double)finalDx / (double)gesturePxPerDay);
			}
		} else if (wasGestureActive && gesturePxPerDay > 0.0f) {
			if (dragKind == DragKind::Create) {
				createCurrentDay = createAnchorDay + (long)::lround((double)finalDx / (double)gesturePxPerDay);
			} else if (dragKind != DragKind::TaskProgress) {
				dragDeltaDays = (long)::lround((double)finalDx / (double)gesturePxPerDay);
				dragDeltaDays = ClampGestureDeltaDays(dragKind, dragOrigStart, dragOrigEnd, dragDeltaDays);
				dragDeltaDays = SnapGestureDeltaDays(dragKind, dragOrigStart, dragOrigEnd, dragDeltaDays, g_lastScale);
			}
		} else if (wasGestureActive && dragKind == DragKind::TaskProgress) {
			int barW = dragAnchorRect.right - dragAnchorRect.left;
			if (barW >= 4) {
				long relX = (long)(pt.x + g_windowOriginX) - dragAnchorRect.left;
				dragCandidatePercent = (int)::lround(100.0 * (double)relX / (double)barW);
				dragCandidatePercent = std::max(0, std::min(100, dragCandidatePercent));
			}
		}
		// Suppress CAPTURECHANGED cancel while we still own the gesture snapshot;
		// commit helpers emit "commit", click/cancel paths emit "cancel" below.
		if (g_captureActive) {
			g_captureActive = false;
			if (wasGestureActive) g_recSkipGestureCancel = true;
			::ReleaseCapture();
			g_recSkipGestureCancel = false;
		}
		if (GripFromClientPoint(pt)) {
			try {
				SelectChartRoot();
			} catch (...) {
				OvLog(L"move-chart grip click failed");
			}
			return 0;
		}
		const int boundaryChip = RowBoundaryInsertFromClientPoint(pt);
		if (boundaryChip >= 0) {
			try {
				HandleHoverQuickAddRow(boundaryChip == 0);
			} catch (...) {
				OvLog(L"hover quick-add row failed");
			}
			return 0;
		}
		if (HoverInsertFromClientPoint(pt)) {
			try {
				HandleHoverQuickAddTask();
			} catch (...) {
				OvLog(L"hover quick-add task failed");
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
			if (wasGestureActive && !wasDragActive && IsWindowEdgeDragKind(dragKind)) {
				// Plain click on a window port: step this edge outward one unit.
				// Snapshot locals only (Reset/release already ran, see above).
				ResetDragGestureState();
				std::string stepStart = dragOrigStart, stepEnd = dragOrigEnd;
				ComputeWindowExpandStep(dragKind, dragOrigStart, dragOrigEnd,
					windowScale, &stepStart, &stepEnd);
				OvLog(dragKind == DragKind::WindowEdgeL
					? L"WINDOWPORT left click -> widen" : L"WINDOWPORT right click -> widen");
				try {
					CommitWindowGesture(stepStart, stepEnd, dragOrigStart, dragOrigEnd);
				} catch (...) {
					OvLog(L"window port click commit failed");
					RecError("CommitWindowGesture/click", (long)E_FAIL, "exception");
				}
				return 0;
			}
			if (wasGestureActive && wasDragActive && IsWindowEdgeDragKind(dragKind)) {
				// Snapshot locals above are the sole source after Reset/release.
				// dragOrigStart/End carry the gesture BASELINE (StartWindowEdgeDrag
				// set them to the doc window, or the D2 in-memory auto-fit
				// baseline) so the commit can detect a zero-delta release (m9).
				ResetDragGestureState();
				try {
					CommitWindowGesture(windowCandidateStart, windowCandidateEnd,
						dragOrigStart, dragOrigEnd);
				} catch (...) {
					OvLog(L"window gesture commit failed");
					RecError("CommitWindowGesture/call", (long)E_FAIL, "exception");
				}
			} else if (wasGestureActive && wasDragActive && dragKind == DragKind::Create) {
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
					if (g_recActive)
						RecEmitGestureCancel("Create", createRowId);
					HtHit hit = HitTestClientPoint(g_mouseDownPt);
					const bool ctrlDown = ((g_mouseDownMk & MK_CONTROL) != 0) ||
						((::GetKeyState(VK_CONTROL) & 0x8000) != 0);
					const bool shiftDown = ((g_mouseDownMk & MK_SHIFT) != 0) ||
						((::GetKeyState(VK_SHIFT) & 0x8000) != 0);
					ApplyClickSelection(hit, ctrlDown, shiftDown);
					SyncSelectionChromeFromOwnSelection();
					RequestOverlayRepaint();
				} else {
					long startDay = createAnchorDay, endDay = createCurrentDay;
					if (endDay < startDay) std::swap(startDay, endDay); // allow dragging left
					try {
						CommitCreateGesture(createRowId, startDay, endDay);
					} catch (...) {
						OvLog(L"create gesture commit failed");
						RecError("CommitCreateGesture/call", (long)E_FAIL, "exception");
					}
				}
			} else if (wasGestureActive && wasDragActive && dragKind == DragKind::LinkPort) {
				wchar_t db[128];
				::swprintf_s(db, 128, L"LINKPORT drop target=%hs", linkDropTargetId.empty() ? "(none)" : linkDropTargetId.c_str());
				OvLog(db);
				ResetDragGestureState();
				try {
					CommitLinkPortDrop(dragId, linkFromKind, linkDropTargetId);
				} catch (...) {
					OvLog(L"link-port drag commit failed");
					RecError("CommitLinkPortDrop/call", (long)E_FAIL, "exception");
				}
			} else if (wasGestureActive && wasDragActive && dragKind == DragKind::TaskProgress) {
				try {
					if (dragCandidatePercent != dragOrigPercent) {
						CommitProgressGesture(dragId, dragCandidatePercent);
					} else if (g_recActive) {
						RecEmitGestureCommit("TaskProgress", dragId, "ok", 0, "\"delta\":\"zero\"");
					}
				} catch (...) {
					OvLog(L"progress drag commit failed");
					RecError("CommitProgressGesture/call", (long)E_FAIL, "exception");
				}
				ResetDragGestureState();
			} else if (wasGestureActive && wasDragActive) {
				try {
					bool rowChangeRequested = (dragKind == DragKind::TaskBody || dragKind == DragKind::Text) && !dragTargetRowId.empty();
					if (dragKind == DragKind::Text || dragDeltaDays != 0 || rowChangeRequested) {
						ArmDragCommitEcho(dragKind, dragAnchorRect, dragDeltaDays, gesturePxPerDay,
							dragTargetRowRect, dragTextAnchored, dragOrigDx, dragOrigDy,
							dragCandidateDx, dragCandidateDy, gesturePxPerPt);
					}
					CommitDragGesture(dragKind, dragId, dragDeltaDays, dragTargetRowId, dragCandidateDx, dragCandidateDy);
				} catch (...) {
					OvLog(L"drag gesture commit failed");
					ClearDragCommitEcho();
					RecError("CommitDragGesture/call", (long)E_FAIL, "exception");
				}
				ResetDragGestureState();
			} else {
				// Gesture started but never crossed drag threshold: cancel (not commit).
				if (g_recActive && wasGestureActive)
					RecEmitGestureCancel(RecDragKindName(dragKind),
						dragId.empty() ? createRowId : dragId);
				ResetDragGestureState();
				long dx = pt.x - g_mouseDownPt.x;
				long dy = pt.y - g_mouseDownPt.y;
				bool isClick = (dx >= -kDragThresholdPx && dx <= kDragThresholdPx &&
					dy >= -kDragThresholdPx && dy <= kDragThresholdPx);
				if (isClick) {
					HtHit hit = HitTestClientPoint(g_mouseDownPt);
					const bool ctrlDown = ((g_mouseDownMk & MK_CONTROL) != 0) ||
						((::GetKeyState(VK_CONTROL) & 0x8000) != 0);
					const bool shiftDown = ((g_mouseDownMk & MK_SHIFT) != 0) ||
						((::GetKeyState(VK_SHIFT) & 0x8000) != 0);
					if (g_linkMode) {
						if (hit.zone == HtZone::TaskBody || hit.zone == HtZone::TaskEdgeL ||
							hit.zone == HtZone::TaskEdgeR || hit.zone == HtZone::Milestone) {
							if (!hit.id.empty()) {
								CommitLinkTarget(hit.id);
							} else {
								ClearLinkMode();
								ApplyClickSelection(hit, ctrlDown, shiftDown);
								SyncSelectionChromeFromOwnSelection();
							}
						} else {
							ClearLinkMode();
							ApplyClickSelection(hit, ctrlDown, shiftDown);
							SyncSelectionChromeFromOwnSelection();
						}
						RequestOverlayRepaint();
					} else {
						ApplyClickSelection(hit, ctrlDown, shiftDown);
						SyncSelectionChromeFromOwnSelection();
						RequestOverlayRepaint();
					}
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
		ApplyClickSelection(hit, false, false);
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
		// SR-SMO-07: labels rename inline (task/milestone/marker/note/row/title).
		if (const EditRegion* region = EditRegionFromClientPoint(pt)) {
			OpenInlineEditor(*region);
			return 0;
		}
		HtHit hit = HitTestClientPoint(pt);
		if (hit.zone == HtZone::TaskBody) {
			try {
				if (TryOpenInlineRename("TASK", hit.id)) return 0;
			} catch (...) {
				OvLog(L"double-click task inline rename failed");
			}
		}
		if (hit.zone == HtZone::TaskEdgeL || hit.zone == HtZone::TaskEdgeR ||
			hit.zone == HtZone::TaskProgressEdge ||
			hit.zone == HtZone::Milestone || hit.zone == HtZone::Marker || hit.zone == HtZone::Text) {
			// The floating card editor (V2): double-clicking a bar, milestone,
			// marker, or text annotation opens the richer editor instead of the
			// simpler TITLE/ROW_LABEL inline box (TEXT uses the same card in a
			// reduced "text mode" — label field + delete button only, see
			// OpenCardEditor). Resolve the item's CURRENT screen rect from the
			// hit snapshot (same source g_lastHit/drag-start use) so the card
			// anchors to where the item actually is now.
			const char* wantKindStr = (hit.zone == HtZone::Milestone) ? "MILESTONE"
				: (hit.zone == HtZone::Marker) ? "MARKER"
				: (hit.zone == HtZone::Text) ? "TEXT" : "TASK";
			HtItemKind wantItemKind = (hit.zone == HtZone::Milestone) ? HtItemKind::Milestone
				: (hit.zone == HtZone::Marker) ? HtItemKind::Marker
				: (hit.zone == HtZone::Text) ? HtItemKind::Text : HtItemKind::Task;
			for (const auto& item : g_hitSnapshot.items) {
				if (item.kind == wantItemKind && item.id == hit.id) {
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
		if (hit.zone == HtZone::EmptyCell && !hit.rowId.empty()) {
			// Async keystate: GetKeyState is only current as of the last message
			// this thread dequeued, which made the modifier read unreliable.
			const bool altDown = ((::GetAsyncKeyState(VK_MENU) & 0x8000) != 0) ||
				((::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 && (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
			HtMenuOp op{};
			op.opKind = altDown ? HtOpKind::AddMilestoneAtPoint : HtOpKind::AddTaskAtPoint;
			try {
				HandleContextMenuCommand(op, hit, pt);
			} catch (...) {
				OvLog(L"empty-cell double-click create failed");
			}
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
		{
			wchar_t hb[64];
			::swprintf_s(hb, 64, L"WM_HOTKEY wp=%d", (int)wp);
			OvLog(hb);
		}
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
	if (msg == WM_ERASEBKGND) return 1;
	if (msg == WM_PAINT && g_gdiplusToken) {
		PAINTSTRUCT ps{};
		::BeginPaint(hwnd, &ps);
		RECT rc{};
		::GetClientRect(hwnd, &rc);
		const int w = rc.right - rc.left;
		const int h = rc.bottom - rc.top;
		if (w > 0 && h > 0) {
			Gdiplus::Bitmap bmp(w, h, PixelFormat32bppPARGB);
			Gdiplus::Graphics g(&bmp);
			g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
			Gdiplus::GraphicsPath path;
			AddRoundRect(path, 0.0f, 0.0f, (Gdiplus::REAL)w, (Gdiplus::REAL)h, (Gdiplus::REAL)Scale(6));
			Gdiplus::SolidBrush surface(GpToken(255, gt::surface));
			g.FillPath(&surface, &path);
			Gdiplus::Pen border(GpToken(255, gt::primary), 1.5f);
			g.DrawPath(&border, &path);
			Gdiplus::Graphics screen(ps.hdc);
			screen.DrawImage(&bmp, 0, 0);
		}
		::EndPaint(hwnd, &ps);
		return 0;
	}
	if (msg == WM_SIZE && g_editHwnd) {
		RECT rc;
		::GetClientRect(hwnd, &rc);
		::MoveWindow(g_editHwnd, Scale(2), Scale(2), rc.right - rc.left - Scale(4), rc.bottom - rc.top - Scale(4), TRUE);
		return 0;
	}
	if (msg == WM_CTLCOLOREDIT) {
		HDC dc = (HDC)wp;
		::SetBkColor(dc, AbRgb(gt::surface));
		::SetTextColor(dc, AbRgb(gt::ink));
		static HBRUSH editBrush = ::CreateSolidBrush(AbRgb(gt::surface));
		return (LRESULT)editBrush;
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
	if (msg == WM_SETFOCUS) {
		g_cardFocusHwnd = hwnd;
		if (g_cardHwnd) ::InvalidateRect(g_cardHwnd, NULL, FALSE);
	}
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
	if (g_recActive) RecEmitInput("card", msg, hwnd, wp, lp);
	if (msg == WM_CLOSE) {
		CancelCardEdit();
		return 0;
	}
	if (msg == WM_ERASEBKGND) return 1;
	if (msg == WM_PAINT && g_gdiplusToken) {
		PAINTSTRUCT ps{};
		::BeginPaint(hwnd, &ps);
		RECT rc{};
		::GetClientRect(hwnd, &rc);
		const int w = rc.right - rc.left;
		const int h = rc.bottom - rc.top;
		if (w > 0 && h > 0) {
			Gdiplus::Bitmap bmp(w, h, PixelFormat32bppPARGB);
			Gdiplus::Graphics g(&bmp);
			g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
			Gdiplus::GraphicsPath shadowPath;
			AddRoundRect(shadowPath, 1.0f, 2.0f, (Gdiplus::REAL)(w - 2), (Gdiplus::REAL)(h - 2), (Gdiplus::REAL)Scale(10));
			Gdiplus::SolidBrush shadow(GpToken(40, gt::ink3));
			g.FillPath(&shadow, &shadowPath);
			Gdiplus::GraphicsPath path;
			AddRoundRect(path, 0.0f, 0.0f, (Gdiplus::REAL)w, (Gdiplus::REAL)h, (Gdiplus::REAL)Scale(10));
			Gdiplus::SolidBrush surface(GpToken(255, gt::surface));
			g.FillPath(&surface, &path);
			Gdiplus::Pen border(GpToken(255, gt::outline), 1.0f);
			g.DrawPath(&border, &path);
			// The card fields are real EDIT children for standard keyboard and
			// accessibility behaviour. Their small captions live on the themed
			// card surface in the reserved left column so date/progress values
			// remain understandable at first sight (SR-IXC/N1).
			Gdiplus::Font captionFont(L"Segoe UI", ScaleF(10.0f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
			Gdiplus::SolidBrush captionBrush(GpToken(255, gt::ink3));
			Gdiplus::StringFormat captionFormat;
			captionFormat.SetAlignment(Gdiplus::StringAlignmentFar);
			captionFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
			auto paintCaption = [&](HWND field, const wchar_t* text) {
				if (!field || !::IsWindowVisible(field)) return;
				RECT fieldRect{};
				::GetWindowRect(field, &fieldRect);
				::MapWindowPoints(HWND_DESKTOP, hwnd, (POINT*)&fieldRect, 2);
				const int left = Scale(kBaseCardPad);
				const int right = fieldRect.left - Scale(5);
				if (right <= left) return;
				g.DrawString(text, -1, &captionFont,
					Gdiplus::RectF((Gdiplus::REAL)left, (Gdiplus::REAL)fieldRect.top,
						(Gdiplus::REAL)(right - left), (Gdiplus::REAL)(fieldRect.bottom - fieldRect.top)),
					&captionFormat, &captionBrush);
			};
			paintCaption(g_cardStartHwnd, L"Start");
			paintCaption(g_cardEndHwnd, L"End");
			paintCaption(g_cardPercentHwnd, L"Progress %");
			if (g_cardFocusHwnd && ::IsWindow(g_cardFocusHwnd)) {
				RECT fr{};
				::GetWindowRect(g_cardFocusHwnd, &fr);
				::MapWindowPoints(HWND_DESKTOP, hwnd, (POINT*)&fr, 2);
				::InflateRect(&fr, Scale(2), Scale(2));
				Gdiplus::GraphicsPath ring;
				AddRoundRect(ring, (Gdiplus::REAL)fr.left, (Gdiplus::REAL)fr.top,
					(Gdiplus::REAL)(fr.right - fr.left), (Gdiplus::REAL)(fr.bottom - fr.top), (Gdiplus::REAL)Scale(4));
				Gdiplus::Pen focus(GpToken(255, gt::primary), 1.5f);
				g.DrawPath(&focus, &ring);
			}
			Gdiplus::Graphics screen(ps.hdc);
			screen.DrawImage(&bmp, 0, 0);
		}
		::EndPaint(hwnd, &ps);
		return 0;
	}
	if (msg == WM_DRAWITEM && g_gdiplusToken) {
		const DRAWITEMSTRUCT* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
		if (!draw || draw->CtlType != ODT_BUTTON || !draw->hDC) return FALSE;
		const int id = (int)draw->CtlID;
		const bool isSwatch = id >= CARD_ID_SWATCH_BASE && id < CARD_ID_SWATCH_BASE + kCardSwatchCount;
		const bool isSave = id == CARD_ID_OK;
		const bool isDelete = id == CARD_ID_DELETE;
		if (!isSwatch && !isSave && !isDelete) return FALSE;

		const RECT& rc = draw->rcItem;
		const int width = rc.right - rc.left;
		const int height = rc.bottom - rc.top;
		if (width <= 0 || height <= 0) return TRUE;

		Gdiplus::Graphics g(draw->hDC);
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		Gdiplus::SolidBrush surface(GpToken(255, gt::surface));
		g.FillRectangle(&surface, rc.left, rc.top, width, height);
		const int inset = Scale(1);
		Gdiplus::GraphicsPath buttonPath;
		AddRoundRect(buttonPath, (Gdiplus::REAL)(rc.left + inset), (Gdiplus::REAL)(rc.top + inset),
			(Gdiplus::REAL)std::max(1, width - inset * 2), (Gdiplus::REAL)std::max(1, height - inset * 2),
			(Gdiplus::REAL)Scale(isSwatch ? 4 : 6));

		if (isSwatch) {
			const int index = id - CARD_ID_SWATCH_BASE;
			const unsigned long fillRgb = gt::ParseHexColor(kAppBarSwatches[index], gt::swatch1);
			Gdiplus::SolidBrush fill(GpToken(255, fillRgb));
			g.FillPath(&fill, &buttonPath);
			if (g_cardSelectedSwatch == index) {
				Gdiplus::Pen ring(GpToken(255, gt::primary), 2.0f);
				g.DrawPath(&ring, &buttonPath);
			}
		} else {
			const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
			const unsigned long fillRgb = isSave
				? (pressed ? gt::primaryDim : gt::primary)
				: (pressed ? gt::deadline : gt::dangerSoft);
			const unsigned long textRgb = isSave ? gt::surface : gt::deadline;
			Gdiplus::SolidBrush fill(GpToken(255, fillRgb));
			g.FillPath(&fill, &buttonPath);
			if (isDelete && !pressed) {
				Gdiplus::Pen dangerOutline(GpToken(255, gt::deadline), 1.0f);
				g.DrawPath(&dangerOutline, &buttonPath);
			}
			Gdiplus::Font buttonFont(L"Segoe UI", ScaleF(12.0f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
			Gdiplus::SolidBrush textBrush(GpToken(255, textRgb));
			Gdiplus::StringFormat textFormat;
			textFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
			textFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
			const wchar_t* label = isSave ? L"Save" : L"Delete";
			g.DrawString(label, -1, &buttonFont,
				Gdiplus::RectF((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
					(Gdiplus::REAL)width, (Gdiplus::REAL)height), &textFormat, &textBrush);
		}
		return TRUE;
	}
	if (msg == WM_COMMAND) {
		int id = LOWORD(wp);
		int notify = HIWORD(wp);
		if (id == CARD_ID_OK && notify == BN_CLICKED) {
			CommitCardEdit();
			return 0;
		}
		if (id == CARD_ID_DELETE && notify == BN_CLICKED) {
			CommitCardDelete();
			return 0;
		}
		if (id >= CARD_ID_SWATCH_BASE && id < CARD_ID_SWATCH_BASE + kCardSwatchCount && notify == BN_CLICKED) {
			g_cardSelectedSwatch = id - CARD_ID_SWATCH_BASE;
			for (int i = 0; i < kCardSwatchCount; ++i) {
				if (g_cardSwatchHwnd[i]) ::InvalidateRect(g_cardSwatchHwnd[i], NULL, FALSE);
			}
			return 0;
		}
	}
	if (msg == WM_CTLCOLOREDIT) {
		HDC dc = (HDC)wp;
		HWND ctl = (HWND)lp;
		::SetBkColor(dc, AbRgb(gt::surface));
		::SetTextColor(dc, AbRgb(gt::ink));
		if (g_cardInvalid && (ctl == g_cardStartHwnd || ctl == g_cardEndHwnd)) {
			::SetBkColor(dc, RGB(0xFD, 0xE7, 0xE9));
			static HBRUSH invalidBrush = ::CreateSolidBrush(RGB(0xFD, 0xE7, 0xE9));
			return (LRESULT)invalidBrush;
		}
		static HBRUSH editBrush = ::CreateSolidBrush(AbRgb(gt::surface));
		return (LRESULT)editBrush;
	}
	if (msg == WM_CTLCOLORSTATIC) {
		HDC dc = (HDC)wp;
		::SetBkColor(dc, AbRgb(gt::surface));
		::SetTextColor(dc, AbRgb(gt::deadline));
		static HBRUSH staticBrush = ::CreateSolidBrush(AbRgb(gt::surface));
		return (LRESULT)staticBrush;
	}
	if (msg == WM_CTLCOLORBTN) {
		HDC dc = (HDC)wp;
		::SetBkColor(dc, AbRgb(gt::surface));
		::SetTextColor(dc, AbRgb(gt::ink));
		static HBRUSH btnBrush = ::CreateSolidBrush(AbRgb(gt::surface));
		return (LRESULT)btnBrush;
	}
	// Losing activation (focus leaving the card entirely — clicking the
	// slide, Alt+Tab, etc.) commits per SR-IXC-07 (same as inline editor
	// click-away). Esc still cancels via CardFieldProc.
	if (msg == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE && !g_cardClosing) {
		CommitCardEdit();
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
	// No WS_EX_TOPMOST: the chrome is OWNED by PowerPoint's root window
	// instead (see EnsureChromeOwner). An owned popup always sits above its
	// owner and follows it when the owner is minimized or loses activation,
	// which is what we actually want. Topmost-plus-unowned made the chrome
	// float above unrelated applications whenever the 150ms Tick that hides
	// it was delayed — it was observed over Chrome, and two earlier
	// "overlay over a fullscreen game" reports were patched in the polling
	// path rather than here.
	g_hwnd = ::CreateWindowExW(
		WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
		kClass, L"", WS_POPUP,
		0, 0, 10, 10, ::GetAncestor(g_pptHwnd, GA_ROOT), NULL, g_inst, NULL);
	static bool themeMenuInited = false;
	if (!themeMenuInited && g_gdiplusToken) {
		ThemeMenu_Init(g_inst, g_gdiplusToken, Scale, RecEmitInput);
		themeMenuInited = true;
	}
}

// ---- premultiplied-alpha painting (GDI+) -----------------------------------

// Shared ghost renderer for live drags and the post-commit echo (SR-SMO-06).
static void PaintTaskBarGhost(Gdiplus::Graphics& g, const RECT& ghostClient, int progressPercent, bool livePreview) {
	using namespace Gdiplus;
	GraphicsPath ghostPath;
	AddRoundRect(ghostPath, (REAL)ghostClient.left, (REAL)ghostClient.top,
		(REAL)(ghostClient.right - ghostClient.left), (REAL)(ghostClient.bottom - ghostClient.top), 3.0f);
	SolidBrush trackFill(GpColor(livePreview ? 110 : 140, ACCENT));
	g.FillPath(&trackFill, &ghostPath);
	if (progressPercent > 0) {
		int barW = ghostClient.right - ghostClient.left;
		int pw = barW * progressPercent / 100;
		if (pw > 2) {
			GraphicsPath progPath;
			AddRoundRect(progPath, (REAL)ghostClient.left, (REAL)ghostClient.top,
				(REAL)pw, (REAL)(ghostClient.bottom - ghostClient.top), 3.0f);
			SolidBrush progFill(GpColor(livePreview ? 180 : 220, ACCENT));
			g.FillPath(&progFill, &progPath);
		}
	}
	Pen ghostPen(GpColor(livePreview ? 220 : 255, ACCENT), 1.5f);
	g.DrawPath(&ghostPen, &ghostPath);
}

static void PaintDragGhostShape(Gdiplus::Graphics& g, DragKind kind, const RECT& anchorClient, const RECT& ghostClient,
	int progressPercent = 0) {
	using namespace Gdiplus;
	if (kind == DragKind::Milestone) {
		int cx = (ghostClient.left + ghostClient.right) / 2;
		int cy = (anchorClient.top + anchorClient.bottom) / 2;
		int rx = (anchorClient.right - anchorClient.left) / 2;
		int ry = (anchorClient.bottom - anchorClient.top) / 2;
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
	} else if (kind == DragKind::Marker) {
		int cx = (ghostClient.left + ghostClient.right) / 2;
		Pen ghostPen(GpColor(220, ACCENT), 2.0f);
		g.DrawLine(&ghostPen, cx, anchorClient.top, cx, anchorClient.bottom);
	} else if (kind == DragKind::TaskProgress) {
		PaintTaskBarGhost(g, ghostClient, progressPercent, true);
	} else if (kind == DragKind::TaskBody || kind == DragKind::TaskEdgeL || kind == DragKind::TaskEdgeR) {
		PaintTaskBarGhost(g, ghostClient, progressPercent, true);
	} else {
		GraphicsPath ghostPath;
		AddRoundRect(ghostPath, (REAL)ghostClient.left, (REAL)ghostClient.top,
			(REAL)(ghostClient.right - ghostClient.left), (REAL)(ghostClient.bottom - ghostClient.top), 3.0f);
		SolidBrush ghostFill(GpColor(110, ACCENT));
		g.FillPath(&ghostFill, &ghostPath);
		Pen ghostPen(GpColor(220, ACCENT), 1.5f);
		g.DrawPath(&ghostPen, &ghostPath);
	}
}

static RECT ComputeDragGhostClientRect(DragKind kind, const RECT& anchorScreen, long deltaDays, float pxPerDay,
	const RECT& targetRowScreen, bool textAnchored, float origDx, float origDy, float candidateDx, float candidateDy, float pxPerPt) {
	RECT anchor = {
		anchorScreen.left - g_windowOriginX,
		anchorScreen.top - g_windowOriginY,
		anchorScreen.right - g_windowOriginX,
		anchorScreen.bottom - g_windowOriginY
	};
	long shiftPx = (pxPerDay > 0.0f) ? (long)::lround((double)deltaDays * (double)pxPerDay) : 0;
	long textShiftPxX = 0, textShiftPxY = 0;
	if (kind == DragKind::Text && pxPerPt > 0.0f) {
		textShiftPxX = (long)::lround((double)(candidateDx - origDx) * (double)pxPerPt);
		textShiftPxY = (long)::lround((double)(candidateDy - origDy) * (double)pxPerPt);
	}
	RECT ghost = anchor;
	switch (kind) {
	case DragKind::Text:
		ghost.left = anchor.left + textShiftPxX;
		ghost.right = anchor.right + textShiftPxX;
		ghost.top = anchor.top + textShiftPxY;
		ghost.bottom = anchor.bottom + textShiftPxY;
		if (!textAnchored && !::IsRectEmpty(&targetRowScreen)) {
			int bandTop = targetRowScreen.top - g_windowOriginY;
			int bandBottom = targetRowScreen.bottom - g_windowOriginY;
			int h = anchor.bottom - anchor.top;
			int cy = (bandTop + bandBottom) / 2;
			ghost.top = cy - h / 2;
			ghost.bottom = ghost.top + h;
		}
		break;
	case DragKind::TaskEdgeL:
		ghost.left = anchor.left + shiftPx;
		if (ghost.left > ghost.right - 2) ghost.left = ghost.right - 2;
		break;
	case DragKind::TaskEdgeR:
		ghost.right = anchor.right + shiftPx;
		if (ghost.right < ghost.left + 2) ghost.right = ghost.left + 2;
		break;
	case DragKind::TaskBody:
		ghost.left = anchor.left + shiftPx;
		ghost.right = anchor.right + shiftPx;
		if (!::IsRectEmpty(&targetRowScreen)) {
			int bandTop = targetRowScreen.top - g_windowOriginY;
			int bandBottom = targetRowScreen.bottom - g_windowOriginY;
			int h = anchor.bottom - anchor.top;
			int cy = (bandTop + bandBottom) / 2;
			ghost.top = cy - h / 2;
			ghost.bottom = ghost.top + h;
		}
		break;
	case DragKind::Milestone:
	case DragKind::Marker:
	default:
		ghost.left = anchor.left + shiftPx;
		ghost.right = anchor.right + shiftPx;
		break;
	}
	return ghost;
}

static void PaintRowBandSelectionWash(Gdiplus::Graphics& g, const std::string& rowId) {
	for (const auto& band : g_rowBands) {
		if (band.rowId != rowId) continue;
		RECT bandClient = {
			band.screenRect.left - g_windowOriginX,
			band.screenRect.top - g_windowOriginY,
			band.screenRect.right - g_windowOriginX,
			band.screenRect.bottom - g_windowOriginY
		};
		Gdiplus::SolidBrush wash(GpToken(gt::chromeRowWashSelect, gt::primary));
		g.FillRectangle(&wash, (INT)bandClient.left, (INT)bandClient.top,
			(INT)(bandClient.right - bandClient.left), (INT)(bandClient.bottom - bandClient.top));
		break;
	}
}

static void PaintOwnSelItemFrame(Gdiplus::Graphics& g, const std::string& kind, const std::string& id) {
	if (!IsItemOwnSelKind(kind) || id.empty()) return;
	const HtItemKind wantKind = OwnSelKindToHtItemKind(kind);
	for (const auto& item : g_hitSnapshot.items) {
		if (item.kind != wantKind || item.id != id) continue;
		const Gdiplus::REAL fx = (Gdiplus::REAL)(item.rect.left - g_windowOriginX);
		const Gdiplus::REAL fy = (Gdiplus::REAL)(item.rect.top - g_windowOriginY);
		const Gdiplus::REAL fw = (Gdiplus::REAL)(item.rect.right - item.rect.left);
		const Gdiplus::REAL fh = (Gdiplus::REAL)(item.rect.bottom - item.rect.top);
		Gdiplus::Pen framePen(GpToken(255, gt::primary), ScaleF(gt::chromeItemFramePx));
		g.DrawRectangle(&framePen, (INT)fx, (INT)fy, (INT)fw, (INT)fh);
		break;
	}
}

void PaintOverlay(Gdiplus::Graphics& g, int W, int H) {
	g_recIndicatorPainted = false;
	using namespace Gdiplus;
	// Mouse-capture layer over the chart.
	//
	// The caller clears to alpha 0, and a layered window pushed with ULW_ALPHA
	// is hit-tested against its per-pixel alpha: where alpha is 0 the window is
	// click-through and never receives the message at all — WM_NCHITTEST is not
	// even sent. That made the overlay interactive ONLY where it happened to
	// paint something (the left rail), while the whole plot area fell through to
	// the PowerPoint shapes underneath, contradicting the hit-test's own
	// contract ("the overlay captures ALL mouse input over the chart area").
	// Reported live 2026-07-18: hover worked only over the left panel, and
	// clicks anywhere else selected the native shape or its text.
	//
	// Alpha 1 is the lowest value that keeps the pixel hit-testable and is
	// visually indistinguishable from fully transparent. RGB must stay 0 for
	// PixelFormat32bppPARGB (premultiplied: channels must not exceed alpha).
	if (!::IsRectEmpty(&g_chartScreenRect)) {
		const int cl = g_chartScreenRect.left - g_windowOriginX;
		const int ct = g_chartScreenRect.top - g_windowOriginY;
		const int cw = g_chartScreenRect.right - g_chartScreenRect.left;
		const int ch = g_chartScreenRect.bottom - g_chartScreenRect.top;
		SolidBrush capture(Color(1, 0, 0, 0));
		g.FillRectangle(&capture, cl, ct, cw, ch);
	}

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

		// M6 (v2.6.1, SR-IXC-15): "Move chart" tooltip on hover makes the move
		// affordance discoverable and documents the Alt+click drill escape
		// hatch, so Alt+click is no longer the sole discovery path. Themed pill
		// (ink bg / surface text, per SR-THEME-01), positioned just below the
		// grip and right-aligned to it so it stays inside the overlay.
		if (g_gripHover) {
			const wchar_t* tip = L"Move chart  \u00B7  Alt+click";
			Gdiplus::Font tipFont(L"Segoe UI", g_tooltipFontPx, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
			Gdiplus::RectF tb;
			g.MeasureString(tip, -1, &tipFont, Gdiplus::PointF(0, 0), &tb);
			const int pad = g_tooltipPad;
			const int tw = (int)tb.Width + pad * 2;
			const int th = (int)tb.Height + pad * 2;
			int tx = g_gripRect.right - tw;          // right-aligned to grip
			int ty = g_gripRect.bottom + Scale(4);   // just below the grip chip
			if (tx < INFL) tx = INFL;                // keep inside the overlay
			Gdiplus::GraphicsPath tipPath;
			AddRoundRect(tipPath, (Gdiplus::REAL)tx, (Gdiplus::REAL)ty, (Gdiplus::REAL)tw, (Gdiplus::REAL)th, 3.0f);
			Gdiplus::SolidBrush tipBg(GpToken(255, gt::ink));
			g.FillPath(&tipBg, &tipPath);
			Gdiplus::StringFormat tipFmt;
			tipFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
			tipFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
			Gdiplus::SolidBrush tipText(GpToken(255, gt::surface));
			g.DrawString(tip, -1, &tipFont, Gdiplus::RectF((Gdiplus::REAL)tx, (Gdiplus::REAL)ty, (Gdiplus::REAL)tw, (Gdiplus::REAL)th), &tipFmt, &tipText);
		}
	}

	if (!g_hoverRowId.empty() && !::IsRectEmpty(&g_hoverBandRect) && !(::GetKeyState(VK_LBUTTON) & 0x8000)) {
		RECT band = {
			g_hoverBandRect.left - g_windowOriginX,
			g_hoverBandRect.top - g_windowOriginY,
			g_hoverBandRect.right - g_windowOriginX,
			g_hoverBandRect.bottom - g_windowOriginY
		};
		SolidBrush wash(GpToken(gt::chromeRowWashHover, gt::primary));
		g.FillRectangle(&wash, (INT)band.left, (INT)band.top,
			(INT)(band.right - band.left), (INT)(band.bottom - band.top));

		if (g_hoverInsertValid) {
			INT ex = (INT)g_hoverInsertRect.left, ey = (INT)g_hoverInsertRect.top;
			INT ew = (INT)(g_hoverInsertRect.right - g_hoverInsertRect.left);
			INT eh = (INT)(g_hoverInsertRect.bottom - g_hoverInsertRect.top);
			SolidBrush plusFill(GpToken(255, gt::surface));
			g.FillEllipse(&plusFill, ex, ey, ew, eh);
			Pen plusPen(GpToken(255, gt::primary), 1.0f);
			g.DrawEllipse(&plusPen, ex, ey, ew, eh);

			Pen glyphPen(GpToken(255, gt::primary), ScaleF(2.0f));
			int cx = (g_hoverInsertRect.left + g_hoverInsertRect.right) / 2;
			int cy = (g_hoverInsertRect.top + g_hoverInsertRect.bottom) / 2;
			int g4 = Scale(4);
			g.DrawLine(&glyphPen, cx - g4, cy, cx + g4, cy);
			g.DrawLine(&glyphPen, cx, cy - g4, cx, cy + g4);
		}
		for (int bi = 0; bi < 2; ++bi) {
			if (!g_rowBoundaryInsertValid[bi]) continue;
			const RECT& chip = g_rowBoundaryInsertRects[bi];
			INT ex = (INT)chip.left, ey = (INT)chip.top;
			INT ew = (INT)(chip.right - chip.left);
			INT eh = (INT)(chip.bottom - chip.top);
			SolidBrush plusFill(GpToken(255, gt::surface));
			g.FillEllipse(&plusFill, ex, ey, ew, eh);
			Pen plusPen(GpToken(255, gt::primary), 1.0f);
			g.DrawEllipse(&plusPen, ex, ey, ew, eh);
			Pen glyphPen(GpToken(255, gt::primary), ScaleF(2.0f));
			int cx = (chip.left + chip.right) / 2;
			int cy = (chip.top + chip.bottom) / 2;
			int g4 = Scale(3);
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

		int bandH = band.bottom - band.top;
		int barH = std::max(4, (int)::lround((double)bandH * (double)kRowBarHeightFrac));
		int barTop = band.top + (bandH - barH) / 2;
		RECT barGhost = { ghostLeft, barTop, ghostRight, barTop + barH };
		g_dragPreviewRect = {
			barGhost.left + g_windowOriginX, barGhost.top + g_windowOriginY,
			barGhost.right + g_windowOriginX, barGhost.bottom + g_windowOriginY
		};
		PaintTaskBarGhost(g, barGhost, 0, true);

		std::wstring tip = Widen(DaysToDate(lowDay)) + L" \u2192 " + Widen(DaysToDate(highDay));
		g_dragPillText = Narrow(tip.c_str());
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
	} else if (g_gestureActive && g_dragActive && !IsWindowEdgeDragKind(g_dragKind)) {
		RECT anchor = {
			g_dragAnchorRect.left - g_windowOriginX,
			g_dragAnchorRect.top - g_windowOriginY,
			g_dragAnchorRect.right - g_windowOriginX,
			g_dragAnchorRect.bottom - g_windowOriginY
		};
		long shiftPx = (long)::lround((double)g_dragDeltaDays * (double)g_dragPxPerDay);
		// Text moves in POINTS (g_dragCandidateDx/Dy relative to g_dragOrigDx/
		// Dy), not a day-delta — converted to a screen-pixel shift via
		// g_dragPxPerPt (see g_dragPxPerPt's header comment). Computed up front
		// so both the ghost-rect switch below and DragKind::Text's own branch
		// can use it without duplicating the conversion.
		long textShiftPxX = 0, textShiftPxY = 0;
		if (g_dragKind == DragKind::Text && g_dragPxPerPt > 0.0f) {
			textShiftPxX = (long)::lround((double)(g_dragCandidateDx - g_dragOrigDx) * (double)g_dragPxPerPt);
			textShiftPxY = (long)::lround((double)(g_dragCandidateDy - g_dragOrigDy) * (double)g_dragPxPerPt);
		}
		RECT ghost = anchor;
		switch (g_dragKind) {
		case DragKind::Text:
			ghost.left = anchor.left + textShiftPxX;
			ghost.right = anchor.right + textShiftPxX;
			ghost.top = anchor.top + textShiftPxY;
			ghost.bottom = anchor.bottom + textShiftPxY;
			if (!g_dragTextAnchored && !::IsRectEmpty(&g_dragTargetRowRect)) {
				// Free text: retarget the ghost's row band vertically (row-
				// reassign), keeping the horizontal shift — mirrors TaskBody.
				int bandTop = g_dragTargetRowRect.top - g_windowOriginY;
				int bandBottom = g_dragTargetRowRect.bottom - g_windowOriginY;
				int h = anchor.bottom - anchor.top;
				int cy = (bandTop + bandBottom) / 2;
				ghost.top = cy - h / 2;
				ghost.bottom = ghost.top + h;
			}
			break;
		case DragKind::TaskEdgeL:
			ghost.left = anchor.left + shiftPx;
			if (ghost.left > ghost.right - 2) ghost.left = ghost.right - 2; // keep end>=start visually too
			break;
		case DragKind::TaskEdgeR:
			ghost.right = anchor.right + shiftPx;
			if (ghost.right < ghost.left + 2) ghost.right = ghost.left + 2;
			break;
		case DragKind::TaskProgress:
			// Progress edge moves within the bar; bar rect stays anchored.
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
		case DragKind::Marker:
		default:
			ghost.left = anchor.left + shiftPx;
			ghost.right = anchor.right + shiftPx;
			break;
		}

		// UF-02: hide the stale native progress fill behind a local wash, then
		// paint the full task ghost (track + progress) at the candidate position.
		if (g_dragKind == DragKind::TaskBody || g_dragKind == DragKind::TaskEdgeL ||
			g_dragKind == DragKind::TaskEdgeR || g_dragKind == DragKind::TaskProgress) {
			SolidBrush cover(GpColor(240, SURFACE));
			g.FillRectangle(&cover, anchor.left, anchor.top, anchor.right - anchor.left, anchor.bottom - anchor.top);
		}

		int ghostProgress = g_dragOrigPercent;
		if (g_dragKind == DragKind::TaskProgress) {
			ghostProgress = g_dragCandidatePercent;
		} else if (g_dragKind == DragKind::TaskEdgeL || g_dragKind == DragKind::TaskEdgeR) {
			// Edge resize does not change percent visually during drag.
			ghostProgress = g_dragOrigPercent;
		}

		g_dragPreviewRect = {
			ghost.left + g_windowOriginX, ghost.top + g_windowOriginY,
			ghost.right + g_windowOriginX, ghost.bottom + g_windowOriginY
		};

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
		} else if (g_dragKind == DragKind::Marker) {
			// A vertical line ghost (matching the marker's own rendered
			// shape), shifted horizontally and spanning the anchor band's
			// full height (the anchor rect IS the synthesized hit band, which
			// already spans the chart's full vertical extent).
			int cx = (ghost.left + ghost.right) / 2;
			Pen ghostPen(GpColor(220, ACCENT), 2.0f);
			g.DrawLine(&ghostPen, cx, anchor.top, cx, anchor.bottom);
		} else if (g_dragKind == DragKind::TaskBody || g_dragKind == DragKind::TaskEdgeL ||
			g_dragKind == DragKind::TaskEdgeR || g_dragKind == DragKind::TaskProgress) {
			PaintDragGhostShape(g, g_dragKind, anchor, ghost, ghostProgress);
		} else {
			PaintDragGhostShape(g, g_dragKind, anchor, ghost, 0);
		}

		// Tooltip: candidate dates / percent near the cursor (SR-IXC-03/04/06).
		std::wstring tip;
		if (g_dragKind == DragKind::TaskProgress) {
			wchar_t buf[32];
			::swprintf_s(buf, 32, L"%d%%", g_dragCandidatePercent);
			tip = buf;
		} else if (g_dragKind == DragKind::Text) {
			wchar_t buf[64];
			::swprintf_s(buf, 64, L"dx %.0f, dy %.0f", g_dragCandidateDx, g_dragCandidateDy);
			tip = buf;
			if (!g_dragTextAnchored && !g_dragTargetRowId.empty() && g_dragTargetRowId != g_dragOrigRowId) {
				tip += L"  (" + Widen(g_dragTargetRowId) + L")";
			}
		} else if (g_dragKind == DragKind::Marker || g_dragKind == DragKind::Milestone) {
			std::string candStart, candEnd;
			ComputeDragCandidateDates(candStart, candEnd);
			tip = Widen(candStart);
		} else {
			std::string candStart, candEnd;
			ComputeDragCandidateDates(candStart, candEnd);
			tip = Widen(candStart) + L" \u2192 " + Widen(candEnd);
			if (g_dragKind == DragKind::TaskBody && !g_dragTargetRowId.empty() && g_dragTargetRowId != g_dragOrigRowId) {
				tip += L"  (" + Widen(g_dragTargetRowId) + L")";
			}
		}
		g_dragPillText = Narrow(tip.c_str());
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
	} else if (g_dragCommitEcho.active && g_dragCommitEcho.kind != DragKind::None && g_dragCommitEcho.kind != DragKind::Create) {
		RECT anchorClient = {
			g_dragCommitEcho.anchorScreen.left - g_windowOriginX,
			g_dragCommitEcho.anchorScreen.top - g_windowOriginY,
			g_dragCommitEcho.anchorScreen.right - g_windowOriginX,
			g_dragCommitEcho.anchorScreen.bottom - g_windowOriginY
		};
		RECT ghostClient = ComputeDragGhostClientRect(g_dragCommitEcho.kind, g_dragCommitEcho.anchorScreen,
			g_dragCommitEcho.deltaDays, g_dragCommitEcho.pxPerDay, g_dragCommitEcho.targetRowScreen,
			g_dragCommitEcho.textAnchored, g_dragCommitEcho.origDx, g_dragCommitEcho.origDy,
			g_dragCommitEcho.candidateDx, g_dragCommitEcho.candidateDy, g_dragCommitEcho.pxPerPt);
		// m7 (SR-WIN-25): clip the committed-echo ghost to the explicit window;
		// a result that ends fully outside (hidden) paints no echo at all.
		bool ghostVisible = true;
		if (g_dragCommitEcho.windowClipValid) {
			const long clipL = g_dragCommitEcho.windowClipLeftPx - g_windowOriginX;
			const long clipR = g_dragCommitEcho.windowClipRightPx - g_windowOriginX;
			if (ghostClient.left < clipL) ghostClient.left = clipL;
			if (ghostClient.right > clipR) ghostClient.right = clipR;
			ghostVisible = ghostClient.right > ghostClient.left;
		}
		if (ghostVisible)
			PaintDragGhostShape(g, g_dragCommitEcho.kind, anchorClient, ghostClient, 0);
	}

	if (g_linkMode && g_appBarShown && g_appBarGeomValid) {
		PaintPositionedHintPill(g, L"Link: click a target task \x00b7 Esc cancels");
	} else if (g_gestureActive && g_dragKind == DragKind::LinkPort && g_dragActive) {
		PaintPositionedHintPill(g, L"Drag to a target task \x00b7 Esc cancels");
	}

	const wchar_t* emptyCellHint = nullptr;
	if (!g_creationFailHint.empty()) {
		emptyCellHint = g_creationFailHint.c_str();
	} else if (g_emptyCellHoverActive && g_emptyCellHintShownThisSession) {
		// UpdateEmptyCellHoverHint() (called once per Tick, before this paint
		// runs) is the sole place that flips shownThisSession once the 600ms
		// dwell elapses; painting just reads the resulting state instead of
		// re-deriving "due" here too (see its comment for why the split used
		// to double-fire).
		emptyCellHint = L"Drag to create a task \x2014 double-click task, Alt+double-click milestone, right-click for more";
	}
	if (emptyCellHint && !g_gestureActive && !g_dragActive && !g_linkMode && !IsEditSessionActive()) {
		PaintPositionedHintPill(g, emptyCellHint);
	}

	// Hover-only action cue (SR-CHR-02): top-edge band, no selection active.
	if (g_ownSelKind.empty() && !g_hasSelectionChrome && !::IsRectEmpty(&g_chartScreenRect)) {
		POINT pt = {};
		if (OverlayGetCursorPos(&pt)) {
			int chartLeft = g_chartScreenRect.left - g_windowOriginX;
			int chartRight = g_chartScreenRect.right - g_windowOriginX;
			int chartTop = g_chartScreenRect.top - g_windowOriginY;
			int ptX = pt.x - g_windowOriginX;
			int ptY = pt.y - g_windowOriginY;
			int bandTop = chartTop - BADGE_H;
			int bandBottom = chartTop + Scale(8);
			if (ptX >= chartLeft && ptX <= chartRight && ptY >= bandTop && ptY <= bandBottom) {
				Gdiplus::Font chipFont(L"Segoe UI", ScaleF(gt::chromeChipFontPx), FontStyleRegular, UnitPixel);
				RectF textBounds;
				g.MeasureString(g_badge.c_str(), -1, &chipFont, PointF(0, 0), &textBounds);
				int chipPadH = Scale(6);
				int chipPadV = Scale(3);
				int chipW = (int)textBounds.Width + chipPadH * 2;
				int chipH = (int)textBounds.Height + chipPadV * 2;
				int chipTop = std::max(Scale(2), chartTop - chipH - Scale(3));
				GraphicsPath chipPath;
				AddRoundRect(chipPath, (REAL)chartLeft, (REAL)chipTop, (REAL)chipW, (REAL)chipH, ScaleF(gt::chromeChipRadius));
				SolidBrush chipBrush(GpToken(gt::chromeChipAlpha, gt::primary));
				g.FillPath(&chipBrush, &chipPath);
				StringFormat sf;
				sf.SetAlignment(StringAlignmentCenter);
				sf.SetLineAlignment(StringAlignmentCenter);
				sf.SetFormatFlags(StringFormatFlagsNoWrap);
				SolidBrush chipText(GpToken(255, gt::surface));
				g.DrawString(g_badge.c_str(), -1, &chipFont,
					RectF((REAL)chartLeft, (REAL)chipTop, (REAL)chipW, (REAL)chipH), &sf, &chipText);
			}
		}
	}

	// Task-bar hover outline. Mirrors PaintOwnSelItemFrame's geometry (same
	// screen->client shift, same 1px chromeItemFramePx stroke) but strokes in
	// gt::primaryDim instead of gt::primary, so a hovered bar reads as "under
	// the cursor" while a selected bar keeps the stronger accent frame. Drawn
	// BEFORE the selection chrome below so selection always wins on the same
	// bar; skipped entirely mid-drag and when the hovered bar is the selected
	// one (a second frame in a paler color would only muddy the selection).
	if (!g_hoverTaskId.empty() && !::IsRectEmpty(&g_hoverTaskRect)
		&& !g_gestureActive && !g_dragActive
		&& !(g_ownSelKind == "TASK" && g_ownSelId == g_hoverTaskId)) {
		const REAL hx = (REAL)(g_hoverTaskRect.left - g_windowOriginX);
		const REAL hy = (REAL)(g_hoverTaskRect.top - g_windowOriginY);
		const REAL hw = (REAL)(g_hoverTaskRect.right - g_hoverTaskRect.left);
		const REAL hh = (REAL)(g_hoverTaskRect.bottom - g_hoverTaskRect.top);
		Pen hoverPen(GpToken(255, gt::primaryDim), ScaleF(gt::chromeItemFramePx));
		g.DrawRectangle(&hoverPen, (INT)hx, (INT)hy, (INT)hw, (INT)hh);
	}

	for (const auto& extra : g_ownSelExtra) {
		if (extra.kind == "ROW") PaintRowBandSelectionWash(g, extra.id);
		else PaintOwnSelItemFrame(g, extra.kind, extra.id);
	}

	// W2 window-edge two-phase preview must precede the selection-chrome early
	// return below: header hover normally has no selected item at all.
	if (g_gestureActive && g_dragActive && IsWindowEdgeDragKind(g_dragKind)) {
		PaintWindowAxisPreview(g);
		PaintWindowPill(g);
	}
	if (!g_gestureActive || IsWindowEdgeDragKind(g_dragKind)) {
		PaintWindowPorts(g);
	}

	// Persistent recording disclosure (SR-REC-16). This must precede every
	// selection-specific early return so recording is visible with no ownSel.
	if (g_recActive) {
		const int x = std::max(Scale(8), (int)(g_chartScreenRect.left - g_windowOriginX + Scale(8)));
		const int y = std::max(Scale(8), (int)(g_chartScreenRect.top - g_windowOriginY + Scale(8)));
		const int w = Scale(38), h = Scale(20);
		GraphicsPath path;
		AddRoundRect(path, (REAL)x, (REAL)y, (REAL)w, (REAL)h, ScaleF(4.0f));
		SolidBrush fill(GpToken(245, gt::deadline));
		g.FillPath(&fill, &path);
		Gdiplus::Font font(L"Segoe UI", ScaleF(10.0f), FontStyleBold, UnitPixel);
		StringFormat format;
		format.SetAlignment(StringAlignmentCenter);
		format.SetLineAlignment(StringAlignmentCenter);
		SolidBrush text(GpToken(255, gt::surface));
		g.DrawString(L"REC", -1, &font,
			RectF((REAL)x, (REAL)y, (REAL)w, (REAL)h), &format, &text);
		g_recIndicatorPainted = true;
	}

	if (!g_hasSelectionChrome || ::IsRectEmpty(&g_frameRect)) return;

	RECT frame = g_frameRect;
	REAL fx = (REAL)frame.left, fy = (REAL)frame.top;
	REAL fw = (REAL)(frame.right - frame.left), fh = (REAL)(frame.bottom - frame.top);

	if (g_ownSelKind == "ROW") {
		const int inset = Scale(3);
		Gdiplus::Pen edge(GpToken(255, gt::primary), ScaleF(3.0f));
		g.DrawLine(&edge, (INT)fx + inset, (INT)fy, (INT)fx + inset, (INT)(fy + fh));
		PaintRowBandSelectionWash(g, g_ownSelId);
		return;
	}

	if (g_ownSelKind.empty()) {
		Pen hairPen(GpToken(255, gt::chromeHairline), (REAL)Scale(1));
		g.DrawRectangle(&hairPen, (INT)fx, (INT)fy, (INT)fw, (INT)fh);
	} else {
		Pen framePen(GpToken(255, gt::primary), ScaleF(gt::chromeItemFramePx));
		g.DrawRectangle(&framePen, (INT)fx, (INT)fy, (INT)fw, (INT)fh);
	}

	// SR-IXC-06: progress drag handle at the percent boundary on selected tasks.
	if (g_ownSelKind == "TASK" && !g_gestureActive && !g_dragActive) {
		int pct = -1;
		for (const auto& item : g_hitSnapshot.items) {
			if (item.kind == HtItemKind::Task && item.id == g_ownSelId) {
				pct = item.progressPercent;
				break;
			}
		}
		if (pct < 0) {
			PpDocument cached;
			if (Gantt_TryPeekCachedDoc(&cached)) {
				for (const auto& t : cached.tasks) {
					if (t.id == g_ownSelId) { pct = t.percent; break; }
				}
			}
		}
		if (pct >= 0 && pct <= 100 && fw > 6.0f) {
			int hx = (INT)(fx + fw * (REAL)pct / 100.0f);
			int hy = (INT)(fy + fh / 2.0f);
			int hh = Scale(10);
			int hw = Scale(4);
			GraphicsPath handlePath;
			AddRoundRect(handlePath, (REAL)(hx - hw / 2), (REAL)(hy - hh / 2), (REAL)hw, (REAL)hh, 2.0f);
			SolidBrush handleFill(GpColor(255, SURFACE));
			g.FillPath(&handleFill, &handlePath);
			Pen handlePen(GpToken(255, gt::primary), 1.5f);
			g.DrawPath(&handlePen, &handlePath);
		}
	}

	// SR-IXC-13 / SR-DEP-04: link ports on selected (or link-drag candidate) bars.
	if (!g_gestureActive || g_dragKind == DragKind::LinkPort) {
		PaintLinkPortsForSnapshot(g);
	}

	if (g_gestureActive && g_dragActive && g_dragKind == DragKind::LinkPort) {
		HtRect item = { g_dragAnchorRect.left, g_dragAnchorRect.top, g_dragAnchorRect.right, g_dragAnchorRect.bottom };
		POINT fromSc = LinkPortScreenCenter(item, g_linkDragFromRight);
		POINT fromClient = { fromSc.x - g_windowOriginX, fromSc.y - g_windowOriginY };
		POINT toClient = g_dragLastPt;
		Pen bandPen(GpToken(200, gt::primary), 2.0f);
		g.DrawLine(&bandPen, (INT)fromClient.x, (INT)fromClient.y, (INT)toClient.x, (INT)toClient.y);
	}

	if (g_ownSelKind == "DEP" && !g_ownSelId.empty()) {
		Pen depPen(GpToken(255, gt::primary), 2.5f);
		for (const auto& item : g_hitSnapshot.items) {
			if (item.kind != HtItemKind::Dependency || item.id != g_ownSelId) continue;
			const int x1 = (INT)(item.rect.left - g_windowOriginX);
			const int y1 = (INT)(item.rect.top - g_windowOriginY);
			const int x2 = (INT)(item.rect.right - g_windowOriginX);
			const int y2 = (INT)(item.rect.bottom - g_windowOriginY);
			g.DrawLine(&depPen, x1, y1, x2, y2);
		}
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
		if (::UpdateLayeredWindow(g_hwnd, NULL, &dst, &size, g_bufDc, &src, 0, &bf, ULW_ALPHA)) {
			++g_overlayPaintCount;
			if (g_paintSampleActive) {
				const ULONGLONG now = ::GetTickCount64();
				g_paintTsMs[g_paintTsWrite % kPaintTsCap] = now;
				g_paintTsWrite = (g_paintTsWrite + 1) % kPaintTsCap;
				if (g_paintTsCount < kPaintTsCap) ++g_paintTsCount;
			}
			if (g_recActive) RecEmitPaint("overlay", g_overlayPaintCount);
		}
	}
	catch (const std::exception& e) {
		OvLog(L"overlay render failed (std::exception)");
		RecError("PaintOverlay", (long)E_FAIL, e.what());
	}
	catch (...) {
		OvLog(L"overlay render failed");
		RecError("PaintOverlay", (long)E_FAIL, "unknown error");
	}
}

void HideOverlay() {
	if (g_shown || g_hotkeysActive) {
		wchar_t buf[128];
		::swprintf_s(buf, 128, L"HideOverlay (shown=%d hotkeys=%d sel=%hs/%hs)",
			g_shown ? 1 : 0, g_hotkeysActive ? 1 : 0, g_ownSelKind.c_str(), g_ownSelId.c_str());
		OvLog(buf);
	}
	// Hide on ACTUAL visibility, not only the g_shown flag: a flag desync
	// would otherwise leave the window painted on screen forever with every
	// later HideOverlay a silent no-op (live report 2026-07-11: overlay
	// remained over a fullscreen game after PowerPoint lost foreground).
	if (g_hwnd && (g_shown || ::IsWindowVisible(g_hwnd))) { ::ShowWindow(g_hwnd, SW_HIDE); g_shown = false; }
	g_buttonsValid = false;
	ClearSelectionState();
	ClearOwnSelection();
	ClearLinkMode();
	CloseCardEditor(); // chart/slide went away (or is hidden): no editor can stay meaningfully open
	UnregisterAllHotkeys(L"HideOverlay"); // no selection left to Delete/Nudge; release any stolen keys

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
	// Live trigger: PP_RECORD=1 starts the flight recorder once per process
	// without UI (agent/meta-test launch path). Checked only on first overlay
	// show so subsequent reposition ticks are free.
	{
		static bool s_ppRecordChecked = false;
		if (!s_ppRecordChecked) {
			s_ppRecordChecked = true;
			wchar_t env[16] = {};
			DWORD n = ::GetEnvironmentVariableW(L"PP_RECORD", env, 16);
			if (n > 0 && n < 16 && ::wcscmp(env, L"1") == 0) {
				SessionRecordStart();
				if (g_recActive) {
					wchar_t msg[MAX_PATH + 96];
					::swprintf_s(msg, MAX_PATH + 96,
						L"session recording auto-started (PP_RECORD=1) dir=%s",
						g_recSessionDir);
					OvLog(msg);
				}
			}
		}
	}
	// Re-probe DPI on every (re)position: the overlay can be dragged to a
	// different-DPI monitor, or the user can change scaling live, between
	// ticks. A change invalidates the hit snapshot + forces a relayout/repaint
	// (via UpdateDpiScaledMetrics) so chrome and hit zones never lag behind.
	bool dpiChanged = UpdateDpiForWindow(g_hwnd);
	if (dpiChanged) UpdateDpiScaledMetrics();
	int chartW = chart.right - chart.left;
	int chartH = chart.bottom - chart.top;
	int wx = chart.left - INFL, wy = chart.top - INFL - BADGE_H;
	// V4/R8: the bottom app bar replaced the old floating mini-toolbar — do not
	// reserve TOOLBAR_H below the chart (that strip was only for Add/Del/-/+).
	int ww = chartW + INFL * 2, wh = chartH + INFL * 2 + BADGE_H;
	g_windowOriginX = wx;
	g_windowOriginY = wy;
	UpdateSelectionFrameFromScreen();
	if (wh < BADGE_H + INFL * 2 + Scale(8)) wh = BADGE_H + INFL * 2 + Scale(8);
	LayoutToolbarButtons(ww, wh);
	LayoutGrip(ww, wh);
	RECT oldWindow = {};
	bool wasShown = g_shown;
	bool hadWindow = ::GetWindowRect(g_hwnd, &oldWindow) != FALSE;
	bool moved = hadWindow && (oldWindow.left != wx || oldWindow.top != wy);
	bool resized = hadWindow && (oldWindow.right - oldWindow.left != ww || oldWindow.bottom - oldWindow.top != wh);
	bool needUpdate = dpiChanged || !wasShown || !hadWindow || moved || resized;
	if (needUpdate) {
		// Neither harness nor production uses TOPMOST any more: the chrome is
		// owned by PowerPoint's root window, so it sits above its owner and
		// follows it without a topmost bit. HWND_TOP keeps it at the front of
		// its own z-order band. See EnsureChromeOwner.
		HWND insertAfter = HWND_TOP;
		::SetWindowPos(g_hwnd, insertAfter, wx, wy, ww, wh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
		++g_overlaySwpCount;
	}
	g_shown = true;
	if (needUpdate) {
		RequestOverlayRepaint();
	}
}

// ---- BOTTOM APP BAR --------------------------------------------------------

void FreeAppBarBackBuffer();

static BOOL CALLBACK DestroyExtraAppBarWindowsProc(HWND hwnd, LPARAM lp) {
	wchar_t cls[64];
	if (::GetClassNameW(hwnd, cls, 64) && wcscmp(cls, kAppBarClass) == 0) {
		HWND keep = reinterpret_cast<HWND>(lp);
		if (!keep || hwnd != keep)
			::DestroyWindow(hwnd);
	}
	return TRUE;
}

static int CountAppBarWindows() {
	int count = 0;
	::EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
		wchar_t cls[64];
		if (::GetClassNameW(hwnd, cls, 64) && wcscmp(cls, kAppBarClass) == 0) {
			++*reinterpret_cast<int*>(lp);
		}
		return TRUE;
	}, reinterpret_cast<LPARAM>(&count));
	return count;
}

AppBarSel AppBarSelFromKind(const std::string& kind) {
	if (kind == "TASK") return AppBarSel::Task;
	if (kind == "ROW") return AppBarSel::Row;
	if (kind == "MILESTONE") return AppBarSel::Milestone;
	if (kind == "MARKER") return AppBarSel::Marker;
	if (kind == "TEXT") return AppBarSel::Note;
	if (kind == "DEP") return AppBarSel::Dependency;
	return AppBarSel::None;
}

void RebuildAppBarModelFromSlide() {
	if (!g_app) return;
	if (g_mutating) return;
	g_mutating = true;
	try {
		std::string json = ReadGanttFromSlide(g_app);
		if (!json.empty()) {
			PpDocument doc = DocumentFromJson(json);
			const bool isMulti = HasMultiSelection();
			AppBarSel sel = isMulti ? AppBarSel::Multi : AppBarSelFromKind(g_ownSelKind);
			g_appBar = BuildAppBar(sel, doc, g_ownSelId, OwnSelCount(), g_recActive);
			const bool selContextChanged =
				g_appBarSelKindBuilt != g_ownSelKind || g_appBarSelIdBuilt != g_ownSelId ||
				g_appBarIsMultiBuilt != isMulti;
			g_appBarSelKindBuilt = g_ownSelKind;
			g_appBarSelIdBuilt = g_ownSelId;
			g_appBarIsMultiBuilt = isMulti;
			g_appBarDocSig = doc.scale + "|" + doc.gridDensity + "|" + doc.axisNumbering + (doc.railLabels ? "|1" : "|0");
			g_lastScale = doc.scale;
			g_appBarValid = true;
			g_appBarModelDirty = true;
			if (selContextChanged) {
				// Force remeasure/repaint so a narrower context cannot leave
				// stale SCALE/Labels pixels from the previous wider bar.
				g_appBarLastMeasuredContentW = 0;
				g_appBarLastMeasuredContentH = 0;
				g_appBarGeomValid = false;
				g_appBarLayout = AppBarModel{};
				FreeAppBarBackBuffer();
			}
		}
	}
	catch (const _com_error& e) {
		OvLog(L"COM error rebuilding app-bar model");
		RecError("RebuildAppBarModel", (long)e.Error(), "COM error");
	}
	catch (const std::exception& e) {
		OvLog(L"document error rebuilding app-bar model");
		RecError("RebuildAppBarModel", (long)E_FAIL, e.what());
	}
	catch (...) {
		OvLog(L"unknown error rebuilding app-bar model");
		RecError("RebuildAppBarModel", (long)E_FAIL, "unknown error");
	}
	g_mutating = false;
}

int AppBarShadowInset() { return Scale(4); }

int AppBarDockGap() { return Scale(8); }

int AppBarContainerHeight() { return Scale(36); }

int AppBarButtonHeight() { return Scale(5) * 2 + (int)ScaleF(11.5f); }

int AppBarSegChipSize() { return Scale(22); }

int AppBarSwatchSize() { return Scale(15); }

int MeasureAppBarItemWidth(Gdiplus::Graphics* g, Gdiplus::Font* btnFont, const AppBarItem& item) {
	const int btnPadX = Scale(8);
	const int btnPadY = Scale(5);
	const int chip = AppBarSegChipSize();
	const int sw = AppBarSwatchSize();
	if (item.icon == AppBarIcon::ScaleSeg) return chip;
	if (item.icon == AppBarIcon::Swatch) return sw;
	int w = btnPadX * 2;
	if (item.icon == AppBarIcon::MoveUp || item.icon == AppBarIcon::MoveDown) w += Scale(10);
	if (!item.label.empty() && g && btnFont) {
		std::wstring wl(item.label.begin(), item.label.end());
		w += (int)std::ceil(MeasureTextW(*g, *btnFont, wl.c_str()));
	} else if (!item.label.empty()) {
		w += Scale((int)(item.label.size() * 7));
	}
	if (w < Scale(20)) w = Scale(20);
	(void)btnPadY;
	return w;
}

int MeasureAppBarSegmentedWidth(const std::vector<AppBarItem>& items, size_t from, size_t to) {
	const int chip = AppBarSegChipSize();
	const int innerPad = Scale(3);
	return innerPad * 2 + (int)(to - from) * chip;
}

namespace {
// Overlay-local command id for the collapsed INSERT "+" popup trigger (not HtMenuCmd).
constexpr int kAppBarInsertMenuCmd = -1001;

bool AppBarIsScaleGroup(const AppBarGroup& group) {
	return group.label == "SCALE";
}

bool AppBarIsSelectionContextGroup(const AppBarGroup& group) {
	for (const auto& item : group.items) {
		if (item.cmd == HtCmd_Rename || item.cmd == HtCmd_Edit) return true;
	}
	return false;
}

void AppBarStripIconLabels(AppBarGroup& group, bool labelsAndGridOnly) {
	for (auto& item : group.items) {
		if (item.cmd == kAppBarInsertMenuCmd) continue;
		if (item.icon == AppBarIcon::ScaleSeg || item.icon == AppBarIcon::Swatch) continue;
		if (labelsAndGridOnly) continue;
		if (item.icon != AppBarIcon::None) item.label.clear();
	}
}

int MeasureAppBarWidth(const AppBarModel& model) {
	const int padH = Scale(6);
	const int gap = Scale(2);
	const int groupPad = Scale(7);
	const int hairline = Scale(1);
	const int nameGap = Scale(8);
	int w = padH * 2;

	Gdiplus::Graphics* measureG = nullptr;
	Gdiplus::Font* btnFont = nullptr;
	try {
		Gdiplus::Bitmap bmp(1, 1, PixelFormat32bppPARGB);
		measureG = new Gdiplus::Graphics(&bmp);
		btnFont = MakeAppBarFont(ScaleF(11.5f), Gdiplus::FontStyleRegular);
		Gdiplus::Font* nameFont = MakeAppBarFont(ScaleF(11.5f), Gdiplus::FontStyleItalic);
		if (!model.name.empty()) {
			std::wstring nw(model.name.begin(), model.name.end());
			w += (int)std::ceil(MeasureTextW(*measureG, *nameFont, nw.c_str())) + nameGap;
		}
		delete nameFont;

		bool firstGroup = true;
		for (const auto& group : model.groups) {
			if (!firstGroup) w += hairline + gap * 2;
			firstGroup = false;
			w += groupPad;
			if (!group.label.empty()) {
				w += Scale((int)(group.label.size() * 6)) + gap;
			}
			for (size_t i = 0; i < group.items.size(); ) {
				if (group.items[i].icon == AppBarIcon::ScaleSeg) {
					size_t j = i;
					while (j < group.items.size() && group.items[j].icon == AppBarIcon::ScaleSeg) ++j;
					w += MeasureAppBarSegmentedWidth(group.items, i, j);
					if (j < group.items.size()) w += gap;
					i = j;
				} else {
					w += MeasureAppBarItemWidth(measureG, btnFont, group.items[i]);
					if (i + 1 < group.items.size()) w += gap;
					++i;
				}
			}
			w += groupPad;
		}
	}
	catch (...) {
		w = padH * 2 + Scale(200);
	}
	delete btnFont;
	delete measureG;
	return w;
}

void CollapseAppBarInsertGroup(AppBarModel& model) {
	for (auto& group : model.groups) {
		if (group.label != "INSERT" || group.items.size() <= 1) continue;
		g_appBarInsertPopupItems = group.items;
		AppBarItem plus;
		plus.cmd = kAppBarInsertMenuCmd;
		plus.label = "+";
		plus.enabled = true;
		group.items = { plus };
		return;
	}
}

void ApplyAppBarOverflowPolicy(int maxContentW) {
	g_appBarLayout = g_appBar;
	g_appBarInsertPopupItems.clear();
	// Guard against accidental duplicate SCALE groups after context transitions.
	for (size_t i = g_appBarLayout.groups.size(); i > 0; --i) {
		if (!AppBarIsScaleGroup(g_appBarLayout.groups[i - 1])) continue;
		for (size_t j = i; j < g_appBarLayout.groups.size(); ) {
			if (AppBarIsScaleGroup(g_appBarLayout.groups[j]))
				g_appBarLayout.groups.erase(g_appBarLayout.groups.begin() + (ptrdiff_t)j);
			else
				++j;
		}
		break;
	}
	if (maxContentW <= 0) return;

	auto fits = [&]() { return MeasureAppBarWidth(g_appBarLayout) <= maxContentW; };
	if (fits()) return;

	CollapseAppBarInsertGroup(g_appBarLayout);
	if (fits()) return;

	for (auto& group : g_appBarLayout.groups) {
		if (AppBarIsScaleGroup(group) || AppBarIsSelectionContextGroup(group)) continue;
		AppBarStripIconLabels(group, false);
	}
	if (fits()) return;

	for (auto& group : g_appBarLayout.groups) {
		if (!AppBarIsSelectionContextGroup(group)) continue;
		AppBarStripIconLabels(group, false);
	}
	if (fits()) return;

	for (auto& group : g_appBarLayout.groups) {
		if (!AppBarIsScaleGroup(group)) continue;
		AppBarStripIconLabels(group, true);
	}
}
} // namespace

void MeasureAppBar() {
	const int h = AppBarContainerHeight();
	g_appBarContentW = MeasureAppBarWidth(g_appBarLayout);
	g_appBarContentH = h;
}

void DrawAppBarChevron(Gdiplus::Graphics& g, int cx, int cy, bool up, unsigned long color) {
	using namespace Gdiplus;
	const int half = Scale(4);
	Pen pen(GpToken(255, color), ScaleF(1.5f));
	if (up) {
		g.DrawLine(&pen, cx - half, cy + half / 2, cx, cy - half / 2);
		g.DrawLine(&pen, cx, cy - half / 2, cx + half, cy + half / 2);
	} else {
		g.DrawLine(&pen, cx - half, cy - half / 2, cx, cy + half / 2);
		g.DrawLine(&pen, cx, cy + half / 2, cx + half, cy - half / 2);
	}
}

void PaintAppBarSegmented(Gdiplus::Graphics& g, int x, int y, const std::vector<AppBarItem>& items, size_t from, size_t to) {
	using namespace Gdiplus;
	const int chip = AppBarSegChipSize();
	const int innerPad = Scale(3);
	const int pillH = chip + innerPad * 2;
	const int pillW = innerPad * 2 + (int)(to - from) * chip;
	GraphicsPath pillPath;
	AddRoundRect(pillPath, (REAL)x, (REAL)y, (REAL)pillW, (REAL)pillH, (REAL)(pillH / 2));
	SolidBrush track(GpToken(255, gt::railSurface));
	g.FillPath(&track, &pillPath);
	Gdiplus::Font* chipFont = MakeAppBarFont(ScaleF(11.5f), FontStyleBold);
	StringFormat sf;
	sf.SetAlignment(StringAlignmentCenter);
	sf.SetLineAlignment(StringAlignmentCenter);
	int cx = x + innerPad;
	int cy = y + innerPad;
	for (size_t i = from; i < to; ++i) {
		const AppBarItem& item = items[i];
		RECT chipRc = { cx, cy, cx + chip, cy + chip };
		if (item.active) {
			GraphicsPath activePath;
			AddRoundRect(activePath, (REAL)cx, (REAL)cy, (REAL)chip, (REAL)chip, ScaleF(5.0f));
			SolidBrush white(GpToken(255, gt::surface));
			g.FillPath(&white, &activePath);
			Pen shadow(GpToken(80, gt::outline), 1.0f);
			g.DrawPath(&shadow, &activePath);
		}
		std::wstring wl(item.label.begin(), item.label.end());
		SolidBrush textBrush(GpToken(255, item.active ? gt::primary : gt::ink2));
		g.DrawString(wl.c_str(), -1, chipFont, RectF((REAL)cx, (REAL)cy, (REAL)chip, (REAL)chip), &sf, &textBrush);
		if (item.enabled) g_appBarHits.push_back({ item.cmd, chipRc, true });
		cx += chip;
	}
	delete chipFont;
}

void PaintAppBarSwatch(Gdiplus::Graphics& g, int x, int y, const AppBarItem& item) {
	using namespace Gdiplus;
	const int sw = AppBarSwatchSize();
	const int rad = Scale(4);
	RECT rc = { x, y, x + sw, y + sw };
	unsigned long fillRgb = gt::ParseHexColor(item.data, gt::swatch1);
	GraphicsPath swPath;
	AddRoundRect(swPath, (REAL)x, (REAL)y, (REAL)sw, (REAL)sw, (REAL)rad);
	SolidBrush fill(GpToken(255, fillRgb));
	g.FillPath(&fill, &swPath);
	if (item.active) {
		Pen ring(GpToken(255, gt::primary), 2.0f);
		g.DrawPath(&ring, &swPath);
	}
	if (item.enabled) g_appBarHits.push_back({ item.cmd, rc, true });
}

void PaintAppBarButton(Gdiplus::Graphics& g, int x, int y, const AppBarItem& item, Gdiplus::Font& btnFont) {
	using namespace Gdiplus;
	const int btnPadX = Scale(8);
	const int btnPadY = Scale(5);
	const int btnH = AppBarButtonHeight();
	int btnW = MeasureAppBarItemWidth(&g, &btnFont, item);
	RECT rc = { x, y, x + btnW, y + btnH };
	bool hovered = item.enabled && item.cmd == g_appBarHoverCmd;
	unsigned long textRgb = gt::ink;
	unsigned long fillRgb = 0;
	if (!item.enabled) {
		textRgb = gt::ink3;
	} else if (item.active && !item.danger) {
		fillRgb = gt::primarySoft;
		textRgb = gt::primary;
	} else if (hovered && item.danger) {
		fillRgb = gt::dangerSoft;
		textRgb = gt::deadline;
	} else if (hovered) {
		fillRgb = gt::primarySoft;
		textRgb = gt::primary;
	} else if (item.danger) {
		textRgb = gt::deadline;
	}
	if (fillRgb != 0) {
		GraphicsPath pillPath;
		AddRoundRect(pillPath, (REAL)x, (REAL)y, (REAL)btnW, (REAL)btnH, (REAL)Scale(7));
		SolidBrush fill(GpToken(255, fillRgb));
		g.FillPath(&fill, &pillPath);
	}
	BYTE textAlpha = item.enabled ? 255 : 102;
	int contentX = x + btnPadX;
	int contentY = y + btnPadY;
	if (item.icon == AppBarIcon::MoveUp || item.icon == AppBarIcon::MoveDown) {
		DrawAppBarChevron(g, contentX + Scale(5), y + btnH / 2, item.icon == AppBarIcon::MoveUp, textRgb);
		contentX += Scale(12);
	}
	if (!item.label.empty()) {
		std::wstring wl(item.label.begin(), item.label.end());
		SolidBrush textBrush(GpToken(textAlpha, textRgb));
		StringFormat sf;
		sf.SetLineAlignment(StringAlignmentCenter);
		g.DrawString(wl.c_str(), -1, &btnFont, RectF((REAL)contentX, (REAL)contentY,
			(REAL)(btnW - btnPadX * 2), (REAL)(btnH - btnPadY * 2)), &sf, &textBrush);
	}
	if (item.enabled) g_appBarHits.push_back({ item.cmd, rc, true });
}

void PaintAppBar(Gdiplus::Graphics& g, int W, int H) {
	using namespace Gdiplus;
	g_appBarHits.clear();
	const int inset = AppBarShadowInset();
	const int containerH = AppBarContainerHeight();
	const int containerW = W - inset * 2;
	const int containerY = inset;
	const int containerX = inset;
	const int padH = Scale(6);
	const int gap = Scale(2);
	const int groupPad = Scale(7);
	const int hairline = Scale(1);
	const int nameGap = Scale(8);
	const int btnH = AppBarButtonHeight();

	// Soft drop shadow
	{
		GraphicsPath shadowPath;
		AddRoundRect(shadowPath, (REAL)(containerX + Scale(1)), (REAL)(containerY + Scale(2)),
			(REAL)containerW, (REAL)containerH, (REAL)Scale(11));
		SolidBrush shadow(GpToken(40, gt::ink3));
		g.FillPath(&shadow, &shadowPath);
	}

	GraphicsPath containerPath;
	AddRoundRect(containerPath, (REAL)containerX, (REAL)containerY,
		(REAL)containerW, (REAL)containerH, (REAL)Scale(11));
	SolidBrush surface(GpToken(255, gt::surface));
	g.FillPath(&surface, &containerPath);
	Pen border(GpToken(255, gt::outline), 1.0f);
	g.DrawPath(&border, &containerPath);

	Gdiplus::Font* groupFont = MakeAppBarFont(ScaleF(9.0f), FontStyleBold);
	Gdiplus::Font* btnFont = MakeAppBarFont(ScaleF(11.5f), FontStyleRegular);
	Gdiplus::Font* nameFont = MakeAppBarFont(ScaleF(11.5f), FontStyleItalic);

	int x = containerX + padH;
	int contentY = containerY + (containerH - btnH) / 2;

	if (!g_appBarLayout.name.empty()) {
		std::wstring nw(g_appBarLayout.name.begin(), g_appBarLayout.name.end());
		SolidBrush nameBrush(GpToken(255, gt::ink2));
		StringFormat sf;
		sf.SetLineAlignment(StringAlignmentCenter);
		g.DrawString(nw.c_str(), -1, nameFont, RectF((REAL)x, (REAL)contentY,
			(REAL)(containerW - padH * 2), (REAL)btnH), &sf, &nameBrush);
		x += (int)std::ceil(MeasureTextW(g, *nameFont, nw.c_str())) + nameGap;
	}

	bool firstGroup = true;
	for (const auto& group : g_appBarLayout.groups) {
		if (!firstGroup) {
			int sepX = x + gap;
			Pen hair(GpToken(255, gt::outline), (REAL)hairline);
			g.DrawLine(&hair, sepX, containerY + Scale(8), sepX, containerY + containerH - Scale(8));
			x += hairline + gap * 2;
		}
		firstGroup = false;
		x += groupPad;

		if (!group.label.empty()) {
			std::wstring gl(group.label.begin(), group.label.end());
			for (auto& ch : gl) if (ch >= L'a' && ch <= L'z') ch = (wchar_t)(ch - L'a' + L'A');
			SolidBrush labelBrush(GpToken(255, gt::ink3));
			StringFormat sf;
			sf.SetLineAlignment(StringAlignmentCenter);
			g.DrawString(gl.c_str(), -1, groupFont, RectF((REAL)x, (REAL)contentY,
				(REAL)Scale(60), (REAL)btnH), &sf, &labelBrush);
			x += (int)std::ceil(MeasureTextW(g, *groupFont, gl.c_str())) + gap;
		}

		for (size_t i = 0; i < group.items.size(); ) {
			if (group.items[i].icon == AppBarIcon::ScaleSeg) {
				size_t j = i;
				while (j < group.items.size() && group.items[j].icon == AppBarIcon::ScaleSeg) ++j;
				const int chip = AppBarSegChipSize();
				const int innerPad = Scale(3);
				const int segH = chip + innerPad * 2;
				int segY = containerY + (containerH - segH) / 2;
				PaintAppBarSegmented(g, x, segY, group.items, i, j);
				x += MeasureAppBarSegmentedWidth(group.items, i, j);
				if (j < group.items.size()) x += gap;
				i = j;
			} else if (group.items[i].icon == AppBarIcon::Swatch) {
				const int sw = AppBarSwatchSize();
				int swY = containerY + (containerH - sw) / 2;
				PaintAppBarSwatch(g, x, swY, group.items[i]);
				x += sw;
				if (i + 1 < group.items.size()) x += gap;
				++i;
			} else {
				PaintAppBarButton(g, x, contentY, group.items[i], *btnFont);
				x += MeasureAppBarItemWidth(&g, btnFont, group.items[i]);
				if (i + 1 < group.items.size()) x += gap;
				++i;
			}
		}
		x += groupPad;
	}

	delete nameFont;
	delete btnFont;
	delete groupFont;
}

void FreeAppBarBackBuffer() {
	if (g_abDc) {
		if (g_abOld) ::SelectObject(g_abDc, g_abOld);
		::DeleteDC(g_abDc);
		g_abDc = NULL;
		g_abOld = NULL;
	}
	if (g_abBmp) {
		::DeleteObject(g_abBmp);
		g_abBmp = NULL;
	}
	g_abBits = nullptr;
	g_abW = 0;
	g_abH = 0;
}

bool EnsureAppBarBackBuffer(int w, int h) {
	if (g_abDc && g_abBits && g_abW == w && g_abH == h) return true;
	FreeAppBarBackBuffer();
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
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
	g_abOld = ::SelectObject(dc, bmp);
	g_abDc = dc;
	g_abBmp = bmp;
	g_abBits = bits;
	g_abW = w;
	g_abH = h;
	return true;
}

void RenderAppBar() {
	if (!g_appBarHwnd || !g_gdiplusToken) return;
	// Never paint when the model/layout lags behind ownSel — hover repaints used
	// to push a stale document-context bitmap after Overlay_SelectForTest (or a
	// real click) changed selection before the next Tick rebuilt the bar.
	if (!g_appBarValid || g_ownSelKind != g_appBarSelKindBuilt || g_ownSelId != g_appBarSelIdBuilt ||
		g_appBarIsMultiBuilt != HasMultiSelection()) {
		wchar_t buf[160];
		::swprintf_s(buf, 160, L"RenderAppBar SKIP valid=%d own=%hs/%hs built=%hs/%hs",
			g_appBarValid ? 1 : 0, g_ownSelKind.c_str(), g_ownSelId.c_str(),
			g_appBarSelKindBuilt.c_str(), g_appBarSelIdBuilt.c_str());
		OvLog(buf);
		return;
	}
	RECT wr;
	if (!::GetWindowRect(g_appBarHwnd, &wr)) { OvLog(L"RenderAppBar SKIP GetWindowRect"); return; }
	int w = wr.right - wr.left, h = wr.bottom - wr.top;
	if (w <= 0 || h <= 0) { OvLog(L"RenderAppBar SKIP zero size"); return; }
	if (!EnsureAppBarBackBuffer(w, h)) { OvLog(L"RenderAppBar SKIP backbuffer"); return; }
	try {
		if (g_abBits && g_abW > 0 && g_abH > 0)
			::memset(g_abBits, 0, (size_t)g_abW * (size_t)g_abH * 4u);
		Gdiplus::Bitmap surface(w, h, w * 4, PixelFormat32bppPARGB, (BYTE*)g_abBits);
		if (surface.GetLastStatus() != Gdiplus::Ok) { OvLog(L"RenderAppBar SKIP surface"); return; }
		Gdiplus::Graphics g(&surface);
		if (g.GetLastStatus() != Gdiplus::Ok) { OvLog(L"RenderAppBar SKIP graphics"); return; }
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
		g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
		g.Clear(Gdiplus::Color(0, 0, 0, 0));
		PaintAppBar(g, w, h);
		g.Flush(Gdiplus::FlushIntentionSync);
		POINT dst = { wr.left, wr.top };
		POINT src = { 0, 0 };
		SIZE size = { w, h };
		BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
		if (::UpdateLayeredWindow(g_appBarHwnd, NULL, &dst, &size, g_abDc, &src, 0, &bf, ULW_ALPHA)) {
			++g_appBarPaintCount;
			if (g_recActive) RecEmitPaint("appbar", g_appBarPaintCount);
			wchar_t okBuf[200];
			::swprintf_s(okBuf, 200, L"RenderAppBar OK %dx%d name=%hs groups=%d barWindows=%d hwnd=%p",
				w, h, g_appBarLayout.name.c_str(), (int)g_appBarLayout.groups.size(),
				CountAppBarWindows(), (void*)g_appBarHwnd);
			OvLog(okBuf);
		} else {
			DWORD err = ::GetLastError();
			wchar_t errBuf[96];
			::swprintf_s(errBuf, 96, L"RenderAppBar ULW FAILED err=%lu", err);
			OvLog(errBuf);
		}
	}
	catch (const std::exception&) {
		OvLog(L"app-bar render failed (std::exception)");
	}
	catch (...) {
		OvLog(L"app-bar render failed");
	}
}

void ShowAppBarInsertMenu(POINT clientPt);
void HandleAppBarCommand(int cmd, POINT clientPt = { 0, 0 });

LRESULT CALLBACK AppBarWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	try {
		// R1b: input tap at TOP (SR-REC-05). g_recActive fast-check inside.
		if (g_recActive) RecEmitInput("appbar", msg, hwnd, wp, lp);
		if (msg == WM_NCHITTEST) return HTCLIENT;
		if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
		if (msg == WM_MOUSEMOVE) {
			POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
			int newHover = 0;
			for (const auto& h : g_appBarHits) {
				if (h.enabled && ::PtInRect(&h.rc, pt)) { newHover = h.cmd; break; }
			}
			if (newHover != g_appBarHoverCmd) {
				g_appBarHoverCmd = newHover;
				if (g_appBarValid && g_ownSelKind == g_appBarSelKindBuilt && g_ownSelId == g_appBarSelIdBuilt &&
					g_appBarIsMultiBuilt == HasMultiSelection())
					RenderAppBar();
			}
			TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
			::TrackMouseEvent(&tme);
			return 0;
		}
		if (msg == WM_MOUSELEAVE) {
			if (g_appBarHoverCmd != 0) {
				g_appBarHoverCmd = 0;
				if (g_appBarValid && g_ownSelKind == g_appBarSelKindBuilt && g_ownSelId == g_appBarSelIdBuilt &&
					g_appBarIsMultiBuilt == HasMultiSelection())
					RenderAppBar();
			}
			return 0;
		}
		if (msg == WM_LBUTTONUP) {
			POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
			int resolved = 0;
			for (const auto& h : g_appBarHits) {
				if (h.enabled && ::PtInRect(&h.rc, pt)) {
					resolved = h.cmd;
					break;
				}
			}
			{
				wchar_t buf[96];
				::swprintf_s(buf, 96, L"appbar click (%ld,%ld) -> cmd %d (hits=%zu)",
					(long)pt.x, (long)pt.y, resolved, g_appBarHits.size());
				OvLog(buf);
			}
			if (resolved != 0) HandleAppBarCommand(resolved, pt);
			return 0;
		}
	}
	catch (const std::exception&) {
		OvLog(L"AppBarWndProc: exception caught");
	}
	catch (...) {
		OvLog(L"AppBarWndProc: exception caught");
	}
	return ::DefWindowProcW(hwnd, msg, wp, lp);
}

void EnsureAppBarWindow() {
	// Orphan cleanup: a prior crashed harness run can leave a visible
	// PowerPlannerAppBar HWND while g_appBarHwnd is NULL — captures then show a
	// stale composite while the dump reads the live model (H1).
	::EnumWindows(DestroyExtraAppBarWindowsProc, reinterpret_cast<LPARAM>(g_appBarHwnd));
	if (g_appBarHwnd) return;
	WNDCLASSEXW wc = { sizeof(wc) };
	wc.lpfnWndProc = AppBarWndProc;
	wc.hInstance = g_inst;
	wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = kAppBarClass;
	::RegisterClassExW(&wc);
	// Owned by PowerPoint's root, not topmost — see the note at the overlay's
	// CreateWindowExW.
	g_appBarHwnd = ::CreateWindowExW(
		WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
		kAppBarClass, L"", WS_POPUP,
		0, 0, 10, 10, ::GetAncestor(g_pptHwnd, GA_ROOT), NULL, g_inst, NULL);
}

void HideAppBar(bool keepGeomCache = false) {
	// Same actual-visibility rule as HideOverlay (flag desync must not pin
	// the bar on screen).
	if (g_appBarHwnd && (g_appBarShown || ::IsWindowVisible(g_appBarHwnd))) { ::ShowWindow(g_appBarHwnd, SW_HIDE); g_appBarShown = false; }
	g_appBarHoverCmd = 0;
	if (!keepGeomCache) {
		g_appBarValid = false;
		g_appBarGeomValid = false;
		g_appBarModelDirty = true;
		g_appBarLastMeasuredContentW = 0;
		g_appBarLastMeasuredContentH = 0;
	}
}

void ShowAppBar(const RECT& chartScreenRect, const RECT& slideScreenRect) {
	EnsureAppBarWindow();
	if (!g_appBarHwnd) return;
	bool dpiChanged = UpdateDpiForWindow(g_appBarHwnd);
	if (dpiChanged) UpdateDpiScaledMetrics();
	if (g_ownSelKind != g_appBarSelKindBuilt || g_ownSelId != g_appBarSelIdBuilt ||
		g_appBarIsMultiBuilt != HasMultiSelection() || !g_appBarValid || g_appBarModelDirty) {
		RebuildAppBarModelFromSlide();
	}
	const int shadow = AppBarShadowInset();
	int maxW = (int)((slideScreenRect.right - slideScreenRect.left) * 0.94);
	if (maxW <= 0)
		maxW = std::max(0, (int)(chartScreenRect.right - chartScreenRect.left));
	int maxContentW = std::max(0, maxW - shadow * 2);
	ApplyAppBarOverflowPolicy(maxContentW);
	MeasureAppBar();
	int w = g_appBarContentW + shadow * 2;
	if (w > maxW) w = maxW;
	int h = g_appBarContentH + shadow * 2;
	int cx = (chartScreenRect.left + chartScreenRect.right) / 2;
	int x = cx - w / 2;
	int y = chartScreenRect.bottom + AppBarDockGap();
	// SR-DOCK-01: clamp horizontal position to slide edges.
	if (slideScreenRect.right > slideScreenRect.left) {
		const int slideL = slideScreenRect.left;
		const int slideR = slideScreenRect.right;
		if (x < slideL) x = slideL;
		if (x + w > slideR) x = std::max(slideL, slideR - w);
	}
	RECT want = { x, y, x + w, y + h };
	const int prevWindowW = g_appBarGeomValid ? (g_appBarLastRect.right - g_appBarLastRect.left) : 0;
	const int prevWindowH = g_appBarGeomValid ? (g_appBarLastRect.bottom - g_appBarLastRect.top) : 0;
	bool windowPosChanged = !g_appBarGeomValid ||
		want.left != g_appBarLastRect.left || want.top != g_appBarLastRect.top;
	bool windowSizeChanged = !g_appBarGeomValid || w != prevWindowW || h != prevWindowH;
	bool contentSizeChanged = g_appBarContentW != g_appBarLastMeasuredContentW ||
		g_appBarContentH != g_appBarLastMeasuredContentH;
	bool geomChanged = windowPosChanged || windowSizeChanged || contentSizeChanged;
	bool firstShow = !g_appBarShown;
	if (geomChanged || firstShow) {
		// Same override rule as ShowOverlayForChartRect: harness runs bypass
		// host-active gating, so their windows must never be TOPMOST.
		HWND barInsertAfter = HWND_TOP;
		::SetWindowPos(g_appBarHwnd, barInsertAfter, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
		++g_appBarSwpCount;
		g_appBarLastRect = want;
		g_appBarGeomValid = true;
	}
	g_appBarLastMeasuredContentW = g_appBarContentW;
	g_appBarLastMeasuredContentH = g_appBarContentH;
	g_appBarShown = true;
	if (geomChanged || firstShow || g_appBarModelDirty) {
		RenderAppBar();
		g_appBarModelDirty = false;
	}
}

void ShowAppBarInsertMenu(POINT clientPt) {
	if (g_appBarInsertPopupItems.empty()) return;
	if (::GetEnvironmentVariableW(L"PP_OVERLAY_NO_MENU", NULL, 0) > 0) return;

	std::vector<HtMenuItem> items;
	items.push_back({ HtCmd_None, "Insert", false, "", true });
	for (const auto& item : g_appBarInsertPopupItems) {
		items.push_back({ item.cmd, MenuLabelForAppBarItem(item), false, "Insert", item.enabled });
	}

	POINT screenPt = { clientPt.x, clientPt.y };
	::ClientToScreen(g_appBarHwnd, &screenPt);
	int cmd = ThemeMenu_Show(items, screenPt, g_appBarHwnd, true);
	if (cmd > 0 && cmd != kAppBarInsertMenuCmd)
		HandleAppBarCommand(cmd, POINT{ 0, 0 });
}

void ShowAppBarSettingsMenu(POINT clientPt) {
	if (::GetEnvironmentVariableW(L"PP_OVERLAY_NO_MENU", NULL, 0) > 0) return;
	if (!g_app || g_mutating) return;
	PpDocument doc;
	try {
		if (!ReadGanttDocFromSlide(g_app, &doc)) return;
	} catch (...) {
		OvLog(L"unable to read document for Settings menu");
		return;
	}
	POINT screenPt = { clientPt.x, clientPt.y };
	::ClientToScreen(g_appBarHwnd, &screenPt);
	int cmd = ThemeMenu_Show(BuildSettingsMenuItems(doc), screenPt, g_appBarHwnd, true);
	if (cmd > 0 && cmd != HtCmd_Settings)
		HandleAppBarCommand(cmd, POINT{ 0, 0 });
}

// SR-SMO-02 op-dispatch total timer: brackets an app-bar command so the
// OPPHASES trace can report the full outside-UpdateGantt wall time
// (dispatchTotal) alongside the inside-UpdateGantt phase breakdown. Covers all
// early-return paths via RAII. Only app-bar dispatch is timed; other op entries
// (hotkey/drag) report dispatchTotal=0 (reset by ReadGanttDocFromSlide).
struct OpDispatchTimer {
	ULONGLONG t0 = ::GetTickCount64();
	~OpDispatchTimer() { Gantt_SetOpDispatchTotalMs((unsigned long long)(::GetTickCount64() - t0)); }
};

void HandleAppBarCommand(int cmd, POINT clientPt) {
	OpDispatchTimer _opDispatchTimer;
	// R1b op event (SR-REC-09): emit after the command body returns.
	struct RecOpEmitter {
		int cmd = 0;
		ULONGLONG t0 = ::GetTickCount64();
		long hr = 0;
		~RecOpEmitter() {
			if (!g_recActive) return;
			const unsigned long long dispatchMs =
				(unsigned long long)(::GetTickCount64() - t0);
			std::string label = RecAppBarCmdLabel(cmd);
			char phaseBuf[512];
			phaseBuf[0] = 0;
			const int phaseLen = Gantt_GetLastOpPhasesForTest(phaseBuf, (int)sizeof(phaseBuf));
			std::string body;
			body.reserve(64 + label.size() + (phaseLen > 0 ? (size_t)phaseLen : 2));
			body += "\"cmd\":";
			body += std::to_string(cmd);
			body += ",\"label\":\"";
			EntityJsonAppendEscaped(body, label);
			body += "\",\"dispatchMs\":";
			body += std::to_string(dispatchMs);
			body += ",\"hr\":";
			body += std::to_string(hr);
			body += ",\"phases\":";
			if (phaseLen > 0) body += phaseBuf;
			else body += "null";
			RecEvent("op", body);
		}
	} _recOp{ cmd };
	if (cmd == kAppBarInsertMenuCmd) {
		ShowAppBarInsertMenu(clientPt);
		return;
	}
	if (cmd == HtCmd_Settings) {
		ShowAppBarSettingsMenu(clientPt);
		return;
	}
	if (cmd == HtCmd_RecordSession) {
		if (g_recActive) SessionRecordStop();
		else SessionRecordStart();
		g_appBarValid = false;
		g_appBarModelDirty = true;
		RequestOverlayRepaint();
		if (g_appBarHwnd) ::InvalidateRect(g_appBarHwnd, NULL, FALSE);
		return;
	}
	if ((cmd == HtCmd_ToggleRailLabels || cmd == HtCmd_CycleGrid ||
		cmd == HtCmd_GridAuto || cmd == HtCmd_GridDay || cmd == HtCmd_GridWeek ||
		cmd == HtCmd_GridMonth || cmd == HtCmd_GridNone ||
		cmd == HtCmd_AxisNumbersDay || cmd == HtCmd_AxisNumbersCW ||
		cmd == HtCmd_RailLabelsOn || cmd == HtCmd_RailLabelsOff) && g_app && !g_mutating) {
		g_mutating = true;
		try {
			PpDocument doc;
			if (ReadGanttDocFromSlide(g_app, &doc)) {
				const std::string keepKind = g_ownSelKind;
				const std::string keepId = g_ownSelId;
				const std::string beforeGrid = doc.gridDensity;
				const std::string beforeAxis = doc.axisNumbering;
				const bool beforeRail = doc.railLabels;
				bool valid = true;
				switch (cmd) {
				case HtCmd_ToggleRailLabels: valid = SetRailLabelsGlobal(doc, !doc.railLabels); break;
				case HtCmd_RailLabelsOn: valid = SetRailLabelsGlobal(doc, true); break;
				case HtCmd_RailLabelsOff: valid = SetRailLabelsGlobal(doc, false); break;
				case HtCmd_CycleGrid: {
					const std::string cur = doc.gridDensity.empty() ? "auto" : doc.gridDensity;
					const char* next = cur == "auto" ? "week" : cur == "week" ? "month" : cur == "month" ? "none" : "auto";
					valid = SetGridDensity(doc, next);
					break;
				}
				case HtCmd_GridAuto: valid = SetGridDensity(doc, "auto"); break;
				case HtCmd_GridDay: valid = SetGridDensity(doc, "day"); break;
				case HtCmd_GridWeek: valid = SetGridDensity(doc, "week"); break;
				case HtCmd_GridMonth: valid = SetGridDensity(doc, "month"); break;
				case HtCmd_GridNone: valid = SetGridDensity(doc, "none"); break;
				case HtCmd_AxisNumbersDay: valid = SetAxisNumbering(doc, "day"); break;
				case HtCmd_AxisNumbersCW: valid = SetAxisNumbering(doc, "cw"); break;
				default: valid = false; break;
				}
				const bool changed = valid && (doc.gridDensity != beforeGrid || doc.axisNumbering != beforeAxis || doc.railLabels != beforeRail);
				if (changed) {
					RebuildChart(doc, keepId);
					if (!keepId.empty() && !keepKind.empty()) SetOwnSelection(keepKind, keepId);
				}
			}
		} catch (const _com_error& e) {
			OvLog(L"COM error changing component settings");
			RecError("HandleAppBarCommand/settings", (long)e.Error(), "COM error");
		} catch (const std::exception& e) {
			OvLog(L"document error changing component settings");
			RecError("HandleAppBarCommand/settings", (long)E_FAIL, e.what());
		} catch (...) {
			OvLog(L"unknown error changing component settings");
			RecError("HandleAppBarCommand/settings", (long)E_FAIL, "unknown error");
		}
		g_mutating = false;
		g_appBarValid = false;
		RequestOverlayRepaint();
		return;
	}
	if (cmd == HtCmd_InsertMarker && g_ownSelKind.empty() && g_app && !g_mutating) {
		g_mutating = true;
		try {
			PpDocument doc;
			if (ReadGanttDocFromSlide(g_app, &doc)) {
				const std::string dateISO = DefaultMarkerDateAtVisibleCenter();
				const std::string newId = AddMarker(doc, "custom", "Marker", dateISO);
				if (!newId.empty()) {
					RebuildChart(doc, newId);
					SetOwnSelection("MARKER", newId);
					RequestOverlayRepaint();
				}
			}
		}
		catch (const _com_error& e) {
			OvLog(L"COM error inserting marker");
			RecError("HandleAppBarCommand/InsertMarker", (long)e.Error(), "COM error");
		}
		catch (const std::exception& e) {
			OvLog(L"document error inserting marker");
			RecError("HandleAppBarCommand/InsertMarker", (long)E_FAIL, e.what());
		}
		catch (...) {
			OvLog(L"unknown error inserting marker");
			RecError("HandleAppBarCommand/InsertMarker", (long)E_FAIL, "unknown error");
		}
		g_mutating = false;
		g_appBarValid = false;
		RequestOverlayRepaint();
		return;
	}

	if (cmd == HtCmd_InsertNote && g_ownSelKind.empty() && g_app && !g_mutating) {
		HtMenuOp op = MapBackgroundAppBarCommand(cmd);
		if (op.opKind == HtOpKind::InsertFreeNote) {
			HtHit hit;
			HandleContextMenuCommand(op, hit, POINT{ 0, 0 });
			g_appBarValid = false;
			RequestOverlayRepaint();
		}
		return;
	}

	if (g_ownSelKind.empty() && g_app && !g_mutating) {
		HtMenuOp op = MapBackgroundAppBarCommand(cmd);
		if (op.opKind != HtOpKind::None) {
			HtHit hit;
			HandleContextMenuCommand(op, hit, POINT{ 0, 0 });
			g_appBarValid = false;
			RequestOverlayRepaint();
			return;
		}
	}

	if (HasMultiSelection() && g_app && !g_mutating) {
		if (cmd == HtCmd_Delete || cmd == HtCmd_DeleteRow) {
			g_mutating = true;
			try {
				DeleteOwnSelectionsInDocument();
			} catch (...) {
				OvLog(L"appbar multi-delete failed");
				RecError("HandleAppBarCommand/multi-delete", (long)E_FAIL, "exception");
			}
			g_mutating = false;
			g_appBarValid = false;
			RequestOverlayRepaint();
			return;
		}
		return;
	}

	if (g_ownSelKind == "ROW" && !g_ownSelId.empty()) {
		HtMenuOp op = MapRowAppBarCommand(cmd);
		if (op.opKind == HtOpKind::RenameRow) {
			for (const auto& region : g_editRegions) {
				if (region.kind == "ROW_LABEL" && region.id == g_ownSelId) {
					try { OpenInlineEditor(region); } catch (...) { OvLog(L"row rename failed"); }
					g_appBarValid = false;
					RequestOverlayRepaint();
					return;
				}
			}
			OvLog(L"row rename: no ROW_LABEL edit region");
			return;
		}
		if (op.opKind != HtOpKind::None) {
			HtHit hit;
			hit.rowId = g_ownSelId;
			hit.id = g_ownSelId;
			HandleContextMenuCommand(op, hit, POINT{ 0, 0 });
			g_appBarValid = false;
			RequestOverlayRepaint();
			return;
		}
	}

	if ((g_ownSelKind == "TASK" || g_ownSelKind == "MILESTONE") && !g_ownSelId.empty()) {
		if (cmd == HtCmd_Rename) {
			try {
				if (TryOpenInlineRename(g_ownSelKind, g_ownSelId)) {
					g_appBarValid = false;
					RequestOverlayRepaint();
					return;
				}
			} catch (...) {
				OvLog(L"appbar Rename failed");
			}
			OvLog(L"appbar Rename: no label edit region for selection");
			return;
		}
		HtMenuOp op = (g_ownSelKind == "TASK") ? MapTaskAppBarCommand(cmd) : MapMilestoneAppBarCommand(cmd);
		if (op.opKind == HtOpKind::Edit) {
			HtItemKind wantItemKind = (g_ownSelKind == "MILESTONE") ? HtItemKind::Milestone : HtItemKind::Task;
			for (const auto& item : g_hitSnapshot.items) {
				if (item.kind == wantItemKind && item.id == g_ownSelId) {
					RECT screenRect = { item.rect.left, item.rect.top, item.rect.right, item.rect.bottom };
					try { OpenCardEditor(g_ownSelKind, g_ownSelId, screenRect); } catch (...) { OvLog(L"appbar Edit failed"); }
					g_appBarValid = false;
					RequestOverlayRepaint();
					return;
				}
			}
			OvLog(L"appbar Edit: no hit rect for selection");
			return;
		}
		if (op.opKind == HtOpKind::EnterLinkMode) {
			g_linkMode = true;
			g_linkFromId = g_ownSelId;
			g_linkFromKind = g_ownSelKind;
			g_appBarValid = false;
			RequestOverlayRepaint();
			return;
		}
		if (op.opKind != HtOpKind::None) {
			HtHit hit;
			hit.id = g_ownSelId;
			hit.kind = (g_ownSelKind == "MILESTONE") ? HtItemKind::Milestone : HtItemKind::Task;
			HandleContextMenuCommand(op, hit, POINT{ 0, 0 });
			g_appBarValid = false;
			RequestOverlayRepaint();
			return;
		}
	}

	if (g_ownSelKind == "MARKER" && !g_ownSelId.empty()) {
		HtMenuOp op = MapMarkerAppBarCommand(cmd);
		if (cmd == HtCmd_Rename) {
			try {
				if (TryOpenInlineRename("MARKER", g_ownSelId)) {
					g_appBarValid = false;
					RequestOverlayRepaint();
					return;
				}
			} catch (...) {
				OvLog(L"appbar marker Rename failed");
			}
			OvLog(L"appbar marker Rename: no label edit region");
			return;
		}
		if (op.opKind == HtOpKind::Edit) {
			for (const auto& item : g_hitSnapshot.items) {
				if (item.kind == HtItemKind::Marker && item.id == g_ownSelId) {
					RECT screenRect = { item.rect.left, item.rect.top, item.rect.right, item.rect.bottom };
					try { OpenCardEditor(g_ownSelKind, g_ownSelId, screenRect); } catch (...) { OvLog(L"appbar marker Rename failed"); }
					g_appBarValid = false;
					RequestOverlayRepaint();
					return;
				}
			}
			OvLog(L"appbar marker Rename: no hit rect for selection");
			return;
		}
		if (op.opKind != HtOpKind::None) {
			HtHit hit;
			hit.id = g_ownSelId;
			hit.kind = HtItemKind::Marker;
			HandleContextMenuCommand(op, hit, POINT{ 0, 0 });
			g_appBarValid = false;
			RequestOverlayRepaint();
			return;
		}
	}

	if (g_ownSelKind == "DEP" && !g_ownSelId.empty() && g_app && !g_mutating) {
		if (cmd == HtCmd_Delete) {
			g_mutating = true;
			try {
				PpDocument doc;
				if (ReadGanttDocFromSlide(g_app, &doc)) {
					if (RemoveDependencyById(doc, g_ownSelId)) {
						RebuildChart(doc, "");
						ClearOwnSelection();
						RequestOverlayRepaint();
					}
				}
			} catch (...) {
				OvLog(L"appbar dep delete failed");
			}
			g_mutating = false;
			g_appBarValid = false;
			RequestOverlayRepaint();
			return;
		}
		return;
	}

	HtMenuOp op;
	HtHit hit;
	hit.id = g_ownSelId;
	hit.rowId = g_ownSelId;
	switch (cmd) {
	case HtCmd_ScaleDay:    op.opKind = HtOpKind::SetScale; op.scale = "day"; break;
	case HtCmd_ScaleWeek:   op.opKind = HtOpKind::SetScale; op.scale = "week"; break;
	case HtCmd_ScaleMonth:  op.opKind = HtOpKind::SetScale; op.scale = "month"; break;
	case HtCmd_ScaleQuarter:op.opKind = HtOpKind::SetScale; op.scale = "quarter"; break;
	case HtCmd_ScaleYear:   op.opKind = HtOpKind::SetScale; op.scale = "year"; break;
	case HtCmd_Delete:      op.opKind = HtOpKind::Delete; op.needsTaskId = true; break;
	case HtCmd_NudgeMinus1: op.opKind = HtOpKind::Nudge; op.nudgeDays = -1; op.needsTaskId = true; break;
	case HtCmd_NudgePlus1:  op.opKind = HtOpKind::Nudge; op.nudgeDays = 1; op.needsTaskId = true; break;
	default: return;
	}
	HandleContextMenuCommand(op, hit, POINT{ 0, 0 });
	g_appBarValid = false;
	RequestOverlayRepaint();
}

// Commit a dependency edge picked in link mode (B7.1). Clears link mode
// before mutating; keeps the source selection on success or silent rejection.
// fromId/fromKind are passed by the WM_LBUTTONUP handler from its snapshot —
// ResetDragGestureState() has already cleared g_dragId/g_linkFromKind by the
// time this runs (same stale-global trap as the hover target).
void CommitLinkPortDrop(const std::string& fromIdIn, const std::string& fromKindIn, const std::string& targetId) {
	if (!g_app || g_mutating || fromIdIn.empty()) return;
	const std::string fromId = fromIdIn;
	const std::string fromKind = fromKindIn.empty() ? "TASK" : fromKindIn;
	if (targetId.empty() || targetId == fromId) {
		g_creationFailHint = L"Cannot link a task to itself";
		SetOwnSelection(fromKind, fromId);
		RequestOverlayRepaint();
		if (g_recActive)
			RecEmitGestureCommit("LinkPort", fromId, "fail", 0, "\"reason\":\"self_or_empty\"");
		return;
	}
	g_mutating = true;
	const char* result = "fail";
	long hr = 0;
	try {
		PpDocument doc;
		if (!ReadGanttDocFromSlide(g_app, &doc)) {
			g_mutating = false;
			if (g_recActive)
				RecEmitGestureCommit("LinkPort", fromId, "fail", 0, "\"reason\":\"doc_read\"");
			return;
		}
		const std::string depId = AddDependency(doc, fromId, targetId);
		if (!depId.empty()) {
			RebuildChart(doc, fromId);
			SetOwnSelection(fromKind, fromId);
			g_creationFailHint.clear();
			RequestOverlayRepaint();
			result = "ok";
		} else {
			SetOwnSelection(fromKind, fromId);
			g_creationFailHint = (fromId == targetId)
				? L"Cannot link a task to itself"
				: L"Already linked or invalid target";
		}
	}
	catch (const _com_error& e) {
		hr = (long)e.Error();
		OvLog(L"COM error during port-link commit");
		RecError("CommitLinkPortDrop", hr, "COM error");
	}
	catch (const std::exception& e) {
		hr = (long)E_FAIL;
		OvLog(L"document error during port-link commit");
		RecError("CommitLinkPortDrop", hr, e.what());
	}
	catch (...) {
		hr = (long)E_FAIL;
		OvLog(L"unknown error during port-link commit");
		RecError("CommitLinkPortDrop", hr, "unknown error");
	}
	g_mutating = false;
	if (g_recActive) {
		std::string extra = "\"targetId\":\"";
		EntityJsonAppendEscaped(extra, targetId);
		extra += "\"";
		RecEmitGestureCommit("LinkPort", fromId, result, hr, extra);
	}
}

static void StartLinkPortDrag(const std::string& id, const std::string& kind, bool fromRight, POINT downPt) {
	ResetDragGestureState();
	g_dragKind = DragKind::LinkPort;
	g_dragId = id;
	g_linkFromKind = kind;
	g_linkDragFromRight = fromRight;
	g_gestureActive = true;
	g_dragLastPt = downPt;
	for (const auto& item : g_hitSnapshot.items) {
		HtItemKind want = (kind == "MILESTONE") ? HtItemKind::Milestone : HtItemKind::Task;
		if (item.kind == want && item.id == id) {
			g_dragAnchorRect = { item.rect.left, item.rect.top, item.rect.right, item.rect.bottom };
			break;
		}
	}
	if (g_recActive)
		RecEmitGestureStart("LinkPort", id, "", downPt);
}

void CommitLinkTarget(const std::string& targetId) {
	if (!g_linkMode || g_linkFromId.empty() || !g_app || g_mutating) return;
	const std::string fromId = g_linkFromId;
	const std::string fromKind = g_linkFromKind;
	ClearLinkMode();
	g_mutating = true;
	try {
		PpDocument doc;
		if (!ReadGanttDocFromSlide(g_app, &doc)) { g_mutating = false; return; }
		const std::string depId = AddDependency(doc, fromId, targetId);
		if (!depId.empty()) {
			RebuildChart(doc, fromId);
			SetOwnSelection(fromKind, fromId);
			RequestOverlayRepaint();
		} else {
			SetOwnSelection(fromKind, fromId);
			if (fromId == targetId) {
				g_creationFailHint = L"Cannot link a task to itself";
			} else {
				g_creationFailHint = L"Already linked or invalid target";
			}
		}
	}
	catch (const _com_error& e) {
		OvLog(L"COM error during link commit");
		RecError("CommitLinkGesture", (long)e.Error(), "COM error");
	}
	catch (const std::exception& e) {
		OvLog(L"document error during link commit");
		RecError("CommitLinkGesture", (long)E_FAIL, e.what());
	}
	catch (...) {
		OvLog(L"unknown error during link commit");
		RecError("CommitLinkGesture", (long)E_FAIL, "unknown error");
	}
	g_mutating = false;
}

// Single choke point for every user-gesture commit (drag, create, toolbar
// button, hover-insert row, inline-edit) — see CommitDragGesture/
// CommitCreateGesture/HandleToolbarButton/HandleHoverQuickAddTask/
// CommitInlineEdit, all of which call this. StartNewUndoEntryIfPossible()
// MUST run before the mutation is applied to PowerPoint so the whole gesture
// (including UpdateGantt's occasional ungroup/regroup on structural edits)
// collapses into one undo entry. UpdateGantt reconciles the existing
// CHART_ROOT in place when possible (pure move/resize/retext never deletes
// or regroups anything) and only falls back to a full delete+re-emit
// (InsertGantt) internally if reconciliation itself fails.
static int g_pptPaintLockDepth = 0;
struct PptPaintLock {
	bool locked = false;
	bool engaged = false;
	explicit PptPaintLock(bool engage = true) : engaged(engage) {
		if (engage && g_pptPaintLockDepth++ == 0 && g_pptHwnd && ::IsWindow(g_pptHwnd))
			locked = !!::LockWindowUpdate(g_pptHwnd);
	}
	~PptPaintLock() {
		if (!engaged) return;
		if (--g_pptPaintLockDepth == 0 && locked) ::LockWindowUpdate(NULL);
	}
};

static bool IsStructuralDocChange(const PpDocument& before, const PpDocument& after) {
	if (before.rows.size() != after.rows.size()) return true;
	if (before.tasks.size() != after.tasks.size()) return true;
	if (before.milestones.size() != after.milestones.size()) return true;
	if (before.markers.size() != after.markers.size()) return true;
	if (before.texts.size() != after.texts.size()) return true;
	if (before.deps.size() != after.deps.size()) return true;
	if (before.brackets.size() != after.brackets.size()) return true;
	if (before.scale != after.scale) return true;
	if (before.gridDensity != after.gridDensity) return true;
	if (before.axisNumbering != after.axisNumbering) return true;
	if (before.windowStart != after.windowStart || before.windowEnd != after.windowEnd) return true;
	if (before.railLabels != after.railLabels) return true;
	return false;
}

// M4 / SR-WIN-26 choke point: after ANY committed op the new document is known
// here, so a selection whose item the resulting scene no longer emits (window
// commit hiding the selected task, a nudge pushing it outside an explicit
// window, ...) is reset to document context in ONE place instead of per-op.
// Commits that re-assert a selection AFTER RebuildChart (drag/create/nudge)
// funnel through SetOwnSelectionPrimary, which applies the same pure predicate
// against the freshly recommitted scene cache. Clearing repaints (the cleared
// chrome must not linger) via the caller's standard RequestOverlayRepaint.
static void ResetOwnSelectionHiddenByWindow(const PpDocument& doc) {
	bool changed = false;
	for (size_t i = g_ownSelExtra.size(); i > 0; --i) {
		const OwnSelEntry& e = g_ownSelExtra[i - 1];
		if (!TimeWindowEmitsItem(doc, e.kind, e.id)) {
			g_ownSelExtra.erase(g_ownSelExtra.begin() + (ptrdiff_t)(i - 1));
			changed = true;
		}
	}
	if (!g_ownSelKind.empty() && !g_ownSelId.empty()
		&& !TimeWindowEmitsItem(doc, g_ownSelKind, g_ownSelId)) {
		OvLog(L"M4: committed op de-emitted the selected item - reset to document context");
		ClearOwnSelection();
		ClearLinkMode();
		changed = true;
	}
	if (changed) {
		InvalidateAppBarForSelectionChange();
		RequestOverlayRepaint();
	}
}

void RebuildChart(PpDocument& doc, const std::string& selectId) {
	bool structural = false;
	try {
		// The structural check only drives the PptPaintLock flicker heuristic
		// (never correctness), and the pre-op doc is exactly what the scene
		// cache holds — nothing has rewritten PP_DOC yet this op. So peek it
		// with NO COM (no shape walk, no id re-verify); only when the cache is
		// invalid do we pay for a full id-checked read + parse.
		PpDocument beforeDoc;
		bool haveBefore = Gantt_TryPeekCachedDoc(&beforeDoc);
		if (!haveBefore) haveBefore = ReadGanttDocFromSlide(g_app, &beforeDoc, /*accumulate=*/true);
		if (haveBefore) {
			structural = IsStructuralDocChange(beforeDoc, doc);
		}
	} catch (...) {
		structural = true;
	}
	PptPaintLock paintLock(structural);
	StartNewUndoEntryIfPossible();
	InvalidateHitSnapshot();
	HRESULT hr = UpdateGantt(g_app, doc, selectId);
	if (FAILED(hr)) OvLog(L"UpdateGantt failed after gesture commit");
	// M4 (SR-WIN-26): the committed doc is ground truth for what the scene
	// emits — clear/prune any selection it just hid (see helper above).
	ResetOwnSelectionHiddenByWindow(doc);
}

// ---- keyboard hotkey handlers ------------------------------------------------
// Both follow the standard commit pattern used throughout this file (see
// RebuildChart's header comment): read PP_DOC, mutate via GanttOps, rebuild,
// SetOwnSelection synchronously (RebuildChart just invalidated the hit
// snapshot, so — same reasoning as CommitDragGesture/HandleContextMenuCommand
// — SyncSelectionChromeFromOwnSelection must wait for the next Tick()).

static bool DeleteOwnSelectionsInDocument() {
	if (!g_app) return false;
	if (OwnSelCount() == 0) return false;
	PpDocument doc;
	if (!ReadGanttDocFromSlide(g_app, &doc)) return false;
	bool changed = false;
	std::vector<std::pair<std::string, std::string>> nonRows;
	std::vector<std::string> rowIds;
	if (!g_ownSelId.empty() && !g_ownSelKind.empty()) {
		if (g_ownSelKind == "ROW") rowIds.push_back(g_ownSelId);
		else nonRows.push_back({ g_ownSelKind, g_ownSelId });
	}
	for (const auto& e : g_ownSelExtra) {
		if (e.kind == "ROW") rowIds.push_back(e.id);
		else nonRows.push_back({ e.kind, e.id });
	}
	for (const auto& p : nonRows) {
		if (p.first == "ROW") rowIds.push_back(p.second);
		else changed |= DeleteById(doc, p.second);
	}
	for (const auto& rowId : rowIds) changed |= DeleteRow(doc, rowId);
	if (!changed) return false;
	RebuildChart(doc, "");
	ClearOwnSelection();
	RequestOverlayRepaint();
	return true;
}

// WM_HOTKEY(Delete): delete the selected task/milestone/text/row(s) and clear selection.
void HandleHotkeyDelete() {
	if (!g_app || g_mutating) { OvLog(L"hotkey Delete ignored: busy or no app"); return; }
	if (!IsSlideViewFocusedForHotkeys()) { OvLog(L"hotkey Delete ignored: slide view not focused (B1 scope)"); return; }
	if (OwnSelCount() == 0) {
		wchar_t buf[128];
		::swprintf_s(buf, 128, L"hotkey Delete ignored: sel %hs/%hs", g_ownSelKind.c_str(), g_ownSelId.c_str());
		OvLog(buf);
		return;
	}

	g_mutating = true;
	try {
		if (!DeleteOwnSelectionsInDocument()) {
			wchar_t buf[128];
			::swprintf_s(buf, 128, L"hotkey Delete: no-op for selection");
			OvLog(buf);
		}
	}
	catch (const _com_error& e) {
		OvLog(L"COM error during hotkey delete");
		RecError("HandleHotkeyDelete", (long)e.Error(), "COM error");
	}
	catch (const std::exception& e) {
		OvLog(L"document error during hotkey delete");
		RecError("HandleHotkeyDelete", (long)E_FAIL, e.what());
	}
	catch (...) {
		OvLog(L"unknown error during hotkey delete");
		RecError("HandleHotkeyDelete", (long)E_FAIL, "unknown error");
	}
	g_mutating = false;
}

// WM_HOTKEY(Left/Right, +-Shift): nudge the selected task/milestone by
// deltaDays (+-1 for plain Left/Right, +-7 for the Shift variants per the
// task spec), keeping the selection on the same item.
void HandleHotkeyNudge(long deltaDays) {
	if (!g_app || g_mutating) return;
	// B1 (v2.6.1) defense-in-depth: same slide-view focus scope as Delete.
	if (!IsSlideViewFocusedForHotkeys()) { OvLog(L"hotkey Nudge ignored: slide view not focused (B1 scope)"); return; }
	const std::string selId = g_ownSelId;
	const std::string selKind = g_ownSelKind;
	if (selId.empty() || (selKind != "TASK" && selKind != "MILESTONE")) return;

	g_mutating = true;
	try {
		PpDocument doc;
		if (ReadGanttDocFromSlide(g_app, &doc)) {
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
	catch (const _com_error& e) {
		OvLog(L"COM error during hotkey nudge");
		RecError("HandleHotkeyNudge", (long)e.Error(), "COM error");
	}
	catch (const std::exception& e) {
		OvLog(L"document error during hotkey nudge");
		RecError("HandleHotkeyNudge", (long)E_FAIL, e.what());
	}
	catch (...) {
		OvLog(L"unknown error during hotkey nudge");
		RecError("HandleHotkeyNudge", (long)E_FAIL, "unknown error");
	}
	g_mutating = false;
}

// Unregister every hotkey id that is currently registered (per-key state in
// g_hotkeysRegistered, so this is safe to call even after a partial-failure
// registration). Called on the shouldRegister true->false transition, and
// from HideOverlay/OverlayStop so a hidden/stopped overlay never leaves keys
// stolen system-wide.
void UnregisterAllHotkeys(const wchar_t* caller) {
	for (int i = 0; i < kHotkeyCount; ++i) {
		if (g_hotkeysRegistered[i]) {
			::UnregisterHotKey(g_hwnd, kHotkeySpecs[i].id);
			g_hotkeysRegistered[i] = false;
		}
	}
	g_hotkeysActive = false;
	wchar_t buf[96];
	::swprintf_s(buf, 96, L"hotkeys unregistered (by %s)", caller ? caller : L"?");
	OvLog(buf);
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
	OvLog(L"hotkeys registered");
}

// B1 (v2.6.1, SR-IXC-19/21): is the keyboard-input focus currently directed at
// the PowerPoint slide-editing surface (or one of OUR overlay windows), as
// opposed to the Notes pane / outline / ribbon edit control / another window?
// This scopes hotkey REGISTRATION: RegisterHotKey steals Delete/arrows
// SYSTEM-WIDE, so registering them while the user types in the Notes pane lets
// those keys nudge/delete the selected chart item — the v2.6.1 data-loss BUG
// (B1). We register ONLY while focus is on the slide surface, so anywhere else
// the OS routes the key to whatever the user is actually editing.
//
// The focused window is read from the FOREGROUND thread's GUITHREADINFO
// (hwndFocus), which works cross-thread unlike GetFocus(). Allowlist (verified
// empirically via the window-probe tree): PowerPoint's slide document window is
// "mdiClass" and its drawing surface "paneClassDC" (children of MDIClient); the
// Notes pane / outline / ribbon are NetUIHWND under NUIPane/MsoWorkPane and
// deliberately do NOT match. Our own overlay/app-bar/editor/card windows count
// as "chart focus" too (e.g. right after a click, before focus settles).
bool IsSlideViewFocusedForHotkeys() {
	if (g_slideFocusOverrideMode >= 0) return g_slideFocusOverrideMode != 0;
	// Poll-only harnesses force host-active but have no real slide view to
	// focus; treat focus as satisfied there so their existing registration path
	// is unaffected (the dedicated slide-focus override above drives the
	// negative path without real keystrokes).
	if (g_hostActiveOverrideMode >= 0) return true;

	HWND fg = ::GetForegroundWindow();
	if (!fg) return false;
	DWORD fgTid = ::GetWindowThreadProcessId(fg, nullptr);

	HWND focus = nullptr;
	GUITHREADINFO gti = {};
	gti.cbSize = sizeof(gti);
	if (::GetGUIThreadInfo(fgTid, &gti)) {
		focus = gti.hwndFocus ? gti.hwndFocus : gti.hwndActive;
	}
	if (!focus) focus = ::GetFocus();   // best-effort fallback (our thread only)
	if (!focus) return false;

	// Our own chrome windows are legitimate "chart focus".
	if (focus == g_hwnd || focus == g_appBarHwnd || focus == g_editorHwnd || focus == g_cardHwnd) return true;
	HWND focusRoot = ::GetAncestor(focus, GA_ROOT);
	if (focusRoot == g_hwnd || focusRoot == g_appBarHwnd || focusRoot == g_editorHwnd || focusRoot == g_cardHwnd) return true;

	// Otherwise the focused control must be the PowerPoint slide-editing
	// surface. Case-insensitive substring so minor casing differences across
	// Office builds still match (and Notes/ribbon NetUIHWND still does not).
	wchar_t cls[128] = {};
	::GetClassNameW(focus, cls, 128);
	for (wchar_t* p = cls; *p; ++p) *p = (wchar_t)::towlower(*p);
	if (::wcsstr(cls, L"paneclassdc") || ::wcsstr(cls, L"mdiclass")) return true;
	return false;
}

// Evaluate shouldRegister = (internal selection is TASK/MILESTONE/TEXT) AND
// (PowerPoint is the foreground app) AND (keyboard focus is on the slide
// surface / our chrome, not Notes/outline/ribbon — B1) AND (not mid-gesture/
// mutation/inline-edit) and register/unregister on a TRANSITION only. Called
// every Tick().
// TEXT is included so the Delete hotkey works on a selected text annotation
// (HandleHotkeyDelete below); the Left/Right/Shift+Left/Right nudge handlers
// already no-op for any selection kind other than TASK/MILESTONE, so
// registering the whole group for a TEXT selection is safe — those handlers
// simply do nothing when invoked with a TEXT selection.
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

	bool selectionIsTaskOrMilestone = (g_ownSelKind == "TASK" || g_ownSelKind == "MILESTONE" || g_ownSelKind == "TEXT" || g_ownSelKind == "ROW" || g_ownSelKind == "MARKER") && !g_ownSelId.empty();

	bool foregroundIsOurs = false;
	if (g_hostActiveOverrideMode >= 0) {
		// Harness override: same bypass as IsHostActiveForOverlayChrome, so
		// KEYS can rely on hotkeys being registered without a real foreground
		// window ever being ours.
		foregroundIsOurs = (g_hostActiveOverrideMode != 0);
	} else {
		HWND fg = ::GetForegroundWindow();
		if (fg) {
			DWORD fgPid = 0;
			::GetWindowThreadProcessId(fg, &fgPid);
			foregroundIsOurs = (fgPid == ::GetCurrentProcessId());
		}
	}

	bool notMidGesture = !g_gestureActive && !g_mutating && !IsEditSessionActive();

	// B1 (v2.6.1): also require keyboard focus to be on the slide surface / our
	// chrome — never register while the user is typing in the Notes pane,
	// outline, or a ribbon edit control (SR-IXC-19/21).
	bool focusInSlideView = IsSlideViewFocusedForHotkeys();

	bool shouldRegister = selectionIsTaskOrMilestone && foregroundIsOurs && focusInSlideView && notMidGesture;

	if (shouldRegister && !g_hotkeysActive) {
		RegisterAllHotkeys();
	} else if (!shouldRegister && g_hotkeysActive) {
		wchar_t buf[224];
		::swprintf_s(buf, 224, L"hotkey gate drop: sel=%hs/%hs fg=%d slideFocus=%d gesture=%d mutating=%d edit=%d",
			g_ownSelKind.c_str(), g_ownSelId.c_str(), foregroundIsOurs ? 1 : 0, focusInSlideView ? 1 : 0,
			g_gestureActive ? 1 : 0, g_mutating ? 1 : 0, IsEditSessionActive() ? 1 : 0);
		OvLog(buf);
		UnregisterAllHotkeys(L"gate");
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
		PpDocument doc;
		if (!ReadGanttDocFromSlide(g_app, &doc)) { g_mutating = false; return; }

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
	catch (const _com_error& e) {
		OvLog(L"COM error during toolbar edit");
		RecError("HandleToolbarButton", (long)e.Error(), "COM error");
	}
	catch (const std::exception& e) {
		OvLog(L"document error during toolbar edit");
		RecError("HandleToolbarButton", (long)E_FAIL, e.what());
	}
	catch (...) {
		OvLog(L"unknown error during toolbar edit");
		RecError("HandleToolbarButton", (long)E_FAIL, "unknown error");
	}
	g_mutating = false;
}

void HandleHoverQuickAddRow(bool insertAbove) {
	if (!g_app || g_mutating || g_hoverRowId.empty()) return;
	const std::string rowId = g_hoverRowId;
	g_mutating = true;
	try {
		PpDocument doc;
		if (!ReadGanttDocFromSlide(g_app, &doc)) { g_mutating = false; return; }
		std::string newRowId = insertAbove
			? AddRowAbove(doc, rowId, "New Row")
			: AddRowBelow(doc, rowId, "New Row");
		if (!newRowId.empty()) {
			RebuildChart(doc, newRowId);
			SetOwnSelection("ROW", newRowId);
			g_creationFailHint.clear();
			RequestOverlayRepaint();
		} else {
			g_creationFailHint = L"Cannot add row here";
		}
	}
	catch (const _com_error&) {
		OvLog(L"COM error during hover quick-add row");
	}
	catch (const std::exception&) {
		OvLog(L"document error during hover quick-add row");
	}
	catch (...) {
		OvLog(L"unknown error during hover quick-add row");
	}
	g_mutating = false;
}

void HandleHoverQuickAddTask() {
	if (!g_app || g_mutating || g_hoverRowId.empty()) return;
	const std::string rowId = g_hoverRowId;

	g_mutating = true;
	try {
		PpDocument doc;
		if (!ReadGanttDocFromSlide(g_app, &doc)) { g_mutating = false; return; }

		if (ComputeEmptyCellPxPerDay() <= 0.0f && !ProjectionPx().ok) {
			g_creationFailHint = L"Cannot add task — chart scale unavailable";
			g_mutating = false;
			return;
		}
		const long centerDay = DayAtVisibleCenter();
		const std::string startISO = DaysToDate(centerDay);
		const std::string endISO = DaysToDate(centerDay + 4);
		std::string taskId = AddTask(doc, rowId, "New Task", startISO, endISO);
		if (!taskId.empty()) {
			RebuildChart(doc, taskId);
			SetOwnSelection("TASK", taskId);
			g_creationFailHint.clear();
			RequestOverlayRepaint();
			wchar_t buf[128];
			::swprintf_s(buf, 128, L"hover quick-add task in row %hs", rowId.c_str());
			OvLog(buf);
		} else {
			g_creationFailHint = L"Cannot add task — row unavailable";
		}
	}
	catch (const _com_error&) {
		OvLog(L"COM error during hover quick-add task");
	}
	catch (const std::exception&) {
		OvLog(L"document error during hover quick-add task");
	}
	catch (...) {
		OvLog(L"unknown error during hover quick-add task");
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
	if ((op.opKind == HtOpKind::Delete || op.opKind == HtOpKind::DeleteRow) && HasMultiSelection()) {
		g_mutating = true;
		try {
			DeleteOwnSelectionsInDocument();
		} catch (...) {
			OvLog(L"context menu multi-delete failed");
		}
		g_mutating = false;
		return;
	}

	g_mutating = true;
	try {
		PpDocument doc;
		if (!ReadGanttDocFromSlide(g_app, &doc)) { g_mutating = false; return; }

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
			const std::string rowId = !hit.rowId.empty() ? hit.rowId : hit.id;
			changed = DeleteRow(doc, rowId);
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
				if (changed) {
					selectKind = "MILESTONE";
				} else if (hit.kind == HtItemKind::Marker || FindMarker(doc, hit.id)) {
					for (const auto& mk : doc.markers) {
						if (mk.id == hit.id) {
							changed = SetMarkerDate(doc, hit.id, DaysToDate(DateToDays(mk.date) + op.nudgeDays));
							if (changed) selectKind = "MARKER";
							break;
						}
					}
				}
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
		case HtOpKind::SetTaskColor: {
			changed = SetTaskColor(doc, hit.id, op.color ? op.color : "");
			if (changed) { selectId = hit.id; selectKind = "TASK"; }
			break;
		}
		case HtOpKind::CycleLabelPlacement: {
			if (const PpTask* task = FindTask(doc, hit.id)) {
				std::string next = "rail";
				if (task->labelPlacement.empty() || task->labelPlacement == "bar") next = "rail";
				else if (task->labelPlacement == "rail") next = "both";
				else next = "bar";
				changed = SetLabelPlacement(doc, hit.id, next);
				if (changed) { selectId = hit.id; selectKind = "TASK"; }
			}
			break;
		}
		case HtOpKind::AddNote: {
			const std::string noteKind = (hit.kind == HtItemKind::Milestone) ? "MILESTONE" : "TASK";
			selectId = AddText(doc, "Note", hit.id, "", "");
			changed = !selectId.empty();
			if (changed) { selectKind = noteKind; selectId = hit.id; }
			break;
		}
		case HtOpKind::Unlink: {
			const int removed = RemoveDependenciesTouching(doc, hit.id);
			changed = removed > 0;
			if (changed) {
				selectId = hit.id;
				selectKind = (hit.kind == HtItemKind::Milestone) ? "MILESTONE" : "TASK";
			}
			break;
		}
		case HtOpKind::InsertFreeNote: {
			std::string rowId, dateISO;
			DefaultFreeNoteCellAtVisibleCenter(rowId, dateISO);
			selectId = AddText(doc, "Note", "", rowId, dateISO);
			changed = !selectId.empty();
			if (changed) selectKind = "TEXT";
			break;
		}
		case HtOpKind::AddRow: {
			// needsRowId => "Add Row Below" (afterRowId = hit.rowId); the
			// background "Add Row" command has needsRowId == false and
			// appends (AddRow's empty-afterRowId behavior).
			if (op.needsRowId) {
				selectId = AddRowBelow(doc, hit.rowId, "New Row");
			} else {
				selectId = AddRow(doc, "New Row", "");
			}
			changed = !selectId.empty();
			if (changed) { selectKind = "ROW"; }
			break;
		}
		case HtOpKind::AddRowAbove: {
			selectId = AddRowAbove(doc, hit.rowId, "New Row");
			changed = !selectId.empty();
			if (changed) { selectKind = "ROW"; }
			break;
		}
		case HtOpKind::MoveRowUp: {
			changed = MoveRowUp(doc, hit.rowId);
			if (changed) { selectId = hit.rowId; selectKind = "ROW"; }
			break;
		}
		case HtOpKind::MoveRowDown: {
			changed = MoveRowDown(doc, hit.rowId);
			if (changed) { selectId = hit.rowId; selectKind = "ROW"; }
			break;
		}
		case HtOpKind::IndentRow: {
			changed = IndentRow(doc, hit.rowId);
			if (changed) { selectId = hit.rowId; selectKind = "ROW"; }
			break;
		}
		case HtOpKind::OutdentRow: {
			changed = OutdentRow(doc, hit.rowId);
			if (changed) { selectId = hit.rowId; selectKind = "ROW"; }
			break;
		}
		case HtOpKind::AddTaskAtPoint: {
			if (ComputeEmptyCellPxPerDay() <= 0.0f && !ProjectionPx().ok) {
				g_creationFailHint = L"Cannot add task — chart scale unavailable";
				break;
			}
			const long screenX = clientPt.x + g_windowOriginX;
			const long anchorDay = AnchorDayFromScreenX(screenX);
			const std::string startISO = DaysToDate(anchorDay);
			const std::string endISO = DaysToDate(anchorDay + 6);
			selectId = AddTask(doc, hit.rowId, "New task", startISO, endISO);
			changed = !selectId.empty();
			if (changed) {
				selectKind = "TASK";
				g_creationFailHint.clear();
			} else {
				g_creationFailHint = L"Cannot add task here";
			}
			break;
		}
		case HtOpKind::AddMilestoneAtPoint: {
			if (ComputeEmptyCellPxPerDay() <= 0.0f && !ProjectionPx().ok) {
				g_creationFailHint = L"Cannot add milestone — chart scale unavailable";
				break;
			}
			const long screenX = clientPt.x + g_windowOriginX;
			const long anchorDay = AnchorDayFromScreenX(screenX);
			selectId = AddMilestone(doc, hit.rowId, "New milestone", DaysToDate(anchorDay));
			changed = !selectId.empty();
			if (changed) {
				selectKind = "MILESTONE";
				g_creationFailHint.clear();
			} else {
				g_creationFailHint = L"Cannot add milestone here";
			}
			break;
		}
		case HtOpKind::AddNoteAtPoint: {
			if (ComputeEmptyCellPxPerDay() <= 0.0f && !ProjectionPx().ok) {
				g_creationFailHint = L"Cannot add note — chart scale unavailable";
				break;
			}
			const long screenX = clientPt.x + g_windowOriginX;
			const long anchorDay = AnchorDayFromScreenX(screenX);
			selectId = AddText(doc, "Note", "", hit.rowId, DaysToDate(anchorDay));
			changed = !selectId.empty();
			if (changed) {
				selectKind = "TEXT";
				g_creationFailHint.clear();
			} else {
				g_creationFailHint = L"Cannot add note here";
			}
			break;
		}
		case HtOpKind::InsertTaskBackground: {
			std::string rowId;
			std::string dateISO;
			DefaultFreeNoteCellAtVisibleCenter(rowId, dateISO);
			if (rowId.empty()) rowId = FirstRowId(doc);
			if (rowId.empty()) rowId = AddRow(doc, "New Row", "");
			if (!rowId.empty()) {
				std::string start, end;
				DefaultTaskDates(doc, rowId, "", start, end);
				selectId = AddTask(doc, rowId, "New Task", start, end);
				changed = !selectId.empty();
				if (changed) selectKind = "TASK";
			}
			break;
		}
		case HtOpKind::InsertMilestoneBackground: {
			std::string rowId;
			std::string dateISO;
			DefaultFreeNoteCellAtVisibleCenter(rowId, dateISO);
			if (rowId.empty()) rowId = FirstRowId(doc);
			if (rowId.empty()) rowId = AddRow(doc, "New Row", "");
			if (!rowId.empty()) {
				selectId = AddMilestone(doc, rowId, "New Milestone", dateISO);
				changed = !selectId.empty();
				if (changed) selectKind = "MILESTONE";
			}
			break;
		}
		case HtOpKind::InsertMarkerBackground: {
			const std::string dateISO = DefaultMarkerDateAtVisibleCenter();
			selectId = AddMarker(doc, "custom", "Marker", dateISO);
			changed = !selectId.empty();
			if (changed) selectKind = "MARKER";
			break;
		}
		case HtOpKind::SetGridDensity: {
			const std::string keepKind = g_ownSelKind;
			const std::string keepId = g_ownSelId;
			if (op.gridDensity && std::string(op.gridDensity) == "__cycle__") {
				const std::string cur = doc.gridDensity.empty() ? "auto" : doc.gridDensity;
				const char* next = "auto";
				if (cur == "auto") next = "week";
				else if (cur == "week") next = "month";
				else if (cur == "month") next = "none";
				changed = SetGridDensity(doc, next);
			} else if (op.gridDensity) {
				changed = SetGridDensity(doc, op.gridDensity);
			}
			if (changed && !keepId.empty() && !keepKind.empty()) {
				selectId = keepId;
				selectKind = keepKind;
			}
			break;
		}
		case HtOpKind::ToggleRailLabels: {
			const std::string keepKind = g_ownSelKind;
			const std::string keepId = g_ownSelId;
			changed = SetRailLabelsGlobal(doc, !doc.railLabels);
			if (changed && !keepId.empty() && !keepKind.empty()) {
				selectId = keepId;
				selectKind = keepKind;
			}
			break;
		}
		case HtOpKind::SetRailLabels: {
			const std::string keepKind = g_ownSelKind;
			const std::string keepId = g_ownSelId;
			changed = SetRailLabelsGlobal(doc, op.railLabels);
			if (changed && !keepId.empty() && !keepKind.empty()) {
				selectId = keepId;
				selectKind = keepKind;
			}
			break;
		}
		case HtOpKind::SetAxisNumbering: {
			const std::string keepKind = g_ownSelKind;
			const std::string keepId = g_ownSelId;
			changed = SetAxisNumbering(doc, op.axisNumbering ? op.axisNumbering : "day");
			if (changed && !keepId.empty() && !keepKind.empty()) {
				selectId = keepId;
				selectKind = keepKind;
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
			} else if (op.opKind == HtOpKind::Delete || op.opKind == HtOpKind::DeleteRow) {
				ClearOwnSelection();
			} else if (selectKind == "ROW" && !hit.rowId.empty() && op.opKind != HtOpKind::DeleteRow) {
				SetOwnSelection("ROW", hit.rowId);
			}
			RequestOverlayRepaint();
			wchar_t buf[160];
			::swprintf_s(buf, 160, L"context menu op %d applied (id=%hs)", (int)op.opKind, hit.id.c_str());
			OvLog(buf);
		}
	}
	catch (const _com_error& e) {
		OvLog(L"COM error during context menu command");
		RecError("HandleContextMenuCommand", (long)e.Error(), "COM error");
	}
	catch (const std::exception& e) {
		OvLog(L"document error during context menu command");
		RecError("HandleContextMenuCommand", (long)E_FAIL, e.what());
	}
	catch (...) {
		OvLog(L"unknown error during context menu command");
		RecError("HandleContextMenuCommand", (long)E_FAIL, "unknown error");
	}
	g_mutating = false;
}

// Build the themed context menu from BuildMenuForZone(hit's zone) and show it
// via PowerPlannerThemeMenu (SR-THEME-03), then execute whatever command the
// user picked. Skipped when PP_OVERLAY_NO_MENU is set (harness paths that only
// need selection, not a visible menu). ThemeMenu_Show pumps until dismissed.
void ShowContextMenuForHit(const HtHit& hit, POINT clientPt) {
	if (hit.zone == HtZone::Outside) return;
	bool hasRowId = !hit.rowId.empty();
	if (hit.zone == HtZone::Label && hit.kind == HtItemKind::RowLabel) {
		hasRowId = !hit.id.empty();
	}

	PpDocument doc;
	std::string hitId;
	try {
		if (g_app) {
			std::string json = ReadGanttFromSlide(g_app);
			if (!json.empty()) doc = DocumentFromJson(json);
		}
	}
	catch (const _com_error& e) {
		RecError("ShowContextMenuForHit/read", (long)e.Error(), "COM error");
	}
	catch (const std::exception& e) {
		RecError("ShowContextMenuForHit/read", (long)E_FAIL, e.what());
	}
	catch (...) {
		RecError("ShowContextMenuForHit/read", (long)E_FAIL, "unknown error");
	}

	if (hit.zone == HtZone::TaskBody || hit.zone == HtZone::TaskEdgeL || hit.zone == HtZone::TaskEdgeR
		|| hit.zone == HtZone::TaskProgressEdge
		|| hit.zone == HtZone::Milestone || hit.zone == HtZone::Marker || hit.zone == HtZone::Text) {
		hitId = hit.id;
	} else if (hit.zone == HtZone::Label && hit.kind == HtItemKind::RowLabel) {
		hitId = hit.id;
	} else if (hasRowId) {
		hitId = hit.rowId;
	}

	std::vector<HtMenuItem> items = BuildMenuForZone(hit.zone, hit.kind, hasRowId, doc, hitId);
	if (items.empty()) return;

	// Under the ops/harness smoke check, skip showing the actual modal popup
	// (it cannot be automated in-process) — selection is already set by the
	// caller before this function runs, which is all that check verifies.
	if (::GetEnvironmentVariableW(L"PP_OVERLAY_NO_MENU", NULL, 0) > 0) return;

	POINT screenPt = { clientPt.x + g_windowOriginX, clientPt.y + g_windowOriginY };
	int cmd = ThemeMenu_Show(items, screenPt, g_hwnd, true);
	::PostMessageW(g_hwnd, WM_NULL, 0, 0);

	if (cmd > 0) {
		HtMenuOp op = MapMenuCommand(hit.zone, cmd, hit.kind, hasRowId, doc, hitId);
		try {
			if (cmd == HtCmd_Rename) {
				if (op.opKind == HtOpKind::RenameRow) {
					const std::string rowId = !hit.rowId.empty() ? hit.rowId : hit.id;
					for (const auto& region : g_editRegions) {
						if (region.kind == "ROW_LABEL" && region.id == rowId) {
							OpenInlineEditor(region);
							break;
						}
					}
					return;
				}
				std::string selKind = "TASK";
				if (hit.zone == HtZone::Milestone) selKind = "MILESTONE";
				else if (hit.zone == HtZone::Marker) selKind = "MARKER";
				else if (hit.zone == HtZone::Text) selKind = "TEXT";
				TryOpenInlineRename(selKind, hit.id);
				return;
			}
			if (op.opKind == HtOpKind::Edit) {
				HtItemKind wantItemKind = HtItemKind::Task;
				std::string selKind = "TASK";
				if (hit.zone == HtZone::Milestone) { wantItemKind = HtItemKind::Milestone; selKind = "MILESTONE"; }
				else if (hit.zone == HtZone::Marker) { wantItemKind = HtItemKind::Marker; selKind = "MARKER"; }
				else if (hit.zone == HtZone::Text) { wantItemKind = HtItemKind::Text; selKind = "TEXT"; }
				for (const auto& item : g_hitSnapshot.items) {
					if (item.kind == wantItemKind && item.id == hit.id) {
						RECT screenRect = { item.rect.left, item.rect.top, item.rect.right, item.rect.bottom };
						OpenCardEditor(selKind, hit.id, screenRect);
						break;
					}
				}
				return;
			}
			if (op.opKind == HtOpKind::EnterLinkMode) {
				g_linkMode = true;
				g_linkFromId = hit.id;
				g_linkFromKind = (hit.kind == HtItemKind::Milestone) ? "MILESTONE" : "TASK";
				g_appBarValid = false;
				RequestOverlayRepaint();
				return;
			}
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
// click before focus returns to the main frame), OR the foreground window is an
// owned popup of the tracked PowerPoint root (GA_ROOTOWNER).
//
// Also requires ppRoot to be non-iconic (not minimized) and visible.
// The chrome windows can be created before Tick has resolved g_pptHwnd, in
// which case CreateWindowExW got a NULL owner. Without WS_EX_TOPMOST an
// unowned popup would sink behind PowerPoint, so rebind the owner as soon as
// the host HWND is known. Cheap and idempotent: GetWindow(GW_OWNER) is a
// local lookup and the SetWindowLongPtr only fires when the owner actually
// differs.
static void EnsureChromeOwner(HWND ppRoot) {
	if (!ppRoot || !::IsWindow(ppRoot)) return;
	HWND wins[] = { g_hwnd, g_appBarHwnd, g_editorHwnd, g_cardHwnd };
	for (HWND w : wins) {
		if (!w || !::IsWindow(w)) continue;
		if (::GetWindow(w, GW_OWNER) == ppRoot) continue;
		::SetWindowLongPtrW(w, GWLP_HWNDPARENT, (LONG_PTR)ppRoot);
	}
}

bool IsHostActiveForOverlayChrome(HWND ppRoot) {
	// Harness override: bypass the real GetForegroundWindow-based logic
	// entirely so the SCOPE stage (and any other stage relying on host-active
	// scoping) can flip this deterministically without a real
	// SetForegroundWindow call. See Overlay_SetHostActiveOverrideForTest.
	if (g_hostActiveOverrideMode >= 0) return g_hostActiveOverrideMode != 0;

	if (!ppRoot) return false;
	if (::IsIconic(ppRoot)) return false;
	if (!::IsWindowVisible(ppRoot)) return false;

	HWND fg = ::GetForegroundWindow();
	if (!fg) return false;
	HWND fgRoot = ::GetAncestor(fg, GA_ROOT);

	if (fgRoot == ppRoot) return true;
	if (fg == g_hwnd || fg == g_editorHwnd || fg == g_cardHwnd || fg == g_appBarHwnd
		|| fg == ThemeMenu_Hwnd() || fg == ThemeMenu_FlyoutHwnd()) return true;
	if (fgRoot == g_hwnd || fgRoot == g_editorHwnd || fgRoot == g_cardHwnd || fgRoot == g_appBarHwnd
		|| fgRoot == ThemeMenu_Hwnd() || fgRoot == ThemeMenu_FlyoutHwnd()) return true;

	HWND fgRootOwner = ::GetAncestor(fg, GA_ROOTOWNER);
	if (fgRootOwner == ppRoot) return true;
	if (fgRootOwner == g_hwnd || fgRootOwner == g_editorHwnd || fgRootOwner == g_cardHwnd || fgRootOwner == g_appBarHwnd
		|| fgRootOwner == ThemeMenu_Hwnd() || fgRootOwner == ThemeMenu_FlyoutHwnd()) return true;

	return false;
}

static bool IsHostViewEditableForOverlay(PowerPoint::DocumentWindowPtr win) {
	try {
		if (!win) return false;
		long vt = (long)win->GetViewType();           // PpViewType
		if (vt != 9 /*ppViewNormal*/ && vt != 1 /*ppViewSlide*/) return false;
		if (g_app) {
			PowerPoint::SlideShowWindowsPtr ss = g_app->GetSlideShowWindows();
			if (ss && ss->GetCount() >= 1) return false;   // presenting: hide
		}
		return true;
	} catch (const _com_error&) { return false; } catch (...) { return false; }
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
	if (!g_app) { if (g_shown) OvLog(L"Tick: g_app null - hiding"); HideOverlay(); HideAppBar(); return; }
	try {
		RECT oldChartRect = g_chartScreenRect;
		RECT oldSelRect = g_selScreenRect;
		bool oldHasSelectionChrome = g_hasSelectionChrome;
		std::string oldSelId = g_selId;
		std::string oldSelKind = g_selKind;
		bool leftButtonDown = (::GetKeyState(VK_LBUTTON) & 0x8000) != 0;
		bool mouseStateChanged = leftButtonDown != g_lastLeftButtonDown;
		g_lastLeftButtonDown = leftButtonDown;

		// VK_ESCAPE cancels link mode first (B7.1), then an in-progress capture
		// gesture (drag or pending click). Latency is bounded by the 150ms tick.
		if (g_linkMode && (::GetAsyncKeyState(VK_ESCAPE) & 0x8000)) {
			ClearLinkMode();
			RequestOverlayRepaint();
		} else if (g_captureActive && (::GetAsyncKeyState(VK_ESCAPE) & 0x8000)) {
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
			bool fgIsOurs;
			if (g_hostActiveOverrideMode >= 0) {
				fgIsOurs = (g_hostActiveOverrideMode != 0);
			} else {
				HWND fg = ::GetForegroundWindow();
				DWORD fgPid = 0;
				if (fg) ::GetWindowThreadProcessId(fg, &fgPid);
				fgIsOurs = (fg && fgPid == ::GetCurrentProcessId());
			}
			if (fgIsOurs) {
				ClearOwnSelection();
			}
		}

		PowerPoint::DocumentWindowPtr win = g_app->GetActiveWindow();
		if (!win) { if (g_shown) OvLog(L"Tick: no active window - hiding"); HideOverlay(); HideAppBar(); return; }
		g_pptHwnd = (HWND)(INT_PTR)g_app->GetHWND();

		// Harness stand-down: the trace/e2e harnesses drive their OWN in-process
		// overlay against a PowerPoint they launch; the registered add-in inside
		// that PowerPoint must not create a SECOND overlapping overlay/app-bar
		// (two PowerPlannerAppBar windows = mixed chrome, corrupted captures).
		// The harness tags its presentation PP_HARNESS=1; the add-in (running in
		// POWERPNT.EXE, unlike the harness exes) hides while it is active. The
		// COM tag read is cached and re-checked every 64 ticks (~10s).
		{
			static bool s_isPowerPntProcess = []() {
				wchar_t path[MAX_PATH] = L"";
				::GetModuleFileNameW(NULL, path, MAX_PATH);
				const wchar_t* base = ::wcsrchr(path, L'\\');
				base = base ? base + 1 : path;
				return ::_wcsicmp(base, L"POWERPNT.EXE") == 0;
			}();
			static int s_harnessCheckTick = 0;
			// FAIL CLOSED: until a tag read SUCCEEDS and proves this is a real
			// user presentation, show nothing. A startup-flake throw used to
			// fail open, and the add-in's bar photobombed one frame of every
			// harness capture; a real user merely sees chrome one tick later.
			static bool s_harnessPresentation = true;
			if (s_isPowerPntProcess) {
				if ((s_harnessCheckTick & 63) == 0) {
					try {
						PowerPoint::_PresentationPtr pres = win->GetPresentation();
						_bstr_t tag = pres->GetTags()->Item(_bstr_t(L"PP_HARNESS"));
						s_harnessPresentation = tag.length() && ::wcscmp((const wchar_t*)tag, L"1") == 0;
						++s_harnessCheckTick; // cache only after a SUCCESSFUL read
					} catch (...) {
						s_harnessPresentation = true; // unknown = stand down; retry next tick
					}
				} else {
					++s_harnessCheckTick;
				}
				if (s_harnessPresentation) {
					HideOverlay();
					HideAppBar(true);
					return;
				}
			}
		}

		// Host-scoping: the overlay chrome (selection frame + toolbar) may be
		// visible ONLY while PowerPoint is the active app (see
		// IsHostActiveForOverlayChrome's comment above). If PowerPoint is not
		// active (minimized, or some unrelated app is foreground), hide the
		// overlay and both auxiliary editor windows and bail out early, same
		// as the no-chart path below. Latency is bounded by one 150ms tick,
		// which is acceptable per the task spec.
		HWND ppRoot = ::GetAncestor(g_pptHwnd, GA_ROOT);
		EnsureChromeOwner(ppRoot);
		g_lastHostActive = IsHostActiveForOverlayChrome(ppRoot);
		if (!g_lastHostActive) {
			HideOverlay();
			HideAppBar(true);
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

		// Harness override (>=1 forces active) also bypasses the view and
		// host-rect fail-closed gates: the harness drives synthetic window
		// states where real COM/GetWindowRect checks are meaningless.
		g_lastViewOk = (g_hostActiveOverrideMode >= 1) ? true : IsHostViewEditableForOverlay(win);
		if (!g_lastViewOk) {
			HideOverlay();
			HideAppBar(true);
			if (g_editorHwnd && ::IsWindow(g_editorHwnd)) ::ShowWindow(g_editorHwnd, SW_HIDE);
			if (g_cardHwnd && ::IsWindow(g_cardHwnd)) ::ShowWindow(g_cardHwnd, SW_HIDE);
			return;
		}

		PowerPoint::_SlidePtr slide = win->GetView()->GetSlide();
		PowerPoint::ShapePtr chart = FindChartRoot(slide);
		if (!chart) {
			if (g_shown) OvLog(L"Tick: no CHART_ROOT found - hiding overlay");
			HideOverlay();
			HideAppBar();
			return;
		}

		float chartLeft = chart->GetLeft(), chartTop = chart->GetTop();
		float chartWidth = chart->GetWidth(), chartHeight = chart->GetHeight();
		g_chartLeftPt = chartLeft;
		g_chartWidthPt = chartWidth;
		g_chartScreenRect = {
			win->PointsToScreenPixelsX(chartLeft),
			win->PointsToScreenPixelsY(chartTop),
			win->PointsToScreenPixelsX(chartLeft + chartWidth),
			win->PointsToScreenPixelsY(chartTop + chartHeight)
		};
		NormalizeRect(g_chartScreenRect);
		if (g_hostActiveOverrideMode < 0) {
			RECT hostRc{}; RECT inter{};
			if (!::GetWindowRect(ppRoot, &hostRc) ||
				!::IntersectRect(&inter, &g_chartScreenRect, &hostRc)) {
				HideOverlay();
				HideAppBar(true);
				if (g_editorHwnd && ::IsWindow(g_editorHwnd)) ::ShowWindow(g_editorHwnd, SW_HIDE);
				if (g_cardHwnd && ::IsWindow(g_cardHwnd)) ::ShowWindow(g_cardHwnd, SW_HIDE);
				return;
			}
		}
		g_chartProj = Narrow((const wchar_t*)chart->GetTags()->Item(_bstr_t(L"PP_PROJ")));
		_bstr_t rowYTag = chart->GetTags()->Item(_bstr_t(L"PP_ROWY"));
		g_chartRowY = rowYTag.length() ? Narrow((const wchar_t*)rowYTag) : "";
		bool chartChanged = !SameRect(oldChartRect, g_chartScreenRect);
		BuildRowBands(chart, win);
		if (g_dragCommitEcho.active) ClearDragCommitEcho();
		bool hoverChanged = UpdateHoverFromCursor();
		// M6: re-derive move-grip hover from the cursor each tick so the "Move
		// chart" tooltip clears when the pointer leaves the overlay (no
		// WM_MOUSEMOVE fires once the cursor is outside our window). The grip
		// rect is window-relative; convert the (screen) cursor accordingly.
		{
			POINT cur{};
			bool overGrip = false;
			if (g_gripValid && !g_gestureActive && OverlayGetCursorPos(&cur)) {
				POINT winPt = { cur.x - g_windowOriginX, cur.y - g_windowOriginY };
				overGrip = GripFromClientPoint(winPt);
			}
			if (overGrip != g_gripHover) { g_gripHover = overGrip; hoverChanged = true; }
		}
		// Transition-only (hidden<->shown), matching UpdateHoverFromCursor's
		// own return-bool pattern above -- see UpdateEmptyCellHoverHint's
		// comment for why re-deriving "due" separately here (as before) could
		// double-fire relative to when Paint actually flips the flag.
		const bool emptyCellHintChanged = UpdateEmptyCellHoverHint();

		ClearSelectionState();
		bool chartRootNativelySelected = false;

		// SINGLE-SELECTION CONTRACT (M3): resolve the FIRST shape of the current
		// PowerPoint-native selection here (COM side), then hand the model
		// decision to the shared Overlay_OnNativeSelectionChanged so this 150ms
		// Tick poll (watchdog) and the Connect.cpp WindowSelectionChange sink
		// (instant path) apply identical suppression / ownSel-mirror /
		// clear-on-foreign logic. CHART_ROOT stays natively selectable (move
		// grip, Alt+click escape hatch); its chrome is still driven from the
		// native Selection geometry below. Chart CHILDREN are overlay-only
		// selections — their native selection is suppressed (Unselect).
		bool hasShapeSel = false;
		std::string firstKind, firstId;
		PowerPoint::ShapePtr firstShape;
		// ChildShapeRange detail for the recorder (SR-REC-06); Note before handler.
		bool hasChildShapeRange = false;
		std::string childKind, childId;
		PowerPoint::SelectionPtr sel = win->GetSelection();
		if (sel && sel->GetType() == PowerPoint::ppSelectionShapes) {
			PowerPoint::ShapeRangePtr sr = sel->GetShapeRange();
			if (sr && sr->GetCount() >= 1) {
				hasShapeSel = true;
				firstShape = sr->Item(_variant_t(1L));
				_bstr_t kind = firstShape->GetTags()->Item(_bstr_t(L"PP_KIND"));
				firstKind = kind.length() ? Narrow((const wchar_t*)kind) : "";
				_bstr_t id = firstShape->GetTags()->Item(_bstr_t(L"PP_ID"));
				firstId = id.length() ? Narrow((const wchar_t*)id) : "";
			}
			// HasChildShapeRange is the live-vs-harness blind spot (2026-07-18 #1).
			try {
				if (sel->GetHasChildShapeRange() == VARIANT_TRUE) {
					hasChildShapeRange = true;
					PowerPoint::ShapeRangePtr csr = sel->GetChildShapeRange();
					if (csr && csr->GetCount() >= 1) {
						PowerPoint::ShapePtr csh = csr->Item(_variant_t(1L));
						_bstr_t ck = csh->GetTags()->Item(_bstr_t(L"PP_KIND"));
						childKind = ck.length() ? Narrow((const wchar_t*)ck) : "";
						_bstr_t ci = csh->GetTags()->Item(_bstr_t(L"PP_ID"));
						childId = ci.length() ? Narrow((const wchar_t*)ci) : "";
					}
				}
			} catch (...) {
				// Property unavailable / COM fail — leave child detail empty.
			}
		}
		int nativeAction = Overlay_OnNativeSelectionChangedWithChild(
			firstKind.c_str(), firstId.c_str(), hasShapeSel,
			hasChildShapeRange, childKind.c_str(), childId.c_str());
		if (nativeAction == OVERLAY_NATIVE_SEL_SUPPRESS_CHILD) {
			try {
				win->GetSelection()->Unselect();
			} catch (const _com_error& e) {
				OvLog(L"COM error unselecting suppressed chart child");
				RecError("Tick/Unselect", (long)e.Error(), "COM error unselecting suppressed chart child");
			} catch (...) {
				OvLog(L"COM error unselecting suppressed chart child");
				RecError("Tick/Unselect", (long)E_FAIL, "unknown error unselecting suppressed chart child");
			}
			OvLog((L"suppressed native selection of chart child PP_KIND=" + Widen(firstKind)).c_str());
		} else if (hasShapeSel && firstKind == "CHART_ROOT" && firstShape) {
			if (g_ownSelKind.empty()) {
				// Only drive full-chart root chrome from native sel when no item is selected via overlay.
				// This prevents intermittent "full component takeover" when clicking items (native often remains root group).
				chartRootNativelySelected = true;
				g_selKind = firstKind;
				g_selId = firstId;
				float left = firstShape->GetLeft(), top = firstShape->GetTop(), w = firstShape->GetWidth(), h = firstShape->GetHeight();
				g_selScreenRect = {
					win->PointsToScreenPixelsX(left),
					win->PointsToScreenPixelsY(top),
					win->PointsToScreenPixelsX(left + w),
					win->PointsToScreenPixelsY(top + h)
				};
				NormalizeRect(g_selScreenRect);
				g_hasSelectionChrome = true;
			}
			// else: have active item ownSel (from click); keep item chrome. Do not override with full root area.
			// (Grip click explicitly clears ownSel before selecting root.)
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
		RECT slideRect = g_chartScreenRect;
		try {
			PowerPoint::_PresentationPtr pres = g_app->GetActivePresentation();
			if (pres) {
				PowerPoint::PageSetupPtr ps = pres->GetPageSetup();
				float sw = ps->GetSlideWidth(), sh = ps->GetSlideHeight();
				slideRect = { win->PointsToScreenPixelsX(0.0f), win->PointsToScreenPixelsY(0.0f),
					win->PointsToScreenPixelsX(sw), win->PointsToScreenPixelsY(sh) };
				NormalizeRect(slideRect);
			}
		} catch (...) { slideRect = g_chartScreenRect; }
		ShowAppBar(g_chartScreenRect, slideRect);
		if (chartChanged || hoverChanged || emptyCellHintChanged || !g_creationFailHint.empty() ||
			mouseStateChanged || !SameSelectionState(oldHasSelectionChrome, oldSelRect, oldSelId, oldSelKind)) {
			RequestOverlayRepaint();
		}
		if (g_recActive) {
			const ULONGLONG now = ::GetTickCount64();
			if (g_recLastIdleSnapshotMs == 0 || now - g_recLastIdleSnapshotMs >= 1000) {
				g_recLastIdleSnapshotMs = now;
				RecEmitSnapshot();
			}
			RecCapturePendingFrame();
		}

		std::string k = g_selKind;
		if (k != g_lastKind) {
			g_lastKind = k;
			wchar_t buf[160];
			::swprintf_s(buf, 160, L"shown for chart (%ld,%ld)-(%ld,%ld), selection PP_KIND=%hs",
				g_chartScreenRect.left, g_chartScreenRect.top, g_chartScreenRect.right, g_chartScreenRect.bottom, k.c_str());
			OvLog(buf);
		}
	} catch (const _com_error& e) {
		wchar_t buf[96];
		::swprintf_s(buf, 96, L"Tick: COM error 0x%08lX - hiding overlay", (unsigned long)e.Error());
		OvLog(buf);
		RecError("Tick", (long)e.Error(), "COM error");
		HideOverlay();
		HideAppBar();
	} catch (const std::exception& e) {
		OvLog(L"Tick: std::exception - hiding overlay");
		RecError("Tick", (long)E_FAIL, e.what());
		HideOverlay();
		HideAppBar();
	} catch (...) {
		OvLog(L"Tick: unknown exception - hiding overlay");
		RecError("Tick", (long)E_FAIL, "unknown error");
		HideOverlay();
		HideAppBar();
	}
}

void CALLBACK TimerProc(HWND, UINT, UINT_PTR, DWORD) { Tick(); }

} // namespace

// ---- shared native-selection-change handler (SR-SMO-05 / ARC-07) -----------
// SINGLE-SELECTION CONTRACT (M3): CHART_ROOT is the only real PowerPoint
// selection a user is meant to see for the whole component. Every internal
// primitive (task bars, progress fills, milestone diamonds, labels, connectors,
// row bands, notes, ...) is a CHILD of that group and is an OVERLAY-ONLY
// selection (ownSel) — its PowerPoint-native selection is suppressed. This
// function is the ONE place that decision is made, so the 150ms Tick poll
// (watchdog) and the Connect.cpp WindowSelectionChange COM sink (instant path,
// which closes the M6 delete-desync race) apply identical logic. It is COM-free
// on purpose: the CALLER resolves the current native selection via COM and
// performs any Unselect() itself, so it is safe to call from both translation
// units (Overlay is linked into the poll-only harness; Connect is not). Defined
// OUTSIDE the anonymous namespace above so it has external linkage for Connect.
int Overlay_OnNativeSelectionChanged(const char* firstShapeKind, const char* firstShapeId, bool hasShapeSelection) {
	const std::string kind = firstShapeKind ? firstShapeKind : "";
	const std::string id = firstShapeId ? firstShapeId : "";

	// COM-free observation mirror for the harness dump ("nativeSelKind").
	// Records the kind as OBSERVED this dispatch (a child kind here means the
	// caller is ABOUT to Unselect it; the next dispatch will observe "" or
	// CHART_ROOT once suppression settles).
	g_lastNativeSelKindForTest = hasShapeSelection ? kind : "";

	const char* resolution = "none";
	int action = OVERLAY_NATIVE_SEL_NONE;

	// Never mutate the selection model mid-mutation/inline-edit — the caller's
	// own guards also cover this, but keep the handler self-safe.
	if (g_mutating || IsEditSessionActive()) {
		resolution = "ignored_busy";
	} else if (hasShapeSelection && !kind.empty() && kind != "CHART_ROOT") {
		// A chart CHILD is natively selected: mirror TASK/MILESTONE picks into
		// ownSel (other child kinds are suppressed without a mirror, matching
		// the historical Tick behavior), then tell the caller to Unselect it.
		g_suppressedKind = kind;
		g_suppressedId = id;
		std::string mirrorKind = IsTaskKind(kind) ? "TASK" : kind;
		if (mirrorKind == "TASK" || mirrorKind == "MILESTONE") {
			SetOwnSelection(mirrorKind, id);
		}
		g_suppressedKind.clear();
		g_suppressedId.clear();
		action = OVERLAY_NATIVE_SEL_SUPPRESS_CHILD;
		resolution = "suppress_child";
	} else if (hasShapeSelection && kind.empty()) {
		// A FOREIGN (non-chart) shape got natively selected: the user started
		// interacting with something else on the slide. Clear our internal
		// selection so the app bar + hotkeys revert to the neutral/component
		// context (B1 item 1 + UF-07 feed). CHART_ROOT and chart children are
		// handled above; an EMPTY selection (hasShapeSelection==false) is
		// deliberately NOT treated as a deselect here because it is also the
		// transient state right after we Unselect() a suppressed child.
		if (!g_ownSelKind.empty()) {
			ClearOwnSelection();
			g_appBarValid = false;   // force app-bar rebuild to component context
			g_appBarModelDirty = true;
			resolution = "clear_own_foreign";
		} else {
			resolution = "foreign";
		}
	} else if (hasShapeSelection && kind == "CHART_ROOT") {
		// CHART_ROOT selected: no model change here (Tick owns CHART_ROOT chrome).
		resolution = "chart_root";
	} else {
		// Empty selection: must not clear ownSel (transient after Unselect).
		resolution = "empty";
	}

	// R1b: nativeSel transition + snapshot while recording (SR-REC-06/11).
	// Tick calls this every 150ms — emit only when the observation changes.
	if (g_recActive) {
		std::string key;
		key.reserve(64 + kind.size() + id.size() + g_recNativeChildKind.size() + g_recNativeChildId.size());
		key += kind; key += '|'; key += id; key += '|';
		key += hasShapeSelection ? '1' : '0'; key += '|';
		key += g_recNativeHasChild ? '1' : '0'; key += '|';
		key += g_recNativeChildKind; key += '|';
		key += g_recNativeChildId; key += '|';
		key += resolution;
		if (key != g_recLastNativeKey) {
			g_recLastNativeKey = key;
			std::string body;
			body.reserve(128 + kind.size() + id.size()
				+ g_recNativeChildKind.size() + g_recNativeChildId.size());
			body += "\"kind\":\"";
			EntityJsonAppendEscaped(body, kind);
			body += "\",\"id\":\"";
			EntityJsonAppendEscaped(body, id);
			body += "\",\"hasChildShapeRange\":";
			body += g_recNativeHasChild ? "true" : "false";
			body += ",\"childKind\":\"";
			EntityJsonAppendEscaped(body, g_recNativeChildKind);
			body += "\",\"childId\":\"";
			EntityJsonAppendEscaped(body, g_recNativeChildId);
			body += "\",\"resolution\":\"";
			EntityJsonAppendEscaped(body, resolution);
			body += "\"";
			RecEvent("nativeSel", body);
			RecEmitSnapshot();
			RecCaptureFrames("sel");
		}
	}

	return action;
}

int Overlay_OnNativeSelectionChangedWithChild(
	const char* firstShapeKind, const char* firstShapeId, bool hasShapeSelection,
	bool hasChildShapeRange, const char* childKind, const char* childId) {
	Overlay_NoteNativeSelDetail(hasChildShapeRange, childKind, childId);
	const bool useChild = hasChildShapeRange && childKind && childKind[0] != '\0';
	return Overlay_OnNativeSelectionChanged(
		useChild ? childKind : firstShapeKind,
		useChild ? childId : firstShapeId,
		hasShapeSelection);
}

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
	if (g_recActive) SessionRecordStop();
	if (g_timer) { ::KillTimer(NULL, g_timer); g_timer = 0; }
	ThemeMenu_Dismiss(0);
	DestroyInlineEditor();
	CloseCardEditor();
	if (g_captureActive) {
		g_captureActive = false;
		if (g_hwnd && ::GetCapture() == g_hwnd) ::ReleaseCapture();
	}
	ResetDragGestureState();
	ClearDragCommitEcho();
	UnregisterAllHotkeys(L"OverlayStop"); // MUST run before DestroyWindow below (needs g_hwnd)
	if (g_hwnd) { ::DestroyWindow(g_hwnd); g_hwnd = NULL; }
	FreeBackBuffer();
	if (g_appBarHwnd) { ::DestroyWindow(g_appBarHwnd); g_appBarHwnd = NULL; }
	FreeAppBarBackBuffer();
	g_appBarShown = false;
	g_appBarValid = false;
	g_appBarHits.clear();
	g_appBar = AppBarModel{};
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
	ClearLinkMode();
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

HWND OverlayAppBarHwnd() { return g_appBarHwnd; }

void OverlayAppBarContentWidthForTest(int* content, int* window) {
	if (content) *content = g_appBarContentW;
	if (window) {
		if (g_appBarGeomValid)
			*window = g_appBarLastRect.right - g_appBarLastRect.left;
		else
			*window = g_appBarContentW + AppBarShadowInset() * 2;
	}
}

bool OverlayAppBarButtonRectForTest(int cmd, RECT* outScreenRect) {
	if (!outScreenRect || !g_appBarHwnd) return false;
	for (const auto& h : g_appBarHits) {
		if (h.cmd == cmd && h.enabled) {
			RECT r = h.rc;
			POINT tl{ r.left, r.top }, br{ r.right, r.bottom };
			::ClientToScreen(g_appBarHwnd, &tl); ::ClientToScreen(g_appBarHwnd, &br);
			outScreenRect->left = tl.x; outScreenRect->top = tl.y;
			outScreenRect->right = br.x; outScreenRect->bottom = br.y;
			return true;
		}
	}
	return false;
}

const char* Overlay_GetSelectedIdForTest() { return g_ownSelId.c_str(); }

void Overlay_InvalidateAppBarForTest() {
	g_appBarValid = false;
	g_appBarModelDirty = true;
	g_appBarLastMeasuredContentW = 0;
	g_appBarLastMeasuredContentH = 0;
	g_appBarGeomValid = false;
	g_appBarLayout = AppBarModel{};
	FreeAppBarBackBuffer();
}

void Overlay_SyncAppBarForTest() {
	if (!g_appBarHwnd || ::IsRectEmpty(&g_chartScreenRect)) return;
	RECT slideRect = g_chartScreenRect;
	ShowAppBar(g_chartScreenRect, slideRect);
}

bool Overlay_IsLinkModeForTest() { return g_linkMode; }

void Overlay_CancelLinkModeForTest() {
	ClearLinkMode();
	RequestOverlayRepaint();
}

void Overlay_SelectForTest(const char* kind, const char* id) {
	if (!kind || !*kind) { ClearOwnSelection(); Overlay_InvalidateAppBarForTest(); }
	else {
		SetOwnSelection(kind, id ? id : "");
		Overlay_InvalidateAppBarForTest();
	}
	Overlay_SyncAppBarForTest();
}

int Overlay_GetSelectedKindForTest() {
	if (g_ownSelId.empty()) return OVERLAY_SELKIND_NONE_FOR_TEST;
	if (g_ownSelKind == "TASK") return OVERLAY_SELKIND_TASK_FOR_TEST;
	if (g_ownSelKind == "MILESTONE") return OVERLAY_SELKIND_MILESTONE_FOR_TEST;
	if (g_ownSelKind == "ROW") return OVERLAY_SELKIND_ROW_FOR_TEST;
	if (g_ownSelKind == "MARKER") return OVERLAY_SELKIND_MARKER_FOR_TEST;
	if (g_ownSelKind == "TEXT") return OVERLAY_SELKIND_TEXT_FOR_TEST;
	return OVERLAY_SELKIND_NONE_FOR_TEST;
}

void Overlay_SetCursorPosOverrideForTest(bool enabled, POINT screenPt, bool altDown) {
	g_cursorOverrideEnabled = enabled;
	g_cursorOverrideScreenPt = screenPt;
	g_cursorOverrideAltDown = altDown;
}

// Forward decl (defined with other test seams below).
void Overlay_GetPaintCadenceForTest(double* outHz, long* outPaintCount,
	long* outWindowMs, double* outP50Ms, double* outP95Ms);

// ---- session recorder ForTest seams (R1b-core) -----------------------------
void Overlay_NoteNativeSelDetail(bool hasChild, const char* childKind, const char* childId) {
	g_recNativeHasChild = hasChild;
	g_recNativeChildKind = childKind ? childKind : "";
	g_recNativeChildId = childId ? childId : "";
}

void Overlay_StartSessionRecordForTest() {
	SessionRecordStart();
}

void Overlay_StopSessionRecordForTest() {
	SessionRecordStop();
}

bool Overlay_IsSessionRecordingForTest() {
	return g_recActive;
}

void Overlay_SetSessionRecording(bool active) {
	if (active == g_recActive) return;
	if (active) SessionRecordStart();
	else SessionRecordStop();
}

void Overlay_SetSessionRecordStateChangedCallback(
	OverlaySessionRecordStateChanged callback, void* context) {
	g_recStateChanged = callback;
	g_recStateChangedContext = context;
}

const char* Overlay_GetSessionDirForTest() {
	static std::string s;
	s = Narrow(g_recSessionDir);
	return s.c_str();
}

void Overlay_RecError(const char* where, long hr, const char* msg) {
	RecError(where, hr, msg);
}

const char* Overlay_DumpEntitiesForTest() {
	// COM-free seam (SR-ENT-08). Apply selection/hover flags from live globals
	// then re-serialize; geometry stays whatever BuildRowBands last cached.
	static std::string s;
	if (g_entityCache.empty()) {
		s = "{\"entities\":[]}";
		return s.c_str();
	}
	for (PpEntity& e : g_entityCache) {
		const bool ownIdMatch = !g_ownSelId.empty() && e.id == g_ownSelId;
		e.flags.selectedOwn = ownIdMatch && (
			e.kind == g_ownSelKind
			|| (g_ownSelKind == "TASK" && IsTaskKind(e.kind))
			|| (g_ownSelKind == "MILESTONE" && (e.kind == "MILESTONE" || e.kind == "MILESTONE_LABEL"))
			|| (g_ownSelKind == "ROW" && e.kind == "ROW_LABEL")
			|| (g_ownSelKind == "MARKER" && (e.kind == "TODAY_LINE" || e.kind == "DEADLINE"
				|| e.kind == "CUSTOM_MARKER" || e.kind == "TODAY_LABEL"
				|| e.kind == "DEADLINE_LABEL" || e.kind == "CUSTOM_MARKER_LABEL"))
			|| (g_ownSelKind == "TEXT" && e.kind == "TEXT")
			|| (g_ownSelKind == "DEP" && e.kind == "DEP"));
		// Prefer ChildShapeRange truth when present (SR-REC-06 / live bug #1).
		if (g_recNativeHasChild && !g_recNativeChildKind.empty()) {
			e.flags.selectedNative = (e.kind == g_recNativeChildKind)
				&& (g_recNativeChildId.empty() || e.id == g_recNativeChildId);
		} else {
			e.flags.selectedNative = !g_lastNativeSelKindForTest.empty()
				&& e.kind == g_lastNativeSelKindForTest
				&& !g_suppressedId.empty() && e.id == g_suppressedId;
		}
		e.flags.hover = (!g_hoverRowId.empty() && (e.id == g_hoverRowId || e.rowId == g_hoverRowId))
			// Task-bar hover: every primitive of the hovered bar unit (fill,
			// progress, label, % readout, rail chrome) shares the task id and
			// is part of the one hovered object (SR-TASK-UNIT-01).
			|| (!g_hoverTaskId.empty() && e.id == g_hoverTaskId && IsTaskKind(e.kind))
			|| (!g_lastHit.id.empty() && e.id == g_lastHit.id);
		// BuildRowBands maps the cached pure Scene primitive's clippedL/clippedR
		// flags into the entity. Preserve that truth here (SR-ENT-02).
		e.flags.visible = true;
	}
	s = EntityDumpToJson(g_entityCache);
	g_entityDumpJson = s;
	return s.c_str();
}

const char* Overlay_DumpChromeStateForTest() {
	static std::string s;
	s.clear();
	s += "{";
	s += "\"ownSelKind\":\"" + g_ownSelKind + "\",";
	s += "\"ownSelId\":\"" + g_ownSelId + "\",";
	s += "\"sessionRecording\":" + std::string(g_recActive ? "true" : "false") + ",";
	s += "\"recIndicatorPainted\":" + std::string(g_recIndicatorPainted ? "true" : "false") + ",";
	s += "\"sessionDir\":\"";
	EntityJsonAppendEscaped(s, Narrow(g_recSessionDir));
	s += "\",";
	s += "\"ownSelCount\":" + std::to_string(OwnSelCount()) + ",";
	s += "\"ownSelExtra\":[";
	for (size_t i = 0; i < g_ownSelExtra.size(); ++i) {
		if (i > 0) s += ",";
		s += "{\"kind\":\"" + g_ownSelExtra[i].kind + "\",\"id\":\"" + g_ownSelExtra[i].id + "\"}";
	}
	s += "],";
	// v2.6.1 selection integrity: the PowerPoint-native selection kind Tick/the
	// COM sink most recently observed ("" / "CHART_ROOT" / a child kind), and
	// whether the Delete/arrow hotkeys are currently registered. Consumed by the
	// component-shape-protection scenario (no_child_shape_selected + hotkey scope).
	// Task-bar hover ground truth (gated by trace_entity_dump's
	// entity_task_hover_scoped invariant): "" when no bar is under the cursor.
	s += "\"hoverTaskId\":\"" + g_hoverTaskId + "\",";
	s += "\"hoverRowId\":\"" + g_hoverRowId + "\",";
	s += "\"nativeSelKind\":\"" + g_lastNativeSelKindForTest + "\",";
	s += "\"hotkeysActive\":" + std::string(g_hotkeysActive ? "true" : "false") + ",";
	s += "\"linkMode\":" + std::string(g_linkMode ? "true" : "false") + ",";
	s += "\"linkDragActive\":" + std::string((g_gestureActive && g_dragKind == DragKind::LinkPort && g_dragActive) ? "true" : "false") + ",";
	s += "\"windowDragActive\":" + std::string((IsWindowEdgeDragActive() && g_dragActive) ? "true" : "false") + ",";
	s += "\"windowPillText\":\"" + (IsWindowEdgeDragActive() ? g_dragPillText : "") + "\",";
	s += "\"linkFromId\":\"" + g_linkFromId + "\",";
	int depCount = 0;
	{
		PpDocument cached;
		if (Gantt_TryPeekCachedDoc(&cached)) depCount = (int)cached.deps.size();
	}
	s += "\"depCount\":" + std::to_string(depCount) + ",";
	// Ground-truth affordance rects (SCREEN coords) so harness profiles click
	// exactly where the overlay hit-tests — no independent geometry recompute.
	{
		auto rectJson = [](const RECT& r) {
			return "{\"left\":" + std::to_string(r.left) + ",\"top\":" + std::to_string(r.top)
				+ ",\"right\":" + std::to_string(r.right) + ",\"bottom\":" + std::to_string(r.bottom) + "}";
		};
		bool portDone = false;
		if (g_ownSelKind == "TASK" || g_ownSelKind == "MILESTONE") {
			const HtItemKind want = (g_ownSelKind == "TASK") ? HtItemKind::Task : HtItemKind::Milestone;
			for (const auto& item : g_hitSnapshot.items) {
				if (item.kind != want || item.id != g_ownSelId) continue;
				RECT lp = LinkPortHitRectScreen(item.rect, false);
				RECT rp = LinkPortHitRectScreen(item.rect, true);
				s += "\"linkPortLeftRect\":" + rectJson(lp) + ",";
				s += "\"linkPortRightRect\":" + rectJson(rp) + ",";
				portDone = true;
				break;
			}
		}
		if (!portDone) s += "\"linkPortLeftRect\":{},\"linkPortRightRect\":{},";
		const RECT windowPortL = WindowPortHitRectScreen(false);
		const RECT windowPortR = WindowPortHitRectScreen(true);
		s += "\"windowHeaderBandRect\":" + (IsRectEmpty(&g_headerBandScreenRect) ? std::string("{}") : rectJson(g_headerBandScreenRect)) + ",";
		s += "\"windowPortLRect\":" + (IsRectEmpty(&windowPortL) ? std::string("{}") : rectJson(windowPortL)) + ",";
		s += "\"windowPortRRect\":" + (IsRectEmpty(&windowPortR) ? std::string("{}") : rectJson(windowPortR)) + ",";
		// Ground truth for the discoverability gate: a port is only real if the
		// overlay would actually route a press there (alpha-covered = inside the
		// chart rect, and the zone hit test agrees with the published rect).
		{
			auto portLive = [&](const RECT& r, HtZone want) {
				if (IsRectEmpty(&r)) return false;
				RECT clipped{};
				if (!::IntersectRect(&clipped, &r, &g_chartScreenRect) || !SameRect(clipped, r)) return false;
				const POINT c = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
				return WindowPortUnderPoint(c) == want;
			};
			s += std::string("\"windowPortLHitTestable\":") + (portLive(windowPortL, HtZone::WindowPortL) ? "true" : "false") + ",";
			s += std::string("\"windowPortRHitTestable\":") + (portLive(windowPortR, HtZone::WindowPortR) ? "true" : "false") + ",";
			s += std::string("\"windowPortHover\":\"") + RecHtZoneName(g_windowPortHoverZone) + "\",";
		}
		for (int i = 0; i < 2; ++i) {
			const char* key = (i == 0) ? "rowAdderAboveRect" : "rowAdderBelowRect";
			if (g_rowBoundaryInsertValid[i]) {
				RECT sr = {
					g_rowBoundaryInsertRects[i].left + g_windowOriginX,
					g_rowBoundaryInsertRects[i].top + g_windowOriginY,
					g_rowBoundaryInsertRects[i].right + g_windowOriginX,
					g_rowBoundaryInsertRects[i].bottom + g_windowOriginY
				};
				s += std::string("\"") + key + "\":" + rectJson(sr) + ",";
			} else {
				s += std::string("\"") + key + "\":{},";
			}
		}
	}
	s += "\"rowCount\":" + std::to_string(g_rowBands.size()) + ",";
	int taskCount = 0;
	int milestoneCount = 0;
	if (g_hitSnapshot.items.empty()) {
		// Post-op the cached doc IS authoritative; the snapshot refills next tick.
		PpDocument cached;
		if (Gantt_TryPeekCachedDoc(&cached)) {
			taskCount = (int)cached.tasks.size();
			milestoneCount = (int)cached.milestones.size();
		}
	} else {
		for (const auto& item : g_hitSnapshot.items) {
			if (item.kind == HtItemKind::Task) ++taskCount;
			else if (item.kind == HtItemKind::Milestone) ++milestoneCount;
		}
	}
	s += "\"taskCount\":" + std::to_string(taskCount) + ",";
	s += "\"milestoneCount\":" + std::to_string(milestoneCount) + ",";
	int rowLabelCount = 0;
	for (const auto& er : g_editRegions) if (er.kind == "ROW_LABEL") ++rowLabelCount;
	s += "\"rowLabelCount\":" + std::to_string(rowLabelCount) + ",";
	s += "\"rowBands\":[";
	for (size_t i = 0; i < g_rowBands.size(); ++i) {
		const auto& b = g_rowBands[i];
		if (i > 0) s += ",";
		s += "{";
		s += "\"rowId\":\"" + b.rowId + "\",";
		s += "\"left\":" + std::to_string(b.screenRect.left) + ",";
		s += "\"top\":" + std::to_string(b.screenRect.top) + ",";
		s += "\"right\":" + std::to_string(b.screenRect.right) + ",";
		s += "\"bottom\":" + std::to_string(b.screenRect.bottom);
		s += "}";
	}
	s += "],";
	s += "\"hasDrag\":" + std::string((g_dragActive || g_gestureActive) ? "true" : "false") + ",";
	s += "\"hasCommitEcho\":" + std::string(g_dragCommitEcho.active ? "true" : "false") + ",";
	s += "\"dragKind\":" + std::to_string(static_cast<int>(g_dragKind)) + ",";
	// SR-SMO-09..12 continuous paint cadence sample (0 when not sampling).
	{
		double hz = 0.0, p50 = 0.0, p95 = 0.0;
		long pc = 0, wms = 0;
		Overlay_GetPaintCadenceForTest(&hz, &pc, &wms, &p50, &p95);
		char cadBuf[192];
		::snprintf(cadBuf, sizeof(cadBuf),
			"\"paintCadence\":{\"active\":%s,\"paintCount\":%ld,\"windowMs\":%ld,\"hz\":%.2f,\"p50Ms\":%.2f,\"p95Ms\":%.2f},",
			g_paintSampleActive ? "true" : "false", pc, wms, hz, p50, p95);
		s += cadBuf;
	}
	s += "\"dragPillText\":\"" + g_dragPillText + "\",";
	s += "\"dragPreviewRect\":{\"left\":" + std::to_string(g_dragPreviewRect.left) + ",\"top\":" + std::to_string(g_dragPreviewRect.top)
		+ ",\"right\":" + std::to_string(g_dragPreviewRect.right) + ",\"bottom\":" + std::to_string(g_dragPreviewRect.bottom) + "},";
	s += "\"dragCandidatePercent\":" + std::to_string(g_dragCandidatePercent) + ",";
	s += "\"dragTargetRowId\":\"" + g_lastCommittedDragTargetRowId + "\",";
	int ownSelTaskPercent = -1;
	std::string ownSelTaskStart, ownSelTaskEnd;
	if (g_ownSelKind == "TASK" && !g_ownSelId.empty()) {
		PpDocument cached;
		if (Gantt_TryPeekCachedDoc(&cached)) {
			for (const auto& t : cached.tasks) {
				if (t.id == g_ownSelId) {
					ownSelTaskPercent = t.percent;
					ownSelTaskStart = t.start;
					ownSelTaskEnd = t.end;
					break;
				}
			}
		}
	}
	s += "\"ownSelTaskPercent\":" + std::to_string(ownSelTaskPercent) + ",";
	s += "\"ownSelTaskStart\":\"" + ownSelTaskStart + "\",";
	s += "\"ownSelTaskEnd\":\"" + ownSelTaskEnd + "\",";
	s += "\"cardVisible\":" + std::string((g_cardHwnd && ::IsWindowVisible(g_cardHwnd)) ? "true" : "false") + ",";
	s += "\"contextMenuVisible\":" + std::string(ThemeMenu_IsVisible() ? "true" : "false") + ",";
	s += "\"appBarVisible\":" + std::string(g_appBarShown ? "true" : "false") + ",";
	s += "\"appBarValid\":" + std::string(g_appBarValid ? "true" : "false") + ",";
	if (g_appBarGeomValid) {
		s += "\"appBarRect\":{\"left\":" + std::to_string(g_appBarLastRect.left) + ",\"top\":" + std::to_string(g_appBarLastRect.top)
			+ ",\"right\":" + std::to_string(g_appBarLastRect.right) + ",\"bottom\":" + std::to_string(g_appBarLastRect.bottom) + "},";
	} else {
		s += "\"appBarRect\":{},";
	}
	PpDocument settingsDoc;
	const bool haveSettingsDoc = Gantt_TryPeekCachedDoc(&settingsDoc);
	const std::string gridDensity = haveSettingsDoc && !settingsDoc.gridDensity.empty()
		? settingsDoc.gridDensity : "auto";
	const std::string axisNumbering = haveSettingsDoc && settingsDoc.axisNumbering == "cw"
		? "cw" : "day";
	const bool railLabels = haveSettingsDoc && settingsDoc.railLabels;
	std::string docDatesSignature;
	if (haveSettingsDoc) {
		for (const auto& task : settingsDoc.tasks)
			docDatesSignature += task.id + ":" + task.start + ":" + task.end + ";";
		for (const auto& milestone : settingsDoc.milestones)
			docDatesSignature += milestone.id + ":" + milestone.date + ";";
	}
	PpProj dumpProj;
	const bool haveDumpProj = ParseProj(g_chartProj, &dumpProj);
	std::string firstAxisLabel;
	if (axisNumbering == "cw" && (g_lastScale == "week" || g_lastScale == "day")) {
		PpProj proj;
		if (ParseProj(g_chartProj, &proj)) {
			long labelDay = proj.minDay;
			if (g_lastScale == "week") {
				long sinceMonday = (labelDay + 3) % 7;
				if (sinceMonday < 0) sinceMonday += 7;
				labelDay -= sinceMonday;
			}
			firstAxisLabel = "CW " + std::to_string(IsoCalendarWeekNumber(labelDay));
		} else {
			firstAxisLabel = "CW";
		}
	}
	s += "\"scale\":\"" + g_lastScale + "\",";
	s += "\"gridDensity\":\"" + gridDensity + "\",";
	s += "\"axisNumbering\":\"" + axisNumbering + "\",";
	s += "\"railLabels\":" + std::string(railLabels ? "true" : "false") + ",";
	s += "\"windowStart\":\"" + (haveSettingsDoc ? settingsDoc.windowStart : "") + "\",";
	s += "\"windowEnd\":\"" + (haveSettingsDoc ? settingsDoc.windowEnd : "") + "\",";
	s += "\"docDatesSignature\":\"" + docDatesSignature + "\",";
	if (haveDumpProj) {
		s += "\"ppProj\":{\"minDay\":" + std::to_string(dumpProj.minDay)
			+ ",\"pad\":" + std::to_string(dumpProj.pad)
			+ ",\"ptPerDay\":" + std::to_string(dumpProj.ptPerDay)
			+ ",\"originX\":" + std::to_string(dumpProj.originX)
			+ ",\"chartLeftPt\":" + std::to_string(g_chartLeftPt)
			+ ",\"chartWidthPt\":" + std::to_string(g_chartWidthPt) + "},";
	} else {
		s += "\"ppProj\":{},";
	}
	s += "\"firstAxisLabel\":\"" + firstAxisLabel + "\",";
	s += "\"chartRect\":{\"left\":" + std::to_string(g_chartScreenRect.left) + ",\"top\":" + std::to_string(g_chartScreenRect.top) + ",\"right\":" + std::to_string(g_chartScreenRect.right) + ",\"bottom\":" + std::to_string(g_chartScreenRect.bottom) + "},";
	s += "\"selScreenRect\":{\"left\":" + std::to_string(g_selScreenRect.left) + ",\"top\":" + std::to_string(g_selScreenRect.top) + ",\"right\":" + std::to_string(g_selScreenRect.right) + ",\"bottom\":" + std::to_string(g_selScreenRect.bottom) + "},";
	s += "\"frameRect\":{\"left\":" + std::to_string(g_frameRect.left) + ",\"top\":" + std::to_string(g_frameRect.top) + ",\"right\":" + std::to_string(g_frameRect.right) + ",\"bottom\":" + std::to_string(g_frameRect.bottom) + "},";
	s += "\"appBarGroups\":[";
	for (size_t i = 0; i < g_appBar.groups.size(); ++i) {
		if (i > 0) s += ",";
		s += "\"" + g_appBar.groups[i].label + "\"";
	}
	s += "],";
	s += "\"appBarLayoutGroups\":[";
	for (size_t i = 0; i < g_appBarLayout.groups.size(); ++i) {
		if (i > 0) s += ",";
		s += "\"" + g_appBarLayout.groups[i].label + "\"";
	}
	s += "],";
	s += "\"appBarWindowCount\":" + std::to_string(CountAppBarWindows()) + ",";
	bool hasScale = false;
	for (const auto& gr : g_appBar.groups) {
		if (gr.label == "SCALE") { hasScale = true; break; }
	}
	s += "\"hasScaleGroup\":" + std::string(hasScale ? "true" : "false");
	s += "}";
	return s.c_str();
}

void Overlay_SetHostActiveOverrideForTest(int mode) {
	g_hostActiveOverrideMode = mode;
}

void Overlay_SetSlideFocusOverrideForTest(int mode) {
	g_slideFocusOverrideMode = mode;
}

void Overlay_PerformAppBarCommandForTest(int cmd) {
	if (cmd <= 0) return;
	// Drive through the same path as real appbar click (includes ROW special case for rename/rowops,
	// scale handling, rebuilds, SetOwnSelection etc). Guarded by the handler itself.
	HandleAppBarCommand(cmd);
	// After a mutation the appbar is usually dirtied inside handler paths; ensure rebuild on next tick.
	g_appBarValid = false;
	RequestOverlayRepaint();
}

void Overlay_PerformHoverQuickAddForTest(const char* rowId) {
	// Set the hover-row state HandleHoverQuickAddTask reads, exactly as a real
	// hover pass over the row gutter would, then drive the SAME code path the
	// chip's WM_LBUTTONUP handler uses (see HoverInsertFromClientPoint branch).
	g_hoverRowId = rowId ? rowId : "";
	try {
		HandleHoverQuickAddTask();
	} catch (...) {
		OvLog(L"hover quick-add task failed");
	}
}

void Overlay_CommitWindowGestureForTest(const char* startISO, const char* endISO) {
	// Baseline = the CURRENT document window, so re-committing the same dates
	// exercises the m9 zero-delta no-op exactly like a snap-back release.
	std::string baselineStart, baselineEnd;
	try {
		PpDocument doc;
		if (g_app && ReadGanttDocFromSlide(g_app, &doc)) {
			baselineStart = doc.windowStart;
			baselineEnd = doc.windowEnd;
		}
	} catch (...) {}
	CommitWindowGesture(startISO ? startISO : "", endISO ? endISO : "",
		baselineStart, baselineEnd);
}

void Overlay_GetRenderCountersForTest(long* overlayPaints, long* appBarPaints,
                                      long* overlaySwp, long* appBarSwp) {
	if (overlayPaints) *overlayPaints = g_overlayPaintCount;
	if (appBarPaints) *appBarPaints = g_appBarPaintCount;
	if (overlaySwp) *overlaySwp = g_overlaySwpCount;
	if (appBarSwp) *appBarSwp = g_appBarSwpCount;
}

void Overlay_BeginPaintCadenceSampleForTest() {
	g_paintSampleActive = true;
	g_paintTsCount = 0;
	g_paintTsWrite = 0;
	g_paintSampleStartMs = ::GetTickCount64();
}

void Overlay_EndPaintCadenceSampleForTest() {
	g_paintSampleActive = false;
}

void Overlay_GetPaintCadenceForTest(double* outHz, long* outPaintCount,
	long* outWindowMs, double* outP50Ms, double* outP95Ms) {
	const long n = g_paintTsCount;
	if (outPaintCount) *outPaintCount = n;

	// Reconstruct ordered samples from ring.
	ULONGLONG ordered[kPaintTsCap];
	const int start = (g_paintTsCount < kPaintTsCap) ? 0 : g_paintTsWrite;
	for (int i = 0; i < n; ++i)
		ordered[i] = g_paintTsMs[(start + i) % kPaintTsCap];

	// Window = first→last paint (not wall-clock from Begin — excludes pre-paint dead time).
	long windowMs = 0;
	if (n >= 2)
		windowMs = (long)(ordered[n - 1] - ordered[0]);
	else if (n == 1 && g_paintSampleStartMs)
		windowMs = (long)(::GetTickCount64() - g_paintSampleStartMs);
	if (outWindowMs) *outWindowMs = windowMs;

	// Hz from (paintCount-1) / span so intervals define cadence (SR-SMO-09).
	double hz = 0.0;
	if (windowMs > 0 && n >= 2)
		hz = (1000.0 * (double)(n - 1)) / (double)windowMs;
	else if (windowMs > 0 && n == 1)
		hz = 1000.0 / (double)windowMs;
	if (outHz) *outHz = hz;

	double p50 = 0.0, p95 = 0.0;
	if (n >= 2) {
		double intervals[kPaintTsCap];
		int ni = 0;
		for (int i = 1; i < n; ++i) {
			const double d = (double)(ordered[i] - ordered[i - 1]);
			if (d >= 0.0) intervals[ni++] = d;
		}
		if (ni > 0) {
			for (int i = 1; i < ni; ++i) {
				double key = intervals[i];
				int j = i - 1;
				while (j >= 0 && intervals[j] > key) {
					intervals[j + 1] = intervals[j];
					--j;
				}
				intervals[j + 1] = key;
			}
			p50 = intervals[(ni - 1) * 50 / 100];
			p95 = intervals[(ni - 1) * 95 / 100];
		}
	}
	if (outP50Ms) *outP50Ms = p50;
	if (outP95Ms) *outP95Ms = p95;
}

void Overlay_DumpWindowStateForTest(char* buf, int bufLen) {
	if (!buf || bufLen <= 0) return;
	auto windowFields = [](HWND hwnd, int& vis, long& l, long& t, long& r, long& b) {
		vis = (hwnd && ::IsWindow(hwnd) && ::IsWindowVisible(hwnd)) ? 1 : 0;
		l = t = r = b = 0;
		if (hwnd && ::IsWindow(hwnd)) {
			RECT wr{};
			if (::GetWindowRect(hwnd, &wr)) {
				l = wr.left; t = wr.top; r = wr.right; b = wr.bottom;
			}
		}
	};
	int ovVis = 0, abVis = 0, edVis = 0, cdVis = 0, mnVis = 0;
	long ovL = 0, ovT = 0, ovR = 0, ovB = 0;
	long abL = 0, abT = 0, abR = 0, abB = 0;
	long edL = 0, edT = 0, edR = 0, edB = 0;
	long cdL = 0, cdT = 0, cdR = 0, cdB = 0;
	long mnL = 0, mnT = 0, mnR = 0, mnB = 0;
	windowFields(g_hwnd, ovVis, ovL, ovT, ovR, ovB);
	windowFields(g_appBarHwnd, abVis, abL, abT, abR, abB);
	windowFields(g_editorHwnd, edVis, edL, edT, edR, edB);
	windowFields(g_cardHwnd, cdVis, cdL, cdT, cdR, cdB);
	windowFields(ThemeMenu_Hwnd(), mnVis, mnL, mnT, mnR, mnB);
	::snprintf(buf, (size_t)bufLen,
		"{\"hostActive\":%d,\"viewOk\":%d,\"shown\":%d,"
		"\"windows\":["
		"{\"cls\":\"PowerPlannerOverlay\",\"vis\":%d,\"l\":%ld,\"t\":%ld,\"r\":%ld,\"b\":%ld},"
		"{\"cls\":\"PowerPlannerAppBar\",\"vis\":%d,\"l\":%ld,\"t\":%ld,\"r\":%ld,\"b\":%ld},"
		"{\"cls\":\"PowerPlannerInlineEditor\",\"vis\":%d,\"l\":%ld,\"t\":%ld,\"r\":%ld,\"b\":%ld},"
		"{\"cls\":\"PowerPlannerCardEditor\",\"vis\":%d,\"l\":%ld,\"t\":%ld,\"r\":%ld,\"b\":%ld},"
		"{\"cls\":\"PowerPlannerThemeMenu\",\"vis\":%d,\"l\":%ld,\"t\":%ld,\"r\":%ld,\"b\":%ld}],"
		"\"counters\":{\"overlayPaints\":%ld,\"appBarPaints\":%ld,\"overlaySwp\":%ld,\"appBarSwp\":%ld}}",
		g_lastHostActive ? 1 : 0, g_lastViewOk ? 1 : 0, g_shown ? 1 : 0,
		ovVis, ovL, ovT, ovR, ovB,
		abVis, abL, abT, abR, abB,
		edVis, edL, edT, edR, edB,
		cdVis, cdL, cdT, cdR, cdB,
		mnVis, mnL, mnT, mnR, mnB,
		g_overlayPaintCount, g_appBarPaintCount, g_overlaySwpCount, g_appBarSwpCount);
	buf[bufLen - 1] = '\0';
}

void Overlay_ShowContextMenuAtClientForTest(int clientX, int clientY) {
	POINT clientPt = { clientX, clientY };
	HtHit hit = HitTestClientPoint(clientPt);
	if (hit.zone == HtZone::Outside) return;
	bool hasRowId = !hit.rowId.empty();
	if (hit.zone == HtZone::Label && hit.kind == HtItemKind::RowLabel) {
		hasRowId = !hit.id.empty();
	}
	PpDocument doc;
	std::string hitId;
	try {
		if (g_app) {
			std::string json = ReadGanttFromSlide(g_app);
			if (!json.empty()) doc = DocumentFromJson(json);
		}
	}
	catch (...) {}
	if (hit.zone == HtZone::TaskBody || hit.zone == HtZone::TaskEdgeL || hit.zone == HtZone::TaskEdgeR
		|| hit.zone == HtZone::TaskProgressEdge
		|| hit.zone == HtZone::Milestone || hit.zone == HtZone::Marker || hit.zone == HtZone::Text) {
		hitId = hit.id;
	} else if (hit.zone == HtZone::Label && hit.kind == HtItemKind::RowLabel) {
		hitId = hit.id;
	} else if (hasRowId) {
		hitId = hit.rowId;
	}
	std::vector<HtMenuItem> items = BuildMenuForZone(hit.zone, hit.kind, hasRowId, doc, hitId);
	if (items.empty()) return;
	POINT screenPt = { clientPt.x + g_windowOriginX, clientPt.y + g_windowOriginY };
	ThemeMenu_Show(items, screenPt, g_hwnd, false);
}

void Overlay_ShowSettingsMenuForTest() {
	if (!g_app || g_mutating || !g_appBarHwnd) return;
	PpDocument doc;
	try {
		if (!ReadGanttDocFromSlide(g_app, &doc)) return;
		RECT button = {};
		POINT screenPt = { 0, 0 };
		if (OverlayAppBarButtonRectForTest(HtCmd_Settings, &button)) {
			screenPt.x = button.left;
			screenPt.y = button.bottom + Scale(2);
		} else {
			::GetCursorPos(&screenPt);
		}
		ThemeMenu_Show(BuildSettingsMenuItems(doc), screenPt, g_appBarHwnd, false);
	} catch (...) {
		OvLog(L"unable to show Settings menu for test");
	}
}
