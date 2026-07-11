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
#include "../PowerPlannerAddin/GanttModel.h"
#include "../PowerPlannerAddin/Overlay.h"
#include "../PowerPlannerAddin/GanttHitTest.h"
#include "../PowerPlannerAddin/GanttAppBar.h"
// GDI+ headers use unqualified min/max; provide them if a prior include pulled
// in NOMINMAX (matches how the addin's own GDI+ TU gets them from windows.h).
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif
#include <gdiplus.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

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

static AppBarSel AppBarSelFromKind(const char* kind) {
	if (!kind || !*kind) return AppBarSel::None;
	if (strcmp(kind, "TASK") == 0) return AppBarSel::Task;
	if (strcmp(kind, "ROW") == 0) return AppBarSel::Row;
	if (strcmp(kind, "MILESTONE") == 0) return AppBarSel::Milestone;
	if (strcmp(kind, "MARKER") == 0) return AppBarSel::Marker;
	if (strcmp(kind, "TEXT") == 0 || strcmp(kind, "NOTE") == 0) return AppBarSel::Note;
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

int wmain(int argc, wchar_t** argv) {
	SetHarnessDpiAwareness();

	bool attach = false;
	for (int i = 1; i < argc; ++i) {
		if (wcscmp(argv[i], L"--attach") == 0) { attach = true; break; }
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

		int cnt = 0;
		HRESULT hr = InsertGantt(app, MakeShowcaseDocument(), &cnt);
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

			auto captureStep = [&](const char* step, const wchar_t* profile) -> std::wstring {
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
				wchar_t appPng[256], ctxPng[256];
				swprintf_s(appPng, 256, L"native\\build\\trace_%ls_%hs_appbar.png", profile ? profile : L"op", step);
				swprintf_s(ctxPng, 256, L"native\\build\\trace_%ls_%hs_ctx.png", profile ? profile : L"op", step);
				bool ok = false;
				HWND ab = OverlayAppBarHwnd();
				if (ab && ::IsWindowVisible(ab)) {
					RECT r{}; ::GetWindowRect(ab, &r);
					ok = CaptureRectToPng(r, appPng);
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
				wprintf(L"TRACE %hs: %hs\n", step, stateWithTMs.c_str());
				wprintf(L"TRACE %hs ARTIFACTS: %ls %ls %ls %ls %ls\n", step, appPng, ctxPng, ovPng, chartPng, largePng);
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
				// Showcase doc task id (MakeShowcaseDocument has no literal t1).
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
				Overlay_PerformAppBarCommandForTest(HtCmd_ScaleMonth);
				captureStep("immed", traceProfile);
				PumpFor(150);
				captureStep("+1", traceProfile);
				PumpFor(300);
				captureStep("+3", traceProfile);
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
			} else if (traceProfile && wcscmp(traceProfile, L"task-select-progress") == 0) {
				// Select a task body, verify TASK sel, progress should be actionable and visible.
				Overlay_SelectForTest("TASK", "discovery");
				// Simulate the common case (native PPT sel remains CHART_ROOT group after item click).
				// Pre-fix this caused full-chart takeover in selScreenRect/frameRect.
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
				PumpFor(400);  // longer settle for bands after select
				captureStep("task-sel", traceProfile);
				// Test edit without breaking visuals (percent controls now in TASK appbar).
				Overlay_PerformAppBarCommandForTest(HtCmd_PercentPlus10);
				Overlay_InvalidateAppBarForTest();
				PumpFor(400);  // longer for rebuild after edit
				captureStep("+1", traceProfile);
				Overlay_PerformAppBarCommandForTest(HtCmd_PercentMinus10);
				Overlay_InvalidateAppBarForTest();
				PumpFor(400);
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
