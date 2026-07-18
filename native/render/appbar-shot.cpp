// S2 app-bar screenshot harness (demo/review tooling — NOT part of any gate).
//
// Builds the mockup-faithful "Q3 Launch Plan" chart in a VISIBLE PowerPoint,
// starts the on-slide overlay (so the docked bottom app bar renders), forces
// host-active so the chrome shows without foreground fights, pumps the 150ms
// tick a few times, then screen-captures the app-bar window (tight crop) and a
// wider "docked over the chart" region to PNGs under native/build/.
//
// Like showcase.cpp this deliberately builds its OWN document (never touches the
// frozen MakeSampleDocument fixture) and leaves PowerPoint open.
#include "../PowerPlannerAddin/pch.h"
#include "../PowerPlannerAddin/GanttBuilder.h"
#include "../PowerPlannerAddin/GanttJson.h"
#include "../PowerPlannerAddin/GanttModel.h"
#include "../PowerPlannerAddin/GanttOps.h"
#include "../PowerPlannerAddin/Overlay.h"
#include "../PowerPlannerAddin/ThemeMenu.h"
#include "../PowerPlannerAddin/GanttHitTest.h"
#include "../PowerPlannerAddin/GanttAppBar.h"
#include "../PowerPlannerAddin/GanttLayout.h"
// GDI+ headers use unqualified min/max; provide them if a prior include pulled
// in NOMINMAX (matches how the addin's own GDI+ TU gets them from windows.h).
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif
#include <gdiplus.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <utility>
#include <memory>

#pragma comment(lib, "gdiplus.lib")

static void SetHarnessDpiAwareness() {
	HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
	if (user32) {
		typedef BOOL(WINAPI * SetProcessDpiAwarenessContextFn)(DPI_AWARENESS_CONTEXT);
		auto pSetCtx = (SetProcessDpiAwarenessContextFn)::GetProcAddress(user32, "SetProcessDpiAwarenessContext");
		if (pSetCtx && pSetCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
	}
	::SetProcessDPIAware();
}

static PpDocument MakeShowcaseDocument() {
	PpDocument doc;
	doc.title = "";
	doc.scale = "week";
	doc.rows = {
		{ "phase1",   "Phase 1", "" },
		{ "research", "Research", "phase1" },
		{ "design",   "Design",   "phase1" },
		{ "impl",     "",         "" },
		{ "qa",       "",         "" },
		{ "launch",   "Launch",   "" },
	};
	doc.tasks = {
		{ "discovery",  "Discovery",      "2026-06-01", "2026-06-12", "research", "#4355E0", 100, "" },
		{ "interviews", "Interviews",     "2026-06-08", "2026-06-19", "research", "#4355E0", 60,  "" },
		{ "wireframes", "Wireframes",     "2026-06-15", "2026-06-26", "design",   "#0E8D8A", 40,  "" },
		{ "visual",     "Visual design",  "2026-06-22", "2026-07-10", "design",   "#0E8D8A", 10,  "" },
		{ "impl_t",     "Implementation", "2026-07-06", "2026-07-31", "impl",     "#7A4FA3", 0,   "rail" },
		{ "qa_t",       "QA + polish",    "2026-07-27", "2026-08-07", "qa",       "#5B6C8F", 0,   "rail" },
	};
	doc.milestones = {
		{ "m_freeze", "Design freeze", "2026-07-10", "design", "" },
		{ "m_ship",   "Ship",          "2026-08-10", "launch", "" },
	};
	doc.markers = {
		{ "today",    "today",    "TODAY",        "2026-07-04", "" },
		{ "deadline", "deadline", "BOARD REVIEW", "2026-07-30", "" },
	};
	doc.texts = {
		{ "note1", "Go/No-Go with exec team", "m_ship", "", "", "", 14.0f, -30.0f },
	};
	return doc;
}

static void PumpFor(DWORD ms) {
	DWORD start = ::GetTickCount();
	MSG msg;
	while (::GetTickCount() - start < ms) {
		while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
			::TranslateMessage(&msg);
			::DispatchMessageW(&msg);
		}
		::Sleep(15);
	}
}

static int PngEncoderClsid(CLSID* clsid) {
	UINT num = 0, size = 0;
	Gdiplus::GetImageEncodersSize(&num, &size);
	if (size == 0) return -1;
	std::vector<BYTE> buf(size);
	auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
	Gdiplus::GetImageEncoders(num, size, codecs);
	for (UINT i = 0; i < num; ++i) {
		if (wcscmp(codecs[i].MimeType, L"image/png") == 0) { *clsid = codecs[i].Clsid; return (int)i; }
	}
	return -1;
}

static bool RectContainsRect(const RECT& outer, const RECT& inner) {
	return inner.left >= outer.left && inner.top >= outer.top &&
		inner.right <= outer.right && inner.bottom <= outer.bottom;
}

static bool GetChartRootScreenRect(PowerPoint::_ApplicationPtr& app, RECT* out) {
	if (!out) return false;
	try {
		PowerPoint::DocumentWindowPtr w = app->GetActiveWindow();
		PowerPoint::_SlidePtr sl = w->GetView()->GetSlide();
		PowerPoint::ShapesPtr shs = sl->GetShapes();
		long nn = shs->GetCount();
		for (long ii = 1; ii <= nn; ++ii) {
			PowerPoint::ShapePtr s = shs->Item(_variant_t(ii));
			_bstr_t k = s->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (k.length() && std::string((const char*)_bstr_t(k)) == "CHART_ROOT") {
				float cl = s->GetLeft(), ct = s->GetTop(), cw = s->GetWidth(), chh = s->GetHeight();
				out->left = w->PointsToScreenPixelsX(cl);
				out->top = w->PointsToScreenPixelsY(ct);
				out->right = w->PointsToScreenPixelsX(cl + cw);
				out->bottom = w->PointsToScreenPixelsY(ct + chh);
				return true;
			}
		}
	} catch (...) {}
	return false;
}

static void SelectChartRootNatively(PowerPoint::_SlidePtr& slide) {
	try {
		PowerPoint::ShapesPtr shs = slide->GetShapes();
		long nn = shs->GetCount();
		for (long ii = 1; ii <= nn; ++ii) {
			auto s = shs->Item(_variant_t(ii));
			_bstr_t k = s->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (k.length() && std::string((const char*)_bstr_t(k)) == "CHART_ROOT") {
				s->Select(Office::msoTrue);
				break;
			}
		}
	} catch (...) {}
}

// Natively select a chart CHILD by PP_KIND + PP_ID so the overlay suppression
// path (Tick poll / COM sink via Overlay_OnNativeSelectionChanged) can be
// exercised. Returns the observed native selection PP_KIND immediately after
// Select() (before any tick suppresses it), or "" on failure.
static std::string SelectChartChildNatively(PowerPoint::_SlidePtr& slide,
	const char* kindWant, const char* idWant) {
	if (!kindWant || !*kindWant) return "";
	try {
		PowerPoint::ShapesPtr shs = slide->GetShapes();
		long nn = shs->GetCount();
		for (long ii = 1; ii <= nn; ++ii) {
			PowerPoint::ShapePtr root = shs->Item(_variant_t(ii));
			_bstr_t rk = root->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (!rk.length() || std::string((const char*)_bstr_t(rk)) != "CHART_ROOT") continue;
			PowerPoint::GroupShapesPtr items = root->GetGroupItems();
			long n = items->GetCount();
			for (long i = 1; i <= n; ++i) {
				PowerPoint::ShapePtr ch = items->Item(_variant_t(i));
				_bstr_t k = ch->GetTags()->Item(_bstr_t(L"PP_KIND"));
				if (!k.length() || std::string((const char*)_bstr_t(k)) != kindWant) continue;
				_bstr_t id = ch->GetTags()->Item(_bstr_t(L"PP_ID"));
				if (idWant && *idWant && (!id.length() || std::string((const char*)_bstr_t(id)) != idWant)) continue;
				try { ch->Select(Office::msoTrue); } catch (...) { return ""; }
				return std::string((const char*)_bstr_t(k));
			}
		}
	} catch (...) {}
	return "";
}

static bool MoveChartChildBy(PowerPoint::_SlidePtr& slide, const char* kindWant,
	const char* idWant, float dx, float dy) {
	if (!kindWant || !*kindWant || !idWant || !*idWant) return false;
	try {
		PowerPoint::ShapesPtr shs = slide->GetShapes();
		for (long ii = 1; ii <= shs->GetCount(); ++ii) {
			PowerPoint::ShapePtr root = shs->Item(_variant_t(ii));
			_bstr_t rk = root->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (!rk.length() || std::string((const char*)_bstr_t(rk)) != "CHART_ROOT") continue;
			PowerPoint::GroupShapesPtr items = root->GetGroupItems();
			for (long i = 1; i <= items->GetCount(); ++i) {
				PowerPoint::ShapePtr ch = items->Item(_variant_t(i));
				_bstr_t k = ch->GetTags()->Item(_bstr_t(L"PP_KIND"));
				_bstr_t id = ch->GetTags()->Item(_bstr_t(L"PP_ID"));
				if (k.length() && id.length()
					&& std::string((const char*)_bstr_t(k)) == kindWant
					&& std::string((const char*)_bstr_t(id)) == idWant) {
					ch->PutLeft(ch->GetLeft() + dx);
					ch->PutTop(ch->GetTop() + dy);
					return true;
				}
			}
		}
	} catch (...) {}
	return false;
}

// v2.6.1 component-shape-protection: natively select a TASK bar child.
static std::string SelectTaskChildNatively(PowerPoint::_SlidePtr& slide, const char* taskId) {
	return SelectChartChildNatively(slide, "TASK", taskId);
}

// Screen rect of a chart child by PP_KIND + PP_ID (TASK_LABEL, TASK_PROGRESS, …).
static bool FindChartChildRect(PowerPoint::_ApplicationPtr& app,
	const char* kindWant, const char* idWant, RECT* out) {
	if (!out || !kindWant || !*kindWant || !idWant || !*idWant) return false;
	try {
		PowerPoint::DocumentWindowPtr w = app->GetActiveWindow();
		PowerPoint::_SlidePtr sl = w->GetView()->GetSlide();
		PowerPoint::ShapesPtr shs = sl->GetShapes();
		long nn = shs->GetCount();
		for (long ii = 1; ii <= nn; ++ii) {
			PowerPoint::ShapePtr root = shs->Item(_variant_t(ii));
			_bstr_t rk = root->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (!rk.length() || std::string((const char*)_bstr_t(rk)) != "CHART_ROOT") continue;
			PowerPoint::GroupShapesPtr items = root->GetGroupItems();
			long n = items->GetCount();
			for (long i = 1; i <= n; ++i) {
				PowerPoint::ShapePtr ch = items->Item(_variant_t(i));
				_bstr_t k = ch->GetTags()->Item(_bstr_t(L"PP_KIND"));
				if (!k.length() || std::string((const char*)_bstr_t(k)) != kindWant) continue;
				_bstr_t id = ch->GetTags()->Item(_bstr_t(L"PP_ID"));
				if (!id.length() || std::string((const char*)_bstr_t(id)) != idWant) continue;
				float l = ch->GetLeft(), t = ch->GetTop(), ww = ch->GetWidth(), h = ch->GetHeight();
				out->left = w->PointsToScreenPixelsX(l);
				out->top = w->PointsToScreenPixelsY(t);
				out->right = w->PointsToScreenPixelsX(l + ww);
				out->bottom = w->PointsToScreenPixelsY(t + h);
				return true;
			}
		}
	} catch (...) {}
	return false;
}

// Read the current native selection's first-shape PP_KIND (""/"CHART_ROOT"/child)
// and count, for honest before/after logging in the protection trace.
static std::string CurrentNativeSelKind(PowerPoint::_ApplicationPtr& app, long* outCount) {
	if (outCount) *outCount = 0;
	try {
		PowerPoint::DocumentWindowPtr w = app->GetActiveWindow();
		if (!w) return "";
		PowerPoint::SelectionPtr sel = w->GetSelection();
		if (!sel || sel->GetType() != PowerPoint::ppSelectionShapes) return "";
		PowerPoint::ShapeRangePtr sr = sel->GetShapeRange();
		if (!sr || sr->GetCount() < 1) return "";
		if (outCount) *outCount = sr->GetCount();
		PowerPoint::ShapePtr sh = sr->Item(_variant_t(1L));
		_bstr_t k = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
		return k.length() ? std::string((const char*)_bstr_t(k)) : "";
	} catch (...) {}
	return "";
}

static AppBarSel AppBarSelFromKind(const char* kind) {
	if (!kind || !*kind) return AppBarSel::None;
	if (strcmp(kind, "TASK") == 0) return AppBarSel::Task;
	if (strcmp(kind, "ROW") == 0) return AppBarSel::Row;
	if (strcmp(kind, "MILESTONE") == 0) return AppBarSel::Milestone;
	if (strcmp(kind, "MARKER") == 0) return AppBarSel::Marker;
	if (strcmp(kind, "TEXT") == 0 || strcmp(kind, "NOTE") == 0) return AppBarSel::Note;
	if (strcmp(kind, "DEP") == 0) return AppBarSel::Dependency;
	return AppBarSel::None;
}

static bool ParseRowBandFromDump(const char* json, const char* rowId, RECT* out) {
	if (!json || !rowId || !out) return false;
	char needle[64];
	::snprintf(needle, sizeof(needle), "\"rowId\":\"%s\"", rowId);
	const char* p = ::strstr(json, needle);
	while (p) {
		const char* leftKey = ::strstr(p, "\"left\":");
		const char* topKey = ::strstr(p, "\"top\":");
		const char* rightKey = ::strstr(p, "\"right\":");
		const char* bottomKey = ::strstr(p, "\"bottom\":");
		if (leftKey && topKey && rightKey && bottomKey
			&& leftKey < topKey && topKey < rightKey && rightKey < bottomKey) {
			long l = 0, t = 0, r = 0, b = 0;
			if (::sscanf_s(leftKey, "\"left\":%ld", &l) == 1
				&& ::sscanf_s(topKey, "\"top\":%ld", &t) == 1
				&& ::sscanf_s(rightKey, "\"right\":%ld", &r) == 1
				&& ::sscanf_s(bottomKey, "\"bottom\":%ld", &b) == 1) {
				out->left = l; out->top = t; out->right = r; out->bottom = b;
				return true;
			}
		}
		p = ::strstr(p + 1, needle);
	}
	return false;
}

static void PumpHoverQuickAddTask(HWND ov, const char* rowId) {
	RECT band{};
	const char* dump = Overlay_DumpChromeStateForTest();
	if (!ParseRowBandFromDump(dump, rowId, &band) || !ov) return;
	const int cy = (band.top + band.bottom) / 2;
	POINT bandPt = { (band.left + band.right) / 2, cy };
	Overlay_SetCursorPosOverrideForTest(true, bandPt);
	POINT clientPt = bandPt;
	::ScreenToClient(ov, &clientPt);
	::PostMessageW(ov, WM_MOUSEMOVE, 0, MAKELPARAM((short)clientPt.x, (short)clientPt.y));
	PumpFor(900);
	POINT chipPt = { band.left - 14, cy };
	Overlay_SetCursorPosOverrideForTest(true, chipPt);
	::ScreenToClient(ov, &chipPt);
	LPARAM clickLp = MAKELPARAM((short)chipPt.x, (short)chipPt.y);
	::PostMessageW(ov, WM_MOUSEMOVE, 0, clickLp);
	PumpFor(120);
	::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, clickLp);
	::PostMessageW(ov, WM_LBUTTONUP, 0, clickLp);
}

static bool FindTaskBodyRect(PowerPoint::_ApplicationPtr& app, const char* taskId, RECT* out);

static bool FindTaskLinkPort(PowerPoint::_ApplicationPtr& app, const char* taskId, bool rightPort, POINT* outScreen) {
	RECT r{};
	if (!FindTaskBodyRect(app, taskId, &r) || !outScreen) return false;
	const int gap = 10;
	outScreen->x = rightPort ? (r.right + gap) : (r.left - gap);
	outScreen->y = (r.top + r.bottom) / 2;
	return true;
}

static bool FindTaskBodyCenter(PowerPoint::_ApplicationPtr& app, const char* taskId, POINT* outScreen) {
	if (!outScreen || !taskId || !*taskId) return false;
	try {
		PowerPoint::DocumentWindowPtr w = app->GetActiveWindow();
		PowerPoint::_SlidePtr sl = w->GetView()->GetSlide();
		PowerPoint::ShapesPtr shs = sl->GetShapes();
		long nn = shs->GetCount();
		for (long ii = 1; ii <= nn; ++ii) {
			PowerPoint::ShapePtr root = shs->Item(_variant_t(ii));
			_bstr_t rk = root->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (!rk.length() || std::string((const char*)_bstr_t(rk)) != "CHART_ROOT") continue;
			PowerPoint::GroupShapesPtr items = root->GetGroupItems();
			long n = items->GetCount();
			for (long i = 1; i <= n; ++i) {
				PowerPoint::ShapePtr ch = items->Item(_variant_t(i));
				_bstr_t k = ch->GetTags()->Item(_bstr_t(L"PP_KIND"));
				if (!k.length() || std::string((const char*)_bstr_t(k)) != "TASK") continue;
				_bstr_t id = ch->GetTags()->Item(_bstr_t(L"PP_ID"));
				if (!id.length() || std::string((const char*)_bstr_t(id)) != taskId) continue;
				float l = ch->GetLeft(), t = ch->GetTop(), ww = ch->GetWidth(), h = ch->GetHeight();
				outScreen->x = w->PointsToScreenPixelsX(l + ww * 0.5f);
				outScreen->y = w->PointsToScreenPixelsY(t + h * 0.5f);
				return true;
			}
		}
	} catch (...) {}
	return false;
}

static void PostTaskBodyDrag(HWND ov, POINT screenCenter, int dragPx) {
	if (!ov) return;
	POINT clientPt = screenCenter;
	::ScreenToClient(ov, &clientPt);
	Overlay_SetCursorPosOverrideForTest(true, screenCenter);
	LPARAM downLp = MAKELPARAM((short)clientPt.x, (short)clientPt.y);
	::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, downLp);
	PumpFor(60);
	const int steps = 5;
	for (int s = 1; s <= steps; ++s) {
		int mx = clientPt.x + dragPx * s / steps;
		POINT screenPt = { screenCenter.x + dragPx * s / steps, screenCenter.y };
		Overlay_SetCursorPosOverrideForTest(true, screenPt);
		::PostMessageW(ov, WM_MOUSEMOVE, 0, MAKELPARAM((short)mx, (short)clientPt.y));
		PumpFor(40);
	}
	int finalX = clientPt.x + dragPx;
	POINT upScreen = { screenCenter.x + dragPx, screenCenter.y };
	Overlay_SetCursorPosOverrideForTest(true, upScreen);
	::PostMessageW(ov, WM_LBUTTONUP, 0, MAKELPARAM((short)finalX, (short)clientPt.y));
	PumpFor(400);
}

static bool FindTaskBodyRect(PowerPoint::_ApplicationPtr& app, const char* taskId, RECT* out);
static bool ParseNamedRectFromDump(const char* json, const char* key, RECT* out);

// ---- W3 time-window trace helpers ------------------------------------------

// Late-bound Invoke (idiom from native/render/undo-probe.cpp's InvokeName,
// trimmed: no exception-detail capture needed for the trace profiles).
static HRESULT InvokeNameW3(IDispatch* disp, LPCWSTR name, WORD flags,
	VARIANT* args, UINT cArgs, VARIANT* result) {
	DISPID dispid = 0;
	LPOLESTR nameOle = const_cast<LPOLESTR>(name);
	HRESULT hr = disp->GetIDsOfNames(IID_NULL, &nameOle, 1, LOCALE_USER_DEFAULT, &dispid);
	if (FAILED(hr)) return hr;
	DISPPARAMS dp = {};
	dp.rgvarg = args;
	dp.cArgs = cArgs;
	EXCEPINFO ei = {};
	UINT argErr = 0;
	hr = disp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, flags, &dp, result, &ei, &argErr);
	if (ei.bstrDescription) ::SysFreeString(ei.bstrDescription);
	if (ei.bstrSource) ::SysFreeString(ei.bstrSource);
	if (ei.bstrHelpFile) ::SysFreeString(ei.bstrHelpFile);
	return hr;
}

// Execute exactly ONE PowerPoint Undo (CommandBars.ExecuteMso("Undo"), with the
// FindControl(Id=128).Execute fallback — undo-probe.cpp's proven mechanisms).
static bool ExecuteUndoOnceW3(IDispatch* appDisp) {
	if (!appDisp) return false;
	_variant_t cbVar;
	if (FAILED(InvokeNameW3(appDisp, L"CommandBars", DISPATCH_PROPERTYGET, NULL, 0, &cbVar))
		|| cbVar.vt != VT_DISPATCH || !cbVar.pdispVal) return false;
	IDispatchPtr cb = cbVar.pdispVal;
	{
		VARIANT arg;
		::VariantInit(&arg);
		arg.vt = VT_BSTR;
		arg.bstrVal = ::SysAllocString(L"Undo");
		HRESULT hr = InvokeNameW3(cb, L"ExecuteMso", DISPATCH_METHOD, &arg, 1, NULL);
		::VariantClear(&arg);
		if (SUCCEEDED(hr)) return true;
	}
	{
		VARIANT fcArgs[2];
		::VariantInit(&fcArgs[0]);
		::VariantInit(&fcArgs[1]);
		fcArgs[0].vt = VT_I4; fcArgs[0].lVal = 128; // Id: standard Undo control
		fcArgs[1].vt = VT_I4; fcArgs[1].lVal = 1;   // Type: msoControlButton
		_variant_t ctrlVar;
		HRESULT hr = InvokeNameW3(cb, L"FindControl", DISPATCH_METHOD, fcArgs, 2, &ctrlVar);
		if (SUCCEEDED(hr) && ctrlVar.vt == VT_DISPATCH && ctrlVar.pdispVal) {
			IDispatchPtr ctrl = ctrlVar.pdispVal;
			if (SUCCEEDED(InvokeNameW3(ctrl, L"Execute", DISPATCH_METHOD, NULL, 0, NULL))) return true;
		}
	}
	return false;
}

// Identity + geometry snapshot over every CHART_ROOT child, compared as a SET
// (a lossless re-emission recreates previously hidden shapes at the END of the
// group's item order). Text-bearing kinds are compared by horizontal CENTER
// only: PowerPoint autosize owns their box, so a re-created label legitimately
// reads back a different (autosized vs scene-sized) rect around the same
// anchor. Everything else must match within EMU float noise (0.1pt).
struct ChartChildGeom {
	std::string key; // "KIND:ID"
	bool textKind = false;
	float l = 0, t = 0, w = 0, h = 0;
};

