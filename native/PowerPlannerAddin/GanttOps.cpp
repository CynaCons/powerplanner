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

// True if id names an existing task OR milestone — the only entity kinds a
// dependency edge may connect.
bool TaskOrMilestoneExists(const PpDocument& doc, const std::string& id) {
	for (const auto& task : doc.tasks) if (task.id == id) return true;
	for (const auto& ms : doc.milestones) if (ms.id == id) return true;
	return false;
}

// True if any row in doc.rows is a child of rowId (groupId == rowId). Used by
// IndentRow to enforce the 2-level nesting max (a row that already has
// children cannot itself become a child).
bool RowHasChildren(const PpDocument& doc, const std::string& rowId) {
	for (const auto& row : doc.rows) if (row.groupId == rowId) return true;
	return false;
}

// Index of rowId within doc.rows' flat display order, or -1 if absent.
int RowIndexOf(const PpDocument& doc, const std::string& rowId) {
	for (size_t i = 0; i < doc.rows.size(); ++i) {
		if (doc.rows[i].id == rowId) return (int)i;
	}
	return -1;
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

std::string AddMilestone(PpDocument& doc, const std::string& rowId, const std::string& label, const std::string& dateISO) {
	PpMilestone ms;
	ms.id = NextId(doc, "ms-");
	ms.rowId = rowId;
	ms.label = label;
	ms.date = dateISO;
	doc.milestones.push_back(ms);
	return ms.id;
}

bool DeleteById(PpDocument& doc, const std::string& id) {
	for (const auto& row : doc.rows) {
		if (row.id == id) {
			std::vector<std::string> deletedTaskIds;
			for (const auto& task : doc.tasks) {
				if (task.rowId == id) deletedTaskIds.push_back(task.id);
			}
			std::vector<std::string> deletedMilestoneIds;
			for (const auto& ms : doc.milestones) {
				if (ms.rowId == id) deletedMilestoneIds.push_back(ms.id);
			}

			RemoveIf(doc.rows, [&](const PpRow& r) { return r.id == id; });
			RemoveIf(doc.tasks, [&](const PpTask& t) { return t.rowId == id; });
			RemoveIf(doc.milestones, [&](const PpMilestone& m) { return m.rowId == id; });
			RemoveIf(doc.deps, [&](const PpDependency& d) {
				if (std::find(deletedTaskIds.begin(), deletedTaskIds.end(), d.from) != deletedTaskIds.end()
					|| std::find(deletedTaskIds.begin(), deletedTaskIds.end(), d.to) != deletedTaskIds.end())
					return true;
				return std::find(deletedMilestoneIds.begin(), deletedMilestoneIds.end(), d.from) != deletedMilestoneIds.end()
					|| std::find(deletedMilestoneIds.begin(), deletedMilestoneIds.end(), d.to) != deletedMilestoneIds.end();
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
			RemoveDependenciesTouching(doc, id);
			RemoveIf(doc.texts, [&](const PpText& t) { return t.anchorId == id; });
			return true;
		}
	}

	if (RemoveIf(doc.milestones, [&](const PpMilestone& m) { return m.id == id; })) {
		RemoveDependenciesTouching(doc, id);
		RemoveIf(doc.texts, [&](const PpText& t) { return t.anchorId == id; });
		return true;
	}
	if (RemoveIf(doc.brackets, [&](const PpBracket& b) { return b.id == id; })) return true;
	if (RemoveIf(doc.deps, [&](const PpDependency& d) { return d.id == id; })) return true;
	if (RemoveIf(doc.markers, [&](const PpMarker& m) { return m.id == id; })) return true;
	if (RemoveIf(doc.texts, [&](const PpText& t) { return t.id == id; })) return true;
	return false;
}

// --- S3 row operations (pure) ---

std::string AddRowAbove(PpDocument& doc, const std::string& refRowId, const std::string& label) {
	int idx = RowIndexOf(doc, refRowId);
	if (idx < 0) return "";

	PpRow row;
	row.id = NextId(doc, "row-");
	row.label = label;
	row.groupId = doc.rows[idx].groupId;
	doc.rows.insert(doc.rows.begin() + idx, row);
	return row.id;
}

std::string AddRowBelow(PpDocument& doc, const std::string& refRowId, const std::string& label) {
	int idx = RowIndexOf(doc, refRowId);
	if (idx < 0) return "";

	PpRow row;
	row.id = NextId(doc, "row-");
	row.label = label;
	row.groupId = doc.rows[idx].groupId;

	// Skip past refRowId's own contiguous child block so the new row lands
	// after the whole group, keeping the group contiguous.
	size_t insertAt = (size_t)idx + 1;
	while (insertAt < doc.rows.size() && doc.rows[insertAt].groupId == refRowId) ++insertAt;

	doc.rows.insert(doc.rows.begin() + insertAt, row);
	return row.id;
}

bool MoveRowUp(PpDocument& doc, const std::string& rowId) {
	int idx = RowIndexOf(doc, rowId);
	if (idx <= 0) return false; // not found (-1), or already at the top edge (0)
	std::swap(doc.rows[idx], doc.rows[idx - 1]);
	return true;
}

bool MoveRowDown(PpDocument& doc, const std::string& rowId) {
	int idx = RowIndexOf(doc, rowId);
	if (idx < 0 || idx >= (int)doc.rows.size() - 1) return false; // not found, or already at the bottom edge
	std::swap(doc.rows[idx], doc.rows[idx + 1]);
	return true;
}

bool IndentRow(PpDocument& doc, const std::string& rowId) {
	int idx = RowIndexOf(doc, rowId);
	if (idx < 0) return false;
	if (RowHasChildren(doc, rowId)) return false; // would exceed the 2-level max

	int parentIdx = -1;
	for (int i = idx - 1; i >= 0; --i) {
		if (doc.rows[i].groupId.empty()) { parentIdx = i; break; }
	}
	if (parentIdx < 0) return false; // no preceding top-level row

	std::string parentId = doc.rows[parentIdx].id;
	if (parentId == doc.rows[idx].groupId) return false; // already this row's parent (no-op)

	doc.rows[idx].groupId = parentId;
	return true;
}

bool OutdentRow(PpDocument& doc, const std::string& rowId) {
	int idx = RowIndexOf(doc, rowId);
	if (idx < 0 || doc.rows[idx].groupId.empty()) return false;
	doc.rows[idx].groupId.clear();
	return true;
}

bool DeleteRow(PpDocument& doc, const std::string& rowId) {
	if (RowIndexOf(doc, rowId) < 0) return false;

	// Re-parent children to top-level BEFORE the cascade removes the row.
	for (auto& row : doc.rows) {
		if (row.groupId == rowId) row.groupId.clear();
	}
	return DeleteById(doc, rowId);
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

bool SetLabelPlacement(PpDocument& doc, const std::string& taskId, const std::string& placement) {
	std::string v = placement.empty() ? "bar" : placement;
	if (v != "bar" && v != "rail" && v != "both") return false;
	for (auto& task : doc.tasks) {
		if (task.id == taskId) {
			task.labelPlacement = v;
			return true;
		}
	}
	return false;
}

bool SetRailLabelsGlobal(PpDocument& doc, bool on) {
	doc.railLabels = on;
	return true;
}

bool SetGridDensity(PpDocument& doc, const std::string& density) {
	std::string v = density.empty() ? "auto" : density;
	if (v != "auto" && v != "year" && v != "quarter" && v != "month" && v != "week" && v != "day" && v != "none") return false;
	doc.gridDensity = (v == "auto") ? "" : v;
	return true;
}

bool SetGridStyle(PpDocument& doc, const std::string& style) {
	std::string v = style.empty() ? "solid" : style;
	if (v != "solid" && v != "dotted") return false;
	doc.gridStyle = (v == "solid") ? "" : v;
	return true;
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

// --- S5 dependency operations (pure) ---

std::string AddDependency(PpDocument& doc, const std::string& from, const std::string& to,
	const std::string& type) {
	if (from == to) return ""; // self-edge
	if (!TaskOrMilestoneExists(doc, from) || !TaskOrMilestoneExists(doc, to)) return "";
	for (const auto& dep : doc.deps) {
		if (dep.from == from && dep.to == to) return ""; // one edge per (from, to) pair
	}
	std::string t = type.empty() ? "finish-to-start" : type;
	if (t != "finish-to-start" && t != "start-to-start" && t != "finish-to-finish" && t != "start-to-finish") return "";

	PpDependency dep;
	dep.id = NextId(doc, "dep");
	dep.from = from;
	dep.to = to;
	dep.type = t;
	doc.deps.push_back(dep);
	return dep.id;
}

int RemoveDependenciesTouching(PpDocument& doc, const std::string& id) {
	const auto oldSize = doc.deps.size();
	RemoveIf(doc.deps, [&](const PpDependency& d) { return d.from == id || d.to == id; });
	return (int)(oldSize - doc.deps.size());
}

bool RemoveDependencyById(PpDocument& doc, const std::string& depId) {
	return RemoveIf(doc.deps, [&](const PpDependency& d) { return d.id == depId; });
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
