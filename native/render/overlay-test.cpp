// N4 overlay test: drive PowerPoint via automation, insert a chart, select it,
// run the on-slide overlay, and verify + screen-capture the result (the overlay
// is a real screen window, so BitBlt from the screen DC captures it composited
// over the slide). Also exercises the selection/PointsToScreenPixels logic,
// which logs to %TEMP%\powerplanner-addin.log.
//
// STAGE RULE: stages run in a FIXED order; on any stage failure the harness
// prints that stage's FAIL marker and exits nonzero IMMEDIATELY (skipping the
// remaining stages) — but always quits PowerPoint and releases every COM
// pointer BEFORE CoUninitialize so POWERPNT never leaks.
//
//   Stage 1: ALPHA   — the overlay hwnd must be driven by UpdateLayeredWindow
//                      (per-pixel premultiplied alpha), NOT LWA_COLORKEY.
//                      GetLayeredWindowAttributes FAILING is the PASS
//                      condition (ULW-driven windows have no attributes).
//   Stage 2: CAPTURE — the overlay window is visible; capture it to PNG.
//
//   ppoverlay.exe <output.png>
#include "../PowerPlannerAddin/pch.h"
#include "../PowerPlannerAddin/GanttBuilder.h"
#include "../PowerPlannerAddin/Overlay.h"
// GDI+ headers need the min/max macros that pch.h's NOMINMAX removes.
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#include <gdiplus.h>
#include <cstdio>
#include <string>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

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
		// Check the deadline inside the drain loop too: overlay ticks can take
		// longer than the 150ms timer period, so a due WM_TIMER is ALWAYS
		// pending and an unbounded drain would never return control.
		while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			if ((long)(end - ::GetTickCount()) <= 0) return;
		}
		::Sleep(15);
	}
}

// Detached watchdog: if anything hangs (modal PowerPoint dialog, COM stall),
// kill the harness after 120s instead of leaking POWERPNT forever.
static DWORD WINAPI WatchdogProc(LPVOID) {
	::Sleep(120000);
	wprintf(L"WATCHDOG: 120s timeout, exiting\n");
	::ExitProcess(3);
	return 0;
}

