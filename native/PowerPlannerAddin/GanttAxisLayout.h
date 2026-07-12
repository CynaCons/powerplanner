// Pure axis-tier layout shared by the native scene builder and the overlay's
// W2 time-window preview. This header intentionally has no COM, Win32, GDI,
// or theme dependencies: callers supply geometry and apply visual tokens.
#pragma once

#include "GanttModel.h"
#include "GanttLayout.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

struct AxisTierRect {
	float left = 0.0f;
	float top = 0.0f;
	float right = 0.0f;
	float bottom = 0.0f;
};

struct AxisTierLabel {
	std::string id;
	std::string label;
	AxisTierRect rect;
	bool centered = false;
};

struct AxisTierTick {
	std::string id;
	float x = 0.0f;
	float top = 0.0f;
	float bottom = 0.0f;
	bool major = false;
	bool dashed = false;
};

struct AxisTierLayout {
	bool hasBottomBand = false;
	AxisTierRect bandDivider;
	std::vector<AxisTierLabel> bottomLabels;
	std::vector<AxisTierLabel> topLabels;
	std::vector<AxisTierTick> ticks;
	std::vector<AxisTierTick> majorTicks;
};

// Compute the date-header cells and plot ticks for [minD..maxD]. Coordinates
// are caller-owned logical units (slide points in BuildGanttScene; client
// pixels in the overlay preview). `plotLeft` is the x coordinate of minD when
// pad is zero, preserving the PP_PROJ mapping used by the scene builder.
inline AxisTierLayout ComputeAxisTierLayout(const PpDocument& doc,
	const std::string& minD, const std::string& maxD, long pad, float ptPerDay,
	float plotLeft, float plotRight, float headTop, float headMid, float axisH,
	float chartBottom)
{
	AxisTierLayout out;
	const long minDAbs = DateToDays(minD), maxDAbs = DateToDays(maxD);
	const char* const monthUpper[] = { "JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC" };
	using Meta = std::pair<std::string, std::string>; // {id, label}

	auto xToUnit = [&](long day) {
		return plotLeft + (float)(day - minDAbs + pad) * ptPerDay;
	};
	auto sinceMon = [](long day) -> int {           // 0 = day is a Monday
		int dow = (int)((((day % 7) + 4) % 7 + 7) % 7); // 0 = Sunday
		return (dow + 6) % 7;
	};
	auto ymd = [](const std::string& iso, int& y, int& m, int& d) {
		y = m = d = 0; sscanf_s(iso.c_str(), "%d-%d-%d", &y, &m, &d);
	};
	auto firstOfMonth = [](int y, int m) -> long {
		char b[16]; sprintf_s(b, "%04d-%02d-01", y, m); return DateToDays(b);
	};
	// Emit cell-start boundaries for a tier covering [minD..maxD]. The first
	// cell may begin before minD; the trailing sentinel closes the last cell.
	auto genTier = [&](const std::string& tier, std::vector<long>& starts, std::vector<Meta>& meta) {
		starts.clear(); meta.clear();
		int y0, m0, d0, y1, m1, d1; ymd(minD, y0, m0, d0); ymd(maxD, y1, m1, d1);
		if (tier == "year") {
			for (int y = y0; y <= y1 + 1; ++y) starts.push_back(firstOfMonth(y, 1));
			for (int y = y0; y <= y1; ++y) { char id[8], lb[8]; sprintf_s(id, "Y%04d", y); sprintf_s(lb, "%d", y); meta.push_back({ id, lb }); }
		} else if (tier == "quarter") {
			int y = y0, q = (m0 - 1) / 3 + 1, q1 = (m1 - 1) / 3 + 1;
			std::vector<std::pair<int, int>> cells;
			while (y < y1 || (y == y1 && q <= q1)) { cells.push_back({ y, q }); if (++q > 4) { q = 1; ++y; } }
			for (auto& c : cells) starts.push_back(firstOfMonth(c.first, (c.second - 1) * 3 + 1));
			starts.push_back(firstOfMonth(y, (q - 1) * 3 + 1));
			for (auto& c : cells) { char id[12], lb[16]; sprintf_s(id, "Q%04d-%d", c.first, c.second); sprintf_s(lb, "Q%d %d", c.second, c.first); meta.push_back({ id, lb }); }
		} else if (tier == "month") {
			int y = y0, m = m0;
			std::vector<std::pair<int, int>> cells;
			while (y < y1 || (y == y1 && m <= m1)) { cells.push_back({ y, m }); if (++m > 12) { m = 1; ++y; } }
			for (auto& c : cells) starts.push_back(firstOfMonth(c.first, c.second));
			starts.push_back(firstOfMonth(y, m));
			bool first = true;
			for (auto& c : cells) {
				char id[12]; sprintf_s(id, "M%04d-%02d", c.first, c.second);
				std::string lb = monthUpper[(c.second - 1) % 12];
				if (first) { char yb[8]; sprintf_s(yb, " %d", c.first); lb += yb; first = false; }
				meta.push_back({ id, lb });
			}
		} else if (tier == "week") {
			long mon = minDAbs - sinceMon(minDAbs);
			for (long w = mon; w <= maxDAbs; w += 7) starts.push_back(w);
			starts.push_back(starts.empty() ? mon : starts.back() + 7);
			for (size_t i = 0; i + 1 < starts.size(); ++i) {
				std::string iso = DaysToDate(starts[i]); int yy, mm, dd; ymd(iso, yy, mm, dd);
				char id[20]; sprintf_s(id, "W%s", iso.c_str()); char lb[16];
				if (doc.axisNumbering == "cw") sprintf_s(lb, "CW %d", IsoCalendarWeekNumber(starts[i]));
				else sprintf_s(lb, "%d", dd);
				meta.push_back({ id, lb });
			}
		} else if (tier == "day") {
			for (long dd = minDAbs; dd <= maxDAbs; ++dd) starts.push_back(dd);
			starts.push_back(maxDAbs + 1);
			for (long dd = minDAbs; dd <= maxDAbs; ++dd) {
				std::string iso = DaysToDate(dd); int yy, mm, dv; ymd(iso, yy, mm, dv);
				char id[20]; sprintf_s(id, "D%s", iso.c_str()); char lb[16];
				if (doc.axisNumbering == "cw") sprintf_s(lb, "CW %d", IsoCalendarWeekNumber(dd));
				else sprintf_s(lb, "%d", dv);
				meta.push_back({ id, lb });
			}
		}
	};
	auto emitLabels = [&](const std::vector<long>& starts, const std::vector<Meta>& meta,
		std::vector<AxisTierLabel>& destination, float bandY, float bandH, bool centered, bool thinDays) {
		int step = 1;
		if (thinDays) step = (ptPerDay >= 10.0f) ? 1 : (ptPerDay >= 5.0f ? 2 : 7);
		for (size_t i = 0; i + 1 < starts.size() && i < meta.size(); ++i) {
			if (thinDays && step > 1) {
				if (step == 7) { if (sinceMon(starts[i]) != 0) continue; }
				else if ((int)(i % step) != 0) continue;
			}
			float xs = xToUnit(starts[i]), xe = xToUnit(starts[i + 1]);
			float visStart = std::max(xs, plotLeft), visEnd = std::min(xe, plotRight);
			if (visEnd - visStart < 2.0f) continue;
			AxisTierLabel label;
			label.id = meta[i].first;
			label.label = meta[i].second;
			label.centered = centered;
			label.rect.top = bandY;
			label.rect.bottom = bandY + bandH;
			label.rect.left = centered ? visStart : visStart + 4.0f;
			label.rect.right = centered ? visEnd : label.rect.left + std::max(6.0f, visEnd - visStart - 6.0f);
			destination.push_back(label);
		}
	};
	auto countVisibleBoundaries = [&](const std::vector<long>& starts, const std::vector<Meta>& meta) {
		int count = 0;
		for (size_t i = 0; i < meta.size() && i < starts.size(); ++i) {
			float x = xToUnit(starts[i]);
			if (x > plotLeft + 0.5f && x <= plotRight) ++count;
		}
		return count;
	};
	auto emitTicks = [&](const std::vector<long>& starts, const std::vector<Meta>& meta,
		std::vector<AxisTierTick>& destination, float yTop, bool major, bool dashed) {
		for (size_t i = 0; i < meta.size() && i < starts.size(); ++i) {
			float x = xToUnit(starts[i]);
			if (x <= plotLeft + 0.5f || x > plotRight) continue;
			destination.push_back({ meta[i].first, x, yTop, chartBottom, major, dashed });
		}
	};

	std::string topTier, bottomTier;
	if (doc.scale == "year") { topTier = "year"; bottomTier = ""; }
	else if (doc.scale == "quarter") { topTier = "year"; bottomTier = "quarter"; }
	else if (doc.scale == "month") { topTier = "year"; bottomTier = "month"; }
	else if (doc.scale == "day") { topTier = "month"; bottomTier = "day"; }
	else { topTier = "month"; bottomTier = "week"; }

	out.hasBottomBand = !bottomTier.empty();
	if (out.hasBottomBand) {
		std::vector<long> starts; std::vector<Meta> meta; genTier(bottomTier, starts, meta);
		emitLabels(starts, meta, out.bottomLabels, headMid, axisH / 2.0f, true, bottomTier == "day");
		out.bandDivider = { plotLeft, headMid, plotRight, headMid };
	}
	{
		std::vector<long> starts; std::vector<Meta> meta; genTier(topTier, starts, meta);
		emitLabels(starts, meta, out.topLabels, headTop, bottomTier.empty() ? axisH : axisH / 2.0f, false, false);
	}
	std::string density = doc.gridDensity;
	std::string tickTier;
	if (density == "none") tickTier = "";
	else if (density.empty() || density == "auto") tickTier = bottomTier.empty() ? "year" : bottomTier;
	else tickTier = density;
	if (!tickTier.empty()) {
		auto coarser = [](const std::string& tier) -> std::string {
			if (tier == "day") return "week"; if (tier == "week") return "month";
			if (tier == "month") return "quarter"; if (tier == "quarter") return "year";
			return "";
		};
		std::vector<long> starts; std::vector<Meta> meta; genTier(tickTier, starts, meta);
		while (countVisibleBoundaries(starts, meta) > 150) {
			std::string coarserTier = coarser(tickTier); if (coarserTier.empty()) break;
			tickTier = coarserTier; genTier(tickTier, starts, meta);
		}
		emitTicks(starts, meta, out.ticks, bottomTier.empty() ? headTop : headMid, false, doc.gridStyle == "dotted");
	}
	if (!bottomTier.empty()) {
		std::vector<long> starts; std::vector<Meta> meta; genTier(topTier, starts, meta);
		emitTicks(starts, meta, out.majorTicks, headTop, true, false);
	}
	return out;
}
