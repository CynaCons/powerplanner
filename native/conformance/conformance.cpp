// Native conformance harness (SRS-PPT-011 / SRS-LAY-001).
//
// Loads each spec/fixtures/<name>.json, runs the C++ GanttLayout at scale = 1,
// and compares the abstract result to spec/fixtures/<name>.expected.json — the
// same golden the web engine is tested against. Exit 0 iff every fixture matches.
//
//   conformance.exe <spec/fixtures dir>
#include "../third_party/json.hpp"
#include "../PowerPlannerAddin/GanttModel.h"
#include "../PowerPlannerAddin/GanttLayout.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using json = nlohmann::json;
namespace fs = std::filesystem;

static std::string getStr(const json& j, const char* key) {
	auto it = j.find(key);
	if (it == j.end() || !it->is_string()) return "";
	return it->get<std::string>();
}

static PpDocument ParseDocument(const json& d) {
	PpDocument doc;
	doc.title = getStr(d, "title");
	for (const auto& r : d.value("rows", json::array())) {
		PpRow row;
		row.id = getStr(r, "id");
		row.label = getStr(r, "label");
		row.groupId = getStr(r, "groupId");  // null/absent -> ""
		row.collapsed = r.value("collapsed", false);
		doc.rows.push_back(row);
	}
	for (const auto& t : d.value("tasks", json::array())) {
		PpTask task;
		task.id = getStr(t, "id");
		task.rowId = getStr(t, "rowId");
		task.label = getStr(t, "label");
		task.start = getStr(t, "start");
		task.end = getStr(t, "end");
		task.color = getStr(t, "color");
		task.percent = t.value("percentComplete", 0);
		doc.tasks.push_back(task);
	}
	for (const auto& m : d.value("milestones", json::array())) {
		PpMilestone ms;
		ms.id = getStr(m, "id");
		ms.rowId = getStr(m, "rowId");
		ms.label = getStr(m, "label");
		ms.date = getStr(m, "date");
		ms.color = getStr(m, "color");
		doc.milestones.push_back(ms);
	}
	for (const auto& b : d.value("brackets", json::array())) {
		PpBracket br;
		br.id = getStr(b, "id");
		br.label = getStr(b, "label");
		br.start = getStr(b, "start");
		br.end = getStr(b, "end");
		br.color = getStr(b, "color");
		for (const auto& rid : b.value("rowIds", json::array())) br.rowIds.push_back(rid.get<std::string>());
		doc.brackets.push_back(br);
	}
	for (const auto& dp : d.value("dependencies", json::array())) {
		PpDependency dep;
		dep.id = getStr(dp, "id");
		dep.from = getStr(dp, "from");
		dep.to = getStr(dp, "to");
		dep.type = getStr(dp, "type");
		doc.deps.push_back(dep);
	}
	return doc;
}

static json ToAbstractJson(const GanttLayoutResult& R) {
	json j;
	j["visibleRowIds"] = R.visibleRowIds;
	j["rowSlots"] = R.rowSlots;
	j["rowOffsets"] = R.rowOffsets;
	j["chartRows"] = R.chartRows;
	j["tasks"] = json::array();
	for (const auto& t : R.tasks)
		j["tasks"].push_back({ {"id", t.id}, {"rowIndex", t.rowIndex}, {"subRow", t.subRow}, {"xDay", t.xDay}, {"widthDays", t.widthDays} });
	j["milestones"] = json::array();
	for (const auto& m : R.milestones)
		j["milestones"].push_back({ {"id", m.id}, {"rowIndex", m.rowIndex}, {"xDay", m.xDay} });
	j["summaries"] = json::array();
	for (const auto& s : R.summaries)
		j["summaries"].push_back({ {"rowId", s.rowId}, {"rowIndex", s.rowIndex}, {"xDay", s.xDay}, {"widthDays", s.widthDays} });
	j["brackets"] = json::array();
	for (const auto& b : R.brackets)
		j["brackets"].push_back({ {"id", b.id}, {"xDay", b.xDay}, {"widthDays", b.widthDays}, {"topRow", b.topRow}, {"bottomRow", b.bottomRow} });
	j["dependencies"] = json::array();
	for (const auto& d : R.dependencies)
		j["dependencies"].push_back({ {"id", d.id}, {"fromXDay", d.fromXDay}, {"toXDay", d.toXDay} });
	return j;
}

int main(int argc, char** argv) {
	fs::path dir = (argc > 1) ? fs::path(argv[1]) : fs::path("../../spec/fixtures");
	if (!fs::exists(dir)) { std::cerr << "fixtures dir not found: " << dir << "\n"; return 2; }

	int total = 0, passed = 0;
	for (const auto& entry : fs::directory_iterator(dir)) {
		const auto p = entry.path();
		if (p.extension() != ".json") continue;
		std::string name = p.filename().string();
		if (name.size() > 14 && name.substr(name.size() - 14) == ".expected.json") continue;

		fs::path expectedPath = p; expectedPath.replace_extension();  // strip .json
		expectedPath += ".expected.json";
		if (!fs::exists(expectedPath)) continue;

		++total;
		json fixture, expected;
		try {
			std::ifstream(p) >> fixture;
			std::ifstream(expectedPath) >> expected;
		} catch (const std::exception& e) {
			std::cout << "[FAIL] " << name << " — parse error: " << e.what() << "\n";
			continue;
		}
		expected.erase("_note");

		PpDocument doc = ParseDocument(fixture["document"]);
		std::string viewStart = fixture["view"].value("viewStart", "2026-01-01");
		json got = ToAbstractJson(LayoutGantt(doc, viewStart));

		if (got == expected) {
			std::cout << "[PASS] " << name << "\n";
			++passed;
		} else {
			std::cout << "[FAIL] " << name << "\n  expected: " << expected.dump() << "\n  got:      " << got.dump() << "\n";
		}
	}

	std::cout << "\n" << passed << "/" << total << " fixtures passed\n";
	return (total > 0 && passed == total) ? 0 : 1;
}