int wmain(int argc, wchar_t** argv) {
	const wchar_t* outPath = (argc > 1) ? argv[1] : L"overlay.png";

	// Unbuffered stdout: markers must reach the pipe even if the watchdog
	// ExitProcess()es (which does NOT flush CRT buffers).
	setvbuf(stdout, NULL, _IONBF, 0);

	HANDLE wd = ::CreateThread(NULL, 0, WatchdogProc, NULL, 0, NULL);
	if (wd) ::CloseHandle(wd);

	// Match PowerPoint's DPI awareness so PointsToScreenPixels and our window
	// coordinates agree (the real add-in inherits this from PowerPoint).
	::SetProcessDPIAware();
	::CoInitialize(NULL);
	GdiplusStartupInput gsi; ULONG_PTR tok; GdiplusStartup(&tok, &gsi, NULL);
	int rc = 0;
	{
		// Every COM smart pointer lives inside this scope so all of them are
		// released before CoUninitialize on every exit path.
		PowerPoint::_ApplicationPtr app;
		PowerPoint::_PresentationPtr pres;
		bool overlayStarted = false;
		try {
			app.CreateInstance(L"PowerPoint.Application");
			app->PutVisible(Office::msoTrue);
			wprintf(L"harness: powerpoint started\n");
			pres = app->GetPresentations()->Add(Office::msoTrue);
			{
				PowerPoint::SlidesPtr slides = pres->GetSlides();
				slides->Add(1, PowerPoint::ppLayoutBlank);
			}
			app->GetActiveWindow()->GetView()->GotoSlide(1);

			int count = 0;
			InsertGantt(app, MakeSampleDocument(), &count);
			wprintf(L"harness: chart inserted (%d shapes)\n", count);

			// Select the chart root group (scoped so the pointers release early).
			{
				PowerPoint::_SlidePtr slide = app->GetActiveWindow()->GetView()->GetSlide();
				PowerPoint::ShapesPtr shapes = slide->GetShapes();
				long n = shapes->GetCount();
				for (long i = 1; i <= n; ++i) {
					PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
					_bstr_t k = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
					if (k.length() && std::wstring((const wchar_t*)k) == L"CHART_ROOT") { sh->Select(Office::msoTrue); break; }
				}
			}

			// Maximize + activate so the slide is actually rendered on screen
			// behind the (transparent) overlay before we capture.
			try { app->GetActiveWindow()->PutWindowState(PowerPoint::ppWindowMaximized); } catch (...) {}
			try { app->Activate(); } catch (...) {}
			HWND ppHwnd = (HWND)(intptr_t)app->GetHWND();
			::ShowWindow(ppHwnd, SW_SHOWMAXIMIZED);
			::SetForegroundWindow(ppHwnd);
			PumpFor(600);

			OverlayStart(app);
			overlayStarted = true;
			wprintf(L"harness: overlay started\n");
			PumpFor(2200);  // let the polling timer fire + paint

			// ---- stage 1: ALPHA -------------------------------------------
			// The overlay must be UpdateLayeredWindow-driven per-pixel alpha.
			// GetLayeredWindowAttributes FAILS for ULW windows => PASS.
			// If it succeeds and dwFlags has LWA_COLORKEY => FAIL.
			if (rc == 0) {
				bool pass = false;
				HWND ov = OverlayHwnd();
				if (!ov) {
					wprintf(L"overlay hwnd missing\n");
				} else {
					COLORREF key = 0; BYTE alpha = 0; DWORD flags = 0;
					BOOL got = ::GetLayeredWindowAttributes(ov, &key, &alpha, &flags);
					if (!got) {
						pass = true;  // no layered attributes: ULW-driven window
					} else if (flags & LWA_COLORKEY) {
						wprintf(L"overlay still uses LWA_COLORKEY (key=0x%06lX)\n", (unsigned long)key);
					} else {
						pass = true;  // attributes exist but no color key
					}
				}
				wprintf(pass ? L"ALPHA PASS\n" : L"ALPHA FAIL\n");
				if (!pass) rc = 1;
			}

			// ---- stage 2: CAPTURE -----------------------------------------
			if (rc == 0) {
				bool pass = false;
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
					{
						Bitmap gb(bmp, NULL);
						CLSID png; GetEncoderClsid(L"image/png", &png);
						gb.Save(outPath, &png, NULL);
					}
					::DeleteObject(bmp); ::DeleteDC(mem); ::ReleaseDC(NULL, screen);
					wprintf(L"captured overlay -> %s (%dx%d)\n", outPath, w, h);
					pass = true;
				} else {
					wprintf(L"overlay window not visible (selection/timer issue)\n");
				}
				wprintf(pass ? L"CAPTURE PASS\n" : L"CAPTURE FAIL\n");
				if (!pass) rc = 1;
			}
		} catch (const _com_error& e) {
			wprintf(L"COM error 0x%08lX: %s\n", (unsigned long)e.Error(),
				e.Description().length() ? (const wchar_t*)e.Description() : L"(no description)");
			rc = 1;
		}

		// Cleanup on EVERY exit path (success, stage failure, _com_error):
		// stop the overlay, quit PowerPoint, and release all COM pointers —
		// all BEFORE CoUninitialize so POWERPNT never leaks.
		if (overlayStarted) {
			try { OverlayStop(); } catch (...) {}
		}
		try { if (pres) { pres->PutSaved(Office::msoTrue); pres->Close(); } } catch (...) {}
		try { if (app) app->Quit(); } catch (...) {}
		pres = nullptr;
		app = nullptr;
	}
	GdiplusShutdown(tok);
	::CoUninitialize();
	return rc;
}
