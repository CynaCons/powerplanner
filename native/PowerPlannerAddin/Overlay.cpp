#include "pch.h"
#include "Overlay.h"
#include "GanttBuilder.h"
#include <string>

// ---- module state ----------------------------------------------------------

namespace {
const wchar_t* kClass = L"PowerPlannerOverlay";
const COLORREF KEY = RGB(255, 0, 255);     // transparent color key
const COLORREF ACCENT = RGB(26, 115, 232); // Material primary blue
const COLORREF HANDLE_INNER = RGB(138, 180, 248);
const int INFL = 5;                         // frame inset from shape edge (px)
const int BADGE_H = 20;                     // badge strip height (px)

PowerPoint::_ApplicationPtr g_app;
HWND     g_hwnd = NULL;
HINSTANCE g_inst = NULL;
UINT_PTR g_timer = 0;
bool     g_shown = false;
std::string g_lastKind;       // last shown PP_KIND (to log on change)
std::wstring g_badge = L"PowerPlanner";

// Forward
void PaintOverlay(HDC hdc, const RECT& rc);

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_PAINT) {
		PAINTSTRUCT ps;
		HDC hdc = ::BeginPaint(hwnd, &ps);
		RECT rc; ::GetClientRect(hwnd, &rc);
		PaintOverlay(hdc, rc);
		::EndPaint(hwnd, &ps);
		return 0;
	}
	return ::DefWindowProcW(hwnd, msg, wp, lp);
}

void EnsureWindow() {
	if (g_hwnd) return;
	WNDCLASSEXW wc = { sizeof(wc) };
	wc.lpfnWndProc = OverlayWndProc;
	wc.hInstance = g_inst;
	wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = kClass;
	::RegisterClassExW(&wc);

	g_hwnd = ::CreateWindowExW(
		WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
		kClass, L"", WS_POPUP,
		0, 0, 10, 10, NULL, NULL, g_inst, NULL);
	if (g_hwnd) ::SetLayeredWindowAttributes(g_hwnd, KEY, 0, LWA_COLORKEY);
}

void FillSquare(HDC hdc, int cx, int cy, int r, HBRUSH fill, HPEN borderPen, HPEN innerPen) {
	RECT s = { cx - r, cy - r, cx + r + 1, cy + r + 1 };
	HGDIOBJ ob = ::SelectObject(hdc, borderPen);
	HGDIOBJ of = ::SelectObject(hdc, fill);
	::RoundRect(hdc, s.left, s.top, s.right, s.bottom, 3, 3);
	::SelectObject(hdc, innerPen);
	::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
	::RoundRect(hdc, s.left + 1, s.top + 1, s.right - 1, s.bottom - 1, 2, 2);
	::SelectObject(hdc, ob);
	::SelectObject(hdc, of);
}

void PaintOverlay(HDC hdc, const RECT& rc) {
	const int W = rc.right, H = rc.bottom;
	// transparent background
	HBRUSH keyBrush = ::CreateSolidBrush(KEY);
	::FillRect(hdc, &rc, keyBrush);
	::DeleteObject(keyBrush);

	// the shape frame lives below the badge strip
	RECT frame = { INFL, BADGE_H + INFL, W - INFL, H - INFL };

	HPEN pen = ::CreatePen(PS_SOLID, 2, ACCENT);
	HGDIOBJ oldPen = ::SelectObject(hdc, pen);
	HGDIOBJ oldBr = ::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
	::RoundRect(hdc, frame.left, frame.top, frame.right, frame.bottom, 6, 6);
	::SelectObject(hdc, oldBr);
	::SelectObject(hdc, oldPen);
	::DeleteObject(pen);

	// 8 handles (white fill, accent border)
	HBRUSH white = ::CreateSolidBrush(RGB(255, 255, 255));
	HPEN hpen = ::CreatePen(PS_SOLID, 1, ACCENT);
	HPEN innerPen = ::CreatePen(PS_SOLID, 1, HANDLE_INNER);
	int mx = (frame.left + frame.right) / 2, my = (frame.top + frame.bottom) / 2;
	int xs[3] = { frame.left, mx, frame.right };
	int ys[3] = { frame.top, my, frame.bottom };
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j) {
			if (i == 1 && j == 1) continue;
			FillSquare(hdc, xs[i], ys[j], 3, white, hpen, innerPen);
		}
	::DeleteObject(white);
	::DeleteObject(hpen);
	::DeleteObject(innerPen);

	// badge: filled Material chip with white label at top-left
	int bw = 96, bh = BADGE_H - 4;
	RECT badge = { INFL, 2, INFL + bw, 2 + bh };
	HBRUSH abr = ::CreateSolidBrush(ACCENT);
	HGDIOBJ ob = ::SelectObject(hdc, abr);
	HGDIOBJ op = ::SelectObject(hdc, ::GetStockObject(NULL_PEN));
	::RoundRect(hdc, badge.left, badge.top, badge.right, badge.bottom, 8, 8);
	::SelectObject(hdc, ob);
	::SelectObject(hdc, op);
	::DeleteObject(abr);
	HFONT font = ::CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
		DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
	HGDIOBJ oldFont = font ? ::SelectObject(hdc, font) : NULL;
	int oldBk = ::SetBkMode(hdc, TRANSPARENT);
	COLORREF oldText = ::SetTextColor(hdc, RGB(255, 255, 255));
	::DrawTextW(hdc, g_badge.c_str(), -1, &badge, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	::SetTextColor(hdc, oldText);
	::SetBkMode(hdc, oldBk);
	if (oldFont) ::SelectObject(hdc, oldFont);
	if (font) ::DeleteObject(font);
}

