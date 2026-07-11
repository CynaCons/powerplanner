#pragma once

#include "GanttHitTest.h"
#include "GanttLayout.h"

#include <algorithm>
#include <string>
#include <vector>
#include <windows.h>

inline std::string Narrow(const wchar_t* w) {
	if (!w || !*w) return "";
	int len = (int)::wcslen(w);
	int n = (int)::WideCharToMultiByte(CP_UTF8, 0, w, len, NULL, 0, NULL, NULL);
	std::string s(n, '\0');
	if (n > 0) ::WideCharToMultiByte(CP_UTF8, 0, w, len, &s[0], n, NULL, NULL);
	return s;
}

inline std::wstring Widen(const std::string& s) {
	if (s.empty()) return L"";
	int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
	std::wstring w(n, L'\0');
	if (n > 0) ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
	return w;
}

inline bool SameRect(const RECT& a, const RECT& b) {
	return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

inline HtRect ToHtRect(const RECT& r) {
	return { r.left, r.top, r.right, r.bottom };
}

inline void NormalizeRect(RECT& rc) {
	if (rc.left > rc.right) std::swap(rc.left, rc.right);
	if (rc.top > rc.bottom) std::swap(rc.top, rc.bottom);
}

inline bool IsTaskKind(const std::string& kind) {
	return kind == "TASK" || kind == "TASK_PROGRESS";
}

inline std::wstring FormatSwatchLabel(int idx) {
	wchar_t buf[8];
	::swprintf_s(buf, 8, L"%d", idx + 1);
	return buf;
}

// Read a single wide EDIT control's text as a narrow UTF-8 string.
inline std::string GetEditText(HWND hwnd) {
	if (!hwnd) return "";
	int len = ::GetWindowTextLengthW(hwnd);
	std::vector<wchar_t> buf((size_t)len + 1);
	::GetWindowTextW(hwnd, buf.data(), len + 1);
	return Narrow(buf.data());
}

inline void SetEditText(HWND hwnd, const std::string& value) {
	if (!hwnd) return;
	::SetWindowTextW(hwnd, Widen(value).c_str());
}

// Strict ISO (YYYY-MM-DD) parse: DateToDays() silently returns 0 for garbage
// input (sscanf_s just leaves unmatched fields as 0), so round-trip through
// DaysToDate() and require an EXACT string match — this also rejects
// civil-calendar nonsense like day 40 or month 13, which DaysFromCivil would
// otherwise silently normalize into some other (wrong) date instead of failing.
inline bool ParseIsoDateStrict(const std::string& text, long& outDays) {
	int y = 0, m = 0, d = 0;
	if (::sscanf_s(text.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return false;
	if (text.size() != 10 || text[4] != '-' || text[7] != '-') return false;
	if (m < 1 || m > 12 || d < 1 || d > 31 || y < 1) return false;
	long days = DateToDays(text);
	if (DaysToDate(days) != text) return false;
	outDays = days;
	return true;
}
