#include "GanttOps.h"
#include "GanttLayout.h"

#include <algorithm>
#include <string>

namespace {
bool IdExists(const PpDocument& doc, const std::string& id) {
	for (const auto& row : doc.rows) if (row.id == id) return true;
	for (const auto& task : doc.tasks) if (task.id == id) return true;
	for (const auto& ms : doc.milestones) if (ms.id == id) return true;
	for (const auto& br : doc.brackets) if (br.id == id) return true;
	for (const auto& dep : doc.deps) if (dep.id == id) return true;
	for (const auto& marker : doc.markers) if (marker.id == id) return true;
	for (const auto& txt : doc.texts) if (txt.id == id) return true;
	return false;
}

std::string NextId(const PpDocument& doc, const std::string& prefix) {
	for (long n = 1;; ++n) {
		std::string id = prefix + std::to_string(n);
		if (!IdExists(doc, id)) return id;
	}
}

bool RowExists(const PpDocument& doc, const std::string& rowId) {
	for (const auto& row : doc.rows) if (row.id == rowId) return true;
	return false;
}

template <typename T, typename Pred>
bool RemoveIf(std::vector<T>& items, Pred pred) {
	const auto oldSize = items.size();
	items.erase(std::remove_if(items.begin(), items.end(), pred), items.end());
	return items.size() != oldSize;
}
}

std::string AddRow(PpDocument& doc, const std::string& label, const std::string& afterRowId) {
	PpRow row;
	row.id = NextId(doc, "row-");
	row.label = label;

	if (!afterRowId.empty()) {
		for (auto it = doc.rows.begin(); it != doc.rows.end(); ++it) {
			if (it->id == afterRowId) {
				doc.rows.insert(it + 1, row);
				return row.id;
			}
		}
	}

	doc.rows.push_back(row);
	return row.id;
}

std::string AddTask(PpDocument& doc, const std::string& rowId, const std::string& label, const std::string& startISO, const std::string& endISO) {
	PpTask task;
	task.id = NextId(doc, "task-");
	task.rowId = rowId;
	task.label = label;
	task.start = startISO;
	task.end = endISO;
	doc.tasks.push_back(task);
	return task.id;
}

bool DeleteById(PpDocument& doc, const std::string& id) {
	for (const auto& row : doc.rows) {
		if (row.id == id) {
			std::vector<std::string> deletedTaskIds;
			for (const auto& task : doc.tasks) {
				if (task.rowId == id) deletedTaskIds.push_back(task.id);
			}

			RemoveIf(doc.rows, [&](const PpRow& r) { return r.id == id; });
			RemoveIf(doc.tasks, [&](const PpTask& t) { return t.rowId == id; });
			RemoveIf(doc.milestones, [&](const PpMilestone& m) { return m.rowId == id; });
			RemoveIf(doc.deps, [&](const PpDependency& d) {
				return std::find(deletedTaskIds.begin(), deletedTaskIds.end(), d.from) != deletedTaskIds.end()
					|| std::find(deletedTaskIds.begin(), deletedTaskIds.end(), d.to) != deletedTaskIds.end();
			});
			RemoveIf(doc.brackets, [&](PpBracket& b) {
				b.rowIds.erase(std::remove(b.rowIds.begin(), b.rowIds.end(), id), b.rowIds.end());
				return b.rowIds.empty();
			});
			RemoveIf(doc.texts, [&](const PpText& t) {
				if (!t.anchorId.empty()) {
					return std::find(deletedTaskIds.begin(), deletedTaskIds.end(), t.anchorId) != deletedTaskIds.end();
				}
				return t.rowId == id;
			});
			return true;
		}
	}

	for (const auto& task : doc.tasks) {
		if (task.id == id) {
			RemoveIf(doc.tasks, [&](const PpTask& t) { return t.id == id; });
			RemoveIf(doc.deps, [&](const PpDependency& d) { return d.from == id || d.to == id; });
			RemoveIf(doc.texts, [&](const PpText& t) { return t.anchorId == id; });
			return true;
		}
	}

	if (RemoveIf(doc.milestones, [&](const PpMilestone& m) { return m.id == id; })) {
		RemoveIf(doc.texts, [&](const PpText& t) { return t.anchorId == id; });
		return true;
	}
	if (RemoveIf(doc.brackets, [&](const PpBracket& b) { return b.id == id; })) return true;
	if (RemoveIf(doc.deps, [&](const PpDependency& d) { return d.id == id; })) return true;
	if (RemoveIf(doc.markers, [&](const PpMarker& m) { return m.id == id; })) return true;
	if (RemoveIf(doc.texts, [&](const PpText& t) { return t.id == id; })) return true;
	return false;
}

