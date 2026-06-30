#include "pch.h"
#include "Overlay.h"
#include "GanttBuilder.h"
#include "GanttJson.h"
#include "GanttOps.h"
#include <algorithm>
#include <string>

// ---- module state ----------------------------------------------------------

namespace {
const wchar_t* kClass = L"PowerPlannerOverlay";
const COLORREF KEY = RGB(255, 0, 255);     // transparent color key
const COLORREF ACCENT = RGB(26, 115, 232); // Material primary blue
const COLORREF HANDLE_INNER = RGB(138, 180, 248);
const COLORREF SURFACE = RGB(255, 255, 255);
const COLORREF SURFACE_VARIANT = RGB(241, 243, 244);
const COLORREF TEXT = RGB(60, 64, 67);
const int INFL = 5;                         // frame inset from shape edge (px)
const int BADGE_H = 20;                     // badge strip height (px)
const int TOOLBAR_H = 28;                   // floating action toolbar height (px)
const int BUTTON_COUNT = 4;

PowerPoint::_ApplicationPtr g_app;
HWND     g_hwnd = NULL;
HINSTANCE g_inst = NULL;
UINT_PTR g_timer = 0;
bool     g_shown = false;
bool     g_mutating = false;
std::string g_lastKind;       // last shown PP_KIND (to log on change)
std::string g_selId;          // current selected PowerPlanner PP_ID
std::string g_selKind;        // current selected PowerPlanner PP_KIND
std::string g_chartProj;      // current CHART_ROOT PP_PROJ payload
std::wstring g_badge = L"PowerPlanner";
RECT g_buttonRects[BUTTON_COUNT] = {};
RECT g_frameRect = {};
RECT g_chartScreenRect = {};
RECT g_selScreenRect = {};
int g_windowOriginX = 0;
int g_windowOriginY = 0;
bool g_buttonsValid = false;
bool g_hasSelectionChrome = false;

enum OverlayButton {
	BTN_ADD = 0,
	BTN_DEL = 1,
	BTN_PCT_MINUS = 2,
	BTN_PCT_PLUS = 3
};

// Forward
void PaintOverlay(HDC hdc, const RECT& rc);
void HandleToolbarButton(int button);

std::string Narrow(const wchar_t* w) {
	if (!w || !*w) return "";
	int len = (int)::wcslen(w);
	int n = (int)::WideCharToMultiByte(CP_UTF8, 0, w, len, NULL, 0, NULL, NULL);
	std::string s(n, '\0');
	if (n > 0) ::WideCharToMultiByte(CP_UTF8, 0, w, len, &s[0], n, NULL, NULL);
	return s;
}

const PpTask* FindTask(const PpDocument& doc, const std::string& id) {
	for (const auto& task : doc.tasks) {
		if (task.id == id) return &task;
	}
	return nullptr;
}

std::string FirstRowId(const PpDocument& doc) {
	return doc.rows.empty() ? "" : doc.rows.front().id;
}

std::string RowForSelection(const PpDocument& doc, const std::string& kind, const std::string& id) {
	if (kind == "TASK" || kind == "TASK_PROGRESS") {
		if (const PpTask* task = FindTask(doc, id)) return task->rowId;
	}
	if (kind == "ROW_LABEL") {
		for (const auto& row : doc.rows) {
			if (row.id == id) return row.id;
		}
	}
	return FirstRowId(doc);
}

void DefaultTaskDates(const PpDocument& doc, const std::string& rowId, const std::string& selectedTaskId, std::string& start, std::string& end) {
	if (const PpTask* selected = FindTask(doc, selectedTaskId)) {
		start = selected->start;
		end = selected->end;
		return;
	}
	for (const auto& task : doc.tasks) {
		if (task.rowId == rowId) {
			start = task.start;
			end = task.end;
			return;
		}
	}
	if (!doc.tasks.empty()) {
		start = doc.tasks.front().start;
		end = doc.tasks.front().end;
		return;
	}
	start = "2026-01-01";
	end = "2026-01-08";
}

bool IsTaskKind(const std::string& kind) {
	return kind == "TASK" || kind == "TASK_PROGRESS";
}

void ClearSelectionState() {
	g_selId.clear();
	g_selKind.clear();
	g_hasSelectionChrome = false;
	g_buttonsValid = false;
	::SetRectEmpty(&g_selScreenRect);
	::SetRectEmpty(&g_frameRect);
}

void LayoutToolbarButtons(int width, int height) {
	g_buttonsValid = false;
	for (int i = 0; i < BUTTON_COUNT; ++i) ::SetRectEmpty(&g_buttonRects[i]);
	if (!g_hasSelectionChrome || ::IsRectEmpty(&g_frameRect) || width <= 0 || height <= 0) return;

	const int buttonW = 32;
	const int buttonH = 20;
	const int gap = 4;
	const int totalW = BUTTON_COUNT * buttonW + (BUTTON_COUNT - 1) * gap;
	int x = g_frameRect.left + 6;
	int y = g_frameRect.bottom + INFL;
	if (x + totalW + 6 > width) x = std::max(INFL, width - totalW - INFL - 6);
	if (x < INFL) x = INFL;
	if (y + buttonH + 3 > height) y = height - TOOLBAR_H + (TOOLBAR_H - buttonH) / 2;
	if (y < INFL) y = INFL;
	for (int i = 0; i < BUTTON_COUNT; ++i) {
		g_buttonRects[i] = { x + i * (buttonW + gap), y, x + i * (buttonW + gap) + buttonW, y + buttonH };
	}
	g_buttonsValid = true;
}

int ButtonFromClientPoint(POINT pt) {
	if (!g_buttonsValid) return -1;
	for (int i = 0; i < BUTTON_COUNT; ++i) {
		if (::PtInRect(&g_buttonRects[i], pt)) return i;
	}
	return -1;
}

void NormalizeRect(RECT& rc) {
	if (rc.left > rc.right) std::swap(rc.left, rc.right);
	if (rc.top > rc.bottom) std::swap(rc.top, rc.bottom);
}

void UpdateSelectionFrameFromScreen() {
	::SetRectEmpty(&g_frameRect);
	g_buttonsValid = false;
	if (!g_hasSelectionChrome || ::IsRectEmpty(&g_selScreenRect)) return;
	g_frameRect = {
		g_selScreenRect.left - g_windowOriginX,
		g_selScreenRect.top - g_windowOriginY,
		g_selScreenRect.right - g_windowOriginX,
		g_selScreenRect.bottom - g_windowOriginY
	};
	if (g_frameRect.right < g_frameRect.left + 8) g_frameRect.right = g_frameRect.left + 8;
	if (g_frameRect.bottom < g_frameRect.top + 8) g_frameRect.bottom = g_frameRect.top + 8;
}

PowerPoint::ShapePtr FindChartRoot(PowerPoint::_SlidePtr slide) {
	if (!slide) return nullptr;
	PowerPoint::ShapesPtr shapes = slide->GetShapes();
	if (!shapes) return nullptr;
	long n = shapes->GetCount();
	for (long i = 1; i <= n; ++i) {
		PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
		_bstr_t kind = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
		if (kind.length() && Narrow((const wchar_t*)kind) == "CHART_ROOT") return sh;
	}
	return nullptr;
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

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_NCHITTEST) {
		POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		::ScreenToClient(hwnd, &pt);
		return ButtonFromClientPoint(pt) >= 0 ? HTCLIENT : HTTRANSPARENT;
	}
	if (msg == WM_MOUSEACTIVATE) {
		return MA_NOACTIVATE;
	}
	if (msg == WM_LBUTTONDOWN) {
		POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		if (ButtonFromClientPoint(pt) >= 0) return 0;
	}
	if (msg == WM_LBUTTONUP) {
		POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		int button = ButtonFromClientPoint(pt);
		if (button >= 0) {
			try {
				HandleToolbarButton(button);
			} catch (...) {
				OvLog(L"toolbar click failed");
			}
			return 0;
		}
	}
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
		WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
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

