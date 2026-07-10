// Pure, COM-free semantic hit-testing for the on-slide Gantt overlay.
//
// The overlay's Tick() snapshots the chart's child shapes (screen rects only)
// into an HtSnapshot; mouse handlers then classify any screen point into a
// semantic zone without touching COM. This module has NO Windows/COM
// dependencies so it can be unit-tested in the PowerPoint-free ops harness.
#pragma once

#include "GanttModel.h"

#include <string>
#include <vector>

// Screen-pixel rectangle (half-open: contains x in [left,right), y in [top,bottom)).
struct HtRect {
	long left = 0;
	long top = 0;
	long right = 0;
	long bottom = 0;
};

// Kinds of chart child shapes that participate in hit-testing.
enum class HtItemKind {
	Task,
	Milestone,
	RowLabel,
	Title,
	Marker,  // TODAY_LINE / DEADLINE / CUSTOM_MARKER vertical date lines
	Text     // PpText annotation (anchored or free-floating), PP_KIND=TEXT
};

struct HtItem {
	HtItemKind kind = HtItemKind::Task;
	std::string id;   // PP_ID (empty for TITLE)
	HtRect rect;      // screen pixels
};

struct HtRowBand {
	std::string rowId;
	long yTop = 0;
	long yBottom = 0; // half-open: band contains y in [yTop, yBottom)
};

// Immutable input to the hit test, rebuilt by the overlay tick when the chart
// rect or child count changes.
struct HtSnapshot {
	HtRect chartRect;               // CHART_ROOT screen rect
	std::vector<HtItem> items;      // TASK / MILESTONE / ROW_LABEL / TITLE / Marker rects
	std::vector<HtRowBand> rowBands;
	// Half-width (in device pixels) of a task's resize edge band for THIS
	// snapshot. Defaults to kHtEdgePx (4px @ 96 DPI) so existing callers that
	// never touch this field keep the pre-DPI-awareness behavior unchanged.
	// The overlay sets this to HtScalePx(kHtEdgePx, dpi) once per tick. Also
	// used as the half-width of a Marker item's synthesized hit band (see
	// HtItemKind::Marker) — markers are thin rendered lines, so their `rect`
	// here is NOT the rendered shape's rect (near-zero width) but a band of
	// [x-edgeBandPx, x+edgeBandPx] spanning the chart's full vertical band,
	// synthesized by the overlay from PP_PROJ (see Overlay.cpp's BuildRowBands).
	long edgeBandPx = 4;
	// Rail column x-extent in screen pixels (half-open [railLeftPx,railRightPx)),
	// from PP_ROWY (DPI-scaled). When both are zero the overlay falls back to
	// label-derived gutter geometry (legacy charts without PP_ROWY).
	long railLeftPx = 0;
	long railRightPx = 0;
};

// Semantic zones, most specific wins. Edge zones take priority over bodies so
// thin tasks stay resizable; see GanttHitTestPoint for the full ordering.
enum class HtZone {
	Outside,    // not inside chartRect
	TaskBody,   // inside a task bar, away from its edges (id = task id)
	TaskEdgeL,  // within +-kHtEdgePx of a task's left edge (id = task id)
	TaskEdgeR,  // within +-kHtEdgePx of a task's right edge (id = task id)
	Milestone,  // inside a milestone marker rect (id = milestone id)
	Label,      // inside a ROW_LABEL or TITLE text rect (kind + id)
	Marker,     // within +-edgeBandPx of a vertical marker line (id = marker id);
	            // below TaskBody/TaskEdge/Milestone, above RowBand/EmptyCell
	Text,       // inside a PpText annotation's rendered rect (id = text id);
	            // below TaskBody/TaskEdge/Milestone/Marker, above RowBand/
	            // EmptyCell (a text sitting over open timeline space still
	            // wins so it can be selected/dragged/deleted)
	RowBand,    // inside a row band but left of / around the label column
	            // (rowId set), or chart background outside any band (rowId empty)
	EmptyCell   // inside a row band's timeline area with nothing under the
	            // cursor (rowId set) — a spot where a task could be created
};

