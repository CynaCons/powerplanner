#include "pch.h"
#include "GanttBuilder.h"
#include "GanttLayout.h"
#include "GanttJson.h"
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

// "#RRGGBB" -> PowerPoint BGR (Office::MsoRGBType). Falls back on bad input.
static Office::MsoRGBType HexToBgr(const std::string& hex, long fallback) {
	std::string s = hex;
	if (!s.empty() && s[0] == '#') s.erase(0, 1);
	if (s.size() != 6) return (Office::MsoRGBType)fallback;
	long r = std::strtol(s.substr(0, 2).c_str(), nullptr, 16);
	long g = std::strtol(s.substr(2, 2).c_str(), nullptr, 16);
	long b = std::strtol(s.substr(4, 2).c_str(), nullptr, 16);
	return (Office::MsoRGBType)((b << 16) | (g << 8) | r);
}

// Darken a BGR colour by a factor (0..1) for the percent-complete inset.
static Office::MsoRGBType DarkenBgr(Office::MsoRGBType bgr, double f) {
	long v = (long)bgr;
	long b = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, r = v & 0xFF;
	r = (long)(r * f); g = (long)(g * f); b = (long)(b * f);
	return (Office::MsoRGBType)((b << 16) | (g << 8) | r);
}

static const char* kMonthNames[] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };

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
		{ "t1", "Discovery",      "2026-06-01", "2026-06-12", "r_research", "#818CF8", 100 },
		{ "t2", "Interviews",     "2026-06-08", "2026-06-19", "r_research", "#818CF8", 60 },
		{ "t3", "Wireframes",     "2026-06-15", "2026-06-26", "r_design",   "#34D399", 40 },
		{ "t4", "Visual design",  "2026-06-22", "2026-07-10", "r_design",   "#34D399", 10 },
		{ "t5", "Implementation", "2026-07-06", "2026-07-31", "r_build",    "#F472B6", 0  },
		{ "t6", "QA + polish",    "2026-07-27", "2026-08-07", "r_build",    "#F472B6", 0  },
	};
	doc.milestones = {
		{ "m1", "Design freeze", "2026-07-10", "r_design",  "#FBBF24" },
		{ "m2", "Ship",          "2026-08-10", "r_launch",  "#FBBF24" },
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

// ---- emission --------------------------------------------------------------

namespace {
const float MARGIN     = 36.0f;
const float ROW_GUTTER = 110.0f;
const float ROW_HEIGHT = 30.0f;   // one row slot, in points
const float AXIS_H     = 24.0f;
const float BAR_INSET  = 6.0f;

void SetTag(PowerPoint::ShapePtr s, const wchar_t* k, const std::wstring& v) {
	s->GetTags()->Add(_bstr_t(k), _bstr_t(v.c_str()));
}
}

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
		const float chartW = slideW - MARGIN * 2.0f;
		const float chartContentW = chartW - ROW_GUTTER;
		const float ptPerDay = chartContentW / (float)(totalDays + pad * 2);
		const float chartTop = MARGIN + AXIS_H;

		// Spec-conformant abstract layout; viewStart = min date (xDay >= 0).
		GanttLayoutResult L = LayoutGantt(doc, minD);

		auto xToPt = [&](long xDay) { return MARGIN + ROW_GUTTER + (float)(xDay + pad) * ptPerDay; };
		auto slotTop = [&](int slot) { return chartTop + (float)slot * ROW_HEIGHT; };

		std::vector<PowerPoint::ShapePtr> emitted;
		auto track = [&](PowerPoint::ShapePtr s) { emitted.push_back(s); ++count; };

		const float chartBottom = chartTop + (float)L.chartRows * ROW_HEIGHT;

		// Title.
		{
			auto title = shapes->AddTextbox(Office::msoTextOrientationHorizontal, MARGIN, MARGIN - 22.0f, chartW, 20.0f);
			title->GetTextFrame()->GetTextRange()->PutText(_bstr_t(Widen(doc.title).c_str()));
			SetTag(title, L"PP_KIND", L"TITLE");
			track(title);
		}

		// Time axis: month gridlines + labels (emitted first so bars draw on top).
		{
			int y0 = 0, m0 = 0, dd = 0, y1 = 0, m1 = 0;
			sscanf_s(minD.c_str(), "%d-%d-%d", &y0, &m0, &dd);
			sscanf_s(maxD.c_str(), "%d-%d-%d", &y1, &m1, &dd);
			int yy = y0, mm = m0;
			while (yy < y1 || (yy == y1 && mm <= m1)) {
				char iso[16]; sprintf_s(iso, "%04d-%02d-01", yy, mm);
				float x = xToPt(DateToDays(iso) - DateToDays(minD));
				if (x >= MARGIN + ROW_GUTTER - 1.0f) {
					auto line = shapes->AddConnector(Office::msoConnectorStraight, x, chartTop, x, chartBottom);
					line->GetLine()->GetForeColor()->PutPpRGB((Office::MsoRGBType)0x00E5E5E5);
					line->GetLine()->PutWeight(0.5f);
					SetTag(line, L"PP_KIND", L"AXIS_GRID");
					track(line);
					char lbl[24]; sprintf_s(lbl, "%s %d", kMonthNames[(mm - 1) % 12], yy);
					auto t = shapes->AddTextbox(Office::msoTextOrientationHorizontal, x + 2.0f, chartTop - 16.0f, 64.0f, 13.0f);
					auto tr = t->GetTextFrame()->GetTextRange();
					tr->PutText(_bstr_t(lbl));
					tr->GetFont()->PutSize(10.0f);
					tr->GetFont()->GetColor()->PutPpRGB((Office::MsoRGBType)0x00909090);
					SetTag(t, L"PP_KIND", L"AXIS_LABEL");
					track(t);
				}
				if (++mm > 12) { mm = 1; ++yy; }
			}
		}

		// Row labels (visible rows, in layout order).
		for (size_t i = 0; i < L.visibleRowIds.size(); ++i) {
			std::string rid = L.visibleRowIds[i];
			std::string label;
			std::string groupId;
			for (const auto& r : doc.rows) if (r.id == rid) { label = r.label; groupId = r.groupId; }
			float indent = groupId.empty() ? 0.0f : 10.0f;
			auto lbl = shapes->AddTextbox(Office::msoTextOrientationHorizontal,
				MARGIN + indent, slotTop(L.rowOffsets[i]) + 5.0f, ROW_GUTTER - 8.0f - indent, 18.0f);
			lbl->GetTextFrame()->GetTextRange()->PutText(_bstr_t(Widen(label).c_str()));
			SetTag(lbl, L"PP_KIND", L"ROW_LABEL");
			SetTag(lbl, L"PP_ID", Widen(rid));
			track(lbl);
		}

		// Summary bars (thin).
		for (const auto& s : L.summaries) {
			float left = xToPt(s.xDay), width = std::max(2.0f, s.widthDays * ptPerDay);
			float top = slotTop(s.rowIndex) + ROW_HEIGHT / 2.0f - 3.0f;
			auto bar = shapes->AddShape(Office::msoShapeRectangle, left, top, width, 6.0f);
			bar->GetFill()->GetForeColor()->PutPpRGB((Office::MsoRGBType)0x00666666);
			bar->GetLine()->GetForeColor()->PutPpRGB((Office::MsoRGBType)0x00444444);
			bar->GetLine()->PutWeight(0.5f);
			SetTag(bar, L"PP_KIND", L"SUMMARY");
			SetTag(bar, L"PP_ID", Widen(s.rowId));
			track(bar);
		}

		// Task colour lookup.
		std::map<std::string, const PpTask*> taskById;
		for (const auto& t : doc.tasks) taskById[t.id] = &t;

		// Task bars.
		std::map<std::string, const LaidTask*> laidById;
		for (const auto& lt : L.tasks) laidById[lt.id] = &lt;
		for (const auto& lt : L.tasks) {
			const PpTask* t = taskById[lt.id];
			float left = xToPt(lt.xDay), width = std::max(2.0f, lt.widthDays * ptPerDay);
			float top = slotTop(L.rowOffsets[lt.rowIndex] + lt.subRow) + BAR_INSET;
			float height = ROW_HEIGHT - BAR_INSET * 2.0f;
			auto bar = shapes->AddShape(Office::msoShapeRoundedRectangle, left, top, width, height);
			bar->GetFill()->GetForeColor()->PutPpRGB(HexToBgr(t->color, 0x00F88C81));
			bar->GetLine()->GetForeColor()->PutPpRGB(HexToBgr(t->color, 0x00F88C81));
			bar->GetLine()->PutWeight(0.5f);
			if (width > 60.0f) bar->GetTextFrame()->GetTextRange()->PutText(_bstr_t(Widen(t->label).c_str()));
			SetTag(bar, L"PP_KIND", L"TASK");
			SetTag(bar, L"PP_ID", Widen(t->id));
			track(bar);

			// Percent-complete: a darker strip along the bottom (label stays readable).
			if (t->percent > 0) {
				float pw = width * (float)t->percent / 100.0f;
				if (pw > 1.5f) {
					auto prog = shapes->AddShape(Office::msoShapeRectangle, left, top + height - 3.5f, pw, 3.5f);
					prog->GetFill()->GetForeColor()->PutPpRGB(DarkenBgr(HexToBgr(t->color, 0x00F88C81), 0.6));
					prog->GetLine()->PutVisible(Office::msoFalse);
					SetTag(prog, L"PP_KIND", L"TASK_PROGRESS");
					SetTag(prog, L"PP_ID", Widen(t->id));
					track(prog);
				}
			}
		}

		// Milestones (diamonds, centered in the row).
		for (const auto& m : L.milestones) {
			float cx = xToPt(m.xDay) + ptPerDay / 2.0f;
			float cy = slotTop(L.rowOffsets[m.rowIndex]) + ROW_HEIGHT / 2.0f;
			float sz = 14.0f;
			auto dia = shapes->AddShape(Office::msoShapeDiamond, cx - sz / 2.0f, cy - sz / 2.0f, sz, sz);
			std::string color, label;
			for (const auto& md : doc.milestones) if (md.id == m.id) { color = md.color; label = md.label; }
			dia->GetFill()->GetForeColor()->PutPpRGB(HexToBgr(color, 0x0024BFFB));
			dia->GetLine()->GetForeColor()->PutPpRGB((Office::MsoRGBType)0x00000000);
			dia->GetLine()->PutWeight(0.5f);
			SetTag(dia, L"PP_KIND", L"MILESTONE");
			SetTag(dia, L"PP_ID", Widen(m.id));
			track(dia);

			// Milestone label beside the diamond.
			if (!label.empty()) {
				auto ml = shapes->AddTextbox(Office::msoTextOrientationHorizontal, cx + sz / 2.0f + 2.0f, cy - 8.0f, 90.0f, 14.0f);
				auto tr = ml->GetTextFrame()->GetTextRange();
				tr->PutText(_bstr_t(Widen(label).c_str()));
				tr->GetFont()->PutSize(10.0f);
				tr->GetFont()->GetColor()->PutPpRGB((Office::MsoRGBType)0x00404040);
				SetTag(ml, L"PP_KIND", L"MILESTONE_LABEL");
				track(ml);
			}
		}

		// Brackets (outline rectangle over the spanned rows).
		for (const auto& b : L.brackets) {
			float left = xToPt(b.xDay), width = std::max(2.0f, b.widthDays * ptPerDay);
			float top = slotTop(L.rowOffsets[b.topRow]) + 2.0f;
			int bottomSlot = L.rowOffsets[b.bottomRow] + L.rowSlots[b.bottomRow];
			float height = std::max(8.0f, slotTop(bottomSlot) - top - 2.0f);
			auto br = shapes->AddShape(Office::msoShapeRectangle, left, top, width, height);
			br->GetFill()->PutVisible(Office::msoFalse);
			br->GetLine()->GetForeColor()->PutPpRGB((Office::MsoRGBType)0x00B89A4F);
			br->GetLine()->PutWeight(1.0f);
			SetTag(br, L"PP_KIND", L"BRACKET");
			SetTag(br, L"PP_ID", Widen(b.id));
			track(br);

			// Bracket label.
			std::string label;
			for (const auto& bd : doc.brackets) if (bd.id == b.id) label = bd.label;
			if (!label.empty()) {
				auto bl = shapes->AddTextbox(Office::msoTextOrientationHorizontal, left + 3.0f, top + 1.0f, width - 6.0f, 14.0f);
				auto tr = bl->GetTextFrame()->GetTextRange();
				tr->PutText(_bstr_t(Widen(label).c_str()));
				tr->GetFont()->PutSize(10.0f);
				tr->GetFont()->GetColor()->PutPpRGB((Office::MsoRGBType)0x00B89A4F);
				SetTag(bl, L"PP_KIND", L"BRACKET_LABEL");
				track(bl);
			}
		}

		// Dependencies (elbow connectors, arrow at successor).
		std::map<std::string, std::pair<std::string, std::string>> depEnds;
		for (const auto& d : doc.deps) depEnds[d.id] = { d.from, d.to };
		for (const auto& d : L.dependencies) {
			auto ends = depEnds.find(d.id);
			if (ends == depEnds.end()) continue;
			const LaidTask* fr = laidById.count(ends->second.first) ? laidById[ends->second.first] : nullptr;
			const LaidTask* to = laidById.count(ends->second.second) ? laidById[ends->second.second] : nullptr;
			if (!fr || !to) continue;
			float x1 = xToPt(d.fromXDay);
			float y1 = slotTop(L.rowOffsets[fr->rowIndex] + fr->subRow) + ROW_HEIGHT / 2.0f;
			float x2 = xToPt(d.toXDay);
			float y2 = slotTop(L.rowOffsets[to->rowIndex] + to->subRow) + ROW_HEIGHT / 2.0f;
			auto conn = shapes->AddConnector(Office::msoConnectorElbow,
				std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2));
			conn->GetLine()->GetForeColor()->PutPpRGB(HexToBgr("#94A3B8", 0x00B8A394));
			conn->GetLine()->PutWeight(0.75f);
			conn->GetLine()->PutEndArrowheadStyle(Office::msoArrowheadTriangle);
			SetTag(conn, L"PP_KIND", L"DEP");
			SetTag(conn, L"PP_ID", Widen(d.id));
			track(conn);
		}

		// Group everything under a tagged chart root.
		if (emitted.size() >= 2) {
			SAFEARRAY* sa = ::SafeArrayCreateVector(VT_BSTR, 0, (ULONG)emitted.size());
			for (LONG i = 0; i < (LONG)emitted.size(); ++i) {
				_bstr_t nm = emitted[i]->GetName();
				::SafeArrayPutElement(sa, &i, (void*)(BSTR)nm);
			}
			_variant_t idx;
			idx.vt = VT_ARRAY | VT_BSTR;
			idx.parray = sa;
			PowerPoint::ShapeRangePtr range = shapes->Range(idx);
			PowerPoint::ShapePtr group = range->Group();
			group->GetTags()->Add(_bstr_t(L"PP_KIND"), _bstr_t(L"CHART_ROOT"));
			group->GetTags()->Add(_bstr_t(L"PP_VERSION"), _bstr_t(L"1"));
			group->GetTags()->Add(_bstr_t(L"PP_DOC"), _bstr_t(Widen(DocumentToJson(doc)).c_str()));
			// Projection params for the inverse mapping (N5 reflow).
			char proj[192];
			::sprintf_s(proj, "{\"minDay\":%ld,\"pad\":%ld,\"ptPerDay\":%.6f,\"originX\":%.4f}",
				DateToDays(minD), pad, ptPerDay, MARGIN + ROW_GUTTER);
			group->GetTags()->Add(_bstr_t(L"PP_PROJ"), _bstr_t(Widen(proj).c_str()));
			// SAFEARRAY freed by _variant_t destructor.
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
			if (kind.length() && Narrow((const wchar_t*)kind) == "CHART_ROOT") {
				_bstr_t doc = sh->GetTags()->Item(_bstr_t(L"PP_DOC"));
				return Narrow((const wchar_t*)doc);
			}
		}
	} catch (const _com_error&) {
		return "";
	}
	return "";
}

