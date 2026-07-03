// keys-probe.cpp — discovery spike (unit disco-keyboard-focus)
//
// Empirically determines how a WS_EX_NOACTIVATE overlay window can receive
// keyboard input (Delete / arrow keys) without breaking the host app's
// activation. Pure Win32, single process, no COM / PowerPoint involvement.
//
// Option A: WH_KEYBOARD_LL low-level hook installed while a flag is set.
// Option B: temporarily clear WS_EX_NOACTIVATE + SetForegroundWindow.
// Option C: RegisterHotKey for VK_DELETE/VK_LEFT/VK_RIGHT while "selection
//           active".
//
// Writes per-option findings + VERDICT line to keys-probe.txt next to the
// exe (native\build\keys-probe.txt). Prints "KEYS PROBE OK" on clean
// completion, exit 0. Watchdog thread hard-exits with code 3 after 60 s.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <string>

#pragma comment(lib, "user32.lib")

// ---------------------------------------------------------------------------
// Counters / globals
// ---------------------------------------------------------------------------

struct KeyCounts {
	int del = 0;
	int left = 0;
	int right = 0;
	void Reset() { del = left = right = 0; }
	int Total() const { return del + left + right; }
	bool AllSeen() const { return del > 0 && left > 0 && right > 0; }
};

static KeyCounts g_hostKeys;      // WM_KEYDOWN received by host window
static KeyCounts g_overlayKeys;   // WM_KEYDOWN received by overlay window
static KeyCounts g_hookKeys;      // keydowns seen by WH_KEYBOARD_LL hook
static KeyCounts g_hotkeys;       // WM_HOTKEY received by overlay window

static int g_hostActivateInactive = 0;   // host WM_ACTIVATE WA_INACTIVE
static int g_hostActivateActive = 0;     // host WM_ACTIVATE WA_ACTIVE/CLICKACTIVE
static int g_hostNcActivateFalse = 0;    // host WM_NCACTIVATE wParam==FALSE
static int g_hostNcActivateTrue = 0;     // host WM_NCACTIVATE wParam==TRUE

static volatile bool g_hookFlag = false; // "selection active" flag for option A
static HHOOK g_hook = NULL;

static HWND g_host = NULL;
static HWND g_overlay = NULL;

static void Count(KeyCounts& c, DWORD vk) {
	if (vk == VK_DELETE) ++c.del;
	else if (vk == VK_LEFT) ++c.left;
	else if (vk == VK_RIGHT) ++c.right;
}

// ---------------------------------------------------------------------------
// Findings file (created immediately, flushed line by line)
// ---------------------------------------------------------------------------

static std::string FindingsPath() {
	char exePath[MAX_PATH] = {};
	::GetModuleFileNameA(NULL, exePath, MAX_PATH);
	std::string path(exePath);
	size_t slash = path.find_last_of("\\/");
	std::string dir = (slash == std::string::npos) ? std::string(".") : path.substr(0, slash);
	return dir + "\\keys-probe.txt";
}

static std::ofstream g_out;

static void Line(const std::string& s) {
	g_out << s << "\n";
	g_out.flush();
	::printf("%s\n", s.c_str());
}

// ---------------------------------------------------------------------------
// Watchdog
// ---------------------------------------------------------------------------

static DWORD WINAPI WatchdogThread(LPVOID) {
	::Sleep(60000);
	::ExitProcess(3);
	return 0;
}

// ---------------------------------------------------------------------------
// Message pump / input synthesis
// ---------------------------------------------------------------------------

static void Pump(DWORD ms) {
	const DWORD end = ::GetTickCount() + ms;
	for (;;) {
		MSG msg;
		while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
			::TranslateMessage(&msg);
			::DispatchMessageW(&msg);
		}
		const DWORD now = ::GetTickCount();
		if ((int)(end - now) <= 0) break;
		::MsgWaitForMultipleObjects(0, NULL, FALSE, end - now, QS_ALLINPUT);
	}
}

static void SendVk(WORD vk) {
	INPUT in[2] = {};
	for (int i = 0; i < 2; ++i) {
		in[i].type = INPUT_KEYBOARD;
		in[i].ki.wVk = vk;
		in[i].ki.wScan = (WORD)::MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
		// VK_DELETE / VK_LEFT / VK_RIGHT are extended keys on the standard layout.
		in[i].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
	}
	in[1].ki.dwFlags |= KEYEVENTF_KEYUP;
	::SendInput(2, in, sizeof(INPUT));
}

