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
// +-kHtEdgePx (i.e. 4px outside and 4px inside the bar).
constexpr long kHtEdgePx = 4;

// Classify a screen point against the snapshot. Pure function.
HtHit GanttHitTestPoint(const HtSnapshot& snap, long x, long y);