struct HtHit {
	HtZone zone = HtZone::Outside;
	HtItemKind kind = HtItemKind::Task; // meaningful when zone == Label
	std::string id;                     // item id; row id for RowBand/EmptyCell
	std::string rowId;                  // row id when zone is RowBand/EmptyCell
};

// Half-width of a task's resize edge band: the band spans the bar edge
// +-kHtEdgePx (i.e. 4px outside and 4px inside the bar). This is the 96-DPI
// (100%) baseline; HtSnapshot::edgeBandPx carries the DPI-scaled value
// actually used by GanttHitTestPoint (defaults to this constant).
constexpr long kHtEdgePx = 4;

// Reference DPI for "100% scaling" on Windows.
constexpr int kHtBaseDpi = 96;

// Scale a 96-DPI ("100%") pixel constant to the given DPI, rounding to the
// nearest integer pixel (same rounding rule as Windows' MulDiv, which this
// deliberately mirrors without depending on <windows.h> so this module stays
// COM/Windows-header-free and usable by the PowerPoint-free ops harness).
// dpi <= 0 is treated as kHtBaseDpi (i.e. a no-op scale).
int HtScalePx(int basePx, int dpi);

// Classify a screen point against the snapshot. Pure function. Task resize
// edges use snap.edgeBandPx (defaults to kHtEdgePx) as the half-width of the
// edge band, so DPI-scaled snapshots get a proportionally wider hit zone.
HtHit GanttHitTestPoint(const HtSnapshot& snap, long x, long y);

// ---- pure right-click context menu model -----------------------------------
// A menu built from a zone alone (no COM/doc access) so it can be assembled
// AND unit-tested without PowerPoint. Overlay.cpp turns HtMenuItem lists into
// a real Win32 popup (CreatePopupMenu/AppendMenuW); ops-test.cpp asserts the
// zone->items mapping and the (zone,cmdId)->op mapping directly.

// Stable command ids: also used as Win32 menu command ids (WM_COMMAND-style
// values returned by TrackPopupMenuEx with TPM_RETURNCMD), so they must all be
// nonzero (0 means "menu dismissed with no selection").
enum HtMenuCmd {
	HtCmd_None = 0,
	HtCmd_AddTaskSameRow,
	HtCmd_Delete,
	HtCmd_NudgeMinus1,
	HtCmd_NudgePlus1,
	HtCmd_PercentMinus10,
	HtCmd_PercentPlus10,
	HtCmd_ScaleDay,
	HtCmd_ScaleWeek,
	HtCmd_ScaleMonth,
	HtCmd_ScaleQuarter,
	HtCmd_ScaleYear,
	HtCmd_AddTaskThisRow,
	HtCmd_AddRowBelow,
	HtCmd_DeleteRow,
	HtCmd_EmptyCellAddTaskHere,
	HtCmd_EmptyCellAddMilestoneHere,
	HtCmd_EmptyCellAddNoteHere,
	HtCmd_AddRow,
	// --- S2 app-bar command ids (one shared id space; menus + app bar) ---
	HtCmd_InsertTask,
	HtCmd_InsertMilestone,
	HtCmd_InsertMarker,
	HtCmd_InsertNote,
	HtCmd_Edit,
	HtCmd_Swatch1,
	HtCmd_Swatch2,
	HtCmd_Swatch3,
	HtCmd_Swatch4,
	HtCmd_Swatch5,
	HtCmd_Swatch6,
	HtCmd_Swatch7,
	HtCmd_Swatch8,
	HtCmd_CycleLabelPlacement,
	HtCmd_Link,
	HtCmd_Unlink,
	HtCmd_AddNote,
	HtCmd_Rename,
	HtCmd_AddRowAbove,
	HtCmd_MoveRowUp,
	HtCmd_MoveRowDown,
	HtCmd_IndentRow,
	HtCmd_OutdentRow,
	HtCmd_ToggleRailLabels,
	HtCmd_CycleGrid,
	HtCmd_GridAuto,
	HtCmd_GridWeek,
	HtCmd_GridMonth,
	HtCmd_GridNone,
	HtCmd_ReanchorNote,
};

