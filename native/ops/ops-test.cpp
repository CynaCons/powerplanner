#include "../PowerPlannerAddin/GanttModel.h"
#include "../PowerPlannerAddin/GanttLayout.h"
#include "../PowerPlannerAddin/GanttJson.h"
#include "../PowerPlannerAddin/GanttOps.h"
#include "../PowerPlannerAddin/GanttHitTest.h"
#include "../PowerPlannerAddin/GanttAppBar.h"
#include "../PowerPlannerAddin/GanttCommandRegistry.h"
#include "../PowerPlannerAddin/GanttTheme.h"
#include "../PowerPlannerAddin/GanttScene.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

static bool Check(bool cond, const char* msg) {
	if (!cond) {
		std::printf("OPS HARNESS FAIL: %s\n", msg);
		return false;
	}
	return true;
}

// Lightweight PP_ROWY row-band checker for ops tests (no COM / GanttBuilder link).
static bool RowYBandsNonDegenerate(const std::string& rowYJson, size_t expectedRows) {
	const char* rowsKey = ::strstr(rowYJson.c_str(), "\"rows\":[");
	if (!rowsKey) return false;
	size_t count = 0;
	const char* p = rowsKey + 8;
	while (*p && *p != ']') {
		if (*p == '{') {
			float top = 0.0f, bot = 0.0f;
			char idBuf[128] = {};
			int n = ::sscanf_s(p, "{\"id\":\"%127[^\"]\",\"top\":%f,\"bot\":%f", idBuf,
				(unsigned)sizeof(idBuf), &top, &bot);
			if (n >= 3 && idBuf[0] && bot > top) ++count;
			while (*p && *p != '}') ++p;
		}
		++p;
	}
	return count == expectedRows;
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
	snap.railLeftPx = snap.chartRect.left;
	snap.railRightPx = 230;
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

	// Text annotation: a real rendered rect (unlike Marker's synthesized band).
	// Placed in row-2's open timeline area (EmptyCell territory absent the
	// text) to prove Text wins over RowBand/EmptyCell; a second point just
	// outside its rect falls through to EmptyCell as expected.
	snap.items.push_back({ HtItemKind::Text, "txt-1", { 660, 290, 720, 310 } });
	ok = zoneCheck(690, 300, HtZone::Text, "txt-1", "hit: text rect (over open row-2 timeline) is Text") && ok;
	ok = zoneCheck(660, 290, HtZone::Text, "txt-1", "hit: text rect top-left corner is Text") && ok;
	ok = zoneCheck(719, 309, HtZone::Text, "txt-1", "hit: text rect bottom-right (half-open) inclusive corner is Text") && ok;
	ok = zoneCheck(720, 300, HtZone::EmptyCell, "row-2", "hit: 1px right of text rect (half-open) falls through to EmptyCell") && ok;
	ok = zoneCheck(725, 300, HtZone::EmptyCell, "row-2", "hit: clear of text rect is EmptyCell") && ok;

	// Priority: a text rect overlapping a TASK bar stays subordinate to the
	// task (Text is below TaskBody/TaskEdge/Milestone per GanttHitTest's pass
	// ordering) — the task must win at the overlap point.
	snap.items.push_back({ HtItemKind::Text, "txt-2", { 350, 190, 450, 210 } });
	ok = zoneCheck(400, 200, HtZone::TaskBody, "task-1", "hit: text overlapping a task bar stays subordinate to TaskBody") && ok;

	// Marker: a vertical date line synthesized as a +-4px band (edgeBandPx,
	// default kHtEdgePx) spanning the chart's full vertical extent (top to
	// bottom), independent of row bands. Placed at x=800 (clear of the
	// task/milestone items above) so it hits regardless of y within the chart.
	snap.items.push_back({ HtItemKind::Marker, "mk-1", { 796, 100, 804, 500 } });
	ok = zoneCheck(800, 120, HtZone::Marker, "mk-1", "hit: marker center (near chart top) is Marker") && ok;
	ok = zoneCheck(800, 450, HtZone::Marker, "mk-1", "hit: marker center (near chart bottom) is Marker") && ok;
	ok = zoneCheck(796, 300, HtZone::Marker, "mk-1", "hit: marker left edge of band is Marker") && ok;
	ok = zoneCheck(803, 300, HtZone::Marker, "mk-1", "hit: marker right edge of band is Marker") && ok;
	ok = zoneCheck(804, 300, HtZone::EmptyCell, "row-2", "hit: 1px right of marker band falls through to EmptyCell") && ok;

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

	// PP_ROWY rail extent: a model row with no ROW_LABEL shape still maps
	// clicks in the rail column to RowBand(rowId).
	{
		snap.rowBands.push_back({ "row-3", 350, 400 });
		HtHit hit = GanttHitTestPoint(snap, 150, 375);
		ok = Check(hit.zone == HtZone::RowBand && hit.rowId == "row-3", "hit: rail column selects row without ROW_LABEL") && ok;
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

static const HtMenuItem* MenuFindCmd(const std::vector<HtMenuItem>& items, int cmdId) {
	for (const auto& it : items) if (it.cmdId == cmdId) return &it;
	return nullptr;
}

static const HtMenuItem* MenuFindInSubmenu(const std::vector<HtMenuItem>& items,
	const char* submenu, int cmdId) {
	for (const auto& it : items) {
		if (it.submenu == submenu && it.cmdId == cmdId) return &it;
	}
	return nullptr;
}

static bool MenuItemsMatchRegistry(const std::vector<HtMenuItem>& menu,
	const std::vector<HtMenuItem>& expected, const char* msg) {
	if (menu.size() != expected.size()) return Check(false, msg);
	for (size_t i = 0; i < menu.size(); ++i) {
		if (menu[i].cmdId != expected[i].cmdId
			|| menu[i].label != expected[i].label
			|| menu[i].submenu != expected[i].submenu
			|| menu[i].enabled != expected[i].enabled) {
			return Check(false, msg);
		}
	}
	return Check(true, msg);
}

static PpDocument MenuSampleDoc() {
	PpDocument doc;
	doc.rows.push_back(PpRow{"r1", "Row 1", "", false});
	doc.rows.push_back(PpRow{"r2", "Row 2", "", false});
	doc.tasks.push_back(PpTask{"t1", "Task 1", "2026-01-01", "2026-01-08", "r1", "#4355E0", 0});
	doc.tasks.push_back(PpTask{"t2", "Task 2", "2026-01-10", "2026-01-15", "r2", "", 50});
	doc.milestones.push_back(PpMilestone{"m1", "MS", "2026-01-12", "r1", ""});
	doc.markers.push_back(PpMarker{"mk1", "custom", "Marker", "2026-01-20", ""});
	doc.texts.push_back(PpText{"n1", "Note", "", "r1", "2026-01-05", "", 0, 0});
	doc.deps.push_back(PpDependency{"dep1", "t1", "t2", "finish-to-start"});
	return doc;
}

// V4 right-click menu model: every zone mirrors the shared app-bar registry;
// MapMenuCommand validates against the built menu then MapRegistryCommand.
// Print 'MENU MAP V4 OK' when these pass.
static bool RunMenuModelV4Checks() {
	bool ok = true;
	PpDocument doc = MenuSampleDoc();

	auto expectFromAppBar = [&](AppBarSel sel, const std::string& selId) {
		return AppBarModelToMenuItems(BuildAppBar(sel, doc, selId));
	};

	// ---- TaskBody / edges: mirror task app-bar context.
	for (HtZone z : { HtZone::TaskBody, HtZone::TaskEdgeL, HtZone::TaskEdgeR }) {
		auto menu = BuildMenuForZone(z, HtItemKind::Task, true, doc, "t1");
		auto expected = expectFromAppBar(AppBarSel::Task, "t1");
		ok = MenuItemsMatchRegistry(menu, expected, "task zone menu mirrors task app-bar registry") && ok;
	}

	// Anti-tautology pins: hardcoded labels+ids independent of registry builders.
	{
		auto items = BuildMenuForZone(HtZone::TaskBody, HtItemKind::Task, true, doc, "t1");
		const HtMenuItem* edit = MenuFindCmd(items, HtCmd_Edit);
		ok = Check(edit && edit->label == "Edit", "pin: task zone Edit/HtCmd_Edit") && ok;
		const HtMenuItem* del = MenuFindCmd(items, HtCmd_Delete);
		ok = Check(del && del->label == "Delete", "pin: task zone Delete/HtCmd_Delete") && ok;
		const HtMenuItem* link = MenuFindCmd(items, HtCmd_Link);
		ok = Check(link && link->label == "Link", "pin: task zone Link/HtCmd_Link") && ok;
		PpDocument noDepDoc = MenuSampleDoc();
		noDepDoc.deps.clear();
		items = BuildMenuForZone(HtZone::TaskBody, HtItemKind::Task, true, noDepDoc, "t1");
		const HtMenuItem* unlink = MenuFindCmd(items, HtCmd_Unlink);
		ok = Check(unlink && !unlink->enabled, "pin: task zone Unlink disabled without deps") && ok;
		items = BuildMenuForZone(HtZone::TaskBody, HtItemKind::Task, true, doc, "t1");
		unlink = MenuFindCmd(items, HtCmd_Unlink);
		ok = Check(unlink && unlink->enabled, "pin: task zone Unlink enabled when deps touch task") && ok;
	}

	// ---- Milestone / Marker / Text zones.
	{
		auto menu = BuildMenuForZone(HtZone::Milestone, HtItemKind::Milestone, true, doc, "m1");
		ok = MenuItemsMatchRegistry(menu, expectFromAppBar(AppBarSel::Milestone, "m1"),
			"milestone menu mirrors milestone app-bar registry") && ok;
	}
	{
		auto menu = BuildMenuForZone(HtZone::Marker, HtItemKind::Marker, true, doc, "mk1");
		ok = MenuItemsMatchRegistry(menu, expectFromAppBar(AppBarSel::Marker, "mk1"),
			"marker menu mirrors marker app-bar registry") && ok;
	}
	{
		auto menu = BuildMenuForZone(HtZone::Text, HtItemKind::Text, true, doc, "n1");
		ok = MenuItemsMatchRegistry(menu, expectFromAppBar(AppBarSel::Note, "n1"),
			"text menu mirrors note app-bar registry") && ok;
	}

	// ---- Row gutter / row label.
	for (int variant = 0; variant < 2; ++variant) {
		std::vector<HtMenuItem> menu = (variant == 0)
			? BuildMenuForZone(HtZone::RowBand, HtItemKind::Task, true, doc, "r1")
			: BuildMenuForZone(HtZone::Label, HtItemKind::RowLabel, true, doc, "r1");
		auto expected = expectFromAppBar(AppBarSel::Row, "r1");
		const char* label = (variant == 0) ? "RowBand(rowId)" : "Label(ROW_LABEL)";
		ok = MenuItemsMatchRegistry(menu, expected, (std::string(label) + " menu mirrors row app-bar").c_str()) && ok;
		const HtMenuItem* delRow = MenuFindCmd(menu, HtCmd_DeleteRow);
		ok = Check(delRow && delRow->label == "Delete",
			(std::string(label) + " menu has Delete row command").c_str()) && ok;
	}

	// ---- Empty cell: add task / milestone / note here.
	{
		auto menu = BuildMenuForZone(HtZone::EmptyCell, HtItemKind::Task, true, doc, "r1");
		auto expected = BuildEmptyCellMenuItems();
		ok = MenuItemsMatchRegistry(menu, expected, "EmptyCell menu is add task/milestone/note here") && ok;
	}

	// ---- Background: Insert + Scale + Grid submenus (+ Labels).
	for (int variant = 0; variant < 2; ++variant) {
		std::vector<HtMenuItem> menu = (variant == 0)
			? BuildMenuForZone(HtZone::RowBand, HtItemKind::Task, false, doc)
			: BuildMenuForZone(HtZone::Label, HtItemKind::Title, false, doc);
		auto expected = expectFromAppBar(AppBarSel::None, "");
		const char* label = (variant == 0) ? "RowBand(background)" : "Label(TITLE)";
		ok = MenuItemsMatchRegistry(menu, expected, (std::string(label) + " menu mirrors background app-bar").c_str()) && ok;
	}

	// Anti-tautology pins for background zone.
	{
		auto items = BuildMenuForZone(HtZone::RowBand, HtItemKind::Task, false, doc);
		const HtMenuItem* row = MenuFindInSubmenu(items, "Insert", HtCmd_AddRow);
		ok = Check(row && row->label == "Row", "pin: background Insert/Row/HtCmd_AddRow") && ok;
		const HtMenuItem* week = MenuFindInSubmenu(items, "Change Scale", HtCmd_ScaleWeek);
		ok = Check(week && week->label == "Week", "pin: background Scale/Week/HtCmd_ScaleWeek") && ok;
		const HtMenuItem* gridAuto = MenuFindInSubmenu(items, "Grid", HtCmd_GridAuto);
		ok = Check(gridAuto && gridAuto->label == "Auto", "pin: background Grid/Auto/HtCmd_GridAuto") && ok;
	}

	// ---- Outside: no menu.
	ok = Check(BuildMenuForZone(HtZone::Outside, HtItemKind::Task, false, doc).empty(),
		"Outside zone has no menu") && ok;

	// ---- MapMenuCommand: every offered cmd maps to a live registry op (not None).
	for (const auto& item : BuildMenuForZone(HtZone::TaskBody, HtItemKind::Task, true, doc, "t1")) {
		if (item.cmdId == HtCmd_None) continue;
		HtMenuOp op = MapMenuCommand(HtZone::TaskBody, item.cmdId, HtItemKind::Task, true, doc, "t1");
		ok = Check(op.opKind != HtOpKind::None,
			"map: every task-menu cmd maps to a registry op") && ok;
	}
	for (const auto& item : BuildEmptyCellMenuItems()) {
		HtMenuOp op = MapMenuCommand(HtZone::EmptyCell, item.cmdId, HtItemKind::Task, true, doc, "r1");
		ok = Check(op.opKind != HtOpKind::None,
			"map: every EmptyCell cmd maps to a registry op") && ok;
	}

	// ---- MapMenuCommand: representative op semantics.
	{
		HtMenuOp op = MapMenuCommand(HtZone::TaskBody, HtCmd_Swatch3, HtItemKind::Task, true, doc, "t1");
		ok = Check(op.opKind == HtOpKind::SetTaskColor && op.needsTaskId
			&& op.color && std::strcmp(op.color, "#7A4FA3") == 0,
			"map: TaskBody+Swatch3 -> SetTaskColor #7A4FA3") && ok;

		op = MapMenuCommand(HtZone::EmptyCell, HtCmd_EmptyCellAddMilestoneHere, HtItemKind::Task, true, doc, "r1");
		ok = Check(op.opKind == HtOpKind::AddMilestoneAtPoint && op.needsRowId,
			"map: EmptyCell+AddMilestoneHere -> AddMilestoneAtPoint") && ok;

		op = MapMenuCommand(HtZone::RowBand, HtCmd_AddRow, HtItemKind::Task, false, doc);
		ok = Check(op.opKind == HtOpKind::AddRow && !op.needsRowId,
			"map: background+AddRow -> AddRow") && ok;

		op = MapMenuCommand(HtZone::RowBand, HtCmd_GridMonth, HtItemKind::Task, false, doc);
		ok = Check(op.opKind == HtOpKind::SetGridDensity
			&& op.gridDensity && std::string(op.gridDensity) == "month",
			"map: background+GridMonth -> SetGridDensity(month)") && ok;

		op = MapMenuCommand(HtZone::RowBand, HtCmd_DeleteRow, HtItemKind::Task, true, doc, "r1");
		ok = Check(op.opKind == HtOpKind::DeleteRow && op.needsRowId,
			"map: RowBand+DeleteRow -> DeleteRow") && ok;
	}

	// ---- MapMenuCommand: invalid combos -> None.
	{
		HtMenuOp op = MapMenuCommand(HtZone::TaskBody, HtCmd_AddRow, HtItemKind::Task, true, doc, "t1");
		ok = Check(op.opKind == HtOpKind::None, "map: TaskBody+AddRow -> None") && ok;
		op = MapMenuCommand(HtZone::Outside, HtCmd_Delete, HtItemKind::Task, false, doc);
		ok = Check(op.opKind == HtOpKind::None, "map: Outside+Delete -> None") && ok;
	}

	return ok;
}

// Zone -> cursor mapping checks: exhaustive over every HtZone value (both the
// bare GanttCursorForZone(zone) and the (zone, overChromeWidget) overload used
// by the overlay's WM_SETCURSOR handler). Print 'CURSOR MAP OK' when these
// pass (after OPS HARNESS OK / DPI HELPER OK / MENU MAP OK).
static bool RunCursorMapChecks() {
	bool ok = true;

	// Exhaustive over every HtZone enumerator: TaskBody/Milestone/Text ->
	// SizeAll, TaskEdgeL/R -> SizeWE, EmptyCell -> Cross, RowBand/Label ->
	// Arrow, Outside -> Default. Listed in HtZone declaration order so a
	// future zone added to the enum without a case here is easy to spot in
	// review.
	static const struct { HtZone zone; HtCursor expected; const char* msg; } kZoneCases[] = {
		{ HtZone::Outside,    HtCursor::Default, "cursor: Outside -> Default" },
		{ HtZone::TaskBody,   HtCursor::SizeAll, "cursor: TaskBody -> SizeAll" },
		{ HtZone::TaskEdgeL,  HtCursor::SizeWE,  "cursor: TaskEdgeL -> SizeWE" },
		{ HtZone::TaskEdgeR,  HtCursor::SizeWE,  "cursor: TaskEdgeR -> SizeWE" },
		{ HtZone::Milestone,  HtCursor::SizeAll, "cursor: Milestone -> SizeAll" },
		{ HtZone::Label,      HtCursor::Arrow,   "cursor: Label -> Arrow" },
		{ HtZone::Marker,     HtCursor::SizeWE,  "cursor: Marker -> SizeWE (ew-resize)" },
		{ HtZone::Text,       HtCursor::SizeAll, "cursor: Text -> SizeAll" },
		{ HtZone::RowBand,    HtCursor::Arrow,   "cursor: RowBand -> Arrow" },
		{ HtZone::EmptyCell,  HtCursor::Cross,   "cursor: EmptyCell -> Cross" },
	};
	constexpr size_t kZoneCount = sizeof(kZoneCases) / sizeof(kZoneCases[0]);
	// If this fires, a zone was added to/removed from HtZone without updating
	// kZoneCases above -- the exhaustiveness this test promises would silently
	// lapse otherwise.
	static_assert(kZoneCount == 10, "HtZone case count changed: update kZoneCases in RunCursorMapChecks");

	for (const auto& c : kZoneCases) {
		ok = Check(GanttCursorForZone(c.zone) == c.expected, c.msg) && ok;
		// overChromeWidget=false must agree with the bare overload for every zone.
		ok = Check(GanttCursorForZone(c.zone, false) == c.expected, (std::string(c.msg) + " (overload, no chrome)").c_str()) && ok;
	}

	// overChromeWidget=true wins over the zone for every zone, including
	// Outside (a click-through outside-chart point that happens to be over
	// the toolbar/hover-insert '+'/move grip is Hand, not Default/Arrow/etc.).
	for (const auto& c : kZoneCases) {
		ok = Check(GanttCursorForZone(c.zone, true) == HtCursor::Hand,
			(std::string("cursor: ") + c.msg + " but overChromeWidget=true -> Hand").c_str()) && ok;
	}

	return ok;
}

// Marker ops checks: AddMarker/SetMarkerDate/SetMarkerLabel, DeleteById's
// marker-removal branch (DeleteById previously never checked doc.markers),
// and a JSON round-trip that preserves a "custom" marker's type unchanged.
// Print 'MARKER OPS OK' when these pass (after OPS HARNESS OK / DPI HELPER
// OK / MENU MAP OK / CURSOR MAP OK).
static bool RunMarkerOpsChecks() {
	bool ok = true;

	PpDocument doc;
	doc.title = "Marker ops sample";
	doc.scale = "week";

	const std::string mk1 = AddMarker(doc, "today", "Today", "2026-06-25");
	ok = Check(!mk1.empty(), "AddMarker returns a non-empty id") && ok;
	ok = Check(doc.markers.size() == 1, "AddMarker appends a marker") && ok;
	ok = Check(doc.markers.back().id == mk1 && doc.markers.back().type == "today"
		&& doc.markers.back().label == "Today" && doc.markers.back().date == "2026-06-25",
		"AddMarker stores type/label/date under the returned id") && ok;

	const std::string mk2 = AddMarker(doc, "deadline", "Ship it", "2026-07-30");
	ok = Check(!mk2.empty() && mk2 != mk1, "AddMarker returns a fresh, unique id for a second marker") && ok;

	const std::string mk3 = AddMarker(doc, "custom", "Board review", "2026-08-01");
	ok = Check(!mk3.empty() && mk3 != mk1 && mk3 != mk2, "AddMarker returns a fresh, unique id for a custom marker") && ok;
	ok = Check(doc.markers.size() == 3, "AddMarker grew the markers list to 3") && ok;

	ok = Check(SetMarkerDate(doc, mk3, "2026-08-15"), "SetMarkerDate returns true for existing marker") && ok;
	ok = Check(doc.markers.back().date == "2026-08-15", "SetMarkerDate updates the date") && ok;
	ok = Check(!SetMarkerDate(doc, "missing-marker", "2026-01-01"), "SetMarkerDate returns false for missing marker") && ok;

	ok = Check(SetMarkerLabel(doc, mk3, "Board review (final)"), "SetMarkerLabel returns true for existing marker") && ok;
	ok = Check(doc.markers.back().label == "Board review (final)", "SetMarkerLabel updates the label") && ok;
	ok = Check(!SetMarkerLabel(doc, "missing-marker", "x"), "SetMarkerLabel returns false for missing marker") && ok;

	// JSON round-trip: a "custom" marker's type/label/date/id survive unchanged.
	{
		const std::string json = DocumentToJson(doc);
		PpDocument roundTrip = DocumentFromJson(json);
		bool foundCustom = false;
		for (const auto& m : roundTrip.markers) {
			if (m.id == mk3) {
				foundCustom = Check(m.type == "custom", "round-trip preserves custom marker's type unchanged")
					&& Check(m.label == "Board review (final)", "round-trip preserves custom marker's label")
					&& Check(m.date == "2026-08-15", "round-trip preserves custom marker's date");
			}
		}
		ok = Check(foundCustom, "round-trip finds the custom marker by id") && ok;
		ok = Check(roundTrip.markers.size() == 3, "round-trip preserves marker count") && ok;
	}

	// DeleteById: DeleteById previously never checked doc.markers (it only
	// handled rows/tasks/milestones/brackets/deps), so deleting a marker id
	// would silently fall through and return false. Confirm the new branch
	// actually removes it and only it.
	ok = Check(DeleteById(doc, mk2), "DeleteById removes a marker (deadline)") && ok;
	ok = Check(doc.markers.size() == 2, "DeleteById shrinks the markers list by one") && ok;
	bool mk2Gone = true;
	for (const auto& m : doc.markers) mk2Gone = mk2Gone && m.id != mk2;
	ok = Check(mk2Gone, "deleted marker is absent") && ok;
	bool othersRemain = false;
	for (const auto& m : doc.markers) othersRemain = othersRemain || m.id == mk1;
	ok = Check(othersRemain, "DeleteById on a marker leaves other markers intact") && ok;
	ok = Check(!DeleteById(doc, mk2), "DeleteById returns false for an already-deleted marker") && ok;

	return ok;
}

// Text ops checks: AddText/SetTextLabel/MoveText, DeleteById's text-removal
// (direct + cascading via row/task/milestone deletion), a JSON round-trip
// (including the absent-field -> empty-vector backward-compat case), and a
// layout assertion that an anchored text's laid-out rect tracks its anchor
// task across a rebuild after the anchor's dates shift. Print 'TEXT OPS OK'
// when these pass (after OPS HARNESS OK / DPI HELPER OK / MENU MAP OK /
// CURSOR MAP OK / MARKER OPS OK).
static bool RunTextOpsChecks() {
	bool ok = true;

	PpDocument doc;
	doc.title = "Text ops sample";
	doc.scale = "week";
	doc.rows.push_back(PpRow{"row-1", "Row 1", "", false});
	doc.tasks.push_back(PpTask{"task-1", "Task 1", "2026-01-01", "2026-01-05", "row-1", "", 0});
	doc.milestones.push_back(PpMilestone{"ms-1", "Milestone 1", "2026-01-10", "row-1", ""});

	// AddText: anchored.
	const std::string anchTxt = AddText(doc, "Anchored note", "task-1", "", "");
	ok = Check(!anchTxt.empty(), "AddText returns a non-empty id (anchored)") && ok;
	ok = Check(doc.texts.size() == 1, "AddText appends a text") && ok;
	ok = Check(doc.texts.back().id == anchTxt && doc.texts.back().label == "Anchored note"
		&& doc.texts.back().anchorId == "task-1", "AddText stores label/anchorId under the returned id") && ok;

	// AddText: free (anchorId empty -> rowId/date placement).
	const std::string freeTxt = AddText(doc, "Free note", "", "row-1", "2026-01-03");
	ok = Check(!freeTxt.empty() && freeTxt != anchTxt, "AddText returns a fresh, unique id for a free text") && ok;
	ok = Check(doc.texts.back().anchorId.empty() && doc.texts.back().rowId == "row-1"
		&& doc.texts.back().date == "2026-01-03", "AddText (free) stores rowId/date, no anchorId") && ok;
	ok = Check(doc.texts.size() == 2, "AddText grew the texts list to 2") && ok;

	// SetTextLabel.
	ok = Check(SetTextLabel(doc, freeTxt, "Free note (renamed)"), "SetTextLabel returns true for existing text") && ok;
	ok = Check(doc.texts.back().label == "Free note (renamed)", "SetTextLabel updates the label") && ok;
	ok = Check(!SetTextLabel(doc, "missing-text", "x"), "SetTextLabel returns false for missing text") && ok;

	// MoveText: dx/dy always updates; free text can also re-home rowId/date.
	ok = Check(MoveText(doc, anchTxt, 5.0f, -3.0f), "MoveText returns true for existing (anchored) text") && ok;
	{
		const PpText* t = nullptr;
		for (const auto& x : doc.texts) if (x.id == anchTxt) t = &x;
		ok = Check(t && t->dx == 5.0f && t->dy == -3.0f, "MoveText updates dx/dy on an anchored text") && ok;
		ok = Check(t && t->anchorId == "task-1", "MoveText does not disturb an anchored text's anchorId") && ok;
	}
	ok = Check(MoveText(doc, freeTxt, 2.0f, 4.0f, "row-1", "2026-01-06"), "MoveText returns true for existing (free) text") && ok;
	{
		const PpText* t = nullptr;
		for (const auto& x : doc.texts) if (x.id == freeTxt) t = &x;
		ok = Check(t && t->dx == 2.0f && t->dy == 4.0f, "MoveText updates dx/dy on a free text") && ok;
		ok = Check(t && t->date == "2026-01-06", "MoveText re-homes a free text's date") && ok;
	}
	ok = Check(!MoveText(doc, "missing-text", 1.0f, 1.0f), "MoveText returns false for missing text") && ok;

	// JSON round-trip: texts survive, including anchorId/rowId/date/dx/dy.
	{
		const std::string json = DocumentToJson(doc);
		PpDocument roundTrip = DocumentFromJson(json);
		ok = Check(roundTrip.texts.size() == 2, "round-trip preserves text count") && ok;
		bool foundAnchored = false, foundFree = false;
		for (const auto& t : roundTrip.texts) {
			if (t.id == anchTxt) {
				foundAnchored = Check(t.anchorId == "task-1", "round-trip preserves anchored text's anchorId")
					&& Check(t.dx == 5.0f && t.dy == -3.0f, "round-trip preserves anchored text's dx/dy");
			}
			if (t.id == freeTxt) {
				foundFree = Check(t.rowId == "row-1", "round-trip preserves free text's rowId")
					&& Check(t.date == "2026-01-06", "round-trip preserves free text's date")
					&& Check(t.anchorId.empty(), "round-trip preserves free text's empty anchorId");
			}
		}
		ok = Check(foundAnchored, "round-trip finds the anchored text by id") && ok;
		ok = Check(foundFree, "round-trip finds the free text by id") && ok;
	}

	// Backward compatibility: a document JSON with no "texts" field at all
	// parses to an empty vector rather than failing/crashing.
	ok = Check(DocumentFromJson("{\"title\":\"No texts field\"}").texts.empty(),
		"JSON defaults missing texts field to an empty vector") && ok;

	// Layout: an anchored text's laid-out rect tracks its anchor task when the
	// anchor's dates shift (the whole point of anchoring — no separate
	// position is stored for it).
	{
		GanttLayoutResult L1 = LayoutGantt(doc, "2026-01-01");
		const LaidText* lt1 = nullptr;
		for (const auto& t : L1.texts) if (t.id == anchTxt) lt1 = &t;
		ok = Check(lt1 != nullptr, "layout produces a LaidText for the anchored text") && ok;
		ok = Check(lt1 && lt1->anchored, "layout marks the anchored text as anchored") && ok;
		// task-1 spans 2026-01-01..2026-01-05 (widthDays = 5), viewStart = 2026-01-01 -> xDay=0.
		ok = Check(lt1 && lt1->xDay == 0 && lt1->widthDays == 5, "layout: anchored text tracks task-1's initial xDay/widthDays") && ok;

		// Shift the anchor task's dates and re-run layout: the text's laid-out
		// xDay/widthDays must move with it (same doc, same anchorId, no change
		// to the PpText itself).
		ok = Check(SetTaskDates(doc, "task-1", "2026-02-01", "2026-02-10"), "SetTaskDates shifts the anchor task") && ok;
		GanttLayoutResult L2 = LayoutGantt(doc, "2026-01-01");
		const LaidText* lt2 = nullptr;
		for (const auto& t : L2.texts) if (t.id == anchTxt) lt2 = &t;
		ok = Check(lt2 != nullptr, "layout still produces a LaidText for the anchored text after the shift") && ok;
		long expectedXDay = DateToDays("2026-02-01") - DateToDays("2026-01-01");
		ok = Check(lt2 && lt2->xDay == expectedXDay, "layout: anchored text's xDay tracks the shifted anchor") && ok;
		ok = Check(lt2 && lt2->widthDays == 10, "layout: anchored text's widthDays tracks the shifted anchor's span") && ok;
		ok = Check(lt2 && lt2->dx == 5.0f && lt2->dy == -3.0f, "layout: anchored text's dx/dy offset is carried through unchanged") && ok;
	}

	// Layout: a free text sits at its (rowId, date) cell origin, independent
	// of any task.
	{
		GanttLayoutResult L = LayoutGantt(doc, "2026-01-01");
		const LaidText* ltFree = nullptr;
		for (const auto& t : L.texts) if (t.id == freeTxt) ltFree = &t;
		ok = Check(ltFree != nullptr, "layout produces a LaidText for the free text") && ok;
		ok = Check(ltFree && !ltFree->anchored, "layout marks the free text as not anchored") && ok;
		long expectedFreeXDay = DateToDays("2026-01-06") - DateToDays("2026-01-01");
		ok = Check(ltFree && ltFree->xDay == expectedFreeXDay, "layout: free text's xDay matches its own date") && ok;
		ok = Check(ltFree && ltFree->rowIndex == 0, "layout: free text sits in row-1 (rowIndex 0)") && ok;
	}

	// DeleteById: direct delete of a free text.
	ok = Check(DeleteById(doc, freeTxt), "DeleteById removes a text directly") && ok;
	bool freeGone = true;
	for (const auto& t : doc.texts) freeGone = freeGone && t.id != freeTxt;
	ok = Check(freeGone, "deleted text is absent") && ok;
	ok = Check(!DeleteById(doc, freeTxt), "DeleteById returns false for an already-deleted text") && ok;

	// DeleteById: cascading delete when the anchor task is deleted.
	ok = Check(DeleteById(doc, "task-1"), "DeleteById removes the anchor task") && ok;
	bool anchoredGone = true;
	for (const auto& t : doc.texts) anchoredGone = anchoredGone && t.id != anchTxt;
	ok = Check(anchoredGone, "DeleteById cascades: text anchored to a deleted task is also removed") && ok;

	// DeleteById: cascading delete when a milestone anchor is deleted.
	{
		const std::string msTxt = AddText(doc, "Milestone note", "ms-1", "", "");
		ok = Check(DeleteById(doc, "ms-1"), "DeleteById removes the anchor milestone") && ok;
		bool msTxtGone = true;
		for (const auto& t : doc.texts) msTxtGone = msTxtGone && t.id != msTxt;
		ok = Check(msTxtGone, "DeleteById cascades: text anchored to a deleted milestone is also removed") && ok;
	}

	// DeleteById: cascading delete when a free text's row is deleted.
	{
		const std::string rowTxt = AddText(doc, "Row note", "", "row-1", "2026-01-02");
		ok = Check(DeleteById(doc, "row-1"), "DeleteById removes the row") && ok;
		bool rowTxtGone = true;
		for (const auto& t : doc.texts) rowTxtGone = rowTxtGone && t.id != rowTxt;
		ok = Check(rowTxtGone, "DeleteById cascades: free text in a deleted row is also removed") && ok;
	}

	return ok;
}

// S1 s1-theme-tokens: assert the pure scene builder colors prims from the
// design tokens (GanttTheme.h). Builds a real scene (COM-free) and inspects
// prim fills/lines. Print 'THEME TOKENS OK' when these pass.
static const Prim* FindPrim(const Scene& sc, const char* kind, const char* id) {
	for (const auto& p : sc.prims) {
		if (p.tagKind == kind && (id == nullptr || p.tagId == id)) return &p;
	}
	return nullptr;
}

static bool RunThemeTokenChecks() {
	bool ok = true;

	// Blend-on-white formula is pinned (regression guard on the token helper):
	// blend(#7A4FA3, white, .40) and blend(swatch1, white, .40), per channel.
	ok = Check(gt::BlendOnWhite(0x7A4FA3, 0.40f) == 0xCAB9DA, "BlendOnWhite(plum,.40) == #CAB9DA") && ok;
	ok = Check(gt::BlendOnWhite(gt::swatch1, 0.40f) == 0xB4BBF3, "BlendOnWhite(swatch1,.40) == #B4BBF3") && ok;
	ok = Check(gt::EffectiveSwatch("") == gt::swatch1, "empty task color resolves to swatch1") && ok;
	ok = Check(gt::EffectiveSwatch("#7A4FA3") == 0x7A4FA3, "explicit task color parses") && ok;
	ok = Check(gt::ParseHexColor("bogus", gt::ink) == gt::ink, "malformed hex falls back to default") && ok;

	PpDocument d;
	d.title = "Theme sample";
	d.scale = "week";
	d.rows.push_back(PpRow{"r1", "Row 1", "", false});
	d.rows.push_back(PpRow{"r2", "Row 2", "", false});
	// Colored task (plum) with progress; uncolored task (⇒ swatch1).
	d.tasks.push_back(PpTask{"tc", "Colored", "2026-06-01", "2026-06-20", "r1", "#7A4FA3", 50});
	d.tasks.push_back(PpTask{"tp", "Plain",   "2026-06-05", "2026-06-25", "r2", "",        0});
	d.milestones.push_back(PpMilestone{"m1", "MS", "2026-06-15", "r1", ""});
	d.markers.push_back(PpMarker{"today", "today", "Today", "2026-06-10", ""});

	Scene sc; std::string mn, mx; long pad = 0; float ppd = 0.0f;
	if (!Check(BuildProjectedScene(d, 960.0f, &sc, &mn, &mx, &pad, &ppd), "BuildProjectedScene succeeds")) return false;

	// v2.5.3-latency-green: in-range nudge keeps the frozen projection window so
	// prim keys stay stable for the scene-diff fast path.
	{
		PpDocument dn = d;
		dn.tasks[0].start = DaysToDate(DateToDays(dn.tasks[0].start) + 1);
		dn.tasks[0].end = DaysToDate(DateToDays(dn.tasks[0].end) + 1);
		ok = Check(DocDatesFitPaddedWindow(dn, mn, mx, pad), "nudge +1d fits padded projection window") && ok;
		Scene scFrozen;
		ok = Check(BuildSceneWithProjection(dn, 960.0f, &scFrozen, mn, mx, pad, ppd), "frozen-window scene builds") && ok;
		ok = Check(scFrozen.prims.size() == sc.prims.size(), "frozen nudge keeps prim count") && ok;
		Scene scFresh; std::string fn, fx; long fp = 0; float fppd = 0.0f;
		ok = Check(BuildProjectedScene(dn, 960.0f, &scFresh, &fn, &fx, &fp, &fppd), "fresh projection after nudge builds") && ok;
		ok = Check(scFresh.prims.size() != sc.prims.size() || fn != mn, "fresh projection shifts window or prims") && ok;
	}

	// v2.5.3-latency-trim-round4: non-structural deltas patch cached JSON.
	{
		const std::string baseJson = DocumentToJson(d);
		PpDocument dn = d;
		dn.tasks[0].start = DaysToDate(DateToDays(dn.tasks[0].start) + 1);
		dn.tasks[0].end = DaysToDate(DateToDays(dn.tasks[0].end) + 1);
		const std::string patched = TryPatchDocJson(d, dn, baseJson);
		ok = Check(!patched.empty(), "TryPatchDocJson patches in-range nudge") && ok;
		ok = Check(DocumentFromJson(patched).tasks[0].start == dn.tasks[0].start,
			"patched JSON carries nudged start date") && ok;
		PpDocument dc = d;
		dc.tasks[0].color = "#112233";
		const std::string colorPatch = TryPatchDocJson(d, dc, baseJson);
		ok = Check(!colorPatch.empty() && DocumentFromJson(colorPatch).tasks[0].color == "#112233",
			"TryPatchDocJson patches task color") && ok;
		PpDocument ds = d;
		ds.rows.push_back(PpRow{"r9", "Extra", "", false});
		ok = Check(TryPatchDocJson(d, ds, baseJson).empty(), "TryPatchDocJson rejects structural row add") && ok;
		GanttJson_CommitParsedCache(baseJson);
		PpDocument dn2 = d;
		dn2.tasks[0].start = DaysToDate(DateToDays(dn2.tasks[0].start) + 2);
		dn2.tasks[0].end = DaysToDate(DateToDays(dn2.tasks[0].end) + 2);
		const std::string fastPatch = GanttJson_TryPatchFast(d, dn2);
		ok = Check(!fastPatch.empty(), "GanttJson_TryPatchFast patches without re-parse") && ok;
		GanttJson_InvalidateParsedCache();
	}

	// Colored task: track = blend(plum,.40); progress = solid plum; radius = bar.radius.
	const Prim* tc = FindPrim(sc, "TASK", "tc");
	ok = Check(tc && tc->style.fill && tc->style.fillBgr == Bgr(gt::BlendOnWhite(0x7A4FA3, 0.40f)),
		"colored task track fill == blend(plum,.40)") && ok;
	ok = Check(tc && tc->style.corner == gt::bar_radius, "task bar corner radius == bar.radius") && ok;
	const Prim* tcp = FindPrim(sc, "TASK_PROGRESS", "tc");
	ok = Check(tcp && tcp->style.fill && tcp->style.fillBgr == Bgr(0x7A4FA3),
		"colored task progress fill == solid plum") && ok;

	// Uncolored task: track = blend(swatch1,.40).
	const Prim* tp = FindPrim(sc, "TASK", "tp");
	ok = Check(tp && tp->style.fillBgr == Bgr(gt::BlendOnWhite(gt::swatch1, 0.40f)),
		"empty-color task track fill == blend(swatch1,.40)") && ok;

	// Milestone diamond fill == ink; today marker line == primary.
	const Prim* ms = FindPrim(sc, "MILESTONE", "m1");
	ok = Check(ms && ms->style.fill && ms->style.fillBgr == Bgr(gt::ink), "milestone fill == ink") && ok;
	const Prim* today = FindPrim(sc, "TODAY_LINE", "today");
	ok = Check(today && today->style.line && today->style.lineBgr == Bgr(gt::primary), "today marker line == primary") && ok;

	// Header band uses the headerBand token (no stray Material grey).
	const Prim* hb = FindPrim(sc, "HEADER_BAND", nullptr);
	ok = Check(hb && hb->style.fillBgr == Bgr(gt::headerBand), "header band fill == headerBand token") && ok;

	return ok;
}

// S1 s1-rail-labels: labelPlacement model + railLabels global + rail dot/label
// emission. Print 'LABEL OPS OK' when these pass.
static bool RunLabelOpsChecks() {
	bool ok = true;

	// --- ops validation ---
	PpDocument d;
	d.rows.push_back(PpRow{"r1", "Row 1", "", false});
	d.rows.push_back(PpRow{"r2", "Row 2", "", false});
	d.tasks.push_back(PpTask{"tr", "Rail task", "2026-06-01", "2026-06-28", "r1", "", 50});
	d.tasks.push_back(PpTask{"tb", "Bar task",  "2026-06-01", "2026-06-28", "r2", "", 0});

	ok = Check(SetLabelPlacement(d, "tr", "rail") && d.tasks[0].labelPlacement == "rail", "SetLabelPlacement sets rail") && ok;
	ok = Check(SetLabelPlacement(d, "tr", "both") && d.tasks[0].labelPlacement == "both", "SetLabelPlacement sets both") && ok;
	ok = Check(SetLabelPlacement(d, "tr", "") && d.tasks[0].labelPlacement == "bar", "SetLabelPlacement normalizes empty to bar") && ok;
	ok = Check(!SetLabelPlacement(d, "tr", "nonsense"), "SetLabelPlacement rejects invalid value") && ok;
	ok = Check(!SetLabelPlacement(d, "missing", "rail"), "SetLabelPlacement returns false for missing task") && ok;
	ok = Check(SetRailLabelsGlobal(d, true) && d.railLabels, "SetRailLabelsGlobal turns on") && ok;
	ok = Check(SetRailLabelsGlobal(d, false) && !d.railLabels, "SetRailLabelsGlobal turns off") && ok;

	// --- JSON round-trip + backward compatibility ---
	SetLabelPlacement(d, "tr", "rail");
	SetRailLabelsGlobal(d, true);
	PpDocument rt = DocumentFromJson(DocumentToJson(d));
	ok = Check(rt.railLabels, "round-trip preserves railLabels") && ok;
	bool trRail = false; for (const auto& t : rt.tasks) if (t.id == "tr") trRail = (t.labelPlacement == "rail");
	ok = Check(trRail, "round-trip preserves task labelPlacement") && ok;
	PpDocument legacy = DocumentFromJson("{\"title\":\"legacy\",\"tasks\":[{\"id\":\"x\",\"rowId\":\"r\",\"label\":\"L\",\"start\":\"2026-01-01\",\"end\":\"2026-01-02\"}]}");
	ok = Check(!legacy.railLabels && legacy.tasks.size() == 1 && legacy.tasks[0].labelPlacement.empty(),
		"legacy JSON defaults railLabels=false / labelPlacement empty") && ok;

	// --- layout assertions: rail label inside rail, at the task's lane; on-bar
	// label absent for the rail task, present for the bar task ---
	PpDocument scDoc;
	scDoc.rows.push_back(PpRow{"r1", "Row 1", "", false});
	scDoc.rows.push_back(PpRow{"r2", "Row 2", "", false});
	scDoc.tasks.push_back(PpTask{"tr", "Rail task", "2026-06-01", "2026-06-28", "r1", "", 50});
	scDoc.tasks.push_back(PpTask{"tb", "Bar task",  "2026-06-01", "2026-06-28", "r2", "", 0});
	SetLabelPlacement(scDoc, "tr", "rail");

	Scene sc; std::string mn, mx; long pad = 0; float ppd = 0.0f;
	if (!Check(BuildProjectedScene(scDoc, 960.0f, &sc, &mn, &mx, &pad, &ppd), "label scene builds")) return false;

	const Prim* railLbl = FindPrim(sc, "RAIL_TASKLBL", "tr");
	const Prim* railDot = FindPrim(sc, "RAIL_DOT", "tr");
	const Prim* trBar = FindPrim(sc, "TASK", "tr");
	const Prim* tbBar = FindPrim(sc, "TASK", "tb");
	ok = Check(railLbl != nullptr, "rail task emits a RAIL_TASKLBL") && ok;
	ok = Check(railDot != nullptr, "rail task emits a RAIL_DOT") && ok;
	if (railLbl) {
		bool insideRail = railLbl->x >= MARGIN && (railLbl->x + railLbl->w) <= (MARGIN + ROW_GUTTER + 0.5f);
		ok = Check(insideRail, "rail label rect lies inside the rail column") && ok;
	}
	if (railLbl && trBar) {
		float lblCy = railLbl->y + railLbl->h / 2.0f;
		float barCy = trBar->y + trBar->h / 2.0f;
		ok = Check(std::fabs(lblCy - barCy) < ROW_HEIGHT / 2.0f, "rail label sits at the task's lane") && ok;
	}
	ok = Check(trBar && trBar->text.empty(), "rail task TASK prim has no embedded label text") && ok;
	ok = Check(FindPrim(sc, "TASK_LABEL", "tr") == nullptr, "rail task emits NO TASK_LABEL") && ok;
	const Prim* tbLbl = FindPrim(sc, "TASK_LABEL", "tb");
	ok = Check(tbBar && tbBar->text.empty(), "bar task TASK prim has no embedded label text") && ok;
	ok = Check(tbLbl && !tbLbl->text.empty(), "bar task label is a separate TASK_LABEL prim") && ok;
	ok = Check(FindPrim(sc, "RAIL_TASKLBL", "tb") == nullptr, "bar task emits NO rail label") && ok;

	// --- global override: railLabels=true forces the bar task into the rail ---
	PpDocument gDoc = scDoc;
	SetRailLabelsGlobal(gDoc, true);
	Scene gsc; std::string gmn, gmx; long gpad = 0; float gppd = 0.0f;
	if (Check(BuildProjectedScene(gDoc, 960.0f, &gsc, &gmn, &gmx, &gpad, &gppd), "global-rail scene builds")) {
		ok = Check(FindPrim(gsc, "RAIL_TASKLBL", "tb") != nullptr, "global railLabels moves bar task to rail") && ok;
		const Prim* tbBar2 = FindPrim(gsc, "TASK", "tb");
		ok = Check(tbBar2 && tbBar2->text.empty(), "global railLabels: TASK prim has no embedded label") && ok;
		ok = Check(FindPrim(gsc, "TASK_LABEL", "tb") == nullptr, "global railLabels suppresses TASK_LABEL") && ok;
	}

	// --- PP_ROWY: milestone-only last row still gets a non-degenerate band ---
	{
		PpDocument mDoc;
		mDoc.rows.push_back(PpRow{"r1", "Row 1", "", false});
		mDoc.rows.push_back(PpRow{"r_launch", "Launch", "", false});
		mDoc.tasks.push_back(PpTask{"t1", "Work", "2026-06-01", "2026-06-12", "r1", "", 0});
		mDoc.milestones.push_back(PpMilestone{"m2", "Ship", "2026-08-10", "r_launch", ""});
		std::string rowY = BuildRowYJson(mDoc, 960.0f, "2026-06-01");
		ok = Check(RowYBandsNonDegenerate(rowY, mDoc.rows.size()),
			"PP_ROWY: every row including milestone-only last has bot>top") && ok;
	}

	return ok;
}

// S1 s1-hier-axis: hierarchical two-band header + grid density/style. Print
// 'GRID OPS OK' when these pass.
static int CountKind(const Scene& sc, const char* kind) {
	int c = 0; for (const auto& p : sc.prims) if (p.tagKind == kind) ++c; return c;
}
static bool AnyIdPrefix(const Scene& sc, const char* kind, char pfx) {
	for (const auto& p : sc.prims) if (p.tagKind == kind && !p.tagId.empty() && p.tagId[0] == pfx) return true;
	return false;
}
static bool AllIdsPrefix(const Scene& sc, const char* kind, char pfx) {
	bool any = false;
	for (const auto& p : sc.prims) if (p.tagKind == kind) { any = true; if (p.tagId.empty() || p.tagId[0] != pfx) return false; }
	return any;
}
static PpDocument GridDoc(const std::string& start, const std::string& end, const std::string& scale) {
	PpDocument d; d.scale = scale;
	d.rows.push_back(PpRow{"r1", "Row 1", "", false});
	d.tasks.push_back(PpTask{"t1", "T", start, end, "r1", "", 0});
	return d;
}

// v2.5.1 i2b scene emission contract (SR-VIZ-01/02/03): TASK_LABEL prim after
// TASK/TASK_PROGRESS; three DEP elbow segments (arrow on final only); marker
// labels in the strip above axis headers with two-level stagger.
// Print 'SCENE VIZ OK' when these pass.
static bool RunSceneVizChecks() {
	bool ok = true;
	const float headTop = MARGIN; // chartTop - AXIS_H

	// --- SR-VIZ-01: TASK_LABEL prim, emission order, inside vs right fallback ---
	{
		PpDocument d;
		d.scale = "week";
		d.rows.push_back(PpRow{"r1", "Row 1", "", false});
		d.tasks.push_back(PpTask{"wide", "Wide bar label", "2026-06-01", "2026-06-28", "r1", "#4355E0", 60});
		d.tasks.push_back(PpTask{"narrow", "Tiny", "2026-06-01", "2026-06-01", "r1", "", 0});
		Scene sc; std::string mn, mx; long pad = 0; float ppd = 0.0f;
		if (!Check(BuildProjectedScene(d, 960.0f, &sc, &mn, &mx, &pad, &ppd), "scene viz: doc builds")) return false;

		const Prim* wideBar = FindPrim(sc, "TASK", "wide");
		const Prim* wideProg = FindPrim(sc, "TASK_PROGRESS", "wide");
		const Prim* wideLbl = FindPrim(sc, "TASK_LABEL", "wide");
		ok = Check(wideBar && wideBar->text.empty(), "scene viz: TASK round-rect carries no label text") && ok;
		ok = Check(wideProg != nullptr, "scene viz: progress task emits TASK_PROGRESS") && ok;
		ok = Check(wideLbl && !wideLbl->text.empty(), "scene viz: bar label is a TASK_LABEL prim") && ok;

		int taskIdx = -1, progIdx = -1, lblIdx = -1;
		for (size_t i = 0; i < sc.prims.size(); ++i) {
			const auto& p = sc.prims[i];
			if (p.tagId != "wide") continue;
			if (p.tagKind == "TASK") taskIdx = (int)i;
			else if (p.tagKind == "TASK_PROGRESS") progIdx = (int)i;
			else if (p.tagKind == "TASK_LABEL") lblIdx = (int)i;
		}
		ok = Check(taskIdx >= 0 && lblIdx > taskIdx, "scene viz: TASK_LABEL follows TASK in emission order") && ok;
		ok = Check(progIdx > taskIdx && lblIdx > progIdx, "scene viz: TASK_LABEL follows TASK_PROGRESS") && ok;

		if (wideBar && wideLbl) {
			ok = Check(wideLbl->style.align == TextAlign::Center,
				"scene viz: wide bar label aligns center inside bar") && ok;
			ok = Check(std::fabs(wideLbl->x - wideBar->x) < 0.5f && std::fabs(wideLbl->w - wideBar->w) < 0.5f,
				"scene viz: inside label spans the bar width") && ok;
		}

		const Prim* narrowBar = FindPrim(sc, "TASK", "narrow");
		const Prim* narrowLbl = FindPrim(sc, "TASK_LABEL", "narrow");
		if (narrowBar && narrowLbl) {
			ok = Check(narrowLbl->style.align == TextAlign::Left,
				"scene viz: narrow bar label falls back right of bar (left-aligned text)") && ok;
			ok = Check(narrowLbl->x >= narrowBar->x + narrowBar->w + 5.0f,
				"scene viz: fallback label sits right of the bar") && ok;
		}
	}

	// --- SR-VIZ-03: three DEP segments; arrow only on final; deps before bars ---
	{
		PpDocument d;
		d.rows.push_back(PpRow{"r1", "Row 1", "", false});
		d.tasks.push_back(PpTask{"t1", "Task 1", "2026-01-01", "2026-01-05", "r1", "", 0});
		d.tasks.push_back(PpTask{"t2", "Task 2", "2026-01-06", "2026-01-10", "r1", "", 0});
		std::string depId = AddDependency(d, "t1", "t2");
		ok = Check(!depId.empty(), "scene viz: dep setup") && ok;
		Scene sc; std::string mn, mx; long pad = 0; float ppd = 0.0f;
		if (!Check(BuildProjectedScene(d, 960.0f, &sc, &mn, &mx, &pad, &ppd), "scene viz: dep scene builds")) return false;

		std::vector<const Prim*> segs;
		for (const auto& p : sc.prims) if (p.tagKind == "DEP" && p.tagId == depId) segs.push_back(&p);
		ok = Check(segs.size() == 3, "scene viz: one dependency emits three DEP prims") && ok;
		if (segs.size() == 3) {
			ok = Check(segs[0]->kind == PrimKind::Line && !segs[0]->style.arrowEnd,
				"scene viz: DEP segment 1 is a plain horizontal line") && ok;
			ok = Check(segs[1]->kind == PrimKind::Line && !segs[1]->style.arrowEnd,
				"scene viz: DEP segment 2 is a plain vertical line") && ok;
			ok = Check(segs[2]->kind == PrimKind::Connector && segs[2]->style.arrowEnd,
				"scene viz: DEP segment 3 is a connector with arrowEnd") && ok;
			float y1 = segs[0]->y, midX = segs[1]->x;
			ok = Check(std::fabs(segs[0]->y - segs[0]->y2) < 0.5f, "scene viz: DEP h-seg is horizontal") && ok;
			ok = Check(std::fabs(segs[1]->x - segs[1]->x2) < 0.5f, "scene viz: DEP v-seg is vertical") && ok;
			ok = Check(std::fabs(segs[2]->y - segs[2]->y2) < 0.5f, "scene viz: DEP arrow-seg is horizontal") && ok;
			ok = Check(std::fabs(segs[0]->y - y1) < 0.5f && std::fabs(segs[1]->y - y1) < 0.5f,
				"scene viz: h-seg and v-seg share source row y") && ok;
			ok = Check(std::fabs(segs[1]->x - midX) < 0.5f && std::fabs(segs[2]->x - midX) < 0.5f,
				"scene viz: v-seg and arrow-seg share elbow x") && ok;
		}

		int lastDep = -1, firstTask = -1;
		for (size_t i = 0; i < sc.prims.size(); ++i) {
			if (sc.prims[i].tagKind == "DEP") lastDep = (int)i;
			if (firstTask < 0 && sc.prims[i].tagKind == "TASK") firstTask = (int)i;
		}
		ok = Check(lastDep >= 0 && firstTask > lastDep, "scene viz: DEP prims emit before TASK bars") && ok;
	}

	// --- SR-VIZ-02: marker labels above axis header band; two-level stagger ---
	{
		PpDocument d;
		d.scale = "week";
		d.rows.push_back(PpRow{"r1", "Row 1", "", false});
		d.tasks.push_back(PpTask{"t1", "T", "2026-06-01", "2026-06-28", "r1", "", 0});
		d.markers.push_back(PpMarker{"m1", "today", "TODAY", "2026-06-10", ""});
		d.markers.push_back(PpMarker{"m2", "deadline", "BOARD REVIEW", "2026-06-10", ""});

		Scene sc; std::string mn, mx; long pad = 0; float ppd = 0.0f;
		if (!Check(BuildProjectedScene(d, 960.0f, &sc, &mn, &mx, &pad, &ppd), "scene viz: marker scene builds")) return false;

		const Prim* todayLbl = FindPrim(sc, "TODAY_LABEL", "m1");
		const Prim* dlLbl = FindPrim(sc, "DEADLINE_LABEL", "m2");
		ok = Check(todayLbl && dlLbl, "scene viz: marker label prims emitted") && ok;
		if (todayLbl) {
			ok = Check(todayLbl->y < headTop, "scene viz: marker label sits above the axis header band") && ok;
		}
		if (todayLbl && dlLbl) {
			const float levelGap = gt::marker_label_strip - gt::marker_label_h;
			ok = Check(std::fabs(todayLbl->y - dlLbl->y) >= levelGap - 0.5f,
				"scene viz: overlapping marker labels use two stagger levels") && ok;
		}
		for (const auto& p : sc.prims) {
			if (p.tagKind != "TODAY_LABEL" && p.tagKind != "DEADLINE_LABEL" && p.tagKind != "CUSTOM_MARKER_LABEL") continue;
			ok = Check(p.y + p.h <= headTop + 0.5f,
				"scene viz: marker label does not descend into axis header text band") && ok;
		}
	}

	return ok;
}

static bool RunGridOpsChecks() {
	bool ok = true;

	// --- ops validation + round-trip ---
	PpDocument d = GridDoc("2026-06-01", "2026-08-10", "week");
	ok = Check(SetGridDensity(d, "month") && d.gridDensity == "month", "SetGridDensity sets month") && ok;
	ok = Check(SetGridDensity(d, "none") && d.gridDensity == "none", "SetGridDensity sets none") && ok;
	ok = Check(SetGridDensity(d, "auto") && d.gridDensity.empty(), "SetGridDensity auto normalizes to empty") && ok;
	ok = Check(!SetGridDensity(d, "bogus"), "SetGridDensity rejects invalid") && ok;
	ok = Check(SetGridStyle(d, "dotted") && d.gridStyle == "dotted", "SetGridStyle sets dotted") && ok;
	ok = Check(SetGridStyle(d, "solid") && d.gridStyle.empty(), "SetGridStyle solid normalizes to empty") && ok;
	ok = Check(!SetGridStyle(d, "bogus"), "SetGridStyle rejects invalid") && ok;
	SetGridDensity(d, "week"); SetGridStyle(d, "dotted");
	PpDocument rt = DocumentFromJson(DocumentToJson(d));
	ok = Check(rt.gridDensity == "week" && rt.gridStyle == "dotted", "round-trip preserves gridDensity/gridStyle") && ok;
	PpDocument legacy = DocumentFromJson("{\"title\":\"x\",\"tasks\":[{\"id\":\"t\",\"rowId\":\"r\",\"start\":\"2026-01-01\",\"end\":\"2026-01-05\"}]}");
	ok = Check(legacy.gridDensity.empty() && legacy.gridStyle.empty(), "legacy JSON defaults grid fields empty") && ok;

	Scene sc; std::string mn, mx; long pad = 0; float ppd = 0.0f;

	// --- week: months top band, Monday bottom cells + Monday separators ---
	{
		PpDocument w = GridDoc("2026-06-01", "2026-08-10", "week");
		if (Check(BuildProjectedScene(w, 960.0f, &sc, &mn, &mx, &pad, &ppd), "week scene builds")) {
			ok = Check(AllIdsPrefix(sc, "AXIS_TOP", 'M'), "week: top band is months") && ok;
			ok = Check(AllIdsPrefix(sc, "AXIS_BOT", 'W'), "week: bottom band is weeks") && ok;
			ok = Check(AllIdsPrefix(sc, "AXIS_TICK", 'W'), "week: separators are Monday (week) ticks") && ok;
			ok = Check(CountKind(sc, "AXIS_MAJOR") >= 1, "week: month major ticks present") && ok;
			ok = Check(CountKind(sc, "AXIS_BANDDIV") == 1, "week: one band divider") && ok;
		}
	}

	// --- quarter: "Q2 2026"-style bottom cells, year top band ---
	{
		PpDocument q = GridDoc("2026-02-01", "2026-11-30", "quarter");
		if (Check(BuildProjectedScene(q, 960.0f, &sc, &mn, &mx, &pad, &ppd), "quarter scene builds")) {
			ok = Check(AllIdsPrefix(sc, "AXIS_TOP", 'Y'), "quarter: top band is years") && ok;
			ok = Check(AllIdsPrefix(sc, "AXIS_BOT", 'Q'), "quarter: bottom band is quarters") && ok;
			bool qLabel = false;
			for (const auto& p : sc.prims) if (p.tagKind == "AXIS_BOT" && !p.text.empty() && p.text[0] == L'Q') qLabel = true;
			ok = Check(qLabel, "quarter: a bottom label reads like 'Q2 2026'") && ok;
		}
	}

	// --- day: day-number bottom cells, auto-thinned (fewer labels than days) ---
	{
		PpDocument dd = GridDoc("2026-01-01", "2026-04-10", "day"); // ~100 days -> step 2
		if (Check(BuildProjectedScene(dd, 960.0f, &sc, &mn, &mx, &pad, &ppd), "day scene builds")) {
			long dayCount = DateToDays("2026-04-10") - DateToDays("2026-01-01") + 1;
			int botCount = CountKind(sc, "AXIS_BOT");
			ok = Check(AllIdsPrefix(sc, "AXIS_BOT", 'D'), "day: bottom band is days") && ok;
			ok = Check(botCount > 0 && botCount < (int)dayCount, "day: labels auto-thinned (fewer than days)") && ok;
		}
	}

	// --- density override: month ticks over a week-scale header ---
	{
		PpDocument w = GridDoc("2026-06-01", "2026-08-10", "week");
		SetGridDensity(w, "month");
		if (Check(BuildProjectedScene(w, 960.0f, &sc, &mn, &mx, &pad, &ppd), "override scene builds")) {
			ok = Check(AllIdsPrefix(sc, "AXIS_TICK", 'M'), "override: gridDensity=month makes ticks month-tier") && ok;
			ok = Check(AllIdsPrefix(sc, "AXIS_BOT", 'W'), "override: bottom labels stay weeks") && ok;
		}
	}

	// --- none: no ticks, bands intact ---
	{
		PpDocument w = GridDoc("2026-06-01", "2026-08-10", "week");
		SetGridDensity(w, "none");
		if (Check(BuildProjectedScene(w, 960.0f, &sc, &mn, &mx, &pad, &ppd), "none scene builds")) {
			ok = Check(CountKind(sc, "AXIS_TICK") == 0, "none: no separator ticks") && ok;
			ok = Check(CountKind(sc, "AXIS_TOP") >= 1 && CountKind(sc, "AXIS_BOT") >= 1, "none: band labels intact") && ok;
		}
	}

	// --- cap fallback: multi-year day scale coarsens below the ~150 cap ---
	{
		PpDocument dd = GridDoc("2024-01-01", "2026-12-31", "day"); // ~1096 days
		if (Check(BuildProjectedScene(dd, 960.0f, &sc, &mn, &mx, &pad, &ppd), "cap scene builds")) {
			int ticks = CountKind(sc, "AXIS_TICK");
			ok = Check(ticks > 0 && ticks <= 150, "cap: day ticks fall back below ~150") && ok;
			ok = Check(!AllIdsPrefix(sc, "AXIS_TICK", 'D'), "cap: tick tier coarsened away from per-day") && ok;
		}
	}

	return ok;
}

static const AppBarGroup* AppBarFindGroup(const AppBarModel& model, const std::string& label) {
	for (const auto& g : model.groups) {
		if (g.label == label) return &g;
	}
	return nullptr;
}

static const AppBarItem* AppBarFindItem(const AppBarGroup& group, int cmd) {
	for (const auto& item : group.items) {
		if (item.cmd == cmd) return &item;
	}
	return nullptr;
}

static const AppBarItem* AppBarFindItemInModel(const AppBarModel& model, int cmd) {
	for (const auto& g : model.groups) {
		const AppBarItem* item = AppBarFindItem(g, cmd);
		if (item) return item;
	}
	return nullptr;
}

static int AppBarCountSwatches(const AppBarModel& model) {
	int n = 0;
	for (const auto& g : model.groups) {
		for (const auto& item : g.items) {
			if (item.icon == AppBarIcon::Swatch) ++n;
		}
	}
	return n;
}

static bool AppBarGlobalOk(const AppBarModel& model, const PpDocument& doc, const char* ctx) {
	bool ok = true;
	ok = Check(!model.groups.empty(), "appbar global: model has groups") && ok;
	const AppBarGroup& global = model.groups.back();
	ok = Check(global.label == "SCALE", "appbar global: last group is SCALE") && ok;
	ok = Check(global.items.size() >= 7, "appbar global: SCALE has scale+labels+grid items") && ok;
	const int scaleCmds[] = {
		HtCmd_ScaleDay, HtCmd_ScaleWeek, HtCmd_ScaleMonth,
		HtCmd_ScaleQuarter, HtCmd_ScaleYear
	};
	const char* scaleLabels[] = { "D", "W", "M", "Q", "Y" };
	for (int i = 0; i < 5; ++i) {
		ok = Check(global.items[i].cmd == scaleCmds[i] &&
			global.items[i].label == scaleLabels[i] &&
			global.items[i].icon == AppBarIcon::ScaleSeg,
			"appbar global: scale segment order") && ok;
	}
	int activeCount = 0;
	for (int i = 0; i < 5; ++i) if (global.items[i].active) ++activeCount;
	ok = Check(activeCount == 1, "appbar global: exactly one scale segment active") && ok;
	const AppBarItem* labelsItem = AppBarFindItem(global, HtCmd_ToggleRailLabels);
	ok = Check(labelsItem != nullptr && labelsItem->label == "Labels" &&
		labelsItem->icon == AppBarIcon::LabelsToggle &&
		labelsItem->active == doc.railLabels,
		"appbar global: Labels item tracks railLabels") && ok;
	ok = Check(AppBarFindItem(global, HtCmd_CycleGrid) != nullptr, "appbar global: Grid item present") && ok;
	(void)ctx;
	return ok;
}

static bool RunAppBarModelChecks() {
	bool ok = true;

	// --- None ---
	{
		PpDocument doc;
		AppBarModel m = BuildAppBar(AppBarSel::None, doc, "");
		ok = Check(m.name.empty(), "appbar none: name empty") && ok;
		ok = Check(m.groups.size() == 2, "appbar none: INSERT + SCALE groups") && ok;
		const AppBarGroup* insert = AppBarFindGroup(m, "INSERT");
		ok = Check(insert != nullptr && insert->items.size() == 5, "appbar none: INSERT has 5 items") && ok;
		if (insert) {
			const int cmds[] = {
				HtCmd_AddRow, HtCmd_InsertTask, HtCmd_InsertMilestone,
				HtCmd_InsertMarker, HtCmd_InsertNote
			};
			for (int i = 0; i < 5; ++i) {
				ok = Check(insert->items[i].cmd == cmds[i], "appbar none: INSERT item order") && ok;
			}
		}
		ok = AppBarGlobalOk(m, doc, "none") && ok;
	}

	// --- Global scale active flags ---
	{
		PpDocument doc;
		doc.scale = "month";
		AppBarModel m = BuildAppBar(AppBarSel::None, doc, "");
		ok = Check(m.groups.back().items[2].active, "appbar scale: month -> M active") && ok;
		ok = Check(!m.groups.back().items[0].active && !m.groups.back().items[4].active,
			"appbar scale: month -> D/Q inactive") && ok;
		doc.scale = "quarter";
		m = BuildAppBar(AppBarSel::None, doc, "");
		ok = Check(m.groups.back().items[3].active, "appbar scale: quarter -> Q active") && ok;
		doc.railLabels = true;
		m = BuildAppBar(AppBarSel::None, doc, "");
		const AppBarItem* labelsItem = AppBarFindItem(m.groups.back(), HtCmd_ToggleRailLabels);
		ok = Check(labelsItem && labelsItem->active, "appbar scale: railLabels -> Labels active") && ok;
	}

	// --- Task ---
	{
		PpDocument doc;
		doc.rows.push_back(PpRow{"r1", "Row 1", "", false});
		doc.tasks.push_back(PpTask{"t1", "My Task", "2026-01-01", "2026-01-05", "r1", "", 0});
		AppBarModel m = BuildAppBar(AppBarSel::Task, doc, "t1");
		ok = Check(m.name == "My Task", "appbar task: name is task label") && ok;
		ok = Check(AppBarFindItemInModel(m, HtCmd_Edit) != nullptr, "appbar task: has Edit") && ok;
		{
			bool sawEdit = false, renameAfterEdit = false;
			for (const auto& g : m.groups) {
				for (const auto& item : g.items) {
					if (item.cmd == HtCmd_Edit) sawEdit = true;
					if (item.cmd == HtCmd_Rename) {
						ok = Check(sawEdit, "appbar task: Rename follows Edit") && ok;
						renameAfterEdit = sawEdit;
					}
				}
			}
			ok = Check(renameAfterEdit, "appbar task: has Rename after Edit") && ok;
		}
		{
			const AppBarItem* nudgeM = AppBarFindItemInModel(m, HtCmd_NudgeMinus1);
			const AppBarItem* nudgeP = AppBarFindItemInModel(m, HtCmd_NudgePlus1);
			ok = Check(nudgeM && nudgeM->label == "-1d", "appbar task: NudgeMinus1 label is -1d") && ok;
			ok = Check(nudgeP && nudgeP->label == "+1d", "appbar task: NudgePlus1 label is +1d") && ok;
		}
		ok = Check(AppBarCountSwatches(m) == 8, "appbar task: exactly 8 swatches") && ok;
		for (int i = 0; i < 8; ++i) {
			const int swatchCmds[] = {
				HtCmd_Swatch1, HtCmd_Swatch2, HtCmd_Swatch3, HtCmd_Swatch4,
				HtCmd_Swatch5, HtCmd_Swatch6, HtCmd_Swatch7, HtCmd_Swatch8
			};
			const AppBarItem* sw = AppBarFindItemInModel(m, swatchCmds[i]);
			ok = Check(sw && sw->data == kAppBarSwatches[i], "appbar task: swatch data order") && ok;
		}
		const AppBarItem* sw1 = AppBarFindItemInModel(m, HtCmd_Swatch1);
		ok = Check(sw1 && sw1->active, "appbar task: empty color -> swatch1 active") && ok;
		doc.tasks[0].color = "#7A4FA3";
		m = BuildAppBar(AppBarSel::Task, doc, "t1");
		const AppBarItem* sw3 = AppBarFindItemInModel(m, HtCmd_Swatch3);
		ok = Check(sw3 && sw3->active, "appbar task: #7A4FA3 -> swatch3 active") && ok;
		doc.tasks[0].color = "#7a4fa3";
		m = BuildAppBar(AppBarSel::Task, doc, "t1");
		sw3 = AppBarFindItemInModel(m, HtCmd_Swatch3);
		ok = Check(sw3 && sw3->active, "appbar task: #7a4fa3 lowercase -> swatch3 active") && ok;
		ok = Check(AppBarFindItemInModel(m, HtCmd_NudgeMinus1) != nullptr, "appbar task: has NudgeMinus1") && ok;
		ok = Check(AppBarFindItemInModel(m, HtCmd_NudgePlus1) != nullptr, "appbar task: has NudgePlus1") && ok;
		doc.tasks[0].labelPlacement = "";
		m = BuildAppBar(AppBarSel::Task, doc, "t1");
		const AppBarItem* lbl = AppBarFindItemInModel(m, HtCmd_CycleLabelPlacement);
		ok = Check(lbl && lbl->label == "Label: bar", "appbar task: empty placement -> Label: bar") && ok;
		doc.tasks[0].labelPlacement = "bar";
		m = BuildAppBar(AppBarSel::Task, doc, "t1");
		lbl = AppBarFindItemInModel(m, HtCmd_CycleLabelPlacement);
		ok = Check(lbl && lbl->label == "Label: bar", "appbar task: bar placement -> Label: bar") && ok;
		doc.tasks[0].labelPlacement = "rail";
		m = BuildAppBar(AppBarSel::Task, doc, "t1");
		lbl = AppBarFindItemInModel(m, HtCmd_CycleLabelPlacement);
		ok = Check(lbl && lbl->label == "Label: rail", "appbar task: rail placement -> Label: rail") && ok;
		const AppBarItem* del = AppBarFindItemInModel(m, HtCmd_Delete);
		ok = Check(del && del->danger, "appbar task: Delete is danger") && ok;
		const AppBarItem* unlink = AppBarFindItemInModel(m, HtCmd_Unlink);
		ok = Check(unlink && !unlink->enabled, "appbar task: Unlink disabled with no deps") && ok;
		doc.deps.push_back(PpDependency{"d1", "t1", "other", "finish-to-start"});
		m = BuildAppBar(AppBarSel::Task, doc, "t1");
		unlink = AppBarFindItemInModel(m, HtCmd_Unlink);
		ok = Check(unlink && unlink->enabled, "appbar task: Unlink enabled when dep touches task") && ok;
		ok = AppBarGlobalOk(m, doc, "task") && ok;
	}

	// --- Task rail-row rule ---
	{
		PpDocument doc;
		doc.rows.push_back(PpRow{"r1", "Row 1", "", false});
		doc.tasks.push_back(PpTask{"t1", "Task", "2026-01-01", "2026-01-05", "r1", "", 0});
		AppBarModel m = BuildAppBar(AppBarSel::Task, doc, "t1");
		ok = Check(AppBarFindGroup(m, "ROW") == nullptr, "appbar task rail: no ROW without rail") && ok;
		doc.railLabels = true;
		m = BuildAppBar(AppBarSel::Task, doc, "t1");
		ok = Check(AppBarFindGroup(m, "ROW") != nullptr, "appbar task rail: ROW when doc.railLabels") && ok;
		doc.railLabels = false;
		doc.tasks.push_back(PpTask{"t2", "Rail peer", "2026-01-01", "2026-01-05", "r1", "", 0, "rail"});
		m = BuildAppBar(AppBarSel::Task, doc, "t1");
		ok = Check(AppBarFindGroup(m, "ROW") != nullptr, "appbar task rail: ROW when peer has rail placement") && ok;
	}

	// --- Row ---
	{
		PpDocument doc;
		doc.rows.push_back(PpRow{"r1", "Row Label", "", false});
		AppBarModel m = BuildAppBar(AppBarSel::Row, doc, "r1");
		ok = Check(m.name == "Row Label", "appbar row: name is row label") && ok;
		const AppBarGroup* row = AppBarFindGroup(m, "ROW");
		ok = Check(row != nullptr && row->items.size() == 7, "appbar row: ROW group has 7 items") && ok;
		if (row) {
			ok = Check(row->items[0].cmd == HtCmd_AddRowAbove && row->items[0].label == "Above", "appbar row: Above") && ok;
			ok = Check(row->items[1].cmd == HtCmd_AddRowBelow && row->items[1].label == "Below", "appbar row: Below") && ok;
			ok = Check(row->items[2].cmd == HtCmd_MoveRowUp && row->items[2].label.empty(), "appbar row: MoveUp") && ok;
			ok = Check(row->items[3].cmd == HtCmd_MoveRowDown && row->items[3].label.empty(), "appbar row: MoveDown") && ok;
			ok = Check(row->items[4].cmd == HtCmd_IndentRow && row->items[4].label == "Indent", "appbar row: Indent") && ok;
			ok = Check(row->items[5].cmd == HtCmd_OutdentRow && row->items[5].label == "Outdent", "appbar row: Outdent") && ok;
			ok = Check(row->items[6].cmd == HtCmd_DeleteRow && row->items[6].danger, "appbar row: danger DeleteRow") && ok;
		}
		ok = AppBarGlobalOk(m, doc, "row") && ok;
	}

	// --- Milestone ---
	{
		PpDocument doc;
		doc.milestones.push_back(PpMilestone{"m1", "MS", "2026-01-01", "r1", ""});
		AppBarModel m = BuildAppBar(AppBarSel::Milestone, doc, "m1");
		ok = Check(AppBarFindItemInModel(m, HtCmd_Edit) != nullptr, "appbar milestone: Edit") && ok;
		{
			const AppBarItem* nudgeM = AppBarFindItemInModel(m, HtCmd_NudgeMinus1);
			const AppBarItem* nudgeP = AppBarFindItemInModel(m, HtCmd_NudgePlus1);
			ok = Check(nudgeM && nudgeM->label == "-1d", "appbar milestone: NudgeMinus1 label is -1d") && ok;
			ok = Check(nudgeP && nudgeP->label == "+1d", "appbar milestone: NudgePlus1 label is +1d") && ok;
		}
		ok = Check(AppBarFindItemInModel(m, HtCmd_NudgeMinus1) != nullptr, "appbar milestone: NudgeMinus1") && ok;
		ok = Check(AppBarFindItemInModel(m, HtCmd_NudgePlus1) != nullptr, "appbar milestone: NudgePlus1") && ok;
		ok = Check(AppBarFindItemInModel(m, HtCmd_AddNote) != nullptr, "appbar milestone: AddNote") && ok;
		const AppBarItem* del = AppBarFindItemInModel(m, HtCmd_Delete);
		ok = Check(del && del->danger, "appbar milestone: danger Delete") && ok;
		ok = Check(AppBarCountSwatches(m) == 0, "appbar milestone: no swatches") && ok;
		ok = AppBarGlobalOk(m, doc, "milestone") && ok;
	}

	// --- Marker ---
	{
		PpDocument doc;
		doc.markers.push_back(PpMarker{"mk1", "today", "Today", "2026-01-01", ""});
		AppBarModel m = BuildAppBar(AppBarSel::Marker, doc, "mk1");
		ok = Check(AppBarFindItemInModel(m, HtCmd_Rename) != nullptr, "appbar marker: Rename") && ok;
		{
			const AppBarItem* nudgeM = AppBarFindItemInModel(m, HtCmd_NudgeMinus1);
			const AppBarItem* nudgeP = AppBarFindItemInModel(m, HtCmd_NudgePlus1);
			ok = Check(nudgeM && nudgeM->label == "-1d", "appbar marker: NudgeMinus1 label is -1d") && ok;
			ok = Check(nudgeP && nudgeP->label == "+1d", "appbar marker: NudgePlus1 label is +1d") && ok;
		}
		ok = Check(AppBarFindItemInModel(m, HtCmd_NudgeMinus1) != nullptr, "appbar marker: NudgeMinus1") && ok;
		ok = Check(AppBarFindItemInModel(m, HtCmd_NudgePlus1) != nullptr, "appbar marker: NudgePlus1") && ok;
		const AppBarItem* del = AppBarFindItemInModel(m, HtCmd_Delete);
		ok = Check(del && del->danger, "appbar marker: danger Delete") && ok;
		ok = AppBarGlobalOk(m, doc, "marker") && ok;
	}

	// --- Note ---
	{
		PpDocument doc;
		doc.texts.push_back(PpText{"n1", "My Note", "", "r1", "2026-01-01", "", 0, 0});
		AppBarModel m = BuildAppBar(AppBarSel::Note, doc, "n1");
		ok = Check(m.name == "My Note", "appbar note: name from label") && ok;
		ok = Check(AppBarFindItemInModel(m, HtCmd_Edit) != nullptr, "appbar note: Edit") && ok;
		const AppBarItem* reanchor = AppBarFindItemInModel(m, HtCmd_ReanchorNote);
		ok = Check(reanchor && reanchor->label == "Re-anchor", "appbar note: Re-anchor") && ok;
		const AppBarItem* del = AppBarFindItemInModel(m, HtCmd_Delete);
		ok = Check(del && del->danger, "appbar note: danger Delete") && ok;
		doc.texts[0].label = "";
		m = BuildAppBar(AppBarSel::Note, doc, "n1");
		ok = Check(m.name == "Note", "appbar note: empty label falls back to Note") && ok;
		ok = AppBarGlobalOk(m, doc, "note") && ok;
	}

	// --- Global LAST for every selection type ---
	{
		PpDocument doc;
		doc.rows.push_back(PpRow{"r1", "R", "", false});
		doc.tasks.push_back(PpTask{"t1", "T", "2026-01-01", "2026-01-05", "r1", "", 0});
		doc.milestones.push_back(PpMilestone{"m1", "M", "2026-01-01", "r1", ""});
		doc.markers.push_back(PpMarker{"mk1", "today", "Mk", "2026-01-01", ""});
		doc.texts.push_back(PpText{"n1", "N", "", "r1", "2026-01-01", "", 0, 0});
		const AppBarSel sels[] = {
			AppBarSel::None, AppBarSel::Task, AppBarSel::Row,
			AppBarSel::Milestone, AppBarSel::Marker, AppBarSel::Note
		};
		const char* ids[] = { "", "t1", "r1", "m1", "mk1", "n1" };
		for (int i = 0; i < 6; ++i) {
			AppBarModel m = BuildAppBar(sels[i], doc, ids[i]);
			ok = Check(m.groups.back().label == "SCALE", "appbar global: SCALE last for every sel") && ok;
		}
	}

	return ok;
}

// S3 s3-row-ops: pure row hierarchy operations (add above/below, move
// up/down, indent/outdent, delete-with-reparent). Print 'ROW OPS OK' when
// these pass.
static PpDocument RowOpsDoc() {
	PpDocument d;
	d.rows.push_back(PpRow{"r1", "Row 1", "", false});
	d.rows.push_back(PpRow{"r2", "Row 2", "r1", false}); // child of r1
	d.rows.push_back(PpRow{"r3", "Row 3", "", false});
	d.rows.push_back(PpRow{"r4", "Row 4", "", false});
	return d;
}
static int RowPos(const PpDocument& doc, const std::string& rowId) {
	for (size_t i = 0; i < doc.rows.size(); ++i) if (doc.rows[i].id == rowId) return (int)i;
	return -1;
}

static bool RunRowOpsChecks() {
	bool ok = true;

	// --- AddRowAbove ---
	{
		PpDocument d = RowOpsDoc(); // r1(0,top) r2(1,child r1) r3(2,top) r4(3,top)
		std::string above = AddRowAbove(d, "r3", "Above r3");
		ok = Check(!above.empty(), "AddRowAbove returns a fresh id") && ok;
		int aboveIdx = RowPos(d, above);
		ok = Check(aboveIdx == RowPos(d, "r3") - 1, "AddRowAbove inserts immediately before the ref row") && ok;
		ok = Check(aboveIdx >= 0 && d.rows[aboveIdx].groupId.empty(), "AddRowAbove inherits the (top-level) ref's groupId") && ok;
		ok = Check(AddRowAbove(d, "missing-row", "Nope").empty(), "AddRowAbove returns \"\" for a missing ref") && ok;
	}

	// --- AddRowBelow ---
	{
		PpDocument d = RowOpsDoc(); // r1(0,top) r2(1,child r1) r3(2,top) r4(3,top)
		std::string below = AddRowBelow(d, "r1", "Below r1");
		ok = Check(!below.empty(), "AddRowBelow returns a fresh id") && ok;
		int belowIdx = RowPos(d, below);
		ok = Check(belowIdx == 2, "AddRowBelow (parent ref) lands after ref's whole child block") && ok;
		ok = Check(belowIdx >= 0 && d.rows[belowIdx].groupId.empty(), "AddRowBelow inherits the ref's groupId") && ok;
		ok = Check(RowPos(d, "r3") == 3, "AddRowBelow pushes rows after the child block down") && ok;

		std::string belowChild = AddRowBelow(d, "r2", "Below r2");
		int belowChildIdx = RowPos(d, belowChild);
		ok = Check(belowChildIdx == RowPos(d, "r2") + 1, "AddRowBelow (childless ref) lands immediately after ref") && ok;
		ok = Check(belowChildIdx >= 0 && d.rows[belowChildIdx].groupId == "r1", "AddRowBelow inherits a child ref's groupId") && ok;

		ok = Check(AddRowBelow(d, "missing-row", "Nope").empty(), "AddRowBelow returns \"\" for a missing ref") && ok;
	}

	// --- MoveRowUp / MoveRowDown ---
	{
		PpDocument d = RowOpsDoc(); // r1(0) r2(1) r3(2) r4(3)
		ok = Check(MoveRowUp(d, "r3") && d.rows[1].id == "r3" && d.rows[2].id == "r2", "MoveRowUp swaps with the previous row") && ok;
		ok = Check(!MoveRowUp(d, "r1"), "MoveRowUp refuses at the top edge") && ok;
		ok = Check(!MoveRowUp(d, "missing-row"), "MoveRowUp returns false for a missing row") && ok;

		ok = Check(MoveRowDown(d, "r1") && d.rows[0].id == "r3" && d.rows[1].id == "r1", "MoveRowDown swaps with the next row") && ok;
		ok = Check(!MoveRowDown(d, "r4"), "MoveRowDown refuses at the bottom edge") && ok;
		ok = Check(!MoveRowDown(d, "missing-row"), "MoveRowDown returns false for a missing row") && ok;
	}

	// --- IndentRow: plain row indents under the nearest preceding top-level row ---
	{
		PpDocument d = RowOpsDoc(); // r1(0,top) r2(1,child r1) r3(2,top) r4(3,top)
		ok = Check(IndentRow(d, "r3"), "IndentRow succeeds for a plain row with a preceding top-level row") && ok;
		int idx = RowPos(d, "r3");
		ok = Check(idx == 2 && d.rows[idx].groupId == "r1", "IndentRow re-parents to the nearest preceding top-level row (skipping child r2) without moving it") && ok;
	}

	// --- IndentRow: a row WITH children refuses ---
	{
		PpDocument d = RowOpsDoc(); // r1 already has child r2
		ok = Check(!IndentRow(d, "r1"), "IndentRow refuses a row that already has children") && ok;
		ok = Check(d.rows[RowPos(d, "r1")].groupId.empty(), "IndentRow leaves a refused row's groupId unchanged") && ok;
	}

	// --- IndentRow: no preceding row refuses ---
	{
		PpDocument d;
		d.rows.push_back(PpRow{"only1", "Only 1", "", false});
		d.rows.push_back(PpRow{"only2", "Only 2", "", false});
		ok = Check(!IndentRow(d, "only1"), "IndentRow refuses the first row (no preceding row exists)") && ok;
	}

	// --- IndentRow: already that parent's child is a no-op ---
	{
		PpDocument d = RowOpsDoc(); // r2's groupId is already "r1", the nearest preceding top-level row
		ok = Check(!IndentRow(d, "r2"), "IndentRow is a no-op (false) when already the nearest top-level row's child") && ok;
		ok = Check(d.rows[RowPos(d, "r2")].groupId == "r1", "IndentRow no-op leaves groupId unchanged") && ok;
	}

	// --- IndentRow: missing row refuses ---
	{
		PpDocument d = RowOpsDoc();
		ok = Check(!IndentRow(d, "missing-row"), "IndentRow returns false for a missing row") && ok;
	}

	// --- OutdentRow ---
	{
		PpDocument d = RowOpsDoc();
		ok = Check(OutdentRow(d, "r2"), "OutdentRow promotes a child to top-level") && ok;
		ok = Check(d.rows[RowPos(d, "r2")].groupId.empty(), "OutdentRow clears the promoted row's groupId") && ok;
		ok = Check(!OutdentRow(d, "r1"), "OutdentRow refuses an already top-level row") && ok;
		ok = Check(!OutdentRow(d, "missing-row"), "OutdentRow returns false for a missing row") && ok;
	}

	// --- DeleteRow ---
	{
		PpDocument d = RowOpsDoc();
		d.tasks.push_back(PpTask{"t1", "Task on r1", "2026-01-01", "2026-01-05", "r1", "", 0});
		d.milestones.push_back(PpMilestone{"m1", "MS on r1", "2026-01-01", "r1", ""});

		ok = Check(DeleteRow(d, "r1"), "DeleteRow returns true for an existing parent row") && ok;
		ok = Check(RowPos(d, "r1") < 0, "DeleteRow removes the parent row itself") && ok;
		int r2idx = RowPos(d, "r2");
		ok = Check(r2idx >= 0 && d.rows[r2idx].groupId.empty(), "DeleteRow re-parents a surviving child row to top-level") && ok;
		bool taskGone = true; for (const auto& t : d.tasks) if (t.id == "t1") taskGone = false;
		ok = Check(taskGone, "DeleteRow cascades away the parent's own tasks") && ok;
		bool msGone = true; for (const auto& m : d.milestones) if (m.id == "m1") msGone = false;
		ok = Check(msGone, "DeleteRow cascades away the parent's own milestones") && ok;
		ok = Check(!DeleteRow(d, "missing-row"), "DeleteRow returns false for a non-row id") && ok;
	}

	return ok;
}

// S5 s5-dep-ops: pure dependency operations (AddDependency with
// self/dup/missing-endpoint/bad-type rejection, RemoveDependenciesTouching).
// Print 'DEP OPS OK' when these pass.
static PpDocument DepOpsDoc() {
	PpDocument d;
	d.rows.push_back(PpRow{"r1", "Row 1", "", false});
	d.tasks.push_back(PpTask{"t1", "Task 1", "2026-01-01", "2026-01-05", "r1", "", 0});
	d.tasks.push_back(PpTask{"t2", "Task 2", "2026-01-06", "2026-01-10", "r1", "", 0});
	d.tasks.push_back(PpTask{"t3", "Task 3", "2026-01-11", "2026-01-15", "r1", "", 0});
	d.milestones.push_back(PpMilestone{"m1", "MS 1", "2026-01-20", "r1", ""});
	return d;
}
static bool DepPresent(const PpDocument& d, const std::string& id,
	const std::string& from, const std::string& to, const std::string& type) {
	for (const auto& dep : d.deps) {
		if (dep.id == id) return dep.from == from && dep.to == to && dep.type == type;
	}
	return false;
}

static bool RunDepOpsChecks() {
	bool ok = true;

	// --- AddDependency: task -> task, type defaulted ---
	{
		PpDocument d = DepOpsDoc();
		std::string id = AddDependency(d, "t1", "t2");
		ok = Check(!id.empty(), "AddDependency task->task returns a fresh id") && ok;
		ok = Check(d.deps.size() == 1 && DepPresent(d, id, "t1", "t2", "finish-to-start"), "AddDependency defaults type to finish-to-start and appends the dep") && ok;

		// Empty type string normalizes to the default too.
		std::string id2 = AddDependency(d, "t2", "t3", "");
		ok = Check(!id2.empty() && DepPresent(d, id2, "t2", "t3", "finish-to-start"), "AddDependency normalizes empty type to finish-to-start") && ok;
	}

	// --- AddDependency: task -> milestone (and explicit non-default type) ---
	{
		PpDocument d = DepOpsDoc();
		std::string id = AddDependency(d, "t1", "m1", "start-to-start");
		ok = Check(!id.empty() && DepPresent(d, id, "t1", "m1", "start-to-start"), "AddDependency accepts a milestone endpoint and an explicit type") && ok;
	}

	// --- AddDependency: reject self ---
	{
		PpDocument d = DepOpsDoc();
		ok = Check(AddDependency(d, "t1", "t1").empty() && d.deps.empty(), "AddDependency rejects a self edge") && ok;
	}

	// --- AddDependency: reject duplicate (from, to) pair even with another type ---
	{
		PpDocument d = DepOpsDoc();
		ok = Check(!AddDependency(d, "t1", "t2").empty(), "AddDependency first edge of a pair succeeds") && ok;
		ok = Check(AddDependency(d, "t1", "t2", "finish-to-finish").empty(), "AddDependency rejects a duplicate pair even with a different type") && ok;
		ok = Check(d.deps.size() == 1, "AddDependency duplicate rejection leaves deps unchanged") && ok;
		// The reverse direction is a DIFFERENT pair and stays allowed.
		ok = Check(!AddDependency(d, "t2", "t1").empty(), "AddDependency allows the reverse-direction pair") && ok;
	}

	// --- AddDependency: reject missing endpoints (both directions) ---
	{
		PpDocument d = DepOpsDoc();
		ok = Check(AddDependency(d, "missing", "t2").empty(), "AddDependency rejects a missing FROM endpoint") && ok;
		ok = Check(AddDependency(d, "t1", "missing").empty(), "AddDependency rejects a missing TO endpoint") && ok;
		// A row id is not a valid endpoint kind either.
		ok = Check(AddDependency(d, "r1", "t2").empty(), "AddDependency rejects a row id as an endpoint") && ok;
		ok = Check(d.deps.empty(), "AddDependency endpoint rejections leave deps unchanged") && ok;
	}

	// --- AddDependency: reject a bad type string ---
	{
		PpDocument d = DepOpsDoc();
		ok = Check(AddDependency(d, "t1", "t2", "nonsense").empty() && d.deps.empty(), "AddDependency rejects an unknown type string") && ok;
	}

	// --- AddDependency: generated ids unique against existing dep ids ---
	{
		PpDocument d = DepOpsDoc();
		d.deps.push_back(PpDependency{"dep1", "t1", "t2", "finish-to-start"});
		std::string id = AddDependency(d, "t2", "t3");
		ok = Check(!id.empty() && id != "dep1", "AddDependency generates an id unique against existing dep ids") && ok;
	}

	// --- RemoveDependenciesTouching ---
	{
		PpDocument d = DepOpsDoc();
		std::string a = AddDependency(d, "t1", "t2");           // touches t2
		std::string b = AddDependency(d, "t2", "t3");           // touches t2
		std::string c = AddDependency(d, "t3", "m1");           // does NOT touch t2
		ok = Check(!a.empty() && !b.empty() && !c.empty() && d.deps.size() == 3, "RemoveDependenciesTouching setup adds three deps") && ok;

		ok = Check(RemoveDependenciesTouching(d, "t2") == 2, "RemoveDependenciesTouching returns the removed count") && ok;
		ok = Check(d.deps.size() == 1 && d.deps[0].id == c, "RemoveDependenciesTouching keeps untouched deps") && ok;

		ok = Check(RemoveDependenciesTouching(d, "t2") == 0, "RemoveDependenciesTouching returns 0 when nothing touches the id") && ok;
		ok = Check(d.deps.size() == 1, "RemoveDependenciesTouching no-op leaves deps unchanged") && ok;
	}

	// --- DeleteById: milestone removal clears deps touching the milestone ---
	{
		PpDocument d = DepOpsDoc();
		std::string depId = AddDependency(d, "t1", "m1");
		ok = Check(!depId.empty() && d.deps.size() == 1, "DeleteById milestone setup: dep t1->m1") && ok;
		ok = Check(DeleteById(d, "m1") && d.milestones.empty(), "DeleteById removes milestone m1") && ok;
		ok = Check(d.deps.empty(), "DeleteById milestone branch removes deps touching the milestone") && ok;
	}

	// --- DeleteById: row cascade removes deps touching the row's milestones ---
	{
		PpDocument d = DepOpsDoc();
		std::string depId = AddDependency(d, "t1", "m1");
		ok = Check(!depId.empty(), "DeleteById row-cascade setup: dep t1->m1") && ok;
		ok = Check(DeleteById(d, "r1") && d.rows.empty() && d.milestones.empty(), "DeleteById row cascade removes row + milestones") && ok;
		ok = Check(d.deps.empty(), "DeleteById row cascade removes deps touching row milestones") && ok;
	}

	return ok;
}

static bool RunRowAppBarMapChecks() {
	bool ok = true;
	{
		HtMenuOp op = MapRowAppBarCommand(HtCmd_AddRowAbove);
		ok = Check(op.opKind == HtOpKind::AddRowAbove && op.needsRowId, "map: row appbar Above -> AddRowAbove") && ok;
	}
	{
		HtMenuOp op = MapRowAppBarCommand(HtCmd_AddRowBelow);
		ok = Check(op.opKind == HtOpKind::AddRow && op.needsRowId, "map: row appbar Below -> AddRow(needsRowId)") && ok;
	}
	{
		HtMenuOp op = MapRowAppBarCommand(HtCmd_MoveRowDown);
		ok = Check(op.opKind == HtOpKind::MoveRowDown && op.needsRowId, "map: row appbar MoveDown -> MoveRowDown") && ok;
	}
	{
		HtMenuOp op = MapRowAppBarCommand(HtCmd_IndentRow);
		ok = Check(op.opKind == HtOpKind::IndentRow && op.needsRowId, "map: row appbar Indent -> IndentRow") && ok;
	}
	{
		HtMenuOp op = MapRowAppBarCommand(HtCmd_DeleteRow);
		ok = Check(op.opKind == HtOpKind::DeleteRow && op.needsRowId, "map: row appbar DeleteRow -> DeleteRow") && ok;
	}
	{
		HtMenuOp op = MapRowAppBarCommand(HtCmd_Rename);
		ok = Check(op.opKind == HtOpKind::RenameRow && op.needsRowId, "map: row appbar Rename -> RenameRow") && ok;
	}
	{
		HtMenuOp op = MapRowAppBarCommand(HtCmd_ScaleWeek);
		ok = Check(op.opKind == HtOpKind::None, "map: row appbar ScaleWeek -> None") && ok;
	}
	return ok;
}

static bool RunTaskAppBarMapChecks() {
	bool ok = true;
	{
		HtMenuOp op = MapTaskAppBarCommand(HtCmd_Swatch3);
		ok = Check(op.opKind == HtOpKind::SetTaskColor && op.needsTaskId
			&& op.color && std::strcmp(op.color, "#7A4FA3") == 0,
			"map: task appbar Swatch3 -> SetTaskColor #7A4FA3") && ok;
	}
	{
		HtMenuOp op = MapTaskAppBarCommand(HtCmd_Swatch1);
		ok = Check(op.opKind == HtOpKind::SetTaskColor && op.color
			&& std::strcmp(op.color, "#4355E0") == 0,
			"map: task appbar Swatch1 -> SetTaskColor #4355E0") && ok;
	}
	{
		HtMenuOp op = MapTaskAppBarCommand(HtCmd_CycleLabelPlacement);
		ok = Check(op.opKind == HtOpKind::CycleLabelPlacement && op.needsTaskId,
			"map: task appbar Label -> CycleLabelPlacement") && ok;
	}
	{
		HtMenuOp op = MapTaskAppBarCommand(HtCmd_AddNote);
		ok = Check(op.opKind == HtOpKind::AddNote && op.needsTaskId,
			"map: task appbar Note -> AddNote") && ok;
	}
	{
		HtMenuOp op = MapTaskAppBarCommand(HtCmd_Edit);
		ok = Check(op.opKind == HtOpKind::Edit && op.needsTaskId,
			"map: task appbar Edit -> Edit") && ok;
	}
	{
		HtMenuOp op = MapTaskAppBarCommand(HtCmd_Delete);
		ok = Check(op.opKind == HtOpKind::Delete && op.needsTaskId,
			"map: task appbar Delete -> Delete") && ok;
	}
	{
		HtMenuOp op = MapTaskAppBarCommand(HtCmd_NudgePlus1);
		ok = Check(op.opKind == HtOpKind::Nudge && op.needsTaskId && op.nudgeDays == 1,
			"map: task appbar +1d -> Nudge +1") && ok;
	}
	{
		HtMenuOp op = MapTaskAppBarCommand(HtCmd_Link);
		ok = Check(op.opKind == HtOpKind::EnterLinkMode && op.needsTaskId,
			"map: task appbar Link -> EnterLinkMode") && ok;
	}
	{
		HtMenuOp op = MapTaskAppBarCommand(HtCmd_Unlink);
		ok = Check(op.opKind == HtOpKind::Unlink && op.needsTaskId,
			"map: task appbar Unlink -> Unlink") && ok;
	}
	{
		HtMenuOp op = MapBackgroundAppBarCommand(HtCmd_InsertNote);
		ok = Check(op.opKind == HtOpKind::InsertFreeNote,
			"map: background appbar Note -> InsertFreeNote") && ok;
	}
	{
		HtMenuOp op = MapTaskAppBarCommand(HtCmd_ScaleWeek);
		ok = Check(op.opKind == HtOpKind::None, "map: task appbar ScaleWeek -> None") && ok;
	}
	{
		HtMenuOp op = MapMilestoneAppBarCommand(HtCmd_NudgeMinus1);
		ok = Check(op.opKind == HtOpKind::Nudge && op.needsTaskId && op.nudgeDays == -1,
			"map: milestone appbar -1d -> Nudge -1") && ok;
	}
	{
		HtMenuOp op = MapMilestoneAppBarCommand(HtCmd_AddNote);
		ok = Check(op.opKind == HtOpKind::AddNote && op.needsTaskId,
			"map: milestone appbar Note -> AddNote") && ok;
	}
	{
		HtMenuOp op = MapMilestoneAppBarCommand(HtCmd_Rename);
		ok = Check(op.opKind == HtOpKind::Edit && op.needsTaskId,
			"map: milestone appbar Rename -> Edit") && ok;
	}
	{
		HtMenuOp op = MapMarkerAppBarCommand(HtCmd_Rename);
		ok = Check(op.opKind == HtOpKind::Edit && op.needsTaskId,
			"map: marker appbar Rename -> Edit") && ok;
	}
	{
		HtMenuOp op = MapMarkerAppBarCommand(HtCmd_Delete);
		ok = Check(op.opKind == HtOpKind::Delete && op.needsTaskId,
			"map: marker appbar Delete -> Delete") && ok;
	}
	{
		HtMenuOp op = MapMarkerAppBarCommand(HtCmd_NudgePlus1);
		ok = Check(op.opKind == HtOpKind::Nudge && op.needsTaskId && op.nudgeDays == 1,
			"map: marker appbar +1d -> Nudge +1") && ok;
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
	bool menuOk = RunMenuModelV4Checks();
	ok = menuOk && ok;
	bool cursorOk = RunCursorMapChecks();
	ok = cursorOk && ok;
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

	ok = Check(SetTaskPercentValue(doc, taskId, 42) && doc.tasks.back().percent == 42, "SetTaskPercentValue sets an absolute value") && ok;
	ok = Check(SetTaskPercentValue(doc, taskId, 250) && doc.tasks.back().percent == 100, "SetTaskPercentValue clamps high values") && ok;
	ok = Check(SetTaskPercentValue(doc, taskId, -50) && doc.tasks.back().percent == 0, "SetTaskPercentValue clamps low values") && ok;
	ok = Check(!SetTaskPercentValue(doc, "missing-task", 50), "SetTaskPercentValue returns false for missing task") && ok;

	ok = Check(SetTaskColor(doc, taskId, "#ff0000") && doc.tasks.back().color == "#ff0000", "SetTaskColor sets task color") && ok;
	ok = Check(SetTaskColor(doc, taskId, "") && doc.tasks.back().color.empty(), "SetTaskColor accepts empty string to clear color") && ok;
	ok = Check(!SetTaskColor(doc, "missing-task", "#00ff00"), "SetTaskColor returns false for missing task") && ok;
	ok = Check(!SetTaskColor(doc, "ms-1", "#00ff00"), "SetTaskColor returns false for a non-task id (milestone)") && ok;

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

	bool markerOpsOk = RunMarkerOpsChecks();
	ok = markerOpsOk && ok;

	bool textOpsOk = RunTextOpsChecks();
	ok = textOpsOk && ok;

	bool themeOk = RunThemeTokenChecks();
	ok = themeOk && ok;

	bool labelOpsOk = RunLabelOpsChecks();
	ok = labelOpsOk && ok;

	bool sceneVizOk = RunSceneVizChecks();
	ok = sceneVizOk && ok;

	bool gridOpsOk = RunGridOpsChecks();
	ok = gridOpsOk && ok;

	bool appBarOk = RunAppBarModelChecks();
	ok = appBarOk && ok;

	bool rowOpsOk = RunRowOpsChecks();
	ok = rowOpsOk && ok;

	bool rowAppBarMapOk = RunRowAppBarMapChecks();
	ok = rowAppBarMapOk && ok;

	bool taskAppBarMapOk = RunTaskAppBarMapChecks();
	ok = taskAppBarMapOk && ok;

	bool depOpsOk = RunDepOpsChecks();
	ok = depOpsOk && ok;

	if (!ok) {
		std::printf("OPS HARNESS FAIL\n");
		return 1;
	}

	std::printf("OPS HARNESS OK\n");
	if (dpiOk) {
		std::printf("DPI HELPER OK\n");
	}
	if (menuOk) {
		std::printf("MENU MAP V4 OK\n");
	}
	if (cursorOk) {
		std::printf("CURSOR MAP OK\n");
	}
	if (markerOpsOk) {
		std::printf("MARKER OPS OK\n");
	}
	if (textOpsOk) {
		std::printf("TEXT OPS OK\n");
	}
	if (themeOk) {
		std::printf("THEME TOKENS OK\n");
	}
	if (labelOpsOk) {
		std::printf("LABEL OPS OK\n");
	}
	if (sceneVizOk) {
		std::printf("SCENE VIZ OK\n");
	}
	if (gridOpsOk) {
		std::printf("GRID OPS OK\n");
	}
	if (appBarOk) {
		std::printf("APPBAR MODEL OK\n");
		std::printf("CREATION MODEL OK\n");
	}
	if (rowOpsOk) {
		std::printf("ROW OPS OK\n");
	}
	if (rowAppBarMapOk) {
		std::printf("ROW APPBAR MAP OK\n");
	}
	if (taskAppBarMapOk) {
		std::printf("TASKCTX MAP OK\n");
	}
	if (depOpsOk) {
		std::printf("DEP OPS OK\n");
	}
	return 0;
}
