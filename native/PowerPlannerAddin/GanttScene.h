// Pure (COM-free) scene construction: maps a PpDocument + its abstract layout
// to a device-neutral Scene of styled primitives in slide points. Split out of
// GanttBuilder.cpp so the offscreen ops harness can build a scene and assert on
// prim colors/geometry without linking PowerPoint. Every color comes from
// GanttTheme.h (G6). GanttBuilder.cpp owns the COM emission of this Scene.
#pragma once

// This header uses Win32 MultiByteToWideChar and std::min/std::max. Guard the
// min/max macros for the pure ops TU (which doesn't include pch.h); in the
// add-in TU pch.h already defined these and included windows.h, so the guards
// no-op.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "GanttModel.h"
#include "GanttLayout.h"
#include "Scene.h"
#include "GanttTheme.h"
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <cstdio>
#include <cmath>

// UTF-8 std::string -> std::wstring at the (eventual) PowerPoint text boundary.
inline std::wstring Widen(const std::string& s) {
	if (s.empty()) return L"";
	int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
	std::wstring w(n, L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
	return w;
}

inline const char* const kMonthNames[] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
inline const char* const kMonthUpper[] = { "JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC" };

// Rough Segoe UI width estimate (points) for label-fit without measuring at emit time.
inline float EstimateTextWidthPt(const std::string& text, float fontSize) {
	return (float)text.size() * fontSize * 0.55f;
}

// Layout constants in points (Material: roomy rail + row rhythm). Shared by the
// pure scene builder and GanttBuilder.cpp's COM projection/reconcile code.
constexpr float MARGIN     = 36.0f;
constexpr float ROW_GUTTER = 140.0f;
constexpr float ROW_HEIGHT = 36.0f;
constexpr float AXIS_H     = 30.0f;
constexpr float BAR_INSET  = 8.0f;

// ---- scene construction (Material visual vocabulary) -----------------------

inline Scene BuildGanttScene(const PpDocument& doc, const GanttLayoutResult& L,
	const std::string& minD, const std::string& maxD, long pad, float ptPerDay,
	float slideW, const Theme& th)
{
	Scene sc;
	const float chartW = slideW - MARGIN * 2.0f;
	const float chartRight = MARGIN + chartW;
	const float chartTop = MARGIN + AXIS_H;
	const float chartBottom = chartTop + (float)L.chartRows * ROW_HEIGHT;
	const long vs = DateToDays(minD);
	auto xToPt = [&](long xDay) { return MARGIN + ROW_GUTTER + (float)(xDay + pad) * ptPerDay; };
	auto slotTop = [&](int slot) { return chartTop + (float)slot * ROW_HEIGHT; };

	// Timeline header band (Material table-header strip; its bottom border is the
	// first row divider drawn below). Tagged (kind-only, no natural id — there is
	// exactly one) so UpdateGantt's diff can match it by (kind, "") without
	// relying on emission-order ordinals colliding with unrelated untagged
	// shapes (see GanttBuilder.cpp's ReconcileChartRoot comment on MatchKey).
	{ Style s; s.fill = true; s.fillBgr = Bgr(gt::headerBand); Prim p = scene::rect(MARGIN, chartTop - AXIS_H, chartW, AXIS_H, s); p.tagKind = "HEADER_BAND"; sc.prims.push_back(p); }

	// Left navigation rail + its right-edge divider (each kind-only-tagged;
	// exactly one of each, same reasoning as HEADER_BAND above).
	{ Style s; s.fill = true; s.fillBgr = Bgr(th.railSurface); Prim p = scene::rect(MARGIN, chartTop, ROW_GUTTER, chartBottom - chartTop, s); p.tagKind = "RAIL_FILL"; sc.prims.push_back(p); }
	{ Style s; s.line = true; s.lineBgr = Bgr(th.divider); s.lineWeight = 1.0f; Prim p = scene::line(MARGIN + ROW_GUTTER, chartTop, MARGIN + ROW_GUTTER, chartBottom, s); p.tagKind = "RAIL_DIVIDER"; sc.prims.push_back(p); }

	// Hierarchical two-band date header (spec R3). The top band is one tier
	// coarser than the bottom; separator ticks in the plot follow the bottom
	// tier (overridable via doc.gridDensity), with the coarser top-tier
	// boundaries drawn as heavier "major" ticks. Every band cell / tick carries
	// a stable per-cell id (Y/Q/M/W/D + ISO) so UpdateGantt's diff stays stable
	// as the visible range grows/shrinks. PP_PROJ (the day↔point mapping) is
	// untouched — this only changes which header shapes are drawn.
	{
		const float plotLeft = MARGIN + ROW_GUTTER;
		const float headTop = chartTop - AXIS_H;
		const float headMid = chartTop - AXIS_H / 2.0f;
		const long minDAbs = DateToDays(minD), maxDAbs = DateToDays(maxD);

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

		using Meta = std::pair<std::string, std::string>; // {id, label}
		// Emit cell-start boundaries for a tier covering [minD..maxD] (the first
		// cell may start before minD; a trailing sentinel start gives the last
		// cell its right edge). meta[i] describes the cell [starts[i],starts[i+1]).
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
					std::string lb = kMonthUpper[(c.second - 1) % 12];
					if (first) { char yb[8]; sprintf_s(yb, " %d", c.first); lb += yb; first = false; } // year on the first month
					meta.push_back({ id, lb });
				}
			} else if (tier == "week") {
				long mon = minDAbs - sinceMon(minDAbs);
				for (long w = mon; w <= maxDAbs; w += 7) starts.push_back(w);
				starts.push_back(starts.empty() ? mon : starts.back() + 7);
				for (size_t i = 0; i + 1 < starts.size(); ++i) {
					std::string iso = DaysToDate(starts[i]); int yy, mm, dd; ymd(iso, yy, mm, dd);
					char id[20]; sprintf_s(id, "W%s", iso.c_str()); char lb[8]; sprintf_s(lb, "%d", dd);
					meta.push_back({ id, lb });
				}
			} else if (tier == "day") {
				for (long dd = minDAbs; dd <= maxDAbs; ++dd) starts.push_back(dd);
				starts.push_back(maxDAbs + 1);
				for (long dd = minDAbs; dd <= maxDAbs; ++dd) {
					std::string iso = DaysToDate(dd); int yy, mm, dv; ymd(iso, yy, mm, dv);
					char id[20]; sprintf_s(id, "D%s", iso.c_str()); char lb[8]; sprintf_s(lb, "%d", dv);
					meta.push_back({ id, lb });
				}
			}
		};

		auto emitBandLabels = [&](const std::vector<long>& starts, const std::vector<Meta>& meta,
			const char* kind, float bandY, float bandH, unsigned long color, TextAlign al, bool thinDays) {
			int step = 1;
			if (thinDays) step = (ptPerDay >= 10.0f) ? 1 : (ptPerDay >= 5.0f ? 2 : 7); // else Mondays only
			for (size_t i = 0; i + 1 < starts.size() && i < meta.size(); ++i) {
				if (thinDays && step > 1) {
					if (step == 7) { if (sinceMon(starts[i]) != 0) continue; }
					else if ((int)(i % step) != 0) continue;
				}
				float xs = xToPt(starts[i] - vs), xe = xToPt(starts[i + 1] - vs);
				float visStart = std::max(xs, plotLeft), visEnd = std::min(xe, chartRight);
				if (visEnd - visStart < 2.0f) continue;
				Style ts; ts.textBgr = Bgr(color); ts.fontSize = 7.0f; ts.bold = true; ts.align = al;
				float tx = (al == TextAlign::Left) ? visStart + 4.0f : visStart;
				float tw = (al == TextAlign::Left) ? std::max(6.0f, visEnd - visStart - 6.0f) : (visEnd - visStart);
				Prim t = scene::text(tx, bandY, tw, bandH, Widen(meta[i].second), ts);
				t.tagKind = kind; t.tagId = meta[i].first; sc.prims.push_back(t);
			}
		};

		auto countVisibleBoundaries = [&](const std::vector<long>& starts, const std::vector<Meta>& meta) {
			int c = 0;
			for (size_t i = 0; i < meta.size() && i < starts.size(); ++i) {
				float x = xToPt(starts[i] - vs);
				if (x > plotLeft + 0.5f && x <= chartRight) ++c;
			}
			return c;
		};
		auto emitTicks = [&](const std::vector<long>& starts, const std::vector<Meta>& meta,
			const char* kind, float yTop, unsigned long color, float weight, bool dash) {
			for (size_t i = 0; i < meta.size() && i < starts.size(); ++i) {
				float x = xToPt(starts[i] - vs);
				if (x <= plotLeft + 0.5f || x > chartRight) continue;
				Style g; g.line = true; g.lineBgr = Bgr(color); g.lineWeight = weight; g.dash = dash;
				Prim ln = scene::line(x, yTop, x, chartBottom, g);
				ln.tagKind = kind; ln.tagId = meta[i].first; sc.prims.push_back(ln);
			}
		};

		// Scale -> (top tier, bottom tier). year is a single full-height band.
		std::string topTier, botTier;
		if (doc.scale == "year") { topTier = "year"; botTier = ""; }
		else if (doc.scale == "quarter") { topTier = "year"; botTier = "quarter"; }
		else if (doc.scale == "month") { topTier = "year"; botTier = "month"; }
		else if (doc.scale == "day") { topTier = "month"; botTier = "day"; }
		else { topTier = "month"; botTier = "week"; } // week (default)

		// Bottom band + its divider (skipped for the single-band year scale).
		if (!botTier.empty()) {
			std::vector<long> bs; std::vector<Meta> bm; genTier(botTier, bs, bm);
			emitBandLabels(bs, bm, "AXIS_BOT", headMid, AXIS_H / 2.0f, gt::ink2, TextAlign::Center, botTier == "day");
			Style d; d.line = true; d.lineBgr = Bgr(th.divider); d.lineWeight = gt::hairline;
			Prim ln = scene::line(plotLeft, headMid, chartRight, headMid, d); ln.tagKind = "AXIS_BANDDIV"; sc.prims.push_back(ln);
		}

		// Top band labels.
		{
			std::vector<long> ts_; std::vector<Meta> tm; genTier(topTier, ts_, tm);
			float topBandH = botTier.empty() ? AXIS_H : AXIS_H / 2.0f;
			emitBandLabels(ts_, tm, "AXIS_TOP", headTop, topBandH, gt::ink3, TextAlign::Left, false);
		}

		// Separator ticks (bottom tier, or gridDensity override), with a ~150
		// cap: on overflow, coarsen one tier (day→week→month→quarter→year) and
		// regenerate. "none" draws no ticks; labels are unaffected either way.
		std::string dens = doc.gridDensity;
		std::string tickTier;
		if (dens == "none") tickTier = "";
		else if (dens.empty() || dens == "auto") tickTier = botTier.empty() ? "year" : botTier;
		else tickTier = dens;

		if (!tickTier.empty()) {
			auto coarser = [](const std::string& t) -> std::string {
				if (t == "day") return "week"; if (t == "week") return "month";
				if (t == "month") return "quarter"; if (t == "quarter") return "year";
				return "";
			};
			std::vector<long> ks; std::vector<Meta> km; genTier(tickTier, ks, km);
			while (countVisibleBoundaries(ks, km) > 150) {
				std::string c = coarser(tickTier); if (c.empty()) break;
				tickTier = c; genTier(tickTier, ks, km);
			}
			float tickTop = botTier.empty() ? headTop : headMid;
			emitTicks(ks, km, "AXIS_TICK", tickTop, th.divider, gt::hairline, doc.gridStyle == "dotted");
		}

		// Major ticks at top-tier boundaries (heavier, outline2). Skipped for
		// year (its ticks already sit at year boundaries).
		if (!botTier.empty()) {
			std::vector<long> ms_; std::vector<Meta> mm2; genTier(topTier, ms_, mm2);
			emitTicks(ms_, mm2, "AXIS_MAJOR", headTop, gt::outline2, gt::hairline_major, false);
		}
	}

	// Row dividers (Material list style), full width. Tagged with the OWNING
	// row's id (naturally unique/stable) rather than left untagged/ordinal-
	// matched, so a divider stays matched to "its" row even if rows are
	// inserted/removed elsewhere and every subsequent ordinal would otherwise
	// shift. The final bottom-of-chart divider has no owning row; it gets a
	// fixed synthetic id since there is exactly one.
	{
		Style d; d.line = true; d.lineBgr = Bgr(th.divider); d.lineWeight = 0.75f;
		for (size_t i = 0; i < L.visibleRowIds.size(); ++i) {
			Prim p = scene::line(MARGIN, slotTop(L.rowOffsets[i]), chartRight, slotTop(L.rowOffsets[i]), d);
			p.tagKind = "ROW_DIVIDER"; p.tagId = L.visibleRowIds[i]; sc.prims.push_back(p);
		}
		Prim bottom = scene::line(MARGIN, chartBottom, chartRight, chartBottom, d);
		bottom.tagKind = "ROW_DIVIDER"; bottom.tagId = "__bottom__"; sc.prims.push_back(bottom);
	}

	// Summary bars.
	for (const auto& s : L.summaries) {
		float left = xToPt(s.xDay), width = std::max(2.0f, s.widthDays * ptPerDay);
		Style su; su.fill = true; su.fillBgr = Bgr(th.summary);
		Prim p = scene::rect(left, slotTop(s.rowIndex) + ROW_HEIGHT / 2.0f - 2.0f, width, 4.0f, su);
		p.tagKind = "SUMMARY"; p.tagId = s.rowId; sc.prims.push_back(p);
	}

	// Dependencies (elbow connectors) — below gridlines/row bands, above bar bodies
	// and all text (SR-VIZ-03). Explicit elbow routing (matches web layout) so the
	// arrow on the final horizontal segment always points into the target edge.
	std::map<std::string, const PpTask*> taskById;
	for (const auto& t : doc.tasks) taskById[t.id] = &t;
	std::map<std::string, const LaidTask*> laidById;
	for (const auto& lt : L.tasks) laidById[lt.id] = &lt;
	std::map<std::string, std::pair<std::string, std::string>> depEnds;
	for (const auto& d : doc.deps) depEnds[d.id] = { d.from, d.to };
	for (const auto& d : L.dependencies) {
		auto e = depEnds.find(d.id); if (e == depEnds.end()) continue;
		const LaidTask* fr = laidById.count(e->second.first) ? laidById[e->second.first] : nullptr;
		const LaidTask* to = laidById.count(e->second.second) ? laidById[e->second.second] : nullptr;
		if (!fr || !to) continue;
		float x1 = xToPt(d.fromXDay), y1 = slotTop(L.rowOffsets[fr->rowIndex] + fr->subRow) + ROW_HEIGHT / 2.0f;
		float x2 = xToPt(d.toXDay), y2 = slotTop(L.rowOffsets[to->rowIndex] + to->subRow) + ROW_HEIGHT / 2.0f;
		float midX = (x1 + x2) / 2.0f;
		Style c; c.line = true; c.lineBgr = Bgr(th.connector); c.lineWeight = gt::dep_weight;
		Prim hSeg = scene::line(x1, y1, midX, y1, c); hSeg.tagKind = "DEP"; hSeg.tagId = d.id; sc.prims.push_back(hSeg);
		Prim vSeg = scene::line(midX, y1, midX, y2, c); vSeg.tagKind = "DEP"; vSeg.tagId = d.id; sc.prims.push_back(vSeg);
		Style ca = c; ca.arrowEnd = true;
		Prim arrSeg = scene::connector(midX, y2, x2, y2, ca); arrSeg.tagKind = "DEP"; arrSeg.tagId = d.id; sc.prims.push_back(arrSeg);
	}

	// Task bars (+ percent-complete). Material bar (docs/design-tokens.md §3 +
	// the mockup's .bar): TRACK = the effective swatch pre-blended 40% over
	// white (rounded rect, radius bar.radius); PROGRESS = the SOLID swatch
	// overlaid from the left across percent% of the width (same radius). The
	// stored per-task color drives the swatch (empty ⇒ swatch1). On-bar label
	// is a separate TASK_LABEL prim emitted after progress (SR-VIZ-01).
	for (const auto& lt : L.tasks) {
		const PpTask* t = taskById[lt.id];
		float left = xToPt(lt.xDay), width = std::max(2.0f, lt.widthDays * ptPerDay);
		float laneTop = slotTop(L.rowOffsets[lt.rowIndex] + lt.subRow);
		float top = laneTop + BAR_INSET;
		float h = ROW_HEIGHT - BAR_INSET * 2.0f;
		float laneCenter = laneTop + ROW_HEIGHT / 2.0f;
		// Label placement: "rail"/"both"/global railLabels put the label in the
		// rail; "bar"/empty keep it on the bar. railLabels (global) suppresses
		// the on-bar label entirely (all-rail override, R4 B4.2).
		const std::string& place = t->labelPlacement;
		bool railEntry = doc.railLabels || place == "rail" || place == "both";
		bool showBarLabel = !doc.railLabels && (place.empty() || place == "bar" || place == "both");

		unsigned long swatch = gt::EffectiveSwatch(t->color);
		unsigned long track = gt::BlendOnWhite(swatch, 0.40f);
		Style bar; bar.fill = true; bar.fillBgr = Bgr(track); bar.corner = gt::bar_radius;
		Prim p = scene::roundRect(left, top, width, h, bar);
		p.tagKind = "TASK"; p.tagId = t->id; sc.prims.push_back(p);
		if (t->percent > 0) {
			float pw = width * (float)t->percent / 100.0f;
			if (pw > 1.5f) {
				Style pr; pr.fill = true; pr.fillBgr = Bgr(swatch); pr.corner = gt::bar_radius;
				Prim u = scene::roundRect(left, top, pw, h, pr); u.tagKind = "TASK_PROGRESS"; u.tagId = t->id; sc.prims.push_back(u);
			}
		}
		if (showBarLabel && !t->label.empty()) {
			const float labelFont = 11.0f;
			const float estW = EstimateTextWidthPt(t->label, labelFont);
			const float innerPad = gt::bar_label_pad * 2.0f;
			const bool fitsInside = width > 54.0f && estW <= width - innerPad;
			Style lbl;
			lbl.fontSize = labelFont;
			lbl.align = fitsInside ? TextAlign::Center : TextAlign::Left;
			float lx, lw, ly;
			if (fitsInside) {
				lbl.textBgr = Bgr(th.onPrimary);
				lx = left; lw = width; ly = top;
			} else {
				lbl.textBgr = Bgr(th.onSurfaceVariant);
				lx = left + width + 6.0f;
				lw = std::max(estW + 4.0f, 48.0f);
				ly = top + (h - 14.0f) / 2.0f;
			}
			Prim lblPrim = scene::text(lx, ly, lw, h, Widen(t->label), lbl);
			lblPrim.tagKind = "TASK_LABEL"; lblPrim.tagId = t->id; sc.prims.push_back(lblPrim);
		}

		// Rail entry: 8pt swatch dot (radius 3) + task label (type.railTask) at
		// the task's lane, inside the left rail. Ellipsized by the renderer
		// (wordwrap off) at the rail's right padding.
		if (railEntry) {
			const float dotSize = 8.0f, dotX = MARGIN + 12.0f, labelX = MARGIN + 24.0f;
			Style ds; ds.fill = true; ds.fillBgr = Bgr(swatch); ds.corner = 3.0f;
			Prim dot = scene::roundRect(dotX, laneCenter - dotSize / 2.0f, dotSize, dotSize, ds);
			dot.tagKind = "RAIL_DOT"; dot.tagId = t->id; sc.prims.push_back(dot);
			Style rl; rl.textBgr = Bgr(gt::ink); rl.fontSize = 8.5f; rl.align = TextAlign::Left;
			Prim rlbl = scene::text(labelX, laneCenter - 7.0f, MARGIN + ROW_GUTTER - labelX - 8.0f, 14.0f, Widen(t->label), rl);
			rlbl.tagKind = "RAIL_TASKLBL"; rlbl.tagId = t->id; sc.prims.push_back(rlbl);

			// With the label off the bar, a wide-enough bar (>= ~110pt) shows a
			// right-aligned % readout (type.pct); suppressed on narrow bars.
			if (t->percent > 0 && width >= 110.0f) {
				char pct[8]; sprintf_s(pct, "%d%%", t->percent);
				Style ps; ps.textBgr = Bgr(th.onPrimary); ps.fontSize = 7.0f; ps.bold = true; ps.align = TextAlign::Right;
				Prim pctp = scene::text(left, top, width - 6.0f, h, Widen(pct), ps);
				pctp.tagKind = "TASK_PCT"; pctp.tagId = t->id; sc.prims.push_back(pctp);
			}
		}
	}

	// Milestones (+ labels). Diamond fill = ink (tokens §1), overridable per
	// milestone via its stored color.
	for (const auto& m : L.milestones) {
		float cx = xToPt(m.xDay) + ptPerDay / 2.0f;
		float cy = slotTop(L.rowOffsets[m.rowIndex]) + ROW_HEIGHT / 2.0f;
		float sz = 13.0f;
		std::string label, msColor;
		for (const auto& md : doc.milestones) if (md.id == m.id) { label = md.label; msColor = md.color; }
		Style d; d.fill = true; d.fillBgr = Bgr(gt::EffectiveColor(msColor, th.milestone));
		Prim dm = scene::diamond(cx - sz / 2.0f, cy - sz / 2.0f, sz, sz, d); dm.tagKind = "MILESTONE"; dm.tagId = m.id; sc.prims.push_back(dm);
		Style ml; ml.textBgr = Bgr(th.onSurfaceVariant); ml.fontSize = 10.0f; ml.align = TextAlign::Left;
		Prim t = scene::text(cx + sz / 2.0f + 3.0f, cy - 8.0f, 96.0f, 14.0f, Widen(label), ml); t.tagKind = "MILESTONE_LABEL"; t.tagId = m.id; sc.prims.push_back(t);
	}

	// Brackets (+ labels). Line color = the bracket's stored color, else the
	// structural hairline default.
	for (const auto& b : L.brackets) {
		float left = xToPt(b.xDay), width = std::max(2.0f, b.widthDays * ptPerDay);
		float top = slotTop(L.rowOffsets[b.topRow]) - 12.0f;
		int bottomSlot = L.rowOffsets[b.bottomRow] + L.rowSlots[b.bottomRow];
		float bottom = slotTop(bottomSlot) - 12.0f;

		std::string label, brColor;
		for (const auto& bd : doc.brackets) if (bd.id == b.id) { label = bd.label; brColor = bd.color; }
		unsigned long bracketColor = gt::EffectiveColor(brColor, th.bracket);

		Style br; br.line = true; br.lineBgr = Bgr(bracketColor); br.lineWeight = 1.5f;

		// 1. Top horizontal line
		Prim pTop = scene::line(left, top, left + width, top, br);
		pTop.tagKind = "BRACKET"; pTop.tagId = b.id; sc.prims.push_back(pTop);

		// 2. Left vertical tick pointing DOWN (tick size 6pt)
		Prim pLeft = scene::line(left, top, left, top + 6.0f, br);
		pLeft.tagKind = "BRACKET_TICK"; pLeft.tagId = b.id; sc.prims.push_back(pLeft);

		// 3. Right vertical tick pointing DOWN (tick size 6pt)
		Prim pRight = scene::line(left + width, top, left + width, top + 6.0f, br);
		pRight.tagKind = "BRACKET_TICK"; pRight.tagId = b.id; sc.prims.push_back(pRight);

		// 4. Bottom horizontal line (thin/lighter, matching web's 0.4 opacity)
		Style brBot; brBot.line = true; brBot.lineBgr = Bgr(th.divider); brBot.lineWeight = 1.0f;
		Prim pBot = scene::line(left, bottom, left + width, bottom, brBot);
		pBot.tagKind = "BRACKET_BOTTOM"; pBot.tagId = b.id; sc.prims.push_back(pBot);

		Style bl; bl.textBgr = Bgr(bracketColor); bl.fontSize = 10.0f; bl.align = TextAlign::Center;
		// Label centered just above the bracket box so it doesn't sit on the bars.
		Prim t = scene::text(left, top - 13.0f, width, 12.0f, Widen(label), bl); t.tagKind = "BRACKET_LABEL"; t.tagId = b.id; sc.prims.push_back(t);
	}

	// Markers (Today line / Deadline lines / Custom lines). Default color by
	// type — today = primary, deadline = deadline token, custom = customMarker
	// token — overridable per marker via its stored color. Labels sit in a
	// reserved strip above the axis header bands (SR-VIZ-02); stagger when
	// horizontal rects would overlap (two levels max).
	struct MarkerLabelSlot { float left, right; int level; };
	std::vector<MarkerLabelSlot> markerLabelSlots;
	auto markerLabelLevel = [&](float lx, float lw) -> int {
		const float lr = lx + lw;
		for (int lvl = 0; lvl < 2; ++lvl) {
			bool clash = false;
			for (const auto& s : markerLabelSlots) {
				if (s.level != lvl) continue;
				if (lx < s.right && lr > s.left) { clash = true; break; }
			}
			if (!clash) return lvl;
		}
		return 1; // two levels max — allow overlap on level 1
	};
	const float headTop = chartTop - AXIS_H;
	for (const auto& m : doc.markers) {
		float mx = xToPt(DateToDays(m.date) - vs);
		if (mx >= MARGIN + ROW_GUTTER && mx <= chartRight) {
			bool isToday = m.type == "today";
			bool isDeadline = m.type == "deadline";
			unsigned long typeDefault = isToday ? th.primary : (isDeadline ? gt::deadline : gt::customMarker);
			unsigned long markerColor = gt::EffectiveColor(m.color, typeDefault);

			Style ms; ms.line = true;
			ms.lineBgr = Bgr(markerColor);
			ms.lineWeight = 1.25f;
			Prim ln = scene::line(mx, chartTop - AXIS_H, mx, chartBottom, ms);
			ln.tagKind = isToday ? "TODAY_LINE" : (isDeadline ? "DEADLINE" : "CUSTOM_MARKER");
			ln.tagId = m.id;
			sc.prims.push_back(ln);

			const float lx = mx + gt::marker_label_gap;
			const float lw = std::max(EstimateTextWidthPt(m.label, 9.0f) + 4.0f, 48.0f);
			int lvl = markerLabelLevel(lx, lw);
			markerLabelSlots.push_back({ lx, lx + lw, lvl });
			float ly = (lvl == 0)
				? headTop - gt::marker_label_strip
				: headTop - gt::marker_label_h;

			Style ml;
			ml.textBgr = Bgr(markerColor);
			ml.fontSize = 9.0f;
			ml.align = TextAlign::Left;
			ml.bold = true;
			Prim t = scene::text(lx, ly, lw, gt::marker_label_h, Widen(m.label), ml);
			t.tagKind = isToday ? "TODAY_LABEL" : (isDeadline ? "DEADLINE_LABEL" : "CUSTOM_MARKER_LABEL");
			t.tagId = m.id;
			sc.prims.push_back(t);
		}
	}

	// Free-standing / anchored text annotations. Anchored text (L.anchored)
	// sits at its anchor's current top-right corner (xDay+widthDays, subRow)
	// plus (dx, dy) points, so it automatically follows the anchor across a
	// rebuild triggered by the anchor's dates shifting (the layout pass
	// recomputes anchor xDay/widthDays/rowIndex/subRow every call — see
	// GanttLayout.cpp Step 9). Free text sits at its (rowId, date) cell origin
	// plus (dx, dy). Tagged PP_KIND=TEXT / PP_ID=<id> so UpdateGantt's diff
	// moves rather than recreates it (stable id, unlike an emission ordinal).
	for (const auto& lt : L.texts) {
		std::string label, txColor;
		for (const auto& td : doc.texts) if (td.id == lt.id) { label = td.label; txColor = td.color; }
		float baseX = lt.anchored ? xToPt(lt.xDay + lt.widthDays) : xToPt(lt.xDay);
		float baseY = slotTop(L.rowOffsets[lt.rowIndex] + lt.subRow);
		float left = baseX + lt.dx;
		float top = baseY + lt.dy;
		Style tx; tx.textBgr = Bgr(gt::EffectiveColor(txColor, gt::ink2)); tx.fontSize = 10.0f; tx.align = TextAlign::Left;
		Prim t = scene::text(left, top, 120.0f, 16.0f, Widen(label), tx);
		t.tagKind = "TEXT"; t.tagId = lt.id; sc.prims.push_back(t);
	}

	// Row labels (in the rail, indented for children). Rows with an empty
	// label are rail-task rows (identity comes from rail task labels) — no
	// ROW_LABEL shape is emitted so BuildRowBands must use PP_ROWY for bands.
	for (size_t i = 0; i < L.visibleRowIds.size(); ++i) {
		std::string label, groupId;
		for (const auto& r : doc.rows) if (r.id == L.visibleRowIds[i]) { label = r.label; groupId = r.groupId; }
		if (label.empty()) continue;
		float indent = groupId.empty() ? 0.0f : 14.0f;
		Style rl; rl.textBgr = Bgr(groupId.empty() ? th.onSurface : th.onSurfaceVariant);
		rl.fontSize = 11.0f; rl.bold = groupId.empty(); rl.align = TextAlign::Left;
		Prim t = scene::text(MARGIN + 8.0f + indent, slotTop(L.rowOffsets[i]), ROW_GUTTER - 16.0f - indent, ROW_HEIGHT, Widen(label), rl);
		t.tagKind = "ROW_LABEL"; t.tagId = L.visibleRowIds[i]; sc.prims.push_back(t);
	}

	// Title.
	{
		Style ti; ti.textBgr = Bgr(th.onSurface); ti.fontSize = 16.0f; ti.align = TextAlign::Left;
		Prim t = scene::text(MARGIN, MARGIN - 26.0f, chartW, 24.0f, Widen(doc.title), ti); t.tagKind = "TITLE"; sc.prims.push_back(t);
	}

	return sc;
}

