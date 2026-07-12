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
	if (sscanf_s(iso.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return 0;
	return DaysFromCivil(y, m, d);
}

// Civil date from day number (Howard Hinnant's inverse algorithm).
std::string DaysToDate(long z) {
	z += 719468;
	const long era = (z >= 0 ? z : z - 146096) / 146097;
	const unsigned doe = (unsigned)(z - era * 146097);
	const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const long y = (long)yoe + era * 400;
	const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const unsigned mp = (5 * doy + 2) / 153;
	const unsigned d = doy - (153 * mp + 2) / 5 + 1;
	const unsigned m = mp < 10 ? mp + 3 : mp - 9;
	char buf[16];
	sprintf_s(buf, "%04ld-%02u-%02u", y + (m <= 2), m, d);
	return buf;
}

int IsoCalendarWeekNumber(long days) {
	// DateToDays(1970-01-01) == 0 and that date was a Thursday, so +3 maps
	// Monday to 0 with a floor-modulo for dates before the epoch as well.
	auto daysSinceMonday = [](long d) {
		long r = (d + 3) % 7;
		return r < 0 ? r + 7 : r;
	};
	const long thursday = days + 3 - daysSinceMonday(days);
	const std::string isoThursday = DaysToDate(thursday);
	int isoYear = 0;
	sscanf_s(isoThursday.c_str(), "%d", &isoYear);
	char jan4[16];
	sprintf_s(jan4, "%04d-01-04", isoYear);
	const long weekOneMonday = DateToDays(jan4) - daysSinceMonday(DateToDays(jan4));
	return (int)((days - weekOneMonday) / 7 + 1);
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

	// Step 9 — texts. Anchored text (anchorId set) tracks a task or milestone's
	// current laid-out position (so a rebuild after the anchor's dates shift
	// carries the text along automatically); free text (anchorId empty) is
	// placed at its own (rowId, date) cell origin. Anchors/rows that no longer
	// exist are silently dropped, mirroring how tasks/milestones drop when
	// their rowId disappears (Step 4/5 above).
	std::map<std::string, const LaidMilestone*> laidMsById;
	for (const auto& lm : R.milestones) laidMsById[lm.id] = &lm;
	for (const auto& txt : doc.texts) {
		LaidText lt;
		lt.id = txt.id;
		lt.dx = txt.dx;
		lt.dy = txt.dy;
		if (!txt.anchorId.empty()) {
			auto ft = laidById.find(txt.anchorId);
			if (ft != laidById.end()) {
				const LaidTask* anchor = ft->second;
				lt.anchored = true;
				lt.rowIndex = anchor->rowIndex;
				lt.subRow = anchor->subRow;
				lt.xDay = anchor->xDay;
				lt.widthDays = anchor->widthDays;
				R.texts.push_back(lt);
				continue;
			}
			auto fm = laidMsById.find(txt.anchorId);
			if (fm != laidMsById.end()) {
				const LaidMilestone* anchor = fm->second;
				lt.anchored = true;
				lt.rowIndex = anchor->rowIndex;
				lt.subRow = 0;
				lt.xDay = anchor->xDay;
				lt.widthDays = 0;
				R.texts.push_back(lt);
				continue;
			}
			continue;  // anchor no longer exists
		}
		auto it = rowIndex.find(txt.rowId);
		if (it == rowIndex.end()) continue;
		lt.anchored = false;
		lt.rowIndex = it->second;
		lt.subRow = 0;
		lt.xDay = xDay(txt.date);
		lt.widthDays = 0;
		R.texts.push_back(lt);
	}

	return R;
}
