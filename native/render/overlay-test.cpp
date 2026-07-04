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
//   Stage 6: DRAGROW  — a synthetic drag straight down by one row-band
//                       height row-reassigns a TASK to the adjacent row,
//                       verified by re-reading PP_DOC.
//   Stage 7: CREATE   — a synthetic drag anchored on an EmptyCell (a row
//                       with no task under the pointer) creates a new task
//                       spanning the drag, verified by re-reading PP_DOC
//                       (task count +1, correct row, ~N-day span).
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
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
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

// The add-in's overlay now scopes its chrome to whether PowerPoint is the
// FOREGROUND app (see Overlay.cpp's IsHostActiveForOverlayChrome) — a
// necessary fix for the "chrome stays on top of other apps" bug, but it
// means this harness (a separate process whose own console/window can itself
// become foreground, e.g. after SendInput or a COM call returns) must
// actively re-steal the foreground before every stage that depends on the
// overlay chrome being visible, or that stage would spuriously fail with an
// now-hidden overlay that has nothing to do with a real regression.
//
// SetForegroundWindow can be silently refused by Windows' foreground-lock
// heuristics when the calling process didn't just receive input; the
// classic workaround is to tap Alt (keybd_event VK_MENU down/up) around the
// call, which satisfies the "last input event" requirement Windows checks.
//
// BUT the Alt-tap is itself hazardous here (coordinator-confirmed
// nondeterministic KEYS failures, two distinct symptoms across runs): a
// lingering Alt-down async keystate (a) flips the overlay's WM_NCHITTEST
// into its HTTRANSPARENT Alt-passthrough hatch so subsequent clicks bypass
// the overlay entirely, and (b) turns later key events into Alt+<key>
// chords that a modifierless hotkey never matches. So:
//   1. Try a PLAIN SetForegroundWindow first — no key events at all. It
//      succeeds whenever this process already owns the foreground or has
//      steal permission, which is the common case mid-run.
//   2. Only fall back to the Alt-tap when the plain call is refused.
//   3. After ANY Alt-tap, actively guarantee Alt is released before
//      returning: re-send VK_MENU keyups (plain + extended scancode) and
//      poll GetAsyncKeyState(VK_MENU) until it reads clear, time-bounded.
//   4. On success, pump ~2 overlay tick periods (300ms, time-bounded) so
//      the overlay's 150ms Tick has observed the new foreground state
//      before the caller starts posting gestures at it.
static bool EnsureForeground(HWND targetHwnd, DWORD timeoutMs = 3000) {
	if (!targetHwnd) return false;
	HWND targetRoot = ::GetAncestor(targetHwnd, GA_ROOT);
	if (!targetRoot) targetRoot = targetHwnd;

	auto isForeground = [targetRoot]() {
		HWND fg = ::GetForegroundWindow();
		return fg && ::GetAncestor(fg, GA_ROOT) == targetRoot;
	};

	bool usedAltTap = false;
	bool ok = false;
	DWORD deadline = ::GetTickCount() + timeoutMs;
	for (;;) {
		if (isForeground()) { ok = true; break; }

		// Plain attempt first: zero side effects on keyboard state.
		::SetForegroundWindow(targetRoot);
		PumpFor(50);
		if (isForeground()) { ok = true; break; }
		if ((long)(deadline - ::GetTickCount()) <= 0) break;

		// Refused: fall back to the Alt-tap variant.
		usedAltTap = true;
		::keybd_event(VK_MENU, 0, 0, 0);
		::keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
		::SetForegroundWindow(targetRoot);
		PumpFor(50);
		if (isForeground()) { ok = true; break; }
		if ((long)(deadline - ::GetTickCount()) <= 0) break;

		::Sleep(50);
	}

	if (usedAltTap) {
		// Guarantee Alt is UP before anything else happens: send keyups for
		// both the plain and extended scancode variants (left/right Alt),
		// then poll the async state until it reads clear — bounded, so a
		// wedged input queue can't stall the harness.
		const BYTE kAltScan = (BYTE)::MapVirtualKeyW(VK_MENU, MAPVK_VK_TO_VSC);
		::keybd_event(VK_MENU, kAltScan, KEYEVENTF_KEYUP, 0);
		::keybd_event(VK_MENU, kAltScan, KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY, 0);
		DWORD altDeadline = ::GetTickCount() + 500;
		while (::GetAsyncKeyState(VK_MENU) & 0x8000) {
			::keybd_event(VK_MENU, kAltScan, KEYEVENTF_KEYUP, 0);
			if ((long)(altDeadline - ::GetTickCount()) <= 0) break;
			::Sleep(15);
		}
	}

	if (ok) {
		// Settle: let the overlay's 150ms Tick poller observe the new
		// foreground state (two tick periods, time-bounded) before the
		// caller interacts with the overlay.
		PumpFor(300);
	}
	return ok;
}

// Wrapper used before every visibility-dependent stage: on failure, prints
// the environment-failure marker (distinct from a stage FAIL) and sets rc
// nonzero so a harness/OS environment problem (foreground-lock, no active
// desktop, etc.) is never misread as an overlay-scoping regression.
static bool RequireForeground(HWND targetHwnd, int* rc) {
	if (EnsureForeground(targetHwnd)) return true;
	wprintf(L"FOREGROUND STEAL FAILED\n");
	*rc = 1;
	return false;
}