static bool IsTextBearingChartKind(const std::string& k) {
	return k == "TITLE" || k == "ROW_LABEL" || k == "RAIL_TASKLBL" || k == "TASK_LABEL"
		|| k == "TASK_PCT" || k == "MILESTONE_LABEL" || k == "AXIS_TOP" || k == "AXIS_BOT"
		|| k == "TODAY_LABEL" || k == "DEADLINE_LABEL" || k == "CUSTOM_MARKER_LABEL"
		|| k == "BRACKET_LABEL" || k == "TEXT";
}

static bool CollectChartChildGeoms(PowerPoint::_ApplicationPtr& app, std::vector<ChartChildGeom>* out) {
	if (!out) return false;
	out->clear();
	try {
		PowerPoint::_SlidePtr sl = app->GetActiveWindow()->GetView()->GetSlide();
		PowerPoint::ShapesPtr shs = sl->GetShapes();
		long nn = shs->GetCount();
		for (long ii = 1; ii <= nn; ++ii) {
			PowerPoint::ShapePtr root = shs->Item(_variant_t(ii));
			_bstr_t rk = root->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (!rk.length() || std::string((const char*)_bstr_t(rk)) != "CHART_ROOT") continue;
			PowerPoint::GroupShapesPtr items = root->GetGroupItems();
			long n = items->GetCount();
			for (long i = 1; i <= n; ++i) {
				PowerPoint::ShapePtr ch = items->Item(_variant_t(i));
				_bstr_t k = ch->GetTags()->Item(_bstr_t(L"PP_KIND"));
				_bstr_t id = ch->GetTags()->Item(_bstr_t(L"PP_ID"));
				ChartChildGeom g;
				const std::string kind = k.length() ? (const char*)_bstr_t(k) : "";
				g.key = kind + ":" + (id.length() ? (const char*)_bstr_t(id) : "");
				g.textKind = IsTextBearingChartKind(kind);
				g.l = ch->GetLeft(); g.t = ch->GetTop(); g.w = ch->GetWidth(); g.h = ch->GetHeight();
				out->push_back(g);
			}
			return true;
		}
	} catch (...) {}
	return false;
}

static bool ChartChildGeomsEqual(std::vector<ChartChildGeom> a, std::vector<ChartChildGeom> b,
	std::string* mismatchDetail) {
	if (mismatchDetail) mismatchDetail->clear();
	auto byKeyThenGeom = [](const ChartChildGeom& x, const ChartChildGeom& y) {
		if (x.key != y.key) return x.key < y.key;
		if (x.l != y.l) return x.l < y.l;
		if (x.t != y.t) return x.t < y.t;
		if (x.w != y.w) return x.w < y.w;
		return x.h < y.h;
	};
	std::sort(a.begin(), a.end(), byKeyThenGeom);
	std::sort(b.begin(), b.end(), byKeyThenGeom);
	if (a.size() != b.size()) {
		if (mismatchDetail) *mismatchDetail = "child count " + std::to_string(a.size()) + " vs " + std::to_string(b.size());
		return false;
	}
	const float tol = 0.1f;
	for (size_t i = 0; i < a.size(); ++i) {
		const ChartChildGeom& x = a[i];
		const ChartChildGeom& y = b[i];
		bool same = x.key == y.key;
		if (same) {
			if (x.textKind) {
				// Autosize shrinks the box toward the text's ALIGNMENT anchor:
				// left edge for left-aligned labels (AXIS_TOP, ROW_LABEL, ...),
				// center for centered ones (TASK_LABEL, AXIS_BOT), right edge
				// for right-aligned. The anchor edge is the render invariant.
				same = std::fabs(x.l - y.l) <= 0.5f
					|| std::fabs((x.l + x.w / 2.0f) - (y.l + y.w / 2.0f)) <= 0.5f
					|| std::fabs((x.l + x.w) - (y.l + y.w)) <= 0.5f;
			} else {
				same = std::fabs(x.l - y.l) <= tol && std::fabs(x.t - y.t) <= tol
					&& std::fabs(x.w - y.w) <= tol && std::fabs(x.h - y.h) <= tol;
			}
		}
		if (!same) {
			if (mismatchDetail) {
				char buf[256];
				::snprintf(buf, sizeof(buf), "%s(%.2f,%.2f,%.2f,%.2f) vs %s(%.2f,%.2f,%.2f,%.2f)",
					x.key.c_str(), x.l, x.t, x.w, x.h, y.key.c_str(), y.l, y.t, y.w, y.h);
				*mismatchDetail = buf;
			}
			return false;
		}
	}
	return true;
}

// Does the live chart currently contain a child with this PP_KIND/PP_ID?
static bool ChartChildExists(PowerPoint::_ApplicationPtr& app, const char* kind, const char* id) {
	try {
		PowerPoint::_SlidePtr sl = app->GetActiveWindow()->GetView()->GetSlide();
		PowerPoint::ShapesPtr shs = sl->GetShapes();
		long nn = shs->GetCount();
		for (long ii = 1; ii <= nn; ++ii) {
			PowerPoint::ShapePtr root = shs->Item(_variant_t(ii));
			_bstr_t rk = root->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (!rk.length() || std::string((const char*)_bstr_t(rk)) != "CHART_ROOT") continue;
			PowerPoint::GroupShapesPtr items = root->GetGroupItems();
			long n = items->GetCount();
			for (long i = 1; i <= n; ++i) {
				PowerPoint::ShapePtr ch = items->Item(_variant_t(i));
				_bstr_t k = ch->GetTags()->Item(_bstr_t(L"PP_KIND"));
				if (!k.length() || std::string((const char*)_bstr_t(k)) != kind) continue;
				if (!id || !*id) return true;
				_bstr_t chId = ch->GetTags()->Item(_bstr_t(L"PP_ID"));
				if (chId.length() && std::string((const char*)_bstr_t(chId)) == id) return true;
			}
			return false;
		}
	} catch (...) {}
	return false;
}

// Screen-pixel px<->day projection derived from the overlay dump's ppProj +
// chartRect fields (the same math as Overlay.cpp's ProjectionPx, but computed
// from the dump so profiles share the overlay's exact ground truth).
struct DumpProjPx {
	bool ok = false;
	double pxPerDay = 0.0;
	double originXpx = 0.0;
	long originDay = 0;
	long minDay = 0;
	long pad = 0;
};
static bool ParseDumpProjectionPx(const char* dump, DumpProjPx* out) {
	if (!dump || !out) return false;
	const char* p = ::strstr(dump, "\"ppProj\":{");
	if (!p) return false;
	long minDay = 0, pad = 0;
	double ptPerDay = 0.0, originX = 0.0, chartLeftPt = 0.0, chartWidthPt = 0.0;
	if (::sscanf_s(p, "\"ppProj\":{\"minDay\":%ld,\"pad\":%ld,\"ptPerDay\":%lf,\"originX\":%lf,\"chartLeftPt\":%lf,\"chartWidthPt\":%lf",
		&minDay, &pad, &ptPerDay, &originX, &chartLeftPt, &chartWidthPt) != 6) return false;
	if (ptPerDay <= 0.0 || chartWidthPt <= 0.0) return false;
	RECT chart{};
	if (!ParseNamedRectFromDump(dump, "chartRect", &chart) || chart.right <= chart.left) return false;
	const double scale = (double)(chart.right - chart.left) / chartWidthPt;
	out->pxPerDay = ptPerDay * scale;
	out->originXpx = (double)chart.left + (originX - chartLeftPt) * scale;
	out->originDay = minDay - pad;
	out->minDay = minDay;
	out->pad = pad;
	out->ok = out->pxPerDay > 0.0;
	return out->ok;
}

static bool FindTaskProgressEdge(PowerPoint::_ApplicationPtr& app, const char* taskId, int percent, POINT* outScreen) {
	RECT r{};
	if (!FindTaskBodyRect(app, taskId, &r) || !outScreen) return false;
	if (percent < 0) percent = 0;
	if (percent > 100) percent = 100;
	outScreen->x = r.left + (r.right - r.left) * percent / 100;
	outScreen->y = (r.top + r.bottom) / 2;
	return true;
}

static bool FindMarkerDragPoint(PowerPoint::_ApplicationPtr& app, const char* markerId, POINT* outScreen) {
	if (!outScreen || !markerId || !*markerId) return false;
	try {
		PowerPoint::DocumentWindowPtr w = app->GetActiveWindow();
		PowerPoint::_SlidePtr sl = w->GetView()->GetSlide();
		PowerPoint::ShapesPtr shs = sl->GetShapes();
		long nn = shs->GetCount();
		for (long ii = 1; ii <= nn; ++ii) {
			PowerPoint::ShapePtr root = shs->Item(_variant_t(ii));
			_bstr_t rk = root->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (!rk.length() || std::string((const char*)_bstr_t(rk)) != "CHART_ROOT") continue;
			PowerPoint::GroupShapesPtr items = root->GetGroupItems();
			long n = items->GetCount();
			for (long i = 1; i <= n; ++i) {
				PowerPoint::ShapePtr ch = items->Item(_variant_t(i));
				_bstr_t id = ch->GetTags()->Item(_bstr_t(L"PP_ID"));
				if (!id.length() || std::string((const char*)_bstr_t(id)) != markerId) continue;
				_bstr_t k = ch->GetTags()->Item(_bstr_t(L"PP_KIND"));
				std::string kind = k.length() ? std::string((const char*)_bstr_t(k)) : "";
				if (kind != "TODAY_LINE" && kind != "DEADLINE" && kind != "CUSTOM_MARKER") continue;
				float l = ch->GetLeft(), t = ch->GetTop(), h = ch->GetHeight();
				outScreen->x = w->PointsToScreenPixelsX(l);
				outScreen->y = w->PointsToScreenPixelsY(t + h * 0.5f);
				return true;
			}
		}
	} catch (...) {}
	return false;
}

// Down + moves (button still held). Pair with PostScreenDragFinish. Split so
// trace profiles can dump live-drag state (pill text, candidate %, preview
// rect) BEFORE mouse-up — that state legitimately clears at drop.
static void PostScreenDragStart(HWND ov, POINT screenStart, int dragPxX, int dragPxY = 0) {
	if (!ov) return;
	POINT clientPt = screenStart;
	::ScreenToClient(ov, &clientPt);
	Overlay_SetCursorPosOverrideForTest(true, screenStart);
	LPARAM downLp = MAKELPARAM((short)clientPt.x, (short)clientPt.y);
	::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, downLp);
	PumpFor(60);
	const int steps = 5;
	for (int s = 1; s <= steps; ++s) {
		int mx = clientPt.x + dragPxX * s / steps;
		int my = clientPt.y + dragPxY * s / steps;
		POINT screenPt = { screenStart.x + dragPxX * s / steps, screenStart.y + dragPxY * s / steps };
		Overlay_SetCursorPosOverrideForTest(true, screenPt);
		::PostMessageW(ov, WM_MOUSEMOVE, 0, MAKELPARAM((short)mx, (short)my));
		PumpFor(40);
	}
}

static void PostScreenDragFinish(HWND ov, POINT screenStart, int dragPxX, int dragPxY = 0) {
	if (!ov) return;
	POINT clientPt = screenStart;
	::ScreenToClient(ov, &clientPt);
	int finalX = clientPt.x + dragPxX;
	int finalY = clientPt.y + dragPxY;
	POINT upScreen = { screenStart.x + dragPxX, screenStart.y + dragPxY };
	Overlay_SetCursorPosOverrideForTest(true, upScreen);
	::PostMessageW(ov, WM_LBUTTONUP, 0, MAKELPARAM((short)finalX, (short)finalY));
	PumpFor(400);
}

static void PostScreenDrag(HWND ov, POINT screenStart, int dragPxX, int dragPxY = 0) {
	PostScreenDragStart(ov, screenStart, dragPxX, dragPxY);
	PostScreenDragFinish(ov, screenStart, dragPxX, dragPxY);
}

static bool JsonFieldNonEmpty(const char* dump, const char* key) {
	if (!dump || !key) return false;
	char needle[64];
	::snprintf(needle, sizeof(needle), "\"%s\":\"", key);
	const char* p = ::strstr(dump, needle);
	return p && p[::strlen(needle)] != '"';
}

static int CountRowBandsBetween(const char* dump, const char* rowA, const char* rowB) {
	if (!dump || !rowA || !rowB) return -1;
	const char* bands = ::strstr(dump, "\"rowBands\":[");
	if (!bands) return -1;
	std::vector<std::string> ids;
	const char* p = bands;
	while ((p = ::strstr(p, "\"rowId\":\"")) != nullptr) {
		p += 9;
		const char* end = ::strchr(p, '"');
		if (!end) break;
		ids.emplace_back(p, end - p);
		p = end + 1;
	}
	int ia = -1, ib = -1;
	for (size_t i = 0; i < ids.size(); ++i) {
		if (ids[i] == rowA) ia = (int)i;
		if (ids[i] == rowB) ib = (int)i;
	}
	if (ia < 0 || ib < 0) return -1;
	return (ia > ib ? ia - ib : ib - ia) + 1;
}

static int JsonFieldInt(const char* dump, const char* key, int fallback = 0) {
	if (!dump || !key) return fallback;
	char needle[64];
	::snprintf(needle, sizeof(needle), "\"%s\":", key);
	const char* p = ::strstr(dump, needle);
	if (!p) return fallback;
	int v = fallback;
	::sscanf_s(p + ::strlen(needle), "%d", &v);
	return v;
}

static bool ParseDragPreviewHeight(const char* dump, int* outH) {
	RECT r{};
	if (!ParseNamedRectFromDump(dump, "dragPreviewRect", &r) || !outH) return false;
	*outH = r.bottom - r.top;
	return *outH > 0;
}

// Count pixels within per-channel tolerance 24 of legacy accent RGB(26,115,232).
static double MeasureAccentPctInRect(const RECT& rc) {
	int w = rc.right - rc.left, h = rc.bottom - rc.top;
	if (w <= 0 || h <= 0) return 100.0;
	HDC screen = ::GetDC(NULL);
	HDC mem = ::CreateCompatibleDC(screen);
	HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
	HGDIOBJ old = ::SelectObject(mem, bmp);
	::BitBlt(mem, 0, 0, w, h, screen, rc.left, rc.top, SRCCOPY);
	::SelectObject(mem, old);

	double pct = 100.0;
	{
		Gdiplus::Bitmap bitmap(bmp, NULL);
		Gdiplus::BitmapData data{};
		Gdiplus::Rect grect(0, 0, w, h);
		if (bitmap.LockBits(&grect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) == Gdiplus::Ok) {
			int accent = 0;
			const int total = w * h;
			for (int y = 0; y < h; ++y) {
				const BYTE* row = static_cast<const BYTE*>(data.Scan0) + y * data.Stride;
				for (int x = 0; x < w; ++x) {
					const BYTE b = row[x * 4 + 0];
					const BYTE g = row[x * 4 + 1];
					const BYTE r = row[x * 4 + 2];
					if (std::abs((int)r - 26) <= 24 && std::abs((int)g - 115) <= 24 && std::abs((int)b - 232) <= 24) {
						++accent;
					}
				}
			}
			bitmap.UnlockBits(&data);
			if (total > 0) pct = 100.0 * (double)accent / (double)total;
		}
	}
	::DeleteObject(bmp);
	::DeleteDC(mem);
	::ReleaseDC(NULL, screen);
	return pct;
}

static bool CheckChromeCalm(
	PowerPoint::_ApplicationPtr& app,
	PowerPoint::_SlidePtr& slide,
	bool overall,
	double maxPct,
	int* matrixRc) {
	if (overall) {
		Overlay_SelectForTest("", "");
		SelectChartRootNatively(slide);
	} else {
		Overlay_SelectForTest("", "");
		try { app->GetActiveWindow()->GetSelection()->Unselect(); } catch (...) {}
	}
	PumpFor(900);
	RECT chartRc{};
	if (!GetChartRootScreenRect(app, &chartRc)) {
		wprintf(overall ? L"CHROME CALM OVERALL FAIL: no chart rect\n" : L"CHROME CALM IDLE FAIL: no chart rect\n");
		if (matrixRc) *matrixRc = 1;
		return false;
	}
	RECT interior = chartRc;
	::InflateRect(&interior, -8, -8);
	if (interior.right <= interior.left || interior.bottom <= interior.top) {
		wprintf(overall ? L"CHROME CALM OVERALL FAIL: interior too small\n" : L"CHROME CALM IDLE FAIL: interior too small\n");
		if (matrixRc) *matrixRc = 1;
		return false;
	}
	const double pct = MeasureAccentPctInRect(interior);
	const bool pass = pct <= maxPct;
	if (pass) {
		wprintf(overall ? L"CHROME CALM OVERALL OK\n" : L"CHROME CALM IDLE OK\n");
	} else {
		wprintf(overall ? L"CHROME CALM OVERALL FAIL: %.1f%%\n" : L"CHROME CALM IDLE FAIL: %.1f%%\n", pct);
		if (matrixRc) *matrixRc = 1;
	}
	return pass;
}

static bool CheckAppBarFitForContext(
	const char* ctxLabel,
	AppBarSel sel,
	const std::string& selId,
	const PpDocument& doc,
	HWND abm) {
	if (!abm || !::IsWindowVisible(abm)) {
		wprintf(L"APPBAR FIT %hs FAIL: window missing\n", ctxLabel);
		return false;
	}
	RECT winRc{};
	::GetWindowRect(abm, &winRc);
	const AppBarModel model = BuildAppBar(sel, doc, selId);
	for (const auto& group : model.groups) {
		for (const auto& item : group.items) {
			if (!item.enabled) continue;
			RECT btnRc{};
			if (!OverlayAppBarButtonRectForTest(item.cmd, &btnRc)) {
				const char* cmdLabel = item.label.empty() ? nullptr : item.label.c_str();
				if (cmdLabel && *cmdLabel) {
					wprintf(L"APPBAR FIT %hs FAIL: %hs rect outside window\n", ctxLabel, cmdLabel);
				} else {
					wprintf(L"APPBAR FIT %hs FAIL: cmd%d rect outside window\n", ctxLabel, item.cmd);
				}
				return false;
			}
			if (!RectContainsRect(winRc, btnRc)) {
				const char* cmdLabel = item.label.empty() ? nullptr : item.label.c_str();
				if (cmdLabel && *cmdLabel) {
					wprintf(L"APPBAR FIT %hs FAIL: %hs rect outside window\n", ctxLabel, cmdLabel);
				} else {
					wprintf(L"APPBAR FIT %hs FAIL: cmd%d rect outside window\n", ctxLabel, item.cmd);
				}
				return false;
			}
		}
	}
	wprintf(L"APPBAR FIT %hs OK\n", ctxLabel);
	return true;
}

// Screen-capture the rectangle [rc] into a PNG at [path]. Clamps nothing — the
// caller passes an on-screen rect.
// Capture ONE window's own pixels (PrintWindow + PW_RENDERFULLCONTENT works
// for layered/ULW windows on Win8.1+). Screen-rect captures composite whatever
// overlaps the rect — which hid a two-app-bar-windows bug behind plausible
// pixels (v2.6.3). Falls back to screen capture when PrintWindow fails.
static bool CaptureWindowToPng(HWND hwnd, const wchar_t* path);
static bool CaptureRectToPng(const RECT& rc, const wchar_t* path) {
	int w = rc.right - rc.left, h = rc.bottom - rc.top;
	if (w <= 0 || h <= 0) return false;
	HDC screen = ::GetDC(NULL);
	HDC mem = ::CreateCompatibleDC(screen);
	HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
	HGDIOBJ old = ::SelectObject(mem, bmp);
	::BitBlt(mem, 0, 0, w, h, screen, rc.left, rc.top, SRCCOPY);
	::SelectObject(mem, old);

	bool ok = false;
	{
		Gdiplus::Bitmap bitmap(bmp, NULL);
		CLSID pngClsid;
		if (PngEncoderClsid(&pngClsid) >= 0) {
			ok = (bitmap.Save(path, &pngClsid, NULL) == Gdiplus::Ok);
		}
	}
	::DeleteObject(bmp);
	::DeleteDC(mem);
	::ReleaseDC(NULL, screen);
	return ok;
}

static bool CaptureWindowToPng(HWND hwnd, const wchar_t* path) {
	if (!hwnd || !::IsWindow(hwnd)) return false;
	RECT r{};
	if (!::GetWindowRect(hwnd, &r)) return false;
	int w = r.right - r.left, h = r.bottom - r.top;
	if (w <= 0 || h <= 0) return false;
	HDC screen = ::GetDC(NULL);
	HDC mem = ::CreateCompatibleDC(screen);
	HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
	HGDIOBJ old = ::SelectObject(mem, bmp);
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
	BOOL printed = ::PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT);
	::SelectObject(mem, old);
	bool ok = false;
	if (printed) {
		Gdiplus::Bitmap bitmap(bmp, NULL);
		CLSID pngClsid;
		if (PngEncoderClsid(&pngClsid) >= 0)
			ok = (bitmap.Save(path, &pngClsid, NULL) == Gdiplus::Ok);
	}
	::DeleteObject(bmp);
	::DeleteDC(mem);
	::ReleaseDC(NULL, screen);
	if (!ok) return CaptureRectToPng(r, path); // fallback: composited screen
	return true;
}

// Gallery captures need proof that the app bar/menu/card was captured from the
// owning window, rather than from the screen composite.  Unlike the general
// harness helper above, this deliberately has no BitBlt fallback.
static bool CaptureWindowPrintWindowToPng(HWND hwnd, const wchar_t* path) {
	if (!hwnd || !::IsWindow(hwnd) || !::IsWindowVisible(hwnd)) return false;
	RECT r{};
	if (!::GetWindowRect(hwnd, &r)) return false;
	const int w = r.right - r.left, h = r.bottom - r.top;
	if (w <= 0 || h <= 0) return false;
	HDC screen = ::GetDC(NULL);
	HDC mem = ::CreateCompatibleDC(screen);
	HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
	HGDIOBJ old = ::SelectObject(mem, bmp);
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
	const BOOL printed = ::PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT);
	::SelectObject(mem, old);
	bool ok = false;
	if (printed) {
		Gdiplus::Bitmap bitmap(bmp, NULL);
		CLSID pngClsid;
		if (PngEncoderClsid(&pngClsid) >= 0)
			ok = (bitmap.Save(path, &pngClsid, NULL) == Gdiplus::Ok);
	}
	::DeleteObject(bmp);
	::DeleteDC(mem);
	::ReleaseDC(NULL, screen);
	return ok;
}

