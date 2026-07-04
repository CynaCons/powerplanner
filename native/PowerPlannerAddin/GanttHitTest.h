// Pure, COM-free semantic hit-testing for the on-slide Gantt overlay.
//
// The overlay's Tick() snapshots the chart's child shapes (screen rects only)
// into an HtSnapshot; mouse handlers then classify any screen point into a
// semantic zone without touching COM. This module has NO Windows/COM
// dependencies so it can be unit-tested in the PowerPoint-free ops harness.
#pragma once

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
	Title
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
	std::vector<HtItem> items;      // TASK / MILESTONE / ROW_LABEL / TITLE rects
	std::vector<HtRowBand> rowBands;
	// Half-width (in device pixels) of a task's resize edge band for THIS
	// snapshot. Defaults to kHtEdgePx (4px @ 96 DPI) so existing callers that
	// never touch this field keep the pre-DPI-awareness behavior unchanged.
	// The overlay sets this to HtScalePx(kHtEdgePx, dpi) once per tick.
	long edgeBandPx = 4;
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
