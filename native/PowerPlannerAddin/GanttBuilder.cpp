#include "pch.h"
#include "GanttBuilder.h"
#include "GanttLayout.h"
#include "GanttJson.h"
#include "Scene.h"
#include "PptRenderer.h"
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <cstdio>
#include <cmath>

// ---- helpers ---------------------------------------------------------------

static std::wstring Widen(const std::string& s) {
	if (s.empty()) return L"";
	int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
	std::wstring w(n, L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
	return w;
}
static std::string Narrow(const wchar_t* w) {
	if (!w || !*w) return "";
	int len = (int)::wcslen(w);
	int n = ::WideCharToMultiByte(CP_UTF8, 0, w, len, NULL, 0, NULL, NULL);
	std::string s(n, '\0');
	::WideCharToMultiByte(CP_UTF8, 0, w, len, &s[0], n, NULL, NULL);
	return s;
}

static const char* kMonthNames[] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };

// Layout constants in points (Material: roomy rail + row rhythm).
namespace {
const float MARGIN     = 36.0f;
const float ROW_GUTTER = 140.0f;
const float ROW_HEIGHT = 36.0f;
const float AXIS_H     = 30.0f;
const float BAR_INSET  = 8.0f;
}

// ---- sample document -------------------------------------------------------

PpDocument MakeSampleDocument() {
	PpDocument doc;
	doc.title = "Q3 Launch Plan";
	doc.rows = {
		{ "r_phase", "Phase 1", "" },
		{ "r_research", "Research", "r_phase" },
		{ "r_design", "Design", "r_phase" },
		{ "r_build", "Build", "" },
		{ "r_launch", "Launch", "" },
	};
	doc.tasks = {
		{ "t1", "Discovery",      "2026-06-01", "2026-06-12", "r_research", "", 100 },
		{ "t2", "Interviews",     "2026-06-08", "2026-06-19", "r_research", "", 60 },
		{ "t3", "Wireframes",     "2026-06-15", "2026-06-26", "r_design",   "", 40 },
		{ "t4", "Visual design",  "2026-06-22", "2026-07-10", "r_design",   "", 10 },
		{ "t5", "Implementation", "2026-07-06", "2026-07-31", "r_build",    "", 0  },
		{ "t6", "QA + polish",    "2026-07-27", "2026-08-07", "r_build",    "", 0  },
	};
	doc.milestones = {
		{ "m1", "Design freeze", "2026-07-10", "r_design",  "" },
		{ "m2", "Ship",          "2026-08-10", "r_launch",  "" },
	};
	doc.brackets = {
		{ "b1", "Delivery", "2026-07-06", "2026-08-07", "", { "r_build" } },
	};
	doc.deps = {
		{ "d1", "t1", "t3", "finish-to-start" },
		{ "d2", "t3", "t5", "finish-to-start" },
		{ "d3", "t5", "t6", "finish-to-start" },
	};
	return doc;
}

// ---- scene construction (Material visual vocabulary) -----------------------

