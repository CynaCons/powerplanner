// The Gantt data model (mirrors spec/data-model.md). UTF-8 std::string fields so
// the model + layout are free of COM/PowerPoint types and JSON-friendly; the
// emitter converts to wide strings at the PowerPoint boundary.
#pragma once

#include <string>
#include <vector>

struct PpTask {
	std::string id, label, start, end, rowId, color;
	int percent = 0;
};
struct PpMilestone {
	std::string id, label, date, rowId, color;
};
struct PpDependency {
	std::string id, from, to, type;  // type: finish-to-start | start-to-start | finish-to-finish | start-to-finish
};
struct PpMarker {
	std::string id, type, label, date, color;  // type: deadline | today
};
struct PpBracket {
	std::string id, label, start, end, color;
	std::vector<std::string> rowIds;
};
struct PpRow {
	std::string id, label, groupId;  // groupId empty => top-level
	bool collapsed = false;
};
struct PpDocument {
	std::string title;
	std::vector<PpRow> rows;
	std::vector<PpTask> tasks;
	std::vector<PpMilestone> milestones;
	std::vector<PpBracket> brackets;
	std::vector<PpDependency> deps;
	std::vector<PpMarker> markers;
};