// One flat entry in a (possibly submenu'd) popup menu. Submenu items share the
// same flat vector; `submenu` names the submenu label they belong under
// (empty = top-level item). separatorBefore requests a separator drawn ABOVE
// this item (skipped if it would be the very first item/entry of its menu).
struct HtMenuItem {
	int cmdId = HtCmd_None;
	std::string label;
	bool separatorBefore = false;
	std::string submenu; // empty = top-level item; non-empty = submenu label
	bool enabled = true;
};

// Build the ordered list of menu items for a right-click at this zone. Items
// are derived from the same shared command registry as the app bar (see
// GanttCommandRegistry.h / BuildAppBar). `doc` and `hitId` supply enabled
// states and context-specific labels (e.g. Label: bar/rail); pass an empty
// doc / hitId when only structure is needed.
//
// `kind` disambiguates HtZone::Label (ROW_LABEL vs TITLE); ignored elsewhere.
// `hasRowId` disambiguates HtZone::RowBand (a row's gutter, rowId set) from
// the chart-background case (rowId empty).
std::vector<HtMenuItem> BuildMenuForZone(HtZone zone, HtItemKind kind, bool hasRowId,
	const PpDocument& doc, const std::string& hitId = "");

// Description of the operation a chosen (zone, cmdId) pair maps to. The
// overlay's WM_COMMAND-equivalent handler switches on opKind and reads
// whichever of rowId/taskId/nudgeDays/percentDelta/scale it needs; needsRowId/
// needsTaskId tell a caller (or ops-test) which identifier the caller must
// still supply (BuildMenuForZone/MapMenuCommand know only the zone, not which
// concrete row/task/milestone id was under the cursor — the overlay fills
// that in from the HtHit that produced the menu).
enum class HtOpKind {
	None,           // invalid (zone,cmdId) combination
	AddTask,        // needsRowId
	Delete,         // needsTaskId (task or milestone id)
	DeleteRow,      // needsRowId
	Nudge,          // needsTaskId; nudgeDays set
	Percent,        // needsTaskId; percentDelta set
	SetScale,       // scale set
	AddRow,         // needsRowId (afterRowId; empty = append)
	AddRowAbove,    // needsRowId (ref row)
	MoveRowUp,      // needsRowId
	MoveRowDown,    // needsRowId
	IndentRow,      // needsRowId
	OutdentRow,     // needsRowId
	RenameRow,      // needsRowId
	AddTaskAtPoint, // needsRowId; anchor day comes from the click point
	SetTaskColor,   // needsTaskId; color set (swatch hex)
	CycleLabelPlacement, // needsTaskId; cycles bar->rail->both->bar
	AddNote,        // needsTaskId; anchored AddText at default offset
	Edit,           // needsTaskId; open card editor for task/milestone
	EnterLinkMode,  // needsTaskId; overlay enters link-pick mode (no doc mutation)
	Unlink,         // needsTaskId; RemoveDependenciesTouching
	InsertFreeNote, // free AddText at visible chart center (background context)
	AddMilestoneAtPoint, // needsRowId; anchor day from click point (empty cell)
	AddNoteAtPoint,      // needsRowId; anchor day from click point (empty cell)
	InsertTaskBackground,       // add task at visible center / first row
	InsertMilestoneBackground,  // add milestone at visible center / first row
	InsertMarkerBackground,     // add custom marker at visible center
	SetGridDensity,      // gridDensity set (or "__cycle__" sentinel for CycleGrid)
	ToggleRailLabels,    // flip doc.railLabels
	ReanchorNote,        // needsTaskId (text id)
};

struct HtMenuOp {
	HtOpKind opKind = HtOpKind::None;
	bool needsRowId = false;
	bool needsTaskId = false;
	long nudgeDays = 0;
	int percentDelta = 0;
	const char* scale = ""; // "day" | "week" | ... when opKind == SetScale
	const char* color = ""; // "#RRGGBB" when opKind == SetTaskColor
	const char* gridDensity = ""; // when opKind == SetGridDensity
};

