#include "../PowerPlannerAddin/pch.h"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

struct WindowRecord {
	HWND hwnd{};
	std::wstring cls;
	RECT win{};
	RECT client{};
	int depth{};
	bool visible{};
};

static std::string Narrow(const std::wstring& value) {
	if (value.empty()) return std::string();
	int needed = ::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, NULL, 0, NULL, NULL);
	if (needed <= 1) return std::string();
	std::string out((size_t)needed - 1, '\0');
	::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, &out[0], needed, NULL, NULL);
	return out;
}

static std::wstring WindowText(HWND hwnd) {
	wchar_t text[512] = {};
	::GetWindowTextW(hwnd, text, (int)(sizeof(text) / sizeof(text[0])));
	return text;
}

static std::wstring WindowClass(HWND hwnd) {
	wchar_t cls[256] = {};
	::GetClassNameW(hwnd, cls, (int)(sizeof(cls) / sizeof(cls[0])));
	return cls;
}

static bool StartsWithPptClass(const std::wstring& cls) {
	return cls.size() >= 3 &&
		::towupper(cls[0]) == L'P' &&
		::towupper(cls[1]) == L'P' &&
		::towupper(cls[2]) == L'T';
}

static bool ContainsPowerPoint(const std::wstring& text) {
	std::wstring lower = text;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t ch) { return (wchar_t)towlower(ch); });
	return lower.find(L"powerpoint") != std::wstring::npos ||
		lower.find(L"power point") != std::wstring::npos;
}

static bool IsKnownContainerClass(const std::wstring& cls) {
	std::wstring lower = cls;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t ch) { return (wchar_t)towlower(ch); });
	return lower == L"mdiclient" ||
		lower == L"mdiclass" ||
		lower.find(L"commandbar") != std::wstring::npos ||
		lower.find(L"workpane") != std::wstring::npos ||
		lower.find(L"nuipane") != std::wstring::npos ||
		lower.find(L"netui") != std::wstring::npos;
}

static void CollectTree(HWND hwnd, int depth, std::vector<WindowRecord>& records) {
	if (!::IsWindow(hwnd)) return;
	WindowRecord rec;
	rec.hwnd = hwnd;
	rec.cls = WindowClass(hwnd);
	rec.depth = depth;
	rec.visible = ::IsWindowVisible(hwnd) != FALSE;
	::GetWindowRect(hwnd, &rec.win);
	::GetClientRect(hwnd, &rec.client);
	records.push_back(rec);

	for (HWND child = ::GetWindow(hwnd, GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT)) {
		CollectTree(child, depth + 1, records);
	}
}

struct EnumTopData {
	DWORD pid{};
	HWND best{};
	int score{-1};
};

static BOOL CALLBACK EnumTopWindowsProc(HWND hwnd, LPARAM lparam) {
	EnumTopData* data = reinterpret_cast<EnumTopData*>(lparam);
	if (!::IsWindowVisible(hwnd)) return TRUE;
	DWORD pid = 0;
	::GetWindowThreadProcessId(hwnd, &pid);
	if (data->pid && pid != data->pid) return TRUE;

	std::wstring cls = WindowClass(hwnd);
	std::wstring title = WindowText(hwnd);
	int score = 0;
	if (StartsWithPptClass(cls)) score += 20;
	if (ContainsPowerPoint(title)) score += 15;
	RECT r{};
	if (::GetWindowRect(hwnd, &r)) {
		int w = r.right - r.left;
		int h = r.bottom - r.top;
		if (w > 400 && h > 300) score += 5;
	}
	if (score > data->score) {
		data->score = score;
		data->best = hwnd;
	}
	return TRUE;
}

static HWND FindPowerPointTopWindow(PowerPoint::_ApplicationPtr app) {
	HWND appHwnd = NULL;
	try { appHwnd = (HWND)(intptr_t)app->GetHWND(); } catch (...) {}
	if (appHwnd && ::IsWindow(appHwnd)) return appHwnd;

	HWND foreground = ::GetForegroundWindow();
	DWORD pid = 0;
	if (foreground) ::GetWindowThreadProcessId(foreground, &pid);

	EnumTopData data;
	data.pid = pid;
	::EnumWindows(EnumTopWindowsProc, reinterpret_cast<LPARAM>(&data));
	if (data.best) return data.best;

	data.pid = 0;
	data.best = NULL;
	data.score = -1;
	::EnumWindows(EnumTopWindowsProc, reinterpret_cast<LPARAM>(&data));
	return data.best;
}

static PowerPoint::_ApplicationPtr ConnectOrLaunchPowerPoint(bool& launched) {
	launched = false;
	PowerPoint::_ApplicationPtr app;
	CLSID clsid{};
	if (SUCCEEDED(::CLSIDFromProgID(L"PowerPoint.Application", &clsid))) {
		IUnknown* unk = nullptr;
		if (SUCCEEDED(::GetActiveObject(clsid, NULL, &unk)) && unk) {
			PowerPoint::_Application* raw = nullptr;
			HRESULT hr = unk->QueryInterface(__uuidof(PowerPoint::_Application), reinterpret_cast<void**>(&raw));
			unk->Release();
			if (SUCCEEDED(hr) && raw) {
				app.Attach(raw);
				return app;
			}
		}
	}
	app.CreateInstance(L"PowerPoint.Application");
	launched = true;
	return app;
}

