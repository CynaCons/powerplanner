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
			int matrixRc = 0;
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
				} else {
					wprintf(L"APPBAR-MATRIX %hs FAIL\n", ctxLabel);
					matrixRc = 1;
				}
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
