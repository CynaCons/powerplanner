// N4 overlay test: drive PowerPoint via automation, insert a chart, select it,
// run the on-slide overlay, and screen-capture the result (the overlay is a
// real screen window, so BitBlt from the screen DC captures it composited over
// the slide). Also exercises the selection/PointsToScreenPixels logic, which
// logs to %TEMP%\powerplanner-addin.log.
//
//   ppoverlay.exe <output.png>
#include "../PowerPlannerAddin/pch.h"
#include "../PowerPlannerAddin/GanttBuilder.h"
#include "../PowerPlannerAddin/Overlay.h"
// GDI+ headers need the min/max macros that pch.h's NOMINMAX removes.
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#include <gdiplus.h>
#include <cstdio>
#include <string>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")

using namespace Gdiplus;

static int GetEncoderClsid(const WCHAR* format, CLSID* clsid) {
	UINT num = 0, size = 0;
	GetImageEncodersSize(&num, &size);
	if (size == 0) return -1;
	ImageCodecInfo* info = (ImageCodecInfo*)malloc(size);
	GetImageEncoders(num, size, info);
	int result = -1;
	for (UINT i = 0; i < num; ++i)
		if (wcscmp(info[i].MimeType, format) == 0) { *clsid = info[i].Clsid; result = i; break; }
	free(info);
	return result;
}

static void PumpFor(DWORD ms) {
	DWORD end = ::GetTickCount() + ms;
	MSG msg;
	while ((long)(end - ::GetTickCount()) > 0) {
		while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { ::TranslateMessage(&msg); ::DispatchMessage(&msg); }
		::Sleep(15);
	}
}

int wmain(int argc, wchar_t** argv) {
	const wchar_t* outPath = (argc > 1) ? argv[1] : L"overlay.png";
	// Match PowerPoint's DPI awareness so PointsToScreenPixels and our window
	// coordinates agree (the real add-in inherits this from PowerPoint).
	::SetProcessDPIAware();
	::CoInitialize(NULL);
	GdiplusStartupInput gsi; ULONG_PTR tok; GdiplusStartup(&tok, &gsi, NULL);
	int rc = 0;
	try {
		PowerPoint::_ApplicationPtr app;
		app.CreateInstance(L"PowerPoint.Application");
		app->PutVisible(Office::msoTrue);
		PowerPoint::_PresentationPtr pres = app->GetPresentations()->Add(Office::msoTrue);
		PowerPoint::SlidesPtr slides = pres->GetSlides();
		slides->Add(1, PowerPoint::ppLayoutBlank);
		app->GetActiveWindow()->GetView()->GotoSlide(1);

		int count = 0;
		InsertGantt(app, MakeSampleDocument(), &count);

		// Select the chart root group.
		PowerPoint::_SlidePtr slide = app->GetActiveWindow()->GetView()->GetSlide();
		PowerPoint::ShapesPtr shapes = slide->GetShapes();
		long n = shapes->GetCount();
		for (long i = 1; i <= n; ++i) {
			PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
			_bstr_t k = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (k.length() && std::wstring((const wchar_t*)k) == L"CHART_ROOT") { sh->Select(Office::msoTrue); break; }
		}

		// Maximize + activate so the slide is actually rendered on screen behind
		// the (transparent) overlay before we capture.
		try { app->GetActiveWindow()->PutWindowState(PowerPoint::ppWindowMaximized); } catch (...) {}
		try { app->Activate(); } catch (...) {}
		HWND ppHwnd = (HWND)(intptr_t)app->GetHWND();
		::ShowWindow(ppHwnd, SW_SHOWMAXIMIZED);
		::SetForegroundWindow(ppHwnd);
		PumpFor(600);

		OverlayStart(app);
		PumpFor(2200);  // let the polling timer fire + paint

		HWND ov = OverlayHwnd();
		RECT r;
		if (ov && ::IsWindowVisible(ov) && ::GetWindowRect(ov, &r)) {
			::InflateRect(&r, 40, 40);
			int w = r.right - r.left, h = r.bottom - r.top;
			HDC screen = ::GetDC(NULL);
			HDC mem = ::CreateCompatibleDC(screen);
			HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
			HGDIOBJ old = ::SelectObject(mem, bmp);
			::BitBlt(mem, 0, 0, w, h, screen, r.left, r.top, SRCCOPY);
			::SelectObject(mem, old);
			Bitmap gb(bmp, NULL);
			CLSID png; GetEncoderClsid(L"image/png", &png);
			gb.Save(outPath, &png, NULL);
			::DeleteObject(bmp); ::DeleteDC(mem); ::ReleaseDC(NULL, screen);
			wprintf(L"captured overlay -> %s (%dx%d)\n", outPath, w, h);
		} else {
			wprintf(L"overlay window not visible (selection/timer issue)\n");
			rc = 1;
		}

		OverlayStop();
		pres->PutSaved(Office::msoTrue);
		pres->Close();
		app->Quit();
	} catch (const _com_error& e) {
		wprintf(L"COM error 0x%08lX: %s\n", (unsigned long)e.Error(),
			e.Description().length() ? (const wchar_t*)e.Description() : L"(no description)");
		rc = 1;
	}
	GdiplusShutdown(tok);
	::CoUninitialize();
	return rc;
}