static Scene BuildGanttScene(const PpDocument& doc, const GanttLayoutResult& L,
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

	// Left navigation rail + its right-edge divider.
	{ Style s; s.fill = true; s.fillBgr = Bgr(th.railSurface); sc.prims.push_back(scene::rect(MARGIN, chartTop, ROW_GUTTER, chartBottom - chartTop, s)); }
	{ Style s; s.line = true; s.lineBgr = Bgr(th.divider); s.lineWeight = 1.0f; sc.prims.push_back(scene::line(MARGIN + ROW_GUTTER, chartTop, MARGIN + ROW_GUTTER, chartBottom, s)); }

	// Month gridlines + labels (behind bars).
	{
		int y0 = 0, m0 = 0, dd = 0, y1 = 0, m1 = 0;
		sscanf_s(minD.c_str(), "%d-%d-%d", &y0, &m0, &dd);
		sscanf_s(maxD.c_str(), "%d-%d-%d", &y1, &m1, &dd);
		int yy = y0, mm = m0;
		while (yy < y1 || (yy == y1 && mm <= m1)) {
			char iso[16]; sprintf_s(iso, "%04d-%02d-01", yy, mm);
			float x = xToPt(DateToDays(iso) - vs);
			if (x >= MARGIN + ROW_GUTTER - 1.0f && x <= chartRight) {
				Style g; g.line = true; g.lineBgr = Bgr(th.divider); g.lineWeight = 0.5f;
				Prim ln = scene::line(x, chartTop, x, chartBottom, g); ln.tagKind = "AXIS_GRID"; sc.prims.push_back(ln);
				char lbl[24]; sprintf_s(lbl, "%s %d", kMonthNames[(mm - 1) % 12], yy);
				Style al; al.textBgr = Bgr(th.onSurfaceVariant); al.fontSize = 10.0f; al.align = TextAlign::Left;
				Prim t = scene::text(x + 3.0f, chartTop - 16.0f, 64.0f, 13.0f, Widen(lbl), al); t.tagKind = "AXIS_LABEL"; sc.prims.push_back(t);
			}
			if (++mm > 12) { mm = 1; ++yy; }
		}
	}

	// Row dividers (Material list style), full width.
	{
		Style d; d.line = true; d.lineBgr = Bgr(th.divider); d.lineWeight = 0.75f;
		for (size_t i = 0; i < L.visibleRowIds.size(); ++i)
			sc.prims.push_back(scene::line(MARGIN, slotTop(L.rowOffsets[i]), chartRight, slotTop(L.rowOffsets[i]), d));
		sc.prims.push_back(scene::line(MARGIN, chartBottom, chartRight, chartBottom, d));
	}

	// Summary bars.
	for (const auto& s : L.summaries) {
		float left = xToPt(s.xDay), width = std::max(2.0f, s.widthDays * ptPerDay);
		Style su; su.fill = true; su.fillBgr = Bgr(th.summary);
		Prim p = scene::rect(left, slotTop(s.rowIndex) + ROW_HEIGHT / 2.0f - 2.0f, width, 4.0f, su);
		p.tagKind = "SUMMARY"; p.tagId = s.rowId; sc.prims.push_back(p);
	}

	// Task bars (+ percent-complete).
	std::map<std::string, const PpTask*> taskById;
	for (const auto& t : doc.tasks) taskById[t.id] = &t;
	std::map<std::string, const LaidTask*> laidById;
	for (const auto& lt : L.tasks) laidById[lt.id] = &lt;
	for (const auto& lt : L.tasks) {
		const PpTask* t = taskById[lt.id];
		float left = xToPt(lt.xDay), width = std::max(2.0f, lt.widthDays * ptPerDay);
		float top = slotTop(L.rowOffsets[lt.rowIndex] + lt.subRow) + BAR_INSET;
		float h = ROW_HEIGHT - BAR_INSET * 2.0f;
		Style bar; bar.fill = true; bar.fillBgr = Bgr(th.primary);
		bar.textBgr = Bgr(th.onPrimary); bar.fontSize = 11.0f; bar.align = TextAlign::Center;
		Prim p = scene::roundRect(left, top, width, h, bar);
		if (width > 54.0f) p.text = Widen(t->label);
		p.tagKind = "TASK"; p.tagId = t->id; sc.prims.push_back(p);
		if (t->percent > 0) {
			float pw = width * (float)t->percent / 100.0f;
			if (pw > 1.5f) {
				Style pr; pr.fill = true; pr.fillBgr = Bgr(th.primaryDark);
				Prim u = scene::rect(left, top + h - 3.5f, pw, 3.5f, pr); u.tagKind = "TASK_PROGRESS"; u.tagId = t->id; sc.prims.push_back(u);
			}
		}
	}

	// Milestones (+ labels).
	for (const auto& m : L.milestones) {
		float cx = xToPt(m.xDay) + ptPerDay / 2.0f;
		float cy = slotTop(L.rowOffsets[m.rowIndex]) + ROW_HEIGHT / 2.0f;
		float sz = 13.0f;
		std::string label;
		for (const auto& md : doc.milestones) if (md.id == m.id) label = md.label;
		Style d; d.fill = true; d.fillBgr = Bgr(th.milestone);
		Prim dm = scene::diamond(cx - sz / 2.0f, cy - sz / 2.0f, sz, sz, d); dm.tagKind = "MILESTONE"; dm.tagId = m.id; sc.prims.push_back(dm);
		Style ml; ml.textBgr = Bgr(th.onSurfaceVariant); ml.fontSize = 10.0f; ml.align = TextAlign::Left;
		Prim t = scene::text(cx + sz / 2.0f + 3.0f, cy - 8.0f, 96.0f, 14.0f, Widen(label), ml); t.tagKind = "MILESTONE_LABEL"; sc.prims.push_back(t);
	}

	// Brackets (+ labels).
	for (const auto& b : L.brackets) {
		float left = xToPt(b.xDay), width = std::max(2.0f, b.widthDays * ptPerDay);
		float top = slotTop(L.rowOffsets[b.topRow]) + 2.0f;
		int bottomSlot = L.rowOffsets[b.bottomRow] + L.rowSlots[b.bottomRow];
		float h = std::max(8.0f, slotTop(bottomSlot) - top - 2.0f);
		Style br; br.line = true; br.lineBgr = Bgr(th.bracket); br.lineWeight = 1.0f;
		Prim p = scene::rect(left, top, width, h, br); p.tagKind = "BRACKET"; p.tagId = b.id; sc.prims.push_back(p);
		std::string label; for (const auto& bd : doc.brackets) if (bd.id == b.id) label = bd.label;
		Style bl; bl.textBgr = Bgr(th.bracket); bl.fontSize = 10.0f; bl.align = TextAlign::Left;
		Prim t = scene::text(left + 4.0f, top + 1.0f, width - 8.0f, 14.0f, Widen(label), bl); t.tagKind = "BRACKET_LABEL"; sc.prims.push_back(t);
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

// ---- emission --------------------------------------------------------------

HRESULT InsertGantt(IDispatch* pApp, const PpDocument& doc, int* outShapeCount) {
	if (!pApp) return E_POINTER;
	int count = 0;
	try {
		PowerPoint::_ApplicationPtr app(pApp);
		PowerPoint::_PresentationPtr pres = app->GetActivePresentation();
		const float slideW = (float)pres->GetPageSetup()->GetSlideWidth();
		PowerPoint::DocumentWindowPtr win = app->GetActiveWindow();
		PowerPoint::_SlidePtr slide = win->GetView()->GetSlide();
		PowerPoint::ShapesPtr shapes = slide->GetShapes();

		// Date range -> projection (pt/day), padded 5% each side.
		std::string minD, maxD;
		auto consider = [&](const std::string& d) {
			if (d.empty()) return;
			if (minD.empty() || d < minD) minD = d;
			if (maxD.empty() || d > maxD) maxD = d;
		};
		for (const auto& t : doc.tasks) { consider(t.start); consider(t.end); }
		for (const auto& m : doc.milestones) consider(m.date);
		if (minD.empty()) return E_FAIL;

		const long totalDays = std::max(1L, (DateToDays(maxD) - DateToDays(minD)) + 1);
		const long pad = std::max(1L, (long)(totalDays * 0.05));
		const float chartContentW = (slideW - MARGIN * 2.0f) - ROW_GUTTER;
		const float ptPerDay = chartContentW / (float)(totalDays + pad * 2);

		GanttLayoutResult L = LayoutGantt(doc, minD);
		Scene sc = BuildGanttScene(doc, L, minD, maxD, pad, ptPerDay, slideW, MaterialLight());
		std::vector<PowerPoint::ShapePtr> emitted = RenderScene(shapes, sc);
		count = (int)emitted.size();

		if (emitted.size() >= 2) {
			SAFEARRAY* saf = ::SafeArrayCreateVector(VT_BSTR, 0, (ULONG)emitted.size());
			for (LONG i = 0; i < (LONG)emitted.size(); ++i) {
				_bstr_t nm = emitted[i]->GetName();
				::SafeArrayPutElement(saf, &i, (void*)(BSTR)nm);
			}
			_variant_t idx; idx.vt = VT_ARRAY | VT_BSTR; idx.parray = saf;
			PowerPoint::ShapeRangePtr range = shapes->Range(idx);
			PowerPoint::ShapePtr group = range->Group();
			group->GetTags()->Add(_bstr_t(L"PP_KIND"), _bstr_t(L"CHART_ROOT"));
			group->GetTags()->Add(_bstr_t(L"PP_VERSION"), _bstr_t(L"1"));
			group->GetTags()->Add(_bstr_t(L"PP_DOC"), _bstr_t(Widen(DocumentToJson(doc)).c_str()));
			char proj[192];
			::sprintf_s(proj, "{\"minDay\":%ld,\"pad\":%ld,\"ptPerDay\":%.6f,\"originX\":%.4f}",
				DateToDays(minD), pad, ptPerDay, MARGIN + ROW_GUTTER);
			group->GetTags()->Add(_bstr_t(L"PP_PROJ"), _bstr_t(Widen(proj).c_str()));
		}
	}
	catch (const _com_error& e) {
		if (outShapeCount) *outShapeCount = count;
		return e.Error() ? e.Error() : E_FAIL;
	}
	if (outShapeCount) *outShapeCount = count;
	return S_OK;
}

std::string ReadGanttFromSlide(IDispatch* pApp) {
	if (!pApp) return "";
	try {
		PowerPoint::_ApplicationPtr app(pApp);
		PowerPoint::_SlidePtr slide = app->GetActiveWindow()->GetView()->GetSlide();
		PowerPoint::ShapesPtr shapes = slide->GetShapes();
		long n = shapes->GetCount();
		for (long i = 1; i <= n; ++i) {
			PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
			_bstr_t kind = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (kind.length() && Narrow((const wchar_t*)kind) == "CHART_ROOT")
				return Narrow((const wchar_t*)sh->GetTags()->Item(_bstr_t(L"PP_DOC")));
		}
	} catch (const _com_error&) { return ""; }
	return "";
}

HRESULT ReflowFromSlide(IDispatch* pApp, bool* outChanged) {
	if (outChanged) *outChanged = false;
	if (!pApp) return E_POINTER;
	try {
		PowerPoint::_ApplicationPtr app(pApp);
		PowerPoint::_SlidePtr slide = app->GetActiveWindow()->GetView()->GetSlide();
		PowerPoint::ShapesPtr shapes = slide->GetShapes();
		PowerPoint::ShapePtr group;
		long n = shapes->GetCount();
		for (long i = 1; i <= n; ++i) {
			PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
			_bstr_t k = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (k.length() && Narrow((const wchar_t*)k) == "CHART_ROOT") { group = sh; break; }
		}
		if (!group) return S_FALSE;

		std::string docJson = Narrow((const wchar_t*)group->GetTags()->Item(_bstr_t(L"PP_DOC")));
		std::string projJson = Narrow((const wchar_t*)group->GetTags()->Item(_bstr_t(L"PP_PROJ")));
		if (docJson.empty() || projJson.empty()) return S_FALSE;

		long minDay = 0, pad = 0; float ptPerDay = 1.0f, originX = 0.0f;
		::sscanf_s(projJson.c_str(), "{\"minDay\":%ld,\"pad\":%ld,\"ptPerDay\":%f,\"originX\":%f}", &minDay, &pad, &ptPerDay, &originX);
		if (ptPerDay <= 0.0f) return S_FALSE;

		PpDocument doc = DocumentFromJson(docJson);
		std::map<std::string, std::pair<float, float>> pos;
		PowerPoint::GroupShapesPtr items = group->GetGroupItems();
		long m = items->GetCount();
		for (long i = 1; i <= m; ++i) {
			PowerPoint::ShapePtr ch = items->Item(_variant_t(i));
			_bstr_t k = ch->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (!k.length() || Narrow((const wchar_t*)k) != "TASK") continue;
			_bstr_t id = ch->GetTags()->Item(_bstr_t(L"PP_ID"));
			pos[Narrow((const wchar_t*)id)] = { ch->GetLeft(), ch->GetWidth() };
		}

		bool changed = false;
		for (auto& t : doc.tasks) {
			auto it = pos.find(t.id);
			if (it == pos.end()) continue;
			const float left = it->second.first, width = it->second.second;
			long startDay = minDay - pad + (long)::llround((left - originX) / ptPerDay);
			long widthDays = (long)::llround(width / ptPerDay);
			if (widthDays < 1) widthDays = 1;
			std::string ns = DaysToDate(startDay), ne = DaysToDate(startDay + widthDays - 1);
			if (ns != t.start || ne != t.end) { t.start = ns; t.end = ne; changed = true; }
		}

		if (changed) { group->Delete(); int cnt = 0; InsertGantt(pApp, doc, &cnt); }
		if (outChanged) *outChanged = changed;
		return S_OK;
	}
	catch (const _com_error&) { return E_FAIL; }
}