// True when the row renders its name in the left rail (vs a rail-task row whose
// identity is carried only by task rail labels).
inline bool RowShowsNameInRail(const PpDocument& doc, const std::string& rowId) {
	for (const auto& r : doc.rows) {
		if (r.id == rowId) return !r.label.empty();
	}
	return false;
}

// PP_ROWY payload: row lane geometry in chart-local points (same space as
// emitted prim Left/Top before grouping). naturalW/naturalH are the chart's
// layout footprint so the overlay can map local coords through a fitted frame.
inline std::string BuildRowYJson(const PpDocument& doc, float slideW, const std::string& minD) {
	GanttLayoutResult L = LayoutGantt(doc, minD);
	const float chartTop = MARGIN + AXIS_H;
	const float naturalW = slideW - MARGIN * 2.0f;

	std::string rowsJson;
	rowsJson.reserve(L.visibleRowIds.size() * 96);
	// Defer naturalH until row bands are emitted — a row with rowSlots==0
	// (milestone-only / empty-task edge cases) must still get one lane so
	// Overlay's PP_ROWY bands cover every doc row.
	float bandBottom = chartTop + (float)L.chartRows * ROW_HEIGHT;
	for (size_t i = 0; i < L.visibleRowIds.size(); ++i) {
		if (i) rowsJson += ",";
		const int slots = (i < L.rowSlots.size()) ? std::max(1, L.rowSlots[i]) : 1;
		const float top = chartTop + (float)L.rowOffsets[i] * ROW_HEIGHT;
		const float bot = top + (float)slots * ROW_HEIGHT;
		if (bot > bandBottom) bandBottom = bot;
		int lvl = 0;
		for (const auto& r : doc.rows) {
			if (r.id == L.visibleRowIds[i]) { lvl = r.groupId.empty() ? 0 : 1; break; }
		}
		const bool name = RowShowsNameInRail(doc, L.visibleRowIds[i]);
		char rowBuf[160];
		::sprintf_s(rowBuf, "{\"id\":\"%s\",\"top\":%.4f,\"bot\":%.4f,\"lvl\":%d,\"name\":%s}",
			L.visibleRowIds[i].c_str(), top, bot, lvl, name ? "true" : "false");
		rowsJson += rowBuf;
	}
	const float naturalH = bandBottom - MARGIN;
	char head[192];
	::sprintf_s(head, "{\"railL\":%.4f,\"railR\":%.4f,\"naturalW\":%.4f,\"naturalH\":%.4f,\"rows\":[",
		MARGIN, MARGIN + ROW_GUTTER, naturalW, naturalH);
	std::string out = head;
	out += rowsJson;
	out += "]}";
	return out;
}