static void UnionCaptureRect(RECT* target, const RECT& addition) {
	if (!target) return;
	if (::IsRectEmpty(target)) { *target = addition; return; }
	if (addition.left < target->left) target->left = addition.left;
	if (addition.top < target->top) target->top = addition.top;
	if (addition.right > target->right) target->right = addition.right;
	if (addition.bottom > target->bottom) target->bottom = addition.bottom;
}

// Save the full chart context as gallery-<context>.png and, separately, strict
// PrintWindow captures for the app bar and any open menu/card surface.
static bool CaptureGalleryContext(PowerPoint::_ApplicationPtr& app, const wchar_t* context,
	HWND floating = NULL, const wchar_t* floatingSuffix = nullptr) {
	if (!context || !*context) return false;
	RECT chart{};
	if (!GetChartRootScreenRect(app, &chart)) return false;
	RECT full = chart;
	bool ok = true;
	wchar_t path[320];
	HWND appBar = OverlayAppBarHwnd();
	::swprintf_s(path, 320, L"native\\build\\gallery-%ls-appbar.png", context);
	const bool appBarOk = CaptureWindowPrintWindowToPng(appBar, path);
	wprintf(L"GALLERY %ls PRINTWINDOW appbar %ls\n", context, appBarOk ? L"OK" : L"FAIL");
	ok = ok && appBarOk;
	if (appBar && ::IsWindowVisible(appBar)) {
		RECT r{}; ::GetWindowRect(appBar, &r); UnionCaptureRect(&full, r);
	}
	if (floating && floatingSuffix && *floatingSuffix) {
		::swprintf_s(path, 320, L"native\\build\\gallery-%ls-%ls.png", context, floatingSuffix);
		const bool floatingOk = CaptureWindowPrintWindowToPng(floating, path);
		wprintf(L"GALLERY %ls PRINTWINDOW %ls %ls\n", context, floatingSuffix,
			floatingOk ? L"OK" : L"FAIL");
		ok = ok && floatingOk;
		if (::IsWindowVisible(floating)) {
			RECT r{}; ::GetWindowRect(floating, &r); UnionCaptureRect(&full, r);
		}
	}
	const int screenW = ::GetSystemMetrics(SM_CXSCREEN);
	const int screenH = ::GetSystemMetrics(SM_CYSCREEN);
	if (full.left < 0) full.left = 0;
	if (full.top < 0) full.top = 0;
	if (full.right > screenW) full.right = screenW;
	if (full.bottom > screenH) full.bottom = screenH;
	::swprintf_s(path, 320, L"native\\build\\gallery-%ls.png", context);
	const bool chartOk = CaptureRectToPng(full, path);
	wprintf(L"GALLERY %ls FULLCHART %ls\n", context, chartOk ? L"OK" : L"FAIL");
	return ok && chartOk;
}

// ============================================================================
// Cold UX walkthrough runner (v2.6.0 gate; SR-IXC-01..22 / SR-IXC-22).
//
// Executes a real user GOAL from native/tools/walkthroughs/<name>.json through
// REAL posted input to the overlay/app-bar windows (WM_MOUSEMOVE/LBUTTONDOWN/
// UP/DBLCLK + Overlay_SetCursorPosOverrideForTest for the physical-cursor reads,
// WM_HOTKEY for the registered Delete/arrow accelerators, WM_KEYDOWN/WM_CHAR to
// the focused inline/card editor) -- NOT the Overlay_SelectForTest / perform
// seams, which are reserved here strictly for read-only target resolution. A
// PNG is captured after every step so a reviewer can judge discoverability and
// convention adherence; where the UI lacks an affordance the natural gesture is
// still attempted and the capture documents the outcome -- that is the point of
// the gate.
// ----------------------------------------------------------------------------

// --- minimal JSON value + recursive-descent parser (schema subset) ----------
struct WJson {
	enum Type { TNull, TBool, TNum, TStr, TArr, TObj } type = TNull;
	bool b = false;
	double num = 0.0;
	std::string str;
	std::vector<WJson> arr;
	std::vector<std::pair<std::string, WJson>> obj;
	const WJson* Find(const char* key) const {
		for (const auto& kv : obj) if (kv.first == key) return &kv.second;
		return nullptr;
	}
	std::string Str(const char* key) const {
		const WJson* v = Find(key);
		return (v && v->type == TStr) ? v->str : std::string();
	}
};

struct WJsonParser {
	const char* p;
	const char* end;
	bool ok = true;
	explicit WJsonParser(const std::string& s) : p(s.c_str()), end(s.c_str() + s.size()) {}
	void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }
	bool parse(WJson& out) { ws(); bool r = value(out); ws(); return r && ok; }
	bool value(WJson& out) {
		ws();
		if (p >= end) { ok = false; return false; }
		char c = *p;
		if (c == '"') return string(out);
		if (c == '{') return object(out);
		if (c == '[') return array(out);
		if (c == 't' || c == 'f') return boolean(out);
		if (c == 'n') return null(out);
		return number(out);
	}
	bool string(WJson& out) {
		out.type = WJson::TStr;
		out.str.clear();
		if (p >= end || *p != '"') { ok = false; return false; }
		++p;
		while (p < end && *p != '"') {
			char c = *p++;
			if (c == '\\' && p < end) {
				char e = *p++;
				switch (e) {
				case 'n': out.str += '\n'; break;
				case 't': out.str += '\t'; break;
				case 'r': out.str += '\r'; break;
				case 'b': out.str += '\b'; break;
				case 'f': out.str += '\f'; break;
				case '/': out.str += '/'; break;
				case '\\': out.str += '\\'; break;
				case '"': out.str += '"'; break;
				case 'u': for (int i = 0; i < 4 && p < end; ++i) ++p; out.str += '?'; break;
				default: out.str += e; break;
				}
			} else {
				out.str += c;
			}
		}
		if (p < end && *p == '"') { ++p; return true; }
		ok = false;
		return false;
	}
	bool object(WJson& out) {
		out.type = WJson::TObj;
		++p; ws();
		if (p < end && *p == '}') { ++p; return true; }
		while (p < end) {
			ws();
			WJson key;
			if (!string(key)) { ok = false; return false; }
			ws();
			if (p >= end || *p != ':') { ok = false; return false; }
			++p;
			WJson val;
			if (!value(val)) { ok = false; return false; }
			out.obj.emplace_back(key.str, std::move(val));
			ws();
			if (p < end && *p == ',') { ++p; continue; }
			if (p < end && *p == '}') { ++p; return true; }
			ok = false;
			return false;
		}
		ok = false;
		return false;
	}
	bool array(WJson& out) {
		out.type = WJson::TArr;
		++p; ws();
		if (p < end && *p == ']') { ++p; return true; }
		while (p < end) {
			WJson val;
			if (!value(val)) { ok = false; return false; }
			out.arr.push_back(std::move(val));
			ws();
			if (p < end && *p == ',') { ++p; continue; }
			if (p < end && *p == ']') { ++p; return true; }
			ok = false;
			return false;
		}
		ok = false;
		return false;
	}
	bool boolean(WJson& out) {
		out.type = WJson::TBool;
		if (end - p >= 4 && strncmp(p, "true", 4) == 0) { out.b = true; p += 4; return true; }
		if (end - p >= 5 && strncmp(p, "false", 5) == 0) { out.b = false; p += 5; return true; }
		ok = false;
		return false;
	}
	bool null(WJson& out) {
		out.type = WJson::TNull;
		if (end - p >= 4 && strncmp(p, "null", 4) == 0) { p += 4; return true; }
		ok = false;
		return false;
	}
	bool number(WJson& out) {
		out.type = WJson::TNum;
		const char* s = p;
		while (p < end && (*p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E' || (*p >= '0' && *p <= '9'))) ++p;
		if (p == s) { ok = false; return false; }
		out.num = atof(std::string(s, p).c_str());
		return true;
	}
};

// --- date helpers (calibrated date <-> x for cell:/days: targets) -----------
static long WalkDaysFromCivil(int y, unsigned m, unsigned d) {
	y -= (m <= 2);
	const int era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = (unsigned)(y - era * 400);
	const unsigned doy = (153u * (m + (m > 2 ? (unsigned)-3 : 9u)) + 2u) / 5u + d - 1u;
	const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
	return (long)era * 146097L + (long)doe - 719468L;
}
static bool WalkParseDateSerial(const std::string& s, long* out) {
	int y = 0, m = 0, d = 0;
	if (sscanf_s(s.c_str(), "%d-%d-%d", &y, &m, &d) == 3 && m >= 1 && m <= 12 && d >= 1 && d <= 31) {
		*out = WalkDaysFromCivil(y, (unsigned)m, (unsigned)d);
		return true;
	}
	return false;
}

// Full screen rect of a task body (mirrors FindTaskBodyCenter; used to
// self-calibrate pixels-per-day and the date->x anchor from actual geometry).
static bool FindTaskBodyRect(PowerPoint::_ApplicationPtr& app, const char* taskId, RECT* out) {
	if (!out || !taskId || !*taskId) return false;
	try {
		PowerPoint::DocumentWindowPtr w = app->GetActiveWindow();
		PowerPoint::_SlidePtr sl = w->GetView()->GetSlide();
		PowerPoint::ShapesPtr shs = sl->GetShapes();
		long nn = shs->GetCount();
		for (long ii = 1; ii <= nn; ++ii) {
			PowerPoint::ShapePtr root = shs->Item(_variant_t(ii));
			_bstr_t rk = root->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (!rk.length() || std::string((const char*)_bstr_t(rk)) != "CHART_ROOT") continue;
			PowerPoint::GroupShapesPtr items = root->GetGroupItems();
			long n = items->GetCount();
			for (long i = 1; i <= n; ++i) {
				PowerPoint::ShapePtr ch = items->Item(_variant_t(i));
				_bstr_t k = ch->GetTags()->Item(_bstr_t(L"PP_KIND"));
				if (!k.length() || std::string((const char*)_bstr_t(k)) != "TASK") continue;
				_bstr_t id = ch->GetTags()->Item(_bstr_t(L"PP_ID"));
				if (!id.length() || std::string((const char*)_bstr_t(id)) != taskId) continue;
				float l = ch->GetLeft(), t = ch->GetTop(), ww = ch->GetWidth(), h = ch->GetHeight();
				out->left = w->PointsToScreenPixelsX(l);
				out->top = w->PointsToScreenPixelsY(t);
				out->right = w->PointsToScreenPixelsX(l + ww);
				out->bottom = w->PointsToScreenPixelsY(t + h);
				return true;
			}
		}
	} catch (...) {}
	return false;
}

// Parse a named "<key>":{"left":..,"top":..,"right":..,"bottom":..} rect out of
// the read-only chrome dump (used for sel:center). Never drives the gesture.
static bool ParseNamedRectFromDump(const char* json, const char* key, RECT* out) {
	if (!json || !key || !out) return false;
	const char* p = ::strstr(json, key);
	if (!p) return false;
	const char* lk = ::strstr(p, "\"left\":");
	const char* tk = ::strstr(p, "\"top\":");
	const char* rk = ::strstr(p, "\"right\":");
	const char* bk = ::strstr(p, "\"bottom\":");
	long l = 0, t = 0, r = 0, b = 0;
	if (lk && tk && rk && bk && lk < tk && tk < rk && rk < bk
		&& ::sscanf_s(lk, "\"left\":%ld", &l) == 1
		&& ::sscanf_s(tk, "\"top\":%ld", &t) == 1
		&& ::sscanf_s(rk, "\"right\":%ld", &r) == 1
		&& ::sscanf_s(bk, "\"bottom\":%ld", &b) == 1) {
		out->left = l; out->top = t; out->right = r; out->bottom = b;
		return true;
	}
	return false;
}

struct WalkCtx {
	PowerPoint::_ApplicationPtr* app = nullptr;
	HWND ov = NULL;
	HWND ab = NULL;
	PpDocument doc;
	bool calib = false;
	long anchorSerial = 0;
	double anchorX = 0.0;
	double pxPerDay = 0.0;
};

// Self-calibrate pixels-per-day + a date->x anchor from the first showcase task
// whose bar rect resolves on screen (so cell:/days: targets track real layout).
static void CalibrateWalk(WalkCtx& c) {
	for (const auto& t : c.doc.tasks) {
		long s = 0, e = 0;
		if (!WalkParseDateSerial(t.start, &s) || !WalkParseDateSerial(t.end, &e)) continue;
		if (e <= s) continue;
		RECT r{};
		if (!FindTaskBodyRect(*c.app, t.id.c_str(), &r)) continue;
		double widthPx = (double)(r.right - r.left);
		if (widthPx < 2.0) continue;
		c.pxPerDay = widthPx / (double)(e - s);
		c.anchorSerial = s;
		c.anchorX = (double)r.left;
		c.calib = true;
		break;
	}
}

static bool WalkLabelEq(const std::string& a, const char* b) {
	size_t n = ::strlen(b);
	if (a.size() != n) return false;
	for (size_t i = 0; i < n; ++i) {
		char x = a[i]; if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
		char y = b[i]; if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
		if (x != y) return false;
	}
	return true;
}

// Map an app-bar button label to candidate command ids (ambiguous labels like
// "Delete"/"Note" list several; the caller picks whichever is laid out now).
static std::vector<int> AppBarCmdCandidates(const std::string& label) {
	std::vector<int> v;
	if (WalkLabelEq(label, "rename")) v = { HtCmd_Rename };
	else if (WalkLabelEq(label, "edit")) v = { HtCmd_Edit };
	else if (WalkLabelEq(label, "milestone")) v = { HtCmd_InsertMilestone };
	else if (WalkLabelEq(label, "task")) v = { HtCmd_InsertTask };
	else if (WalkLabelEq(label, "row")) v = { HtCmd_AddRow };
	else if (WalkLabelEq(label, "marker")) v = { HtCmd_InsertMarker };
	else if (WalkLabelEq(label, "note")) v = { HtCmd_AddNote, HtCmd_InsertNote };
	else if (WalkLabelEq(label, "link")) v = { HtCmd_Link };
	else if (WalkLabelEq(label, "unlink")) v = { HtCmd_Unlink };
	else if (WalkLabelEq(label, "delete")) v = { HtCmd_Delete, HtCmd_DeleteRow };
	else if (WalkLabelEq(label, "above")) v = { HtCmd_AddRowAbove };
	else if (WalkLabelEq(label, "below")) v = { HtCmd_AddRowBelow };
	else if (WalkLabelEq(label, "indent")) v = { HtCmd_IndentRow };
	else if (WalkLabelEq(label, "outdent")) v = { HtCmd_OutdentRow };
	else if (WalkLabelEq(label, "+1d")) v = { HtCmd_NudgePlus1 };
	else if (WalkLabelEq(label, "-1d")) v = { HtCmd_NudgeMinus1 };
	else if (WalkLabelEq(label, "+10%")) v = { HtCmd_PercentPlus10 };
	else if (WalkLabelEq(label, "-10%")) v = { HtCmd_PercentMinus10 };
	else if (WalkLabelEq(label, "settings")) v = { HtCmd_Settings };
	return v;
}

// Resolve a target token to a SCREEN point. `hasStart`/`startPt` support the
// relative "to" forms (days:+N, point:+dx,+dy). *isAppBar is set for appbar:
// targets so the caller posts to the app-bar window instead of the overlay.
static bool ResolveWalkTarget(WalkCtx& c, const std::string& target,
	bool hasStart, POINT startPt, POINT* out, bool* isAppBar) {
	if (isAppBar) *isAppBar = false;
	if (target.empty() || !out) return false;
	size_t colon = target.find(':');
	std::string kind = (colon == std::string::npos) ? target : target.substr(0, colon);
	std::string arg = (colon == std::string::npos) ? std::string() : target.substr(colon + 1);

	if (kind == "task") {
		POINT pt{};
		if (FindTaskBodyCenter(*c.app, arg.c_str(), &pt)) { *out = pt; return true; }
		return false;
	}
	if (kind == "taskL" || kind == "taskR") {
		RECT r{};
		if (!FindTaskBodyRect(*c.app, arg.c_str(), &r)) return false;
		out->y = (r.top + r.bottom) / 2;
		out->x = (kind == "taskL") ? (r.left + 4) : (r.right - 4);
		return true;
	}
	if (kind == "row") {
		RECT band{};
		if (!ParseRowBandFromDump(Overlay_DumpChromeStateForTest(), arg.c_str(), &band)) return false;
		out->x = (band.left + band.right) / 2;
		out->y = (band.top + band.bottom) / 2;
		return true;
	}
	if (kind == "cell") {
		size_t at = arg.find('@');
		if (at == std::string::npos) return false;
		std::string rowId = arg.substr(0, at);
		std::string date = arg.substr(at + 1);
		RECT band{};
		if (!ParseRowBandFromDump(Overlay_DumpChromeStateForTest(), rowId.c_str(), &band)) return false;
		long serial = 0;
		if (!c.calib || !WalkParseDateSerial(date, &serial)) return false;
		out->x = (long)::lround(c.anchorX + (double)(serial - c.anchorSerial) * c.pxPerDay);
		out->y = (band.top + band.bottom) / 2;
		return true;
	}
	if (kind == "sel") {
		RECT r{};
		if (!ParseNamedRectFromDump(Overlay_DumpChromeStateForTest(), "selScreenRect", &r)) return false;
		if (r.right <= r.left || r.bottom <= r.top) return false;
		out->x = (r.left + r.right) / 2;
		out->y = (r.top + r.bottom) / 2;
		return true;
	}
	if (kind == "appbar") {
		if (isAppBar) *isAppBar = true;
		for (int cmd : AppBarCmdCandidates(arg)) {
			RECT r{};
			if (OverlayAppBarButtonRectForTest(cmd, &r)) {
				out->x = (r.left + r.right) / 2;
				out->y = (r.top + r.bottom) / 2;
				return true;
			}
		}
		return false;
	}
	if (kind == "days") {
		if (!hasStart || !c.calib) return false;
		double nDays = atof(arg.c_str());
		out->x = startPt.x + (long)::lround(nDays * c.pxPerDay);
		out->y = startPt.y;
		return true;
	}
	if (kind == "point") {
		size_t comma = arg.find(',');
		if (comma == std::string::npos) return false;
		bool rel = (!arg.empty() && (arg[0] == '+' || arg[0] == '-'));
		double xv = atof(arg.substr(0, comma).c_str());
		double yv = atof(arg.substr(comma + 1).c_str());
		if (rel) {
			if (!hasStart) return false;
			out->x = startPt.x + (long)::lround(xv);
			out->y = startPt.y + (long)::lround(yv);
		} else {
			POINT cp = { (long)::lround(xv), (long)::lround(yv) };
			if (c.ov) ::ClientToScreen(c.ov, &cp);
			*out = cp;
		}
		return true;
	}
	return false;
}

// --- real input posting (mirrors overlay-test.cpp / PostTaskBodyDrag idioms) -
static void WalkHover(HWND w, POINT screenPt, DWORD ms) {
	if (!w) return;
	Overlay_SetCursorPosOverrideForTest(true, screenPt);
	POINT cp = screenPt; ::ScreenToClient(w, &cp);
	::PostMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM((short)cp.x, (short)cp.y));
	PumpFor(ms > 0 ? ms : 600);
}
static void WalkClick(HWND w, POINT screenPt, WPARAM extraMk) {
	if (!w) return;
	Overlay_SetCursorPosOverrideForTest(true, screenPt);
	POINT cp = screenPt; ::ScreenToClient(w, &cp);
	LPARAM lp = MAKELPARAM((short)cp.x, (short)cp.y);
	::PostMessageW(w, WM_MOUSEMOVE, 0, lp);
	PumpFor(60);
	::PostMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON | extraMk, lp);
	PumpFor(40);
	::PostMessageW(w, WM_LBUTTONUP, extraMk, lp);
	PumpFor(280);
}
static void WalkDblClick(HWND w, POINT screenPt) {
	if (!w) return;
	Overlay_SetCursorPosOverrideForTest(true, screenPt);
	POINT cp = screenPt; ::ScreenToClient(w, &cp);
	LPARAM lp = MAKELPARAM((short)cp.x, (short)cp.y);
	::PostMessageW(w, WM_MOUSEMOVE, 0, lp);
	::PostMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON, lp);
	::PostMessageW(w, WM_LBUTTONUP, 0, lp);
	PumpFor(30);
	::PostMessageW(w, WM_LBUTTONDBLCLK, MK_LBUTTON, lp);
	::PostMessageW(w, WM_LBUTTONUP, 0, lp);
	PumpFor(320);
}
static void WalkDrag(HWND w, POINT fromPt, POINT toPt) {
	if (!w) return;
	Overlay_SetCursorPosOverrideForTest(true, fromPt);
	POINT fc = fromPt; ::ScreenToClient(w, &fc);
	::PostMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM((short)fc.x, (short)fc.y));
	PumpFor(50);
	::PostMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM((short)fc.x, (short)fc.y));
	PumpFor(60);
	const int steps = 8;
	for (int s = 1; s <= steps; ++s) {
		POINT sp = { fromPt.x + (toPt.x - fromPt.x) * s / steps,
			fromPt.y + (toPt.y - fromPt.y) * s / steps };
		Overlay_SetCursorPosOverrideForTest(true, sp);
		POINT sc = sp; ::ScreenToClient(w, &sc);
		::PostMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM((short)sc.x, (short)sc.y));
		PumpFor(35);
	}
	Overlay_SetCursorPosOverrideForTest(true, toPt);
	POINT tc = toPt; ::ScreenToClient(w, &tc);
	::PostMessageW(w, WM_LBUTTONUP, 0, MAKELPARAM((short)tc.x, (short)tc.y));
	PumpFor(420);
}