// Sends Delete, Left, Right ONLY if one of our own windows is foreground —
// never spray synthetic Delete keys at some other application.
static bool SendTriple() {
	const HWND fg = ::GetForegroundWindow();
	if (fg != g_host && fg != g_overlay) return false;
	SendVk(VK_DELETE); Pump(120);
	SendVk(VK_LEFT);   Pump(120);
	SendVk(VK_RIGHT);  Pump(120);
	Pump(250);
	return true;
}

// Best-effort foreground acquisition (probe may be launched from a
// non-foreground automation shell; use the AttachThreadInput fallback).
static bool ForceForeground(HWND hwnd) {
	if (::GetForegroundWindow() == hwnd) { ::SetFocus(hwnd); return true; }
	::SetForegroundWindow(hwnd);
	Pump(150);
	if (::GetForegroundWindow() == hwnd) { ::SetFocus(hwnd); return true; }

	const HWND fg = ::GetForegroundWindow();
	const DWORD myTid = ::GetCurrentThreadId();
	const DWORD fgTid = fg ? ::GetWindowThreadProcessId(fg, NULL) : 0;
	if (fgTid && fgTid != myTid) {
		::AttachThreadInput(myTid, fgTid, TRUE);
		::BringWindowToTop(hwnd);
		::SetForegroundWindow(hwnd);
		::SetFocus(hwnd);
		::AttachThreadInput(myTid, fgTid, FALSE);
		Pump(200);
	}
	if (::GetForegroundWindow() == hwnd) { ::SetFocus(hwnd); return true; }
	return false;
}

// ---------------------------------------------------------------------------
// Hook + window procedures
// ---------------------------------------------------------------------------

static LRESULT CALLBACK LowLevelKeyProc(int code, WPARAM wParam, LPARAM lParam) {
	if (code == HC_ACTION && g_hookFlag &&
		(wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
		const KBDLLHOOKSTRUCT* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
		Count(g_hookKeys, k->vkCode);
		// Not consuming (return 1 would eat the key system-wide).
	}
	return ::CallNextHookEx(g_hook, code, wParam, lParam);
}

static LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_KEYDOWN:
		Count(g_hostKeys, (DWORD)wParam);
		return 0;
	case WM_ACTIVATE:
		if (LOWORD(wParam) == WA_INACTIVE) ++g_hostActivateInactive;
		else ++g_hostActivateActive;
		break;
	case WM_NCACTIVATE:
		if (wParam) ++g_hostNcActivateTrue;
		else ++g_hostNcActivateFalse;
		break;
	default:
		break;
	}
	return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_KEYDOWN:
		Count(g_overlayKeys, (DWORD)wParam);
		return 0;
	case WM_HOTKEY:
		if (wParam == 1) ++g_hotkeys.del;
		else if (wParam == 2) ++g_hotkeys.left;
		else if (wParam == 3) ++g_hotkeys.right;
		return 0;
	case WM_MOUSEACTIVATE:
		return MA_NOACTIVATE; // typical overlay behavior
	default:
		break;
	}
	return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string K(const KeyCounts& c) {
	char buf[96];
	::sprintf_s(buf, "del=%d left=%d right=%d", c.del, c.left, c.right);
	return buf;
}

