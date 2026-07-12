#include "ThemeMenu.h"
#include "GanttTheme.h"
#include "OverlayGeometry.h"

#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

namespace {

const wchar_t* kMenuClass = L"PowerPlannerThemeMenu";

HINSTANCE g_inst = NULL;
ULONG_PTR g_gdiplusToken = 0;
int (*g_scale)(int) = nullptr;

int S(int px) { return g_scale ? g_scale(px) : px; }
Gdiplus::REAL SF(float px) { return (Gdiplus::REAL)S((int)px); }

HWND g_menuHwnd = NULL;
HWND g_flyoutHwnd = NULL;
HWND g_ownerHwnd = NULL;

HDC g_menuDc = NULL;
HBITMAP g_menuBmp = NULL;
HGDIOBJ g_menuOld = NULL;
void* g_menuBits = nullptr;
int g_menuW = 0, g_menuH = 0;

HDC g_flyDc = NULL;
HBITMAP g_flyBmp = NULL;
HGDIOBJ g_flyOld = NULL;
void* g_flyBits = nullptr;
int g_flyW = 0, g_flyH = 0;

enum class RowKind { Separator, Action, Submenu };

struct MenuRow {
	RowKind kind = RowKind::Action;
	int cmdId = 0;
	std::string label;
	bool enabled = true;
	bool danger = false;
	std::string submenuKey;
};

std::vector<MenuRow> g_rows;
std::map<std::string, std::vector<MenuRow>> g_submenus;
std::string g_openSubmenu;
int g_hoverRow = -1;
int g_hoverFlyoutRow = -1;
int g_menuResult = 0;
bool g_menuDone = false;
bool g_menuModal = true;
HHOOK g_mouseHook = NULL;

struct MenuHit {
	int rowIndex;
	RECT rc;
	bool enabled;
	int cmdId;
};
std::vector<MenuHit> g_menuHits;
std::vector<MenuHit> g_flyoutHits;

bool IsDangerCmd(int cmd) {
	return cmd == HtCmd_Delete || cmd == HtCmd_DeleteRow;
}

void BuildMenuModel(const std::vector<HtMenuItem>& items) {
	g_rows.clear();
	g_submenus.clear();
	for (const auto& item : items) {
		if (!item.submenu.empty()) {
			MenuRow row;
			row.kind = RowKind::Action;
			row.cmdId = item.cmdId;
			row.label = item.label;
			row.enabled = item.enabled;
			row.danger = IsDangerCmd(item.cmdId);
			g_submenus[item.submenu].push_back(row);
		}
	}
	for (const auto& item : items) {
		if (item.separatorBefore) {
			MenuRow sep;
			sep.kind = RowKind::Separator;
			g_rows.push_back(sep);
		}
		if (!item.submenu.empty()) continue;
		if (item.cmdId == HtCmd_None) {
			if (g_submenus.count(item.label)) {
				MenuRow trig;
				trig.kind = RowKind::Submenu;
				trig.label = item.label;
				trig.submenuKey = item.label;
				trig.enabled = true;
				g_rows.push_back(trig);
			}
			continue;
		}
		MenuRow row;
		row.kind = RowKind::Action;
		row.cmdId = item.cmdId;
		row.label = item.label;
		row.enabled = item.enabled;
		row.danger = IsDangerCmd(item.cmdId);
		g_rows.push_back(row);
	}
}

int MenuRowHeight() { return S(32); }
int MenuPadX() { return S(12); }
int MenuMinWidth() { return S(200); }
int MenuShadowInset() { return S(4); }
int MenuCornerRadius() { return S(8); }
int MenuSepHeight() { return S(9); }

int MeasureMenuWidth(Gdiplus::Graphics& g, Gdiplus::Font& font, const std::vector<MenuRow>& rows) {
	int maxW = MenuMinWidth();
	for (const auto& row : rows) {
		if (row.kind == RowKind::Separator) continue;
		std::wstring wl(row.label.begin(), row.label.end());
		int w = (int)std::ceil(MeasureTextW(g, font, wl.c_str())) + MenuPadX() * 2 + S(28);
		if (row.kind == RowKind::Submenu) w += S(16);
		if (w > maxW) maxW = w;
	}
	return maxW;
}

int MeasureMenuHeight(const std::vector<MenuRow>& rows) {
	int h = MenuShadowInset() * 2;
	for (const auto& row : rows) {
		if (row.kind == RowKind::Separator) h += MenuSepHeight();
		else h += MenuRowHeight();
	}
	return h;
}

void DrawAppBarChevronMenu(Gdiplus::Graphics& g, int cx, int cy, bool up, unsigned long color) {
	using namespace Gdiplus;
	const int half = S(4);
	Pen pen(GpToken(255, color), SF(1.5f));
	if (up) {
		g.DrawLine(&pen, cx - half, cy + half / 2, cx, cy - half / 2);
		g.DrawLine(&pen, cx, cy - half / 2, cx + half, cy + half / 2);
	} else {
		g.DrawLine(&pen, cx - half, cy - half / 2, cx, cy + half / 2);
		g.DrawLine(&pen, cx, cy + half / 2, cx + half, cy - half / 2);
	}
}

bool EnsureMenuBuffer(HDC* dc, HBITMAP* bmp, HGDIOBJ* old, void** bits, int* w, int* h, int wantW, int wantH) {
	if (*w == wantW && *h == wantH && *dc) return true;
	if (*dc) {
		if (*old) ::SelectObject(*dc, *old);
		if (*bmp) ::DeleteObject(*bmp);
		::DeleteDC(*dc);
		*dc = NULL; *bmp = NULL; *old = NULL; *bits = nullptr;
	}
	*w = wantW; *h = wantH;
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = wantW;
	bi.bmiHeader.biHeight = -wantH;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	*dc = ::CreateCompatibleDC(NULL);
	*bmp = ::CreateDIBSection(*dc, &bi, DIB_RGB_COLORS, bits, NULL, 0);
	if (!*dc || !*bmp) return false;
	*old = ::SelectObject(*dc, *bmp);
	return true;
}

void FreeMenuBuffer(HDC* dc, HBITMAP* bmp, HGDIOBJ* old, int* w, int* h) {
	if (*dc) {
		if (*old) ::SelectObject(*dc, *old);
		if (*bmp) ::DeleteObject(*bmp);
		::DeleteDC(*dc);
	}
	*dc = NULL; *bmp = NULL; *old = NULL; *w = *h = 0;
}

void PaintMenuSurface(Gdiplus::Graphics& g, int W, int H, const std::vector<MenuRow>& rows,
	std::vector<MenuHit>& hits, int hoverRow, const std::string& openSubmenuKey) {
	using namespace Gdiplus;
	hits.clear();
	const int inset = MenuShadowInset();
	const int containerW = W - inset * 2;
	const int containerH = H - inset * 2;
	const int containerX = inset;
	const int containerY = inset;

	{
		GraphicsPath shadowPath;
		AddRoundRect(shadowPath, (REAL)(containerX + S(1)), (REAL)(containerY + S(2)),
			(REAL)containerW, (REAL)containerH, (REAL)MenuCornerRadius());
		SolidBrush shadow(GpToken(40, gt::ink3));
		g.FillPath(&shadow, &shadowPath);
	}
	GraphicsPath containerPath;
	AddRoundRect(containerPath, (REAL)containerX, (REAL)containerY,
		(REAL)containerW, (REAL)containerH, (REAL)MenuCornerRadius());
	SolidBrush surface(GpToken(255, gt::surface));
	g.FillPath(&surface, &containerPath);
	Pen border(GpToken(255, gt::outline), 1.0f);
	g.DrawPath(&border, &containerPath);

	Gdiplus::Font* font = MakeAppBarFont(SF(12.0f), FontStyleRegular);
	StringFormat sf;
	sf.SetLineAlignment(StringAlignmentCenter);

	int y = containerY;
	for (size_t i = 0; i < rows.size(); ++i) {
		const MenuRow& row = rows[i];
		if (row.kind == RowKind::Separator) {
			int sepY = y + MenuSepHeight() / 2;
			Pen hair(GpToken(255, gt::outline), 1.0f);
			g.DrawLine(&hair, containerX + S(8), sepY, containerX + containerW - S(8), sepY);
			y += MenuSepHeight();
			continue;
		}
		RECT rc = { containerX, y, containerX + containerW, y + MenuRowHeight() };
		bool hovered = ((int)i == hoverRow);
		bool submenuOpen = (row.kind == RowKind::Submenu && row.submenuKey == openSubmenuKey);
		unsigned long fillRgb = 0;
		unsigned long textRgb = gt::ink;
		if (!row.enabled) {
			textRgb = gt::ink3;
		} else if (hovered || submenuOpen) {
			if (row.danger) { fillRgb = gt::dangerSoft; textRgb = gt::deadline; }
			else { fillRgb = gt::primarySoft; textRgb = gt::primary; }
		} else if (row.danger) {
			textRgb = gt::deadline;
		}
		if (fillRgb != 0) {
			GraphicsPath pill;
			AddRoundRect(pill, (REAL)rc.left + S(4), (REAL)rc.top + S(2),
				(REAL)(rc.right - rc.left - S(8)), (REAL)(rc.bottom - rc.top - S(4)), (REAL)S(6));
			SolidBrush fill(GpToken(255, fillRgb));
			g.FillPath(&fill, &pill);
		}
		std::wstring wl(row.label.begin(), row.label.end());
		BYTE textAlpha = row.enabled ? 255 : 120;
		SolidBrush textBrush(GpToken(textAlpha, textRgb));
		g.DrawString(wl.c_str(), -1, font, RectF((REAL)(rc.left + MenuPadX()), (REAL)rc.top,
			(REAL)(rc.right - rc.left - MenuPadX() * 2 - S(16)), (REAL)(rc.bottom - rc.top)), &sf, &textBrush);
		if (row.kind == RowKind::Submenu) {
			int cx = rc.right - S(16);
			int cy = (rc.top + rc.bottom) / 2;
			DrawAppBarChevronMenu(g, cx, cy, false, textRgb);
		}
		if (row.kind == RowKind::Action && row.enabled) {
			hits.push_back({ (int)i, rc, true, row.cmdId });
		} else if (row.kind == RowKind::Submenu && row.enabled) {
			hits.push_back({ (int)i, rc, true, 0 });
		}
		y += MenuRowHeight();
	}
	delete font;
}

void RenderLayered(HWND hwnd, HDC srcDc, int w, int h) {
	POINT dst = {};
	RECT wr{};
	::GetWindowRect(hwnd, &wr);
	dst.x = wr.left; dst.y = wr.top;
	SIZE size = { w, h };
	POINT src = { 0, 0 };
	BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
	::UpdateLayeredWindow(hwnd, NULL, &dst, &size, srcDc, &src, 0, &bf, ULW_ALPHA);
}

void RenderMainMenu() {
	if (!g_menuHwnd || !g_gdiplusToken) return;
	Gdiplus::Font measureFont(L"Segoe UI", SF(12.0f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::Bitmap measureBmp(1, 1, PixelFormat32bppPARGB);
	Gdiplus::Graphics measureG(&measureBmp);
	int contentW = MeasureMenuWidth(measureG, measureFont, g_rows);
	int contentH = MeasureMenuHeight(g_rows);
	int w = contentW + MenuShadowInset() * 2;
	int h = contentH + MenuShadowInset() * 2;
	if (!EnsureMenuBuffer(&g_menuDc, &g_menuBmp, &g_menuOld, &g_menuBits, &g_menuW, &g_menuH, w, h)) return;
	Gdiplus::Bitmap surface(g_menuW, g_menuH, PixelFormat32bppPARGB);
	Gdiplus::Graphics g(&surface);
	g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
	PaintMenuSurface(g, w, h, g_rows, g_menuHits, g_hoverRow, g_openSubmenu);
	Gdiplus::Rect dest(0, 0, w, h);
	Gdiplus::BitmapData data{};
	if (surface.LockBits(&dest, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data) == Gdiplus::Ok) {
		for (int y = 0; y < h; ++y) {
			BYTE* dst = (BYTE*)g_menuBits + y * w * 4;
			const BYTE* src = (const BYTE*)data.Scan0 + y * data.Stride;
			for (int x = 0; x < w; ++x) {
				BYTE a = src[x * 4 + 3];
				dst[x * 4 + 0] = (BYTE)((src[x * 4 + 2] * a + 127) / 255);
				dst[x * 4 + 1] = (BYTE)((src[x * 4 + 1] * a + 127) / 255);
				dst[x * 4 + 2] = (BYTE)((src[x * 4 + 0] * a + 127) / 255);
				dst[x * 4 + 3] = a;
			}
		}
		surface.UnlockBits(&data);
	}
	RenderLayered(g_menuHwnd, g_menuDc, w, h);
	RECT wr{};
	::GetWindowRect(g_menuHwnd, &wr);
	::SetWindowPos(g_menuHwnd, NULL, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void RenderFlyout() {
	if (!g_flyoutHwnd || g_openSubmenu.empty()) return;
	auto it = g_submenus.find(g_openSubmenu);
	if (it == g_submenus.end()) {
		::ShowWindow(g_flyoutHwnd, SW_HIDE);
		return;
	}
	const std::vector<MenuRow>& rows = it->second;
	Gdiplus::Font measureFont(L"Segoe UI", SF(12.0f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::Bitmap measureBmp(1, 1, PixelFormat32bppPARGB);
	Gdiplus::Graphics measureG(&measureBmp);
	int contentW = MeasureMenuWidth(measureG, measureFont, rows);
	int contentH = MeasureMenuHeight(rows);
	int w = contentW + MenuShadowInset() * 2;
	int h = contentH + MenuShadowInset() * 2;
	if (!EnsureMenuBuffer(&g_flyDc, &g_flyBmp, &g_flyOld, &g_flyBits, &g_flyW, &g_flyH, w, h)) return;
	Gdiplus::Bitmap surface(g_flyW, g_flyH, PixelFormat32bppPARGB);
	Gdiplus::Graphics g(&surface);
	g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
	PaintMenuSurface(g, w, h, rows, g_flyoutHits, g_hoverFlyoutRow, "");
	Gdiplus::Rect dest(0, 0, w, h);
	Gdiplus::BitmapData data{};
	if (surface.LockBits(&dest, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data) == Gdiplus::Ok) {
		for (int y = 0; y < h; ++y) {
			BYTE* dst = (BYTE*)g_flyBits + y * w * 4;
			const BYTE* src = (const BYTE*)data.Scan0 + y * data.Stride;
			for (int x = 0; x < w; ++x) {
				BYTE a = src[x * 4 + 3];
				dst[x * 4 + 0] = (BYTE)((src[x * 4 + 2] * a + 127) / 255);
				dst[x * 4 + 1] = (BYTE)((src[x * 4 + 1] * a + 127) / 255);
				dst[x * 4 + 2] = (BYTE)((src[x * 4 + 0] * a + 127) / 255);
				dst[x * 4 + 3] = a;
			}
		}
		surface.UnlockBits(&data);
	}
	RECT menuWr{};
	::GetWindowRect(g_menuHwnd, &menuWr);
	int fx = menuWr.right - S(2);
	int fy = menuWr.top + MenuShadowInset();
	if (g_hoverRow >= 0 && g_hoverRow < (int)g_rows.size()) {
		int rowY = MenuShadowInset();
		for (int i = 0; i < g_hoverRow; ++i) {
			if (g_rows[i].kind == RowKind::Separator) rowY += MenuSepHeight();
			else rowY += MenuRowHeight();
		}
		fy = menuWr.top + rowY;
	}
	HMONITOR mon = ::MonitorFromRect(&menuWr, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi = { sizeof(mi) };
	if (::GetMonitorInfoW(mon, &mi)) {
		if (fx + w > mi.rcWork.right) fx = menuWr.left - w + S(2);
		if (fy + h > mi.rcWork.bottom) fy = mi.rcWork.bottom - h;
	}
	::SetWindowPos(g_flyoutHwnd, HWND_TOP, fx, fy, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	RenderLayered(g_flyoutHwnd, g_flyDc, w, h);
}

bool PointInAnyMenu(POINT screenPt) {
	auto inWnd = [&](HWND hwnd) {
		if (!hwnd || !::IsWindowVisible(hwnd)) return false;
		RECT wr{};
		::GetWindowRect(hwnd, &wr);
		return ::PtInRect(&wr, screenPt) != 0;
	};
	return inWnd(g_menuHwnd) || inWnd(g_flyoutHwnd);
}

void FinishMenu(int cmd) {
	g_menuResult = cmd;
	g_menuDone = true;
	if (g_mouseHook) {
		::UnhookWindowsHookEx(g_mouseHook);
		g_mouseHook = NULL;
	}
	if (g_flyoutHwnd) ::ShowWindow(g_flyoutHwnd, SW_HIDE);
	if (g_menuHwnd) ::ShowWindow(g_menuHwnd, SW_HIDE);
	g_openSubmenu.clear();
	g_hoverRow = g_hoverFlyoutRow = -1;
	if (g_ownerHwnd) ::PostMessageW(g_ownerHwnd, WM_NULL, 0, 0);
}

LRESULT CALLBACK MouseHookProc(int code, WPARAM wp, LPARAM lp) {
	if (code >= 0 && !g_menuDone && (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN)) {
		MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lp;
		if (!PointInAnyMenu(ms->pt)) FinishMenu(0);
	}
	return ::CallNextHookEx(g_mouseHook, code, wp, lp);
}

int HitTestMenu(POINT clientPt, const std::vector<MenuHit>& hits) {
	for (const auto& h : hits) {
		if (h.enabled && ::PtInRect(&h.rc, clientPt)) return h.rowIndex;
	}
	return -1;
}

int CmdForFlyoutRow(int rowIndex) {
	auto it = g_submenus.find(g_openSubmenu);
	if (it == g_submenus.end() || rowIndex < 0 || rowIndex >= (int)it->second.size()) return 0;
	return it->second[(size_t)rowIndex].enabled ? it->second[(size_t)rowIndex].cmdId : 0;
}

LRESULT CALLBACK FlyoutWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_NCHITTEST) return HTCLIENT;
	if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
	if (msg == WM_MOUSEMOVE) {
		POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		int row = HitTestMenu(pt, g_flyoutHits);
		if (row != g_hoverFlyoutRow) {
			g_hoverFlyoutRow = row;
			RenderFlyout();
		}
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
		::TrackMouseEvent(&tme);
		return 0;
	}
	if (msg == WM_MOUSELEAVE) {
		g_hoverFlyoutRow = -1;
		RenderFlyout();
		return 0;
	}
	if (msg == WM_LBUTTONUP) {
		POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		int row = HitTestMenu(pt, g_flyoutHits);
		int cmd = CmdForFlyoutRow(row);
		if (cmd > 0) FinishMenu(cmd);
		return 0;
	}
	return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK MenuWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_NCHITTEST) return HTCLIENT;
	if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
	if (msg == WM_KEYDOWN) {
		if (wp == VK_ESCAPE) { FinishMenu(0); return 0; }
		if (wp == VK_RETURN && g_hoverRow >= 0 && g_hoverRow < (int)g_rows.size()) {
			const MenuRow& row = g_rows[(size_t)g_hoverRow];
			if (row.kind == RowKind::Action && row.enabled) FinishMenu(row.cmdId);
			else if (row.kind == RowKind::Submenu) {
				g_openSubmenu = row.submenuKey;
				RenderFlyout();
			}
			return 0;
		}
		if (wp == VK_UP || wp == VK_DOWN) {
			int next = g_hoverRow;
			int delta = (wp == VK_DOWN) ? 1 : -1;
			for (int tries = 0; tries < (int)g_rows.size() + 2; ++tries) {
				next += delta;
				if (next < 0) next = (int)g_rows.size() - 1;
				if (next >= (int)g_rows.size()) next = 0;
				if (g_rows[(size_t)next].kind != RowKind::Separator) break;
			}
			g_hoverRow = next;
			const MenuRow& row = g_rows[(size_t)g_hoverRow];
			if (row.kind == RowKind::Submenu) {
				g_openSubmenu = row.submenuKey;
				RenderFlyout();
			} else {
				g_openSubmenu.clear();
				if (g_flyoutHwnd) ::ShowWindow(g_flyoutHwnd, SW_HIDE);
			}
			RenderMainMenu();
			return 0;
		}
	}
	if (msg == WM_MOUSEMOVE) {
		POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		int row = HitTestMenu(pt, g_menuHits);
		if (row != g_hoverRow) {
			g_hoverRow = row;
			if (row >= 0 && row < (int)g_rows.size() && g_rows[(size_t)row].kind == RowKind::Submenu) {
				g_openSubmenu = g_rows[(size_t)row].submenuKey;
				RenderFlyout();
			} else {
				g_openSubmenu.clear();
				if (g_flyoutHwnd) ::ShowWindow(g_flyoutHwnd, SW_HIDE);
			}
			RenderMainMenu();
		}
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
		::TrackMouseEvent(&tme);
		return 0;
	}
	if (msg == WM_MOUSELEAVE) {
		g_hoverRow = -1;
		RenderMainMenu();
		return 0;
	}
	if (msg == WM_LBUTTONUP) {
		POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		int row = HitTestMenu(pt, g_menuHits);
		if (row >= 0 && row < (int)g_rows.size()) {
			const MenuRow& r = g_rows[(size_t)row];
			if (r.kind == RowKind::Action && r.enabled) FinishMenu(r.cmdId);
			else if (r.kind == RowKind::Submenu) {
				g_openSubmenu = r.submenuKey;
				RenderFlyout();
			}
		}
		return 0;
	}
	return ::DefWindowProcW(hwnd, msg, wp, lp);
}

void EnsureMenuWindows() {
	if (!g_menuHwnd) {
		WNDCLASSEXW wc = { sizeof(wc) };
		wc.lpfnWndProc = MenuWndProc;
		wc.hInstance = g_inst;
		wc.lpszClassName = kMenuClass;
		::RegisterClassExW(&wc);
		g_menuHwnd = ::CreateWindowExW(
			WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
			kMenuClass, L"", WS_POPUP,
			0, 0, 10, 10, NULL, NULL, g_inst, NULL);
	}
	if (!g_flyoutHwnd) {
		WNDCLASSEXW wc = { sizeof(wc) };
		wc.lpfnWndProc = FlyoutWndProc;
		wc.hInstance = g_inst;
		wc.lpszClassName = L"PowerPlannerThemeMenuFlyout";
		::RegisterClassExW(&wc);
		g_flyoutHwnd = ::CreateWindowExW(
			WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
			L"PowerPlannerThemeMenuFlyout", L"", WS_POPUP,
			0, 0, 10, 10, NULL, NULL, g_inst, NULL);
	}
}

void ClampMenuPosition(POINT& screenPt, int menuW, int menuH) {
	HMONITOR mon = ::MonitorFromPoint(screenPt, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi = { sizeof(mi) };
	if (!::GetMonitorInfoW(mon, &mi)) return;
	if (screenPt.x + menuW > mi.rcWork.right) screenPt.x = mi.rcWork.right - menuW;
	if (screenPt.y + menuH > mi.rcWork.bottom) screenPt.y = mi.rcWork.bottom - menuH;
	if (screenPt.x < mi.rcWork.left) screenPt.x = mi.rcWork.left;
	if (screenPt.y < mi.rcWork.top) screenPt.y = mi.rcWork.top;
}

int PumpUntilMenuClosed() {
	g_menuDone = false;
	MSG msg;
	while (!g_menuDone && g_menuHwnd && ::IsWindow(g_menuHwnd)) {
		while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				::PostQuitMessage((int)msg.wParam);
				return 0;
			}
			::TranslateMessage(&msg);
			::DispatchMessageW(&msg);
		}
		if (!g_menuDone) ::Sleep(5);
	}
	return g_menuResult;
}

} // namespace

void ThemeMenu_Init(HINSTANCE inst, ULONG_PTR gdiplusToken, int (*scalePx)(int)) {
	g_inst = inst;
	g_gdiplusToken = gdiplusToken;
	g_scale = scalePx;
}

int ThemeMenu_Show(const std::vector<HtMenuItem>& items, POINT screenPt, HWND ownerHwnd, bool block) {
	if (items.empty()) return 0;
	BuildMenuModel(items);
	if (g_rows.empty()) return 0;

	EnsureMenuWindows();
	if (!g_menuHwnd) return 0;

	g_ownerHwnd = ownerHwnd;
	g_menuModal = block;
	g_menuResult = 0;
	g_hoverRow = g_hoverFlyoutRow = -1;
	g_openSubmenu.clear();

	Gdiplus::Font measureFont(L"Segoe UI", SF(12.0f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::Bitmap measureBmp(1, 1, PixelFormat32bppPARGB);
	Gdiplus::Graphics measureG(&measureBmp);
	int w = MeasureMenuWidth(measureG, measureFont, g_rows) + MenuShadowInset() * 2;
	int h = MeasureMenuHeight(g_rows) + MenuShadowInset() * 2;
	ClampMenuPosition(screenPt, w, h);

	::SetWindowPos(g_menuHwnd, HWND_TOPMOST, screenPt.x, screenPt.y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	RenderMainMenu();

	if (!g_mouseHook)
		g_mouseHook = ::SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, g_inst, 0);

	if (!block) return 0;
	if (ownerHwnd) ::SetForegroundWindow(ownerHwnd);
	return PumpUntilMenuClosed();
}

void ThemeMenu_Dismiss(int resultCmd) {
	if (resultCmd > 0) g_menuResult = resultCmd;
	FinishMenu(g_menuResult);
}

bool ThemeMenu_IsVisible() {
	return g_menuHwnd && ::IsWindow(g_menuHwnd) && ::IsWindowVisible(g_menuHwnd);
}

HWND ThemeMenu_Hwnd() { return g_menuHwnd; }
HWND ThemeMenu_FlyoutHwnd() { return g_flyoutHwnd; }
