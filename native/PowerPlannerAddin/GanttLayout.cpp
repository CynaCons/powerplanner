// Implementation of spec/layout.md. Mirrors src/layout/engine.ts step for step.
#include "GanttLayout.h"
#include <algorithm>
#include <map>
#include <set>
#include <cstdio>
#include <climits>

// Days since 1970-01-01 for a proleptic Gregorian Y-M-D (Howard Hinnant's algo).
static long DaysFromCivil(int y, int m, int d) {
	y -= (m <= 2);
	const long era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = (unsigned)(y - era * 400);
	const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (long)doe - 719468;
}

long DateToDays(const std::string& iso) {
	int y = 0, m = 0, d = 0;
	if (std::sscanf(iso.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return 0;
	return DaysFromCivil(y, m, d);
}

namespace {
// max(2, span+1): inclusive-end width with a 2-unit floor (engine: max(2, endX-x+1)).
long SpanWidthDays(const std::string& start, const std::string& end) {
	return std::max(2L, (DateToDays(end) - DateToDays(start)) + 1);
}
}

GanttLayoutResult LayoutGantt(const PpDocument& doc, const std::string& viewStart) {
	GanttLayoutResult R;
	const long vs = DateToDays(viewStart);
	auto xDay = [&](const std::string& iso) { return DateToDays(iso) - vs; };

	// Step 1 — visible rows (hide children of collapsed groups).
	std::set<std::string> collapsed;
	for (const auto& r : doc.rows) if (r.collapsed) collapsed.insert(r.id);
	std::vector<const PpRow*> visible;
	std::map<std::string, int> rowIndex;
	for (const auto& r : doc.rows) {
		if (!r.groupId.empty() && collapsed.count(r.groupId)) continue;
		rowIndex[r.id] = (int)visible.size();
		visible.push_back(&r);
		R.visibleRowIds.push_back(r.id);
	}

	// Step 2 — sub-row stacking (greedy first-fit by ascending start).
	std::vector<int> subRowCount(visible.size(), 1);
	std::map<std::string, int> taskSubRow;
	for (size_t i = 0; i < visible.size(); ++i) {
		std::vector<const PpTask*> rowTasks;
		for (const auto& t : doc.tasks) if (t.rowId == visible[i]->id) rowTasks.push_back(&t);
		std::stable_sort(rowTasks.begin(), rowTasks.end(),
			[](const PpTask* a, const PpTask* b) { return a->start < b->start; });
		std::vector<std::string> tracks;  // each track = ISO end date occupying it
		for (const PpTask* t : rowTasks) {
			int placed = -1;
			for (size_t k = 0; k < tracks.size(); ++k) {
				if (tracks[k] <= t->start) { tracks[k] = t->end; placed = (int)k; break; }
			}
			if (placed == -1) { tracks.push_back(t->end); placed = (int)tracks.size() - 1; }
			taskSubRow[t->id] = placed;
		}
		subRowCount[i] = std::max(1, (int)tracks.size());
	}

	// Step 3 — row slots + offsets.
	int acc = 0;
	for (size_t i = 0; i < visible.size(); ++i) {
		R.rowSlots.push_back(subRowCount[i]);
		R.rowOffsets.push_back(acc);
		acc += subRowCount[i];
	}
	R.chartRows = acc;

	// Step 4 — tasks (document order, visible rows only).
	for (const auto& t : doc.tasks) {
		auto it = rowIndex.find(t.rowId);
		if (it == rowIndex.end()) continue;
		LaidTask lt;
		lt.id = t.id;
		lt.rowIndex = it->second;
		lt.subRow = taskSubRow.count(t.id) ? taskSubRow[t.id] : 0;
		lt.xDay = xDay(t.start);
		lt.widthDays = SpanWidthDays(t.start, t.end);
		R.tasks.push_back(lt);
	}

	// Step 5 — milestones.
	for (const auto& m : doc.milestones) {
		auto it = rowIndex.find(m.rowId);
		if (it == rowIndex.end()) continue;
		R.milestones.push_back({ m.id, it->second, xDay(m.date) });
	}

	// Step 6 — summary bars for parent rows with descendant tasks.
	std::map<std::string, std::vector<std::string>> childRowsByParent;
	for (const auto& r : doc.rows) if (!r.groupId.empty()) childRowsByParent[r.groupId].push_back(r.id);
	for (const auto& parent : doc.rows) {
		auto ch = childRowsByParent.find(parent.id);
		if (ch == childRowsByParent.end() || ch->second.empty()) continue;
		auto pit = rowIndex.find(parent.id);
		if (pit == rowIndex.end()) continue;
		std::set<std::string> childIds(ch->second.begin(), ch->second.end());
		std::string mn, mx;
		for (const auto& t : doc.tasks) {
			if (!childIds.count(t.rowId)) continue;
			if (mn.empty() || t.start < mn) mn = t.start;
			if (mx.empty() || t.end > mx) mx = t.end;
		}
		if (mn.empty()) continue;
		R.summaries.push_back({ parent.id, pit->second, xDay(mn), SpanWidthDays(mn, mx) });
	}

	// Step 7 — brackets.
	for (const auto& b : doc.brackets) {
		int topRow = INT_MAX, bottomRow = 0;
		for (const auto& rid : b.rowIds) {
			int idx = rowIndex.count(rid) ? rowIndex[rid] : 0;
			topRow = std::min(topRow, idx);
			bottomRow = std::max(bottomRow, idx);
		}
		if (topRow == INT_MAX) topRow = 0;
		R.brackets.push_back({ b.id, xDay(b.start), SpanWidthDays(b.start, b.end), topRow, bottomRow });
	}

	// Step 8 — dependencies (endpoints by type).
	std::map<std::string, const LaidTask*> laidById;
	for (const auto& lt : R.tasks) laidById[lt.id] = &lt;
	for (const auto& dep : doc.deps) {
		auto f = laidById.find(dep.from), t = laidById.find(dep.to);
		if (f == laidById.end() || t == laidById.end()) continue;
		const LaidTask* fr = f->second;
		const LaidTask* to = t->second;
		long fromX = (dep.type == "finish-to-start" || dep.type == "finish-to-finish") ? fr->xDay + fr->widthDays : fr->xDay;
		long toX = (dep.type == "finish-to-start" || dep.type == "start-to-start") ? to->xDay : to->xDay + to->widthDays;
		R.dependencies.push_back({ dep.id, fromX, toX });
	}

	return R;
}
