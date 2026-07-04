// N5 reflow test: insert a chart, move task t5's bar by +14 days, reflow, and
// verify the document's t5 dates shifted accordingly (the "agent" reads the
// moved shape back into dates and re-emits).
#include "../PowerPlannerAddin/pch.h"
#include "../PowerPlannerAddin/GanttBuilder.h"
#include "../PowerPlannerAddin/GanttJson.h"
#include "../PowerPlannerAddin/GanttLayout.h"
#include <cstdio>
#include <string>

static std::string Narrow(const wchar_t* w) {
	if (!w || !*w) return "";
	int len = (int)::wcslen(w);
	int n = ::WideCharToMultiByte(CP_UTF8, 0, w, len, NULL, 0, NULL, NULL);
	std::string s(n, '\0');
	::WideCharToMultiByte(CP_UTF8, 0, w, len, &s[0], n, NULL, NULL);
	return s;
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
	const wchar_t* outPath = (argc > 1) ? argv[1] : NULL;
	SetHarnessDpiAwareness();
	::CoInitialize(NULL);
	int rc = 1;
	try {
		PowerPoint::_ApplicationPtr app;
		app.CreateInstance(L"PowerPoint.Application");
		app->PutVisible(Office::msoTrue);
		PowerPoint::_PresentationPtr pres = app->GetPresentations()->Add(Office::msoTrue);
		pres->GetSlides()->Add(1, PowerPoint::ppLayoutBlank);
		app->GetActiveWindow()->GetView()->GotoSlide(1);

		int cnt = 0;
		InsertGantt(app, MakeSampleDocument(), &cnt);

		PowerPoint::_SlidePtr slide = app->GetActiveWindow()->GetView()->GetSlide();
		PowerPoint::ShapesPtr shapes = slide->GetShapes();
		long n = shapes->GetCount();
		PowerPoint::ShapePtr group;
		for (long i = 1; i <= n; ++i) {
			PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
			_bstr_t k = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (k.length() && Narrow((const wchar_t*)k) == "CHART_ROOT") { group = sh; break; }
		}
		if (!group) { wprintf(L"no chart group found\n"); throw _com_error(E_FAIL); }

		std::string proj = Narrow((const wchar_t*)group->GetTags()->Item(_bstr_t(L"PP_PROJ")));
		long minDay = 0, pad = 0; float ptPerDay = 1.0f, originX = 0.0f;
		::sscanf_s(proj.c_str(), "{\"minDay\":%ld,\"pad\":%ld,\"ptPerDay\":%f,\"originX\":%f}", &minDay, &pad, &ptPerDay, &originX);
		wprintf(L"ptPerDay=%.4f\n", ptPerDay);

		// Move t5's bar right by 14 days.
		PowerPoint::GroupShapesPtr items = group->GetGroupItems();
		long m = items->GetCount();
		bool moved = false;
		for (long i = 1; i <= m; ++i) {
			PowerPoint::ShapePtr ch = items->Item(_variant_t(i));
			_bstr_t k = ch->GetTags()->Item(_bstr_t(L"PP_KIND"));
			_bstr_t id = ch->GetTags()->Item(_bstr_t(L"PP_ID"));
			if (k.length() && Narrow((const wchar_t*)k) == "TASK" && id.length() && Narrow((const wchar_t*)id) == "t5") {
				float oldLeft = ch->GetLeft();
				ch->PutLeft(oldLeft + 14.0f * ptPerDay);
				wprintf(L"moved t5 left %.1f -> %.1f (+14 days)\n", oldLeft, oldLeft + 14.0f * ptPerDay);
				moved = true;
				break;
			}
		}
		if (!moved) wprintf(L"t5 not found among group items\n");

		bool changed = false;
		HRESULT hr = ReflowFromSlide(app, &changed);
		wprintf(L"ReflowFromSlide hr=0x%08lX changed=%d\n", (unsigned long)hr, changed);

		std::string docJson = ReadGanttFromSlide(app);
		PpDocument doc = DocumentFromJson(docJson);
		for (const auto& t : doc.tasks)
			if (t.id == "t5") {
				wprintf(L"t5 now: %hs .. %hs   (was 2026-07-06 .. 2026-07-31; expect 2026-07-20 .. 2026-08-14)\n",
					t.start.c_str(), t.end.c_str());
				if (t.start == "2026-07-20" && t.end == "2026-08-14") { wprintf(L"REFLOW PASS\n"); rc = 0; }
				else wprintf(L"REFLOW MISMATCH\n");
			}

		if (outPath) {
			PowerPoint::_SlidePtr s2 = app->GetActiveWindow()->GetView()->GetSlide();
			s2->Export(_bstr_t(outPath), _bstr_t(L"PNG"), 1600, 900);
			wprintf(L"exported reflowed chart -> %s\n", outPath);
		}

		pres->PutSaved(Office::msoTrue);
		pres->Close();
		app->Quit();
	}
	catch (const _com_error& e) {
		wprintf(L"COM error 0x%08lX: %s\n", (unsigned long)e.Error(), e.Description().length() ? (const wchar_t*)e.Description() : L"");
	}
	::CoUninitialize();
	return rc;
}
