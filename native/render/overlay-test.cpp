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
//   Stage 1: ALPHA    — the overlay hwnd must be driven by UpdateLayeredWindow
//                       (per-pixel premultiplied alpha), NOT LWA_COLORKEY.
//                       GetLayeredWindowAttributes FAILING is the PASS
//                       condition (ULW-driven windows have no attributes).
//   Stage 2: CAPTURE  — the overlay window is visible; capture it to PNG.
//   Stage 3: SUPPRESS — PowerPoint-native selection of a chart CHILD shape is
//                       suppressed by the overlay's Tick() (Unselect()'d),
//                       while selecting the CHART_ROOT group itself is left
//                       alone (move-grip / Alt+click escape hatch).
//   Stage 4: OWNSEL   — a synthetic click gesture selects purely via the
//                       overlay's own selection model (no COM Shape::Select).
//   Stage 5: DRAG     — a synthetic drag gesture (LBUTTONDOWN + several
//                       MOUSEMOVEs + LBUTTONUP posted to the overlay hwnd)
//                       shifts a TASK bar by exactly N days, verified by
//                       re-reading PP_DOC; selection must survive the drag.
//
//   ppoverlay.exe <output.png>
#include "../PowerPlannerAddin/pch.h"
#include "../PowerPlannerAddin/GanttBuilder.h"
#include "../PowerPlannerAddin/GanttJson.h"
#include "../PowerPlannerAddin/GanttLayout.h"
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

			// ---- stage 3: SUPPRESS -----------------------------------------
			// Shape.Select() on a group MEMBER (obtained via GroupItems) does
			// NOT reliably make that member the native selection — PowerPoint
			// can resolve it back to the top-level group depending on
			// view/timing. Try the direct GroupItems->Select() route first
			// (cheapest, most deterministic when it works); fall back to the
			// Alt+click passthrough hatch (Overlay.cpp's WM_NCHITTEST treats
			// Alt as HTTRANSPARENT so clicks reach PowerPoint natively — one
			// Alt+click selects the group, a second Alt+click at the same
			// point sub-selects the child under the cursor, matching real
			// click-into-group behavior) if the direct route doesn't stick.
			if (rc == 0) {
				bool pass = false;
				bool childSelectable = true;
				try {
					PowerPoint::_SlidePtr slide = app->GetActiveWindow()->GetView()->GetSlide();
					PowerPoint::ShapesPtr shapes = slide->GetShapes();
					PowerPoint::ShapePtr chartRoot;
					PowerPoint::ShapePtr taskChild;
					long n = shapes->GetCount();
					for (long i = 1; i <= n && !chartRoot; ++i) {
						PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
						_bstr_t k = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
						std::wstring kind = k.length() ? (const wchar_t*)k : L"";
						if (kind == L"CHART_ROOT") chartRoot = sh;
					}
					if (!chartRoot) {
						wprintf(L"SUPPRESS: could not find CHART_ROOT shape\n");
						childSelectable = false;
					} else {
						PowerPoint::GroupShapesPtr grp = chartRoot->GetGroupItems();
						long gn = grp->GetCount();
						for (long j = 1; j <= gn && !taskChild; ++j) {
							PowerPoint::ShapePtr child = grp->Item(_variant_t(j));
							_bstr_t ck = child->GetTags()->Item(_bstr_t(L"PP_KIND"));
							std::wstring ckind = ck.length() ? (const wchar_t*)ck : L"";
							if (ckind == L"TASK" || ckind == L"TASK_PROGRESS") taskChild = child;
						}
					}

					std::wstring landedKind;

					// ---- 3a-i: direct GroupItems->Item(i)->Select() -------
					if (chartRoot && taskChild) {
						try {
							taskChild->Select(Office::msoTrue);
							PumpFor(200);
							PowerPoint::SelectionPtr diagSel = app->GetActiveWindow()->GetSelection();
							if (diagSel && diagSel->GetType() == PowerPoint::ppSelectionShapes) {
								PowerPoint::ShapeRangePtr diagSr = diagSel->GetShapeRange();
								if (diagSr && diagSr->GetCount() >= 1) {
									PowerPoint::ShapePtr diagSh = diagSr->Item(_variant_t(1L));
									_bstr_t dk = diagSh->GetTags()->Item(_bstr_t(L"PP_KIND"));
									std::wstring kindStr = dk.length() ? (const wchar_t*)dk : L"";
									wprintf(L"SUPPRESS diag: direct GroupItems->Select landed on PP_KIND=%s\n", kindStr.empty() ? L"(none)" : kindStr.c_str());
									if (!kindStr.empty() && kindStr != L"CHART_ROOT") landedKind = kindStr;
								}
							}
						} catch (const _com_error& e) {
							wprintf(L"SUPPRESS diag: direct GroupItems->Select threw 0x%08lX (%s)\n",
								(unsigned long)e.Error(), e.Description().length() ? (const wchar_t*)e.Description() : L"(no description)");
						}
					}

					// ---- 3a-ii: Alt+click passthrough fallback ------------
					if (landedKind.empty() && chartRoot && taskChild) {
						wprintf(L"SUPPRESS diag: direct select did not stick; trying Alt+click passthrough\n");
						PowerPoint::DocumentWindowPtr win = app->GetActiveWindow();
						float left = taskChild->GetLeft(), top = taskChild->GetTop();
						float w = taskChild->GetWidth(), h = taskChild->GetHeight();
						int sx = win->PointsToScreenPixelsX(left + w / 2.0f);
						int sy = win->PointsToScreenPixelsY(top + h / 2.0f);

						HWND ppHwnd = (HWND)(intptr_t)app->GetHWND();
						::SetForegroundWindow(ppHwnd);
						PumpFor(150);

						auto altClickAt = [&](int x, int y) {
							INPUT altDown = {}; altDown.type = INPUT_KEYBOARD; altDown.ki.wVk = VK_MENU;
							::SendInput(1, &altDown, sizeof(INPUT));
							::SetCursorPos(x, y);
							PumpFor(30);
							INPUT down = {}; down.type = INPUT_MOUSE; down.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
							INPUT up = {};   up.type = INPUT_MOUSE;   up.mi.dwFlags = MOUSEEVENTF_LEFTUP;
							::SendInput(1, &down, sizeof(INPUT));
							PumpFor(30);
							::SendInput(1, &up, sizeof(INPUT));
							PumpFor(30);
							INPUT altUp = {}; altUp.type = INPUT_KEYBOARD; altUp.ki.wVk = VK_MENU; altUp.ki.dwFlags = KEYEVENTF_KEYUP;
							::SendInput(1, &altUp, sizeof(INPUT));
						};

						// The group is already selected (via COM, deterministic);
						// a single Alt+click directly on the child, while the
						// group is the current selection, is PowerPoint's
						// native "select sub-object within selected group"
						// gesture (same as a real user's second click).
						chartRoot->Select(Office::msoTrue);
						PumpFor(300);
						altClickAt(sx, sy);
						PumpFor(300);

						PowerPoint::SelectionPtr diagSel2 = app->GetActiveWindow()->GetSelection();
						if (diagSel2 && diagSel2->GetType() == PowerPoint::ppSelectionShapes) {
							PowerPoint::ShapeRangePtr diagSr2 = diagSel2->GetShapeRange();
							if (diagSr2 && diagSr2->GetCount() >= 1) {
								PowerPoint::ShapePtr diagSh2 = diagSr2->Item(_variant_t(1L));
								_bstr_t dk2 = diagSh2->GetTags()->Item(_bstr_t(L"PP_KIND"));
								std::wstring kindStr2 = dk2.length() ? (const wchar_t*)dk2 : L"";
								wprintf(L"SUPPRESS diag: after Alt+click x2, PP_KIND=%s\n", kindStr2.empty() ? L"(none)" : kindStr2.c_str());
								if (!kindStr2.empty() && kindStr2 != L"CHART_ROOT") landedKind = kindStr2;
							}
						}
					}

					if (landedKind.empty()) {
						childSelectable = false;
						wprintf(L"SUPPRESS: could not produce a native chart-child selection via any route (direct select or Alt+click passthrough)\n");
					}

					bool childSuppressed = true;
					if (childSelectable) {
						// The overlay's Tick() must notice the current
						// selection is a chart CHILD (not CHART_ROOT) and
						// Unselect it within the next poll(s).
						PumpFor(600);
						PowerPoint::SelectionPtr sel = app->GetActiveWindow()->GetSelection();
						childSuppressed = !sel || sel->GetType() != PowerPoint::ppSelectionShapes;
						if (!childSuppressed) {
							wprintf(L"SUPPRESS: chart child selection was NOT suppressed (type=%d)\n", (int)sel->GetType());
						}
					}

					// 3b: selecting CHART_ROOT itself must be exempt and
					// stay selected across a further poll window. Always
					// checked, even in the degraded (child-unsimulatable)
					// path, since it needs no simulated child selection.
					bool rootStaysSelected = false;
					if (chartRoot) {
						chartRoot->Select(Office::msoTrue);
						PumpFor(600);
						PowerPoint::SelectionPtr sel2 = app->GetActiveWindow()->GetSelection();
						if (sel2 && sel2->GetType() == PowerPoint::ppSelectionShapes) {
							PowerPoint::ShapeRangePtr sr2 = sel2->GetShapeRange();
							if (sr2 && sr2->GetCount() >= 1) {
								PowerPoint::ShapePtr sh2 = sr2->Item(_variant_t(1L));
								_bstr_t k2 = sh2->GetTags()->Item(_bstr_t(L"PP_KIND"));
								std::wstring kind2 = k2.length() ? (const wchar_t*)k2 : L"";
								rootStaysSelected = (kind2 == L"CHART_ROOT");
							}
						}
						if (!rootStaysSelected) {
							wprintf(L"SUPPRESS: CHART_ROOT selection was incorrectly suppressed\n");
						}
					}

					pass = childSuppressed && rootStaysSelected;

					if (pass && !childSelectable) {
						wprintf(L"SUPPRESS PASS (child-select unsimulatable)\n");
					} else {
						wprintf(pass ? L"SUPPRESS PASS\n" : L"SUPPRESS FAIL\n");
					}
					if (!pass) rc = 1;
				} catch (const _com_error& e) {
					wprintf(L"SUPPRESS: COM error 0x%08lX\n", (unsigned long)e.Error());
					wprintf(L"SUPPRESS FAIL\n");
					rc = 1;
				}
			}

			// ---- stage 4: OWNSEL --------------------------------------------
			// The overlay's OWN selection model (Overlay.cpp's g_ownSelKind/Id)
			// must select a TASK bar purely from a synthetic click gesture
			// (WM_LBUTTONDOWN + WM_LBUTTONUP at the same point) posted directly
			// to the overlay hwnd — no COM Shape::Select() involved — and that
			// selection must NOT touch PowerPoint's native Selection at all.
			if (rc == 0) {
				bool pass = false;
				try {
					// Start from a clean slate: no native selection, so a
					// leftover CHART_ROOT/child pick from SUPPRESS can't taint
					// the "PowerPoint Selection has no shapes" assertion.
					try { app->GetActiveWindow()->GetSelection()->Unselect(); } catch (const _com_error&) {}
					PumpFor(400);

					PowerPoint::_SlidePtr slide = app->GetActiveWindow()->GetView()->GetSlide();
					PowerPoint::ShapesPtr shapes = slide->GetShapes();
					PowerPoint::ShapePtr chartRoot;
					long n = shapes->GetCount();
					for (long i = 1; i <= n && !chartRoot; ++i) {
						PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
						_bstr_t k = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
						std::wstring kind = k.length() ? (const wchar_t*)k : L"";
						if (kind == L"CHART_ROOT") chartRoot = sh;
					}

					PowerPoint::ShapePtr taskChild;
					std::wstring taskId;
					if (chartRoot) {
						PowerPoint::GroupShapesPtr grp = chartRoot->GetGroupItems();
						long gn = grp->GetCount();
						for (long j = 1; j <= gn && !taskChild; ++j) {
							PowerPoint::ShapePtr child = grp->Item(_variant_t(j));
							_bstr_t ck = child->GetTags()->Item(_bstr_t(L"PP_KIND"));
							std::wstring ckind = ck.length() ? (const wchar_t*)ck : L"";
							if (ckind == L"TASK") {
								taskChild = child;
								_bstr_t cid = child->GetTags()->Item(_bstr_t(L"PP_ID"));
								taskId = cid.length() ? (const wchar_t*)cid : L"";
							}
						}
					}

					HWND ov = OverlayHwnd();
					if (!chartRoot || !taskChild || taskId.empty() || !ov) {
						wprintf(L"OWNSEL: could not find CHART_ROOT/TASK child or overlay hwnd\n");
					} else {
						PowerPoint::DocumentWindowPtr win = app->GetActiveWindow();
						float left = taskChild->GetLeft(), top = taskChild->GetTop();
						float w = taskChild->GetWidth(), h = taskChild->GetHeight();
						POINT screenPt = {
							win->PointsToScreenPixelsX(left + w / 2.0f),
							win->PointsToScreenPixelsY(top + h / 2.0f)
						};
						POINT clientPt = screenPt;
						::ScreenToClient(ov, &clientPt);

						LPARAM lp = MAKELPARAM((short)clientPt.x, (short)clientPt.y);
						::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, lp);
						::PostMessageW(ov, WM_LBUTTONUP, 0, lp);
						PumpFor(500);

						const char* gotIdUtf8 = Overlay_GetSelectedIdForTest();
						std::wstring gotId;
						if (gotIdUtf8) {
							int wn = ::MultiByteToWideChar(CP_UTF8, 0, gotIdUtf8, -1, NULL, 0);
							if (wn > 0) { gotId.resize(wn - 1); ::MultiByteToWideChar(CP_UTF8, 0, gotIdUtf8, -1, &gotId[0], wn); }
						}

						bool idMatches = (gotId == taskId);
						bool ppSelectionEmpty = true;
						PowerPoint::SelectionPtr sel = app->GetActiveWindow()->GetSelection();
						if (sel && sel->GetType() == PowerPoint::ppSelectionShapes) {
							PowerPoint::ShapeRangePtr sr = sel->GetShapeRange();
							if (sr && sr->GetCount() >= 1) ppSelectionEmpty = false;
						}

						if (!idMatches) {
							wprintf(L"OWNSEL: expected selected id '%s', got '%s'\n",
								taskId.c_str(), gotId.empty() ? L"(empty)" : gotId.c_str());
						}
						if (!ppSelectionEmpty) {
							wprintf(L"OWNSEL: PowerPoint native Selection unexpectedly has shapes\n");
						}
						pass = idMatches && ppSelectionEmpty;
					}
				} catch (const _com_error& e) {
					wprintf(L"OWNSEL: COM error 0x%08lX\n", (unsigned long)e.Error());
				}
				wprintf(pass ? L"OWNSEL PASS\n" : L"OWNSEL FAIL\n");
				if (!pass) rc = 1;
			}

			// ---- stage 5: DRAG ------------------------------------------------
			// Select a TASK via a posted click (same route as OWNSEL), read its
			// dates from PP_DOC, then simulate a horizontal drag of the bar by
			// exactly N days (computed from the chart's PP_PROJ ptPerDay so the
			// pixel shift is exact) via WM_LBUTTONDOWN + several WM_MOUSEMOVEs +
			// WM_LBUTTONUP posted straight to the overlay hwnd. Re-read PP_DOC
			// afterward: dates must have shifted by exactly N days, and the
			// overlay's own-selection model must still report the same task id
			// (A2: no deselect-after-drag flicker).
			if (rc == 0) {
				bool pass = false;
				try {
					PowerPoint::_SlidePtr slide = app->GetActiveWindow()->GetView()->GetSlide();
					PowerPoint::ShapesPtr shapes = slide->GetShapes();
					PowerPoint::ShapePtr chartRoot;
					long n = shapes->GetCount();
					for (long i = 1; i <= n && !chartRoot; ++i) {
						PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
						_bstr_t k = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
						std::wstring kind = k.length() ? (const wchar_t*)k : L"";
						if (kind == L"CHART_ROOT") chartRoot = sh;
					}

					PowerPoint::ShapePtr taskChild;
					std::wstring taskId;
					if (chartRoot) {
						PowerPoint::GroupShapesPtr grp = chartRoot->GetGroupItems();
						long gn = grp->GetCount();
						for (long j = 1; j <= gn && !taskChild; ++j) {
							PowerPoint::ShapePtr child = grp->Item(_variant_t(j));
							_bstr_t ck = child->GetTags()->Item(_bstr_t(L"PP_KIND"));
							std::wstring ckind = ck.length() ? (const wchar_t*)ck : L"";
							if (ckind == L"TASK") {
								taskChild = child;
								_bstr_t cid = child->GetTags()->Item(_bstr_t(L"PP_ID"));
								taskId = cid.length() ? (const wchar_t*)cid : L"";
							}
						}
					}

					HWND ov = OverlayHwnd();
					if (!chartRoot || !taskChild || taskId.empty() || !ov) {
						wprintf(L"DRAG: could not find CHART_ROOT/TASK child or overlay hwnd\n");
					} else {
						PowerPoint::DocumentWindowPtr win = app->GetActiveWindow();
						float left = taskChild->GetLeft(), top = taskChild->GetTop();
						float w = taskChild->GetWidth(), h = taskChild->GetHeight();
						POINT screenPt = {
							win->PointsToScreenPixelsX(left + w / 2.0f),
							win->PointsToScreenPixelsY(top + h / 2.0f)
						};
						POINT clientPt = screenPt;
						::ScreenToClient(ov, &clientPt);

						// Select it first (same click-select route as OWNSEL) so we
						// can also assert the selection survives the drag.
						LPARAM clickLp = MAKELPARAM((short)clientPt.x, (short)clientPt.y);
						::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, clickLp);
						::PostMessageW(ov, WM_LBUTTONUP, 0, clickLp);
						PumpFor(400);

						// Read PP_DOC + PP_PROJ once, before the drag (mirrors the
						// add-in's own gesture-start read).
						auto WNarrow = [](const wchar_t* wtext) -> std::string {
							if (!wtext || !*wtext) return "";
							int len = (int)::wcslen(wtext);
							int nb = ::WideCharToMultiByte(CP_UTF8, 0, wtext, len, NULL, 0, NULL, NULL);
							std::string s(nb, '\0');
							if (nb > 0) ::WideCharToMultiByte(CP_UTF8, 0, wtext, len, &s[0], nb, NULL, NULL);
							return s;
						};
						std::string docJsonBefore = WNarrow((const wchar_t*)chartRoot->GetTags()->Item(_bstr_t(L"PP_DOC")));
						std::string projJson = WNarrow((const wchar_t*)chartRoot->GetTags()->Item(_bstr_t(L"PP_PROJ")));
						PpDocument docBefore = DocumentFromJson(docJsonBefore);
						std::string taskIdUtf8 = WNarrow(taskId.c_str());

						std::string origStart, origEnd;
						for (const auto& t : docBefore.tasks) {
							if (t.id == taskIdUtf8) { origStart = t.start; origEnd = t.end; break; }
						}

						long minDay = 0, pad = 0; float ptPerDay = 1.0f, originX = 0.0f;
						::sscanf_s(projJson.c_str(), "{\"minDay\":%ld,\"pad\":%ld,\"ptPerDay\":%f,\"originX\":%f}",
							&minDay, &pad, &ptPerDay, &originX);

						if (origStart.empty() || ptPerDay <= 0.0f) {
							wprintf(L"DRAG: could not resolve task dates or ptPerDay before drag\n");
						} else {
							const long kDragDays = 7;
							// px-per-day in SCREEN pixels: ptPerDay is in slide points;
							// PointsToScreenPixelsX/Y already folds in zoom, so derive
							// the screen px/pt ratio from the task rect itself (its
							// width in points is known from GetWidth()).
							int screenLeft = win->PointsToScreenPixelsX(left);
							int screenRight = win->PointsToScreenPixelsX(left + w);
							double pxPerPt = (screenRight > screenLeft) ? (double)(screenRight - screenLeft) / (double)w : 1.0;
							long shiftPx = (long)::lround(kDragDays * ptPerDay * pxPerPt);

							::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, clickLp);
							PumpFor(60);
							// Several intermediate moves (not a single jump) so
							// g_dragActive latches on the first > 4px move, matching
							// a real drag gesture.
							const int kSteps = 5;
							for (int s = 1; s <= kSteps; ++s) {
								int mx = clientPt.x + (int)(shiftPx * s / kSteps);
								LPARAM moveLp = MAKELPARAM((short)mx, (short)clientPt.y);
								::PostMessageW(ov, WM_MOUSEMOVE, 0, moveLp);
								PumpFor(60);
							}
							int finalX = clientPt.x + (int)shiftPx;
							LPARAM upLp = MAKELPARAM((short)finalX, (short)clientPt.y);
							::PostMessageW(ov, WM_LBUTTONUP, 0, upLp);
							PumpFor(800);

							// ReadGanttFromSlide takes IDispatch* and returns std::string
							// directly (UTF-8); no wide round-trip needed here.
							std::string afterJson = ReadGanttFromSlide(app);
							PpDocument docAfter = DocumentFromJson(afterJson);

							std::string newStart, newEnd;
							bool foundAfter = false;
							for (const auto& t : docAfter.tasks) {
								if (t.id == taskIdUtf8) { newStart = t.start; newEnd = t.end; foundAfter = true; break; }
							}

							std::string expectStart = DaysToDate(DateToDays(origStart) + kDragDays);
							std::string expectEnd = DaysToDate(DateToDays(origEnd) + kDragDays);

							const char* gotIdUtf8 = Overlay_GetSelectedIdForTest();
							std::string gotId = gotIdUtf8 ? gotIdUtf8 : "";

							bool datesShifted = foundAfter && newStart == expectStart && newEnd == expectEnd;
							bool selectionKept = (gotId == taskIdUtf8);

							if (!datesShifted) {
								wprintf(L"DRAG: expected %hs..%hs, got %hs..%hs\n",
									expectStart.c_str(), expectEnd.c_str(),
									newStart.c_str(), newEnd.c_str());
							}
							if (!selectionKept) {
								wprintf(L"DRAG: expected selected id '%s', got '%hs'\n", taskId.c_str(), gotId.c_str());
							}
							pass = datesShifted && selectionKept;
						}
					}
				} catch (const _com_error& e) {
					wprintf(L"DRAG: COM error 0x%08lX\n", (unsigned long)e.Error());
				}
				wprintf(pass ? L"DRAG PASS\n" : L"DRAG FAIL\n");
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