HRESULT ReflowFromSlide(IDispatch* pApp, bool* outChanged) {
	if (outChanged) *outChanged = false;
	if (!pApp) return E_POINTER;
	try {
		PowerPoint::_ApplicationPtr app(pApp);
		PowerPoint::_SlidePtr slide = app->GetActiveWindow()->GetView()->GetSlide();
		PowerPoint::ShapesPtr shapes = slide->GetShapes();

		// Find the chart root group.
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
		::sscanf_s(projJson.c_str(), "{\"minDay\":%ld,\"pad\":%ld,\"ptPerDay\":%f,\"originX\":%f}",
			&minDay, &pad, &ptPerDay, &originX);
		if (ptPerDay <= 0.0f) return S_FALSE;

		PpDocument doc = DocumentFromJson(docJson);

		// Read task shape positions from the group's children (PP_ID -> left,width).
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

		// Invert positions -> dates; update the document.
		bool changed = false;
		for (auto& t : doc.tasks) {
			auto it = pos.find(t.id);
			if (it == pos.end()) continue;
			const float left = it->second.first, width = it->second.second;
			long startDay = minDay - pad + (long)::llround((left - originX) / ptPerDay);
			long widthDays = (long)::llround(width / ptPerDay);
			if (widthDays < 1) widthDays = 1;
			std::string ns = DaysToDate(startDay);
			std::string ne = DaysToDate(startDay + widthDays - 1);
			if (ns != t.start || ne != t.end) { t.start = ns; t.end = ne; changed = true; }
		}

		if (changed) {
			group->Delete();
			int cnt = 0;
			InsertGantt(pApp, doc, &cnt);
		}
		if (outChanged) *outChanged = changed;
		return S_OK;
	}
	catch (const _com_error&) {
		return E_FAIL;
	}
}
