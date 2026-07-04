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

	// Month gridlines + labels (behind bars). Tagged with the ISO "YYYY-MM"
	// month as tagId: a NATURALLY unique, stable identity (unlike an emission
	// ordinal) that survives the visible month SET changing size across a
	// rebuild (e.g. a date shift that adds/drops a month at either edge) —
	// each month's gridline/label keeps its own identity instead of every
	// later month's ordinal shifting by one.
	{
		int y0 = 0, m0 = 0, dd = 0, y1 = 0, m1 = 0;
		sscanf_s(minD.c_str(), "%d-%d-%d", &y0, &m0, &dd);
		sscanf_s(maxD.c_str(), "%d-%d-%d", &y1, &m1, &dd);
		int yy = y0, mm = m0;
		while (yy < y1 || (yy == y1 && mm <= m1)) {
			char iso[16]; sprintf_s(iso, "%04d-%02d-01", yy, mm);
			float x = xToPt(DateToDays(iso) - vs);
			if (x >= MARGIN + ROW_GUTTER - 1.0f && x <= chartRight) {
				char monthId[8]; sprintf_s(monthId, "%04d-%02d", yy, mm);
				Style g; g.line = true; g.lineBgr = Bgr(th.divider); g.lineWeight = 0.5f;
				Prim ln = scene::line(x, chartTop, x, chartBottom, g); ln.tagKind = "AXIS_GRID"; ln.tagId = monthId; sc.prims.push_back(ln);
				char lbl[24]; sprintf_s(lbl, "%s %d", kMonthNames[(mm - 1) % 12], yy);
				Style al; al.textBgr = Bgr(th.onSurfaceVariant); al.fontSize = 10.0f; al.align = TextAlign::Left;
				Prim t = scene::text(x + 3.0f, chartTop - 16.0f, 64.0f, 13.0f, Widen(lbl), al); t.tagKind = "AXIS_LABEL"; t.tagId = monthId; sc.prims.push_back(t);
			}
			if (++mm > 12) { mm = 1; ++yy; }
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

	// Task bars (+ percent-complete). Material bar (docs/design-tokens.md §3 +
	// the mockup's .bar): TRACK = the effective swatch pre-blended 40% over
	// white (rounded rect, radius bar.radius); PROGRESS = the SOLID swatch
	// overlaid from the left across percent% of the width (same radius). The
	// stored per-task color drives the swatch (empty ⇒ swatch1). On-bar label
	// stays white text on the track shape.
	std::map<std::string, const PpTask*> taskById;
	for (const auto& t : doc.tasks) taskById[t.id] = &t;
	std::map<std::string, const LaidTask*> laidById;
	for (const auto& lt : L.tasks) laidById[lt.id] = &lt;
	for (const auto& lt : L.tasks) {
		const PpTask* t = taskById[lt.id];
		float left = xToPt(lt.xDay), width = std::max(2.0f, lt.widthDays * ptPerDay);
		float top = slotTop(L.rowOffsets[lt.rowIndex] + lt.subRow) + BAR_INSET;
		float h = ROW_HEIGHT - BAR_INSET * 2.0f;
		unsigned long swatch = gt::EffectiveSwatch(t->color);
		unsigned long track = gt::BlendOnWhite(swatch, 0.40f);
		Style bar; bar.fill = true; bar.fillBgr = Bgr(track); bar.corner = gt::bar_radius;
		bar.textBgr = Bgr(th.onPrimary); bar.fontSize = 11.0f; bar.align = TextAlign::Center;
		Prim p = scene::roundRect(left, top, width, h, bar);
		if (width > 54.0f) p.text = Widen(t->label);
		p.tagKind = "TASK"; p.tagId = t->id; sc.prims.push_back(p);
		if (t->percent > 0) {
			float pw = width * (float)t->percent / 100.0f;
			if (pw > 1.5f) {
				Style pr; pr.fill = true; pr.fillBgr = Bgr(swatch); pr.corner = gt::bar_radius;
				Prim u = scene::roundRect(left, top, pw, h, pr); u.tagKind = "TASK_PROGRESS"; u.tagId = t->id; sc.prims.push_back(u);
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
	// token — overridable per marker via its stored color.
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

			Style ml;
			ml.textBgr = Bgr(markerColor);
			ml.fontSize = 9.0f;
			ml.align = TextAlign::Left;
			ml.bold = true;
			Prim t = scene::text(mx + 4.0f, chartTop - AXIS_H + 2.0f, 96.0f, 12.0f, Widen(m.label), ml);
			t.tagKind = isToday ? "TODAY_LABEL" : (isDeadline ? "DEADLINE_LABEL" : "CUSTOM_MARKER_LABEL");
			t.tagId = m.id;
			sc.prims.push_back(t);
		}
	}

	// Dependencies (elbow connectors).
	std::map<std::string, std::pair<std::string, std::string>> depEnds;
	for (const auto& d : doc.deps) depEnds[d.id] = { d.from, d.to };
	for (const auto& d : L.dependencies) {
		auto e = depEnds.find(d.id); if (e == depEnds.end()) continue;
		const LaidTask* fr = laidById.count(e->second.first) ? laidById[e->second.first] : nullptr;
		const LaidTask* to = laidById.count(e->second.second) ? laidById[e->second.second] : nullptr;
		if (!fr || !to) continue;
		float x1 = xToPt(d.fromXDay), y1 = slotTop(L.rowOffsets[fr->rowIndex] + fr->subRow) + ROW_HEIGHT / 2.0f;
		float x2 = xToPt(d.toXDay), y2 = slotTop(L.rowOffsets[to->rowIndex] + to->subRow) + ROW_HEIGHT / 2.0f;
		Style c; c.line = true; c.lineBgr = Bgr(th.connector); c.lineWeight = 1.0f; c.arrowEnd = true;
		Prim p = scene::connector(x1, y1, x2, y2, c); p.tagKind = "DEP"; p.tagId = d.id; sc.prims.push_back(p);
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

	// Row labels (in the rail, indented for children).
	for (size_t i = 0; i < L.visibleRowIds.size(); ++i) {
		std::string label, groupId;
		for (const auto& r : doc.rows) if (r.id == L.visibleRowIds[i]) { label = r.label; groupId = r.groupId; }
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

// Shared by InsertGantt/UpdateGantt (and the ops harness): date range ->
// projection (pt/day), padded 5% each side, then the resulting Scene. Returns
// false if the document has no dated tasks/milestones to anchor a range on.
inline bool BuildProjectedScene(const PpDocument& doc, float slideW, Scene* outScene,
	std::string* outMinD, std::string* outMaxD, long* outPad, float* outPtPerDay) {
	std::string minD, maxD;
	auto consider = [&](const std::string& d) {
		if (d.empty()) return;
		if (minD.empty() || d < minD) minD = d;
		if (maxD.empty() || d > maxD) maxD = d;
	};
	for (const auto& t : doc.tasks) { consider(t.start); consider(t.end); }
	for (const auto& m : doc.milestones) consider(m.date);
	if (minD.empty()) return false;

	const long totalDays = std::max(1L, (DateToDays(maxD) - DateToDays(minD)) + 1);
	const long pad = std::max(1L, (long)(totalDays * 0.05));
	const float chartContentW = (slideW - MARGIN * 2.0f) - ROW_GUTTER;
	const float ptPerDay = chartContentW / (float)(totalDays + pad * 2);

	GanttLayoutResult L = LayoutGantt(doc, minD);
	*outScene = BuildGanttScene(doc, L, minD, maxD, pad, ptPerDay, slideW, MaterialLight());
	*outMinD = minD; *outMaxD = maxD; *outPad = pad; *outPtPerDay = ptPerDay;
	return true;
}