// Match PowerPoint's DPI awareness (per-monitor-DPI-aware, V2 where available)
// so PointsToScreenPixels and our window coordinates agree exactly like they
// do for the real add-in, which inherits this from POWERPNT.EXE. Resolved
// dynamically via GetProcAddress: SetProcessDpiAwarenessContext is Win10
// 1703+ only, so this falls back to the older process-DPI-aware API (still
// better than unaware, just not per-monitor) on older Windows / SDKs.
static void SetHarnessDpiAwareness() {
	HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
	if (user32) {
		typedef BOOL(WINAPI * SetProcessDpiAwarenessContextFn)(DPI_AWARENESS_CONTEXT);
		auto pSetCtx = (SetProcessDpiAwarenessContextFn)::GetProcAddress(user32, "SetProcessDpiAwarenessContext");
		if (pSetCtx && pSetCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
	}
	::SetProcessDPIAware();
}

int wmain(int argc, wchar_t** argv) {
	const wchar_t* outPath = (argc > 1) ? argv[1] : L"overlay.png";

	// Unbuffered stdout: markers must reach the pipe even if the watchdog
	// ExitProcess()es (which does NOT flush CRT buffers).
	setvbuf(stdout, NULL, _IONBF, 0);

	HANDLE wd = ::CreateThread(NULL, 0, WatchdogProc, NULL, 0, NULL);
	if (wd) ::CloseHandle(wd);

	SetHarnessDpiAwareness();
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
			RequireForeground(ppHwnd, &rc);
			PumpFor(600);

			if (rc == 0) {
				OverlayStart(app);
				overlayStarted = true;
				wprintf(L"harness: overlay started\n");
				PumpFor(2200);  // let the polling timer fire + paint
			}

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
			if (rc == 0) RequireForeground(ppHwnd, &rc);
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
			if (rc == 0) RequireForeground(ppHwnd, &rc);
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

						HWND ppHwndInner = (HWND)(intptr_t)app->GetHWND();
						EnsureForeground(ppHwndInner);
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
			if (rc == 0) RequireForeground(ppHwnd, &rc);
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
			if (rc == 0) RequireForeground(ppHwnd, &rc);
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

						// Park the REAL OS cursor at the gesture point before posting
						// synthetic messages. WM_LBUTTONDOWN/MOUSEMOVE/UP here are
						// POSTED (not real hardware input), but the SUPPRESS stage
						// earlier used SendInput (real hardware events) and left the
						// physical cursor parked wherever that was — a later
						// SetCapture() in this window can make Windows resync with a
						// REAL WM_MOUSEMOVE at that STALE physical position, which
						// then interleaves with our posted synthetic moves and can
						// spuriously retarget an in-progress row-reassign drag
						// (observed: DRAGROW intermittently not detecting the row
						// change because a stray real mousemove at the old SUPPRESS
						// click point raced with our posted moves). Moving the real
						// cursor here makes any such stray event harmless (same point).
						::SetCursorPos(screenPt.x, screenPt.y);

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
							// a real drag gesture. SetCursorPos tracks each posted move:
							// while this window holds capture, Windows can deliver a
							// REAL WM_MOUSEMOVE reporting the actual (else-stale)
							// physical cursor position, which would otherwise interleave
							// with — and can even arrive AFTER — our posted synthetic
							// moves (confirmed empirically via the DRAGROW stage's
							// row-target flake). Keeping the real cursor in sync makes
							// any such stray event harmless.
							const int kSteps = 5;
							for (int s = 1; s <= kSteps; ++s) {
								int mx = clientPt.x + (int)(shiftPx * s / kSteps);
								::SetCursorPos(screenPt.x + (mx - clientPt.x), screenPt.y);
								LPARAM moveLp = MAKELPARAM((short)mx, (short)clientPt.y);
								::PostMessageW(ov, WM_MOUSEMOVE, 0, moveLp);
								PumpFor(60);
							}
							int finalX = clientPt.x + (int)shiftPx;
							::SetCursorPos(screenPt.x + (int)shiftPx, screenPt.y);
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

			// ---- stage 6: DRAGROW ---------------------------------------------
			// Vertical row-reassign: pick a TASK, record its rowId from PP_DOC,
			// drag it straight down by one row-band height (no horizontal
			// component) via posted LBUTTONDOWN/MOUSEMOVEs/LBUTTONUP, then
			// re-read PP_DOC and assert the task's rowId changed to the row
			// whose band the pointer landed in.
			auto WNarrowFn = [](const wchar_t* wtext) -> std::string {
				if (!wtext || !*wtext) return "";
				int len = (int)::wcslen(wtext);
				int nb = ::WideCharToMultiByte(CP_UTF8, 0, wtext, len, NULL, 0, NULL, NULL);
				std::string s(nb, '\0');
				if (nb > 0) ::WideCharToMultiByte(CP_UTF8, 0, wtext, len, &s[0], nb, NULL, NULL);
				return s;
			};
			if (rc == 0) RequireForeground(ppHwnd, &rc);
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

					// Find a TASK child and, separately, every ROW_LABEL child's
					// screen rect (to compute row-band height/order) by walking
					// GroupItems once.
					PowerPoint::ShapePtr taskChild;
					std::wstring taskId;
					struct RowRect { std::wstring rowId; float top; float height; };
					std::vector<RowRect> rowRects;
					if (chartRoot) {
						PowerPoint::GroupShapesPtr grp = chartRoot->GetGroupItems();
						long gn = grp->GetCount();
						for (long j = 1; j <= gn; ++j) {
							PowerPoint::ShapePtr child = grp->Item(_variant_t(j));
							_bstr_t ck = child->GetTags()->Item(_bstr_t(L"PP_KIND"));
							std::wstring ckind = ck.length() ? (const wchar_t*)ck : L"";
							if (ckind == L"TASK" && !taskChild) {
								taskChild = child;
								_bstr_t cid = child->GetTags()->Item(_bstr_t(L"PP_ID"));
								taskId = cid.length() ? (const wchar_t*)cid : L"";
							}
							if (ckind == L"ROW_LABEL") {
								_bstr_t rid = child->GetTags()->Item(_bstr_t(L"PP_ID"));
								std::wstring rowId = rid.length() ? (const wchar_t*)rid : L"";
								if (!rowId.empty()) {
									rowRects.push_back({ rowId, child->GetTop(), child->GetHeight() });
								}
							}
						}
					}
					std::sort(rowRects.begin(), rowRects.end(), [](const RowRect& a, const RowRect& b) { return a.top < b.top; });

					HWND ov = OverlayHwnd();
					std::string docJson = ReadGanttFromSlide(app);
					PpDocument docPre = DocumentFromJson(docJson);
					std::string taskIdUtf8 = WNarrowFn(taskId.c_str());
					std::string origRowId;
					for (const auto& t : docPre.tasks) {
						if (t.id == taskIdUtf8) { origRowId = t.rowId; break; }
					}

					int origIdx = -1;
					for (size_t k = 0; k < rowRects.size(); ++k) {
						if (WNarrowFn(rowRects[k].rowId.c_str()) == origRowId) { origIdx = (int)k; break; }
					}

					if (!chartRoot || !taskChild || taskId.empty() || !ov || origIdx < 0 || origIdx + 1 >= (int)rowRects.size()) {
						wprintf(L"DRAGROW: could not find TASK/rowId/adjacent row band (task rowId=%hs, rows=%zu)\n",
							origRowId.c_str(), rowRects.size());
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

						// Row-band height in SCREEN pixels: derive from the two
						// adjacent ROW_LABEL rects' point-tops via
						// PointsToScreenPixelsY (same conversion the overlay's
						// BuildRowBands uses), so the move lands solidly inside
						// the next row's band regardless of zoom.
						int rowTopScreen = win->PointsToScreenPixelsY(rowRects[origIdx].top);
						int nextRowTopScreen = win->PointsToScreenPixelsY(rowRects[origIdx + 1].top);
						int bandHeightPx = nextRowTopScreen - rowTopScreen;
						std::wstring targetRowId = rowRects[origIdx + 1].rowId;

						// See the DRAG stage's SetCursorPos comment: park the real
						// cursor at the gesture point first so a stray real
						// WM_MOUSEMOVE (from Windows resyncing on SetCapture) can't
						// race our posted moves with a stale position.
						::SetCursorPos(screenPt.x, screenPt.y);

						LPARAM downLp = MAKELPARAM((short)clientPt.x, (short)clientPt.y);
						::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, downLp);
						PumpFor(60);
						const int kSteps = 5;
						for (int s = 1; s <= kSteps; ++s) {
							int my = clientPt.y + (int)((long)bandHeightPx * s / kSteps);
							// ROOT CAUSE (confirmed empirically): while this window
							// holds mouse capture, Windows periodically delivers a
							// REAL WM_MOUSEMOVE reporting wherever the PHYSICAL cursor
							// actually is. We only ever POST synthetic messages here
							// (which move the overlay's notion of pointer position but
							// NOT the real OS cursor), so if the real cursor is left
							// parked at the gesture's start point, that stray real
							// event keeps reporting the ORIGINAL y — and, being a live
							// event (not a one-time backlog), it can interleave
							// with and even follow our LAST posted move, latching the
							// WRONG row right before commit. Moving the real cursor to
							// match at every step makes any such stray event harmless
							// (same position we intend).
							::SetCursorPos(screenPt.x, screenPt.y + (my - clientPt.y));
							LPARAM moveLp = MAKELPARAM((short)clientPt.x, (short)my);
							::PostMessageW(ov, WM_MOUSEMOVE, 0, moveLp);
							PumpFor(60);
						}
						int finalY = clientPt.y + bandHeightPx;
						LPARAM finalMoveLp = MAKELPARAM((short)clientPt.x, (short)finalY);
						// Re-assert the FINAL target position (both real cursor and
						// posted message) a few more times before committing, so any
						// live stray real-cursor mousemove reports the SAME (correct)
						// position rather than a stale one.
						for (int extra = 0; extra < 3; ++extra) {
							::SetCursorPos(screenPt.x, screenPt.y + bandHeightPx);
							::PostMessageW(ov, WM_MOUSEMOVE, 0, finalMoveLp);
							PumpFor(60);
						}
						LPARAM upLp = finalMoveLp;
						::PostMessageW(ov, WM_LBUTTONUP, 0, upLp);
						PumpFor(800);

						std::string afterJson = ReadGanttFromSlide(app);
						PpDocument docAfter = DocumentFromJson(afterJson);
						std::string newRowId;
						bool foundAfter = false;
						for (const auto& t : docAfter.tasks) {
							if (t.id == taskIdUtf8) { newRowId = t.rowId; foundAfter = true; break; }
						}

						std::string targetRowIdUtf8 = WNarrowFn(targetRowId.c_str());
						bool rowChanged = foundAfter && newRowId == targetRowIdUtf8;
						if (!rowChanged) {
							wprintf(L"DRAGROW: expected rowId '%hs', got '%hs' (orig '%hs')\n",
								targetRowIdUtf8.c_str(), newRowId.c_str(), origRowId.c_str());
						}
						pass = rowChanged;
					}
				} catch (const _com_error& e) {
					wprintf(L"DRAGROW: COM error 0x%08lX\n", (unsigned long)e.Error());
				}
				wprintf(pass ? L"DRAGROW PASS\n" : L"DRAGROW FAIL\n");
				if (!pass) rc = 1;
			}

			// ---- stage 7: CREATE ------------------------------------------------
			// Find an empty cell: a row with no task under it far to the right
			// of the chart (r_launch in the sample doc has only a milestone, no
			// tasks), posted-drag ~10 days to the right from a point inside that
			// row's band, then re-read PP_DOC: task count +1, new task's row
			// matches, and its span is ~10 days (+-1 day tolerance).
			if (rc == 0) RequireForeground(ppHwnd, &rc);
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

					std::string docJsonPre = ReadGanttFromSlide(app);
					PpDocument docPre = DocumentFromJson(docJsonPre);
					size_t taskCountBefore = docPre.tasks.size();

					// r_launch has no tasks in MakeSampleDocument (only a
					// milestone) — an empty row to anchor the create-drag in.
					const std::string emptyRowId = "r_launch";
					bool rowHasTask = false;
					for (const auto& t : docPre.tasks) if (t.rowId == emptyRowId) rowHasTask = true;

					// Find that row's ROW_LABEL rect (for its band's screen y)
					// and the chart's screen rect (for an x far right of any
					// task, inside the chart).
					PowerPoint::ShapePtr rowLabelShape;
					RECT chartScreenRect = {};
					if (chartRoot) {
						PowerPoint::GroupShapesPtr grp = chartRoot->GetGroupItems();
						long gn = grp->GetCount();
						float crLeft = chartRoot->GetLeft(), crTop = chartRoot->GetTop();
						float crW = chartRoot->GetWidth(), crH = chartRoot->GetHeight();
						PowerPoint::DocumentWindowPtr winForRect = app->GetActiveWindow();
						chartScreenRect.left = winForRect->PointsToScreenPixelsX(crLeft);
						chartScreenRect.top = winForRect->PointsToScreenPixelsY(crTop);
						chartScreenRect.right = winForRect->PointsToScreenPixelsX(crLeft + crW);
						chartScreenRect.bottom = winForRect->PointsToScreenPixelsY(crTop + crH);
						for (long j = 1; j <= gn; ++j) {
							PowerPoint::ShapePtr child = grp->Item(_variant_t(j));
							_bstr_t ck = child->GetTags()->Item(_bstr_t(L"PP_KIND"));
							std::wstring ckind = ck.length() ? (const wchar_t*)ck : L"";
							if (ckind == L"ROW_LABEL") {
								_bstr_t rid = child->GetTags()->Item(_bstr_t(L"PP_ID"));
								std::string ridUtf8 = WNarrowFn(rid.length() ? (const wchar_t*)rid : L"");
								if (ridUtf8 == emptyRowId) { rowLabelShape = child; break; }
							}
						}
					}

					HWND ov = OverlayHwnd();
					if (!chartRoot || rowHasTask || !rowLabelShape || !ov) {
						wprintf(L"CREATE: preconditions not met (rowHasTask=%d, rowLabelShape=%d, ov=%d)\n",
							(int)rowHasTask, rowLabelShape ? 1 : 0, ov ? 1 : 0);
					} else {
						PowerPoint::DocumentWindowPtr win = app->GetActiveWindow();
						float labelTop = rowLabelShape->GetTop();
						float labelH = rowLabelShape->GetHeight();
						int bandTopScreen = win->PointsToScreenPixelsY(labelTop);
						int bandBottomScreen = win->PointsToScreenPixelsY(labelTop + labelH);
						int bandCyScreen = (bandTopScreen + bandBottomScreen) / 2;

						// Anchor x: well inside the chart's timeline area, right
						// of the label column but left enough that a +10-day
						// rightward drag stays inside the chart rect.
						int anchorXScreen = chartScreenRect.left + (int)((chartScreenRect.right - chartScreenRect.left) * 0.35);

						POINT screenPt = { anchorXScreen, bandCyScreen };
						POINT clientPt = screenPt;
						::ScreenToClient(ov, &clientPt);

						// px-per-day: derive from any TASK rect's width/day-span,
						// same as the add-in's ComputeDragPxPerDay fallback path
						// (documented choice: reference an existing task rect
						// rather than PP_PROJ, since PP_PROJ's ptPerDay is in
						// POINTS and would need a second zoom-factor lookup).
						PowerPoint::ShapePtr refTask;
						if (chartRoot) {
							PowerPoint::GroupShapesPtr grp2 = chartRoot->GetGroupItems();
							long gn2 = grp2->GetCount();
							for (long j = 1; j <= gn2 && !refTask; ++j) {
								PowerPoint::ShapePtr child = grp2->Item(_variant_t(j));
								_bstr_t ck = child->GetTags()->Item(_bstr_t(L"PP_KIND"));
								std::wstring ckind = ck.length() ? (const wchar_t*)ck : L"";
								if (ckind == L"TASK") refTask = child;
							}
						}
						double pxPerDay = 0.0;
						std::string refTaskId;
						if (refTask) {
							_bstr_t rid = refTask->GetTags()->Item(_bstr_t(L"PP_ID"));
							refTaskId = WNarrowFn(rid.length() ? (const wchar_t*)rid : L"");
						}
						for (const auto& t : docPre.tasks) {
							if (t.id == refTaskId) {
								long span = DateToDays(t.end) - DateToDays(t.start) + 1;
								if (span > 0 && refTask) {
									int rL = win->PointsToScreenPixelsX(refTask->GetLeft());
									int rR = win->PointsToScreenPixelsX(refTask->GetLeft() + refTask->GetWidth());
									if (rR > rL) pxPerDay = (double)(rR - rL) / (double)span;
								}
								break;
							}
						}

						if (pxPerDay <= 0.0) {
							wprintf(L"CREATE: could not derive px-per-day from a reference task\n");
						} else {
							const int kCreateDays = 10;
							long shiftPx = (long)::lround(kCreateDays * pxPerDay);

							// See the DRAG stage's SetCursorPos comment.
							::SetCursorPos(screenPt.x, screenPt.y);

							::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM((short)clientPt.x, (short)clientPt.y));
							PumpFor(60);
							const int kSteps = 5;
							for (int s = 1; s <= kSteps; ++s) {
								int mx = clientPt.x + (int)(shiftPx * s / kSteps);
								::SetCursorPos(screenPt.x + (mx - clientPt.x), screenPt.y);
								::PostMessageW(ov, WM_MOUSEMOVE, 0, MAKELPARAM((short)mx, (short)clientPt.y));
								PumpFor(60);
							}
							int finalX = clientPt.x + (int)shiftPx;
							::SetCursorPos(screenPt.x + (int)shiftPx, screenPt.y);
							::PostMessageW(ov, WM_LBUTTONUP, 0, MAKELPARAM((short)finalX, (short)clientPt.y));
							PumpFor(800);

							std::string docJsonAfter = ReadGanttFromSlide(app);
							PpDocument docAfter = DocumentFromJson(docJsonAfter);
							size_t taskCountAfter = docAfter.tasks.size();

							bool countOk = (taskCountAfter == taskCountBefore + 1);
							bool rowOk = false;
							bool spanOk = false;
							if (countOk) {
								// The new task is whichever one wasn't present
								// before (ids are assigned sequentially by
								// NextId, so a set-difference by id is exact).
								for (const auto& t : docAfter.tasks) {
									bool wasPresent = false;
									for (const auto& tb : docPre.tasks) if (tb.id == t.id) { wasPresent = true; break; }
									if (!wasPresent) {
										rowOk = (t.rowId == emptyRowId);
										long spanDays = DateToDays(t.end) - DateToDays(t.start) + 1;
										spanOk = (spanDays >= kCreateDays - 1 && spanDays <= kCreateDays + 1);
										if (!rowOk) wprintf(L"CREATE: new task rowId '%hs' != expected '%hs'\n", t.rowId.c_str(), emptyRowId.c_str());
										if (!spanOk) wprintf(L"CREATE: new task span %ld days, expected ~%d\n", spanDays, kCreateDays);
										break;
									}
								}
							} else {
								wprintf(L"CREATE: task count %zu -> %zu, expected +1\n", taskCountBefore, taskCountAfter);
							}
							pass = countOk && rowOk && spanOk;
						}
					}
				} catch (const _com_error& e) {
					wprintf(L"CREATE: COM error 0x%08lX\n", (unsigned long)e.Error());
				}
				wprintf(pass ? L"CREATE PASS\n" : L"CREATE FAIL\n");
				if (!pass) rc = 1;
			}

			// ---- stage 8: INPLACE ---------------------------------------------
			// Rebuild-in-place: a pure move/resize gesture (a body drag, same
			// mechanics as stage 5/DRAG but on a DIFFERENT task — "t2" — so it
			// doesn't interact with stage 5's already-shifted "t1"-ish task)
			// must NOT delete/recreate the CHART_ROOT group. Record the group's
			// COM Shape Id + child count before the drag; after it, both must be
			// UNCHANGED (same COM identity => RebuildChart's UpdateGantt path
			// reconciled in place rather than falling back to delete+InsertGantt),
			// while the dragged bar's Left and PP_DOC dates must have actually
			// moved (the gesture really committed).
			if (rc == 0) RequireForeground(ppHwnd, &rc);
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

					if (!chartRoot) {
						wprintf(L"INPLACE: could not find CHART_ROOT\n");
					} else {
						int rootIdBefore = chartRoot->GetId();
						long childCountBefore = chartRoot->GetGroupItems()->GetCount();

						PowerPoint::ShapePtr taskChild;
						const std::wstring kTargetTaskId = L"t2";
						{
							PowerPoint::GroupShapesPtr grp = chartRoot->GetGroupItems();
							long gn = grp->GetCount();
							for (long j = 1; j <= gn && !taskChild; ++j) {
								PowerPoint::ShapePtr child = grp->Item(_variant_t(j));
								_bstr_t ck = child->GetTags()->Item(_bstr_t(L"PP_KIND"));
								std::wstring ckind = ck.length() ? (const wchar_t*)ck : L"";
								if (ckind != L"TASK") continue;
								_bstr_t cid = child->GetTags()->Item(_bstr_t(L"PP_ID"));
								std::wstring cidW = cid.length() ? (const wchar_t*)cid : L"";
								if (cidW == kTargetTaskId) taskChild = child;
							}
						}

						HWND ov = OverlayHwnd();
						if (!taskChild || !ov) {
							wprintf(L"INPLACE: could not find TASK '%s' or overlay hwnd\n", kTargetTaskId.c_str());
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

							// See the DRAG stage's SetCursorPos comment.
							::SetCursorPos(screenPt.x, screenPt.y);

							LPARAM clickLp = MAKELPARAM((short)clientPt.x, (short)clientPt.y);
							::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, clickLp);
							::PostMessageW(ov, WM_LBUTTONUP, 0, clickLp);
							PumpFor(400);

							std::string docJsonBefore = ReadGanttFromSlide(app);
							PpDocument docBefore = DocumentFromJson(docJsonBefore);
							std::string taskIdUtf8 = WNarrowFn(kTargetTaskId.c_str());
							std::string origStart, origEnd;
							for (const auto& t : docBefore.tasks) {
								if (t.id == taskIdUtf8) { origStart = t.start; origEnd = t.end; break; }
							}

							std::string projJson = WNarrowFn((const wchar_t*)chartRoot->GetTags()->Item(_bstr_t(L"PP_PROJ")));
							long minDay = 0, pad = 0; float ptPerDay = 1.0f, originX = 0.0f;
							::sscanf_s(projJson.c_str(), "{\"minDay\":%ld,\"pad\":%ld,\"ptPerDay\":%f,\"originX\":%f}",
								&minDay, &pad, &ptPerDay, &originX);

							if (origStart.empty() || ptPerDay <= 0.0f) {
								wprintf(L"INPLACE: could not resolve task dates or ptPerDay before drag\n");
							} else {
								const long kDragDays = 5;
								int screenLeft = win->PointsToScreenPixelsX(left);
								int screenRight = win->PointsToScreenPixelsX(left + w);
								double pxPerPt = (screenRight > screenLeft) ? (double)(screenRight - screenLeft) / (double)w : 1.0;
								long shiftPx = (long)::lround(kDragDays * ptPerDay * pxPerPt);

								::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, clickLp);
								PumpFor(60);
								const int kSteps = 5;
								for (int s = 1; s <= kSteps; ++s) {
									int mx = clientPt.x + (int)(shiftPx * s / kSteps);
									::PostMessageW(ov, WM_MOUSEMOVE, 0, MAKELPARAM((short)mx, (short)clientPt.y));
									PumpFor(60);
								}
								int finalX = clientPt.x + (int)shiftPx;
								::PostMessageW(ov, WM_LBUTTONUP, 0, MAKELPARAM((short)finalX, (short)clientPt.y));
								PumpFor(800);

								// Re-find CHART_ROOT by tag (NOT the cached shape
								// pointer — if UpdateGantt fell back to delete+
								// recreate, the old COM pointer would be stale).
								PowerPoint::ShapePtr chartRootAfter;
								long n2 = shapes->GetCount();
								for (long i = 1; i <= n2 && !chartRootAfter; ++i) {
									PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
									_bstr_t k2 = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
									std::wstring kind2 = k2.length() ? (const wchar_t*)k2 : L"";
									if (kind2 == L"CHART_ROOT") chartRootAfter = sh;
								}

								bool sameShapeId = false;
								bool sameChildCount = false;
								int rootIdAfter = -1;
								long childCountAfter = -1;
								float leftAfter = 0.0f;
								if (chartRootAfter) {
									rootIdAfter = chartRootAfter->GetId();
									childCountAfter = chartRootAfter->GetGroupItems()->GetCount();
									sameShapeId = (rootIdAfter == rootIdBefore);
									sameChildCount = (childCountAfter == childCountBefore);

									PowerPoint::GroupShapesPtr grpAfter = chartRootAfter->GetGroupItems();
									long gnAfter = grpAfter->GetCount();
									for (long j = 1; j <= gnAfter; ++j) {
										PowerPoint::ShapePtr child = grpAfter->Item(_variant_t(j));
										_bstr_t ck = child->GetTags()->Item(_bstr_t(L"PP_KIND"));
										std::wstring ckind = ck.length() ? (const wchar_t*)ck : L"";
										if (ckind != L"TASK") continue;
										_bstr_t cid = child->GetTags()->Item(_bstr_t(L"PP_ID"));
										std::wstring cidW = cid.length() ? (const wchar_t*)cid : L"";
										if (cidW == kTargetTaskId) { leftAfter = child->GetLeft(); break; }
									}
								}

								std::string afterJson = ReadGanttFromSlide(app);
								PpDocument docAfter = DocumentFromJson(afterJson);
								std::string newStart, newEnd;
								bool foundAfter = false;
								for (const auto& t : docAfter.tasks) {
									if (t.id == taskIdUtf8) { newStart = t.start; newEnd = t.end; foundAfter = true; break; }
								}
								std::string expectStart = DaysToDate(DateToDays(origStart) + kDragDays);
								std::string expectEnd = DaysToDate(DateToDays(origEnd) + kDragDays);
								bool datesShifted = foundAfter && newStart == expectStart && newEnd == expectEnd;
								bool leftMoved = std::fabs(leftAfter - left) > 0.5f;

								if (!chartRootAfter) {
									wprintf(L"INPLACE: CHART_ROOT missing after drag\n");
								}
								if (!sameShapeId) {
									wprintf(L"INPLACE: chart root Shape Id changed %d -> %d (delete/recreate happened)\n", rootIdBefore, rootIdAfter);
								}
								if (!sameChildCount) {
									wprintf(L"INPLACE: child count changed %ld -> %ld\n", childCountBefore, childCountAfter);
								}
								if (!leftMoved) {
									wprintf(L"INPLACE: dragged bar's Left did not change (before=%.2f after=%.2f)\n", left, leftAfter);
								}
								if (!datesShifted) {
									wprintf(L"INPLACE: expected %hs..%hs, got %hs..%hs\n",
										expectStart.c_str(), expectEnd.c_str(), newStart.c_str(), newEnd.c_str());
								}
								pass = chartRootAfter && sameShapeId && sameChildCount && leftMoved && datesShifted;
							}
						}
					}
				} catch (const _com_error& e) {
					wprintf(L"INPLACE: COM error 0x%08lX\n", (unsigned long)e.Error());
				}
				wprintf(pass ? L"INPLACE PASS\n" : L"INPLACE FAIL\n");
				if (!pass) rc = 1;
			}

			// ---- stage 9: KEYS -------------------------------------------------
			// Keyboard hotkey handler correctness. Per the task brief: "Posting
			// WM_HOTKEY bypasses the RegisterHotKey OS layer — acceptable: the
			// probe already proved delivery (keys-probe.txt OPTION C); the stage
			// proves HANDLER correctness." So this stage does NOT rely on
			// RegisterHotKey/actual key presses at all — it re-selects a known
			// still-existing task via a posted click (INPLACE's pattern: the
			// document has been mutated by stages 3-8, so the task must be
			// re-found by id at its LIVE rect, not assumed to be where
			// MakeSampleDocument first put it), then posts WM_HOTKEY straight to
			// the overlay hwnd with the SAME wParam ids Overlay.cpp's own
			// RegisterHotKey calls would use (Overlay.h's
			// OverlayHotkeyIdForTest), simulating Right-arrow then Delete.
			//
			// "t4" is used because it is untouched by every earlier stage:
			// OWNSEL/DRAG/DRAGROW pick "the first TASK child found" (t1),
			// INPLACE explicitly targets "t2", and CREATE only ADDS a task in
			// r_launch — none of them touch t4's row/dates, so its PP_DOC
			// dates are still exactly MakeSampleDocument's ("2026-06-22" /
			// "2026-07-10") going into this stage, making the +1 day
			// assertion unambiguous.
			if (rc == 0) RequireForeground(ppHwnd, &rc);
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

					const std::wstring kTargetTaskId = L"t4";
					PowerPoint::ShapePtr taskChild;
					if (chartRoot) {
						PowerPoint::GroupShapesPtr grp = chartRoot->GetGroupItems();
						long gn = grp->GetCount();
						for (long j = 1; j <= gn && !taskChild; ++j) {
							PowerPoint::ShapePtr child = grp->Item(_variant_t(j));
							_bstr_t ck = child->GetTags()->Item(_bstr_t(L"PP_KIND"));
							std::wstring ckind = ck.length() ? (const wchar_t*)ck : L"";
							if (ckind != L"TASK") continue;
							_bstr_t cid = child->GetTags()->Item(_bstr_t(L"PP_ID"));
							std::wstring cidW = cid.length() ? (const wchar_t*)cid : L"";
							if (cidW == kTargetTaskId) taskChild = child;
						}
					}

					HWND ov = OverlayHwnd();
					if (!chartRoot || !taskChild || !ov) {
						wprintf(L"KEYS: could not find TASK '%s' or overlay hwnd\n", kTargetTaskId.c_str());
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
						::SetCursorPos(screenPt.x, screenPt.y);

						// Re-select explicitly (INPLACE pattern): a plain click
						// gesture posted at the LIVE rect center, same route as
						// OWNSEL/INPLACE.
						LPARAM clickLp = MAKELPARAM((short)clientPt.x, (short)clientPt.y);
						::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, clickLp);
						::PostMessageW(ov, WM_LBUTTONUP, 0, clickLp);
						PumpFor(400);

						const char* gotIdUtf8 = Overlay_GetSelectedIdForTest();
						std::string taskIdUtf8 = WNarrowFn(kTargetTaskId.c_str());
						bool selectedOk = gotIdUtf8 && taskIdUtf8 == gotIdUtf8;
						if (!selectedOk) {
							wprintf(L"KEYS: re-select click did not select '%s' (got '%hs')\n",
								kTargetTaskId.c_str(), gotIdUtf8 ? gotIdUtf8 : "(null)");
						} else {
							std::string docJsonBefore = ReadGanttFromSlide(app);
							PpDocument docBefore = DocumentFromJson(docJsonBefore);
							std::string origStart, origEnd;
							bool foundBefore = false;
							for (const auto& t : docBefore.tasks) {
								if (t.id == taskIdUtf8) { origStart = t.start; origEnd = t.end; foundBefore = true; break; }
							}

							if (!foundBefore) {
								wprintf(L"KEYS: task '%s' missing from PP_DOC before Right-arrow\n", kTargetTaskId.c_str());
							} else {
								// Simulate Right-arrow: post WM_HOTKEY with the
								// SAME id Overlay.cpp's RegisterHotKey(HOTKEY_
								// RIGHT,...) call would receive from Windows.
								::PostMessageW(ov, WM_HOTKEY, (WPARAM)OVERLAY_HOTKEY_RIGHT_FOR_TEST, 0);
								PumpFor(500);

								std::string docJsonAfterRight = ReadGanttFromSlide(app);
								PpDocument docAfterRight = DocumentFromJson(docJsonAfterRight);
								std::string newStart, newEnd;
								bool foundAfterRight = false;
								for (const auto& t : docAfterRight.tasks) {
									if (t.id == taskIdUtf8) { newStart = t.start; newEnd = t.end; foundAfterRight = true; break; }
								}
								std::string expectStart = DaysToDate(DateToDays(origStart) + 1);
								std::string expectEnd = DaysToDate(DateToDays(origEnd) + 1);
								bool datesShifted = foundAfterRight && newStart == expectStart && newEnd == expectEnd;

								const char* gotIdAfterRight = Overlay_GetSelectedIdForTest();
								bool selectionRetained = gotIdAfterRight && taskIdUtf8 == gotIdAfterRight;

								if (!datesShifted) {
									wprintf(L"KEYS: Right-arrow expected %hs..%hs, got %hs..%hs (found=%d)\n",
										expectStart.c_str(), expectEnd.c_str(), newStart.c_str(), newEnd.c_str(), (int)foundAfterRight);
								}
								if (!selectionRetained) {
									wprintf(L"KEYS: selection not retained after Right-arrow (got '%hs')\n",
										gotIdAfterRight ? gotIdAfterRight : "(null)");
								}

								bool rightOk = datesShifted && selectionRetained;
								bool deleteOk = false;
								if (rightOk) {
									// Simulate Delete: same posted-WM_HOTKEY route.
									::PostMessageW(ov, WM_HOTKEY, (WPARAM)OVERLAY_HOTKEY_DELETE_FOR_TEST, 0);
									PumpFor(500);

									std::string docJsonAfterDelete = ReadGanttFromSlide(app);
									PpDocument docAfterDelete = DocumentFromJson(docJsonAfterDelete);
									bool stillPresent = false;
									for (const auto& t : docAfterDelete.tasks) {
										if (t.id == taskIdUtf8) { stillPresent = true; break; }
									}
									deleteOk = !stillPresent;
									if (!deleteOk) {
										wprintf(L"KEYS: task '%s' still present in PP_DOC after Delete hotkey\n", kTargetTaskId.c_str());
									}
								}

								pass = rightOk && deleteOk;
							}
						}
					}
				} catch (const _com_error& e) {
					wprintf(L"KEYS: COM error 0x%08lX\n", (unsigned long)e.Error());
				}
				wprintf(pass ? L"KEYS PASS\n" : L"KEYS FAIL\n");
				if (!pass) rc = 1;
			}

			// ---- stage 10: EDITOR ---------------------------------------------
			// Floating card editor. By this point stages 3-9 have mutated the
			// document (KEYS deleted "t4" entirely) so, per the task brief,
			// this stage does NOT assume a specific still-existing task id —
			// it re-reads PP_DOC fresh and picks the first TASK it finds
			// there, then resolves that id's LIVE on-screen rect via the
			// CHART_ROOT group walk (same pattern INPLACE/KEYS use) rather
			// than any cached rect from an earlier stage.
			//
			// Flow: post a real double-click (DOWN+UP twice, matching
			// Windows' own WM_LBUTTONDBLCLK generation — CS_DBLCLKS is set on
			// the overlay's window class) at the task's live bar center;
			// assert the card window exists (FindWindowW by class name, see
			// Overlay.h's PP_CARD_EDITOR_CLASS); SetWindowTextW the start-
			// date child (GetDlgItem by OVERLAY_CARD_ID_START_FOR_TEST) to a
			// known different valid date; post Enter to that field (the
			// task brief also allows the OK button; the field's own
			// VK_RETURN handler commits identically and needs no extra
			// coordinate math); pump; re-read PP_DOC and assert the start
			// date changed accordingly, the end date shifted so the span is
			// preserved (SetTaskDates predates this stage and always sets
			// BOTH dates — the card only edits Start here, so it must submit
			// the unchanged-but-still-required End field verbatim), and the
			// overlay's own-selection model retained the task as selected.
			if (rc == 0) RequireForeground(ppHwnd, &rc);
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

					std::string docJsonPre = ReadGanttFromSlide(app);
					PpDocument docPre = DocumentFromJson(docJsonPre);
					if (docPre.tasks.empty()) {
						wprintf(L"EDITOR: PP_DOC has no tasks left to target\n");
					} else {
						std::string targetTaskId = docPre.tasks.front().id;
						std::string origStart, origEnd;
						for (const auto& t : docPre.tasks) {
							if (t.id == targetTaskId) { origStart = t.start; origEnd = t.end; break; }
						}
						std::wstring targetTaskIdW(targetTaskId.begin(), targetTaskId.end());

						PowerPoint::ShapePtr taskChild;
						if (chartRoot) {
							PowerPoint::GroupShapesPtr grp = chartRoot->GetGroupItems();
							long gn = grp->GetCount();
							for (long j = 1; j <= gn && !taskChild; ++j) {
								PowerPoint::ShapePtr child = grp->Item(_variant_t(j));
								_bstr_t ck = child->GetTags()->Item(_bstr_t(L"PP_KIND"));
								std::wstring ckind = ck.length() ? (const wchar_t*)ck : L"";
								if (ckind != L"TASK") continue;
								_bstr_t cid = child->GetTags()->Item(_bstr_t(L"PP_ID"));
								std::wstring cidW = cid.length() ? (const wchar_t*)cid : L"";
								if (cidW == targetTaskIdW) taskChild = child;
							}
						}

						HWND ov = OverlayHwnd();
						if (!chartRoot || !taskChild || !ov) {
							wprintf(L"EDITOR: could not find live TASK '%hs' or overlay hwnd\n", targetTaskId.c_str());
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
							::SetCursorPos(screenPt.x, screenPt.y);

							LPARAM clickLp = MAKELPARAM((short)clientPt.x, (short)clientPt.y);
							// A real double-click is DOWN,UP,DOWN,UP with the
							// second DOWN carrying WM_LBUTTONDBLCLK instead of
							// a plain WM_LBUTTONDOWN (matches CS_DBLCLKS
							// window-class semantics, which the overlay uses).
							::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, clickLp);
							::PostMessageW(ov, WM_LBUTTONUP, 0, clickLp);
							PumpFor(60);
							::PostMessageW(ov, WM_LBUTTONDBLCLK, MK_LBUTTON, clickLp);
							::PostMessageW(ov, WM_LBUTTONUP, 0, clickLp);
							PumpFor(500);

							HWND card = ::FindWindowW(PP_CARD_EDITOR_CLASS, NULL);
							if (!card || !::IsWindow(card)) {
								wprintf(L"EDITOR: card editor window not found after double-click\n");
							} else {
								HWND startField = ::GetDlgItem(card, OVERLAY_CARD_ID_START_FOR_TEST);
								HWND endField = ::GetDlgItem(card, OVERLAY_CARD_ID_END_FOR_TEST);
								if (!startField || !endField) {
									wprintf(L"EDITOR: could not resolve start/end date child controls\n");
								} else {
									std::string newStartStr = DaysToDate(DateToDays(origStart) + 3);
									std::wstring newStartW(newStartStr.begin(), newStartStr.end());
									std::wstring origEndW(origEnd.begin(), origEnd.end());
									// End must stay a VALID date >= the new
									// start (the card commits both fields
									// together): origEnd is already >= origStart,
									// and origStart+3 <= origEnd is guaranteed
									// by MakeSampleDocument's >= 1-week spans,
									// so resubmitting origEnd verbatim keeps
									// the commit valid without recomputing it.
									::SetWindowTextW(startField, newStartW.c_str());
									::SetWindowTextW(endField, origEndW.c_str());
									PumpFor(150);

									// Post Enter to the start field: CardFieldProc's
									// WM_KEYDOWN/VK_RETURN handler commits the
									// WHOLE card, matching the task brief's
									// "WM_KEYDOWN VK_RETURN to the field" option.
									::PostMessageW(startField, WM_KEYDOWN, VK_RETURN, 0);
									::PostMessageW(startField, WM_KEYUP, VK_RETURN, 0);
									PumpFor(600);

									bool cardClosed = !::IsWindow(card);
									std::string docJsonAfter = ReadGanttFromSlide(app);
									PpDocument docAfter = DocumentFromJson(docJsonAfter);
									std::string newStart, newEnd;
									bool foundAfter = false;
									for (const auto& t : docAfter.tasks) {
										if (t.id == targetTaskId) { newStart = t.start; newEnd = t.end; foundAfter = true; break; }
									}

									bool startChanged = foundAfter && newStart == newStartStr;
									bool endUnchanged = foundAfter && newEnd == origEnd;

									const char* gotIdUtf8 = Overlay_GetSelectedIdForTest();
									bool selectionRetained = gotIdUtf8 && targetTaskId == gotIdUtf8;

									if (!cardClosed) {
										wprintf(L"EDITOR: card window still exists after commit\n");
									}
									if (!startChanged) {
										wprintf(L"EDITOR: expected start '%hs', got '%hs' (found=%d)\n",
											newStartStr.c_str(), newStart.c_str(), (int)foundAfter);
									}
									if (!endUnchanged) {
										wprintf(L"EDITOR: expected end unchanged '%hs', got '%hs'\n", origEnd.c_str(), newEnd.c_str());
									}
									if (!selectionRetained) {
										wprintf(L"EDITOR: selection not retained after commit (got '%hs')\n", gotIdUtf8 ? gotIdUtf8 : "(null)");
									}

									pass = cardClosed && startChanged && endUnchanged && selectionRetained;
								}
							}
						}
					}
				} catch (const _com_error& e) {
					wprintf(L"EDITOR: COM error 0x%08lX\n", (unsigned long)e.Error());
				}
				wprintf(pass ? L"EDITOR PASS\n" : L"EDITOR FAIL\n");
				if (!pass) rc = 1;
			}

			// ---- stage 11: SCOPE -----------------------------------------------
			// The overlay chrome must be scoped to the host: think-cell-style
			// add-ins only paint their floating chrome while their host app is
			// the foreground window (Overlay.cpp's IsHostActiveForOverlayChrome,
			// wired into Tick()). Verify by creating a plain top-level window,
			// stealing the foreground for it (same bounded-retry pattern as
			// everywhere else in this harness), and asserting the overlay hides;
			// then re-foreground PowerPoint and assert it reappears.
			if (rc == 0) RequireForeground(ppHwnd, &rc);
			if (rc == 0) {
				bool pass = false;
				HWND testWnd = NULL;
				try {
					HWND ov = OverlayHwnd();
					if (!ov) {
						wprintf(L"SCOPE: overlay hwnd missing\n");
					} else {
						// A minimal, plain, visible top-level window — anything
						// the OS will hand the foreground to. WS_EX_APPWINDOW so
						// it behaves like a normal app window (not a tool/owned
						// window PowerPoint might still be considered "active"
						// through).
						const wchar_t* kScopeTestClass = L"PPScopeTestWindow";
						WNDCLASSEXW wc = { sizeof(wc) };
						wc.lpfnWndProc = ::DefWindowProcW;
						wc.hInstance = ::GetModuleHandleW(NULL);
						wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
						wc.lpszClassName = kScopeTestClass;
						::RegisterClassExW(&wc);

						testWnd = ::CreateWindowExW(WS_EX_APPWINDOW, kScopeTestClass,
							L"PP Scope Test", WS_OVERLAPPEDWINDOW,
							100, 100, 300, 200, NULL, NULL, wc.hInstance, NULL);

						if (!testWnd) {
							wprintf(L"SCOPE: could not create test window\n");
						} else {
							::ShowWindow(testWnd, SW_SHOW);
							::UpdateWindow(testWnd);

							bool stole = EnsureForeground(testWnd);
							PumpFor(1000);

							if (!stole) {
								wprintf(L"FOREGROUND STEAL FAILED\n");
							} else {
								bool overlayHidden = !::IsWindowVisible(ov);
								HWND card = ::FindWindowW(PP_CARD_EDITOR_CLASS, NULL);
								bool cardHiddenOrGone = (!card || !::IsWindowVisible(card));

								if (!overlayHidden) {
									wprintf(L"SCOPE: overlay still visible while an unrelated app is foreground\n");
								}
								if (!cardHiddenOrGone) {
									wprintf(L"SCOPE: card editor still visible while an unrelated app is foreground\n");
								}

								// Re-foreground PowerPoint and confirm the overlay
								// comes back within a bounded pump.
								bool refocused = EnsureForeground(ppHwnd);
								PumpFor(1000);
								bool overlayVisibleAgain = refocused && ::IsWindowVisible(ov);

								if (!refocused) {
									wprintf(L"FOREGROUND STEAL FAILED\n");
								} else if (!overlayVisibleAgain) {
									wprintf(L"SCOPE: overlay did not reappear after re-foregrounding PowerPoint\n");
								}

								pass = overlayHidden && cardHiddenOrGone && refocused && overlayVisibleAgain;
							}
						}
					}
				} catch (const _com_error& e) {
					wprintf(L"SCOPE: COM error 0x%08lX\n", (unsigned long)e.Error());
				}
				if (testWnd) ::DestroyWindow(testWnd);
				wprintf(pass ? L"SCOPE PASS\n" : L"SCOPE FAIL\n");
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
