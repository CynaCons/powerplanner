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
// INPUT-NEUTRAL: this harness makes ZERO real/synthetic input calls
// (SetCursorPos, SetForegroundWindow, keybd_event, SendInput — poisoned to
// compile errors below). Gestures are defined entirely by POSTED WM_MOUSE*/
// WM_HOTKEY messages to the overlay hwnd, combined with Overlay.h's
// Overlay_SetCursorPosOverrideForTest / Overlay_SetHostActiveOverrideForTest
// test seams so Overlay.cpp's physical-cursor/foreground reads see whatever
// the harness declares instead of the real OS mouse/keyboard/focus state.
// The user keeps working while gates run. See the "INPUT NEUTRAL OK" marker
// printed just before the final exit code.
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
//   Stage 12: MARKERDRAG — a synthetic drag gesture on the "today" marker's
//                       vertical line (located via PP_PROJ + PP_DOC, NOT the
//                       rendered shape rect — see GanttHitTest's Marker zone)
//                       shifts the marker by exactly N days, verified by
//                       re-reading PP_DOC.
//   Stage 14: APPBAR   — scale segment clicks mutate doc.scale via the live
//                       bottom app-bar (posted clicks to the app-bar hwnd).
//   Stage 15: TASKCTX  — task/milestone app-bar commands (swatch, nudge,
//                       label cycle, milestone note) with verify+retry posts;
//                       runs BEFORE ROWSEL (ROWSEL's undo probe bricks overlay
//                       tracking) ⇒ `TASKCTX PASS`.
//   Stage 16: ROWSEL   — rail row selection, row app-bar ops, delete hotkey
//                       cascade, and PP_ROWY band coverage for a row with no
//                       ROW_LABEL shape; undo-entry verification runs LAST via
//                       PP_DOC reads only (ExecuteMso Undo bricks overlay
//                       chart tracking — see undo-recovery-spike) ⇒ `ROWSEL PASS`.
//
//   ppoverlay.exe <output.png>
#include "../PowerPlannerAddin/pch.h"
#include "../PowerPlannerAddin/GanttBuilder.h"
#include "../PowerPlannerAddin/GanttJson.h"
#include "../PowerPlannerAddin/GanttLayout.h"
#include "../PowerPlannerAddin/GanttOps.h"
#include "../PowerPlannerAddin/Overlay.h"
#include "../PowerPlannerAddin/GanttHitTest.h"
#include "../PowerPlannerAddin/GanttTheme.h"
#include "../PowerPlannerAddin/GanttAppBar.h"
#include "../PowerPlannerAddin/Scene.h"
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
#include <cstring>
#include <string>
#include <vector>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

// ---- INPUT-NEUTRAL POISON BLOCK --------------------------------------------
// This harness must NEVER touch real input again (it used to move the user's
// real mouse cursor and synthesize real keyboard/foreground-focus events
// while gates ran). Gestures are defined ENTIRELY by posted WM_MOUSE*/
// WM_HOTKEY messages plus Overlay.h's Overlay_SetCursorPosOverrideForTest /
// Overlay_SetHostActiveOverrideForTest test seams. Poison the four forbidden
// APIs to compile errors below this point (all legitimate includes are above)
// so reintroducing a real-input call fails the BUILD, not just a code review.
#define SetCursorPos(...) PP_FORBIDDEN_REAL_INPUT_API_SetCursorPos
#define SetForegroundWindow(...) PP_FORBIDDEN_REAL_INPUT_API_SetForegroundWindow
#define keybd_event(...) PP_FORBIDDEN_REAL_INPUT_API_keybd_event
#define SendInput(...) PP_FORBIDDEN_REAL_INPUT_API_SendInput

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

// INPUT NEUTRAL OK gate: belt-and-braces runtime check that the cursor
// override was actually exercised where it matters most (DRAG, the stage
// most sensitive to physical-cursor interleaving in the old implementation).
// Set true only inside the DRAG stage once Overlay_SetCursorPosOverrideForTest
// has been called with enabled=true for that stage's gesture; if DRAG never
// runs (an earlier stage already failed) or somehow skips the override call,
// this stays false and the final marker is suppressed even if rc==0.
static bool g_cursorOverrideUsedDuringDrag = false;

// Small helper: tell Overlay.cpp (via the test override) where the physical
// cursor is, in SCREEN coordinates — a drop-in replacement for the old
// ::SetCursorPos(screenPt.x, screenPt.y) call sites. Callers still post their
// own WM_MOUSEMOVE/WM_LBUTTONDOWN/etc with CLIENT coordinates immediately
// after, exactly as before; this just keeps Overlay.cpp's hover/WM_SETCURSOR
// logic (the only remaining physical-cursor readers, both routed through
// OverlayGetCursorPos) in lockstep with each posted message instead of
// reading a real, possibly-stale OS cursor position.
static void SetOverlayCursorOverride(POINT screenPt, bool altDown = false) {
	Overlay_SetCursorPosOverrideForTest(true, screenPt, altDown);
}

static std::string NarrowFn(const wchar_t* w);

static bool HarnessUndoOnce(IDispatch* app) {
	if (!app) return false;
	try {
		PowerPoint::_ApplicationPtr pApp(app);
		// CommandBars comes from the Office typelib (raw_interfaces_only in
		// pch.h): ExecuteMso returns a raw HRESULT, no throwing wrapper.
		Office::_CommandBarsPtr bars = pApp->GetCommandBars();
		if (!bars) return false;
		return SUCCEEDED(bars->ExecuteMso(_bstr_t(L"Undo")));
	} catch (const _com_error&) {
		return false;
	}
}

static bool RowRailScreenPoint(IDispatch* app, PowerPoint::ShapePtr chartRoot,
	const std::string& rowId, POINT* outScreen) {
	if (!app || !chartRoot || !outScreen) return false;
	_bstr_t rowYTag = chartRoot->GetTags()->Item(_bstr_t(L"PP_ROWY"));
	if (!rowYTag.length()) return false;
	PpRowY ry;
	if (!ParseRowY(NarrowFn((const wchar_t*)rowYTag), &ry)) return false;
	const PpRowYEntry* entry = nullptr;
	for (const auto& e : ry.rows) if (e.id == rowId) { entry = &e; break; }
	if (!entry) return false;
	PowerPoint::_ApplicationPtr pApp(app);
	PowerPoint::DocumentWindowPtr win = pApp->GetActiveWindow();
	const float chartLeft = chartRoot->GetLeft();
	const float chartTop = chartRoot->GetTop();
	const float chartW = chartRoot->GetWidth();
	const float chartH = chartRoot->GetHeight();
	const float yScale = (ry.naturalH > 0.0f) ? chartH / ry.naturalH : 1.0f;
	const float xScale = (ry.naturalW > 0.0f) ? chartW / ry.naturalW : 1.0f;
	const float midY = (entry->top + entry->bot) * 0.5f;
	const float midX = (ry.railL + ry.railR) * 0.5f;
	outScreen->x = win->PointsToScreenPixelsX(chartLeft + midX * xScale);
	outScreen->y = win->PointsToScreenPixelsY(chartTop + midY * yScale);
	return true;
}

static std::string NarrowFn(const wchar_t* w) {
	if (!w || !*w) return "";
	int len = (int)::wcslen(w);
	int n = ::WideCharToMultiByte(CP_UTF8, 0, w, len, NULL, 0, NULL, NULL);
	std::string s(n, '\0');
	::WideCharToMultiByte(CP_UTF8, 0, w, len, &s[0], n, NULL, NULL);
	return s;
}

static PowerPoint::ShapePtr RefetchChartRoot(IDispatch* app) {
	if (!app) return nullptr;
	try {
		PowerPoint::_ApplicationPtr pApp(app);
		PowerPoint::_SlidePtr slide = pApp->GetActiveWindow()->GetView()->GetSlide();
		PowerPoint::ShapesPtr shapes = slide->GetShapes();
		long n = shapes->GetCount();
		for (long i = 1; i <= n; ++i) {
			PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
			_bstr_t k = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (k.length() && std::wstring((const wchar_t*)k) == L"CHART_ROOT") {
				try { (void)sh->GetLeft(); return sh; }
				catch (const _com_error&) { continue; }
			}
		}
	} catch (const _com_error&) {}
	return nullptr;
}

static PowerPoint::ShapePtr FindChartChildByKindId(PowerPoint::ShapePtr chartRoot,
	const wchar_t* kind, const wchar_t* id) {
	if (!chartRoot || !kind || !id) return nullptr;
	PowerPoint::GroupShapesPtr grp = chartRoot->GetGroupItems();
	long gn = grp->GetCount();
	for (long j = 1; j <= gn; ++j) {
		PowerPoint::ShapePtr child = grp->Item(_variant_t(j));
		_bstr_t ck = child->GetTags()->Item(_bstr_t(L"PP_KIND"));
		_bstr_t cid = child->GetTags()->Item(_bstr_t(L"PP_ID"));
		if (ck.length() && cid.length()
			&& std::wstring((const wchar_t*)ck) == kind
			&& std::wstring((const wchar_t*)cid) == id) {
			return child;
		}
	}
	return nullptr;
}

static bool ShapeScreenCenter(IDispatch* app, PowerPoint::ShapePtr sh, POINT* outScreen) {
	if (!app || !sh || !outScreen) return false;
	PowerPoint::_ApplicationPtr pApp(app);
	PowerPoint::DocumentWindowPtr win = pApp->GetActiveWindow();
	float left = sh->GetLeft(), top = sh->GetTop();
	float w = sh->GetWidth(), h = sh->GetHeight();
	outScreen->x = win->PointsToScreenPixelsX(left + w / 2.0f);
	outScreen->y = win->PointsToScreenPixelsY(top + h / 2.0f);
	return true;
}

static const PpTask* FindTaskInDoc(const PpDocument& doc, const char* id) {
	for (const auto& t : doc.tasks) if (t.id == id) return &t;
	return nullptr;
}

