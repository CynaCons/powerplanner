#include "../PowerPlannerAddin/GanttModel.h"
#include "../PowerPlannerAddin/GanttLayout.h"
#include "../PowerPlannerAddin/GanttJson.h"
#include "../PowerPlannerAddin/GanttOps.h"
#include "../PowerPlannerAddin/GanttHitTest.h"

#include <cstdio>
#include <string>

static bool Check(bool cond, const char* msg) {
	if (!cond) {
		std::printf("OPS HARNESS FAIL: %s\n", msg);
		return false;
	}
	return true;
}

// Free-standing zone-check helper (takes the snapshot explicitly) shared by
// RunHitTestChecks' 96dpi checks (via its own local lambda) and
// RunDpiHelperChecks' 192dpi edge-band checks below.
static bool zoneCheck2(const HtSnapshot& snap, long x, long y, HtZone zone, const char* id, const char* msg) {
	HtHit hit = GanttHitTestPoint(snap, x, y);
	return Check(hit.zone == zone && hit.id == id, msg);
}

// Pure hit-test checks over a synthetic snapshot (no COM, no window).
static bool RunHitTestChecks() {
	HtSnapshot snap;
	snap.chartRect = { 100, 100, 900, 500 };
	// Title strip across the top of the chart.
	snap.items.push_back({ HtItemKind::Title, "", { 300, 110, 700, 140 } });
	// Two rows with label rects in the left column.
	snap.items.push_back({ HtItemKind::RowLabel, "row-1", { 110, 180, 220, 220 } });
	snap.items.push_back({ HtItemKind::RowLabel, "row-2", { 110, 280, 220, 320 } });
	snap.rowBands.push_back({ "row-1", 150, 250 });
	snap.rowBands.push_back({ "row-2", 250, 350 });
	// Task bar in row 1, milestone marker in row 2.
	snap.items.push_back({ HtItemKind::Task, "task-1", { 300, 180, 500, 220 } });
	snap.items.push_back({ HtItemKind::Milestone, "ms-1", { 600, 280, 640, 320 } });

	bool ok = true;
	auto zoneCheck = [&](long x, long y, HtZone zone, const char* id, const char* msg) {
		HtHit hit = GanttHitTestPoint(snap, x, y);
		return Check(hit.zone == zone && hit.id == id, msg);
	};

	// Outside: beyond the chart rect (right/bottom are exclusive).
	ok = zoneCheck(50, 50, HtZone::Outside, "", "hit: point left of chart is Outside") && ok;
	ok = zoneCheck(900, 300, HtZone::Outside, "", "hit: chart right edge is exclusive (Outside)") && ok;
	ok = zoneCheck(400, 600, HtZone::Outside, "", "hit: point below chart is Outside") && ok;

	// Task body vs edge bands (edge = bar edge +-4px).
	ok = zoneCheck(400, 200, HtZone::TaskBody, "task-1", "hit: task center is TaskBody") && ok;
	ok = zoneCheck(300, 200, HtZone::TaskEdgeL, "task-1", "hit: task left edge is TaskEdgeL") && ok;
	ok = zoneCheck(296, 200, HtZone::TaskEdgeL, "task-1", "hit: 4px outside left edge is TaskEdgeL") && ok;
	ok = zoneCheck(304, 200, HtZone::TaskEdgeL, "task-1", "hit: 4px inside left edge is TaskEdgeL") && ok;
	ok = zoneCheck(305, 200, HtZone::TaskBody, "task-1", "hit: 5px inside left edge is TaskBody") && ok;
	ok = zoneCheck(500, 200, HtZone::TaskEdgeR, "task-1", "hit: task right edge is TaskEdgeR") && ok;
	ok = zoneCheck(504, 200, HtZone::TaskEdgeR, "task-1", "hit: 4px outside right edge is TaskEdgeR") && ok;
	ok = zoneCheck(497, 200, HtZone::TaskEdgeR, "task-1", "hit: 3px inside right edge is TaskEdgeR") && ok;
	// 5px outside the left edge: no longer an edge; falls through to the row.
	ok = zoneCheck(295, 200, HtZone::EmptyCell, "row-1", "hit: 5px outside left edge falls to EmptyCell") && ok;
	// Above/below the bar the edge band does not apply.
	ok = zoneCheck(300, 230, HtZone::EmptyCell, "row-1", "hit: edge x-band outside task y-range is EmptyCell") && ok;

	// Milestone.
	ok = zoneCheck(620, 300, HtZone::Milestone, "ms-1", "hit: milestone rect is Milestone") && ok;

	// Labels (row label + title) report kind + id.
	{
		HtHit hit = GanttHitTestPoint(snap, 150, 200);
		ok = Check(hit.zone == HtZone::Label && hit.kind == HtItemKind::RowLabel && hit.id == "row-1", "hit: row label rect is Label(RowLabel,row-1)") && ok;
		hit = GanttHitTestPoint(snap, 400, 120);
		ok = Check(hit.zone == HtZone::Label && hit.kind == HtItemKind::Title && hit.id.empty(), "hit: title rect is Label(Title)") && ok;
	}

	// Row band vs empty cell: left of the label column is RowBand, the open
	// timeline area is EmptyCell; both carry the rowId.
	{
		HtHit hit = GanttHitTestPoint(snap, 115, 240);
		ok = Check(hit.zone == HtZone::RowBand && hit.rowId == "row-1", "hit: gutter left of label column is RowBand(row-1)") && ok;
		hit = GanttHitTestPoint(snap, 700, 200);
		ok = Check(hit.zone == HtZone::EmptyCell && hit.rowId == "row-1", "hit: open timeline in row 1 is EmptyCell(row-1)") && ok;
		hit = GanttHitTestPoint(snap, 400, 300);
		ok = Check(hit.zone == HtZone::EmptyCell && hit.rowId == "row-2", "hit: open timeline in row 2 is EmptyCell(row-2)") && ok;
		hit = GanttHitTestPoint(snap, 100, 300);
		ok = Check(hit.zone == HtZone::RowBand && hit.rowId == "row-2", "hit: chart left edge inside band is RowBand(row-2)") && ok;
	}

	// Inside the chart but in no band: chart background = RowBand with empty rowId.
	{
		HtHit hit = GanttHitTestPoint(snap, 400, 145);
		ok = Check(hit.zone == HtZone::RowBand && hit.rowId.empty() && hit.id.empty(), "hit: in-chart point outside all bands is RowBand with empty rowId") && ok;
	}

	return ok;
}