void HideOverlay() {
	if (g_shown && g_hwnd) { ::ShowWindow(g_hwnd, SW_HIDE); g_shown = false; }
}

void ShowOverlayForScreenRect(int x1, int y1, int x2, int y2) {
	EnsureWindow();
	if (!g_hwnd) return;
	int wx = x1 - INFL, wy = y1 - INFL - BADGE_H;
	int ww = (x2 - x1) + INFL * 2, wh = (y2 - y1) + INFL * 2 + BADGE_H;
	if (ww < 24) ww = 24;
	if (wh < 24) wh = 24;
	::SetWindowPos(g_hwnd, HWND_TOPMOST, wx, wy, ww, wh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	::InvalidateRect(g_hwnd, NULL, TRUE);
	::UpdateWindow(g_hwnd);
	g_shown = true;
}

// Append a debug line (shared with Connect's log).
void OvLog(const wchar_t* msg) {
	wchar_t path[MAX_PATH];
	DWORD n = ::GetTempPathW(MAX_PATH, path);
	if (!n || n > MAX_PATH) return;
	::wcscat_s(path, MAX_PATH, L"powerplanner-addin.log");
	HANDLE h = ::CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return;
	std::wstring line = std::wstring(L"[overlay] ") + msg + L"\r\n";
	DWORD w = 0; ::WriteFile(h, line.c_str(), (DWORD)(line.size() * sizeof(wchar_t)), &w, NULL);
	::CloseHandle(h);
}

// Poll the selection; show/position handles or hide.
void Tick() {
	if (!g_app) { HideOverlay(); return; }
	try {
		PowerPoint::DocumentWindowPtr win = g_app->GetActiveWindow();
		if (!win) { HideOverlay(); return; }
		PowerPoint::SelectionPtr sel = win->GetSelection();
		if (!sel || sel->GetType() != PowerPoint::ppSelectionShapes) { HideOverlay(); return; }
		PowerPoint::ShapeRangePtr sr = sel->GetShapeRange();
		if (!sr || sr->GetCount() < 1) { HideOverlay(); return; }
		PowerPoint::ShapePtr sh = sr->Item(_variant_t(1L));
		_bstr_t kind = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
		if (!kind.length()) { HideOverlay(); return; }

		bool mouseUp = !(::GetKeyState(VK_LBUTTON) & 0x8000);
		if (mouseUp) {
			std::string kStr = (const char*)kind;
			if (kStr == "TASK" || kStr == "MILESTONE") {
				bool changed = false;
				ReflowFromSlide(g_app, &changed);
				if (changed) {
					return;
				}
			}
		}

		float left = sh->GetLeft(), top = sh->GetTop(), w = sh->GetWidth(), h = sh->GetHeight();
		int x1 = win->PointsToScreenPixelsX(left);
		int y1 = win->PointsToScreenPixelsY(top);
		int x2 = win->PointsToScreenPixelsX(left + w);
		int y2 = win->PointsToScreenPixelsY(top + h);
		ShowOverlayForScreenRect(x1, y1, x2, y2);

		std::string k = (const char*)kind;
		if (k != g_lastKind) {
			g_lastKind = k;
			wchar_t buf[160];
			::swprintf_s(buf, 160, L"shown for PP_KIND=%hs at screen (%d,%d)-(%d,%d)", k.c_str(), x1, y1, x2, y2);
			OvLog(buf);
		}
	} catch (const _com_error&) {
		HideOverlay();
	}
}

void CALLBACK TimerProc(HWND, UINT, UINT_PTR, DWORD) { Tick(); }

} // namespace

void OverlayStart(IDispatch* app) {
	g_inst = (HINSTANCE)::GetModuleHandleW(NULL);
	g_app = app;  // QI to _Application
	EnsureWindow();
	g_timer = ::SetTimer(NULL, 0, 150, TimerProc);
	OvLog(L"started");
}

void OverlayStop() {
	if (g_timer) { ::KillTimer(NULL, g_timer); g_timer = 0; }
	if (g_hwnd) { ::DestroyWindow(g_hwnd); g_hwnd = NULL; }
	g_app = nullptr;
	g_shown = false;
	g_lastKind.clear();
}

HWND OverlayHwnd() { return g_hwnd; }