// Natural dated bounds (tasks + milestones only — matches BuildProjectedScene).
inline void ComputeDocDateExtents(const PpDocument& doc, std::string* outMinD, std::string* outMaxD) {
	std::string minD, maxD;
	auto consider = [&](const std::string& d) {
		if (d.empty()) return;
		if (minD.empty() || d < minD) minD = d;
		if (maxD.empty() || d > maxD) maxD = d;
	};
	for (const auto& t : doc.tasks) { consider(t.start); consider(t.end); }
	for (const auto& m : doc.milestones) consider(m.date);
	if (minD.empty()) {
		SYSTEMTIME st; ::GetLocalTime(&st);
		char todayBuf[16];
		::sprintf_s(todayBuf, "%04d-%02d-%02d", (int)st.wYear, (int)st.wMonth, (int)st.wDay);
		minD = todayBuf;
		maxD = DaysToDate(DateToDays(minD) + 30);
	}
	*outMinD = minD;
	*outMaxD = maxD;
}

// True when every dated element still fits inside [winMinD-pad .. winMaxD+pad].
// Used by UpdateGantt to keep PP_PROJ/ptPerDay stable across in-range nudges
// (SR-SMO-01 v2.5.3-latency-green) so the scene-diff fast path stays eligible.
inline bool DocDatesFitPaddedWindow(const PpDocument& doc, const std::string& winMinD,
	const std::string& winMaxD, long pad) {
	if (winMinD.empty() || winMaxD.empty()) return false;
	const long lo = DateToDays(winMinD) - pad;
	const long hi = DateToDays(winMaxD) + pad;
	auto inWin = [&](const std::string& d) {
		if (d.empty()) return true;
		const long day = DateToDays(d);
		return day >= lo && day <= hi;
	};
	for (const auto& t : doc.tasks) {
		if (!inWin(t.start) || !inWin(t.end)) return false;
	}
	for (const auto& m : doc.milestones) if (!inWin(m.date)) return false;
	for (const auto& b : doc.brackets) {
		if (!inWin(b.start) || !inWin(b.end)) return false;
	}
	for (const auto& mk : doc.markers) if (!inWin(mk.date)) return false;
	return true;
}