// Return the focused inline/card editor control if one is open (keys route to
// it); else NULL so registered accelerators go to the overlay via WM_HOTKEY.
static HWND WalkKeyEditorTarget() {
	HWND card = ::FindWindowW(L"PowerPlannerCardEditor", nullptr);
	HWND inl = ::FindWindowW(L"PowerPlannerInlineEditor", nullptr);
	HWND ed = (card && ::IsWindowVisible(card)) ? card
		: ((inl && ::IsWindowVisible(inl)) ? inl : NULL);
	if (!ed) return NULL;
	HWND f = ::GetFocus();
	if (f && (f == ed || ::IsChild(ed, f))) return f;
	return ed;
}
static WORD WalkVkFromChar(char ch) {
	if (ch >= 'a' && ch <= 'z') return (WORD)(ch - 32);
	if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) return (WORD)ch;
	if (ch == ' ') return VK_SPACE;
	return 0;
}
static WORD WalkVkFromKeyName(const std::string& k) {
	if (k == "enter" || k == "return") return VK_RETURN;
	if (k == "escape" || k == "esc") return VK_ESCAPE;
	if (k == "tab") return VK_TAB;
	if (k == "f2") return VK_F2;
	if (k == "delete" || k == "del") return VK_DELETE;
	if (k == "left") return VK_LEFT;
	if (k == "right") return VK_RIGHT;
	if (k == "up") return VK_UP;
	if (k == "down") return VK_DOWN;
	if (k == "space") return VK_SPACE;
	if (k == "backspace") return VK_BACK;
	if (k.size() == 1) return WalkVkFromChar(k[0]);
	return 0;
}
static void WalkKey(WalkCtx& c, const std::string& key) {
	if (key.rfind("type:", 0) == 0) {
		std::string text = key.substr(5);
		HWND target = WalkKeyEditorTarget();
		if (!target) target = c.ov;
		for (char ch : text) {
			WORD vk = WalkVkFromChar(ch);
			::PostMessageW(target, WM_KEYDOWN, (WPARAM)vk, 0);
			::PostMessageW(target, WM_CHAR, (WPARAM)(unsigned char)ch, 0);
			::PostMessageW(target, WM_KEYUP, (WPARAM)vk, 0);
			PumpFor(45);
		}
		PumpFor(180);
		return;
	}
	std::string k = key;
	for (auto& ch : k) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
	HWND editor = WalkKeyEditorTarget();
	if (!editor && (k == "delete" || k == "del")) {
		::PostMessageW(c.ov, WM_HOTKEY, (WPARAM)OVERLAY_HOTKEY_DELETE_FOR_TEST, 0);
	} else if (!editor && k == "left") {
		::PostMessageW(c.ov, WM_HOTKEY, (WPARAM)OVERLAY_HOTKEY_LEFT_FOR_TEST, 0);
	} else if (!editor && k == "right") {
		::PostMessageW(c.ov, WM_HOTKEY, (WPARAM)OVERLAY_HOTKEY_RIGHT_FOR_TEST, 0);
	} else {
		HWND target = editor ? editor : c.ov;
		WORD vk = WalkVkFromKeyName(k);
		::PostMessageW(target, WM_KEYDOWN, (WPARAM)vk, 0);
		::PostMessageW(target, WM_KEYUP, (WPARAM)vk, 0);
	}
	PumpFor(320);
}

// Capture the chart + docked app bar into walkthrough_<name>_<NN>_<kind>.png,
// and print a driver-parseable artifact marker (forward-slash relative path).
static bool WalkCapture(const std::string& name, int idx, const std::string& kind, HWND ov, HWND ab) {
	RECT lr{ 0, 0, 0, 0 };
	bool have = false;
	if (ov && ::IsWindowVisible(ov)) { ::GetWindowRect(ov, &lr); have = true; }
	if (ab && ::IsWindowVisible(ab)) {
		RECT ar{}; ::GetWindowRect(ab, &ar);
		if (have) {
			lr.left = min(lr.left, ar.left); lr.top = min(lr.top, ar.top);
			lr.right = max(lr.right, ar.right); lr.bottom = max(lr.bottom, ar.bottom);
		} else { lr = ar; have = true; }
	}
	RECT scr{ 0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN) };
	if (!have) lr = scr;
	lr.left -= 180; lr.top -= 120; lr.right += 90; lr.bottom += 40;
	if (lr.left < scr.left) lr.left = scr.left;
	if (lr.top < scr.top) lr.top = scr.top;
	if (lr.right > scr.right) lr.right = scr.right;
	if (lr.bottom > scr.bottom) lr.bottom = scr.bottom;
	wchar_t wname[128] = {}, wkind[64] = {};
	::MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, wname, 128);
	::MultiByteToWideChar(CP_UTF8, 0, kind.c_str(), -1, wkind, 64);
	wchar_t path[320];
	::swprintf_s(path, 320, L"native\\build\\walkthrough_%ls_%02d_%ls.png", wname, idx, wkind);
	bool ok = CaptureRectToPng(lr, path);
	char apath[400];
	::snprintf(apath, sizeof(apath), "native/build/walkthrough_%s_%02d_%s.png", name.c_str(), idx, kind.c_str());
	wprintf(L"WALKTHROUGH ARTIFACT %02d: %hs\n", idx, apath);
	return ok;
}

// Load + run a walkthrough JSON. Returns process rc (0 on reaching COMPLETE).
static int RunWalkthrough(PowerPoint::_ApplicationPtr& app, const wchar_t* jsonPath) {
	std::string content;
	{
		std::ifstream f(jsonPath, std::ios::binary);
		if (f) { std::ostringstream ss; ss << f.rdbuf(); content = ss.str(); }
	}
	if (content.empty()) {
		wprintf(L"WALKTHROUGH FAIL: cannot read %ls\n", jsonPath ? jsonPath : L"(null)");
		return 3;
	}
	WJson root;
	WJsonParser parser(content);
	if (!parser.parse(root) || root.type != WJson::TObj) {
		wprintf(L"WALKTHROUGH FAIL: JSON parse error in %ls\n", jsonPath);
		return 3;
	}
	std::string name = root.Str("name");
	std::string goal = root.Str("goal");
	if (name.empty()) name = "walk";
	const WJson* steps = root.Find("steps");
	const int nSteps = (steps && steps->type == WJson::TArr) ? (int)steps->arr.size() : 0;

	Overlay_SetHostActiveOverrideForTest(1);
	// Raise the PowerPoint window before any step/capture: overlay windows are
	// NOTOPMOST under harness override, and the walkthrough PNGs must show the
	// CHART behind the chrome — the first review round captured the app bar
	// floating over an unrelated terminal window because PPT was never raised.
	try {
		HWND ppRoot = ::GetAncestor((HWND)(INT_PTR)app->GetHWND(), GA_ROOT);
		if (ppRoot) {
			::ShowWindow(ppRoot, SW_SHOWMAXIMIZED);
			::SetForegroundWindow(ppRoot);
			::BringWindowToTop(ppRoot);
		}
	} catch (...) {}
	PumpFor(1400); // several ticks: overlay + docked app bar show + first paint

	WalkCtx c;
	c.app = std::addressof(app);
	c.ov = OverlayHwnd();
	c.ab = OverlayAppBarHwnd();
	c.doc = MakeShowcaseDocument();
	CalibrateWalk(c);

	wprintf(L"WALKTHROUGH BEGIN %hs steps=%d calib=%hs pxPerDay=%.2f\n",
		name.c_str(), nSteps, c.calib ? "yes" : "no", c.pxPerDay);
	wprintf(L"WALKTHROUGH GOAL: %hs\n", goal.c_str());
	::fflush(stdout);

	int idx = 0, okCount = 0, failCount = 0;
	if (steps && steps->type == WJson::TArr) {
		for (const auto& st : steps->arr) {
			++idx;
			std::string kind = st.Str("kind");
			std::string target = st.Str("target");
			std::string to = st.Str("to");
			std::string keyv = st.Str("key");
			std::string note = st.Str("note");
			std::string mods = st.Str("mods");
			bool stepOk = true;
			std::string detail;

			// Windows can be recreated across ops; re-resolve each step.
			c.ov = OverlayHwnd();
			c.ab = OverlayAppBarHwnd();

			if (kind == "hover" || kind == "click" || kind == "dblclick" || kind == "drag") {
				POINT pt{};
				bool isAB = false;
				if (!ResolveWalkTarget(c, target, false, POINT{ 0, 0 }, &pt, &isAB)) {
					stepOk = false;
					detail = "unresolved target [" + target + "]";
				} else {
					HWND w = isAB ? c.ab : c.ov;
					if (!w) {
						stepOk = false;
						detail = "no target window";
					} else if (kind == "hover") {
						WalkHover(w, pt, 0);
					} else if (kind == "click") {
						WPARAM mk = (mods == "ctrl") ? MK_CONTROL : (mods == "shift") ? MK_SHIFT : 0;
						WalkClick(w, pt, mk);
					} else if (kind == "dblclick") {
						WalkDblClick(w, pt);
					} else if (kind == "drag") {
						POINT toPt{};
						bool isAB2 = false;
						if (!ResolveWalkTarget(c, to, true, pt, &toPt, &isAB2)) {
							stepOk = false;
							detail = "unresolved to [" + to + "]";
						} else {
							WalkDrag(w, pt, toPt);
						}
					}
				}
			} else if (kind == "key") {
				if (keyv.empty()) { stepOk = false; detail = "missing key"; }
				else WalkKey(c, keyv);
			} else if (kind == "wait") {
				PumpFor(700);
			} else if (kind == "capture") {
				// capture-only documentation frame (handled below)
			} else {
				stepOk = false;
				detail = "unknown kind [" + kind + "]";
			}

			PumpFor(150); // let chrome settle before the frame
			c.ov = OverlayHwnd();
			c.ab = OverlayAppBarHwnd();
			bool capOk = WalkCapture(name, idx, kind.empty() ? "step" : kind, c.ov, c.ab);
			if (!capOk) {
				stepOk = false;
				detail = detail.empty() ? "capture failed" : (detail + "; capture failed");
			}
			if (stepOk) ++okCount; else ++failCount;

			// note is passed as an ARGUMENT (its '%'/text is data, never a format).
			wprintf(L"WALKTHROUGH STEP %02d %hs %hs%hs%hs\n",
				idx, stepOk ? "OK" : "FAIL", note.c_str(),
				detail.empty() ? "" : " | ", detail.c_str());
			::fflush(stdout);
		}
	}

	wprintf(L"WALKTHROUGH SUMMARY %hs ok=%d findings=%d total=%d\n", name.c_str(), okCount, failCount, idx);
	wprintf(L"WALKTHROUGH COMPLETE %hs\n", name.c_str());
	::fflush(stdout);
	return 0;
}