static const char* YN(bool b) { return b ? "yes" : "no"; }

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	::SetProcessDPIAware();
	::CreateThread(NULL, 0, WatchdogThread, NULL, 0, NULL);

	g_out.open(FindingsPath(), std::ios::binary | std::ios::trunc);
	if (!g_out.is_open()) {
		::printf("cannot open findings file\n");
		return 2;
	}

	const HINSTANCE inst = ::GetModuleHandleW(NULL);

	WNDCLASSW hostCls = {};
	hostCls.lpfnWndProc = HostWndProc;
	hostCls.hInstance = inst;
	hostCls.hCursor = ::LoadCursorW(NULL, IDC_ARROW);
	hostCls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	hostCls.lpszClassName = L"PPKeysProbeHost";
	::RegisterClassW(&hostCls);

	WNDCLASSW ovlCls = hostCls;
	ovlCls.lpfnWndProc = OverlayWndProc;
	ovlCls.hbrBackground = (HBRUSH)(COLOR_HIGHLIGHT + 1);
	ovlCls.lpszClassName = L"PPKeysProbeOverlay";
	::RegisterClassW(&ovlCls);

	// Host window: stand-in for the focused PowerPoint main window.
	g_host = ::CreateWindowExW(0, L"PPKeysProbeHost", L"PP Keys Probe Host",
		WS_OVERLAPPEDWINDOW, 100, 100, 640, 420, NULL, NULL, inst, NULL);

	// Overlay window: layered, topmost, NOACTIVATE — same style family as the
	// add-in's on-slide overlay.
	g_overlay = ::CreateWindowExW(
		WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
		L"PPKeysProbeOverlay", L"PP Keys Probe Overlay",
		WS_POPUP, 220, 220, 320, 180, NULL, NULL, inst, NULL);

	if (!g_host || !g_overlay) {
		Line("SETUP: window creation failed");
		Line("VERDICT: NONE probe setup failed (window creation)");
		::printf("KEYS PROBE OK\n");
		return 0;
	}
	::SetLayeredWindowAttributes(g_overlay, 0, 200, LWA_ALPHA);

	::ShowWindow(g_host, SW_SHOWNORMAL);
	::ShowWindow(g_overlay, SW_SHOWNOACTIVATE);
	Pump(300);

	const bool fgOk = ForceForeground(g_host);
	Pump(300);
	if (!fgOk) {
		Line("SETUP: could not acquire foreground for probe host window (foreground lock); key-delivery results below are inconclusive");
	}

	// -----------------------------------------------------------------------
	// OPTION A — WH_KEYBOARD_LL hook while flag set, host focused
	// -----------------------------------------------------------------------
	g_hostKeys.Reset(); g_overlayKeys.Reset(); g_hookKeys.Reset();
	g_hook = ::SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyProc, inst, 0);
	const bool hookInstalled = (g_hook != NULL);
	g_hookFlag = true;
	bool aSent = false;
	if (hookInstalled && ForceForeground(g_host)) {
		aSent = SendTriple();
	}
	g_hookFlag = false;
	KeyCounts aHook = g_hookKeys;
	KeyCounts aHost = g_hostKeys;
	if (g_hook) { ::UnhookWindowsHookEx(g_hook); g_hook = NULL; }
	{
		char buf[512];
		::sprintf_s(buf,
			"OPTION A (WH_KEYBOARD_LL): installed=%s sent=%s; hook saw %s while host focused; host still received WM_KEYDOWN %s; hook is GLOBAL (system-wide, all processes; AV/latency/injected-flag risk)",
			YN(hookInstalled), YN(aSent), K(aHook).c_str(), K(aHost).c_str());
		Line(buf);
	}

	// -----------------------------------------------------------------------
	// OPTION B — temporarily clear WS_EX_NOACTIVATE + SetForegroundWindow
	// -----------------------------------------------------------------------
	g_hostKeys.Reset(); g_overlayKeys.Reset();
	g_hostActivateInactive = g_hostActivateActive = 0;
	g_hostNcActivateFalse = g_hostNcActivateTrue = 0;

	ForceForeground(g_host);
	Pump(200);
	// Baseline reached; now "selection becomes active":
	g_hostActivateInactive = g_hostActivateActive = 0;
	g_hostNcActivateFalse = g_hostNcActivateTrue = 0;

	const LONG_PTR savedEx = ::GetWindowLongPtrW(g_overlay, GWL_EXSTYLE);
	::SetWindowLongPtrW(g_overlay, GWL_EXSTYLE, savedEx & ~(LONG_PTR)WS_EX_NOACTIVATE);
	const bool overlayGotFg = ForceForeground(g_overlay);
	Pump(200);
	const int hostLostActivation = g_hostActivateInactive;
	const int hostNcLost = g_hostNcActivateFalse;
	bool bSent = false;
	if (overlayGotFg) bSent = SendTriple();
	KeyCounts bOverlay = g_overlayKeys;
	// Give focus back to host, restore NOACTIVATE.
	const bool hostBack = ForceForeground(g_host);
	Pump(200);
	::SetWindowLongPtrW(g_overlay, GWL_EXSTYLE, savedEx);
	const int hostReactivated = g_hostActivateActive;
	const bool flicker = (hostLostActivation > 0 && hostReactivated > 0);
	{
		char buf[512];
		::sprintf_s(buf,
			"OPTION B (clear NOACTIVATE + SetForegroundWindow): overlay took foreground=%s sent=%s; overlay received WM_KEYDOWN %s; host lost activation (WM_ACTIVATE inactive=%d, WM_NCACTIVATE false=%d); focus returned to host=%s (WM_ACTIVATE active=%d); activation flicker=%s",
			YN(overlayGotFg), YN(bSent), K(bOverlay).c_str(),
			hostLostActivation, hostNcLost, YN(hostBack), hostReactivated, YN(flicker));
		Line(buf);
	}

	// -----------------------------------------------------------------------
	// OPTION C — RegisterHotKey while "selection active", host focused
	// -----------------------------------------------------------------------
	g_hostKeys.Reset(); g_overlayKeys.Reset(); g_hotkeys.Reset();
	const bool cFg = ForceForeground(g_host);
	Pump(200);
	const bool regDel = ::RegisterHotKey(g_overlay, 1, 0, VK_DELETE) != FALSE;
	const bool regLeft = ::RegisterHotKey(g_overlay, 2, 0, VK_LEFT) != FALSE;
	const bool regRight = ::RegisterHotKey(g_overlay, 3, 0, VK_RIGHT) != FALSE;
	const bool allReg = regDel && regLeft && regRight;
	bool cSent = false;
	if (cFg) cSent = SendTriple();
	KeyCounts cHot = g_hotkeys;
	KeyCounts cHostWhileReg = g_hostKeys;
	::UnregisterHotKey(g_overlay, 1);
	::UnregisterHotKey(g_overlay, 2);
	::UnregisterHotKey(g_overlay, 3);
	Pump(100);
	// Recovery check: after unregister the focused host must see keys again.
	g_hostKeys.Reset();
	bool cSent2 = false;
	if (ForceForeground(g_host)) cSent2 = SendTriple();
	KeyCounts cHostAfter = g_hostKeys;
	const bool hotkeysArrived = cHot.AllSeen();
	const bool keysStolen = cSent && hotkeysArrived && cHostWhileReg.Total() == 0;
	const bool hostRecovered = cSent2 && cHostAfter.AllSeen();
	{
		char buf[512];
		::sprintf_s(buf,
			"OPTION C (RegisterHotKey while selection active): registered del/left/right=%s/%s/%s sent=%s; overlay WM_HOTKEY without focus %s; focused host WM_KEYDOWN while registered %s (keys stolen from host=%s); after unregister host received %s (recovery=%s); NOTE while registered these keys are stolen system-wide, so registration must be scoped to selection-active + host-foreground",
			YN(regDel), YN(regLeft), YN(regRight), YN(cSent),
			K(cHot).c_str(), K(cHostWhileReg).c_str(), YN(keysStolen),
			K(cHostAfter).c_str(), YN(hostRecovered));
		Line(buf);
	}

	Line("CAVEAT: keys synthesized via SendInput within one process (LLKHF_INJECTED set); real keyboard input may differ under UIPI/elevation mismatch or when hardware-only filters are present");

	// -----------------------------------------------------------------------
	// Verdict
	// -----------------------------------------------------------------------
	const bool hookWorks = hookInstalled && aSent && aHook.AllSeen();
	const bool activateWorks = overlayGotFg && bSent && bOverlay.AllSeen() && hostBack;

	std::string verdict;
	if (allReg && hotkeysArrived && hostRecovered) {
		verdict = "VERDICT: HOTKEY WM_HOTKEY arrives without focus and unregister cleanly returns keys to the host; scoped registration (selection active + host foreground) avoids global hooks and activation flicker";
	} else if (hookWorks) {
		verdict = "VERDICT: HOOK low-level hook sees Delete/arrows while host stays focused; global scope is the accepted risk since hotkey/activation paths failed";
	} else if (activateWorks) {
		verdict = "VERDICT: ACTIVATE overlay can take real focus after clearing NOACTIVATE, at the cost of visible host activation flicker; only viable path left";
	} else {
		verdict = "VERDICT: NONE no option delivered keys reliably in this environment; keyboard unit ships cursors only";
	}
	Line(verdict);

	::DestroyWindow(g_overlay);
	::DestroyWindow(g_host);
	g_out.close();
	::printf("KEYS PROBE OK\n");
	return 0;
}