// Map a chosen menu command back to the operation it represents, validating
// that cmdId is actually one of the items BuildMenuForZone(zone,kind,hasRowId)
// would have offered. Returns HtOpKind::None (all other fields default) for
// any combination BuildMenuForZone would not produce — e.g. asking for
// HtCmd_PercentMinus10 on a Milestone zone (milestones have no percent), or
// HtCmd_AddTaskThisRow on RowBand background (hasRowId=false).
HtMenuOp MapMenuCommand(HtZone zone, int cmdId, HtItemKind kind, bool hasRowId,
	const PpDocument& doc = PpDocument{}, const std::string& hitId = "");

// ONE shared (cmdId)->op registry used by app-bar dispatch and context menus.
HtMenuOp MapRegistryCommand(int cmdId);

// Map an app-bar command issued while a ROW is selected (R8 row group + Rename).
// Pure registry for ops-test; Overlay.cpp's HandleAppBarCommand dispatches on
// the returned opKind.
HtMenuOp MapRowAppBarCommand(int cmdId);

// Map app-bar commands issued while a TASK is selected (swatches, label cycle,
// note, edit, delete, nudge). Pure registry for ops-test; Overlay.cpp dispatches
// on the returned opKind.
HtMenuOp MapTaskAppBarCommand(int cmdId);

// Map app-bar commands issued while a MILESTONE is selected (nudge, note, edit,
// delete, rename). Pure registry for ops-test.
HtMenuOp MapMilestoneAppBarCommand(int cmdId);

// Map app-bar commands issued while a MARKER is selected (rename, nudge,
// delete). Pure registry for ops-test.
HtMenuOp MapMarkerAppBarCommand(int cmdId);

// Map app-bar commands issued while a NOTE (text) is selected.
HtMenuOp MapNoteAppBarCommand(int cmdId);

// Map app-bar commands issued with no selection (INSERT group on background).
HtMenuOp MapBackgroundAppBarCommand(int cmdId);

// ---- pure zone -> cursor mapping --------------------------------------------
// WM_SETCURSOR needs to pick a cursor shape from the hit zone under the
// pointer. This enum is the pure (COM/Windows-free) output of that mapping —
// Overlay.cpp's WM_SETCURSOR handler turns each value into a real HCURSOR via
// LoadCursor(NULL, IDC_...); no HCURSOR appears in this layer so it stays
// testable from the PowerPoint-free ops harness.
enum class HtCursor {
	Arrow,     // default: RowBand, Label, outside any special widget
	SizeAll,   // TaskBody, Milestone, Text: whole-item move
	SizeWE,    // TaskEdgeL/TaskEdgeR/Marker: horizontal resize (ew-resize)
	Cross,     // EmptyCell: click-drag creates a new task here
	Hand,      // toolbar / hover-insert-row '+' / move-chart grip chrome widgets
	Default    // outside the chart and not over any chrome widget: let
	           // Windows pick (typically arrow), i.e. "we have no opinion"
};

// Map a semantic hit zone to the cursor Overlay.cpp's WM_SETCURSOR handler
// should show. Pure function of the zone alone — chrome-widget hits (toolbar
// button / hover-insert '+' / move grip) are not modeled by HtZone (they are
// resolved earlier, before hit-testing reaches the chart at all), so callers
// that are over one of those widgets should pass HtZone::Outside is NOT
// correct for that case: see GanttCursorForZone's overload below that takes an
// explicit "over chrome widget" flag for the one ambiguous case (Outside can
// mean either "over a chrome widget" or "truly outside everything").
HtCursor GanttCursorForZone(HtZone zone);

// Overload used by the overlay's WM_SETCURSOR handler, which already knows
// (from its own button/grip/hover-insert hit-testing, done before falling
// back to the chart hit test) whether the point is over a chrome widget.
// overChromeWidget wins over the zone (a click-through outside-chart point
// that happens to be over the toolbar is Hand, not Default).
HtCursor GanttCursorForZone(HtZone zone, bool overChromeWidget);