int wmain(int argc, wchar_t** argv) {
	SetHarnessDpiAwareness();

	bool attach = false;
	for (int i = 1; i < argc; ++i) {
		if (wcscmp(argv[i], L"--attach") == 0) { attach = true; break; }
	}
	bool gallery = false;
	for (int i = 1; i < argc; ++i) {
		if (wcscmp(argv[i], L"--gallery") == 0) { gallery = true; break; }
	}
	HWND existingPpt = ::FindWindowW(L"PPTFrameClass", nullptr);
	if (existingPpt && !attach) {
		wprintf(L"APPBARSHOT REFUSE: PowerPoint already running; close it or pass --attach\n");
		return 2;
	}

	::CoInitialize(NULL);
	Gdiplus::GdiplusStartupInput gsi;
	ULONG_PTR gdiToken = 0;
	Gdiplus::GdiplusStartup(&gdiToken, &gsi, NULL);

	int rc = 1;
	try {
		PowerPoint::_ApplicationPtr app;
		app.CreateInstance(L"PowerPoint.Application");
		app->PutVisible(Office::msoTrue);
		PowerPoint::_PresentationPtr pres = app->GetPresentations()->Add(Office::msoTrue);
		// Stand-down marker for the REGISTERED add-in loaded inside this
		// PowerPoint: its own overlay + app bar would exactly overlap the
		// harness's in-process chrome (two PowerPlannerAppBar windows — mixed
		// pixels in captures, double chrome for the user). The add-in's Tick
		// sees this tag and keeps its chrome hidden in harness presentations.
		try { pres->GetTags()->Add(_bstr_t(L"PP_HARNESS"), _bstr_t(L"1")); } catch (...) {}
		pres->GetSlides()->Add(1, PowerPoint::ppLayoutBlank);
		app->GetActiveWindow()->GetView()->GotoSlide(1);

		const float slideW = (float)pres->GetPageSetup()->GetSlideWidth();
		const float slideH = (float)pres->GetPageSetup()->GetSlideHeight();

		PowerPoint::_SlidePtr slide = app->GetActiveWindow()->GetView()->GetSlide();
		PowerPoint::ShapesPtr shapes = slide->GetShapes();
		{
			PowerPoint::ShapePtr title = shapes->AddTextbox(Office::msoTextOrientationHorizontal,
				36.0f, 22.0f, slideW - 72.0f, slideH * 0.12f);
			PowerPoint::TextRangePtr tr = title->GetTextFrame()->GetTextRange();
			tr->PutText(_bstr_t(L"Q3 Launch Plan"));
			PowerPoint::FontPtr f = tr->GetFont();
			f->PutName(_bstr_t(L"Segoe UI Light"));
			f->PutSize(28.0f);
			f->GetColor()->PutPpRGB((Office::MsoRGBType)0x00261D1B);
			title->GetLine()->PutVisible(Office::msoFalse);
			title->GetFill()->PutVisible(Office::msoFalse);
		}

		PpDocument showcaseDoc = MakeShowcaseDocument();
		// The gallery has one explicit dependency-selection frame. Keep the
		// normal harness fixture untouched so existing traces remain byte-for-byte
		// representative of their established setup.
		if (gallery) {
			showcaseDoc.deps.push_back({ "d_gallery_discovery_wireframes", "discovery", "wireframes", "finish-to-start" });
		}
		int cnt = 0;
		HRESULT hr = InsertGantt(app, showcaseDoc, &cnt);
		if (FAILED(hr)) throw _com_error(hr);
		FitChartRootToSlide(app);

		// A freshly inserted chart leaves the CHART_ROOT group natively selected
		// (PowerPoint shows its selection handles around the whole chart — the
		// "weird selection" over the app). Clear it so the demo opens clean; the
		// overlay drives its own chrome from here on.
		try { app->GetActiveWindow()->GetSelection()->Unselect(); } catch (...) {}

		// Bring PowerPoint to the foreground so the slide view is unoccluded for
		// the screen grab, then start the overlay + force host-active so the
		// docked app bar renders regardless of focus races.
		HWND ppHwnd = (HWND)(INT_PTR)app->GetHWND();
		if (ppHwnd) { ::ShowWindow(ppHwnd, SW_SHOWMAXIMIZED); ::SetForegroundWindow(ppHwnd); }
		PumpFor(400);

		OverlayStart(app);

		// --live: interactive demo. Leave the overlay running with AUTHENTIC
		// host-scoping (no test override) and pump the message loop until the
		// user closes PowerPoint, so the docked app bar is live and clickable.
		bool live = false;
		for (int i = 1; i < argc; ++i) if (wcscmp(argv[i], L"--live") == 0) live = true;
		if (live) {
			wprintf(L"APPBAR-SHOT LIVE: overlay running; close PowerPoint to exit\n");
			::fflush(stdout);
			while (ppHwnd && ::IsWindow(ppHwnd)) PumpFor(200);
			OverlayStop();
			if (gdiToken) Gdiplus::GdiplusShutdown(gdiToken);
			::CoUninitialize();
			return 0;
		}

		// --report : for agent feedback loop, print structured JSON using new dump hook
		bool wantReport = false;
		for (int i = 1; i < argc; ++i) if (wcscmp(argv[i], L"--report") == 0) wantReport = true;
		if (wantReport) {
			Overlay_SetHostActiveOverrideForTest(1);
			PumpFor(1200);
			const char* state = Overlay_DumpChromeStateForTest();
			wprintf(L"REPORT: %hs\n", state ? state : "{}");
			// also capture a png for visual
			bool reportOk = false;
			HWND ab = OverlayAppBarHwnd();
			if (ab && ::IsWindowVisible(ab)) {
				RECT r{}; ::GetWindowRect(ab, &r);
				reportOk = CaptureRectToPng(r, L"native\\build\\appbar-report.png");
			}
			if (reportOk) {
				wprintf(L"REPORT OK\n");
			} else {
				wprintf(L"REPORT FAIL\n");
			}
			OverlayStop();
			if (gdiToken) Gdiplus::GdiplusShutdown(gdiToken);
			::CoUninitialize();
			return reportOk ? 0 : 1;
		}

		// --walkthrough <path.json> : v2.6.0 cold UX walkthrough gate. Executes a
		// real user goal through real posted gestures (see RunWalkthrough above),
		// capturing a PNG per step for reviewer judgment of discoverability +
		// interaction conventions (SR-IXC-01..22). Reserves the select/perform
		// seams for read-only target resolution only, never for the gesture.
		const wchar_t* walkPath = nullptr;
		for (int i = 1; i < argc; ++i) {
			if (wcscmp(argv[i], L"--walkthrough") == 0 && i + 1 < argc) {
				walkPath = argv[i + 1];
				break;
			}
		}
		if (walkPath) {
			int wrc = RunWalkthrough(app, walkPath);
			OverlayStop();
			if (gdiToken) Gdiplus::GdiplusShutdown(gdiToken);
			::CoUninitialize();
			return wrc;
		}

		// --trace <op> : v2.4.0 operation trace for before/immediate/delayed observation.
		// Drives select + op using seams, emits TRACE <step>: {chrome json} and saves
		// step-named PNGs (appbar + wide context including chart body) to detect flashes,
		// sel drops, wrong chrome (e.g. scale on row).
		bool wantTrace = false;
		const wchar_t* traceProfile = nullptr;
		for (int i = 1; i < argc; ++i) {
			if (wcscmp(argv[i], L"--trace") == 0 && i + 1 < argc) {
				wantTrace = true;
				traceProfile = argv[i + 1];
				break;
			}
		}
		if (wantTrace) {
			// i4b-latency-traces (v2.5.3, SR-SMO-02) §1: monotonic zero epoch for
			// every "tMs"/OPDISPATCH timestamp emitted below (GetTickCount64,
			// relative to trace start). Existing profiles' emitted format is
			// unchanged except for the added "tMs" field on each state line.
			const ULONGLONG traceStartTickMs = ::GetTickCount64();
			Overlay_SetHostActiveOverrideForTest(1);
			PumpFor(1200);

			// Stable chart rect for consistent content captures across steps (detect graph + left title disappearance)
			RECT stableChartRect = {0,0,0,0};
			try {
				PowerPoint::DocumentWindowPtr w = app->GetActiveWindow();
				PowerPoint::_SlidePtr sl = w->GetView()->GetSlide();
				PowerPoint::ShapePtr ch;
				PowerPoint::ShapesPtr shs = sl->GetShapes();
				long nn = shs->GetCount();
				for (long ii = 1; ii <= nn; ++ii) {
					PowerPoint::ShapePtr s = shs->Item(_variant_t(ii));
					_bstr_t k = s->GetTags()->Item(_bstr_t(L"PP_KIND"));
					if (k.length() && std::string((const char*)_bstr_t(k)) == "CHART_ROOT") { ch = s; break; }
				}
				if (ch) {
					float cl = ch->GetLeft(), ct = ch->GetTop(), cw = ch->GetWidth(), chh = ch->GetHeight();
					stableChartRect.left = w->PointsToScreenPixelsX(cl) - 140; // include left titles
					stableChartRect.top = w->PointsToScreenPixelsY(ct) - 30;
					stableChartRect.right = w->PointsToScreenPixelsX(cl + cw) + 60;
					stableChartRect.bottom = w->PointsToScreenPixelsY(ct + chh) + 80;
					RECT scr{0,0,::GetSystemMetrics(SM_CXSCREEN),::GetSystemMetrics(SM_CYSCREEN)};
					if (stableChartRect.left < scr.left) stableChartRect.left = scr.left;
					if (stableChartRect.top < scr.top) stableChartRect.top = scr.top;
				}
			} catch (...) {}

			long windowIdlePaintDeltaForTrace = -1;
			// W3: profile-scoped extra JSON fields injected into every TRACE state
			// line while non-empty (same mechanism as windowIdlePaintDelta below,
			// generalized). Each entry must be a complete "key":value pair with a
			// trailing comma.
			std::string extraTraceFieldsForTrace;
			auto captureStep = [&](const char* step, const wchar_t* profile) -> std::wstring {
				Overlay_SyncAppBarForTest();
				const char* state = Overlay_DumpChromeStateForTest();
				// i4b-latency-traces (v2.5.3, SR-SMO-02) §1: stamp every emitted
				// TRACE state line with "tMs" (ms since trace start) so the driver
				// can compute per-op latency. Inserted right after the opening
				// brace; parsers stay backward-tolerant to its absence (older
				// captures / other emitters of this dump).
				std::string stateWithTMs = state ? state : "{}";
				{
					const ULONGLONG tMs = ::GetTickCount64() - traceStartTickMs;
					char tBuf[32];
					::snprintf(tBuf, sizeof(tBuf), "\"tMs\":%llu,", (unsigned long long)tMs);
					size_t bracePos = stateWithTMs.find('{');
					if (bracePos != std::string::npos) stateWithTMs.insert(bracePos + 1, tBuf);
				}
				if (profile && wcscmp(profile, L"window-edge-drag") == 0 && windowIdlePaintDeltaForTrace >= 0) {
					char idleBuf[64];
					::snprintf(idleBuf, sizeof(idleBuf), "\"windowIdlePaintDelta\":%ld,", windowIdlePaintDeltaForTrace);
					size_t bracePos = stateWithTMs.find('{');
					if (bracePos != std::string::npos) stateWithTMs.insert(bracePos + 1, idleBuf);
				}
				if (!extraTraceFieldsForTrace.empty()) {
					size_t bracePos = stateWithTMs.find('{');
					if (bracePos != std::string::npos) stateWithTMs.insert(bracePos + 1, extraTraceFieldsForTrace);
				}
				wchar_t appPng[256], ctxPng[256];
				swprintf_s(appPng, 256, L"native\\build\\trace_%ls_%hs_appbar.png", profile ? profile : L"op", step);
				swprintf_s(ctxPng, 256, L"native\\build\\trace_%ls_%hs_ctx.png", profile ? profile : L"op", step);
				bool ok = false;
				HWND ab = OverlayAppBarHwnd();
				if (ab && ::IsWindowVisible(ab)) {
					RECT r{}; ::GetWindowRect(ab, &r);
					ok = CaptureWindowToPng(ab, appPng);
					// wide context shifted to include chart body above bar
					RECT wide = r;
					wide.top -= 380; wide.left -= 120; wide.right += 120; wide.bottom += 30;
					RECT scr{ 0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN) };
					if (wide.top < scr.top) wide.top = scr.top;
					if (wide.left < scr.left) wide.left = scr.left;
					if (wide.right > scr.right) wide.right = scr.right;
					if (wide.bottom > scr.bottom) wide.bottom = scr.bottom;
					ok = ok && CaptureRectToPng(wide, ctxPng);
				}
				// Also capture main overlay area for full chrome+content visibility
				HWND ov = OverlayHwnd();
				wchar_t ovPng[256];
				swprintf_s(ovPng, 256, L"native\\build\\trace_%ls_%hs_overlay.png", profile ? profile : L"op", step);
				if (ov && ::IsWindowVisible(ov)) {
					RECT or{}; ::GetWindowRect(ov, &or);
					// expand a bit to ensure chart content
					or.top -= 80; or.left -= 40; or.right += 40; or.bottom += 20;
					CaptureRectToPng(or, ovPng);
				}
				// Stable content rect capture (same pixels every step) to hunt disappearing graph bars and left titles
				wchar_t chartPng[256];
				swprintf_s(chartPng, 256, L"native\\build\\trace_%ls_%hs_chart.png", profile ? profile : L"op", step);
				if (stableChartRect.right > stableChartRect.left + 10) {
					CaptureRectToPng(stableChartRect, chartPng);
				}
				// Extra large consistent region capture (generous around overlay) for reliable visual diff of content presence
				wchar_t largePng[256];
				swprintf_s(largePng, 256, L"native\\build\\trace_%ls_%hs_large.png", profile ? profile : L"op", step);
				if (ov && ::IsWindowVisible(ov)) {
					RECT lr{}; ::GetWindowRect(ov, &lr);
					lr.left -= 220; lr.top -= 120; lr.right += 80; lr.bottom += 180;
					RECT scr{0,0,::GetSystemMetrics(SM_CXSCREEN),::GetSystemMetrics(SM_CYSCREEN)};
					if (lr.left<scr.left) lr.left=scr.left; if (lr.top<scr.top) lr.top=scr.top;
					CaptureRectToPng(lr, largePng);
				}
				// W2 proof uses a tight header-only capture: a full-chart hash could
				// change for unrelated host chrome, while this exact region proves the
				// candidate axis preview overwrote the frozen native header.
				wchar_t headerPng[256] = L"";
				if (profile && wcscmp(profile, L"window-edge-drag") == 0) {
					RECT header{};
					if (ParseNamedRectFromDump(state, "windowHeaderBandRect", &header)
						&& header.right > header.left && header.bottom > header.top) {
						swprintf_s(headerPng, 256, L"native\\build\\trace_%ls_%hs_header.png", profile, step);
						CaptureRectToPng(header, headerPng);
					}
				}
				wprintf(L"TRACE %hs: %hs\n", step, stateWithTMs.c_str());
				wprintf(L"TRACE %hs ARTIFACTS: %ls %ls %ls %ls %ls\n", step, appPng, ctxPng, ovPng, chartPng, largePng);
				if (headerPng[0]) wprintf(L"TRACE %hs ARTIFACTS: %ls\n", step, headerPng);
				return std::wstring(appPng);
			};

			// i4b-latency-traces (v2.5.3, SR-SMO-02) §1: print the OPDISPATCH
			// marker (same relative clock as the "tMs" stamps above) at the exact
			// moment a profile performs its operation via the app-bar perform /
			// seam call, e.g. "TRACE OPDISPATCH: {"tMs":1234}". The driver parses
			// this the same way it parses a step line (step name "OPDISPATCH")
			// and treats its "tMs" as opDispatchTMs for latency math.
			auto emitOpDispatch = [&]() {
				const ULONGLONG tMs = ::GetTickCount64() - traceStartTickMs;
				wprintf(L"TRACE OPDISPATCH: {\"tMs\":%llu}\n", (unsigned long long)tMs);
			};
			// i4b-latency-traces fix: step "tMs" stamps precede PNG captures but the
			// driver used step deltas for opLatencyMs, so multi-second capture
			// overhead polluted the budget. Emit authoritative synchronous dispatch
			// duration immediately after the seam call (COM rebuild completes inside).
			auto performOpWithLatency = [&](int cmd) {
				const ULONGLONG t0 = ::GetTickCount64();
				Overlay_PerformAppBarCommandForTest(cmd);
				const ULONGLONG elapsed = ::GetTickCount64() - t0;
				wprintf(L"TRACE OPLATENCY: {\"ms\":%llu}\n", (unsigned long long)elapsed);
				char phaseBuf[512];
				const int phaseLen = Gantt_GetLastOpPhasesForTest(phaseBuf, (int)sizeof(phaseBuf));
				if (phaseLen > 0)
					wprintf(L"TRACE OPPHASES: %hs\n", phaseBuf);
			};

			// Pre setup depends on profile. Flows whose op IS the selection
			// (row-label-select / row-then-overall / task-*) and the overall-*
			// auto-clear flows start clean; the row op flows (row-add-below /
			// row-rename / row-scale) run their op "while row selected", so
			// select the row up front and simulate the common native state
			// (CHART_ROOT stays natively selected after an item click because
			// children are suppressed) so the op runs under the takeover guard.
			bool traceCleanStart = traceProfile && (
				wcscmp(traceProfile, L"row-label-select") == 0 ||
				wcscmp(traceProfile, L"row-then-overall") == 0 ||
				wcscmp(traceProfile, L"task-select-progress") == 0 ||
				wcscmp(traceProfile, L"task-bar-unit") == 0 ||
				wcscmp(traceProfile, L"entity-dump") == 0 ||
				wcscmp(traceProfile, L"session-recorder") == 0 ||
				wcscmp(traceProfile, L"component-shape-protection") == 0 ||
				wcscmp(traceProfile, L"appbar-docked") == 0 ||
				wcscmp(traceProfile, L"appbar-context-evolution") == 0 ||
				wcscmp(traceProfile, L"multi-row-delete") == 0 ||
				wcscmp(traceProfile, L"drag-paint-cadence") == 0 ||
				wcsstr(traceProfile, L"overall-"));
			if (traceCleanStart) {
				Overlay_SelectForTest("", ""); // clean start: the branch drives selection itself
			} else {
				Overlay_SelectForTest("ROW", "research");
				try {
					PowerPoint::ShapesPtr shs = slide->GetShapes();
					long nn = shs->GetCount();
					for (long ii = 1; ii <= nn; ++ii) {
						auto s = shs->Item(_variant_t(ii));
						_bstr_t k = s->GetTags()->Item(_bstr_t(L"PP_KIND"));
						if (k.length() && std::string((const char*)_bstr_t(k)) == "CHART_ROOT") {
							s->Select(Office::msoTrue);
							break;
						}
					}
				} catch (...) {}
			}
			PumpFor(300);
			captureStep("pre", traceProfile);

			// overall component (CHART_ROOT) move/resize simulation for hunting weird UX
			// (user drag on group or grip). We mutate the native shape directly then
			// observe overlay/appbar/rowBands/chartRect recovery over ticks + captures.
			PowerPoint::ShapePtr overallChart;
			try {
				PowerPoint::DocumentWindowPtr w2 = app->GetActiveWindow();
				PowerPoint::_SlidePtr sl2 = w2->GetView()->GetSlide();
				PowerPoint::ShapesPtr shs = sl2->GetShapes();
				long n = shs->GetCount();
				for (long i=1; i<=n; ++i) {
					auto s = shs->Item(_variant_t(i));
					_bstr_t k = s->GetTags()->Item(_bstr_t(L"PP_KIND"));
					if (k.length() && std::string((const char*)_bstr_t(k)) == "CHART_ROOT") { overallChart = s; break; }
				}
			} catch (...) {}

			if (traceProfile && (wcscmp(traceProfile, L"overall-move") == 0 || wcscmp(traceProfile, L"overall-resize") == 0) && overallChart) {
				float origL = overallChart->GetLeft();
				float origT = overallChart->GetTop();
				float origW = overallChart->GetWidth();
				float origH = overallChart->GetHeight();
				if (wcscmp(traceProfile, L"overall-move") == 0) {
					overallChart->PutLeft(origL + 60.0f);  // discrete move
					overallChart->PutTop(origT + 20.0f);
				} else {
					overallChart->PutWidth(origW + 120.0f);  // resize wider
					overallChart->PutHeight(origH + 40.0f);
				}
				// exercise native select of root (like grip) to trigger auto-clear of item sel + chart follow
				try { overallChart->Select(Office::msoTrue); } catch(...) {}
				PumpFor(80);  // let at least one partial Tick run to update g_chartScreenRect, bands, possibly clear ownSel
				captureStep("immed", traceProfile);
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
				// restore for cleanliness (not required but keeps harness state nice)
				try {
					overallChart->PutLeft(origL);
					overallChart->PutTop(origT);
					overallChart->PutWidth(origW);
					overallChart->PutHeight(origH);
				} catch(...) {}
				// fall through to stop
			}

			if (traceProfile && wcscmp(traceProfile, L"row-add-below") == 0) {
				// "New row below" while row selected -- primary reported flash / sel issue
				Overlay_PerformAppBarCommandForTest(HtCmd_AddRowBelow);
				captureStep("immed", traceProfile);
				PumpFor(150); // one tick
				captureStep("+1", traceProfile);
				PumpFor(450); // +3 total
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"row-rename") == 0) {
				// Rename row (opens inline editor; commit will rebuild)
				Overlay_PerformAppBarCommandForTest(HtCmd_Rename);
				captureStep("immed", traceProfile);
				PumpFor(150);
				captureStep("+1", traceProfile);
				// For rename trace we stop here; full commit would require editor drive (see overlay-test)
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"row-scale") == 0) {
				// Attempt scale while row selected (should not show scale chrome per v2.4.1)
				// Just select row + observe (no scale op since global)
				// Dump already shows hasScaleGroup and scale
				captureStep("immed", traceProfile);
				PumpFor(150);
				captureStep("+1", traceProfile);
				Overlay_PerformAppBarCommandForTest(HtCmd_ScaleWeek); // try anyway to exercise
				captureStep("+3", traceProfile);
			} else if (traceProfile && (wcscmp(traceProfile, L"overall-move") == 0 || wcscmp(traceProfile, L"overall-resize") == 0)) {
				// handled above
			} else if (traceProfile && wcscmp(traceProfile, L"row-label-select") == 0) {
				// Simulate hover (via bands) then click select row. Check rowLabelCount does not drop (title must not disappear).
				Overlay_SelectForTest("ROW", "research");
				// Simulate the common case (native PPT sel remains CHART_ROOT group after item click, because we suppress children).
				// Pre-fix this would cause Tick to set full-chart selScreenRect/frameRect (taking over entire drawing area).
				try {
					PowerPoint::ShapesPtr shs = slide->GetShapes();
					long nn = shs->GetCount();
					for (long ii=1; ii<=nn; ++ii) {
						auto s = shs->Item(_variant_t(ii));
						_bstr_t k = s->GetTags()->Item(_bstr_t(L"PP_KIND"));
						if (k.length() && std::string((const char*)_bstr_t(k)) == "CHART_ROOT") {
							s->Select(Office::msoTrue);
							break;
						}
					}
				} catch(...) {}
				PumpFor(200);
				captureStep("immed", traceProfile);
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"row-then-overall") == 0) {
				// Select row, then overall component. Must not cause content (graph + titles) to disappear.
				Overlay_SelectForTest("ROW", "research");
				PumpFor(150);
				captureStep("row-sel", traceProfile);
				// Select overall (native root like grip or direct)
				try {
					PowerPoint::ShapesPtr shs = slide->GetShapes();
					long nn = shs->GetCount();
					for (long ii=1; ii<=nn; ++ii) {
						auto s = shs->Item(_variant_t(ii));
						_bstr_t k = s->GetTags()->Item(_bstr_t(L"PP_KIND"));
						if (k.length() && std::string((const char*)_bstr_t(k)) == "CHART_ROOT") {
							s->Select(Office::msoTrue);
							break;
						}
					}
				} catch(...) {}
				PumpFor(200);
				captureStep("overall-after", traceProfile);
				PumpFor(150);
				captureStep("+1", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"task-scale-keep-sel") == 0) {
				// v2.6.3 SR-BAR-02: scale lives in document context. Deselect,
				// change scale from component bar, then re-select the task.
				Overlay_SelectForTest("", "");
				PumpFor(300);
				captureStep("pre", traceProfile);

				Overlay_PerformAppBarCommandForTest(HtCmd_ScaleMonth);
				captureStep("scale-immed", traceProfile);
				PumpFor(150);
				captureStep("scale+1", traceProfile);
				Overlay_SelectForTest("TASK", "discovery");
				SelectChartRootNatively(slide);
				PumpFor(300);
				captureStep("task-resel", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"scale-settings") == 0) {
				// U7: document-context display settings are persisted in PP_DOC.
				// Drive the same app-bar command handler that the Settings popover
				// returns to, then prove pull-from-slide + reflow retain every value.
				Overlay_SelectForTest("", "");
				PumpFor(300);
				captureStep("document-context", traceProfile);
				Overlay_ShowSettingsMenuForTest();
				PumpFor(100);
				captureStep("settings-open", traceProfile);
				ThemeMenu_Dismiss();
				PumpFor(100);
				Overlay_PerformAppBarCommandForTest(HtCmd_GridWeek);
				PumpFor(150);
				captureStep("grid-week", traceProfile);
				Overlay_PerformAppBarCommandForTest(HtCmd_AxisNumbersCW);
				PumpFor(150);
				captureStep("axis-cw", traceProfile);
				Overlay_PerformAppBarCommandForTest(HtCmd_RailLabelsOn);
				PumpFor(150);
				captureStep("rail-on", traceProfile);

				bool settingsRoundTrip = false;
				try {
					PpDocument pulled = DocumentFromJson(ReadGanttFromSlide(app));
					bool reflowChanged = false;
					HRESULT reflowHr = ReflowFromSlide(app, &reflowChanged);
					PpDocument reflowed = DocumentFromJson(ReadGanttFromSlide(app));
					settingsRoundTrip = SUCCEEDED(reflowHr)
						&& pulled.gridDensity == "week" && pulled.axisNumbering == "cw" && pulled.railLabels
						&& reflowed.gridDensity == "week" && reflowed.axisNumbering == "cw" && reflowed.railLabels;
				} catch (...) {
					settingsRoundTrip = false;
				}
				PumpFor(300);
				captureStep("roundtrip", traceProfile);
				const char* state = Overlay_DumpChromeStateForTest();
				const bool axisHasCw = state && ::strstr(state, "\"axisNumbering\":\"cw\"")
					&& ::strstr(state, "\"firstAxisLabel\":\"CW ");
				wprintf(settingsRoundTrip && axisHasCw ? L"SCALE SETTINGS ROUNDTRIP OK\n" : L"SCALE SETTINGS ROUNDTRIP FAIL\n");
				if (!settingsRoundTrip || !axisHasCw) {
					OverlayStop();
					if (gdiToken) Gdiplus::GdiplusShutdown(gdiToken);
					::CoUninitialize();
					return 1;
				}
			} else if (traceProfile && wcscmp(traceProfile, L"window-repair-lossless") == 0) {
				// W1 / SR-WIN-21 proof: apply a narrow explicit window directly through
				// the pure op, then exercise BOTH Repair layout and FitChartRootToFrame
				// (which defensively calls ReflowFromSlide). The fields dumped at every
				// step let harness_driver verify PP_DOC dates byte-for-byte and PP_PROJ.
				auto datesSignature = [](const PpDocument& doc) {
					std::string out;
					for (const auto& task : doc.tasks) out += task.id + ":" + task.start + ":" + task.end + ";";
					for (const auto& milestone : doc.milestones) out += milestone.id + ":" + milestone.date + ";";
					return out;
				};
				auto findChartRoot = [&]() -> PowerPoint::ShapePtr {
					try {
						PowerPoint::ShapesPtr shapes = slide->GetShapes();
						for (long i = 1; i <= shapes->GetCount(); ++i) {
							PowerPoint::ShapePtr candidate = shapes->Item(_variant_t(i));
							_bstr_t kind = candidate->GetTags()->Item(_bstr_t(L"PP_KIND"));
							if (kind.length() && std::string((const char*)_bstr_t(kind)) == "CHART_ROOT") return candidate;
						}
					} catch (...) {}
					return nullptr;
				};

				PpDocument before = DocumentFromJson(ReadGanttFromSlide(app));
				const std::string datesBefore = datesSignature(before);
				PpDocument windowed = before;
				const bool setOk = SetTimeWindow(windowed, "2026-06-15", "2026-07-15");
				const HRESULT setHr = setOk ? UpdateGantt(app, windowed) : E_FAIL;
				PumpFor(350);
				captureStep("window", traceProfile);

				bool repairChanged = true;
				const HRESULT repairHr = SUCCEEDED(setHr) ? ReflowFromSlide(app, &repairChanged) : E_FAIL;
				const bool repairDatesOk = SUCCEEDED(repairHr)
					&& datesSignature(DocumentFromJson(ReadGanttFromSlide(app))) == datesBefore;
				PumpFor(250);
				captureStep("repair", traceProfile);

				HRESULT resizeHr = E_FAIL;
				if (PowerPoint::ShapePtr root = findChartRoot()) {
					resizeHr = FitChartRootToFrame(app, root->GetLeft() + 12.0f, root->GetTop() + 8.0f,
						root->GetWidth() * 0.94f, root->GetHeight() * 0.94f);
				}
				const bool resizeDatesOk = SUCCEEDED(resizeHr)
					&& datesSignature(DocumentFromJson(ReadGanttFromSlide(app))) == datesBefore;
				PumpFor(300);
				captureStep("resize", traceProfile);

				PpDocument cleared = DocumentFromJson(ReadGanttFromSlide(app));
				const bool clearOk = ClearTimeWindow(cleared);
				const HRESULT clearHr = clearOk ? UpdateGantt(app, cleared) : E_FAIL;
				const bool clearDatesOk = SUCCEEDED(clearHr)
					&& datesSignature(DocumentFromJson(ReadGanttFromSlide(app))) == datesBefore;
				PumpFor(300);
				captureStep("cleared", traceProfile);
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
				wprintf(setOk && SUCCEEDED(setHr) && repairDatesOk && resizeDatesOk && clearDatesOk
					? L"WINDOW REPAIR LOSSLESS OK\n" : L"WINDOW REPAIR LOSSLESS FAIL\n");
			} else if (traceProfile && wcscmp(traceProfile, L"task-nudge-latency") == 0) {
				// i4b-latency-traces (v2.5.3, SR-SMO-02) §2: select TASK t1 (showcase
				// doc has no literal t1; same "discovery" substitution as
				// task-scale-keep-sel) -> dispatch +1d via the app-bar perform seam
				// -> standard pre/immed/+1/+3 captures with "tMs" timestamps, plus a
				// single OPDISPATCH marker at the exact dispatch instant so the
				// driver can compute opLatencyMs.
				Overlay_SelectForTest("TASK", "discovery");
				try {
					PowerPoint::ShapesPtr shs = slide->GetShapes();
					long nn = shs->GetCount();
					for (long ii = 1; ii <= nn; ++ii) {
						auto s = shs->Item(_variant_t(ii));
						_bstr_t k = s->GetTags()->Item(_bstr_t(L"PP_KIND"));
						if (k.length() && std::string((const char*)_bstr_t(k)) == "CHART_ROOT") {
							s->Select(Office::msoTrue);
							break;
						}
					}
				} catch (...) {}
				PumpFor(300);
				captureStep("pre", traceProfile);
				emitOpDispatch();
				performOpWithLatency(HtCmd_NudgePlus1);
				captureStep("immed", traceProfile);
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"task-color-latency") == 0) {
				// i4b-latency-traces (v2.5.3, SR-SMO-02) §2: select TASK t1 -> dispatch
				// a swatch color command via the app-bar perform seam -> standard
				// pre/immed/+1/+3 captures + OPDISPATCH marker (see task-nudge-latency
				// above for the shared rationale).
				Overlay_SelectForTest("TASK", "discovery");
				try {
					PowerPoint::ShapesPtr shs = slide->GetShapes();
					long nn = shs->GetCount();
					for (long ii = 1; ii <= nn; ++ii) {
						auto s = shs->Item(_variant_t(ii));
						_bstr_t k = s->GetTags()->Item(_bstr_t(L"PP_KIND"));
						if (k.length() && std::string((const char*)_bstr_t(k)) == "CHART_ROOT") {
							s->Select(Office::msoTrue);
							break;
						}
					}
				} catch (...) {}
				PumpFor(300);
				captureStep("pre", traceProfile);
				emitOpDispatch();
				performOpWithLatency(HtCmd_Swatch3);
				captureStep("immed", traceProfile);
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"hover-quick-add-task") == 0) {
				Overlay_SelectForTest("", "");
				PumpFor(300);
				captureStep("pre", traceProfile);
				// Drive the SAME code path the row-gutter "+" chip click uses
				// (input-neutral seam) instead of only hovering via the cursor
				// override, which never actually triggered the action. Uses the
				// same showcase row id the row-scale profile selects ("research",
				// via Overlay_SelectForTest("ROW", "research") in the shared
				// non-clean-start setup above -- this profile isn't in the
				// traceCleanStart list either, so it goes through that same path).
				Overlay_PerformHoverQuickAddForTest("research");
				captureStep("immed", traceProfile);
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"drag-commit-echo") == 0) {
				// SR-SMO-06: drag-commit keeps echo geometry through rebuild.
				Overlay_SelectForTest("TASK", "discovery");
				SelectChartRootNatively(slide);
				PumpFor(400);
				captureStep("pre", traceProfile);
				HWND ov = OverlayHwnd();
				POINT center{};
				if (ov && FindTaskBodyCenter(app, "discovery", &center)) {
					PostTaskBodyDrag(ov, center, 90);
				}
				captureStep("immed", traceProfile);
				const char* immedDump = Overlay_DumpChromeStateForTest();
				wprintf(L"DRAGCOMMITECHO marker=%hs\n",
					(immedDump && ::strstr(immedDump, "\"hasCommitEcho\":true")) ? "echo-active" : "echo-cleared");
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"inline-rename-task") == 0) {
				// SR-SMO-07: Rename on TASK opens inline editor (not card).
				Overlay_SelectForTest("TASK", "discovery");
				SelectChartRootNatively(slide);
				PumpFor(400);
				captureStep("pre", traceProfile);
				Overlay_PerformAppBarCommandForTest(HtCmd_Rename);
				captureStep("immed", traceProfile);
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"task-select-progress") == 0) {
				// v2.6.2: progress edge drag on wireframes (40%) instead of ±10% steppers.
				Overlay_SelectForTest("TASK", "wireframes");
				try {
					PowerPoint::ShapesPtr shs = slide->GetShapes();
					long nn = shs->GetCount();
					for (long ii=1; ii<=nn; ++ii) {
						auto s = shs->Item(_variant_t(ii));
						_bstr_t k = s->GetTags()->Item(_bstr_t(L"PP_KIND"));
						if (k.length() && std::string((const char*)_bstr_t(k)) == "CHART_ROOT") {
							s->Select(Office::msoTrue);
							break;
						}
					}
				} catch(...) {}
				Overlay_InvalidateAppBarForTest();
				PumpFor(400);
				captureStep("task-sel", traceProfile);
				HWND ov = OverlayHwnd();
				POINT edge{};
				if (ov && FindTaskProgressEdge(app, "wireframes", 40, &edge)) {
					PostScreenDrag(ov, edge, 48);
				}
				Overlay_InvalidateAppBarForTest();
				PumpFor(400);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"drag-date-pill") == 0) {
				Overlay_SelectForTest("TASK", "interviews");
				SelectChartRootNatively(slide);
				PumpFor(400);
				captureStep("pre", traceProfile);
				HWND ov = OverlayHwnd();
				POINT center{};
				if (ov && FindTaskBodyCenter(app, "interviews", &center)) {
					// Inline drag with a MID-GESTURE capture: the date pill only
					// exists while the button is down, so dump it before mouse-up.
					POINT clientPt = center;
					::ScreenToClient(ov, &clientPt);
					Overlay_SetCursorPosOverrideForTest(true, center);
					::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM((short)clientPt.x, (short)clientPt.y));
					PumpFor(60);
					const int dragPx = 90, dragSteps = 6;
					for (int s = 1; s <= dragSteps; ++s) {
						POINT sp = { center.x + dragPx * s / dragSteps, center.y };
						Overlay_SetCursorPosOverrideForTest(true, sp);
						::PostMessageW(ov, WM_MOUSEMOVE, 0, MAKELPARAM((short)(clientPt.x + dragPx * s / dragSteps), (short)clientPt.y));
						PumpFor(40);
					}
					captureStep("mid", traceProfile);
					POINT up = { center.x + dragPx, center.y };
					Overlay_SetCursorPosOverrideForTest(true, up);
					::PostMessageW(ov, WM_LBUTTONUP, 0, MAKELPARAM((short)(clientPt.x + dragPx), (short)clientPt.y));
					PumpFor(400);
				}
				captureStep("immed", traceProfile);
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"drag-paint-cadence") == 0) {
				// Phase 13 v2.8.1 SR-SMO-09..12: continuous paint cadence during
				// a long mid-gesture task drag (>=300 ms of motion).
				Overlay_SelectForTest("TASK", "interviews");
				SelectChartRootNatively(slide);
				PumpFor(400);
				captureStep("pre", traceProfile);
				HWND ov = OverlayHwnd();
				POINT center{};
				if (ov && FindTaskBodyCenter(app, "interviews", &center)) {
					POINT clientPt = center;
					::ScreenToClient(ov, &clientPt);
					Overlay_SetCursorPosOverrideForTest(true, center);
					::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM((short)clientPt.x, (short)clientPt.y));
					PumpFor(40);
					Overlay_BeginPaintCadenceSampleForTest();
					// Denser moves over >=300 ms so paint cadence reflects continuous
					// drag preview (SR-SMO-09), not a sparse 6-step sample.
					const int dragPx = 120, dragSteps = 40;
					for (int s = 1; s <= dragSteps; ++s) {
						POINT sp = { center.x + dragPx * s / dragSteps, center.y };
						Overlay_SetCursorPosOverrideForTest(true, sp);
						::PostMessageW(ov, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM((short)(clientPt.x + dragPx * s / dragSteps), (short)clientPt.y));
						// Pump one paint cycle; 16 ms ≈ 60 Hz host cadence.
						PumpFor(16);
					}
					double hz = 0.0, p50 = 0.0, p95 = 0.0;
					long pc = 0, wms = 0;
					Overlay_GetPaintCadenceForTest(&hz, &pc, &wms, &p50, &p95);
					char cadExtra[220];
					::snprintf(cadExtra, sizeof(cadExtra),
						"\"paintHz\":%.2f,\"paintCount\":%ld,\"paintWindowMs\":%ld,\"paintP50Ms\":%.2f,\"paintP95Ms\":%.2f,",
						hz, pc, wms, p50, p95);
					extraTraceFieldsForTrace = cadExtra;
					captureStep("mid", traceProfile);
					extraTraceFieldsForTrace.clear();
					wprintf(L"TRACE CADENCE: {\"hz\":%.2f,\"paintCount\":%ld,\"windowMs\":%ld,\"p50Ms\":%.2f,\"p95Ms\":%.2f,\"budgetHz\":30}\n",
						hz, pc, wms, p50, p95);
					::fflush(stdout);
					Overlay_EndPaintCadenceSampleForTest();
					POINT up = { center.x + dragPx, center.y };
					Overlay_SetCursorPosOverrideForTest(true, up);
					::PostMessageW(ov, WM_LBUTTONUP, 0, MAKELPARAM((short)(clientPt.x + dragPx), (short)clientPt.y));
					PumpFor(400);
				}
				captureStep("immed", traceProfile);
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"link-drag-port") == 0) {
				Overlay_SelectForTest("TASK", "discovery");
				SelectChartRootNatively(slide);
				PumpFor(400);
				captureStep("pre", traceProfile);
				const char* preDump = Overlay_DumpChromeStateForTest();
				int preDeps = 0;
				if (preDump) {
					const char* p = ::strstr(preDump, "\"depCount\":");
					if (p) ::sscanf_s(p, "\"depCount\":%d", &preDeps);
				}
				HWND ov = OverlayHwnd();
				POINT portPt{}, targetPt{};
				RECT portRect{};
				if (ov && ParseNamedRectFromDump(preDump, "linkPortRightRect", &portRect)
					&& portRect.right > portRect.left
					&& FindTaskBodyCenter(app, "wireframes", &targetPt)) {
					// Drag from the dump-published port hit rect (ground truth from the
					// overlay's own hit-test geometry — no independent recompute).
					portPt = { (portRect.left + portRect.right) / 2, (portRect.top + portRect.bottom) / 2 };
					PostScreenDrag(ov, portPt, targetPt.x - portPt.x, targetPt.y - portPt.y);
				} else {
					wprintf(L"LINKPORT: linkPortRightRect not published for selection\n");
				}
				captureStep("immed", traceProfile);
				const char* immedDump = Overlay_DumpChromeStateForTest();
				int postDeps = preDeps;
				if (immedDump) {
					const char* p = ::strstr(immedDump, "\"depCount\":");
					if (p) ::sscanf_s(p, "\"depCount\":%d", &postDeps);
				}
				wprintf(L"LINKPORT deps %d->%d\n", preDeps, postDeps);
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"row-adder-boundaries") == 0) {
				Overlay_SelectForTest("", "");
				PumpFor(300);
				captureStep("pre", traceProfile);
				const char* preDump = Overlay_DumpChromeStateForTest();
				int preRows = 0;
				if (preDump) {
					const char* p = ::strstr(preDump, "\"rowCount\":");
					if (p) ::sscanf_s(p, "\"rowCount\":%d", &preRows);
				}
				HWND ov = OverlayHwnd();
				RECT band{};
				if (ov && ParseRowBandFromDump(preDump, "research", &band)) {
					// Hover INSIDE the row's left-rail area first (band-center x sits
					// on the TODAY marker band) so the boundary chips lay out, then
					// click the ABOVE chip at its dump-published ground-truth rect.
					POINT hoverPt = { band.left + 40, (band.top + band.bottom) / 2 };
					Overlay_SetCursorPosOverrideForTest(true, hoverPt);
					POINT hoverClient = hoverPt;
					::ScreenToClient(ov, &hoverClient);
					::PostMessageW(ov, WM_MOUSEMOVE, 0, MAKELPARAM((short)hoverClient.x, (short)hoverClient.y));
					PumpFor(400);
					RECT chip{};
					if (ParseNamedRectFromDump(Overlay_DumpChromeStateForTest(), "rowAdderAboveRect", &chip)
						&& chip.right > chip.left) {
						POINT chipPt = { (chip.left + chip.right) / 2, (chip.top + chip.bottom) / 2 };
						Overlay_SetCursorPosOverrideForTest(true, chipPt);
						POINT clientPt = chipPt;
						::ScreenToClient(ov, &clientPt);
						LPARAM lp = MAKELPARAM((short)clientPt.x, (short)clientPt.y);
						::PostMessageW(ov, WM_MOUSEMOVE, 0, lp);
						PumpFor(150);
						::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, lp);
						::PostMessageW(ov, WM_LBUTTONUP, 0, lp);
					} else {
						wprintf(L"ROWADDER: rowAdderAboveRect not published after hover\n");
					}
				}
				PumpFor(400);
				captureStep("immed", traceProfile);
				const char* immedDump = Overlay_DumpChromeStateForTest();
				int postRows = preRows;
				if (immedDump) {
					const char* p = ::strstr(immedDump, "\"rowCount\":");
					if (p) ::sscanf_s(p, "\"rowCount\":%d", &postRows);
				}
				wprintf(L"ROWADDER rows %d->%d\n", preRows, postRows);
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"drag-row-retarget") == 0) {
				Overlay_SelectForTest("TASK", "interviews");
				SelectChartRootNatively(slide);
				PumpFor(400);
				captureStep("pre", traceProfile);
				HWND ov = OverlayHwnd();
				POINT center{};
				const char* preDump = Overlay_DumpChromeStateForTest();
				RECT researchBand{}, designBand{};
				if (ParseRowBandFromDump(preDump, "research", &researchBand)
					&& ParseRowBandFromDump(preDump, "design", &designBand)
					&& ov && FindTaskBodyCenter(app, "interviews", &center)) {
					const int researchCy = (researchBand.top + researchBand.bottom) / 2;
					const int designCy = (designBand.top + designBand.bottom) / 2;
					const int dragDy = designCy - researchCy;
					PostScreenDrag(ov, center, 0, dragDy);
				}
				captureStep("immed", traceProfile);
				{
					char phaseBuf[512];
					const int phaseLen = Gantt_GetLastOpPhasesForTest(phaseBuf, (int)sizeof(phaseBuf));
					if (phaseLen > 0)
						wprintf(L"TRACE OPPHASES: %hs\n", phaseBuf);
				}
				const char* immedDump = Overlay_DumpChromeStateForTest();
				wprintf(L"DRAGRETARGET targetRow=%hs\n",
					immedDump && ::strstr(immedDump, "dragTargetRowId") ? ::strstr(immedDump, "dragTargetRowId") : "(none)");
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"marker-snap") == 0) {
				Overlay_SelectForTest("MARKER", "today");
				SelectChartRootNatively(slide);
				PumpFor(400);
				captureStep("pre", traceProfile);
				HWND ov = OverlayHwnd();
				POINT mkPt{};
				if (ov && FindMarkerDragPoint(app, "today", &mkPt)) {
					PostScreenDragStart(ov, mkPt, 42);
					captureStep("mid", traceProfile);
					PostScreenDragFinish(ov, mkPt, 42);
				}
				captureStep("immed", traceProfile);
				PumpFor(200);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"window-edge-drag") == 0) {
				// W2: real hover + posted mouse gesture, with target coordinates read
				// exclusively from overlay dump rectangles (no geometry duplicate).
				Overlay_SelectForTest("", "");
				Overlay_SetCursorPosOverrideForTest(true, POINT{ 0, 0 });
				PumpFor(300);
				long paintsBefore = 0, appPaintsBefore = 0, swpBefore = 0, appSwpBefore = 0;
				long paintsAfter = 0, appPaintsAfter = 0, swpAfter = 0, appSwpAfter = 0;
				Overlay_GetRenderCountersForTest(&paintsBefore, &appPaintsBefore, &swpBefore, &appSwpBefore);
				PumpFor(300); // idle-stable proof: no hover transition, hence no overlay paint
				Overlay_GetRenderCountersForTest(&paintsAfter, &appPaintsAfter, &swpAfter, &appSwpAfter);
				windowIdlePaintDeltaForTrace = paintsAfter - paintsBefore;
				captureStep("pre", traceProfile);

				HWND ov = OverlayHwnd();
				const char* preDump = Overlay_DumpChromeStateForTest();
				RECT header{}, rightPort{};
				if (ov && ParseNamedRectFromDump(preDump, "windowHeaderBandRect", &header)
					&& header.right > header.left && header.bottom > header.top) {
					POINT hover = { (header.left + header.right) / 2, (header.top + header.bottom) / 2 };
					POINT hoverClient = hover;
					::ScreenToClient(ov, &hoverClient);
					Overlay_SetCursorPosOverrideForTest(true, hover);
					::PostMessageW(ov, WM_MOUSEMOVE, 0, MAKELPARAM((short)hoverClient.x, (short)hoverClient.y));
					PumpFor(160);
					captureStep("hover", traceProfile);

					if (ParseNamedRectFromDump(Overlay_DumpChromeStateForTest(), "windowPortRRect", &rightPort)
						&& rightPort.right > rightPort.left && rightPort.bottom > rightPort.top) {
						POINT port = { (rightPort.left + rightPort.right) / 2, (rightPort.top + rightPort.bottom) / 2 };
						// The screenshot harness runs near 200% DPI; ~340 px is about
						// two visible week units and stays far from the min-span clamp.
						PostScreenDragStart(ov, port, -340);
						captureStep("mid", traceProfile);
						PostScreenDragFinish(ov, port, -340);
					} else {
						wprintf(L"WINDOWEDGE: windowPortRRect not published after header hover\n");
					}
				} else {
					wprintf(L"WINDOWEDGE: windowHeaderBandRect not published\n");
				}
				captureStep("immed", traceProfile);
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);

				// W3 m9: zero-delta release. Re-hover to reveal the ports on the
				// now-explicit window, press the RIGHT port, cross the drag
				// threshold, return to the origin, release. The real commit must
				// treat candidate == baseline as a pure no-op (no doc write, no
				// rebuild, no undo entry) — asserted by the driver comparing the
				// zerodelta step's doc fields against +3.
				if (ov) {
					const char* postDump = Overlay_DumpChromeStateForTest();
					RECT header2{}, rightPort2{};
					if (ParseNamedRectFromDump(postDump, "windowHeaderBandRect", &header2)
						&& header2.right > header2.left && header2.bottom > header2.top) {
						POINT hover2 = { (header2.left + header2.right) / 2, (header2.top + header2.bottom) / 2 };
						POINT hover2Client = hover2;
						::ScreenToClient(ov, &hover2Client);
						Overlay_SetCursorPosOverrideForTest(true, hover2);
						::PostMessageW(ov, WM_MOUSEMOVE, 0, MAKELPARAM((short)hover2Client.x, (short)hover2Client.y));
						PumpFor(160);
						if (ParseNamedRectFromDump(Overlay_DumpChromeStateForTest(), "windowPortRRect", &rightPort2)
							&& rightPort2.right > rightPort2.left && rightPort2.bottom > rightPort2.top) {
							POINT port2 = { (rightPort2.left + rightPort2.right) / 2, (rightPort2.top + rightPort2.bottom) / 2 };
							POINT port2Client = port2;
							::ScreenToClient(ov, &port2Client);
							Overlay_SetCursorPosOverrideForTest(true, port2);
							::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM((short)port2Client.x, (short)port2Client.y));
							PumpFor(60);
							static const int kZeroDeltaPath[] = { 20, 40, 60, 40, 20, 0 };
							for (int dx : kZeroDeltaPath) {
								POINT sp = { port2.x + dx, port2.y };
								Overlay_SetCursorPosOverrideForTest(true, sp);
								::PostMessageW(ov, WM_MOUSEMOVE, 0, MAKELPARAM((short)(port2Client.x + dx), (short)port2Client.y));
								PumpFor(40);
							}
							::PostMessageW(ov, WM_LBUTTONUP, 0, MAKELPARAM((short)port2Client.x, (short)port2Client.y));
							PumpFor(400);
						} else {
							wprintf(L"WINDOWEDGE: zero-delta port not published after re-hover\n");
						}
					} else {
						wprintf(L"WINDOWEDGE: zero-delta header band not published\n");
					}
				}
				captureStep("zerodelta", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"window-clip-rerender") == 0) {
				// W3: lossless clip/hide + M4 hidden-selection reset + C2 clipped-
				// chart anchor math, all e2e over the REAL commit dispatch.
				// Window [2026-06-15 .. 2026-07-27] on the showcase doc hides
				// 'discovery' (06-01..06-12), clips 'interviews' (06-08..06-19) at
				// the left edge and 'qa_t' (07-27..08-07) at the right edge.
				Overlay_SelectForTest("TASK", "discovery");
				SelectChartRootNatively(slide);
				PumpFor(400);
				std::vector<ChartChildGeom> geomsPre;
				const bool geomsPreOk = CollectChartChildGeoms(app, &geomsPre);
				captureStep("pre", traceProfile);

				Overlay_CommitWindowGestureForTest("2026-06-15", "2026-07-27");
				PumpFor(400);
				{
					const bool hiddenBarAbsent = !ChartChildExists(app, "TASK", "discovery")
						&& !ChartChildExists(app, "TASK_LABEL", "discovery");
					const bool continuationPresent = ChartChildExists(app, "WINDOW_CONTINUATION", "interviews-L")
						&& ChartChildExists(app, "WINDOW_CONTINUATION", "qa_t-R");
					const bool clippedBarStillEmitted = ChartChildExists(app, "TASK", "interviews")
						&& ChartChildExists(app, "TASK", "qa_t");
					char clipBuf[192];
					::snprintf(clipBuf, sizeof(clipBuf),
						"\"windowClipHiddenBarAbsent\":%s,\"windowClipContinuationPresent\":%s,\"windowClipStraddlersEmitted\":%s,",
						hiddenBarAbsent ? "true" : "false",
						continuationPresent ? "true" : "false",
						clippedBarStillEmitted ? "true" : "false");
					extraTraceFieldsForTrace = clipBuf;
				}
				captureStep("shrink", traceProfile);
				// M4 proof: 'discovery' was selected when the commit hid it; the
				// dump at this step must show the selection reset to document
				// context (ownSelKind/ownSelId empty).
				captureStep("sel-reset", traceProfile);

				// SR-WIN-20 lossless: clearing the window must restore geometry +
				// doc EXACTLY to the pre state (same model-op route as the W1
				// window-repair-lossless profile; the Fit-to-tasks UI lands in W4).
				try {
					PpDocument clearedDoc = DocumentFromJson(ReadGanttFromSlide(app));
					if (ClearTimeWindow(clearedDoc)) UpdateGantt(app, clearedDoc);
				} catch (...) {}
				PumpFor(400);
				{
					std::vector<ChartChildGeom> geomsExpanded;
					std::string mismatch;
					const bool lossless = geomsPreOk
						&& CollectChartChildGeoms(app, &geomsExpanded)
						&& ChartChildGeomsEqual(geomsPre, geomsExpanded, &mismatch);
					extraTraceFieldsForTrace += std::string("\"windowLosslessGeometryRestored\":")
						+ (lossless ? "true," : "false,");
					if (!lossless) wprintf(L"WINDOWCLIP lossless mismatch: %hs\n", mismatch.c_str());
				}
				captureStep("expand", traceProfile);

				// Re-apply the window through the seam for the C2 half.
				Overlay_CommitWindowGestureForTest("2026-06-15", "2026-07-27");
				PumpFor(400);
				captureStep("reshrink", traceProfile);

				HWND ovClip = OverlayHwnd();
				DumpProjPx clipProj{};
				ParseDumpProjectionPx(Overlay_DumpChromeStateForTest(), &clipProj);

				// C2a: create-at-point in an empty cell of the CLIPPED chart must
				// land on the clicked day (row 'launch', target 2026-07-06, 5-day
				// drag-create -> 2026-07-06..2026-07-11).
				{
					bool createLanded = false;
					std::string createDetail = "(setup failed)";
					RECT launchBand{};
					if (ovClip && clipProj.ok
						&& ParseRowBandFromDump(Overlay_DumpChromeStateForTest(), "launch", &launchBand)) {
						const long targetDay = DateToDays("2026-07-06");
						POINT downPt = {
							(LONG)::llround(clipProj.originXpx + (double)(targetDay - clipProj.originDay) * clipProj.pxPerDay),
							(launchBand.top + launchBand.bottom) / 2
						};
						const int dragPx = (int)::llround(clipProj.pxPerDay * 5.0);
						PostScreenDrag(ovClip, downPt, dragPx);
						PumpFor(250);
						try {
							PpDocument afterCreate = DocumentFromJson(ReadGanttFromSlide(app));
							for (const auto& t : afterCreate.tasks) {
								if (t.rowId != "launch") continue;
								createDetail = t.start + ".." + t.end;
								createLanded = (t.start == "2026-07-06" && t.end == "2026-07-11");
								break;
							}
						} catch (...) { createDetail = "(read failed)"; }
					}
					extraTraceFieldsForTrace += std::string("\"windowCreateLandedOnDate\":")
						+ (createLanded ? "true," : "false,")
						+ "\"windowCreateDetail\":\"" + createDetail + "\",";
				}
				captureStep("create-on-clipped", traceProfile);

				// C2b: drag the LEFT-clipped straddler by exactly +7 days; the
				// commit must move 06-08..06-19 to 06-15..06-26 (a clipped rect
				// must not distort px/day).
				{
					bool dragLanded = false;
					std::string dragDetail = "(setup failed)";
					POINT center{};
					if (ovClip && clipProj.ok && FindTaskBodyCenter(app, "interviews", &center)) {
						const int dxPx = (int)::llround(clipProj.pxPerDay * 7.0);
						PostTaskBodyDrag(ovClip, center, dxPx);
						PumpFor(250);
						try {
							PpDocument afterDrag = DocumentFromJson(ReadGanttFromSlide(app));
							for (const auto& t : afterDrag.tasks) {
								if (t.id != "interviews") continue;
								dragDetail = t.start + ".." + t.end;
								dragLanded = (t.start == "2026-06-15" && t.end == "2026-06-26");
								break;
							}
						} catch (...) { dragDetail = "(read failed)"; }
					}
					extraTraceFieldsForTrace += std::string("\"windowStraddlerDragLanded\":")
						+ (dragLanded ? "true," : "false,")
						+ "\"windowStraddlerDragDetail\":\"" + dragDetail + "\",";
				}
				captureStep("drag-straddler", traceProfile);
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"window-commit-latency") == 0) {
				// W3 m6/D3: measure BOTH commit shapes through the REAL dispatch.
				// Shape 1 (in-place reconcile): [05-28..08-13] covers everything
				// and keeps the auto-fit projection's axis tick set (same Mondays
				// / month starts), so no prim enters or leaves — the group
				// survives and geometry rewrites in place. Shape 2 (structural):
				// [06-15..07-27] hides items, so the reconcile deletes/creates
				// the entering/leaving prims.
				auto commitWindowWithLatency = [&](const char* s, const char* e) -> unsigned long long {
					const ULONGLONG t0 = ::GetTickCount64();
					Overlay_CommitWindowGestureForTest(s, e);
					const ULONGLONG elapsed = ::GetTickCount64() - t0;
					wprintf(L"TRACE OPLATENCY: {\"ms\":%llu}\n", (unsigned long long)elapsed);
					char phaseBuf[512];
					const int phaseLen = Gantt_GetLastOpPhasesForTest(phaseBuf, (int)sizeof(phaseBuf));
					if (phaseLen > 0) wprintf(L"TRACE OPPHASES: %hs\n", phaseBuf);
					return (unsigned long long)elapsed;
				};
				Overlay_SelectForTest("", "");
				PumpFor(300);
				captureStep("pre", traceProfile);
				emitOpDispatch();
				const unsigned long long inplaceMs = commitWindowWithLatency("2026-05-28", "2026-08-13");
				{
					char latBuf[96];
					::snprintf(latBuf, sizeof(latBuf), "\"windowCommitInplaceMs\":%llu,", inplaceMs);
					extraTraceFieldsForTrace = latBuf;
				}
				captureStep("immed", traceProfile);
				const unsigned long long structuralMs = commitWindowWithLatency("2026-06-15", "2026-07-27");
				{
					char latBuf[96];
					::snprintf(latBuf, sizeof(latBuf), "\"windowCommitStructuralMs\":%llu,", structuralMs);
					extraTraceFieldsForTrace += latBuf;
				}
				captureStep("structural", traceProfile);
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
				wprintf(L"WINDOWLATENCY inplaceMs=%llu structuralMs=%llu\n", inplaceMs, structuralMs);
			} else if (traceProfile && wcscmp(traceProfile, L"window-undo") == 0) {
				// W3 M2: IN-PLACE window commit ([05-28..08-13] keeps the auto-fit
				// axis tick set, so nothing enters/leaves and the group identity
				// survives; undoing a structural regroup would orphan the overlay's
				// chart binding) -> ONE external undo restores the window fields AND
				// geometry together (single undo entry, SR-WIN-27) -> a +1d nudge
				// must NOT resurrect the undone window (the PP_DOC drift probe
				// re-syncs the scene cache at read-back).
				// Frame + PP_PROJ + PP_DOC are the restore witnesses: an external
				// undo can leave GroupItems temporarily broken on the restored
				// group (observed 0x800A01A8), so child-level geometry is not
				// OM-readable at this point — but the group FRAME, the projection
				// tag and the document tag are, and the window commit changes all
				// three (frame width shrinks when the clamped note re-enters the
				// body, PP_PROJ gets pad=0/minDay=windowStart, PP_DOC gains the
				// window fields). One undo restoring all three together IS the
				// single-entry proof.
				auto chartFrameAndProj = [&](float* l, float* t, float* w, float* h, std::string* proj) -> bool {
					try {
						PowerPoint::_SlidePtr slNow = app->GetActiveWindow()->GetView()->GetSlide();
						PowerPoint::ShapesPtr shsNow = slNow->GetShapes();
						long nNow = shsNow->GetCount();
						for (long i = 1; i <= nNow; ++i) {
							PowerPoint::ShapePtr sh = shsNow->Item(_variant_t(i));
							_bstr_t k = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
							if (k.length() && std::string((const char*)_bstr_t(k)) == "CHART_ROOT") {
								*l = sh->GetLeft(); *t = sh->GetTop();
								*w = sh->GetWidth(); *h = sh->GetHeight();
								_bstr_t pj = sh->GetTags()->Item(_bstr_t(L"PP_PROJ"));
								*proj = pj.length() ? (const char*)_bstr_t(pj) : "";
								return true;
							}
						}
					} catch (...) {}
					return false;
				};

				Overlay_SelectForTest("TASK", "discovery");
				SelectChartRootNatively(slide);
				PumpFor(400);
				float preL = 0, preT = 0, preW = 0, preH = 0;
				std::string preProj;
				const bool preFrameOk = chartFrameAndProj(&preL, &preT, &preW, &preH, &preProj);
				std::string docJsonPre;
				try { docJsonPre = ReadGanttFromSlide(app); } catch (...) {}
				captureStep("pre", traceProfile);

				Overlay_CommitWindowGestureForTest("2026-05-28", "2026-08-13");
				PumpFor(400);
				{
					std::string docJsonCommit;
					try { docJsonCommit = ReadGanttFromSlide(app); } catch (...) {}
					float cL = 0, cT = 0, cW = 0, cH = 0;
					std::string cProj;
					const bool commitFrameOk = chartFrameAndProj(&cL, &cT, &cW, &cH, &cProj);
					const bool commitChangedDoc = !docJsonPre.empty() && docJsonCommit != docJsonPre;
					const bool commitChangedGeom = preFrameOk && commitFrameOk
						&& (std::fabs(cW - preW) > 0.25f || cProj != preProj);
					extraTraceFieldsForTrace = std::string("\"windowCommitChangedDoc\":")
						+ (commitChangedDoc ? "true," : "false,")
						+ "\"windowCommitChangedGeometry\":" + (commitChangedGeom ? "true," : "false,");
				}
				captureStep("commit", traceProfile);

				const bool undoDispatched = ExecuteUndoOnceW3(app);
				PumpFor(700);
				{
					std::string docJsonUndo;
					try { docJsonUndo = ReadGanttFromSlide(app); } catch (...) {}
					float uL = 0, uT = 0, uW = 0, uH = 0;
					std::string uProj;
					const bool undoFrameOk = chartFrameAndProj(&uL, &uT, &uW, &uH, &uProj);
					const bool docRestored = undoDispatched && !docJsonPre.empty() && docJsonUndo == docJsonPre;
					const bool geomRestored = undoDispatched && preFrameOk && undoFrameOk
						&& std::fabs(uL - preL) <= 0.25f && std::fabs(uT - preT) <= 0.25f
						&& std::fabs(uW - preW) <= 0.25f && std::fabs(uH - preH) <= 0.25f
						&& uProj == preProj;
					char undoBuf[192];
					::snprintf(undoBuf, sizeof(undoBuf),
						"\"windowUndoDispatched\":%s,\"windowUndoDocRestored\":%s,\"windowUndoGeometryRestored\":%s,",
						undoDispatched ? "true" : "false",
						docRestored ? "true" : "false",
						geomRestored ? "true" : "false");
					extraTraceFieldsForTrace += undoBuf;
					wprintf(L"WINDOWUNDO dispatched=%d docRestored=%d geomRestored=%d frame=(%.2f,%.2f,%.2f,%.2f)->(%.2f,%.2f,%.2f,%.2f)\n",
						undoDispatched ? 1 : 0, docRestored ? 1 : 0, geomRestored ? 1 : 0,
						preL, preT, preW, preH, uL, uT, uW, uH);
				}
				captureStep("undo", traceProfile);

				// The external undo leaves the restored group's GroupItems
				// dispatch broken (0x800A01A8), so the overlay hides itself —
				// and clears selection — on every tick until the chart is
				// re-emitted. Select + dispatch back-to-back WITHOUT pumping in
				// between (the seam is synchronous; ticks only run while
				// messages pump): the nudge is the first op-path doc read after
				// the undo (the drift-probe/cache-poisoning trap), and its
				// reconcile fails over to the InsertGantt re-emit, which heals
				// the chart — the product story for "first edit after Ctrl+Z".
				Overlay_SelectForTest("TASK", "discovery");
				emitOpDispatch();
				performOpWithLatency(HtCmd_NudgePlus1);
				{
					std::string docAfterNudge;
					try { docAfterNudge = ReadGanttFromSlide(app); } catch (...) {}
					const bool nudged = docAfterNudge.find("2026-06-02") != std::string::npos;
					const bool windowGone = docAfterNudge.find("windowStart") == std::string::npos;
					wprintf(L"WINDOWUNDO-DIAG post-nudge docNudged=%d docWindowGone=%d docLen=%zu\n",
						nudged ? 1 : 0, windowGone ? 1 : 0, docAfterNudge.size());
				}
				captureStep("nudge", traceProfile);
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"create-preview-shape") == 0) {
				Overlay_SelectForTest("", "");
				PumpFor(300);
				captureStep("pre", traceProfile);
				HWND ov = OverlayHwnd();
				const char* preDump = Overlay_DumpChromeStateForTest();
				RECT band{};
				int rowBandH = 0;
				int previewH = 0;
				if (ParseRowBandFromDump(preDump, "launch", &band)) {
					rowBandH = band.bottom - band.top;
					POINT start = { band.right - 60, (band.top + band.bottom) / 2 };
					if (ov) {
						PostScreenDragStart(ov, start, 100);
						captureStep("mid", traceProfile);
						ParseDragPreviewHeight(Overlay_DumpChromeStateForTest(), &previewH);
						PostScreenDragFinish(ov, start, 100);
					}
				}
				captureStep("immed", traceProfile);
				wprintf(L"CREATEPREVIEW rowBandH=%d previewH=%d ratio=%.2f\n",
					rowBandH, previewH, rowBandH > 0 ? (double)previewH / (double)rowBandH : 0.0);
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"progress-drag") == 0) {
				Overlay_SelectForTest("TASK", "wireframes");
				SelectChartRootNatively(slide);
				PumpFor(400);
				captureStep("pre", traceProfile);
				HWND ov = OverlayHwnd();
				POINT edge{};
				if (ov && FindTaskProgressEdge(app, "wireframes", 40, &edge)) {
					PostScreenDragStart(ov, edge, 36);
					captureStep("mid", traceProfile);
					PostScreenDragFinish(ov, edge, 36);
				}
				captureStep("immed", traceProfile);
				wprintf(L"PROGRESSDRAG pct=%d\n", JsonFieldInt(Overlay_DumpChromeStateForTest(), "dragCandidatePercent", -1));
				PumpFor(200);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"card-commit-clickaway") == 0) {
				Overlay_SelectForTest("TASK", "visual");
				SelectChartRootNatively(slide);
				PumpFor(400);
				captureStep("pre", traceProfile);
				Overlay_PerformAppBarCommandForTest(HtCmd_Edit);
				PumpFor(350);
				HWND card = ::FindWindowW(PP_CARD_EDITOR_CLASS, nullptr);
				if (card) {
					HWND label = ::GetDlgItem(card, OVERLAY_CARD_ID_LABEL_FOR_TEST);
					if (label) ::SetWindowTextW(label, L"Card committed name");
				}
				captureStep("card-open", traceProfile);
				HWND ov = OverlayHwnd();
				if (ov) {
					RECT or{}; ::GetWindowRect(ov, &or);
					POINT away = { or.left + 8, or.top + 8 };
					POINT cp = away; ::ScreenToClient(ov, &cp);
					LPARAM lp = MAKELPARAM((short)cp.x, (short)cp.y);
					::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, lp);
					::PostMessageW(ov, WM_LBUTTONUP, 0, lp);
				}
				PumpFor(400);
				captureStep("immed", traceProfile);
				const char* immedDump = Overlay_DumpChromeStateForTest();
				wprintf(L"CARDCOMMIT cardVisible=%hs\n",
					(immedDump && ::strstr(immedDump, "\"cardVisible\":false")) ? "committed-closed" : "still-open");
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"appbar-docked") == 0) {
				// v2.6.3 SR-DOCK-01/02: bar immediately below chart rect; follows
				// CHART_ROOT move/resize with no drift at +1/+3 ticks.
				Overlay_SelectForTest("", "");
				PumpFor(300);
				captureStep("pre", traceProfile);
				PowerPoint::ShapePtr dockChart;
				try {
					PowerPoint::DocumentWindowPtr w2 = app->GetActiveWindow();
					PowerPoint::_SlidePtr sl2 = w2->GetView()->GetSlide();
					PowerPoint::ShapesPtr shs = sl2->GetShapes();
					long n = shs->GetCount();
					for (long i = 1; i <= n; ++i) {
						auto s = shs->Item(_variant_t(i));
						_bstr_t k = s->GetTags()->Item(_bstr_t(L"PP_KIND"));
						if (k.length() && std::string((const char*)_bstr_t(k)) == "CHART_ROOT") {
							dockChart = s;
							break;
						}
					}
				} catch (...) {}
				if (dockChart) {
					float origL = dockChart->GetLeft();
					float origT = dockChart->GetTop();
					float origW = dockChart->GetWidth();
					float origH = dockChart->GetHeight();
					dockChart->PutLeft(origL + 60.0f);
					dockChart->PutTop(origT + 20.0f);
					try { dockChart->Select(Office::msoTrue); } catch (...) {}
					PumpFor(80);
					captureStep("move-immed", traceProfile);
					PumpFor(150);
					captureStep("move+1", traceProfile);
					PumpFor(300);
					captureStep("move+3", traceProfile);
					dockChart->PutLeft(origL);
					dockChart->PutTop(origT);
					dockChart->PutWidth(origW + 120.0f);
					dockChart->PutHeight(origH + 40.0f);
					try { dockChart->Select(Office::msoTrue); } catch (...) {}
					PumpFor(80);
					captureStep("resize-immed", traceProfile);
					PumpFor(150);
					captureStep("resize+1", traceProfile);
					PumpFor(300);
					captureStep("resize+3", traceProfile);
					try {
						dockChart->PutLeft(origL);
						dockChart->PutTop(origT);
						dockChart->PutWidth(origW);
						dockChart->PutHeight(origH);
					} catch (...) {}
				}
			} else if (traceProfile && wcscmp(traceProfile, L"appbar-context-evolution") == 0) {
				// v2.6.3 SR-BAR-01/02/03: document context has INSERT+SCALE;
				// item contexts expose only relevant groups; transitions are immediate.
				Overlay_SelectForTest("", "");
				PumpFor(300);
				captureStep("pre", traceProfile);
				Overlay_SelectForTest("TASK", "discovery");
				SelectChartRootNatively(slide);
				PumpFor(300);
				captureStep("task-sel", traceProfile);
				Overlay_SelectForTest("ROW", "research");
				SelectChartRootNatively(slide);
				PumpFor(300);
				captureStep("row-sel", traceProfile);
				Overlay_SelectForTest("MILESTONE", "m_ship");
				SelectChartRootNatively(slide);
				PumpFor(300);
				captureStep("milestone-sel", traceProfile);
				Overlay_SelectForTest("MARKER", "today");
				SelectChartRootNatively(slide);
				PumpFor(300);
				captureStep("marker-sel", traceProfile);
				Overlay_SelectForTest("", "");
				Overlay_InvalidateAppBarForTest();
				PumpFor(400);
				captureStep("component", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"entity-dump") == 0) {
				auto publishEntities = [&]() {
					const char* dump = Overlay_DumpEntitiesForTest();
					extraTraceFieldsForTrace = "\"entityDump\":";
					extraTraceFieldsForTrace += dump ? dump : "{\"entities\":[]}";
					extraTraceFieldsForTrace += ",";
				};
				Overlay_SelectForTest("TASK", "discovery");
				PumpFor(350);
				publishEntities();
				captureStep("before", traceProfile);
				Overlay_PerformAppBarCommandForTest(HtCmd_Swatch2);
				PumpFor(450);
				publishEntities();
				captureStep("after-style", traceProfile);

				// Task-bar hover: park the pointer well outside the chart for a
				// DETERMINISTIC no-hover control (the steps above run with the
				// machine's real cursor wherever it happens to be), then move it
				// onto a task in a different row than the selected one.
				{
					HWND hovOv = OverlayHwnd();
					POINT offPt{ 0, 0 };
					Overlay_SetCursorPosOverrideForTest(true, offPt);
					if (hovOv) ::PostMessageW(hovOv, WM_MOUSEMOVE, 0, MAKELPARAM(0, 0));
					PumpFor(300);
					publishEntities();
					captureStep("hover-off", traceProfile);

					POINT hoverPt{};
					if (hovOv && FindTaskBodyCenter(app, "wireframes", &hoverPt)) {
						POINT hoverClient = hoverPt;
						::ScreenToClient(hovOv, &hoverClient);
						Overlay_SetCursorPosOverrideForTest(true, hoverPt);
						::PostMessageW(hovOv, WM_MOUSEMOVE, 0,
							MAKELPARAM((short)hoverClient.x, (short)hoverClient.y));
						PumpFor(350);
					}
					publishEntities();
					captureStep("task-hover", traceProfile);
					Overlay_SetCursorPosOverrideForTest(false, POINT{});
					PumpFor(250);
				}
				extraTraceFieldsForTrace.clear();
			} else if (traceProfile && wcscmp(traceProfile, L"session-recorder") == 0) {
				Overlay_SelectForTest("", "");
				PumpFor(250);
				Overlay_PerformAppBarCommandForTest(HtCmd_RecordSession);
				PumpFor(300);
				captureStep("recording-on", traceProfile);

				// Native child-only geometry must be visible even when the root
				// bounds/count/tags stay unchanged. Restore after the sample.
				if (MoveChartChildBy(slide, "TASK_PROGRESS", "wireframes", 4.0f, 0.0f)) {
					PumpFor(1100);
					MoveChartChildBy(slide, "TASK_PROGRESS", "wireframes", -4.0f, 0.0f);
					PumpFor(1100);
				}

				Overlay_SelectForTest("TASK", "discovery");
				SelectChartRootNatively(slide);
				PumpFor(250);
				Overlay_PerformAppBarCommandForTest(HtCmd_NudgePlus1);
				PumpFor(350);

				HWND recOv = OverlayHwnd();
				POINT center{};
				if (recOv && FindTaskBodyCenter(app, "discovery", &center)) {
					POINT clientPt = center;
					::ScreenToClient(recOv, &clientPt);
					Overlay_SetCursorPosOverrideForTest(true, center);
					::PostMessageW(recOv, WM_LBUTTONDOWN, MK_LBUTTON,
						MAKELPARAM((short)clientPt.x, (short)clientPt.y));
					PumpFor(40);
					Overlay_BeginPaintCadenceSampleForTest();
					const int dragPx = 96, dragSteps = 36;
					for (int s = 1; s <= dragSteps; ++s) {
						POINT sp = { center.x + dragPx * s / dragSteps, center.y };
						Overlay_SetCursorPosOverrideForTest(true, sp);
						::PostMessageW(recOv, WM_MOUSEMOVE, MK_LBUTTON,
							MAKELPARAM((short)(clientPt.x + dragPx * s / dragSteps), (short)clientPt.y));
						PumpFor(16);
					}
					double hz = 0.0, p50 = 0.0, p95 = 0.0;
					long pc = 0, wms = 0;
					Overlay_GetPaintCadenceForTest(&hz, &pc, &wms, &p50, &p95);
					char cadence[220];
					::snprintf(cadence, sizeof(cadence),
						"\"paintHz\":%.2f,\"paintCount\":%ld,\"paintWindowMs\":%ld,\"paintP50Ms\":%.2f,\"paintP95Ms\":%.2f,",
						hz, pc, wms, p50, p95);
					extraTraceFieldsForTrace = cadence;
					captureStep("mid-drag", traceProfile);
					extraTraceFieldsForTrace.clear();
					Overlay_EndPaintCadenceSampleForTest();
					::PostMessageW(recOv, WM_LBUTTONUP, 0,
						MAKELPARAM((short)(clientPt.x + dragPx), (short)clientPt.y));
					PumpFor(500);
				}
				Overlay_RecError("trace_session_recorder/forced", (long)E_FAIL,
					"forced swallowed-catch evidence");
				PumpFor(1150); // exercises the ~1 Hz idle snapshot path
				captureStep("recorded", traceProfile);
				Overlay_StopSessionRecordForTest();
				captureStep("stopped", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"task-bar-unit") == 0) {
				// v2.9.0 SR-TASK-UNIT: task bar is one object. Native pick of
				// TASK_LABEL and TASK_PROGRESS both mirror to ownSel TASK and
				// suppress the child; overlay click on the label rect selects
				// the same task; body drag from the label still moves dates.
				Overlay_SelectForTest("", "");
				PumpFor(300);
				captureStep("pre", traceProfile);

				// Live PowerPoint reports the group in ShapeRange and the actual
				// clicked primitive in ChildShapeRange. The child must win.
				int actGroupedLabel = Overlay_OnNativeSelectionChangedWithChild(
					"CHART_ROOT", "", true, true, "TASK_LABEL", "discovery");
				wprintf(L"TASKUNIT groupedLabelSelect: handlerAction=%d\n", actGroupedLabel);
				PumpFor(220);
				captureStep("grouped-label-settled", traceProfile);

				// (1) Native select TASK_LABEL → ownSel TASK, no child remains.
				std::string labelAtSelect = SelectChartChildNatively(slide, "TASK_LABEL", "discovery");
				int actLabel = Overlay_OnNativeSelectionChanged("TASK_LABEL", "discovery", true);
				wprintf(L"TASKUNIT labelSelect: com=%hs handlerAction=%d\n",
					labelAtSelect.empty() ? "(none)" : labelAtSelect.c_str(), actLabel);
				PumpFor(220);
				captureStep("label-immed", traceProfile);
				PumpFor(450);
				captureStep("label-settled", traceProfile);

				// (2) Clear, then native select TASK body rect — same unit id.
				Overlay_SelectForTest("", "");
				PumpFor(200);
				std::string bodyAtSelect = SelectChartChildNatively(slide, "TASK", "discovery");
				int actBody = Overlay_OnNativeSelectionChanged("TASK", "discovery", true);
				wprintf(L"TASKUNIT bodySelect: com=%hs handlerAction=%d\n",
					bodyAtSelect.empty() ? "(none)" : bodyAtSelect.c_str(), actBody);
				PumpFor(220);
				captureStep("body-immed", traceProfile);
				PumpFor(450);
				captureStep("body-settled", traceProfile);

				// (3) Overlay click center of TASK_LABEL → ownSel TASK discovery.
				Overlay_SelectForTest("", "");
				PumpFor(200);
				HWND ovUnit = OverlayHwnd();
				RECT labelRc{};
				if (ovUnit && FindChartChildRect(app, "TASK_LABEL", "discovery", &labelRc)
					&& labelRc.right > labelRc.left && labelRc.bottom > labelRc.top) {
					POINT lp{
						(labelRc.left + labelRc.right) / 2,
						(labelRc.top + labelRc.bottom) / 2
					};
					WalkClick(ovUnit, lp, 0);
					PumpFor(320);
				} else {
					// Fallback: inject the same decision path as a label hit.
					Overlay_OnNativeSelectionChanged("TASK_LABEL", "discovery", true);
					PumpFor(220);
				}
				captureStep("label-click", traceProfile);

				// (4) Progress fill is the same unit when present (wireframes has %).
				// Prefer discovery if it has progress; else wireframes TASK_PROGRESS.
				Overlay_SelectForTest("", "");
				PumpFor(150);
				std::string progKind = SelectChartChildNatively(slide, "TASK_PROGRESS", "wireframes");
				if (!progKind.empty()) {
					int actProg = Overlay_OnNativeSelectionChanged("TASK_PROGRESS", "wireframes", true);
					wprintf(L"TASKUNIT progressSelect: com=%hs handlerAction=%d\n",
						progKind.c_str(), actProg);
				} else {
					// No progress shape: mirror path still must accept the kind.
					int actProg = Overlay_OnNativeSelectionChanged("TASK_PROGRESS", "wireframes", true);
					wprintf(L"TASKUNIT progressSelect: com=(none) handlerAction=%d\n", actProg);
				}
				PumpFor(400);
				captureStep("progress-settled", traceProfile);

				// (5) Drag from label area moves dates (date range shifts).
				// Capture start date before drag via dump if available.
				Overlay_SelectForTest("TASK", "discovery");
				SelectChartRootNatively(slide);
				PumpFor(250);
				captureStep("pre-drag", traceProfile);
				if (ovUnit && FindChartChildRect(app, "TASK_LABEL", "discovery", &labelRc)
					&& labelRc.right > labelRc.left) {
					POINT from{
						(labelRc.left + labelRc.right) / 2,
						(labelRc.top + labelRc.bottom) / 2
					};
					PostScreenDrag(ovUnit, from, 72);
					PumpFor(400);
				} else if (ovUnit) {
					POINT center{};
					if (FindTaskBodyCenter(app, "discovery", &center)) {
						PostScreenDrag(ovUnit, center, 72);
						PumpFor(400);
					}
				}
				captureStep("post-drag", traceProfile);
			} else if (traceProfile && wcscmp(traceProfile, L"component-shape-protection") == 0) {
				// v2.6.1 U1 selection integrity (SR-SHP-01..03 / SR-IXC-19/21 /
				// UF-07 / SR-IXC-09). Three sub-tests in one profile:
				//   (6a) a native pick of a chart CHILD is suppressed within one
				//        tick and the overlay ownSel mirrors it (no child stays
				//        PowerPoint-selected);
				//   (6b) Delete/arrow hotkeys are only registered while the slide
				//        view is focused, and a Delete delivered while focus is
				//        off does NOT mutate the chart (anti-theft, B1);
				//   (6c) deselect resets the app bar to the component (SCALE)
				//        context and clears ownSel.
				Overlay_SelectForTest("", "");
				Overlay_SetSlideFocusOverrideForTest(-1); // real gate (host-active shortcut => focus OK)
				PumpFor(300);
				captureStep("pre", traceProfile);          // ownSel empty, component context

				// (6a) SR-SHP-02: model a native pick of a chart CHILD. A real COM
				// Select() on a grouped child collapses to selecting the CHART_ROOT
				// group (logged below as honest evidence), so ALSO drive the SAME
				// shared handler the Tick poll and the Connect sink call, to
				// exercise the suppression decision + ownSel mirror deterministically.
				std::string kindAtSelect = SelectTaskChildNatively(slide, "discovery");
				long cntAfter = 0;
				std::string kindAfter = CurrentNativeSelKind(app, &cntAfter);
				int act = Overlay_OnNativeSelectionChanged("TASK", "discovery", true);
				wprintf(L"PROTECT afterSelect: comSelectKind=%hs nativeNow=%hs count=%ld handlerAction=%d(1=suppress)\n",
					kindAtSelect.empty() ? "(none)" : kindAtSelect.c_str(),
					kindAfter.empty() ? "(none)" : kindAfter.c_str(), cntAfter, act);
				PumpFor(220);                               // real ticks re-observe the actual native sel
				captureStep("child-immed", traceProfile);
				PumpFor(500);                               // settle (a few ticks)
				captureStep("child-settled", traceProfile);
				{
					long cntSettled = 0;
					std::string kindSettled = CurrentNativeSelKind(app, &cntSettled);
					wprintf(L"PROTECT settled: nativeSelKind=%hs count=%ld ownSelKind=%d\n",
						kindSettled.empty() ? "(none)" : kindSettled.c_str(), cntSettled,
						Overlay_GetSelectedKindForTest());
				}

				// (6b) SR-IXC-19/21 hotkey scope. Task still selected: registration
				// gate must register when slide-view focused and DROP when focus
				// leaves it; a Delete delivered while focus is off must NOT mutate.
				Overlay_SetSlideFocusOverrideForTest(1);    // focus on the slide view
				PumpFor(240);
				captureStep("hk-focus", traceProfile);       // hotkeysActive true
				Overlay_SetSlideFocusOverrideForTest(0);     // focus elsewhere (Notes/ribbon)
				PumpFor(240);                                 // tick unregisters
				HWND ovH = OverlayHwnd();
				if (ovH) {
					// Deliver Delete directly (simulating "the key reached us"):
					// the handler's B1 scope check must still no-op it.
					::PostMessageW(ovH, WM_HOTKEY, (WPARAM)OVERLAY_HOTKEY_DELETE_FOR_TEST, 0);
					PumpFor(220);
					::PostMessageW(ovH, WM_HOTKEY, (WPARAM)OVERLAY_HOTKEY_DELETE_FOR_TEST, 0);
					PumpFor(220);
				}
				captureStep("hk-offfocus", traceProfile);     // hotkeysActive false; taskCount unchanged
				Overlay_SetSlideFocusOverrideForTest(-1);     // restore real gate

				// (6c) UF-07 / SR-IXC-09: deselect -> component/default context.
				Overlay_SelectForTest("", "");
				Overlay_InvalidateAppBarForTest();
				PumpFor(450);
				captureStep("reset", traceProfile);           // ownSel empty, SCALE present
			} else if (traceProfile && wcscmp(traceProfile, L"multi-row-delete") == 0) {
				// v2.6.4 UF-03: Ctrl/Shift multi-select + bulk delete (SR-IXC-18).
				Overlay_SelectForTest("", "");
				PumpFor(300);
				captureStep("pre", traceProfile);
				const int preRowCount = JsonFieldInt(Overlay_DumpChromeStateForTest(), "rowCount", -1);

				HWND ov = OverlayHwnd();
				RECT band{};
				auto clickRow = [&](const char* rowId, WPARAM mk) -> bool {
					if (!ov) return false;
					if (!ParseRowBandFromDump(Overlay_DumpChromeStateForTest(), rowId, &band)) return false;
					// Click in the LEFT RAIL (row label zone): the band center x sits
					// mid-chart where the TODAY marker's full-height hit band runs —
					// a center click selects the MARKER, not the row.
					POINT pt{ band.left + 40, (band.top + band.bottom) / 2 };
					WalkClick(ov, pt, mk);
					PumpFor(280);
					return true;
				};

				bool shiftOk = false;
				if (clickRow("research", 0)) {
					captureStep("shift-anchor", traceProfile);
					if (clickRow("launch", MK_SHIFT)) {
						const char* shiftDump = Overlay_DumpChromeStateForTest();
						const int got = JsonFieldInt(shiftDump, "ownSelCount", 0);
						const int want = CountRowBandsBetween(shiftDump, "research", "launch");
						shiftOk = (want > 0 && got == want);
						wprintf(L"MULTIROW shiftRange=%s ownSelCount=%d want=%d\n",
							shiftOk ? L"PASS" : L"FAIL", got, want);
						captureStep("shift-range", traceProfile);
					}
				}

				Overlay_SelectForTest("", "");
				Overlay_InvalidateAppBarForTest();
				PumpFor(300);

				bool ctrlOk = false;
				bool deleteOk = false;
				if (clickRow("research", 0)) {
					captureStep("ctrl-anchor", traceProfile);
					if (clickRow("design", MK_CONTROL)) {
						const int got = JsonFieldInt(Overlay_DumpChromeStateForTest(), "ownSelCount", 0);
						ctrlOk = (got == 2);
						wprintf(L"MULTIROW ctrlToggle=%s ownSelCount=%d\n",
							ctrlOk ? L"PASS" : L"FAIL", got);
						captureStep("ctrl-two", traceProfile);
						Overlay_SetSlideFocusOverrideForTest(1);
						Overlay_PerformAppBarCommandForTest(HtCmd_Delete);
						PumpFor(450);
						captureStep("post-delete", traceProfile);
						const char* postDump = Overlay_DumpChromeStateForTest();
						const int postRows = JsonFieldInt(postDump, "rowCount", -1);
						const int postSel = JsonFieldInt(postDump, "ownSelCount", -1);
						deleteOk = (preRowCount >= 0 && postRows == preRowCount - 2 && postSel == 0);
						wprintf(L"MULTIROW bulkDelete=%s rowCount %d->%d ownSelCount=%d\n",
							deleteOk ? L"PASS" : L"FAIL", preRowCount, postRows, postSel);
						Overlay_SetSlideFocusOverrideForTest(-1);
					}
				}

				const bool multiRowPass = shiftOk && ctrlOk && deleteOk;
				wprintf(L"MULTIROW %s (shift=%d ctrl=%d delete=%d)\n",
					multiRowPass ? L"PASS" : L"FAIL", shiftOk ? 1 : 0, ctrlOk ? 1 : 0, deleteOk ? 1 : 0);
			} else if (traceProfile && wcscmp(traceProfile, L"theme-coherent-surfaces") == 0) {
				Overlay_SelectForTest("TASK", "wireframes");
				SelectChartRootNatively(slide);
				PumpFor(400);
				captureStep("pre", traceProfile);
				HWND ov = OverlayHwnd();
				POINT center{};
				if (ov && FindTaskBodyCenter(app, "wireframes", &center)) {
					POINT clientPt = center;
					::ScreenToClient(ov, &clientPt);
					Overlay_ShowContextMenuAtClientForTest(clientPt.x, clientPt.y);
					PumpFor(250);
					captureStep("menu-open", traceProfile);
					const char* menuDump = Overlay_DumpChromeStateForTest();
					const bool menuVis = menuDump && ::strstr(menuDump, "\"contextMenuVisible\":true");
					HWND menuWnd = ::FindWindowW(PP_THEME_MENU_CLASS, nullptr);
					wprintf(L"THEMEMENU visible=%d hwnd=%p\n", menuVis ? 1 : 0, (void*)menuWnd);
					ThemeMenu_Dismiss(0);
					PumpFor(150);
				} else {
					wprintf(L"THEMEMENU: wireframes task center not found\n");
				}
				Overlay_PerformAppBarCommandForTest(HtCmd_Edit);
				PumpFor(350);
				captureStep("card-open", traceProfile);
				const char* cardDump = Overlay_DumpChromeStateForTest();
				wprintf(L"THEMECARD visible=%hs\n",
					(cardDump && ::strstr(cardDump, "\"cardVisible\":true")) ? "true" : "false");
				PumpFor(150);
				captureStep("immed", traceProfile);
				PumpFor(300);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			} else {
				// default: just exercise select + a row op
				Overlay_PerformAppBarCommandForTest(HtCmd_AddRowBelow);
				captureStep("immed", traceProfile);
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
			}

			// Completion marker: every other harness mode prints an OK marker; the
			// driver classifies a marker-less rc==0 run as FLAKE (and retries it).
			wprintf(L"TRACE COMPLETE OK\n");

			OverlayStop();
			if (gdiToken) Gdiplus::GdiplusShutdown(gdiToken);
			::CoUninitialize();
			return 0;
		}

		Overlay_SetHostActiveOverrideForTest(1);
		PumpFor(1800); // several 150ms ticks: chart overlay + app bar show + paint

		// --gallery: repeatable U8 visual-review sweep. Each primary PNG is the
		// full chart context; companion appbar/menu/card PNGs are strict
		// PrintWindow captures so they cannot hide an overlapping-window defect.
		if (gallery) {
			UINT dpi = 96;
			typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
			if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll")) {
				if (auto getDpiForWindow = (GetDpiForWindowFn)::GetProcAddress(user32, "GetDpiForWindow")) {
					if (ppHwnd) dpi = getDpiForWindow(ppHwnd);
				}
			}
			wprintf(L"GALLERY DPI %u (%u%%)\n", dpi, (dpi * 100 + 48) / 96);
			wprintf(L"GALLERY 150PCT SKIPPED: no headless monitor/DPI override is available\n");

			bool galleryOk = true;
			auto selectContext = [&](const char* kind, const char* id, const wchar_t* name) {
				Overlay_SelectForTest(kind, id);
				if (kind && *kind) SelectChartRootNatively(slide);
				else {
					try { app->GetActiveWindow()->GetSelection()->Unselect(); } catch (...) {}
				}
				PumpFor(500);
				galleryOk = CaptureGalleryContext(app, name) && galleryOk;
			};

			selectContext("", "", L"document");
			selectContext("TASK", "visual", L"task");
			selectContext("ROW", "research", L"row");
			selectContext("MILESTONE", "m_ship", L"milestone");
			selectContext("MARKER", "today", L"marker");

			// Use the real Ctrl+click selection path for the multi-selection frame.
			Overlay_SelectForTest("", "");
			try { app->GetActiveWindow()->GetSelection()->Unselect(); } catch (...) {}
			PumpFor(300);
			HWND overlay = OverlayHwnd();
			RECT researchBand{}, designBand{};
			const char* multiDump = Overlay_DumpChromeStateForTest();
			bool multiOk = overlay
				&& ParseRowBandFromDump(multiDump, "research", &researchBand)
				&& ParseRowBandFromDump(multiDump, "design", &designBand);
			if (multiOk) {
				WalkClick(overlay, POINT{ researchBand.left + 40, (researchBand.top + researchBand.bottom) / 2 }, 0);
				PumpFor(280);
				WalkClick(overlay, POINT{ designBand.left + 40, (designBand.top + designBand.bottom) / 2 }, MK_CONTROL);
				PumpFor(350);
				multiOk = JsonFieldInt(Overlay_DumpChromeStateForTest(), "ownSelCount", 0) == 2;
			}
			wprintf(L"GALLERY MULTI %ls\n", multiOk ? L"OK" : L"FAIL");
			galleryOk = multiOk && CaptureGalleryContext(app, L"multi") && galleryOk;

			selectContext("DEP", "d_gallery_discovery_wireframes", L"dep-link");

			// Settings is document-context only. Its menu owns a strict PrintWindow
			// companion capture alongside the full-chart frame.
			Overlay_SelectForTest("", "");
			try { app->GetActiveWindow()->GetSelection()->Unselect(); } catch (...) {}
			PumpFor(300);
			Overlay_ShowSettingsMenuForTest();
			PumpFor(200);
			HWND settingsMenu = ThemeMenu_Hwnd();
			const bool settingsVisible = settingsMenu && ::IsWindowVisible(settingsMenu);
			wprintf(L"GALLERY SETTINGS MENU %ls\n", settingsVisible ? L"OK" : L"FAIL");
			galleryOk = settingsVisible && CaptureGalleryContext(app, L"settings", settingsMenu, L"menu") && galleryOk;
			ThemeMenu_Dismiss();
			PumpFor(150);

			// The on-slide custom context menu, not the retired ribbon menu.
			Overlay_SelectForTest("TASK", "wireframes");
			SelectChartRootNatively(slide);
			PumpFor(350);
			POINT taskCenter{};
			bool contextMenuVisible = false;
			if (overlay && FindTaskBodyCenter(app, "wireframes", &taskCenter)) {
				POINT clientPt = taskCenter;
				::ScreenToClient(overlay, &clientPt);
				Overlay_ShowContextMenuAtClientForTest(clientPt.x, clientPt.y);
				PumpFor(200);
				HWND menu = ThemeMenu_Hwnd();
				contextMenuVisible = menu && ::IsWindowVisible(menu);
				galleryOk = contextMenuVisible && CaptureGalleryContext(app, L"context-menu", menu, L"menu") && galleryOk;
			}
			wprintf(L"GALLERY CONTEXT MENU %ls\n", contextMenuVisible ? L"OK" : L"FAIL");
			if (!contextMenuVisible) galleryOk = false;
			ThemeMenu_Dismiss();
			PumpFor(150);

			Overlay_SelectForTest("TASK", "visual");
			SelectChartRootNatively(slide);
			PumpFor(350);
			Overlay_PerformAppBarCommandForTest(HtCmd_Edit);
			PumpFor(350);
			HWND card = ::FindWindowW(PP_CARD_EDITOR_CLASS, nullptr);
			const bool cardVisible = card && ::IsWindowVisible(card);
			wprintf(L"GALLERY CARD %ls\n", cardVisible ? L"OK" : L"FAIL");
			galleryOk = cardVisible && CaptureGalleryContext(app, L"card", card, L"card") && galleryOk;
			if (cardVisible) ::PostMessageW(card, WM_KEYDOWN, VK_ESCAPE, 0);
			PumpFor(150);

			wprintf(galleryOk ? L"GALLERY COMPLETE OK\n" : L"GALLERY COMPLETE FAIL\n");
			OverlayStop();
			if (gdiToken) Gdiplus::GdiplusShutdown(gdiToken);
			::CoUninitialize();
			return galleryOk ? 0 : 1;
		}

		// --matrix: capture the app bar in each selection context so the visuals
		// can be reviewed (None / task / row / milestone). Uses Overlay_SelectForTest
		// to drive the internal selection without simulating clicks.
		bool matrix = false;
		for (int i = 1; i < argc; ++i) if (wcscmp(argv[i], L"--matrix") == 0) matrix = true;
		if (matrix) {
			struct Ctx { const char* kind; const char* id; const wchar_t* file; const wchar_t* ctxFile; };
			const Ctx ctxs[] = {
				{ "",          "",        L"native\\build\\ab-none.png",      NULL },
				{ "TASK",      "visual",  L"native\\build\\ab-task.png",      L"native\\build\\ab-task-ctx.png" },
				{ "ROW",       "research",L"native\\build\\ab-row.png",       NULL },
				{ "MILESTONE", "m_ship",  L"native\\build\\ab-milestone.png", NULL },
			};
			const PpDocument showcaseDoc = MakeShowcaseDocument();
			int matrixRc = 0;
			bool appbarFitAll = true;

			CheckChromeCalm(app, slide, false, 2.0, &matrixRc);
			CheckChromeCalm(app, slide, true, 3.0, &matrixRc);

			for (const auto& c : ctxs) {
				Overlay_SelectForTest(c.kind, c.id);
				PumpFor(900); // a few ticks: selection chrome + app-bar model rebuild + paint
				HWND abm = OverlayAppBarHwnd();
				const char* ctxLabel = c.kind[0] ? c.kind : "NONE";
				if (abm && ::IsWindowVisible(abm)) {
					RECT r{}; ::GetWindowRect(abm, &r);
					bool ctxOk = CaptureRectToPng(r, c.file);
					if (c.ctxFile) {
						RECT wide = r;
						wide.top -= 360; wide.left -= 90; wide.right += 90; wide.bottom += 24;
						RECT scr{ 0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN) };
						if (wide.top < scr.top) wide.top = scr.top;
						if (wide.left < scr.left) wide.left = scr.left;
						if (wide.right > scr.right) wide.right = scr.right;
						if (wide.bottom > scr.bottom) wide.bottom = scr.bottom;
						ctxOk = ctxOk && CaptureRectToPng(wide, c.ctxFile);
					}
					if (ctxOk) {
						wprintf(L"APPBAR-MATRIX %hs OK\n", ctxLabel);
					} else {
						wprintf(L"APPBAR-MATRIX %hs FAIL\n", ctxLabel);
						matrixRc = 1;
					}
					if (!CheckAppBarFitForContext(ctxLabel, AppBarSelFromKind(c.kind), c.id, showcaseDoc, abm)) {
						appbarFitAll = false;
						matrixRc = 1;
					}
				} else {
					wprintf(L"APPBAR-MATRIX %hs FAIL\n", ctxLabel);
					wprintf(L"APPBAR FIT %hs FAIL: window missing\n", ctxLabel);
					appbarFitAll = false;
					matrixRc = 1;
				}
			}
			if (appbarFitAll) {
				wprintf(L"APPBAR FIT OK\n");
			}
			OverlayStop();
			if (gdiToken) Gdiplus::GdiplusShutdown(gdiToken);
			::CoUninitialize();
			return matrixRc;
		}

		HWND ab = OverlayAppBarHwnd();
		if (!ab || !::IsWindowVisible(ab)) {
			wprintf(L"APPBAR-SHOT: app-bar window not visible\n");
		} else {
			RECT abRc{};
			::GetWindowRect(ab, &abRc);

			// Tight crop of just the bar.
			bool shotOk = CaptureRectToPng(abRc, L"native\\build\\appbar-shot.png");

			// Wider "docked over the chart" context crop: expand upward to show
			// the chart bottom, and a little around the sides, clamped to screen.
			RECT wide = abRc;
			wide.top    -= 340;
			wide.left   -= 80;
			wide.right  += 80;
			wide.bottom += 24;
			RECT scr{ 0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN) };
			if (wide.top < scr.top) wide.top = scr.top;
			if (wide.left < scr.left) wide.left = scr.left;
			if (wide.right > scr.right) wide.right = scr.right;
			if (wide.bottom > scr.bottom) wide.bottom = scr.bottom;
			shotOk = shotOk && CaptureRectToPng(wide, L"native\\build\\appbar-shot-context.png");

			if (shotOk) {
				wprintf(L"APPBAR-SHOT OK bar=(%ld,%ld,%ld,%ld)\n", abRc.left, abRc.top, abRc.right, abRc.bottom);
				rc = 0;
			}
		}

		// Leave the overlay running + PowerPoint open unless asked to close.
		if (argc > 1 && wcscmp(argv[1], L"--close") == 0) {
			OverlayStop();
			pres->Close();
			app->Quit();
		}
	}
	catch (const _com_error& e) {
		wprintf(L"COM error 0x%08lX: %s\n", (unsigned long)e.Error(),
			e.Description().length() ? (const wchar_t*)e.Description() : L"");
	}
	if (gdiToken) Gdiplus::GdiplusShutdown(gdiToken);
	::CoUninitialize();
	return rc;
}
