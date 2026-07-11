// Pure, COM-free semantic hit-testing for the on-slide Gantt overlay.
// See GanttHitTest.h for the contract. No Windows headers here on purpose —
// this file is compiled into the PowerPoint-free ops harness.
#include "GanttHitTest.h"
#include "GanttCommandRegistry.h"

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

	// 1b) Task progress boundary (between date edges and milestones).
	const HtItem* progItem = nullptr;
	long progDist = edgeBandPx + 1;
	for (const auto& it : snap.items) {
		if (it.kind != HtItemKind::Task) continue;
		if (it.progressPercent <= 0 || it.progressPercent >= 100) continue;
		if (y < it.rect.top || y >= it.rect.bottom) continue;
		long barW = it.rect.right - it.rect.left;
		if (barW < 4) continue;
		long progX = it.rect.left + barW * it.progressPercent / 100;
		long dp = AbsLong(x - progX);
		if (dp <= edgeBandPx && dp < progDist) {
			progItem = &it;
			progDist = dp;
		}
	}
	if (progItem) {
		hit.zone = HtZone::TaskProgressEdge;
		hit.kind = HtItemKind::Task;
		hit.id = progItem->id;
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

	// 4) Text annotations: a real rendered rect (unlike markers' synthesized
	//    band), so a plain InRect check suffices. Below task/milestone hits
	//    (already returned above — a text placed over a task stays subordinate
	//    to the task itself), above Labels/Markers/RowBand/EmptyCell (a text
	//    sitting over open timeline space still wins so it stays selectable/
	//    draggable/deletable).
	for (const auto& it : snap.items) {
		if (it.kind != HtItemKind::Text) continue;
		if (InRect(it.rect, x, y)) {
			hit.zone = HtZone::Text;
			hit.kind = HtItemKind::Text;
			hit.id = it.id;
			return hit;
		}
	}

	// 5) Labels (row labels + title).
	for (const auto& it : snap.items) {
		if (it.kind != HtItemKind::RowLabel && it.kind != HtItemKind::Title) continue;
		if (InRect(it.rect, x, y)) {
			hit.zone = HtZone::Label;
			hit.kind = it.kind;
			hit.id = it.id;
			if (it.kind == HtItemKind::RowLabel) hit.rowId = it.id;
			return hit;
		}
	}

	// 6) Markers (vertical date lines): thin rendered lines, so the overlay
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

	// 7) Row bands: right of the rail column is an empty timeline cell; the
	//    rail column itself (row-name area and rail-task label area per B2.1/
	//    B2.2) is RowBand and selects the row. Rail extent comes from
	//    snap.railRightPx when set (PP_ROWY); otherwise labelRight is used.
	for (const auto& band : snap.rowBands) {
		if (y < band.yTop || y >= band.yBottom) continue;
		long labelRight = snap.chartRect.left;
		if (snap.railRightPx > snap.railLeftPx) {
			labelRight = snap.railRightPx;
		} else {
			for (const auto& it : snap.items) {
				if (it.kind == HtItemKind::RowLabel && it.id == band.rowId) {
					labelRight = it.rect.right;
					break;
				}
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

// ---- pure shared command registry ------------------------------------------

HtMenuOp MapRegistryCommand(int cmdId) {
	HtMenuOp op;
	switch (cmdId) {
	case HtCmd_AddTaskSameRow:
	case HtCmd_AddTaskThisRow:
		op.opKind = HtOpKind::AddTask; op.needsRowId = true;
		break;
	case HtCmd_InsertTask:
		op.opKind = HtOpKind::InsertTaskBackground;
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
	case HtCmd_ScaleQuarter:
		op.opKind = HtOpKind::SetScale; op.scale = "quarter";
		break;
	case HtCmd_ScaleYear:
		op.opKind = HtOpKind::SetScale; op.scale = "year";
		break;
	case HtCmd_AddRowBelow:
		op.opKind = HtOpKind::AddRow; op.needsRowId = true;
		break;
	case HtCmd_AddRow:
		op.opKind = HtOpKind::AddRow; op.needsRowId = false;
		break;
	case HtCmd_AddRowAbove:
		op.opKind = HtOpKind::AddRowAbove; op.needsRowId = true;
		break;
	case HtCmd_MoveRowUp:
		op.opKind = HtOpKind::MoveRowUp; op.needsRowId = true;
		break;
	case HtCmd_MoveRowDown:
		op.opKind = HtOpKind::MoveRowDown; op.needsRowId = true;
		break;
	case HtCmd_IndentRow:
		op.opKind = HtOpKind::IndentRow; op.needsRowId = true;
		break;
	case HtCmd_OutdentRow:
		op.opKind = HtOpKind::OutdentRow; op.needsRowId = true;
		break;
	case HtCmd_DeleteRow:
		op.opKind = HtOpKind::DeleteRow; op.needsRowId = true;
		break;
	case HtCmd_Rename:
		op.opKind = HtOpKind::RenameRow; op.needsRowId = true;
		break;
	case HtCmd_EmptyCellAddTaskHere:
		op.opKind = HtOpKind::AddTaskAtPoint; op.needsRowId = true;
		break;
	case HtCmd_EmptyCellAddMilestoneHere:
		op.opKind = HtOpKind::AddMilestoneAtPoint; op.needsRowId = true;
		break;
	case HtCmd_EmptyCellAddNoteHere:
		op.opKind = HtOpKind::AddNoteAtPoint; op.needsRowId = true;
		break;
	case HtCmd_Swatch1: op.opKind = HtOpKind::SetTaskColor; op.needsTaskId = true; op.color = kAppBarSwatches[0]; break;
	case HtCmd_Swatch2: op.opKind = HtOpKind::SetTaskColor; op.needsTaskId = true; op.color = kAppBarSwatches[1]; break;
	case HtCmd_Swatch3: op.opKind = HtOpKind::SetTaskColor; op.needsTaskId = true; op.color = kAppBarSwatches[2]; break;
	case HtCmd_Swatch4: op.opKind = HtOpKind::SetTaskColor; op.needsTaskId = true; op.color = kAppBarSwatches[3]; break;
	case HtCmd_Swatch5: op.opKind = HtOpKind::SetTaskColor; op.needsTaskId = true; op.color = kAppBarSwatches[4]; break;
	case HtCmd_Swatch6: op.opKind = HtOpKind::SetTaskColor; op.needsTaskId = true; op.color = kAppBarSwatches[5]; break;
	case HtCmd_Swatch7: op.opKind = HtOpKind::SetTaskColor; op.needsTaskId = true; op.color = kAppBarSwatches[6]; break;
	case HtCmd_Swatch8: op.opKind = HtOpKind::SetTaskColor; op.needsTaskId = true; op.color = kAppBarSwatches[7]; break;
	case HtCmd_CycleLabelPlacement:
		op.opKind = HtOpKind::CycleLabelPlacement; op.needsTaskId = true;
		break;
	case HtCmd_AddNote:
		op.opKind = HtOpKind::AddNote; op.needsTaskId = true;
		break;
	case HtCmd_Edit:
		op.opKind = HtOpKind::Edit; op.needsTaskId = true;
		break;
	case HtCmd_Link:
		op.opKind = HtOpKind::EnterLinkMode; op.needsTaskId = true;
		break;
	case HtCmd_Unlink:
		op.opKind = HtOpKind::Unlink; op.needsTaskId = true;
		break;
	case HtCmd_InsertNote:
		op.opKind = HtOpKind::InsertFreeNote;
		break;
	case HtCmd_InsertMilestone:
		op.opKind = HtOpKind::InsertMilestoneBackground;
		break;
	case HtCmd_InsertMarker:
		op.opKind = HtOpKind::InsertMarkerBackground;
		break;
	case HtCmd_ToggleRailLabels:
		op.opKind = HtOpKind::ToggleRailLabels;
		break;
	case HtCmd_CycleGrid:
		op.opKind = HtOpKind::SetGridDensity; op.gridDensity = "__cycle__";
		break;
	case HtCmd_GridAuto:
		op.opKind = HtOpKind::SetGridDensity; op.gridDensity = "auto";
		break;
	case HtCmd_GridWeek:
		op.opKind = HtOpKind::SetGridDensity; op.gridDensity = "week";
		break;
	case HtCmd_GridMonth:
		op.opKind = HtOpKind::SetGridDensity; op.gridDensity = "month";
		break;
	case HtCmd_GridNone:
		op.opKind = HtOpKind::SetGridDensity; op.gridDensity = "none";
		break;
	case HtCmd_ReanchorNote:
		op.opKind = HtOpKind::ReanchorNote; op.needsTaskId = true;
		break;
	default:
		break;
	}
	return op;
}

namespace {

bool RegistryRowAppBarCmd(int cmdId) {
	switch (cmdId) {
	case HtCmd_Rename:
	case HtCmd_AddRowAbove:
	case HtCmd_AddRowBelow:
	case HtCmd_MoveRowUp:
	case HtCmd_MoveRowDown:
	case HtCmd_IndentRow:
	case HtCmd_OutdentRow:
	case HtCmd_DeleteRow:
		return true;
	default:
		return false;
	}
}

bool RegistryTaskAppBarCmd(int cmdId) {
	switch (cmdId) {
	case HtCmd_Edit:
	case HtCmd_Rename:
	case HtCmd_Swatch1: case HtCmd_Swatch2: case HtCmd_Swatch3: case HtCmd_Swatch4:
	case HtCmd_Swatch5: case HtCmd_Swatch6: case HtCmd_Swatch7: case HtCmd_Swatch8:
	case HtCmd_NudgeMinus1:
	case HtCmd_NudgePlus1:
	case HtCmd_CycleLabelPlacement:
	case HtCmd_Link:
	case HtCmd_Unlink:
	case HtCmd_AddNote:
	case HtCmd_Delete:
	case HtCmd_AddRowAbove:
	case HtCmd_AddRowBelow:
	case HtCmd_MoveRowUp:
	case HtCmd_MoveRowDown:
	case HtCmd_IndentRow:
	case HtCmd_OutdentRow:
	case HtCmd_DeleteRow:
		return true;
	default:
		return false;
	}
}

bool RegistryMilestoneAppBarCmd(int cmdId) {
	switch (cmdId) {
	case HtCmd_Edit:
	case HtCmd_Rename:
	case HtCmd_NudgeMinus1:
	case HtCmd_NudgePlus1:
	case HtCmd_AddNote:
	case HtCmd_Delete:
		return true;
	default:
		return false;
	}
}

bool RegistryMarkerAppBarCmd(int cmdId) {
	switch (cmdId) {
	case HtCmd_Rename:
	case HtCmd_NudgeMinus1:
	case HtCmd_NudgePlus1:
	case HtCmd_Delete:
		return true;
	default:
		return false;
	}
}

bool RegistryNoteAppBarCmd(int cmdId) {
	switch (cmdId) {
	case HtCmd_Edit:
	case HtCmd_ReanchorNote:
	case HtCmd_Delete:
		return true;
	default:
		return false;
	}
}

bool RegistryBackgroundAppBarCmd(int cmdId) {
	switch (cmdId) {
	case HtCmd_AddRow:
	case HtCmd_InsertTask:
	case HtCmd_InsertMilestone:
	case HtCmd_InsertMarker:
	case HtCmd_InsertNote:
		return true;
	default:
		return false;
	}
}

} // namespace

HtMenuOp MapRowAppBarCommand(int cmdId) {
	if (!RegistryRowAppBarCmd(cmdId)) return {};
	return MapRegistryCommand(cmdId);
}

HtMenuOp MapTaskAppBarCommand(int cmdId) {
	if (!RegistryTaskAppBarCmd(cmdId)) return {};
	// Rename on TASK opens inline label edit (SR-IXC-16); overlay handles it
	// directly — must not map to Edit/card.
	if (cmdId == HtCmd_Rename) return {};
	return MapRegistryCommand(cmdId);
}

HtMenuOp MapMilestoneAppBarCommand(int cmdId) {
	if (!RegistryMilestoneAppBarCmd(cmdId)) return {};
	HtMenuOp op = MapRegistryCommand(cmdId);
	if (cmdId == HtCmd_Edit || cmdId == HtCmd_Rename) {
		op.opKind = HtOpKind::Edit;
		op.needsTaskId = true;
	}
	return op;
}

HtMenuOp MapMarkerAppBarCommand(int cmdId) {
	if (!RegistryMarkerAppBarCmd(cmdId)) return {};
	HtMenuOp op = MapRegistryCommand(cmdId);
	if (cmdId == HtCmd_Rename) {
		op.opKind = HtOpKind::Edit;
		op.needsTaskId = true;
	}
	return op;
}

HtMenuOp MapNoteAppBarCommand(int cmdId) {
	if (!RegistryNoteAppBarCmd(cmdId)) return {};
	return MapRegistryCommand(cmdId);
}

HtMenuOp MapBackgroundAppBarCommand(int cmdId) {
	if (!RegistryBackgroundAppBarCmd(cmdId)) return {};
	return MapRegistryCommand(cmdId);
}

// ---- pure right-click context menu model -----------------------------------

std::vector<HtMenuItem> BuildMenuForZone(HtZone zone, HtItemKind kind, bool hasRowId,
	const PpDocument& doc, const std::string& hitId) {
	if (zone == HtZone::Outside) return {};

	if (zone == HtZone::EmptyCell) {
		return BuildEmptyCellMenuItems();
	}

	AppBarSel sel = HtZoneToAppBarSel(zone, kind, hasRowId);
	if (sel == AppBarSel::None && zone != HtZone::RowBand && zone != HtZone::Label) {
		return {};
	}

	const std::string selId = hitId;
	AppBarModel model = BuildAppBar(sel, doc, selId);
	return AppBarModelToMenuItems(model);
}

HtMenuOp MapMenuCommand(HtZone zone, int cmdId, HtItemKind kind, bool hasRowId,
	const PpDocument& doc, const std::string& hitId) {
	HtMenuOp none;
	if (cmdId == HtCmd_None) return none;

	bool offered = false;
	for (const auto& item : BuildMenuForZone(zone, kind, hasRowId, doc, hitId)) {
		if (item.cmdId == cmdId) { offered = true; break; }
	}
	if (!offered) return none;

	HtMenuOp op = MapRegistryCommand(cmdId);
	// Marker Rename is Edit in the marker app-bar context.
	if (zone == HtZone::Marker && cmdId == HtCmd_Rename) {
		op.opKind = HtOpKind::Edit;
		op.needsTaskId = true;
	}
	// Note context uses Edit for the edit button (not RenameRow).
	if (zone == HtZone::Text && cmdId == HtCmd_Edit) {
		op.opKind = HtOpKind::Edit;
		op.needsTaskId = true;
	}
	if ((zone == HtZone::RowBand || (zone == HtZone::Label && kind == HtItemKind::RowLabel))
		&& hasRowId && cmdId == HtCmd_Rename) {
		op.opKind = HtOpKind::RenameRow;
		op.needsRowId = true;
		op.needsTaskId = false;
	}
	return op;
}

// ---- pure zone -> cursor mapping --------------------------------------------

HtCursor GanttCursorForZone(HtZone zone) {
	switch (zone) {
	case HtZone::TaskBody:
	case HtZone::Milestone:
	case HtZone::Text:
		return HtCursor::SizeAll;
	case HtZone::TaskEdgeL:
	case HtZone::TaskEdgeR:
	case HtZone::TaskProgressEdge:
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