static PowerPoint::_PresentationPtr EnsurePresentation(PowerPoint::_ApplicationPtr app, bool& createdPresentation) {
	createdPresentation = false;
	app->PutVisible(Office::msoTrue);
	PowerPoint::PresentationsPtr presentations = app->GetPresentations();
	PowerPoint::_PresentationPtr pres;
	if (presentations->GetCount() > 0) {
		try { pres = app->GetActivePresentation(); } catch (...) {}
		if (pres == nullptr) pres = presentations->Item(_variant_t(1L));
	} else {
		pres = presentations->Add(Office::msoTrue);
		createdPresentation = true;
	}

	PowerPoint::SlidesPtr slides = pres->GetSlides();
	if (slides->GetCount() < 1) {
		slides->Add(1, PowerPoint::ppLayoutBlank);
	}
	try { app->GetActiveWindow()->GetView()->GotoSlide(1); } catch (...) {}
	try { app->GetActiveWindow()->PutWindowState(PowerPoint::ppWindowMaximized); } catch (...) {}
	try { app->Activate(); } catch (...) {}
	HWND hwnd = NULL;
	try { hwnd = (HWND)(intptr_t)app->GetHWND(); } catch (...) {}
	if (hwnd) {
		::ShowWindow(hwnd, SW_SHOWMAXIMIZED);
		::SetForegroundWindow(hwnd);
	}
	::Sleep(500);
	return pres;
}

static int FindCandidate(const std::vector<WindowRecord>& records) {
	if (records.empty()) return -1;
	const RECT root = records.front().win;
	const int rootW = root.right - root.left;
	const int rootH = root.bottom - root.top;
	if (rootW <= 0 || rootH <= 0) return -1;

	int best = -1;
	long long bestScore = 0;
	for (size_t i = 1; i < records.size(); ++i) {
		const WindowRecord& rec = records[i];
		const int cw = rec.client.right - rec.client.left;
		const int ch = rec.client.bottom - rec.client.top;
		const int ww = rec.win.right - rec.win.left;
		const int wh = rec.win.bottom - rec.win.top;
		if (IsKnownContainerClass(rec.cls)) continue;
		if (!rec.visible || cw < 300 || ch < 200 || ww <= 0 || wh <= 0) continue;
		if (rec.depth < 2) continue;
		if (ww >= rootW * 90 / 100 && wh >= rootH * 90 / 100) continue;

		const int centerX = (rec.win.left + rec.win.right) / 2;
		const int centerY = (rec.win.top + rec.win.bottom) / 2;
		const int rootCenterX = (root.left + root.right) / 2;
		const int rootCenterY = (root.top + root.bottom) / 2;
		if (abs(centerX - rootCenterX) > rootW / 3) continue;
		if (abs(centerY - rootCenterY) > rootH / 3) continue;

		long long area = (long long)cw * ch;
		if (area < 60000) continue;
		long long score = area + (long long)rec.depth * 1000;
		if (score > bestScore) {
			bestScore = score;
			best = (int)i;
		}
	}
	return best;
}

static void WriteFallback() {
	std::ofstream out("window-classes.txt", std::ios::binary | std::ios::trunc);
	out << "FALLBACK_POLLING_ONLY\n";
}

static void WriteRecords(const std::vector<WindowRecord>& records, int candidate) {
	if (candidate < 0) {
		WriteFallback();
		return;
	}
	std::ofstream out("window-classes.txt", std::ios::binary | std::ios::trunc);
	for (const WindowRecord& rec : records) {
		const int cw = rec.client.right - rec.client.left;
		const int ch = rec.client.bottom - rec.client.top;
		std::ostringstream line;
		line << std::string((size_t)rec.depth * 2, ' ')
			<< "0x" << std::hex << std::uppercase << (uintptr_t)rec.hwnd << std::dec
			<< "  " << Narrow(rec.cls)
			<< "  win=(" << rec.win.left << "," << rec.win.top << "," << rec.win.right << "," << rec.win.bottom << ")"
			<< " client=(" << cw << "," << ch << ")\n";
		out << line.str();
	}
	out << "CANDIDATE_SLIDE_CLASS=" << Narrow(records[(size_t)candidate].cls) << "\n";
}

int wmain() {
	::SetProcessDPIAware();
	HRESULT coHr = ::CoInitialize(NULL);
	bool coInitialized = SUCCEEDED(coHr);
	bool launched = false;
	bool createdPresentation = false;
	PowerPoint::_ApplicationPtr app;
	PowerPoint::_PresentationPtr pres;
	try {
		app = ConnectOrLaunchPowerPoint(launched);
		pres = EnsurePresentation(app, createdPresentation);
		HWND root = FindPowerPointTopWindow(app);
		std::vector<WindowRecord> records;
		if (root) CollectTree(root, 0, records);
		int candidate = FindCandidate(records);
		WriteRecords(records, candidate);

		if (createdPresentation || launched) {
			try { pres->PutSaved(Office::msoTrue); } catch (...) {}
			try { pres->Close(); } catch (...) {}
		}
		if (launched) {
			try { app->Quit(); } catch (...) {}
		}
	} catch (...) {
		WriteFallback();
		if (createdPresentation || launched) {
			try { if (pres) pres->PutSaved(Office::msoTrue); } catch (...) {}
			try { if (pres) pres->Close(); } catch (...) {}
		}
		if (launched) {
			try { if (app) app->Quit(); } catch (...) {}
		}
	}
	if (coInitialized) ::CoUninitialize();
	wprintf(L"WINDOW PROBE OK\n");
	return 0;
}
