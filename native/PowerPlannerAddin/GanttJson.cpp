#include "GanttJson.h"
#include "../third_party/json.hpp"

using json = nlohmann::json;

static std::string getStr(const json& j, const char* key) {
	auto it = j.find(key);
	if (it == j.end() || !it->is_string()) return "";
	return it->get<std::string>();
}

std::string DocumentToJson(const PpDocument& d) {
	json j;
	j["schemaVersion"] = 1;
	j["title"] = d.title;
	j["calendar"] = { {"scale", d.scale.empty() ? "week" : d.scale}, {"fiscalYearStart", 1}, {"workingDays", {1, 2, 3, 4, 5}}, {"holidays", json::array()} };
	if (d.railLabels) j["railLabels"] = true;
	if (!d.gridDensity.empty()) j["gridDensity"] = d.gridDensity;
	if (!d.gridStyle.empty()) j["gridStyle"] = d.gridStyle;

	j["rows"] = json::array();
	for (const auto& r : d.rows) {
		json rr = { {"id", r.id}, {"label", r.label} };
		rr["groupId"] = r.groupId.empty() ? json(nullptr) : json(r.groupId);
		if (r.collapsed) rr["collapsed"] = true;
		j["rows"].push_back(rr);
	}
	j["tasks"] = json::array();
	for (const auto& t : d.tasks) {
		json tt = { {"id", t.id}, {"rowId", t.rowId}, {"label", t.label}, {"start", t.start}, {"end", t.end}, {"percentComplete", t.percent} };
		if (!t.color.empty()) tt["color"] = t.color;
		if (!t.labelPlacement.empty()) tt["labelPlacement"] = t.labelPlacement;
		j["tasks"].push_back(tt);
	}
	j["milestones"] = json::array();
	for (const auto& m : d.milestones) {
		json mm = { {"id", m.id}, {"rowId", m.rowId}, {"label", m.label}, {"date", m.date} };
		if (!m.color.empty()) mm["color"] = m.color;
		j["milestones"].push_back(mm);
	}
	j["brackets"] = json::array();
	for (const auto& b : d.brackets) {
		json bb = { {"id", b.id}, {"label", b.label}, {"start", b.start}, {"end", b.end}, {"rowIds", b.rowIds} };
		if (!b.color.empty()) bb["color"] = b.color;
		j["brackets"].push_back(bb);
	}
	j["dependencies"] = json::array();
	for (const auto& dp : d.deps)
		j["dependencies"].push_back({ {"id", dp.id}, {"from", dp.from}, {"to", dp.to}, {"type", dp.type} });
	j["markers"] = json::array();
	for (const auto& m : d.markers) {
		json mm = { {"id", m.id}, {"type", m.type}, {"label", m.label}, {"date", m.date} };
		if (!m.color.empty()) mm["color"] = m.color;
		j["markers"].push_back(mm);
	}
	if (!d.texts.empty()) {
		j["texts"] = json::array();
		for (const auto& t : d.texts) {
			json tt = { {"id", t.id}, {"label", t.label}, {"dx", t.dx}, {"dy", t.dy} };
			if (!t.anchorId.empty()) tt["anchorId"] = t.anchorId;
			if (!t.rowId.empty()) tt["rowId"] = t.rowId;
			if (!t.date.empty()) tt["date"] = t.date;
			if (!t.color.empty()) tt["color"] = t.color;
			j["texts"].push_back(tt);
		}
	}
	j["style"] = { {"theme", "light"}, {"preset", "default"} };
	return j.dump();
}

PpDocument DocumentFromJson(const std::string& jsonStr) {
	PpDocument doc;
	json d = json::parse(jsonStr);
	doc.title = getStr(d, "title");
	doc.scale = "week";
	auto cal = d.find("calendar");
	if (cal != d.end() && cal->is_object()) {
		std::string scale = getStr(*cal, "scale");
		if (!scale.empty()) doc.scale = scale;
	}
	doc.railLabels = d.value("railLabels", false);
	doc.gridDensity = getStr(d, "gridDensity");
	doc.gridStyle = getStr(d, "gridStyle");
	for (const auto& r : d.value("rows", json::array())) {
		PpRow row;
		row.id = getStr(r, "id");
		row.label = getStr(r, "label");
		row.groupId = getStr(r, "groupId");
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
		task.labelPlacement = getStr(t, "labelPlacement");
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
	for (const auto& m : d.value("markers", json::array())) {
		PpMarker marker;
		marker.id = getStr(m, "id");
		marker.type = getStr(m, "type");
		marker.label = getStr(m, "label");
		marker.date = getStr(m, "date");
		marker.color = getStr(m, "color");
		doc.markers.push_back(marker);
	}
	for (const auto& t : d.value("texts", json::array())) {
		PpText txt;
		txt.id = getStr(t, "id");
		txt.label = getStr(t, "label");
		txt.anchorId = getStr(t, "anchorId");
		txt.rowId = getStr(t, "rowId");
		txt.date = getStr(t, "date");
		txt.color = getStr(t, "color");
		txt.dx = t.value("dx", 0.0f);
		txt.dy = t.value("dy", 0.0f);
		doc.texts.push_back(txt);
	}
	return doc;
}