bool MoveTaskToRow(PpDocument& doc, const std::string& taskId, const std::string& newRowId) {
	if (!RowExists(doc, newRowId)) return false;
	for (auto& task : doc.tasks) {
		if (task.id == taskId) {
			task.rowId = newRowId;
			return true;
		}
	}
	return false;
}

bool SetTaskPercent(PpDocument& doc, const std::string& taskId, int pct) {
	for (auto& task : doc.tasks) {
		if (task.id == taskId) {
			task.percent = std::max(0, std::min(100, pct));
			return true;
		}
	}
	return false;
}

bool SetTaskPercentValue(PpDocument& doc, const std::string& taskId, int pct) {
	return SetTaskPercent(doc, taskId, pct);
}

bool SetTaskColor(PpDocument& doc, const std::string& taskId, const std::string& color) {
	for (auto& task : doc.tasks) {
		if (task.id == taskId) {
			task.color = color;
			return true;
		}
	}
	return false;
}

std::string AddMarker(PpDocument& doc, const std::string& type, const std::string& label, const std::string& dateISO) {
	PpMarker marker;
	marker.id = NextId(doc, "mk");
	marker.type = type;
	marker.label = label;
	marker.date = dateISO;
	doc.markers.push_back(marker);
	return marker.id;
}

bool SetMarkerDate(PpDocument& doc, const std::string& id, const std::string& dateISO) {
	for (auto& marker : doc.markers) {
		if (marker.id == id) {
			marker.date = dateISO;
			return true;
		}
	}
	return false;
}

bool SetMarkerLabel(PpDocument& doc, const std::string& id, const std::string& label) {
	for (auto& marker : doc.markers) {
		if (marker.id == id) {
			marker.label = label;
			return true;
		}
	}
	return false;
}

std::string AddText(PpDocument& doc, const std::string& label, const std::string& anchorId,
	const std::string& rowId, const std::string& dateISO) {
	PpText txt;
	txt.id = NextId(doc, "txt");
	txt.label = label;
	txt.anchorId = anchorId;
	if (anchorId.empty()) {
		txt.rowId = rowId;
		txt.date = dateISO;
	}
	doc.texts.push_back(txt);
	return txt.id;
}

bool SetTextLabel(PpDocument& doc, const std::string& id, const std::string& label) {
	for (auto& txt : doc.texts) {
		if (txt.id == id) {
			txt.label = label;
			return true;
		}
	}
	return false;
}

bool MoveText(PpDocument& doc, const std::string& id, float dx, float dy,
	const std::string& rowId, const std::string& dateISO) {
	for (auto& txt : doc.texts) {
		if (txt.id == id) {
			txt.dx = dx;
			txt.dy = dy;
			if (txt.anchorId.empty()) {
				if (!rowId.empty()) txt.rowId = rowId;
				if (!dateISO.empty()) txt.date = dateISO;
			}
			return true;
		}
	}
	return false;
}

bool NudgeTask(PpDocument& doc, const std::string& taskId, long deltaDays) {
	for (auto& task : doc.tasks) {
		if (task.id == taskId) {
			task.start = DaysToDate(DateToDays(task.start) + deltaDays);
			task.end = DaysToDate(DateToDays(task.end) + deltaDays);
			return true;
		}
	}
	return false;
}

bool SetTaskDates(PpDocument& doc, const std::string& taskId, const std::string& startISO, const std::string& endISO) {
	for (auto& task : doc.tasks) {
		if (task.id == taskId) {
			long startDay = DateToDays(startISO);
			long endDay = DateToDays(endISO);
			if (endDay < startDay) endDay = startDay; // enforce end >= start
			task.start = DaysToDate(startDay);
			task.end = DaysToDate(endDay);
			return true;
		}
	}
	return false;
}

bool SetTitle(PpDocument& doc, const std::string& title) {
	doc.title = title;
	return true;
}

bool SetEntityLabel(PpDocument& doc, const std::string& id, const std::string& label) {
	for (auto& row : doc.rows) {
		if (row.id == id) {
			row.label = label;
			return true;
		}
	}
	for (auto& milestone : doc.milestones) {
		if (milestone.id == id) {
			milestone.label = label;
			return true;
		}
	}
	for (auto& bracket : doc.brackets) {
		if (bracket.id == id) {
			bracket.label = label;
			return true;
		}
	}
	for (auto& task : doc.tasks) {
		if (task.id == id) {
			task.label = label;
			return true;
		}
	}
	return false;
}

bool SetScale(PpDocument& doc, const std::string& scale) {
	if (scale != "day" && scale != "week" && scale != "month" && scale != "quarter" && scale != "year") return false;
	doc.scale = scale;
	return true;
}

int OpsSelfTest() {
	return 0;
}
