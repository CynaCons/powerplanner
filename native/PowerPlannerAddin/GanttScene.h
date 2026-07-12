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
#include "GanttAxisLayout.h"
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

	// Hierarchical two-band date header. ComputeAxisTierLayout owns all
	// calendar/tier/visibility math; this scene-only pass applies tokens and
	// turns the pure result into primitives in its historical emission order.
	{
		const float plotLeft = MARGIN + ROW_GUTTER;
		const float headTop = chartTop - AXIS_H;
		const float headMid = chartTop - AXIS_H / 2.0f;
		const AxisTierLayout axis = ComputeAxisTierLayout(doc, minD, maxD, pad, ptPerDay,
			plotLeft, chartRight, headTop, headMid, AXIS_H, chartBottom);
		auto emitLabels = [&](const std::vector<AxisTierLabel>& labels, const char* kind, unsigned long color) {
			for (const auto& label : labels) {
				Style style; style.textBgr = Bgr(color); style.fontSize = 7.0f; style.bold = true;
				style.align = label.centered ? TextAlign::Center : TextAlign::Left;
				Prim text = scene::text(label.rect.left, label.rect.top,
					label.rect.right - label.rect.left, label.rect.bottom - label.rect.top,
					Widen(label.label), style);
				text.tagKind = kind; text.tagId = label.id; sc.prims.push_back(text);
			}
		};
		emitLabels(axis.bottomLabels, "AXIS_BOT", gt::ink2);
		if (axis.hasBottomBand) {
			Style style; style.line = true; style.lineBgr = Bgr(th.divider); style.lineWeight = gt::hairline;
			Prim line = scene::line(axis.bandDivider.left, axis.bandDivider.top, axis.bandDivider.right, axis.bandDivider.bottom, style);
			line.tagKind = "AXIS_BANDDIV"; sc.prims.push_back(line);
		}
		emitLabels(axis.topLabels, "AXIS_TOP", gt::ink3);
		auto emitTicks = [&](const std::vector<AxisTierTick>& ticks, const char* kind, unsigned long color, float weight) {
			for (const auto& tick : ticks) {
				Style style; style.line = true; style.lineBgr = Bgr(color); style.lineWeight = weight; style.dash = tick.dashed;
				Prim line = scene::line(tick.x, tick.top, tick.x, tick.bottom, style);
				line.tagKind = kind; line.tagId = tick.id; sc.prims.push_back(line);
			}
		};
		emitTicks(axis.ticks, "AXIS_TICK", th.divider, gt::hairline);
		emitTicks(axis.majorTicks, "AXIS_MAJOR", gt::outline2, gt::hairline_major);
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

inline bool HasExplicitTimeWindow(const PpDocument& doc) {
	return !doc.windowStart.empty() && !doc.windowEnd.empty();
}

inline bool TimeWindowDateRangeIntersects(const std::string& start, const std::string& end,
	long windowStartDay, long windowEndDay) {
	return !start.empty() && !end.empty()
		&& DateToDays(start) <= windowEndDay && DateToDays(end) >= windowStartDay;
}

inline bool IsTimeWindowExemptPrim(const Prim& p) {
	const std::string& k = p.tagKind;
	return k == "HEADER_BAND" || k == "TITLE" || k == "RAIL_FILL" || k == "RAIL_DIVIDER"
		|| k == "ROW_DIVIDER" || k == "ROW_LABEL" || k == "RAIL_DOT" || k == "RAIL_TASKLBL"
		|| k.compare(0, 5, "AXIS_") == 0;
}

inline bool IsTimeWindowTaskPrim(const Prim& p) {
	return p.tagKind == "TASK" || p.tagKind == "TASK_PROGRESS"
		|| p.tagKind == "TASK_LABEL" || p.tagKind == "TASK_PCT";
}

inline bool IsTimeWindowMilestonePrim(const Prim& p) {
	return p.tagKind == "MILESTONE" || p.tagKind == "MILESTONE_LABEL";
}

inline bool IsTimeWindowMarkerPrim(const Prim& p) {
	return p.tagKind == "TODAY_LINE" || p.tagKind == "DEADLINE" || p.tagKind == "CUSTOM_MARKER"
		|| p.tagKind == "TODAY_LABEL" || p.tagKind == "DEADLINE_LABEL" || p.tagKind == "CUSTOM_MARKER_LABEL";
}

inline bool IsTimeWindowBracketPrim(const Prim& p) {
	return p.tagKind == "BRACKET" || p.tagKind == "BRACKET_TICK"
		|| p.tagKind == "BRACKET_BOTTOM" || p.tagKind == "BRACKET_LABEL";
}

inline bool ClipPrimBoxToTimeWindow(Prim* p, float clipLeft, float clipRight) {
	const float originalLeft = p->x;
	const float originalRight = p->x + p->w;
	if (originalRight <= clipLeft || originalLeft >= clipRight) return false;
	if (originalLeft < clipLeft) { p->x = clipLeft; p->w = originalRight - clipLeft; p->clippedL = true; }
	if (originalRight > clipRight) { p->w = clipRight - p->x; p->clippedR = true; }
	return p->w > 0.01f;
}

inline bool ClipPrimLineToTimeWindow(Prim* p, float clipLeft, float clipRight) {
	const float originalX = p->x, originalX2 = p->x2;
	if ((originalX < clipLeft && originalX2 < clipLeft) || (originalX > clipRight && originalX2 > clipRight)) return false;
	if (originalX == originalX2) return originalX >= clipLeft && originalX <= clipRight;
	auto clipEndpoint = [&](bool first, float edge) {
		float& x = first ? p->x : p->x2;
		float& y = first ? p->y : p->y2;
		const float ox = first ? p->x2 : p->x;
		const float oy = first ? p->y2 : p->y;
		y = oy + (y - oy) * (edge - ox) / (x - ox);
		x = edge;
	};
	if (p->x < clipLeft) { clipEndpoint(true, clipLeft); p->clippedL = true; }
	if (p->x > clipRight) { clipEndpoint(true, clipRight); p->clippedR = true; }
	if (p->x2 < clipLeft) { clipEndpoint(false, clipLeft); p->clippedL = true; }
	if (p->x2 > clipRight) { clipEndpoint(false, clipRight); p->clippedR = true; }
	return true;
}

// Explicit-window filter. It runs after normal prim emission, so it cannot
// alter the document or layout. Composite DEP/bracket elements are handled
// before generic clipping to avoid dangling segments/fragments.
inline void ClipSceneToExplicitTimeWindow(const PpDocument& doc, Scene* scene,
	float clipLeft, float clipRight) {
	if (!scene || !HasExplicitTimeWindow(doc)) return;
	const long windowStartDay = DateToDays(doc.windowStart);
	const long windowEndDay = DateToDays(doc.windowEnd);

	std::map<std::string, bool> ownerVisible;
	for (const auto& t : doc.tasks)
		ownerVisible[t.id] = TimeWindowDateRangeIntersects(t.start, t.end, windowStartDay, windowEndDay);
	for (const auto& m : doc.milestones)
		ownerVisible[m.id] = !m.date.empty() && DateToDays(m.date) >= windowStartDay && DateToDays(m.date) <= windowEndDay;
	std::map<std::string, bool> markerVisible;
	for (const auto& m : doc.markers)
		markerVisible[m.id] = !m.date.empty() && DateToDays(m.date) >= windowStartDay && DateToDays(m.date) <= windowEndDay;

	// Snapshot original anchor geometry before clipping. Anchored notes use this
	// to move from a truncated task's true right edge to its visible right edge.
	std::map<std::string, float> anchorRight;
	for (const auto& p : scene->prims) {
		if (p.tagKind == "TASK" || p.tagKind == "MILESTONE") anchorRight[p.tagId] = p.x + p.w;
	}

	std::map<std::string, bool> bracketVisible;
	std::map<std::string, std::pair<float, float>> bracketBounds;
	for (const auto& b : doc.brackets) {
		bracketVisible[b.id] = TimeWindowDateRangeIntersects(b.start, b.end, windowStartDay, windowEndDay);
	}
	for (const auto& p : scene->prims) {
		if (p.tagKind == "BRACKET") bracketBounds[p.tagId] = { std::min(p.x, p.x2), std::max(p.x, p.x2) };
	}

	std::map<std::string, bool> depVisible;
	for (const auto& d : doc.deps) {
		const auto from = ownerVisible.find(d.from), to = ownerVisible.find(d.to);
		depVisible[d.id] = from != ownerVisible.end() && to != ownerVisible.end() && from->second && to->second;
	}
	// A visible dependency still drops as a unit if its elbow leaves the window.
	for (const auto& p : scene->prims) {
		if (p.tagKind != "DEP" || !depVisible[p.tagId]) continue;
		const float lo = std::min(p.x, p.x2), hi = std::max(p.x, p.x2);
		if (lo < clipLeft || hi > clipRight) depVisible[p.tagId] = false;
	}

	std::map<std::string, const PpText*> textById;
	for (const auto& text : doc.texts) textById[text.id] = &text;

	std::vector<Prim> clipped;
	std::vector<Prim> continuations;
	clipped.reserve(scene->prims.size());
	for (Prim p : scene->prims) {
		if (IsTimeWindowExemptPrim(p)) { clipped.push_back(p); continue; }
		if (p.tagKind == "DEP") {
			if (depVisible[p.tagId]) clipped.push_back(p);
			continue;
		}
		if (IsTimeWindowBracketPrim(p)) {
			if (!bracketVisible[p.tagId]) continue;
			auto b = bracketBounds.find(p.tagId);
			if (b == bracketBounds.end()) continue;
			const float originalLeft = b->second.first, originalRight = b->second.second;
			const float left = std::max(originalLeft, clipLeft), right = std::min(originalRight, clipRight);
			if (right <= left) continue;
			if (originalLeft < clipLeft) p.clippedL = true;
			if (originalRight > clipRight) p.clippedR = true;
			if (p.tagKind == "BRACKET" || p.tagKind == "BRACKET_BOTTOM") {
				p.x = left; p.x2 = right;
			} else if (p.tagKind == "BRACKET_TICK") {
				const bool isLeftTick = p.x <= (originalLeft + originalRight) / 2.0f;
				p.x = p.x2 = isLeftTick ? left : right;
			} else { // BRACKET_LABEL
				p.x = left; p.w = right - left;
			}
			clipped.push_back(p);
			continue;
		}
		if (IsTimeWindowTaskPrim(p)) {
			auto it = ownerVisible.find(p.tagId);
			if (it == ownerVisible.end() || !it->second) continue;
		} else if (IsTimeWindowMilestonePrim(p)) {
			auto it = ownerVisible.find(p.tagId);
			if (it == ownerVisible.end() || !it->second) continue;
		} else if (IsTimeWindowMarkerPrim(p)) {
			auto it = markerVisible.find(p.tagId);
			if (it == markerVisible.end() || !it->second) continue;
		} else if (p.tagKind == "TEXT") {
			auto text = textById.find(p.tagId);
			if (text != textById.end() && !text->second->anchorId.empty()) {
				auto owner = ownerVisible.find(text->second->anchorId);
				if (owner != ownerVisible.end() && !owner->second) continue;
				auto right = anchorRight.find(text->second->anchorId);
				if (right != anchorRight.end()) p.x += std::max(clipLeft, std::min(clipRight, right->second)) - right->second;
			}
		}

		bool keep = true;
		if (p.kind == PrimKind::Line || p.kind == PrimKind::Connector)
			keep = ClipPrimLineToTimeWindow(&p, clipLeft, clipRight);
		else
			keep = ClipPrimBoxToTimeWindow(&p, clipLeft, clipRight);
		if (!keep) continue;
		if (p.tagKind == "TASK" && (p.clippedL || p.clippedR)) {
			Style cue; cue.line = true; cue.lineBgr = p.style.fillBgr; cue.lineWeight = 1.5f; cue.arrowEnd = true;
			const float cy = p.y + p.h / 2.0f;
			if (p.clippedL) {
				Prim leftCue = scene::connector(clipLeft + 5.0f, cy, clipLeft, cy, cue);
				leftCue.tagKind = "WINDOW_CONTINUATION"; leftCue.tagId = p.tagId + "-L";
				continuations.push_back(leftCue);
			}
			if (p.clippedR) {
				Prim rightCue = scene::connector(clipRight - 5.0f, cy, clipRight, cy, cue);
				rightCue.tagKind = "WINDOW_CONTINUATION"; rightCue.tagId = p.tagId + "-R";
				continuations.push_back(rightCue);
			}
		}
		clipped.push_back(p);
	}
	clipped.insert(clipped.end(), continuations.begin(), continuations.end());
	scene->prims.swap(clipped);
}

inline bool BuildSceneWithProjection(const PpDocument& doc, float slideW, Scene* outScene,
	const std::string& minD, const std::string& maxD, long pad, float ptPerDay) {
	GanttLayoutResult L = LayoutGantt(doc, minD);
	*outScene = BuildGanttScene(doc, L, minD, maxD, pad, ptPerDay, slideW, MaterialLight());
	if (HasExplicitTimeWindow(doc)) {
		const float clipLeft = MARGIN + ROW_GUTTER;
		const float clipRight = slideW - MARGIN;
		ClipSceneToExplicitTimeWindow(doc, outScene, clipLeft, clipRight);
	}
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
	long pad = 0; float ptPerDay = 0.0f;
	if (HasExplicitTimeWindow(doc)) {
		minD = doc.windowStart;
		maxD = doc.windowEnd;
		const long totalDays = std::max(1L, DateToDays(maxD) - DateToDays(minD) + 1);
		const float chartContentW = (slideW - MARGIN * 2.0f) - ROW_GUTTER;
		pad = 0;
		ptPerDay = chartContentW / (float)totalDays;
	} else {
		ComputeDocDateExtents(doc, &minD, &maxD);
		ComputeProjectionParams(minD, maxD, slideW, &pad, &ptPerDay);
	}
	BuildSceneWithProjection(doc, slideW, outScene, minD, maxD, pad, ptPerDay);
	*outMinD = minD; *outMaxD = maxD; *outPad = pad; *outPtPerDay = ptPerDay;
	return true;
}