	LayoutToolbarButtons(W, H);
	if (!g_hasSelectionChrome || ::IsRectEmpty(&g_frameRect)) return;

	RECT frame = g_frameRect;

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
	int badgeTop = std::max(2, (int)frame.top - BADGE_H - 3);
	RECT badge = { frame.left, badgeTop, frame.left + bw, badgeTop + bh };
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

	// floating Material mini-toolbar
	if (g_buttonsValid) {
		RECT bg = g_buttonRects[0];
		bg.left -= 6; bg.top -= 3; bg.right = g_buttonRects[BUTTON_COUNT - 1].right + 6; bg.bottom += 3;
		HBRUSH bgBrush = ::CreateSolidBrush(SURFACE);
		HPEN bgPen = ::CreatePen(PS_SOLID, 1, RGB(218, 220, 224));
		HGDIOBJ oldBgBr = ::SelectObject(hdc, bgBrush);
		HGDIOBJ oldBgPen = ::SelectObject(hdc, bgPen);
		::RoundRect(hdc, bg.left, bg.top, bg.right, bg.bottom, 10, 10);
		::SelectObject(hdc, oldBgBr);
		::SelectObject(hdc, oldBgPen);
		::DeleteObject(bgBrush);
		::DeleteObject(bgPen);

		HFONT btnFont = ::CreateFontW(-12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
		HGDIOBJ oldBtnFont = btnFont ? ::SelectObject(hdc, btnFont) : NULL;
		int oldBtnBk = ::SetBkMode(hdc, TRANSPARENT);
		COLORREF oldBtnText = ::SetTextColor(hdc, TEXT);
		const wchar_t* labels[BUTTON_COUNT] = { L"Add", L"Del", L"-", L"+" };
		for (int i = 0; i < BUTTON_COUNT; ++i) {
			HBRUSH bbr = ::CreateSolidBrush(i == BTN_ADD ? RGB(232, 240, 254) : SURFACE_VARIANT);
			HPEN bpen = ::CreatePen(PS_SOLID, 1, i == BTN_ADD ? ACCENT : RGB(218, 220, 224));
			HGDIOBJ oldB = ::SelectObject(hdc, bbr);
			HGDIOBJ oldP = ::SelectObject(hdc, bpen);
			const RECT& br = g_buttonRects[i];
			::RoundRect(hdc, br.left, br.top, br.right, br.bottom, 8, 8);
			::SelectObject(hdc, oldB);
			::SelectObject(hdc, oldP);
			::DeleteObject(bbr);
			::DeleteObject(bpen);
			RECT tr = br;
			::DrawTextW(hdc, labels[i], -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		::SetTextColor(hdc, oldBtnText);
		::SetBkMode(hdc, oldBtnBk);
		if (oldBtnFont) ::SelectObject(hdc, oldBtnFont);
		if (btnFont) ::DeleteObject(btnFont);
	}
}

void HideOverlay() {
	if (g_shown && g_hwnd) { ::ShowWindow(g_hwnd, SW_HIDE); g_shown = false; }
	g_buttonsValid = false;
	ClearSelectionState();
	::SetRectEmpty(&g_chartScreenRect);
	g_chartProj.clear();
}

void ShowOverlayForChartRect(const RECT& chart) {
	EnsureWindow();
	if (!g_hwnd) return;
	int chartW = chart.right - chart.left;
	int chartH = chart.bottom - chart.top;
	int wx = chart.left - INFL, wy = chart.top - INFL - BADGE_H;
	int ww = chartW + INFL * 2, wh = chartH + INFL * 2 + BADGE_H + TOOLBAR_H;
	g_windowOriginX = wx;
	g_windowOriginY = wy;
	UpdateSelectionFrameFromScreen();
	const int toolbarMinW = 2 * (INFL + 6) + BUTTON_COUNT * 32 + (BUTTON_COUNT - 1) * 4;
	if (ww < toolbarMinW) ww = toolbarMinW;
	if (wh < BADGE_H + TOOLBAR_H + INFL * 2 + 8) wh = BADGE_H + TOOLBAR_H + INFL * 2 + 8;
	LayoutToolbarButtons(ww, wh);
	::SetWindowPos(g_hwnd, HWND_TOPMOST, wx, wy, ww, wh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	::InvalidateRect(g_hwnd, NULL, TRUE);
	::UpdateWindow(g_hwnd);
	g_shown = true;
}

void RebuildChart(PpDocument& doc, const std::string& selectId) {
	PowerPoint::_SlidePtr slide = g_app->GetActiveWindow()->GetView()->GetSlide();
	PowerPoint::ShapesPtr shapes = slide->GetShapes();
	long n = shapes->GetCount();
	for (long i = 1; i <= n; ++i) {
		PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
		_bstr_t kind = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
		if (kind.length() && Narrow((const wchar_t*)kind) == "CHART_ROOT") {
			sh->Delete();
			break;
		}
	}

	int cnt = 0;
	HRESULT hr = InsertGantt(g_app, doc, &cnt, selectId);
	if (FAILED(hr)) OvLog(L"InsertGantt failed after toolbar edit");
}

void HandleToolbarButton(int button) {
	if (!g_app || g_mutating) return;
	const std::string selId = g_selId;
	const std::string selKind = g_selKind;
	if (button != BTN_ADD && !IsTaskKind(selKind)) return;
	if (button != BTN_ADD && selId.empty()) return;

	g_mutating = true;
	try {
		std::string json = ReadGanttFromSlide(g_app);
		if (json.empty()) { g_mutating = false; return; }

		PpDocument doc = DocumentFromJson(json);
		std::string selectId;
		bool changed = false;
		if (button == BTN_ADD) {
			std::string rowId = RowForSelection(doc, selKind, selId);
			if (!rowId.empty()) {
				std::string start, end;
				DefaultTaskDates(doc, rowId, IsTaskKind(selKind) ? selId : "", start, end);
				selectId = AddTask(doc, rowId, "New Task", start, end);
				changed = !selectId.empty();
			}
		} else if (button == BTN_DEL) {
			selectId.clear();
			changed = DeleteById(doc, selId);
		} else if (button == BTN_PCT_MINUS || button == BTN_PCT_PLUS) {
			if (const PpTask* task = FindTask(doc, selId)) {
				int delta = (button == BTN_PCT_PLUS) ? 10 : -10;
				changed = SetTaskPercent(doc, selId, task->percent + delta);
				if (changed) selectId = selId;
			}
		}

		if (changed) {
			RebuildChart(doc, selectId);
			wchar_t buf[128];
			::swprintf_s(buf, 128, L"toolbar button %d applied to %hs/%hs", button, selKind.c_str(), selId.c_str());
			OvLog(buf);
		}
	}
	catch (const _com_error&) {
		OvLog(L"COM error during toolbar edit");
	}
	catch (const std::exception&) {
		OvLog(L"document error during toolbar edit");
	}
	catch (...) {
		OvLog(L"unknown error during toolbar edit");
	}
	g_mutating = false;
}

// Poll the slide; keep the overlay over the chart while selection chrome follows
// the selected PowerPlanner child shape.
void Tick() {
	if (g_mutating) return;
	if (!g_app) { HideOverlay(); return; }
	try {
		PowerPoint::DocumentWindowPtr win = g_app->GetActiveWindow();
		if (!win) { HideOverlay(); return; }

		PowerPoint::_SlidePtr slide = win->GetView()->GetSlide();
		PowerPoint::ShapePtr chart = FindChartRoot(slide);
		if (!chart) {
			HideOverlay();
			return;
		}

		float chartLeft = chart->GetLeft(), chartTop = chart->GetTop();
		float chartWidth = chart->GetWidth(), chartHeight = chart->GetHeight();
		g_chartScreenRect = {
			win->PointsToScreenPixelsX(chartLeft),
			win->PointsToScreenPixelsY(chartTop),
			win->PointsToScreenPixelsX(chartLeft + chartWidth),
			win->PointsToScreenPixelsY(chartTop + chartHeight)
		};
		NormalizeRect(g_chartScreenRect);
		g_chartProj = Narrow((const wchar_t*)chart->GetTags()->Item(_bstr_t(L"PP_PROJ")));

		ClearSelectionState();
		PowerPoint::SelectionPtr sel = win->GetSelection();
		if (sel && sel->GetType() == PowerPoint::ppSelectionShapes) {
			PowerPoint::ShapeRangePtr sr = sel->GetShapeRange();
			if (sr && sr->GetCount() >= 1) {
				PowerPoint::ShapePtr sh = sr->Item(_variant_t(1L));
				_bstr_t kind = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
				if (kind.length()) {
					_bstr_t id = sh->GetTags()->Item(_bstr_t(L"PP_ID"));
					g_selKind = Narrow((const wchar_t*)kind);
					g_selId = Narrow((const wchar_t*)id);
					float left = sh->GetLeft(), top = sh->GetTop(), w = sh->GetWidth(), h = sh->GetHeight();
					g_selScreenRect = {
						win->PointsToScreenPixelsX(left),
						win->PointsToScreenPixelsY(top),
						win->PointsToScreenPixelsX(left + w),
						win->PointsToScreenPixelsY(top + h)
					};
					NormalizeRect(g_selScreenRect);
					g_hasSelectionChrome = true;
				}
			}
		}

		if (g_hasSelectionChrome) {
			bool mouseUp = !(::GetKeyState(VK_LBUTTON) & 0x8000);
			if (mouseUp) {
				std::string kStr = g_selKind;
				if (kStr == "TASK" || kStr == "MILESTONE") {
					bool changed = false;
					ReflowFromSlide(g_app, &changed);
					if (changed) {
						ClearSelectionState();
						ShowOverlayForChartRect(g_chartScreenRect);
						return;
					}
				}
			}
		}

		ShowOverlayForChartRect(g_chartScreenRect);

		std::string k = g_selKind;
		if (k != g_lastKind) {
			g_lastKind = k;
			wchar_t buf[160];
			::swprintf_s(buf, 160, L"shown for chart (%ld,%ld)-(%ld,%ld), selection PP_KIND=%hs",
				g_chartScreenRect.left, g_chartScreenRect.top, g_chartScreenRect.right, g_chartScreenRect.bottom, k.c_str());
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
	ClearSelectionState();
	g_buttonsValid = false;
	::SetRectEmpty(&g_frameRect);
}

HWND OverlayHwnd() { return g_hwnd; }
