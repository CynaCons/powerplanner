#include "pch.h"
#include "Overlay.h"
#include "GanttBuilder.h"
#include "GanttJson.h"
#include "GanttOps.h"
// GDI+ headers need the min/max macros that pch.h's NOMINMAX removes. Define
// them only for the header, then drop them so std::min/std::max keep working.
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define PP_OVERLAY_UNDEF_MIN
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define PP_OVERLAY_UNDEF_MAX
#endif
#include <gdiplus.h>
#ifdef PP_OVERLAY_UNDEF_MIN
#undef min
#undef PP_OVERLAY_UNDEF_MIN
#endif
#ifdef PP_OVERLAY_UNDEF_MAX
#undef max
#undef PP_OVERLAY_UNDEF_MAX
#endif
#include <algorithm>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// ---- module state ----------------------------------------------------------

namespace {
const wchar_t* kClass = L"PowerPlannerOverlay";
const wchar_t* kEditorClass = L"PowerPlannerInlineEditor";
const COLORREF ACCENT = RGB(26, 115, 232); // Material primary blue
const COLORREF HANDLE_INNER = RGB(138, 180, 248);
const COLORREF SURFACE = RGB(255, 255, 255);
const COLORREF SURFACE_VARIANT = RGB(241, 243, 244);
const COLORREF TEXT = RGB(60, 64, 67);
const int INFL = 5;                         // frame inset from shape edge (px)
const int BADGE_H = 20;                     // badge strip height (px)
const int TOOLBAR_H = 28;                   // floating action toolbar height (px)
const int BUTTON_COUNT = 4;
const int ROW_INSERT_BUTTON = 16;
const BYTE HOVER_WASH_ALPHA = 28;           // translucent accent wash over hovered row

PowerPoint::_ApplicationPtr g_app;
HWND     g_hwnd = NULL;
HINSTANCE g_inst = NULL;
UINT_PTR g_timer = 0;
bool     g_shown = false;
bool     g_mutating = false;
bool     g_inTick = false;
ULONG_PTR g_gdiplusToken = 0;

// 32-bpp premultiplied-alpha back buffer, pushed via UpdateLayeredWindow.
HDC     g_bufDc = NULL;
HBITMAP g_bufBmp = NULL;
HGDIOBJ g_bufOld = NULL;
void*   g_bufBits = nullptr;
int     g_bufW = 0;
int     g_bufH = 0;
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

struct RowBand {
	std::string rowId;
	RECT screenRect;
	int screenLeftGutter;
};

std::vector<RowBand> g_rowBands;
std::string g_hoverRowId;
RECT g_hoverBandRect = {};
RECT g_hoverInsertRect = {};
bool g_hoverInsertValid = false;
bool g_lastLeftButtonDown = false;

struct EditRegion {
	std::string kind;
	std::string id;
	RECT screenRect;
};

std::vector<EditRegion> g_editRegions;
HWND g_editorHwnd = NULL;
HWND g_editHwnd = NULL;
WNDPROC g_oldEditProc = NULL;
bool g_editClosing = false;
std::string g_editKind;
std::string g_editId;

enum OverlayButton {
	BTN_ADD = 0,
	BTN_DEL = 1,
	BTN_PCT_MINUS = 2,
	BTN_PCT_PLUS = 3
};

// Forward
void PaintOverlay(Gdiplus::Graphics& g, int W, int H);
void RenderOverlay();
void HandleToolbarButton(int button);
void HandleHoverInsertRow();
void RebuildChart(PpDocument& doc, const std::string& selectId);
void OvLog(const wchar_t* msg);
void OpenInlineEditor(const EditRegion& region);
void CommitInlineEdit();
void CancelInlineEdit();
LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK InlineEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

std::string Narrow(const wchar_t* w) {
	if (!w || !*w) return "";
	int len = (int)::wcslen(w);
	int n = (int)::WideCharToMultiByte(CP_UTF8, 0, w, len, NULL, 0, NULL, NULL);
	std::string s(n, '\0');
	if (n > 0) ::WideCharToMultiByte(CP_UTF8, 0, w, len, &s[0], n, NULL, NULL);
	return s;
}

std::wstring Widen(const std::string& s) {
	if (s.empty()) return L"";
	int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
	std::wstring w(n, L'\0');
	if (n > 0) ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
	return w;
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

bool SameRect(const RECT& a, const RECT& b) {
	return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

bool SameSelectionState(bool hasSelection, const RECT& selRect, const std::string& selId, const std::string& selKind) {
	return g_hasSelectionChrome == hasSelection && SameRect(g_selScreenRect, selRect) && g_selId == selId && g_selKind == selKind;
}

void ClearHoverState() {
	g_hoverRowId.clear();
	::SetRectEmpty(&g_hoverBandRect);
	::SetRectEmpty(&g_hoverInsertRect);
	g_hoverInsertValid = false;
}

const EditRegion* EditRegionFromScreenPoint(POINT pt) {
	for (const auto& region : g_editRegions) {
		if (::PtInRect(&region.screenRect, pt)) return &region;
	}
	return nullptr;
}

const EditRegion* EditRegionFromClientPoint(POINT pt) {
	POINT screenPt = { pt.x + g_windowOriginX, pt.y + g_windowOriginY };
	return EditRegionFromScreenPoint(screenPt);
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

void LayoutHoverInsertHotspot() {
	g_hoverInsertValid = false;
	::SetRectEmpty(&g_hoverInsertRect);
	if (g_hoverRowId.empty() || ::IsRectEmpty(&g_hoverBandRect)) return;
	if (::GetKeyState(VK_LBUTTON) & 0x8000) return;

	int bandTop = g_hoverBandRect.top - g_windowOriginY;
	int bandBottom = g_hoverBandRect.bottom - g_windowOriginY;
	int cy = (bandTop + bandBottom) / 2;
	int left = g_chartScreenRect.left - g_windowOriginX + 6;
	for (const auto& band : g_rowBands) {
		if (band.rowId == g_hoverRowId) {
			left = std::max((int)(g_chartScreenRect.left - g_windowOriginX + 4), band.screenLeftGutter - g_windowOriginX - ROW_INSERT_BUTTON - 4);
			break;
		}
	}
	g_hoverInsertRect = { left, cy - ROW_INSERT_BUTTON / 2, left + ROW_INSERT_BUTTON, cy + ROW_INSERT_BUTTON / 2 };
	g_hoverInsertValid = true;
}

bool HoverInsertFromClientPoint(POINT pt) {
	if (!g_hoverInsertValid) LayoutHoverInsertHotspot();
	return g_hoverInsertValid && ::PtInRect(&g_hoverInsertRect, pt);
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

void BuildRowBands(PowerPoint::ShapePtr chart, PowerPoint::DocumentWindowPtr win) {
	g_rowBands.clear();
	g_editRegions.clear();
	if (!chart || !win) return;
	try {
		PowerPoint::GroupShapesPtr items = chart->GetGroupItems();
		if (!items) return;
		long n = items->GetCount();
		for (long i = 1; i <= n; ++i) {
			PowerPoint::ShapePtr ch = items->Item(_variant_t(i));
			_bstr_t kind = ch->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (!kind.length()) continue;
			std::string kindStr = Narrow((const wchar_t*)kind);
			if (kindStr != "ROW_LABEL" && kindStr != "TITLE") continue;
			float left = ch->GetLeft(), top = ch->GetTop(), w = ch->GetWidth(), h = ch->GetHeight();
			RECT rr = {
				win->PointsToScreenPixelsX(left),
				win->PointsToScreenPixelsY(top),
				win->PointsToScreenPixelsX(left + w),
				win->PointsToScreenPixelsY(top + h)
			};
			NormalizeRect(rr);
			if (rr.bottom <= rr.top) continue;
			if (kindStr == "TITLE") {
				g_editRegions.push_back({ "TITLE", "", rr });
				continue;
			}
			_bstr_t id = ch->GetTags()->Item(_bstr_t(L"PP_ID"));
			std::string rowId = Narrow((const wchar_t*)id);
			if (rowId.empty()) continue;
			g_rowBands.push_back({ rowId, { g_chartScreenRect.left, rr.top, g_chartScreenRect.right, rr.bottom }, rr.left });
			g_editRegions.push_back({ "ROW_LABEL", rowId, rr });
		}
		std::sort(g_rowBands.begin(), g_rowBands.end(), [](const RowBand& a, const RowBand& b) {
			return a.screenRect.top < b.screenRect.top;
		});
	}
	catch (const _com_error&) {
		g_rowBands.clear();
		g_editRegions.clear();
	}
}

bool UpdateHoverFromCursor() {
	std::string oldId = g_hoverRowId;
	RECT oldRect = g_hoverBandRect;
	ClearHoverState();

	POINT pt = {};
	if (!::GetCursorPos(&pt) || !::PtInRect(&g_chartScreenRect, pt)) {
		return oldId != g_hoverRowId || !SameRect(oldRect, g_hoverBandRect);
	}

	for (const auto& band : g_rowBands) {
		if (pt.y >= band.screenRect.top && pt.y <= band.screenRect.bottom) {
			g_hoverRowId = band.rowId;
			g_hoverBandRect = band.screenRect;
			break;
		}
	}
	LayoutHoverInsertHotspot();
	return oldId != g_hoverRowId || !SameRect(oldRect, g_hoverBandRect);
}

// Single repaint entry point: everything that wants pixels on screen goes
// through RenderOverlay(). Later units (drag ghosts etc.) should add their
// drawing inside PaintOverlay so it flows through the same path.
void RequestOverlayRepaint() {
	if (!g_hwnd) return;
	RenderOverlay();
}

std::string CurrentEditText(const EditRegion& region) {
	std::string json = ReadGanttFromSlide(g_app);
	if (json.empty()) return "";
	PpDocument doc = DocumentFromJson(json);
	if (region.kind == "TITLE") return doc.title;
	if (region.kind == "ROW_LABEL") {
		for (const auto& row : doc.rows) {
			if (row.id == region.id) return row.label;
		}
	}
	return "";
}

void FocusPowerPoint() {
	try {
		if (!g_app) return;
		HWND ppt = (HWND)(INT_PTR)g_app->GetHWND();
		if (ppt) ::SetForegroundWindow(ppt);
	}
	catch (...) {
	}
}

void ClearEditTarget() {
	g_editKind.clear();
	g_editId.clear();
}

void HideInlineEditor() {
	g_editClosing = true;
	if (g_editorHwnd) ::ShowWindow(g_editorHwnd, SW_HIDE);
	g_editClosing = false;
}

void DestroyInlineEditor() {
	g_editClosing = true;
	if (g_editHwnd && g_oldEditProc) {
		::SetWindowLongPtrW(g_editHwnd, GWLP_WNDPROC, (LONG_PTR)g_oldEditProc);
		g_oldEditProc = NULL;
	}
	if (g_editorHwnd) {
		::DestroyWindow(g_editorHwnd);
		g_editorHwnd = NULL;
	}
	g_editHwnd = NULL;
	g_editClosing = false;
	ClearEditTarget();
}

void EnsureEditorWindow() {
	if (g_editorHwnd && g_editHwnd) return;

	WNDCLASSEXW wc = { sizeof(wc) };
	wc.lpfnWndProc = EditorWndProc;
	wc.hInstance = g_inst;
	wc.hCursor = ::LoadCursor(NULL, IDC_IBEAM);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszClassName = kEditorClass;
	::RegisterClassExW(&wc);

	g_editorHwnd = ::CreateWindowExW(
		WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
		kEditorClass, L"", WS_POPUP | WS_BORDER,
		0, 0, 100, 24, NULL, NULL, g_inst, NULL);
	if (!g_editorHwnd) return;

	g_editHwnd = ::CreateWindowExW(
		0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
		0, 0, 100, 24, g_editorHwnd, (HMENU)1, g_inst, NULL);
	if (g_editHwnd) {
		::SendMessageW(g_editHwnd, WM_SETFONT, (WPARAM)::GetStockObject(DEFAULT_GUI_FONT), TRUE);
		g_oldEditProc = (WNDPROC)::SetWindowLongPtrW(g_editHwnd, GWLP_WNDPROC, (LONG_PTR)InlineEditProc);
	}
}

void OpenInlineEditor(const EditRegion& region) {
	if (!g_app || g_mutating) return;
	try {
		std::string value = CurrentEditText(region);
		EnsureEditorWindow();
		if (!g_editorHwnd || !g_editHwnd) return;

		RECT rc = region.screenRect;
		NormalizeRect(rc);
		int w = std::max(80, (int)(rc.right - rc.left));
		int h = std::max(22, (int)(rc.bottom - rc.top));
		::SetWindowPos(g_editorHwnd, HWND_TOPMOST, rc.left, rc.top, w, h, SWP_SHOWWINDOW);
		::MoveWindow(g_editHwnd, 0, 0, w, h, TRUE);
		std::wstring text = Widen(value);
		::SetWindowTextW(g_editHwnd, text.c_str());
		g_editKind = region.kind;
		g_editId = region.id;
		::SendMessageW(g_editHwnd, EM_SETSEL, 0, -1);
		::SetForegroundWindow(g_editorHwnd);
		::SetFocus(g_editHwnd);
	}
	catch (const _com_error&) {
		OvLog(L"COM error opening inline editor");
		HideInlineEditor();
		ClearEditTarget();
	}
	catch (const std::exception&) {
		OvLog(L"document error opening inline editor");
		HideInlineEditor();
		ClearEditTarget();
	}
	catch (...) {
		OvLog(L"unknown error opening inline editor");
		HideInlineEditor();
		ClearEditTarget();
	}
}

void CommitInlineEdit() {
	if (!g_editHwnd || g_editKind.empty() || g_mutating) return;
	std::string kind = g_editKind;
	std::string id = g_editId;
	try {
		int len = ::GetWindowTextLengthW(g_editHwnd);
		std::vector<wchar_t> buf((size_t)len + 1);
		::GetWindowTextW(g_editHwnd, buf.data(), len + 1);
		std::string text = Narrow(buf.data());

		HideInlineEditor();
		ClearEditTarget();

		g_mutating = true;
		try {
			std::string json = ReadGanttFromSlide(g_app);
			if (!json.empty()) {
				PpDocument doc = DocumentFromJson(json);
				bool changed = (kind == "TITLE") ? SetTitle(doc, text) : SetEntityLabel(doc, id, text);
				if (changed) RebuildChart(doc, kind == "ROW_LABEL" ? id : "");
			}
		}
		catch (const _com_error&) {
			OvLog(L"COM error committing inline edit");
		}
		catch (const std::exception&) {
			OvLog(L"document error committing inline edit");
		}
		catch (...) {
			OvLog(L"unknown error committing inline edit");
		}
		g_mutating = false;
		FocusPowerPoint();
	}
	catch (...) {
		g_mutating = false;
		HideInlineEditor();
		ClearEditTarget();
		FocusPowerPoint();
		OvLog(L"inline edit commit failed");
	}
}

void CancelInlineEdit() {
	HideInlineEditor();
	ClearEditTarget();
	FocusPowerPoint();
}

// Append a debug line (shared with Connect's log).
void OvLog(const wchar_t* msg) {
	wchar_t path[MAX_PATH];
	DWORD n = ::GetTempPathW(MAX_PATH, path);
	if (!n || n > MAX_PATH) return;
	::wcscat_s(path, MAX_PATH, L"powerplanner-addin.log");
	HANDLE h = ::CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return;
	wchar_t pidBuf[48];
	::swprintf_s(pidBuf, 48, L"[overlay %lu @%lu] ", ::GetCurrentProcessId(), ::GetTickCount());
	std::wstring line = std::wstring(pidBuf) + msg + L"\r\n";
	DWORD w = 0; ::WriteFile(h, line.c_str(), (DWORD)(line.size() * sizeof(wchar_t)), &w, NULL);
	::CloseHandle(h);
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_NCHITTEST) {
		POINT screenPt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		POINT pt = screenPt;
		::ScreenToClient(hwnd, &pt);
		return (ButtonFromClientPoint(pt) >= 0 || HoverInsertFromClientPoint(pt) || EditRegionFromScreenPoint(screenPt)) ? HTCLIENT : HTTRANSPARENT;
	}
	if (msg == WM_MOUSEACTIVATE) {
		return MA_NOACTIVATE;
	}
	if (msg == WM_LBUTTONDOWN) {
		POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		if (ButtonFromClientPoint(pt) >= 0 || HoverInsertFromClientPoint(pt)) return 0;
	}
	if (msg == WM_LBUTTONUP) {
		POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		if (HoverInsertFromClientPoint(pt)) {
			try {
				HandleHoverInsertRow();
			} catch (...) {
				OvLog(L"hover row insert failed");
			}
			return 0;
		}
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
	if (msg == WM_LBUTTONDBLCLK) {
		POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
		if (const EditRegion* region = EditRegionFromClientPoint(pt)) {
			OpenInlineEditor(*region);
			return 0;
		}
	}
	if (msg == WM_PAINT) {
		// Content is pushed by UpdateLayeredWindow (RenderOverlay); WM_PAINT
		// only needs to validate the region so the queue stays quiet.
		::ValidateRect(hwnd, NULL);
		return 0;
	}
	return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_CLOSE) {
		CancelInlineEdit();
		return 0;
	}
	if (msg == WM_SIZE && g_editHwnd) {
		RECT rc;
		::GetClientRect(hwnd, &rc);
		::MoveWindow(g_editHwnd, 0, 0, rc.right - rc.left, rc.bottom - rc.top, TRUE);
		return 0;
	}
	if (msg == WM_DESTROY && hwnd == g_editorHwnd) {
		g_editorHwnd = NULL;
		g_editHwnd = NULL;
		g_oldEditProc = NULL;
	}
	return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK InlineEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_KEYDOWN) {
		if (wp == VK_RETURN) {
			CommitInlineEdit();
			return 0;
		}
		if (wp == VK_ESCAPE) {
			CancelInlineEdit();
			return 0;
		}
	}
	if (msg == WM_KILLFOCUS && !g_editClosing) {
		CommitInlineEdit();
		return 0;
	}
	return g_oldEditProc ? ::CallWindowProcW(g_oldEditProc, hwnd, msg, wp, lp) : ::DefWindowProcW(hwnd, msg, wp, lp);
}

void EnsureWindow() {
	if (g_hwnd) return;
	WNDCLASSEXW wc = { sizeof(wc) };
	wc.lpfnWndProc = OverlayWndProc;
	wc.hInstance = g_inst;
	wc.style = CS_DBLCLKS;
	wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = kClass;
	::RegisterClassExW(&wc);

	// NOTE: no SetLayeredWindowAttributes here — the window is driven purely by
	// UpdateLayeredWindow (per-pixel premultiplied alpha). A layered window
	// without attributes stays invisible until the first RenderOverlay() push.
	g_hwnd = ::CreateWindowExW(
		WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
		kClass, L"", WS_POPUP,
		0, 0, 10, 10, NULL, NULL, g_inst, NULL);
}

// ---- premultiplied-alpha painting (GDI+) -----------------------------------

Gdiplus::Color GpColor(BYTE a, COLORREF c) {
	return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

void AddRoundRect(Gdiplus::GraphicsPath& path, Gdiplus::REAL x, Gdiplus::REAL y,
	Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL r) {
	if (w <= 0.0f || h <= 0.0f) return;
	Gdiplus::REAL maxR = (w < h ? w : h) / 2.0f;
	if (r > maxR) r = maxR;
	if (r <= 0.0f) {
		path.AddRectangle(Gdiplus::RectF(x, y, w, h));
		return;
	}
	Gdiplus::REAL d = r * 2.0f;
	path.StartFigure();
	path.AddArc(x, y, d, d, 180.0f, 90.0f);
	path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
	path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
	path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
	path.CloseFigure();
}

void DrawHandle(Gdiplus::Graphics& g, int cx, int cy, int r) {
	using namespace Gdiplus;
	GraphicsPath path;
	AddRoundRect(path, (REAL)(cx - r), (REAL)(cy - r), (REAL)(2 * r + 1), (REAL)(2 * r + 1), 1.5f);
	SolidBrush fill(Color(255, 255, 255, 255));
	g.FillPath(&fill, &path);
	GraphicsPath innerPath;
	AddRoundRect(innerPath, (REAL)(cx - r + 1), (REAL)(cy - r + 1), (REAL)(2 * r - 1), (REAL)(2 * r - 1), 1.0f);
	Pen inner(GpColor(255, HANDLE_INNER), 1.0f);
	g.DrawPath(&inner, &innerPath);
	Pen border(GpColor(255, ACCENT), 1.0f);
	g.DrawPath(&border, &path);
}

void PaintOverlay(Gdiplus::Graphics& g, int W, int H) {
	using namespace Gdiplus;
	// Background stays fully transparent (alpha 0) — the caller cleared it.

	LayoutToolbarButtons(W, H);
	LayoutHoverInsertHotspot();

	if (!g_hoverRowId.empty() && !::IsRectEmpty(&g_hoverBandRect) && !(::GetKeyState(VK_LBUTTON) & 0x8000)) {
		RECT band = {
			g_hoverBandRect.left - g_windowOriginX,
			g_hoverBandRect.top - g_windowOriginY,
			g_hoverBandRect.right - g_windowOriginX,
			g_hoverBandRect.bottom - g_windowOriginY
		};
		// Genuinely translucent accent wash over the whole hovered row band.
		SolidBrush wash(GpColor(HOVER_WASH_ALPHA, ACCENT));
		g.FillRectangle(&wash, (INT)band.left, (INT)band.top,
			(INT)(band.right - band.left), (INT)(band.bottom - band.top));
		// Soft accent edge lines top/bottom.
		Pen edge(GpColor(110, ACCENT), 1.0f);
		g.DrawLine(&edge, (INT)band.left, (INT)band.top, (INT)band.right, (INT)band.top);
		g.DrawLine(&edge, (INT)band.left, (INT)band.bottom, (INT)band.right, (INT)band.bottom);
		// Solid accent bar on the left edge.
		SolidBrush bar(GpColor(255, ACCENT));
		g.FillRectangle(&bar, (INT)band.left, (INT)band.top, 3, (INT)(band.bottom - band.top));

		if (g_hoverInsertValid) {
			INT ex = (INT)g_hoverInsertRect.left, ey = (INT)g_hoverInsertRect.top;
			INT ew = (INT)(g_hoverInsertRect.right - g_hoverInsertRect.left);
			INT eh = (INT)(g_hoverInsertRect.bottom - g_hoverInsertRect.top);
			SolidBrush plusFill(GpColor(255, SURFACE));
			g.FillEllipse(&plusFill, ex, ey, ew, eh);
			Pen plusPen(GpColor(255, ACCENT), 1.0f);
			g.DrawEllipse(&plusPen, ex, ey, ew, eh);

			Pen glyphPen(GpColor(255, ACCENT), 2.0f);
			int cx = (g_hoverInsertRect.left + g_hoverInsertRect.right) / 2;
			int cy = (g_hoverInsertRect.top + g_hoverInsertRect.bottom) / 2;
			g.DrawLine(&glyphPen, cx - 4, cy, cx + 4, cy);
			g.DrawLine(&glyphPen, cx, cy - 4, cx, cy + 4);
		}
	}

	if (!g_hasSelectionChrome || ::IsRectEmpty(&g_frameRect)) return;

	RECT frame = g_frameRect;
	REAL fx = (REAL)frame.left, fy = (REAL)frame.top;
	REAL fw = (REAL)(frame.right - frame.left), fh = (REAL)(frame.bottom - frame.top);

	// Soft halo behind the frame, then the crisp accent frame itself.
	{
		GraphicsPath halo;
		AddRoundRect(halo, fx - 1.5f, fy - 1.5f, fw + 3.0f, fh + 3.0f, 4.5f);
		Pen haloPen(GpColor(56, ACCENT), 4.0f);
		g.DrawPath(&haloPen, &halo);

		GraphicsPath framePath;
		AddRoundRect(framePath, fx, fy, fw, fh, 3.0f);
		Pen framePen(GpColor(255, ACCENT), 2.0f);
		g.DrawPath(&framePen, &framePath);
	}

	// 8 handles (white fill, accent border)
	int mx = (frame.left + frame.right) / 2, my = (frame.top + frame.bottom) / 2;
	int xs[3] = { (int)frame.left, mx, (int)frame.right };
	int ys[3] = { (int)frame.top, my, (int)frame.bottom };
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j) {
			if (i == 1 && j == 1) continue;
			DrawHandle(g, xs[i], ys[j], 3);
		}

	// badge: filled Material chip with white label at top-left
	int bw = 96, bh = BADGE_H - 4;
	int badgeTop = std::max(2, (int)frame.top - BADGE_H - 3);
	{
		GraphicsPath badgePath;
		AddRoundRect(badgePath, fx, (REAL)badgeTop, (REAL)bw, (REAL)bh, 4.0f);
		SolidBrush badgeBrush(GpColor(255, ACCENT));
		g.FillPath(&badgeBrush, &badgePath);

		Gdiplus::Font badgeFont(L"Segoe UI", 11.0f, FontStyleRegular, UnitPixel);
		StringFormat sf;
		sf.SetAlignment(StringAlignmentCenter);
		sf.SetLineAlignment(StringAlignmentCenter);
		sf.SetFormatFlags(StringFormatFlagsNoWrap);
		SolidBrush textBrush(Color(255, 255, 255, 255));
		g.DrawString(g_badge.c_str(), -1, &badgeFont,
			RectF(fx, (REAL)badgeTop, (REAL)bw, (REAL)bh), &sf, &textBrush);
	}

	// floating Material mini-toolbar
	if (g_buttonsValid) {
		RECT bg = g_buttonRects[0];
		bg.left -= 6; bg.top -= 3; bg.right = g_buttonRects[BUTTON_COUNT - 1].right + 6; bg.bottom += 3;
		GraphicsPath bgPath;
		AddRoundRect(bgPath, (REAL)bg.left, (REAL)bg.top,
			(REAL)(bg.right - bg.left), (REAL)(bg.bottom - bg.top), 5.0f);
		SolidBrush bgBrush(GpColor(255, SURFACE));
		g.FillPath(&bgBrush, &bgPath);
		Pen bgPen(GpColor(255, RGB(218, 220, 224)), 1.0f);
		g.DrawPath(&bgPen, &bgPath);

		Gdiplus::Font btnFont(L"Segoe UI", 12.0f, FontStyleBold, UnitPixel);
		StringFormat sf;
		sf.SetAlignment(StringAlignmentCenter);
		sf.SetLineAlignment(StringAlignmentCenter);
		sf.SetFormatFlags(StringFormatFlagsNoWrap);
		SolidBrush textBrush(GpColor(255, TEXT));
		const wchar_t* labels[BUTTON_COUNT] = { L"Add", L"Del", L"-", L"+" };
		for (int i = 0; i < BUTTON_COUNT; ++i) {
			const RECT& br = g_buttonRects[i];
			GraphicsPath btnPath;
			AddRoundRect(btnPath, (REAL)br.left, (REAL)br.top,
				(REAL)(br.right - br.left), (REAL)(br.bottom - br.top), 4.0f);
			SolidBrush btnBrush(GpColor(255, i == BTN_ADD ? RGB(232, 240, 254) : SURFACE_VARIANT));
			g.FillPath(&btnBrush, &btnPath);
			Pen btnPen(GpColor(255, i == BTN_ADD ? ACCENT : RGB(218, 220, 224)), 1.0f);
			g.DrawPath(&btnPen, &btnPath);
			g.DrawString(labels[i], -1, &btnFont,
				RectF((REAL)br.left, (REAL)br.top,
					(REAL)(br.right - br.left), (REAL)(br.bottom - br.top)),
				&sf, &textBrush);
		}
	}
}

// ---- back buffer + UpdateLayeredWindow push --------------------------------

void FreeBackBuffer() {
	if (g_bufDc) {
		if (g_bufOld) ::SelectObject(g_bufDc, g_bufOld);
		::DeleteDC(g_bufDc);
		g_bufDc = NULL;
		g_bufOld = NULL;
	}
	if (g_bufBmp) {
		::DeleteObject(g_bufBmp);
		g_bufBmp = NULL;
	}
	g_bufBits = nullptr;
	g_bufW = 0;
	g_bufH = 0;
}

bool EnsureBackBuffer(int w, int h) {
	if (g_bufDc && g_bufBits && g_bufW == w && g_bufH == h) return true;
	FreeBackBuffer();
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;  // top-down so GDI+ stride is simply w*4
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HDC screen = ::GetDC(NULL);
	HBITMAP bmp = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
	HDC dc = ::CreateCompatibleDC(screen);
	::ReleaseDC(NULL, screen);
	if (!bmp || !dc || !bits) {
		if (dc) ::DeleteDC(dc);
		if (bmp) ::DeleteObject(bmp);
		return false;
	}
	g_bufOld = ::SelectObject(dc, bmp);
	g_bufDc = dc;
	g_bufBmp = bmp;
	g_bufBits = bits;
	g_bufW = w;
	g_bufH = h;
	return true;
}

// Compose the full overlay into the 32-bpp premultiplied back buffer and push
// it to the screen with UpdateLayeredWindow. Never lets exceptions escape (it
// runs from the timer tick inside PowerPoint's process).
void RenderOverlay() {
	if (!g_hwnd || !g_gdiplusToken) return;
	RECT wr;
	if (!::GetWindowRect(g_hwnd, &wr)) return;
	int w = wr.right - wr.left, h = wr.bottom - wr.top;
	if (w <= 0 || h <= 0) return;
	if (!EnsureBackBuffer(w, h)) return;
	try {
		Gdiplus::Bitmap surface(w, h, w * 4, PixelFormat32bppPARGB, (BYTE*)g_bufBits);
		Gdiplus::Graphics g(&surface);
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
		g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
		g.Clear(Gdiplus::Color(0, 0, 0, 0));
		PaintOverlay(g, w, h);
		g.Flush(Gdiplus::FlushIntentionSync);

		POINT dst = { wr.left, wr.top };
		POINT src = { 0, 0 };
		SIZE  size = { w, h };
		BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
		::UpdateLayeredWindow(g_hwnd, NULL, &dst, &size, g_bufDc, &src, 0, &bf, ULW_ALPHA);
	}
	catch (const std::exception&) {
		OvLog(L"overlay render failed (std::exception)");
	}
	catch (...) {
		OvLog(L"overlay render failed");
	}
}

void HideOverlay() {
	if (g_shown && g_hwnd) { ::ShowWindow(g_hwnd, SW_HIDE); g_shown = false; }
	g_buttonsValid = false;
	ClearSelectionState();
	ClearHoverState();
	g_lastLeftButtonDown = false;
	g_rowBands.clear();
	g_editRegions.clear();
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
	RECT oldWindow = {};
	bool wasShown = g_shown;
	bool hadWindow = ::GetWindowRect(g_hwnd, &oldWindow) != FALSE;
	::SetWindowPos(g_hwnd, HWND_TOPMOST, wx, wy, ww, wh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	g_shown = true;
	if (!wasShown || !hadWindow || oldWindow.left != wx || oldWindow.top != wy || oldWindow.right - oldWindow.left != ww || oldWindow.bottom - oldWindow.top != wh) {
		RequestOverlayRepaint();
	}
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
				int newPct = task->percent + delta;
				if (newPct < 0) newPct = 0;
				if (newPct > 100) newPct = 100;
				if (newPct != task->percent) {
					changed = SetTaskPercent(doc, selId, newPct);
					if (changed) selectId = selId;
				}
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

void HandleHoverInsertRow() {
	if (!g_app || g_mutating || g_hoverRowId.empty()) return;
	const std::string afterRowId = g_hoverRowId;

	g_mutating = true;
	try {
		std::string json = ReadGanttFromSlide(g_app);
		if (json.empty()) { g_mutating = false; return; }

		PpDocument doc = DocumentFromJson(json);
		std::string rowId = AddRow(doc, "New Row", afterRowId);
		if (!rowId.empty()) {
			RebuildChart(doc, "");
			wchar_t buf[128];
			::swprintf_s(buf, 128, L"hover insert row after %hs", afterRowId.c_str());
			OvLog(buf);
		}
	}
	catch (const _com_error&) {
		OvLog(L"COM error during hover row insert");
	}
	catch (const std::exception&) {
		OvLog(L"document error during hover row insert");
	}
	catch (...) {
		OvLog(L"unknown error during hover row insert");
	}
	g_mutating = false;
}

// Poll the slide; keep the overlay over the chart while selection chrome follows
// the selected PowerPlanner child shape.
void Tick() {
	if (g_mutating) return;
	// Re-entrancy guard: while an outgoing COM call inside Tick blocks, its COM
	// modal wait still dispatches WM_TIMER — without this guard those nested
	// ticks issue new COM calls on the same channel and can starve the outer
	// call's reply forever (observed as a permanent hang in the harness).
	if (g_inTick) return;
	struct TickGuard {
		TickGuard() { g_inTick = true; }
		~TickGuard() { g_inTick = false; }
	} tickGuard;
	if (!g_app) { HideOverlay(); return; }
	try {
		RECT oldChartRect = g_chartScreenRect;
		RECT oldSelRect = g_selScreenRect;
		bool oldHasSelectionChrome = g_hasSelectionChrome;
		std::string oldSelId = g_selId;
		std::string oldSelKind = g_selKind;
		bool leftButtonDown = (::GetKeyState(VK_LBUTTON) & 0x8000) != 0;
		bool mouseStateChanged = leftButtonDown != g_lastLeftButtonDown;
		g_lastLeftButtonDown = leftButtonDown;

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
		bool chartChanged = !SameRect(oldChartRect, g_chartScreenRect);
		BuildRowBands(chart, win);
		bool hoverChanged = UpdateHoverFromCursor();

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
						RequestOverlayRepaint();
						return;
					}
				}
			}
		}

		ShowOverlayForChartRect(g_chartScreenRect);
		if (chartChanged || hoverChanged || mouseStateChanged || !SameSelectionState(oldHasSelectionChrome, oldSelRect, oldSelId, oldSelKind)) {
			RequestOverlayRepaint();
		}

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
	// GDI+ is started here (and shut down in OverlayStop) — never in DllMain.
	if (!g_gdiplusToken) {
		Gdiplus::GdiplusStartupInput gsi;
		if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gsi, NULL) != Gdiplus::Ok) {
			g_gdiplusToken = 0;
			OvLog(L"GdiplusStartup failed");
		}
	}
	EnsureWindow();
	g_timer = ::SetTimer(NULL, 0, 150, TimerProc);
	OvLog(L"started");
}

void OverlayStop() {
	if (g_timer) { ::KillTimer(NULL, g_timer); g_timer = 0; }
	DestroyInlineEditor();
	if (g_hwnd) { ::DestroyWindow(g_hwnd); g_hwnd = NULL; }
	FreeBackBuffer();
	if (g_gdiplusToken) {
		Gdiplus::GdiplusShutdown(g_gdiplusToken);
		g_gdiplusToken = 0;
	}
	g_app = nullptr;
	g_shown = false;
	g_lastKind.clear();
	ClearSelectionState();
	ClearHoverState();
	g_buttonsValid = false;
	g_rowBands.clear();
	g_editRegions.clear();
	::SetRectEmpty(&g_frameRect);
	::SetRectEmpty(&g_chartScreenRect);
}

HWND OverlayHwnd() { return g_hwnd; }