// DPI scale helper checks: 96->1x (no-op), 120->1.25x, 144->1.5x, 192->2x on
// representative chrome constants, plus scaled edge-band hit tests at 192dpi
// (2x). Print the literal marker 'DPI HELPER OK' when all of these pass —
// this unit's pass signal, since plain "OPS HARNESS OK" predates DPI-awareness
// and would otherwise pass vacuously.
static bool RunDpiHelperChecks() {
	bool ok = true;

	// 96 DPI (100%) is a no-op on any base pixel constant.
	ok = Check(HtScalePx(5, 96) == 5, "HtScalePx(5,96) == 5 (INFL @ 100%)") && ok;
	ok = Check(HtScalePx(20, 96) == 20, "HtScalePx(20,96) == 20 (BADGE_H @ 100%)") && ok;
	ok = Check(HtScalePx(28, 96) == 28, "HtScalePx(28,96) == 28 (TOOLBAR_H @ 100%)") && ok;
	ok = Check(HtScalePx(32, 96) == 32, "HtScalePx(32,96) == 32 (button width @ 100%)") && ok;
	ok = Check(HtScalePx(4, 96) == 4, "HtScalePx(4,96) == 4 (kHtEdgePx @ 100%)") && ok;

	// 120 DPI (125%).
	ok = Check(HtScalePx(5, 120) == 6, "HtScalePx(5,120) == 6 (INFL @ 125%, MulDiv(5,120,96)=6.25->6)") && ok;
	ok = Check(HtScalePx(20, 120) == 25, "HtScalePx(20,120) == 25 (BADGE_H @ 125%)") && ok;
	ok = Check(HtScalePx(28, 120) == 35, "HtScalePx(28,120) == 35 (TOOLBAR_H @ 125%)") && ok;
	ok = Check(HtScalePx(32, 120) == 40, "HtScalePx(32,120) == 40 (button width @ 125%)") && ok;
	ok = Check(HtScalePx(4, 120) == 5, "HtScalePx(4,120) == 5 (kHtEdgePx @ 125%)") && ok;

	// 144 DPI (150%).
	ok = Check(HtScalePx(5, 144) == 8, "HtScalePx(5,144) == 8 (INFL @ 150%, 5*1.5=7.5->8)") && ok;
	ok = Check(HtScalePx(20, 144) == 30, "HtScalePx(20,144) == 30 (BADGE_H @ 150%)") && ok;
	ok = Check(HtScalePx(28, 144) == 42, "HtScalePx(28,144) == 42 (TOOLBAR_H @ 150%)") && ok;
	ok = Check(HtScalePx(32, 144) == 48, "HtScalePx(32,144) == 48 (button width @ 150%)") && ok;
	ok = Check(HtScalePx(4, 144) == 6, "HtScalePx(4,144) == 6 (kHtEdgePx @ 150%)") && ok;

	// 192 DPI (200%): exact doubling.
	ok = Check(HtScalePx(5, 192) == 10, "HtScalePx(5,192) == 10 (INFL @ 200%)") && ok;
	ok = Check(HtScalePx(20, 192) == 40, "HtScalePx(20,192) == 40 (BADGE_H @ 200%)") && ok;
	ok = Check(HtScalePx(28, 192) == 56, "HtScalePx(28,192) == 56 (TOOLBAR_H @ 200%)") && ok;
	ok = Check(HtScalePx(32, 192) == 64, "HtScalePx(32,192) == 64 (button width @ 200%)") && ok;
	ok = Check(HtScalePx(16, 192) == 32, "HtScalePx(16,192) == 32 (ROW_INSERT_BUTTON/GRIP_SIZE @ 200%)") && ok;
	ok = Check(HtScalePx(4, 192) == 8, "HtScalePx(4,192) == 8 (kHtEdgePx/drag threshold @ 200%)") && ok;

	// dpi<=0 falls back to the 96 baseline (no-op) rather than dividing by zero.
	ok = Check(HtScalePx(4, 0) == 4, "HtScalePx(4,0) falls back to 96 baseline") && ok;

	// Scaled edge-band hit tests at 192dpi (2x): the edge band widens from 4px
	// to 8px, so points that were EmptyCell at 96dpi become TaskEdgeL/R at
	// 192dpi, and the task body only starts strictly beyond the wider band.
	{
		HtSnapshot snap;
		snap.chartRect = { 100, 100, 900, 500 };
		snap.rowBands.push_back({ "row-1", 150, 250 });
		snap.items.push_back({ HtItemKind::Task, "task-1", { 300, 180, 500, 220 } });
		snap.edgeBandPx = HtScalePx(kHtEdgePx, 192); // 8px

		ok = Check(snap.edgeBandPx == 8, "edgeBandPx scaled to 8px at 192dpi") && ok;
		// 8px outside/inside the left edge is still within the widened band.
		ok = zoneCheck2(snap, 292, 200, HtZone::TaskEdgeL, "task-1", "hit@192dpi: 8px outside left edge is TaskEdgeL") && ok;
		ok = zoneCheck2(snap, 308, 200, HtZone::TaskEdgeL, "task-1", "hit@192dpi: 8px inside left edge is TaskEdgeL") && ok;
		// 9px inside the left edge is now clear of the (widened) band -> body.
		ok = zoneCheck2(snap, 309, 200, HtZone::TaskBody, "task-1", "hit@192dpi: 9px inside left edge is TaskBody") && ok;
		// The 5px-outside point that was EmptyCell at 96dpi (4px band) is now
		// TaskEdgeL at 192dpi's 8px band.
		ok = zoneCheck2(snap, 295, 200, HtZone::TaskEdgeL, "task-1", "hit@192dpi: 5px outside left edge is now TaskEdgeL (wider band)") && ok;
		// Right edge, symmetric check.
		ok = zoneCheck2(snap, 508, 200, HtZone::TaskEdgeR, "task-1", "hit@192dpi: 8px outside right edge is TaskEdgeR") && ok;
	}

	return ok;
}