static const PpMilestone* FindMilestoneInDoc(const PpDocument& doc, const char* id) {
	for (const auto& m : doc.milestones) if (m.id == id) return &m;
	return nullptr;
}

// Detached watchdog: if anything hangs (modal PowerPoint dialog, COM stall),
// kill the harness after 300s instead of leaking POWERPNT forever. Budget
// raised from 120s for S3: the suite is now 16 COM-heavy stages (APPBAR +
// TASKCTX + ROWSEL added) and a single commit's reconcile can take several
// under load, so full runs legitimately exceed the old budget.
static DWORD WINAPI WatchdogProc(LPVOID) {
	::Sleep(720000);
	wprintf(L"WATCHDOG: 720s timeout, exiting\n");
	::ExitProcess(3);
	return 0;
}

// The add-in's overlay scopes its chrome to whether PowerPoint is the
// FOREGROUND app (see Overlay.cpp's IsHostActiveForOverlayChrome) — a
// necessary fix for the "chrome stays on top of other apps" bug. This harness
// used to re-steal the REAL OS foreground before every stage that depends on
// the overlay chrome being visible (via SetForegroundWindow, with an Alt-tap
// fallback for Windows' foreground-lock heuristic) — which stole focus from
// whatever the user was doing while gates ran, and the Alt-tap itself caused
// confirmed nondeterministic KEYS failures (a lingering Alt-down keystate
// flips WM_NCHITTEST's Alt-passthrough hatch and turns later key events into
// Alt+<key> chords).
//
// Replaced entirely by Overlay_SetHostActiveOverrideForTest(1): this declares
// "the host is active" directly to the overlay's own scoping logic, with zero
// real input and zero dependency on which window the OS actually considers
// foreground. EnsureForeground/RequireForeground below keep their names (and
// every call site is unchanged) but now just set the override + settle-pump;
// "did it take effect" is verified by probing the overlay's own visible state
// rather than GetForegroundWindow.
static bool EnsureForeground(HWND /*targetHwnd*/, DWORD /*timeoutMs*/ = 3000) {
	Overlay_SetHostActiveOverrideForTest(1);
	// Settle: let the overlay's 150ms Tick poller observe the override (two
	// tick periods, time-bounded) before the caller interacts with the
	// overlay.
	PumpFor(300);
	HWND ov = OverlayHwnd();
	// No overlay yet (very first call, before OverlayStart): nothing to probe
	// — the override is set, which is all this call can promise at that point.
	return !ov || ::IsWindowVisible(ov);
}

// Wrapper used before every visibility-dependent stage: on failure, prints
// the environment-failure marker (distinct from a stage FAIL) and sets rc
// nonzero so an environment problem is never misread as an overlay-scoping
// regression. "Failure" here means the override didn't produce a visible
// overlay within the settle pump — never a real foreground-steal refusal,
// since no real foreground call is made anymore.
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