inline void ComputeProjectionParams(const std::string& minD, const std::string& maxD, float slideW,
	long* outPad, float* outPtPerDay) {
	const long totalDays = std::max(1L, (DateToDays(maxD) - DateToDays(minD)) + 1);
	*outPad = std::max(1L, (long)(totalDays * 0.05));
	const float chartContentW = (slideW - MARGIN * 2.0f) - ROW_GUTTER;
	*outPtPerDay = chartContentW / (float)(totalDays + (*outPad) * 2);
}

inline bool BuildSceneWithProjection(const PpDocument& doc, float slideW, Scene* outScene,
	const std::string& minD, const std::string& maxD, long pad, float ptPerDay) {
	GanttLayoutResult L = LayoutGantt(doc, minD);
	*outScene = BuildGanttScene(doc, L, minD, maxD, pad, ptPerDay, slideW, MaterialLight());
	return true;
}

// Shared by InsertGantt/UpdateGantt (and the ops harness): date range ->
// projection (pt/day), padded 5% each side, then the resulting Scene. A
// document with no dated tasks/milestones (e.g. a rows-only chart after
// deleting every task) does NOT fail: it gets a default today..today+30d
// window so the chart still emits a valid axis + rows + PP_PROJ and every
// creation route keeps working on empty charts (SR-CRE-01,
// docs/SRS_CreationFlows.md). PP_PROJ field meanings
// {minDay,pad,ptPerDay,originX} are frozen and unchanged by the fallback —
// minDay is simply the default window's first day.
inline bool BuildProjectedScene(const PpDocument& doc, float slideW, Scene* outScene,
	std::string* outMinD, std::string* outMaxD, long* outPad, float* outPtPerDay) {
	std::string minD, maxD;
	ComputeDocDateExtents(doc, &minD, &maxD);
	long pad = 0; float ptPerDay = 0.0f;
	ComputeProjectionParams(minD, maxD, slideW, &pad, &ptPerDay);
	BuildSceneWithProjection(doc, slideW, outScene, minD, maxD, pad, ptPerDay);
	*outMinD = minD; *outMaxD = maxD; *outPad = pad; *outPtPerDay = ptPerDay;
	return true;
}