int main() {
	PpDocument doc;
	doc.title = "Ops harness sample";
	doc.scale = "week";
	doc.rows.push_back(PpRow{"row-1", "Row 1", "", false});
	doc.rows.push_back(PpRow{"row-2", "Row 2", "", false});
	doc.tasks.push_back(PpTask{"task-1", "Task 1", "2026-01-01", "2026-01-02", "row-1", "#4472c4", 50});
	doc.tasks.push_back(PpTask{"task-2", "Task 2", "2026-02-01", "2026-02-04", "row-2", "#70ad47", 10});
	doc.milestones.push_back(PpMilestone{"ms-1", "Milestone 1", "2026-01-03", "row-1", "#ffc000"});
	doc.brackets.push_back(PpBracket{"br-1", "Bracket 1", "2026-01-01", "2026-01-03", "#000000", {"row-1"}});
	doc.brackets.push_back(PpBracket{"br-2", "Bracket 2", "2026-01-01", "2026-02-04", "#000000", {"row-1", "row-2"}});
	doc.deps.push_back(PpDependency{"dep-1", "task-1", "task-2", "finish-to-start"});

	bool ok = true;
	ok = Check(OpsSelfTest() == 0, "OpsSelfTest returns 0") && ok;
	ok = RunHitTestChecks() && ok;
	bool dpiOk = RunDpiHelperChecks();
	ok = dpiOk && ok;
	ok = Check(doc.tasks.size() == 2, "model sanity keeps initial tasks") && ok;

	const std::string addedRow = AddRow(doc, "Inserted Row", "row-1");
	ok = Check(!addedRow.empty() && addedRow != "row-1" && addedRow != "row-2", "AddRow returns fresh id") && ok;
	ok = Check(doc.rows.size() == 3 && doc.rows[1].id == addedRow && doc.rows[1].label == "Inserted Row", "AddRow inserts after requested row") && ok;

	const std::string appendedRow = AddRow(doc, "Appended Row", "missing-row");
	ok = Check(doc.rows.back().id == appendedRow, "AddRow appends when afterRowId missing") && ok;

	const std::string taskId = AddTask(doc, addedRow, "Added Task", "2026-03-01", "2026-03-05");
	bool foundTask = false;
	for (const auto& task : doc.tasks) foundTask = foundTask || (task.id == taskId && task.rowId == addedRow && task.label == "Added Task");
	ok = Check(foundTask, "AddTask adds task and returns id") && ok;

	long startBefore = DateToDays(doc.tasks.back().start);
	long endBefore = DateToDays(doc.tasks.back().end);
	ok = Check(NudgeTask(doc, taskId, 14), "NudgeTask returns true for existing task") && ok;
	ok = Check(DateToDays(doc.tasks.back().start) == startBefore + 14 && DateToDays(doc.tasks.back().end) == endBefore + 14, "NudgeTask shifts start and end by delta days") && ok;

	ok = Check(SetTaskDates(doc, taskId, "2026-04-01", "2026-04-10"), "SetTaskDates returns true for existing task") && ok;
	ok = Check(doc.tasks.back().start == "2026-04-01" && doc.tasks.back().end == "2026-04-10", "SetTaskDates sets start and end") && ok;
	ok = Check(SetTaskDates(doc, taskId, "2026-05-05", "2026-05-01") && doc.tasks.back().start == "2026-05-05" && doc.tasks.back().end == "2026-05-05", "SetTaskDates clamps end to start when end < start") && ok;
	ok = Check(!SetTaskDates(doc, "missing-task", "2026-01-01", "2026-01-02"), "SetTaskDates returns false for missing task") && ok;

	ok = Check(MoveTaskToRow(doc, taskId, "row-2"), "MoveTaskToRow returns true for existing task/row") && ok;
	ok = Check(doc.tasks.back().rowId == "row-2", "MoveTaskToRow updates rowId") && ok;
	ok = Check(!MoveTaskToRow(doc, taskId, "missing-row"), "MoveTaskToRow rejects missing row") && ok;

	ok = Check(SetTaskPercent(doc, taskId, 150) && doc.tasks.back().percent == 100, "SetTaskPercent clamps high values") && ok;
	ok = Check(SetTaskPercent(doc, taskId, -5) && doc.tasks.back().percent == 0, "SetTaskPercent clamps low values") && ok;

	ok = Check(SetTitle(doc, "Updated title") && doc.title == "Updated title", "SetTitle updates title") && ok;

	ok = Check(SetScale(doc, "month") && doc.scale == "month", "SetScale accepts month") && ok;
	ok = Check(!SetScale(doc, "nonsense") && doc.scale == "month", "SetScale rejects invalid values without changing scale") && ok;

	const std::string json = DocumentToJson(doc);
	PpDocument roundTrip = DocumentFromJson(json);
	ok = Check(roundTrip.scale == "month", "JSON round-trip preserves calendar.scale") && ok;
	ok = Check(DocumentFromJson("{\"title\":\"No calendar\"}").scale == "week", "JSON defaults missing calendar.scale to week") && ok;

	ok = Check(DeleteById(doc, taskId), "DeleteById removes task") && ok;
	for (const auto& task : doc.tasks) ok = Check(task.id != taskId, "deleted task is absent") && ok;

	ok = Check(DeleteById(doc, "row-1"), "DeleteById removes row") && ok;
	bool rowCascadeOk = true;
	for (const auto& row : doc.rows) rowCascadeOk = rowCascadeOk && row.id != "row-1";
	for (const auto& task : doc.tasks) rowCascadeOk = rowCascadeOk && task.rowId != "row-1";
	for (const auto& ms : doc.milestones) rowCascadeOk = rowCascadeOk && ms.rowId != "row-1";
	for (const auto& dep : doc.deps) rowCascadeOk = rowCascadeOk && dep.from != "task-1" && dep.to != "task-1";
	for (const auto& br : doc.brackets) {
		rowCascadeOk = rowCascadeOk && br.id != "br-1";
		for (const auto& rowId : br.rowIds) rowCascadeOk = rowCascadeOk && rowId != "row-1";
	}
	ok = Check(rowCascadeOk, "DeleteById row cascades tasks, milestones, dependent deps, and row-only brackets") && ok;

	ok = Check(DeleteById(doc, "dep-1") == false, "DeleteById returns false for already cascaded dependency") && ok;

	if (!ok) {
		std::printf("OPS HARNESS FAIL\n");
		return 1;
	}

	std::printf("OPS HARNESS OK\n");
	if (dpiOk) {
		std::printf("DPI HELPER OK\n");
	}
	return 0;
}
