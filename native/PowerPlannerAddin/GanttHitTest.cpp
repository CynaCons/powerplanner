// Pure, COM-free semantic hit-testing for the on-slide Gantt overlay.
// See GanttHitTest.h for the contract. No Windows headers here on purpose —
// this file is compiled into the PowerPoint-free ops harness.
#include "GanttHitTest.h"

namespace {

bool InRect(const HtRect& r, long x, long y) {
	return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

long AbsLong(long v) {
	return v < 0 ? -v : v;
}

} // namespace

int HtScalePx(int basePx, int dpi) {
	if (dpi <= 0) dpi = kHtBaseDpi;
	// Round-half-away-from-zero, matching MulDiv(basePx, dpi, 96)'s rounding
	// for the positive values this is ever called with.
	long long num = (long long)basePx * (long long)dpi;
	long long den = (long long)kHtBaseDpi;
	if (num >= 0) return (int)((num + den / 2) / den);
	return (int)((num - den / 2) / den);
}

HtHit GanttHitTestPoint(const HtSnapshot& snap, long x, long y) {
	HtHit hit;
	if (!InRect(snap.chartRect, x, y)) {
		hit.zone = HtZone::Outside;
		return hit;
	}

	// 1) Task resize edges win over everything else (thin bars stay resizable
	//    even when a neighbour's body or a milestone overlaps the band). Across
	//    tasks, the nearest edge wins. The edge band's half-width is DPI-scaled
	//    (snap.edgeBandPx), defaulting to kHtEdgePx (4px @ 96 DPI) so existing
	//    callers that never set it keep the original behavior.
	long edgeBandPx = snap.edgeBandPx;
	const HtItem* edgeItem = nullptr;
	bool edgeIsLeft = false;
	long edgeDist = edgeBandPx + 1;
	for (const auto& it : snap.items) {
		if (it.kind != HtItemKind::Task) continue;
		if (y < it.rect.top || y >= it.rect.bottom) continue;
		long dl = AbsLong(x - it.rect.left);
		long dr = AbsLong(x - it.rect.right);
		if (dl <= edgeBandPx && dl < edgeDist) { edgeItem = &it; edgeIsLeft = true; edgeDist = dl; }
		if (dr <= edgeBandPx && dr < edgeDist) { edgeItem = &it; edgeIsLeft = false; edgeDist = dr; }
	}
	if (edgeItem) {
		hit.zone = edgeIsLeft ? HtZone::TaskEdgeL : HtZone::TaskEdgeR;
		hit.kind = HtItemKind::Task;
		hit.id = edgeItem->id;
		return hit;
	}

	// 2) Milestones (small markers drawn over the band).
	for (const auto& it : snap.items) {
		if (it.kind != HtItemKind::Milestone) continue;
		if (InRect(it.rect, x, y)) {
			hit.zone = HtZone::Milestone;
			hit.kind = HtItemKind::Milestone;
			hit.id = it.id;
			return hit;
		}
	}

	// 3) Task bodies.
	for (const auto& it : snap.items) {
		if (it.kind != HtItemKind::Task) continue;
		if (InRect(it.rect, x, y)) {
			hit.zone = HtZone::TaskBody;
			hit.kind = HtItemKind::Task;
			hit.id = it.id;
			return hit;
		}
	}

	// 4) Labels (row labels + title).
	for (const auto& it : snap.items) {
		if (it.kind != HtItemKind::RowLabel && it.kind != HtItemKind::Title) continue;
		if (InRect(it.rect, x, y)) {
			hit.zone = HtZone::Label;
			hit.kind = it.kind;
			hit.id = it.id;
			return hit;
		}
	}

	// 5) Markers (vertical date lines): thin rendered lines, so the overlay
	//    synthesizes each marker's hit rect as a band spanning the chart's
	//    full vertical extent, +-edgeBandPx wide. Below task/milestone hits
	//    (already returned above), above RowBand/EmptyCell (below). The
	//    nearest marker wins if bands from two markers overlap (mirrors the
	//    task-edge tie-break).
	{
		const HtItem* markerItem = nullptr;
		long markerDist = edgeBandPx + 1;
		for (const auto& it : snap.items) {
			if (it.kind != HtItemKind::Marker) continue;
			if (!InRect(it.rect, x, y)) continue;
			long cx = (it.rect.left + it.rect.right) / 2;
			long d = AbsLong(x - cx);
			if (d < markerDist) { markerItem = &it; markerDist = d; }
		}
		if (markerItem) {
			hit.zone = HtZone::Marker;
			hit.kind = HtItemKind::Marker;
			hit.id = markerItem->id;
			return hit;
		}
	}

	// 6) Row bands: right of the row's label column is an empty timeline cell;
	//    left of / around the label column is the generic row band.
	for (const auto& band : snap.rowBands) {
		if (y < band.yTop || y >= band.yBottom) continue;
		long labelRight = snap.chartRect.left;
		for (const auto& it : snap.items) {
			if (it.kind == HtItemKind::RowLabel && it.id == band.rowId) {
				labelRight = it.rect.right;
				break;
			}
		}
		hit.zone = (x >= labelRight) ? HtZone::EmptyCell : HtZone::RowBand;
		hit.id = band.rowId;
		hit.rowId = band.rowId;
		return hit;
	}

	// Inside the chart but in no band (e.g. the strip around the title):
	// chart background — reported as a RowBand with an empty rowId.
	hit.zone = HtZone::RowBand;
	return hit;
}

// ---- pure right-click context menu model -----------------------------------

namespace {

void AddScaleSubmenu(std::vector<HtMenuItem>& items, bool separatorBeforeHeader) {
	// The "Change Scale" top-level entry itself carries no command (its role
	// is purely to host the D/W/M submenu); Win32's AppendMenuW for a submenu
	// takes a menu handle, not a cmdId, so HtCmd_None here is correct and
	// MapMenuCommand never accepts it.
	items.push_back({ HtCmd_None, "Change Scale", separatorBeforeHeader, "" });
	items.push_back({ HtCmd_ScaleDay,   "Day",   false, "Change Scale" });
	items.push_back({ HtCmd_ScaleWeek,  "Week",  false, "Change Scale" });
	items.push_back({ HtCmd_ScaleMonth, "Month", false, "Change Scale" });
}

} // namespace

std::vector<HtMenuItem> BuildMenuForZone(HtZone zone, HtItemKind kind, bool hasRowId) {
	std::vector<HtMenuItem> items;

	switch (zone) {
	case HtZone::TaskBody:
	case HtZone::TaskEdgeL:
	case HtZone::TaskEdgeR:
		items.push_back({ HtCmd_AddTaskSameRow, "Add Task", false, "" });
		items.push_back({ HtCmd_Delete, "Delete", false, "" });
		items.push_back({ HtCmd_NudgeMinus1, "Nudge -1 day", true, "" });
		items.push_back({ HtCmd_NudgePlus1, "Nudge +1 day", false, "" });
		items.push_back({ HtCmd_PercentMinus10, "Percent -10%", true, "" });
		items.push_back({ HtCmd_PercentPlus10, "Percent +10%", false, "" });
		AddScaleSubmenu(items, /*separatorBeforeHeader=*/true);
		return items;

	case HtZone::Milestone:
		items.push_back({ HtCmd_AddTaskSameRow, "Add Task", false, "" });
		items.push_back({ HtCmd_Delete, "Delete", false, "" });
		items.push_back({ HtCmd_NudgeMinus1, "Nudge -1 day", true, "" });
		items.push_back({ HtCmd_NudgePlus1, "Nudge +1 day", false, "" });
		// No Percent items: milestones have no percent-complete.
		AddScaleSubmenu(items, /*separatorBeforeHeader=*/true);
		return items;

	case HtZone::RowBand:
		if (!hasRowId) {
			// Chart background (RowBand hit with an empty rowId).
			items.push_back({ HtCmd_AddRow, "Add Row", false, "" });
			AddScaleSubmenu(items, /*separatorBeforeHeader=*/true);
			return items;
		}
		items.push_back({ HtCmd_AddTaskThisRow, "Add Task", false, "" });
		items.push_back({ HtCmd_AddRowBelow, "Add Row Below", false, "" });
		items.push_back({ HtCmd_DeleteRow, "Delete Row", false, "" });
		return items;

	case HtZone::Label:
		if (kind == HtItemKind::RowLabel) {
			items.push_back({ HtCmd_AddTaskThisRow, "Add Task", false, "" });
			items.push_back({ HtCmd_AddRowBelow, "Add Row Below", false, "" });
			items.push_back({ HtCmd_DeleteRow, "Delete Row", false, "" });
			return items;
		}
		// Label(TITLE) and any other Label kind: same as chart background.
		items.push_back({ HtCmd_AddRow, "Add Row", false, "" });
		AddScaleSubmenu(items, /*separatorBeforeHeader=*/true);
		return items;

	case HtZone::EmptyCell:
		items.push_back({ HtCmd_EmptyCellAddTaskHere, "Add Task Here", false, "" });
		return items;

	case HtZone::Outside:
	default:
		return items; // no menu
	}
}

HtMenuOp MapMenuCommand(HtZone zone, int cmdId, HtItemKind kind, bool hasRowId) {
	HtMenuOp op;
	if (cmdId == HtCmd_None) return op;

	// Validate against the SAME item table BuildMenuForZone would produce for
	// this exact (zone,kind,hasRowId), so the two functions can never drift
	// apart — an invalid combo (e.g. HtCmd_PercentMinus10 on a Milestone, or
	// HtCmd_AddTaskThisRow on RowBand background) simply won't be found here.
	bool offered = false;
	for (const auto& item : BuildMenuForZone(zone, kind, hasRowId)) {
		if (item.cmdId == cmdId) { offered = true; break; }
	}
	if (!offered) return op;

	switch (cmdId) {
	case HtCmd_AddTaskSameRow:
		op.opKind = HtOpKind::AddTask; op.needsRowId = true;
		break;
	case HtCmd_Delete:
		op.opKind = HtOpKind::Delete; op.needsTaskId = true;
		break;
	case HtCmd_NudgeMinus1:
		op.opKind = HtOpKind::Nudge; op.needsTaskId = true; op.nudgeDays = -1;
		break;
	case HtCmd_NudgePlus1:
		op.opKind = HtOpKind::Nudge; op.needsTaskId = true; op.nudgeDays = 1;
		break;
	case HtCmd_PercentMinus10:
		op.opKind = HtOpKind::Percent; op.needsTaskId = true; op.percentDelta = -10;
		break;
	case HtCmd_PercentPlus10:
		op.opKind = HtOpKind::Percent; op.needsTaskId = true; op.percentDelta = 10;
		break;
	case HtCmd_ScaleDay:
		op.opKind = HtOpKind::SetScale; op.scale = "day";
		break;
	case HtCmd_ScaleWeek:
		op.opKind = HtOpKind::SetScale; op.scale = "week";
		break;
	case HtCmd_ScaleMonth:
		op.opKind = HtOpKind::SetScale; op.scale = "month";
		break;
	case HtCmd_AddTaskThisRow:
		op.opKind = HtOpKind::AddTask; op.needsRowId = true;
		break;
	case HtCmd_AddRowBelow:
		op.opKind = HtOpKind::AddRow; op.needsRowId = true;
		break;
	case HtCmd_DeleteRow:
		op.opKind = HtOpKind::DeleteRow; op.needsRowId = true;
		break;
	case HtCmd_EmptyCellAddTaskHere:
		op.opKind = HtOpKind::AddTaskAtPoint; op.needsRowId = true;
		break;
	case HtCmd_AddRow:
		op.opKind = HtOpKind::AddRow; op.needsRowId = false;
		break;
	default:
		break;
	}
	return op;
}

// ---- pure zone -> cursor mapping --------------------------------------------

HtCursor GanttCursorForZone(HtZone zone) {
	switch (zone) {
	case HtZone::TaskBody:
	case HtZone::Milestone:
		return HtCursor::SizeAll;
	case HtZone::TaskEdgeL:
	case HtZone::TaskEdgeR:
	case HtZone::Marker:
		return HtCursor::SizeWE;
	case HtZone::EmptyCell:
		return HtCursor::Cross;
	case HtZone::RowBand:
	case HtZone::Label:
		return HtCursor::Arrow;
	case HtZone::Outside:
	default:
		return HtCursor::Default;
	}
}

HtCursor GanttCursorForZone(HtZone zone, bool overChromeWidget) {
	if (overChromeWidget) return HtCursor::Hand;
	return GanttCursorForZone(zone);
}