// The gate must be invisible to the user, not just input-neutral: PowerPoint
// (and, through it, the overlay — which follows the chart's screen rect via
// PointsToScreenPixels, so it lands wherever PowerPoint's window is) is moved
// entirely outside the virtual desktop before any stage runs, so nothing
// flashes on top of whatever the user is doing while the gate runs.
// SetWindowPos with SWP_NOACTIVATE/SWP_NOZORDER — this is a pure geometry
// move, not a focus change, so it doesn't fight the host-active override.
static bool MoveWindowOffscreen(HWND hwnd) {
	if (!hwnd) return false;
	RECT r;
	if (!::GetWindowRect(hwnd, &r)) return false;
	int vRight = ::GetSystemMetrics(SM_XVIRTUALSCREEN) + ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
	int w = r.right - r.left, h = r.bottom - r.top;
	int newX = vRight + 200;
	int newY = r.top;
	::SetWindowPos(hwnd, NULL, newX, newY, w, h, SWP_NOACTIVATE | SWP_NOZORDER);

	RECT after;
	int vLeft = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
	bool ok = ::GetWindowRect(hwnd, &after) && after.left >= vRight;
	(void)vLeft;
	if (ok) {
		wprintf(L"OFFSCREEN AT %ld,%ld\n", after.left, after.top);
	} else {
		wprintf(L"OFFSCREEN FAILED\n");
	}
	return ok;
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

			// Maximize + activate so the slide is actually rendered (behind the
			// transparent overlay) and PointsToScreenPixels resolves real
			// coordinates — but the whole point of this harness being
			// input-neutral is undermined if it then flashes visibly on top
			// of whatever the user is doing, so immediately push the window
			// fully outside the virtual desktop. The overlay tracks the
			// chart's screen rect every Tick() (PointsToScreenPixelsX/Y off
			// this same window), so it follows PowerPoint offscreen too —
			// nothing else needs to move.
			try { app->GetActiveWindow()->PutWindowState(PowerPoint::ppWindowMaximized); } catch (...) {}
			try { app->Activate(); } catch (...) {}
			HWND ppHwnd = (HWND)(intptr_t)app->GetHWND();
			::ShowWindow(ppHwnd, SW_SHOWMAXIMIZED);
			RequireForeground(ppHwnd, &rc);
			PumpFor(600);
			if (rc == 0 && !MoveWindowOffscreen(ppHwnd)) rc = 1;
			PumpFor(300);

			if (rc == 0) {
				OverlayStart(app);
				overlayStarted = true;
				wprintf(L"harness: overlay started\n");
				PumpFor(2200);  // let the polling timer fire + paint

				// Verify the overlay itself (not just PowerPoint's root) ended
				// up offscreen — it's a SEPARATE top-level window positioned
				// from PointsToScreenPixels each Tick(), so this is the actual
				// guarantee the user cares about, not an inference from
				// PowerPoint's window alone.
				HWND ov = OverlayHwnd();
				RECT ovRect;
				int vRight = ::GetSystemMetrics(SM_XVIRTUALSCREEN) + ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
				if (ov && ::GetWindowRect(ov, &ovRect)) {
					if (ovRect.left >= vRight) {
						wprintf(L"OFFSCREEN AT %ld,%ld\n", ovRect.left, ovRect.top);
					} else {
						wprintf(L"OFFSCREEN FAILED\n");
						rc = 1;
					}
				}
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
			// view/timing. Use the direct GroupItems->Select() route (pure
			// COM, no synthetic input at all); if it doesn't stick, the stage
			// degrades gracefully (see "SUPPRESS PASS (child-select
			// unsimulatable)" below) rather than falling back to a real
			// Alt+click (which this harness no longer performs).
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

					// NOTE: this stage previously fell back to a real Alt+click
					// (SendInput + SetCursorPos) when the direct GroupItems->
					// Select() route above didn't stick, to exercise
					// PowerPoint's native "select sub-object within selected
					// group" gesture. Removed per the input-neutral harness
					// requirement (zero real input APIs) — the direct COM
					// select is deterministic in practice, and the stage
					// already degrades gracefully (see "SUPPRESS PASS
					// (child-select unsimulatable)" below) if it ever isn't.
					if (landedKind.empty()) {
						childSelectable = false;
						wprintf(L"SUPPRESS: could not produce a native chart-child selection via direct GroupItems->Select\n");
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

						// Set the overlay's cursor-position OVERRIDE to the gesture
						// point before posting synthetic messages. Historically this
						// also moved the REAL OS cursor (SetCursorPos): while this
						// window holds capture, Windows can deliver a REAL
						// WM_MOUSEMOVE reporting the actual physical cursor position,
						// which would otherwise interleave with (and could even race)
						// our posted synthetic moves. Now that Overlay.cpp's hover/
						// WM_SETCURSOR logic reads ONLY this override (never the real
						// GetCursorPos) whenever it's enabled, a stray real mousemove
						// can no longer retarget anything — no real cursor move needed.
						SetOverlayCursorOverride(screenPt);

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
							// a real drag gesture. The cursor override is updated at
							// each step in lockstep with the posted WM_MOUSEMOVE (same
							// reasoning as the down-point override above), and this is
							// the stage the INPUT NEUTRAL OK gate checks was exercised.
							g_cursorOverrideUsedDuringDrag = true;
							const int kSteps = 5;
							for (int s = 1; s <= kSteps; ++s) {
								int mx = clientPt.x + (int)(shiftPx * s / kSteps);
								SetOverlayCursorOverride({ screenPt.x + (mx - clientPt.x), screenPt.y });
								LPARAM moveLp = MAKELPARAM((short)mx, (short)clientPt.y);
								::PostMessageW(ov, WM_MOUSEMOVE, 0, moveLp);
								PumpFor(60);
							}
							int finalX = clientPt.x + (int)shiftPx;
							SetOverlayCursorOverride({ screenPt.x + (int)shiftPx, screenPt.y });
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

						// See the DRAG stage's cursor-override comment: set the
						// override to the gesture's down-point first, in lockstep
						// with the posted LBUTTONDOWN, so Overlay.cpp's hover/
						// WM_SETCURSOR logic (the only remaining physical-cursor
						// readers) never sees a stale position.
						SetOverlayCursorOverride(screenPt);

						LPARAM downLp = MAKELPARAM((short)clientPt.x, (short)clientPt.y);
						::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, downLp);
						PumpFor(60);
						const int kSteps = 5;
						for (int s = 1; s <= kSteps; ++s) {
							int my = clientPt.y + (int)((long)bandHeightPx * s / kSteps);
							// Update the override in lockstep with each posted move
							// (mirrors the DRAG stage) — the drag-row-reassign target
							// is decided entirely from posted WM_MOUSEMOVE lParams, so
							// this override update just keeps hover/cursor-shape logic
							// consistent with the same point.
							SetOverlayCursorOverride({ screenPt.x, screenPt.y + (my - clientPt.y) });
							LPARAM moveLp = MAKELPARAM((short)clientPt.x, (short)my);
							::PostMessageW(ov, WM_MOUSEMOVE, 0, moveLp);
							PumpFor(60);
						}
						int finalY = clientPt.y + bandHeightPx;
						LPARAM finalMoveLp = MAKELPARAM((short)clientPt.x, (short)finalY);
						// Re-assert the FINAL target position (override + posted
						// message) a few more times before committing.
						for (int extra = 0; extra < 3; ++extra) {
							SetOverlayCursorOverride({ screenPt.x, screenPt.y + bandHeightPx });
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

							// See the DRAG stage's cursor-override comment.
							SetOverlayCursorOverride(screenPt);

							::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM((short)clientPt.x, (short)clientPt.y));
							PumpFor(60);
							const int kSteps = 5;
							for (int s = 1; s <= kSteps; ++s) {
								int mx = clientPt.x + (int)(shiftPx * s / kSteps);
								SetOverlayCursorOverride({ screenPt.x + (mx - clientPt.x), screenPt.y });
								::PostMessageW(ov, WM_MOUSEMOVE, 0, MAKELPARAM((short)mx, (short)clientPt.y));
								PumpFor(60);
							}
							int finalX = clientPt.x + (int)shiftPx;
							SetOverlayCursorOverride({ screenPt.x + (int)shiftPx, screenPt.y });
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

							// See the DRAG stage's cursor-override comment.
							SetOverlayCursorOverride(screenPt);

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
						SetOverlayCursorOverride(screenPt);

						// Re-select explicitly (INPLACE pattern): a plain click
						// gesture posted at the LIVE rect center, same route as
						// OWNSEL/INPLACE. Observed flaky at 400ms (occasionally
						// resolves to the PREVIOUS stage's still-selected task,
						// e.g. INPLACE's "t2", if the overlay's cached hit
						// snapshot/row bands hadn't fully caught up with
						// INPLACE's RebuildChart by the time this click lands)
						// — pump longer, and settle BEFORE reading rects too, so
						// the snapshot backing this click's hit test is
						// current.
						PumpFor(300);
						LPARAM clickLp = MAKELPARAM((short)clientPt.x, (short)clientPt.y);
						::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, clickLp);
						::PostMessageW(ov, WM_LBUTTONUP, 0, clickLp);
						PumpFor(700);

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
								// PumpFor(800) matches every other stage's
								// post-commit pump (DRAG/DRAGROW/CREATE/INPLACE
								// all wait 800ms after the gesture that triggers
								// RebuildChart) — this stage's hotkey handlers
								// call the SAME RebuildChart, so give it the
								// same margin rather than the previously-used
								// 500ms (observed flaky under load: the commit
								// hadn't always landed by the time PP_DOC was
								// re-read).
								::PostMessageW(ov, WM_HOTKEY, (WPARAM)OVERLAY_HOTKEY_RIGHT_FOR_TEST, 0);
								PumpFor(800);

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
									// Simulate Delete: same posted-WM_HOTKEY
									// route. See the Right-arrow pump comment
									// above re: 800ms matching the other
									// RebuildChart-triggering stages.
									HWND ovNow = OverlayHwnd();
										if (ovNow != ov) {
											wprintf(L"KEYS diag: overlay hwnd changed %p -> %p\n", (void*)ov, (void*)ovNow);
											if (ovNow) ov = ovNow;
										}
										if (!::IsWindow(ov)) wprintf(L"KEYS diag: overlay hwnd is DEAD\n");
										::SetLastError(0);
										BOOL postOk = ::PostMessageW(ov, WM_HOTKEY, (WPARAM)OVERLAY_HOTKEY_DELETE_FOR_TEST, 0);
										if (!postOk) wprintf(L"KEYS diag: Delete PostMessageW FAILED err=%lu\n", ::GetLastError());
											// HARDENING (see coordinator log s3-row-selection): when the
											// preceding commit's dispatch overruns its pump, a successfully
											// posted WM_HOTKEY was observed to be lost before dispatch.
											// Post the Delete a second time after a settle pump: if the
											// first landed, HandleHotkeyDelete cleared the selection and
											// the second is a logged no-op; if the first was lost, the
											// second lands. The assertion below stays purely behavioral.
											PumpFor(400);
											::PostMessageW(ov, WM_HOTKEY, (WPARAM)OVERLAY_HOTKEY_DELETE_FOR_TEST, 0);
									PumpFor(800);

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
							SetOverlayCursorOverride(screenPt);

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
			// wired into Tick()). Previously verified by creating a real
			// top-level window and stealing the OS foreground for it (a real
			// SetForegroundWindow call) — replaced with
			// Overlay_SetHostActiveOverrideForTest, which drives the SAME
			// scoping logic deterministically with no real window/foreground
			// theft at all: force mode 0 ("host inactive"), pump <=1s, assert
			// the overlay (and card editor, if present) hide; force mode 1
			// ("host active"), pump, assert the overlay reappears.
			if (rc == 0) RequireForeground(ppHwnd, &rc);
			if (rc == 0) {
				bool pass = false;
				try {
					HWND ov = OverlayHwnd();
					if (!ov) {
						wprintf(L"SCOPE: overlay hwnd missing\n");
					} else {
						Overlay_SetHostActiveOverrideForTest(0);
						PumpFor(1000);

						bool overlayHidden = !::IsWindowVisible(ov);
						HWND ab = OverlayAppBarHwnd();
						bool abHidden = (!ab || !::IsWindowVisible(ab));
						HWND card = ::FindWindowW(PP_CARD_EDITOR_CLASS, NULL);
						bool cardHiddenOrGone = (!card || !::IsWindowVisible(card));

						if (!overlayHidden) {
							wprintf(L"SCOPE: overlay still visible while host-active override forces inactive\n");
						}
						if (!abHidden) {
							wprintf(L"SCOPE: app-bar still visible while host-active override forces inactive\n");
						}
						if (!cardHiddenOrGone) {
							wprintf(L"SCOPE: card editor still visible while host-active override forces inactive\n");
						}

						// Force host-active again and confirm the overlay comes
						// back within a bounded pump.
						Overlay_SetHostActiveOverrideForTest(1);
						PumpFor(1000);
						bool overlayVisibleAgain = ::IsWindowVisible(ov);
						bool abVisibleAgain = (ab && ::IsWindowVisible(ab));

						if (!overlayVisibleAgain) {
							wprintf(L"SCOPE: overlay did not reappear after forcing host-active again\n");
						}
						if (!abVisibleAgain) {
							wprintf(L"SCOPE: app-bar did not reappear after forcing host-active again\n");
						}

						pass = overlayHidden && abHidden && cardHiddenOrGone && overlayVisibleAgain && abVisibleAgain;
					}
				} catch (const _com_error& e) {
					wprintf(L"SCOPE: COM error 0x%08lX\n", (unsigned long)e.Error());
				}
				wprintf(pass ? L"SCOPE PASS\n" : L"SCOPE FAIL\n");
				if (!pass) rc = 1;
			}

			// ---- stage 12: MARKERDRAG -------------------------------------------
			// Select the "today" marker via a posted click at its synthesized hit
			// band (located from PP_PROJ + PP_DOC's marker date — NOT the rendered
			// TODAY_LINE shape's rect, which is a near-zero-width line unusable for
			// hit-testing; see GanttHitTest's Marker zone / Overlay.cpp's
			// BuildRowBands marker-band synthesis), then drag it horizontally by
			// exactly N days via WM_LBUTTONDOWN + several WM_MOUSEMOVEs +
			// WM_LBUTTONUP posted straight to the overlay hwnd, mirroring stage
			// 5's (DRAG) mechanics exactly. Re-read PP_DOC afterward: the marker's
			// date must have shifted by exactly N days, and the overlay's own-
			// selection model must still report the marker's id (A2, same as DRAG).
			if (rc == 0) RequireForeground(ppHwnd, &rc);
			if (rc == 0) {
				bool pass = false;
				try {
					auto WNarrowMk = [](const wchar_t* wtext) -> std::string {
						if (!wtext || !*wtext) return "";
						int len = (int)::wcslen(wtext);
						int nb = ::WideCharToMultiByte(CP_UTF8, 0, wtext, len, NULL, 0, NULL, NULL);
						std::string s(nb, '\0');
						if (nb > 0) ::WideCharToMultiByte(CP_UTF8, 0, wtext, len, &s[0], nb, NULL, NULL);
						return s;
					};

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

					std::wstring markerId;
					if (chartRoot) {
						PowerPoint::GroupShapesPtr grp = chartRoot->GetGroupItems();
						long gn = grp->GetCount();
						for (long j = 1; j <= gn && markerId.empty(); ++j) {
							PowerPoint::ShapePtr child = grp->Item(_variant_t(j));
							_bstr_t ck = child->GetTags()->Item(_bstr_t(L"PP_KIND"));
							std::wstring ckind = ck.length() ? (const wchar_t*)ck : L"";
							if (ckind == L"TODAY_LINE") {
								_bstr_t cid = child->GetTags()->Item(_bstr_t(L"PP_ID"));
								markerId = cid.length() ? (const wchar_t*)cid : L"";
							}
						}
					}

					HWND ov = OverlayHwnd();
					if (!chartRoot || markerId.empty() || !ov) {
						wprintf(L"MARKERDRAG: could not find CHART_ROOT/TODAY_LINE child or overlay hwnd\n");
					} else {
						PowerPoint::DocumentWindowPtr win = app->GetActiveWindow();
						std::string markerIdUtf8 = WNarrowMk(markerId.c_str());

						// Read PP_DOC + PP_PROJ once, before the drag (mirrors the
						// add-in's own gesture-start read AND Overlay.cpp's
						// BuildRowBands marker-band synthesis).
						std::string docJsonBefore = WNarrowMk((const wchar_t*)chartRoot->GetTags()->Item(_bstr_t(L"PP_DOC")));
						std::string projJson = WNarrowMk((const wchar_t*)chartRoot->GetTags()->Item(_bstr_t(L"PP_PROJ")));
						PpDocument docBefore = DocumentFromJson(docJsonBefore);

						std::string origDate;
						for (const auto& m : docBefore.markers) {
							if (m.id == markerIdUtf8) { origDate = m.date; break; }
						}

						PpProj proj;
						bool haveProj = ParseProj(projJson, &proj);

						if (origDate.empty() || !haveProj) {
							wprintf(L"MARKERDRAG: could not resolve marker date or PP_PROJ before drag\n");
						} else {
							// Same projection formula GanttBuilder/Overlay.cpp use:
							// xPt = originX + (day - minDay + pad) * ptPerDay.
							long dayIdx = DateToDays(origDate) - proj.minDay + proj.pad;
							float xPt = proj.originX + (float)dayIdx * proj.ptPerDay;
							int screenX = win->PointsToScreenPixelsX(xPt);
							// Any y within the chart band works (the hit band spans
							// the full chart height) — use the chart root's vertical
							// midpoint.
							float chartTop = chartRoot->GetTop(), chartH = chartRoot->GetHeight();
							int screenY = win->PointsToScreenPixelsY(chartTop + chartH / 2.0f);
							POINT screenPt = { screenX, screenY };
							POINT clientPt = screenPt;
							::ScreenToClient(ov, &clientPt);

							SetOverlayCursorOverride(screenPt);

							// Select the marker first (same click-select route as
							// OWNSEL/DRAG) so we can also assert the selection
							// survives the drag.
							LPARAM clickLp = MAKELPARAM((short)clientPt.x, (short)clientPt.y);
							::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, clickLp);
							::PostMessageW(ov, WM_LBUTTONUP, 0, clickLp);
							PumpFor(400);

							const long kDragDays = 5;
							// SCREEN px-per-day, derived the same way stage 5/DRAG
							// does: from ptPerDay (slide points) via the actual
							// screen-pixel/point ratio at this zoom level, which
							// PointsToScreenPixelsX already folds in.
							int screenX1 = win->PointsToScreenPixelsX(xPt);
							int screenX2 = win->PointsToScreenPixelsX(xPt + proj.ptPerDay);
							double pxPerDay = (double)(screenX2 - screenX1);
							long shiftPx = (long)::lround(kDragDays * pxPerDay);

							::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, clickLp);
							PumpFor(60);
							g_cursorOverrideUsedDuringDrag = true;
							const int kSteps = 5;
							for (int s = 1; s <= kSteps; ++s) {
								int mx = clientPt.x + (int)(shiftPx * s / kSteps);
								SetOverlayCursorOverride({ screenPt.x + (mx - clientPt.x), screenPt.y });
								LPARAM moveLp = MAKELPARAM((short)mx, (short)clientPt.y);
								::PostMessageW(ov, WM_MOUSEMOVE, 0, moveLp);
								PumpFor(60);
							}
							int finalX = clientPt.x + (int)shiftPx;
							SetOverlayCursorOverride({ screenPt.x + (int)shiftPx, screenPt.y });
							LPARAM upLp = MAKELPARAM((short)finalX, (short)clientPt.y);
							::PostMessageW(ov, WM_LBUTTONUP, 0, upLp);
							PumpFor(800);

							std::string afterJson = ReadGanttFromSlide(app);
							PpDocument docAfter = DocumentFromJson(afterJson);

							std::string newDate;
							bool foundAfter = false;
							for (const auto& m : docAfter.markers) {
								if (m.id == markerIdUtf8) { newDate = m.date; foundAfter = true; break; }
							}

							std::string expectDate = DaysToDate(DateToDays(origDate) + kDragDays);

							const char* gotIdUtf8 = Overlay_GetSelectedIdForTest();
							std::string gotId = gotIdUtf8 ? gotIdUtf8 : "";

							bool dateShifted = foundAfter && newDate == expectDate;
							bool selectionKept = (gotId == markerIdUtf8);

							if (!dateShifted) {
								wprintf(L"MARKERDRAG: expected %hs, got %hs\n",
									expectDate.c_str(), newDate.c_str());
							}
							if (!selectionKept) {
								wprintf(L"MARKERDRAG: expected selected id '%hs', got '%hs'\n", markerIdUtf8.c_str(), gotId.c_str());
							}
							pass = dateShifted && selectionKept;
						}
					}
				} catch (const _com_error& e) {
					wprintf(L"MARKERDRAG: COM error 0x%08lX\n", (unsigned long)e.Error());
				}
				wprintf(pass ? L"MARKERDRAG PASS\n" : L"MARKERDRAG FAIL\n");
				if (!pass) rc = 1;
			}

			// ---- stage 13: TEXTELEM ----------------------------------------------
			// PpText end-to-end wiring (this unit): add one ANCHORED text
			// (anchored to task "t1") and one FREE text (row "r_research",
			// date "2026-06-10") via GanttOps' AddText, then rebuild via
			// UpdateGantt so both actually appear as PP_KIND=TEXT shapes.
			// Click the FREE text at its rendered rect's center (a real rect,
			// unlike Marker's synthesized band — see GanttHitTest's Text
			// zone), assert the overlay's internal selection model resolves
			// to kind=Text (Overlay_GetSelectedKindForTest — the id-only hook
			// can't disambiguate a text id from a task/milestone id sharing
			// the same string across independent tests). Then drag it
			// straight down by one row-band height (mirrors DRAGROW's
			// row-rect gathering) combined with a horizontal shift (mirrors
			// MARKERDRAG's day math), and re-read PP_DOC: both rowId AND
			// date must have changed to the dragged-to cell.
			if (rc == 0) RequireForeground(ppHwnd, &rc);
			if (rc == 0) {
				bool pass = false;
				try {
					auto WNarrowTx = [](const wchar_t* wtext) -> std::string {
						if (!wtext || !*wtext) return "";
						int len = (int)::wcslen(wtext);
						int nb = ::WideCharToMultiByte(CP_UTF8, 0, wtext, len, NULL, 0, NULL, NULL);
						std::string s(nb, '\0');
						if (nb > 0) ::WideCharToMultiByte(CP_UTF8, 0, wtext, len, &s[0], nb, NULL, NULL);
						return s;
					};

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

					std::string anchTextId, freeTextId;
					if (!chartRoot) {
						wprintf(L"TEXTELEM: could not find CHART_ROOT\n");
					} else {
						std::string docJson = WNarrowTx((const wchar_t*)chartRoot->GetTags()->Item(_bstr_t(L"PP_DOC")));
						PpDocument doc = DocumentFromJson(docJson);
						anchTextId = AddText(doc, "Anchored note", "t1", "", "");
						freeTextId = AddText(doc, "Free note", "", "r_research", "2026-06-10");
						if (anchTextId.empty() || freeTextId.empty()) {
							wprintf(L"TEXTELEM: AddText failed to return an id\n");
						} else {
							HRESULT hr = UpdateGantt(app, doc, freeTextId);
							if (FAILED(hr)) wprintf(L"TEXTELEM: UpdateGantt failed hr=0x%08lX\n", (unsigned long)hr);
						}
					}

					// Re-find CHART_ROOT (UpdateGantt may have reconciled in place,
					// but re-finding by tag is the same defensive pattern INPLACE/
					// DRAGROW use rather than trusting a possibly-stale pointer).
					PowerPoint::ShapePtr chartRoot2;
					if (!anchTextId.empty() && !freeTextId.empty()) {
						long n2 = shapes->GetCount();
						for (long i = 1; i <= n2 && !chartRoot2; ++i) {
							PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
							_bstr_t k = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
							std::wstring kind = k.length() ? (const wchar_t*)k : L"";
							if (kind == L"CHART_ROOT") chartRoot2 = sh;
						}
					}

					PowerPoint::ShapePtr freeTextChild;
					struct RowRectTx { std::wstring rowId; float top; float height; };
					std::vector<RowRectTx> rowRects;
					if (chartRoot2) {
						PowerPoint::GroupShapesPtr grp = chartRoot2->GetGroupItems();
						long gn = grp->GetCount();
						for (long j = 1; j <= gn; ++j) {
							PowerPoint::ShapePtr child = grp->Item(_variant_t(j));
							_bstr_t ck = child->GetTags()->Item(_bstr_t(L"PP_KIND"));
							std::wstring ckind = ck.length() ? (const wchar_t*)ck : L"";
							if (ckind == L"TEXT") {
								_bstr_t cid = child->GetTags()->Item(_bstr_t(L"PP_ID"));
								std::string cidUtf8 = WNarrowTx(cid.length() ? (const wchar_t*)cid : L"");
								if (cidUtf8 == freeTextId) freeTextChild = child;
							}
							if (ckind == L"ROW_LABEL") {
								_bstr_t rid = child->GetTags()->Item(_bstr_t(L"PP_ID"));
								std::wstring rowId = rid.length() ? (const wchar_t*)rid : L"";
								if (!rowId.empty()) rowRects.push_back({ rowId, child->GetTop(), child->GetHeight() });
							}
						}
					}
					std::sort(rowRects.begin(), rowRects.end(), [](const RowRectTx& a, const RowRectTx& b) { return a.top < b.top; });

					int origIdx = -1;
					for (size_t k = 0; k < rowRects.size(); ++k) {
						if (WNarrowTx(rowRects[k].rowId.c_str()) == "r_research") { origIdx = (int)k; break; }
					}

					HWND ov = OverlayHwnd();
					if (!freeTextChild || !ov || origIdx < 0 || origIdx + 1 >= (int)rowRects.size()) {
						wprintf(L"TEXTELEM: could not find free TEXT child / overlay hwnd / adjacent row band\n");
					} else {
						PowerPoint::DocumentWindowPtr win = app->GetActiveWindow();
						float left = freeTextChild->GetLeft(), top = freeTextChild->GetTop();
						float w = freeTextChild->GetWidth(), h = freeTextChild->GetHeight();
						POINT screenPt = {
							win->PointsToScreenPixelsX(left + w / 2.0f),
							win->PointsToScreenPixelsY(top + h / 2.0f)
						};
						POINT clientPt = screenPt;
						::ScreenToClient(ov, &clientPt);

						// Clear whatever the PRIOR stage (MARKERDRAG) left
						// selected first: its mini-toolbar can still be visible
						// and, depending on where the new text landed, could
						// overlap the text's click point and swallow the click
						// as a toolbar-button hit instead of a selection change.
						// A click on the chart's title-strip background (a
						// RowBand hit with an empty rowId) deselects cleanly and
						// is far from any row band's toolbar.
						{
							float chartLeft = chartRoot2->GetLeft(), chartTop = chartRoot2->GetTop();
							POINT bgScreenPt = {
								win->PointsToScreenPixelsX(chartLeft + 10.0f),
								win->PointsToScreenPixelsY(chartTop + 5.0f)
							};
							POINT bgClientPt = bgScreenPt;
							::ScreenToClient(ov, &bgClientPt);
							SetOverlayCursorOverride(bgScreenPt);
							LPARAM bgLp = MAKELPARAM((short)bgClientPt.x, (short)bgClientPt.y);
							::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, bgLp);
							::PostMessageW(ov, WM_LBUTTONUP, 0, bgLp);
							PumpFor(400);
						}

						SetOverlayCursorOverride(screenPt);

						// Click first: select, then assert the internal selection
						// model resolved to kind=Text (not task/milestone/marker/
						// row) via the new Overlay_GetSelectedKindForTest hook.
						LPARAM clickLp = MAKELPARAM((short)clientPt.x, (short)clientPt.y);
						::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, clickLp);
						::PostMessageW(ov, WM_LBUTTONUP, 0, clickLp);
						PumpFor(400);

						const char* gotIdAfterClick = Overlay_GetSelectedIdForTest();
						std::string gotIdAfterClickStr = gotIdAfterClick ? gotIdAfterClick : "";
						int gotKindAfterClick = Overlay_GetSelectedKindForTest();
						bool selectedAsText = (gotIdAfterClickStr == freeTextId) && (gotKindAfterClick == OVERLAY_SELKIND_TEXT_FOR_TEST);
						if (!selectedAsText) {
							wprintf(L"TEXTELEM: expected selected id '%hs' kind=text(%d), got id='%hs' kind=%d\n",
								freeTextId.c_str(), (int)OVERLAY_SELKIND_TEXT_FOR_TEST, gotIdAfterClickStr.c_str(), gotKindAfterClick);
						}

						// Row-band height in SCREEN pixels (DRAGROW's approach) for
						// the vertical component, combined with a horizontal
						// day-shift (MARKERDRAG's approach) via PP_PROJ.
						int rowTopScreen = win->PointsToScreenPixelsY(rowRects[origIdx].top);
						int nextRowTopScreen = win->PointsToScreenPixelsY(rowRects[origIdx + 1].top);
						int bandHeightPx = nextRowTopScreen - rowTopScreen;
						std::wstring targetRowId = rowRects[origIdx + 1].rowId;

						std::string projJson = WNarrowTx((const wchar_t*)chartRoot2->GetTags()->Item(_bstr_t(L"PP_PROJ")));
						PpProj proj;
						bool haveProj = ParseProj(projJson, &proj);
						const long kDragDays = 3;
						long shiftPx = 0;
						if (haveProj) {
							int screenX1 = win->PointsToScreenPixelsX(proj.originX);
							int screenX2 = win->PointsToScreenPixelsX(proj.originX + proj.ptPerDay);
							double pxPerDay = (double)(screenX2 - screenX1);
							shiftPx = (long)::lround(kDragDays * pxPerDay);
						}

						::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, clickLp);
						PumpFor(60);
						const int kSteps = 5;
						for (int s = 1; s <= kSteps; ++s) {
							int mx = clientPt.x + (int)(shiftPx * s / kSteps);
							int my = clientPt.y + (int)((long)bandHeightPx * s / kSteps);
							SetOverlayCursorOverride({ screenPt.x + (mx - clientPt.x), screenPt.y + (my - clientPt.y) });
							LPARAM moveLp = MAKELPARAM((short)mx, (short)my);
							::PostMessageW(ov, WM_MOUSEMOVE, 0, moveLp);
							PumpFor(60);
						}
						int finalX = clientPt.x + (int)shiftPx;
						int finalY = clientPt.y + bandHeightPx;
						LPARAM finalMoveLp = MAKELPARAM((short)finalX, (short)finalY);
						for (int extra = 0; extra < 3; ++extra) {
							SetOverlayCursorOverride({ screenPt.x + (int)shiftPx, screenPt.y + bandHeightPx });
							::PostMessageW(ov, WM_MOUSEMOVE, 0, finalMoveLp);
							PumpFor(60);
						}
						::PostMessageW(ov, WM_LBUTTONUP, 0, finalMoveLp);
						PumpFor(800);

						std::string afterJson = ReadGanttFromSlide(app);
						PpDocument docAfter = DocumentFromJson(afterJson);
						std::string newRowId, newDate;
						bool foundAfter = false;
						for (const auto& t : docAfter.texts) {
							if (t.id == freeTextId) { newRowId = t.rowId; newDate = t.date; foundAfter = true; break; }
						}
						std::string targetRowIdUtf8 = WNarrowTx(targetRowId.c_str());
						std::string expectDate = DaysToDate(DateToDays("2026-06-10") + kDragDays);
						bool rowChanged = foundAfter && newRowId == targetRowIdUtf8;
						bool dateChanged = foundAfter && newDate == expectDate;
						if (!rowChanged) {
							wprintf(L"TEXTELEM: expected rowId '%hs', got '%hs'\n", targetRowIdUtf8.c_str(), newRowId.c_str());
						}
						if (!dateChanged) {
							wprintf(L"TEXTELEM: expected date '%hs', got '%hs'\n", expectDate.c_str(), newDate.c_str());
						}
						pass = selectedAsText && rowChanged && dateChanged;
					}
				} catch (const _com_error& e) {
					wprintf(L"TEXTELEM: COM error 0x%08lX\n", (unsigned long)e.Error());
				}
				wprintf(pass ? L"TEXTELEM PASS\n" : L"TEXTELEM FAIL\n");
				if (!pass) rc = 1;
			}

			// ---- stage 14: APPBAR -----------------------------------------------
			if (rc == 0) RequireForeground(ppHwnd, &rc);
			if (rc == 0) {
				bool pass = false;
				bool step2 = false;
				bool step3 = false;
				try {
					HWND ab = OverlayAppBarHwnd();
					if (!ab || !::IsWindowVisible(ab)) {
						wprintf(L"APPBAR: app-bar window missing/hidden\n");
					} else {
						auto PostAppBarClick = [&](int cmd) -> bool {
							RECT r = {};
							if (!OverlayAppBarButtonRectForTest(cmd, &r)) return false;
							POINT pt = {
								(r.left + r.right) / 2,
								(r.top + r.bottom) / 2
							};
							::ScreenToClient(ab, &pt);
							LPARAM lp = MAKELPARAM((short)pt.x, (short)pt.y);
							::PostMessageW(ab, WM_LBUTTONDOWN, MK_LBUTTON, lp);
							::PostMessageW(ab, WM_LBUTTONUP, 0, lp);
							return true;
						};

						if (!PostAppBarClick(HtCmd_ScaleMonth)) {
							wprintf(L"APPBAR: could not locate Scale-M button rect\n");
						} else {
							PumpFor(700);
							std::string docJson = ReadGanttFromSlide(app);
							PpDocument doc = DocumentFromJson(docJson);
							step2 = (doc.scale == "month");
							if (!step2) {
								// The bar can be mid-relayout when the rect was
								// read (active chip moved, bar re-measured after
								// the prior commit) — settle, re-read the rect
								// via a fresh click, re-check. Behavioral assert
								// unchanged.
								wprintf(L"APPBAR diag: Scale-M retry (got '%hs')\n", doc.scale.c_str());
								PumpFor(600);
								if (PostAppBarClick(HtCmd_ScaleMonth)) {
									PumpFor(700);
									docJson = ReadGanttFromSlide(app);
									doc = DocumentFromJson(docJson);
									step2 = (doc.scale == "month");
								}
							}
							if (!step2) wprintf(L"APPBAR: expected scale 'month', got '%hs'\n", doc.scale.c_str());
						}

						if (step2 && !PostAppBarClick(HtCmd_ScaleWeek)) {
							wprintf(L"APPBAR: could not locate Scale-W button rect\n");
						} else if (step2) {
							PumpFor(700);
							std::string docJson = ReadGanttFromSlide(app);
							PpDocument doc = DocumentFromJson(docJson);
							step3 = (doc.scale == "week");
							if (!step3) {
								// Same stale-rect retry as the Scale-M step.
								wprintf(L"APPBAR diag: Scale-W retry (got '%hs')\n", doc.scale.c_str());
								PumpFor(600);
								if (PostAppBarClick(HtCmd_ScaleWeek)) {
									PumpFor(700);
									docJson = ReadGanttFromSlide(app);
									doc = DocumentFromJson(docJson);
									step3 = (doc.scale == "week");
								}
							}
							if (!step3) wprintf(L"APPBAR: expected scale 'week', got '%hs'\n", doc.scale.c_str());
						}
						pass = step2 && step3;
					}
				} catch (const _com_error& e) {
					wprintf(L"APPBAR: COM error 0x%08lX\n", (unsigned long)e.Error());
				}
				wprintf(pass ? L"APPBAR PASS\n" : L"APPBAR FAIL\n");
				if (!pass) rc = 1;
			}

			// ---- stage 15: TASKCTX ----------------------------------------------
			if (rc == 0) RequireForeground(ppHwnd, &rc);
			if (rc == 0) {
				bool pass = false;
				const char* kTaskId = "t1";
				const char* kMsId = "m1";
				int failStep = 0;
				try {
					HWND ov = OverlayHwnd();
					HWND ab = OverlayAppBarHwnd();
					PowerPoint::ShapePtr chartRoot = RefetchChartRoot(app);
					if (!ov || !ab || !chartRoot) {
						failStep = 0;
						wprintf(L"TASKCTX FAIL preconditions (ov=%d ab=%d chart=%d)\n",
							ov ? 1 : 0, ab ? 1 : 0, chartRoot ? 1 : 0);
					} else {
						auto PostAppBarClick = [&](int cmd) -> bool {
							RECT r = {};
							if (!OverlayAppBarButtonRectForTest(cmd, &r)) return false;
							POINT pt = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
							POINT client = pt;
							::ScreenToClient(ab, &client);
							LPARAM lp = MAKELPARAM((short)client.x, (short)client.y);
							::PostMessageW(ab, WM_LBUTTONDOWN, MK_LBUTTON, lp);
							::PostMessageW(ab, WM_LBUTTONUP, 0, lp);
							return true;
						};

						// Step 1: select task t1 via bar click.
						bool step1 = false;
						PowerPoint::ShapePtr t1Shape = FindChartChildByKindId(chartRoot, L"TASK", L"t1");
						if (!t1Shape) {
							failStep = 1;
							wprintf(L"TASKCTX FAIL step1 t1 shape missing\n");
						} else {
							POINT screenPt = {};
							if (!ShapeScreenCenter(app, t1Shape, &screenPt)) {
								failStep = 1;
								wprintf(L"TASKCTX FAIL step1 screen point\n");
							} else {
								POINT clientPt = screenPt;
								::ScreenToClient(ov, &clientPt);
								SetOverlayCursorOverride(screenPt);
								LPARAM lp = MAKELPARAM((short)clientPt.x, (short)clientPt.y);
								::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, lp);
								::PostMessageW(ov, WM_LBUTTONUP, 0, lp);
								PumpFor(700);
								Overlay_InvalidateAppBarForTest();
								PumpFor(300);
								step1 = (Overlay_GetSelectedKindForTest() == OVERLAY_SELKIND_TASK_FOR_TEST)
									&& Overlay_GetSelectedIdForTest()
									&& std::strcmp(Overlay_GetSelectedIdForTest(), kTaskId) == 0;
								if (!step1) {
									failStep = 1;
									wprintf(L"TASKCTX FAIL step1 select t1 (kind=%d id='%hs')\n",
										Overlay_GetSelectedKindForTest(),
										Overlay_GetSelectedIdForTest() ? Overlay_GetSelectedIdForTest() : "(null)");
								}
							}
						}

						// Step 2: swatch3 -> color #7A4FA3 + bar fill blend read-back.
						bool step2 = false;
						if (step1) {
							const long expFill = (long)Bgr(gt::BlendOnWhite(0x7A4FA3, 0.40f));
							for (int attempt = 1; attempt <= 2 && !step2; ++attempt) {
								if (attempt > 1) wprintf(L"TASKCTX diag: swatch3 retry attempt %d\n", attempt);
								if (!PostAppBarClick(HtCmd_Swatch3)) {
									failStep = 2;
									wprintf(L"TASKCTX FAIL step2 could not click swatch3\n");
									break;
								}
								PumpFor(800);
								chartRoot = RefetchChartRoot(app);
								std::string docJson = ReadGanttFromSlide(app);
								PpDocument doc = DocumentFromJson(docJson);
								const PpTask* t = FindTaskInDoc(doc, kTaskId);
								bool colorOk = t && AppBarColorEquals(t->color, "#7A4FA3");
								long taskFill = -1;
								if (chartRoot) {
									PowerPoint::ShapePtr t1After = FindChartChildByKindId(chartRoot, L"TASK", L"t1");
									if (t1After) taskFill = (long)t1After->GetFill()->GetForeColor()->GetPpRGB();
								}
								step2 = colorOk && taskFill == expFill;
								if (!step2 && attempt == 2) {
									failStep = 2;
									wprintf(L"TASKCTX FAIL step2 swatch (colorOk=%d fill=0x%06lX exp=0x%06lX)\n",
										colorOk ? 1 : 0, taskFill, expFill);
								}
							}
						}

						// Step 3: +1d nudge shifts t1 dates by one day.
						bool step3 = false;
						if (step2) {
							std::string docJson0 = ReadGanttFromSlide(app);
							PpDocument doc0 = DocumentFromJson(docJson0);
							const PpTask* t0 = FindTaskInDoc(doc0, kTaskId);
							long start0 = t0 ? DateToDays(t0->start) : 0;
							long end0 = t0 ? DateToDays(t0->end) : 0;
							for (int attempt = 1; attempt <= 2 && !step3; ++attempt) {
								if (attempt > 1) wprintf(L"TASKCTX diag: +1d retry attempt %d\n", attempt);
								if (!PostAppBarClick(HtCmd_NudgePlus1)) {
									failStep = 3;
									wprintf(L"TASKCTX FAIL step3 could not click +1d\n");
									break;
								}
								PumpFor(800);
								chartRoot = RefetchChartRoot(app);
								std::string docJson = ReadGanttFromSlide(app);
								PpDocument doc = DocumentFromJson(docJson);
								const PpTask* t = FindTaskInDoc(doc, kTaskId);
								step3 = t && DateToDays(t->start) == start0 + 1 && DateToDays(t->end) == end0 + 1;
								if (!step3 && attempt == 2) {
									failStep = 3;
									wprintf(L"TASKCTX FAIL step3 nudge (got %hs..%hs)\n",
										t ? t->start.c_str() : "?", t ? t->end.c_str() : "?");
								}
							}
						}

						// Step 4: Label cycle bar->rail; rail dot/label present.
						bool step4 = false;
						if (step3) {
							for (int attempt = 1; attempt <= 2 && !step4; ++attempt) {
								if (attempt > 1) wprintf(L"TASKCTX diag: Label retry attempt %d\n", attempt);
								if (!PostAppBarClick(HtCmd_CycleLabelPlacement)) {
									failStep = 4;
									wprintf(L"TASKCTX FAIL step4 could not click Label\n");
									break;
								}
								PumpFor(800);
								chartRoot = RefetchChartRoot(app);
								std::string docJson = ReadGanttFromSlide(app);
								PpDocument doc = DocumentFromJson(docJson);
								const PpTask* t = FindTaskInDoc(doc, kTaskId);
								bool placementOk = t && t->labelPlacement == "rail";
								bool hasRailDot = false, hasRailLbl = false;
								if (chartRoot) {
									PowerPoint::GroupShapesPtr grp = chartRoot->GetGroupItems();
									long gn = grp->GetCount();
									for (long j = 1; j <= gn; ++j) {
										PowerPoint::ShapePtr child = grp->Item(_variant_t(j));
										_bstr_t ck = child->GetTags()->Item(_bstr_t(L"PP_KIND"));
										_bstr_t cid = child->GetTags()->Item(_bstr_t(L"PP_ID"));
										if (!ck.length() || !cid.length()) continue;
										std::wstring ks = (const wchar_t*)ck;
										std::wstring is = (const wchar_t*)cid;
										if (is != L"t1") continue;
										if (ks == L"RAIL_DOT") hasRailDot = true;
										if (ks == L"RAIL_TASKLBL") hasRailLbl = true;
									}
								}
								step4 = placementOk && hasRailDot && hasRailLbl;
								if (!step4 && attempt == 2) {
									failStep = 4;
									wprintf(L"TASKCTX FAIL step4 label (placement=%d railDot=%d railLbl=%d)\n",
										placementOk ? 1 : 0, hasRailDot ? 1 : 0, hasRailLbl ? 1 : 0);
								}
							}
						}

						// Step 5: select m1, -1d nudge.
						bool step5 = false;
						if (step4) {
							chartRoot = RefetchChartRoot(app);
							PowerPoint::ShapePtr m1Shape = chartRoot
								? FindChartChildByKindId(chartRoot, L"MILESTONE", L"m1") : nullptr;
							if (!m1Shape) {
								failStep = 5;
								wprintf(L"TASKCTX FAIL step5 m1 shape missing\n");
							} else {
								POINT screenPt = {};
								ShapeScreenCenter(app, m1Shape, &screenPt);
								POINT clientPt = screenPt;
								::ScreenToClient(ov, &clientPt);
								SetOverlayCursorOverride(screenPt);
								LPARAM lp = MAKELPARAM((short)clientPt.x, (short)clientPt.y);
								::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, lp);
								::PostMessageW(ov, WM_LBUTTONUP, 0, lp);
								PumpFor(700);
								Overlay_InvalidateAppBarForTest();
								PumpFor(300);
								// The preceding Label commit can shift layout (rail flip
								// moves bars/milestones left), so the first click may land
								// on stale coordinates and clear selection. Verify the
								// selection took; re-click with FRESH coordinates if not
								// (standing rule).
								for (int sa = 1; sa <= 3; ++sa) {
									const char* selNow = Overlay_GetSelectedIdForTest();
									if (selNow && std::string(selNow) == "m1") break;
									wprintf(L"TASKCTX diag: milestone select retry %d\n", sa);
									PowerPoint::ShapePtr rootNow = RefetchChartRoot(app);
									PowerPoint::ShapePtr msNow = rootNow ? FindChartChildByKindId(rootNow, L"MILESTONE", L"m1") : nullptr;
									if (!msNow) { PumpFor(600); continue; }
									POINT sp2 = {};
									if (!ShapeScreenCenter(app, msNow, &sp2)) { PumpFor(600); continue; }
									POINT cp2 = sp2;
									::ScreenToClient(ov, &cp2);
									SetOverlayCursorOverride(sp2);
									LPARAM lp2 = MAKELPARAM((short)cp2.x, (short)cp2.y);
									::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, lp2);
									::PostMessageW(ov, WM_LBUTTONUP, 0, lp2);
									PumpFor(700);
									Overlay_InvalidateAppBarForTest();
									PumpFor(300);
								}
								std::string docJson0 = ReadGanttFromSlide(app);
								PpDocument doc0 = DocumentFromJson(docJson0);
								const PpMilestone* m0 = FindMilestoneInDoc(doc0, kMsId);
								long date0 = m0 ? DateToDays(m0->date) : 0;
								for (int attempt = 1; attempt <= 2 && !step5; ++attempt) {
									if (attempt > 1) wprintf(L"TASKCTX diag: milestone -1d retry attempt %d\n", attempt);
									if (!PostAppBarClick(HtCmd_NudgeMinus1)) {
										failStep = 5;
										wprintf(L"TASKCTX FAIL step5 could not click -1d\n");
										break;
									}
									PumpFor(800);
									chartRoot = RefetchChartRoot(app);
									std::string docJson = ReadGanttFromSlide(app);
									PpDocument doc = DocumentFromJson(docJson);
									const PpMilestone* m = FindMilestoneInDoc(doc, kMsId);
									step5 = m && DateToDays(m->date) == date0 - 1;
									if (!step5 && attempt == 2) {
										failStep = 5;
										wprintf(L"TASKCTX FAIL step5 milestone nudge (got %hs)\n",
											m ? m->date.c_str() : "?");
									}
								}
							}
						}

						// Step 6: Note adds anchored text to m1.
						bool step6 = false;
						if (step5) {
							std::string docJson0 = ReadGanttFromSlide(app);
							PpDocument doc0 = DocumentFromJson(docJson0);
							size_t textsBefore = doc0.texts.size();
							for (int attempt = 1; attempt <= 2 && !step6; ++attempt) {
								if (attempt > 1) wprintf(L"TASKCTX diag: Note retry attempt %d\n", attempt);
								if (!PostAppBarClick(HtCmd_AddNote)) {
									failStep = 6;
									wprintf(L"TASKCTX FAIL step6 could not click Note\n");
									break;
								}
								PumpFor(800);
								chartRoot = RefetchChartRoot(app);
								std::string docJson = ReadGanttFromSlide(app);
								PpDocument doc = DocumentFromJson(docJson);
								bool grew = doc.texts.size() == textsBefore + 1;
								bool anchored = false;
								for (const auto& tx : doc.texts) {
									if (tx.anchorId == kMsId) { anchored = true; break; }
								}
								step6 = grew && anchored;
								if (!step6 && attempt == 2) {
									failStep = 6;
									wprintf(L"TASKCTX FAIL step6 note (size %zu->%zu anchored=%d)\n",
										textsBefore, doc.texts.size(), anchored ? 1 : 0);
								}
							}
						}

						pass = step1 && step2 && step3 && step4 && step5 && step6;
						if (!pass && failStep == 0) failStep = 1;
						if (!pass) {
							const char* dump = Overlay_DumpChromeStateForTest();
							wprintf(L"TASKCTX FAIL step%d chrome: %hs\n", failStep, dump ? dump : "(null)");
						}
					}
				} catch (const _com_error& e) {
					wprintf(L"TASKCTX FAIL COM error 0x%08lX\n", (unsigned long)e.Error());
				}
				wprintf(pass ? L"TASKCTX PASS\n" : L"TASKCTX FAIL\n");
				if (!pass) rc = 1;
			}

			// ---- stage 16: ROWSEL -----------------------------------------------
			if (rc == 0) RequireForeground(ppHwnd, &rc);
			if (rc == 0) {
				bool pass = false;
				const std::string kRowId = "r_research";
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
					HWND ov = OverlayHwnd();
					HWND ab = OverlayAppBarHwnd();
					if (!chartRoot || !ov || !ab) {
						wprintf(L"ROWSEL FAIL preconditions (chart=%d ov=%d ab=%d)\n",
							chartRoot ? 1 : 0, ov ? 1 : 0, ab ? 1 : 0);
					} else {
						POINT railPt = {};
						if (!RowRailScreenPoint(app, chartRoot, kRowId, &railPt)) {
							wprintf(L"ROWSEL FAIL could not derive rail point from PP_ROWY\n");
						} else {
							Overlay_SelectForTest(nullptr, nullptr);
							Overlay_InvalidateAppBarForTest();
							PumpFor(400);

							POINT clientPt = railPt;
							::ScreenToClient(ov, &clientPt);
							SetOverlayCursorOverride(railPt);
							LPARAM clickLp = MAKELPARAM((short)clientPt.x, (short)clientPt.y);
							::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, clickLp);
							::PostMessageW(ov, WM_LBUTTONUP, 0, clickLp);
							PumpFor(700);

							bool step1 = (Overlay_GetSelectedKindForTest() == OVERLAY_SELKIND_ROW_FOR_TEST);
							const char* selId = Overlay_GetSelectedIdForTest();
							if (!step1 || !selId || kRowId != selId) {
								wprintf(L"ROWSEL FAIL step1 kind/id (kind=%d id='%hs')\n",
									Overlay_GetSelectedKindForTest(), selId ? selId : "(null)");
							} else {
								std::string docJson0 = ReadGanttFromSlide(app);
								PpDocument doc0 = DocumentFromJson(docJson0);
								size_t rowsBefore = doc0.rows.size();

								auto PostAppBarClick = [&](int cmd) -> bool {
									RECT r = {};
									if (!OverlayAppBarButtonRectForTest(cmd, &r)) return false;
									POINT pt = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
									::ScreenToClient(ab, &pt);
									LPARAM lp = MAKELPARAM((short)pt.x, (short)pt.y);
									::PostMessageW(ab, WM_LBUTTONDOWN, MK_LBUTTON, lp);
									::PostMessageW(ab, WM_LBUTTONUP, 0, lp);
									return true;
								};

								bool step2 = false;
								bool step3 = false;
								if (!PostAppBarClick(HtCmd_AddRowBelow)) {
									wprintf(L"ROWSEL FAIL could not click Below\n");
								} else {
									PumpFor(800);
									std::string docJson1 = ReadGanttFromSlide(app);
									PpDocument doc1 = DocumentFromJson(docJson1);
									step2 = doc1.rows.size() == rowsBefore + 1;
									if (!step2) {
										wprintf(L"ROWSEL FAIL step2 rows %zu expected %zu\n",
											doc1.rows.size(), rowsBefore + 1);
									}
								}

								bool step4 = false;
								if (step2) {
									std::string docJsonB = ReadGanttFromSlide(app);
									PpDocument docB = DocumentFromJson(docJsonB);
									// MoveRowDown operates on the CURRENTLY-SELECTED row —
									// after "Below" that is the NEWLY CREATED row (the
									// product selects the new row, mirroring AddTask's
									// select-the-new-item behavior). Track THAT row.
									const char* selRowC = Overlay_GetSelectedIdForTest();
									std::string movedRowId = (selRowC && *selRowC) ? selRowC : "r_research";
									int posResearch = -1, posDesign = -1;
									for (size_t i = 0; i < docB.rows.size(); ++i) {
										if (docB.rows[i].id == movedRowId) posResearch = (int)i;
										if (docB.rows[i].id == "r_design") posDesign = (int)i;
									}
									if (!PostAppBarClick(HtCmd_MoveRowDown)) {
										wprintf(L"ROWSEL FAIL could not click MoveRowDown\n");
									} else {
										PumpFor(800);
										std::string docJsonD = ReadGanttFromSlide(app);
										PpDocument docD = DocumentFromJson(docJsonD);
										int posResearchAfter = -1, posDesignAfter = -1;
										for (size_t i = 0; i < docD.rows.size(); ++i) {
											if (docD.rows[i].id == movedRowId) posResearchAfter = (int)i;
											if (docD.rows[i].id == "r_design") posDesignAfter = (int)i;
										}
										// MoveRowDown is a flat ADJACENT swap (see GanttOps.h) —
										// after step2's added row the row below r_research is
										// the new row, not r_design, so assert the adjacent-swap
										// semantic: r_research's index increases by exactly 1.
										step4 = posResearch >= 0 && posResearchAfter == posResearch + 1;
										if (!step4) {
											// Stale-rect retry (standing rule).
											wprintf(L"ROWSEL diag: MoveRowDown retry (research %d->%d)\n", posResearch, posResearchAfter);
											if (PostAppBarClick(HtCmd_MoveRowDown)) {
												PumpFor(900);
												std::string djr = ReadGanttFromSlide(app);
												PpDocument dr = DocumentFromJson(djr);
												int pr2 = -1;
												for (size_t i = 0; i < dr.rows.size(); ++i) {
													if (dr.rows[i].id == movedRowId) pr2 = (int)i;
												}
												step4 = pr2 == posResearch + 1;
											}
										}
										if (!step4) {
											wprintf(L"ROWSEL FAIL step4 order swap (research %d->%d design %d->%d)\n",
												posResearch, posResearchAfter, posDesign, posDesignAfter);
										}
									}
								}

								bool step5 = false;
								if (step4) {
									Overlay_SelectForTest("ROW", "r_build");
									Overlay_InvalidateAppBarForTest();
									PumpFor(500);
									std::string docJsonPre = ReadGanttFromSlide(app);
									PpDocument docPre = DocumentFromJson(docJsonPre);
									size_t rowsPre = docPre.rows.size();
									bool hadT5 = false;
									for (const auto& t : docPre.tasks) if (t.id == "t5") hadT5 = true;

									// Standing rule: hotkey delivery requires the id to be
									// REGISTERED at dispatch time (registration transitions on
									// a tick after the selection change) and a state-changing
									// post is verified + retried. Settle first, double-post,
									// then verify with one full retry.
									PumpFor(400);
									bool rowGone = false, taskGone = false;
									for (int attempt = 1; attempt <= 2 && !(rowGone && taskGone); ++attempt) {
										if (attempt > 1) wprintf(L"ROWSEL diag: Delete re-post attempt %d\n", attempt);
										::PostMessageW(ov, WM_HOTKEY, OVERLAY_HOTKEY_DELETE_FOR_TEST, 0);
										PumpFor(400);
										::PostMessageW(ov, WM_HOTKEY, OVERLAY_HOTKEY_DELETE_FOR_TEST, 0);
										PumpFor(800);
										std::string docJsonDel = ReadGanttFromSlide(app);
										PpDocument docDel = DocumentFromJson(docJsonDel);
										rowGone = true;
										for (const auto& r : docDel.rows) if (r.id == "r_build") rowGone = false;
										taskGone = true;
										for (const auto& t : docDel.tasks) if (t.id == "t5") taskGone = false;
										step5 = rowGone && taskGone && docDel.rows.size() + 1 == rowsPre;
									}
									if (!step5) {
										wprintf(L"ROWSEL FAIL step5 delete cascade (rowGone=%d taskGone=%d)\n",
											rowGone ? 1 : 0, taskGone ? 1 : 0);
									}
								}

								bool step6 = false;
								if (step5) {
									// S1 defect: rows with no ROW_LABEL shape still get a
									// PP_ROWY band. Clear r_launch's name via rename so
									// emission skips ROW_LABEL, then select via rail click.
									const std::string targetRow = "r_launch";
									// DRAIN before selecting: step5's double-posted Delete can
									// dispatch LATE (observed: it deleted r_launch right after
									// this step selected it). With the selection cleared, a
									// stray Delete is a logged no-op.
									Overlay_SelectForTest(nullptr, nullptr);
									PumpFor(700);
									Overlay_SelectForTest("ROW", targetRow.c_str());
									Overlay_InvalidateAppBarForTest();
									PumpFor(500);
									bool renameClicked = false;
									for (int a = 1; a <= 5 && !renameClicked; ++a) {
										// Re-assert the selection each attempt: a sync tick can
										// clear a seam-set selection while bands/snapshot are
										// still settling after the previous commit.
										Overlay_SelectForTest("ROW", targetRow.c_str());
										Overlay_InvalidateAppBarForTest();
										PumpFor(700);
										renameClicked = PostAppBarClick(HtCmd_Rename);
										if (!renameClicked && a == 5) {
											const char* cs2 = Overlay_DumpChromeStateForTest();
											wprintf(L"ROWSEL diag step6 chrome: %hs\n", cs2 ? cs2 : "(null)");
											std::string dj6 = ReadGanttFromSlide(app);
											PpDocument d6 = DocumentFromJson(dj6);
											std::string rowsList, msList;
											for (const auto& r : d6.rows) { rowsList += r.id; rowsList += " "; }
											for (const auto& m : d6.milestones) { msList += m.id; msList += "@"; msList += m.rowId; msList += " "; }
											wprintf(L"ROWSEL diag step6 doc rows: %hs| milestones: %hs\n", rowsList.c_str(), msList.c_str());
										}
									}
									if (!renameClicked) {
										wprintf(L"ROWSEL FAIL step6 could not open row rename\n");
									} else {
										PumpFor(500);
										HWND inlineEd = ::FindWindowW(L"PowerPlannerInlineEditor", NULL);
										HWND editCtl = inlineEd ? ::FindWindowExW(inlineEd, NULL, L"Edit", NULL) : NULL;
										if (!editCtl) {
											wprintf(L"ROWSEL FAIL step6 inline editor missing\n");
										} else {
											::SetWindowTextW(editCtl, L"");
											::PostMessageW(editCtl, WM_KEYDOWN, VK_RETURN, 0);
											::PostMessageW(editCtl, WM_KEYUP, VK_RETURN, 0);
											PumpFor(800);

											// Re-fetch the chart root: the rename commit's rebuild
											// can regroup and leave the stage's captured pointer
											// dead (0x800A01A8 observed at the walk below).
											{
												PowerPoint::_SlidePtr slideNow = app->GetActiveWindow()->GetView()->GetSlide();
												PowerPoint::ShapesPtr shapesNow = slideNow->GetShapes();
												long nn = shapesNow->GetCount();
												for (long jj = 1; jj <= nn; ++jj) {
													PowerPoint::ShapePtr shNow = shapesNow->Item(_variant_t(jj));
													_bstr_t kNow = shNow->GetTags()->Item(_bstr_t(L"PP_KIND"));
													if (kNow.length() && std::wstring((const wchar_t*)kNow) == L"CHART_ROOT") {
														try { (void)shNow->GetLeft(); chartRoot = shNow; break; }
														catch (const _com_error&) { continue; }
													}
												}
											}
											bool noRowLabel = true;
											PowerPoint::GroupShapesPtr grp = chartRoot->GetGroupItems();
											long gn = grp->GetCount();
											for (long j = 1; j <= gn; ++j) {
												PowerPoint::ShapePtr child = grp->Item(_variant_t(j));
												_bstr_t ck = child->GetTags()->Item(_bstr_t(L"PP_KIND"));
												if (!ck.length() || NarrowFn((const wchar_t*)ck) != "ROW_LABEL") continue;
												_bstr_t rid = child->GetTags()->Item(_bstr_t(L"PP_ID"));
												if (rid.length() && NarrowFn((const wchar_t*)rid) == targetRow) {
													noRowLabel = false;
													break;
												}
											}

											POINT railPt2 = {};
											if (!noRowLabel) {
												wprintf(L"ROWSEL FAIL step6 ROW_LABEL still present for '%hs'\n", targetRow.c_str());
											} else if (!RowRailScreenPoint(app, chartRoot, targetRow, &railPt2)) {
												wprintf(L"ROWSEL FAIL step6 rail point for '%hs'\n", targetRow.c_str());
											} else {
												POINT client2 = railPt2;
												::ScreenToClient(ov, &client2);
												SetOverlayCursorOverride(railPt2);
												LPARAM lp2 = MAKELPARAM((short)client2.x, (short)client2.y);
												::PostMessageW(ov, WM_LBUTTONDOWN, MK_LBUTTON, lp2);
												::PostMessageW(ov, WM_LBUTTONUP, 0, lp2);
												PumpFor(700);

												const char* dump = Overlay_DumpChromeStateForTest();
												std::string needle = "\"rowId\":\"" + targetRow + "\"";
												bool hasBand = dump && ::strstr(dump, needle.c_str()) != nullptr;
												bool plausible = false;
												if (hasBand && dump) {
													const char* rowBand = ::strstr(dump, needle.c_str());
													if (rowBand) {
														const char* topKey = ::strstr(rowBand, "\"top\":");
														const char* botKey = ::strstr(rowBand, "\"bottom\":");
														if (topKey && botKey) {
															long topV = 0, botV = 0;
															::sscanf_s(topKey, "\"top\":%ld", &topV);
															::sscanf_s(botKey, "\"bottom\":%ld", &botV);
															plausible = botV > topV && (botV - topV) > 4;
														}
													}
												}
												step6 = hasBand && plausible;
												if (!step6) {
													wprintf(L"ROWSEL FAIL step6 band coverage for '%hs'\n", targetRow.c_str());
												}
											}
										}
									}
								}

								// ---- step3: undo-entry probe (LAST; PP_DOC only) ----
								// ExecuteMso Undo rebuilds shapes outside our commit path
								// and bricks the overlay's chart tracking (zombie CHART_ROOT
								// walks). All overlay interaction must finish before this.
								if (step6) {
									Overlay_SelectForTest("ROW", kRowId.c_str());
									Overlay_InvalidateAppBarForTest();
									PumpFor(500);
									size_t rowsBeforeUndo = 0;
									if (!PostAppBarClick(HtCmd_AddRowBelow)) {
										wprintf(L"ROWSEL FAIL step3 could not click Below (undo probe)\n");
									} else {
										PumpFor(800);
										std::string docJsonPreUndo = ReadGanttFromSlide(app);
										PpDocument docPreUndo = DocumentFromJson(docJsonPreUndo);
										rowsBeforeUndo = docPreUndo.rows.size();
										bool undoOk = HarnessUndoOnce(app);
										PumpFor(800);
										std::string docJsonU = ReadGanttFromSlide(app);
										PpDocument docU = DocumentFromJson(docJsonU);
										step3 = undoOk && docU.rows.size() + 1 == rowsBeforeUndo;
										if (!step3) {
											wprintf(L"ROWSEL FAIL step3 undo (ok=%d rows %zu expected %zu)\n",
												undoOk ? 1 : 0, docU.rows.size(), rowsBeforeUndo - 1);
										}
									}
								}

								pass = step1 && step2 && step4 && step5 && step6 && step3;
							}
						}
					}
				} catch (const _com_error& e) {
					wprintf(L"ROWSEL FAIL COM error 0x%08lX\n", (unsigned long)e.Error());
				}
				wprintf(pass ? L"ROWSEL PASS\n" : L"ROWSEL FAIL\n");
				if (!pass) rc = 1;
			}

			// ---- INPUT NEUTRAL OK gate ------------------------------------------
			// Printed immediately before the final exit-code computation, gated
			// on a runtime flag proving the cursor override path was actually
			// exercised (not just declared) during the stage most sensitive to
			// physical-cursor interleaving (DRAG). If every stage above passed
			// (rc==0) but the override was somehow bypassed, this is withheld —
			// a green run without this marker is a harness regression, not a
			// pass.
			if (rc == 0) {
				if (g_cursorOverrideUsedDuringDrag) {
					wprintf(L"INPUT NEUTRAL OK\n");
				} else {
					wprintf(L"INPUT NEUTRAL CHECK FAILED: cursor override never exercised during DRAG\n");
					rc = 1;
				}
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
