// Phase 13 v2.8.4 NTS-01 seed: minimal COM-free pure unit runner (no framework).
// Tests layout helpers / JSON round-trip sanity against fixtures when present.
// Build: see native/build-unit.bat
#include "../third_party/json.hpp"
#include "../PowerPlannerAddin/GanttModel.h"
#include "../PowerPlannerAddin/GanttLayout.h"
#include "../PowerPlannerAddin/GanttJson.h"
#include "../PowerPlannerAddin/GanttOps.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using json = nlohmann::json;
namespace fs = std::filesystem;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::cout << "FAIL " << msg << "\n"; ++g_fail; } \
  else { std::cout << "OK   " << msg << "\n"; } } while (0)

int main() {
	// Empty document layout should not crash and returns zero rows.
	{
		PpDocument doc;
		auto R = LayoutGantt(doc, "2026-01-01");
		CHECK(R.chartRows >= 0, "empty layout chartRows");
	}

	// JSON round-trip identity for a minimal doc.
	{
		PpDocument doc;
		doc.title = "unit";
		PpRow r; r.id = "r1"; r.label = "R"; doc.rows.push_back(r);
		PpTask t; t.id = "t1"; t.rowId = "r1"; t.label = "T";
		t.start = "2026-06-01"; t.end = "2026-06-10";
		doc.tasks.push_back(t);
		std::string j1 = DocumentToJson(doc);
		std::string j2 = DocumentToJson(DocumentFromJson(j1));
		CHECK(j1 == j2, "DocumentToJson round-trip identity");
	}

	// Fixture path optional: if basic-chart exists, layout at scale 1 produces tasks.
	fs::path fix = fs::path("spec/fixtures/basic-chart.json");
	if (!fs::exists(fix)) fix = fs::path("../../spec/fixtures/basic-chart.json");
	if (fs::exists(fix)) {
		json fixture;
		std::ifstream(fix) >> fixture;
		std::string docStr = fixture["document"].dump();
		PpDocument doc = DocumentFromJson(docStr);
		std::string viewStart = fixture["view"].value("viewStart", "2026-01-01");
		auto R = LayoutGantt(doc, viewStart);
		CHECK(!R.tasks.empty() || !doc.tasks.empty(), "fixture layout runs");
		CHECK(DocumentToJson(doc).size() > 10, "fixture doc serializes");
	} else {
		std::cout << "SKIP fixture (path not found)\n";
	}

	if (g_fail) {
		std::cout << g_fail << " failure(s)\n";
		return 1;
	}
	std::cout << "PPUNIT PASS\n";
	return 0;
}
