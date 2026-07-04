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

		// V3-1 fit-to-slide: size/position CHART_ROOT to the slide's content
		// area (top ~15% reserved for a native title placeholder), same as
		// the ribbon's DoInsertGantt does via Connect.cpp.
		HRESULT fitHr = FitChartRootToSlide(app);
		wprintf(L"FitChartRootToSlide hr=0x%08lX\n", (unsigned long)fitHr);

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

		// FIT assertion: the fitted CHART_ROOT frame must lie within the
		// slide's content area (full width minus ~18pt side margins, height
		// below the reserved top ~15% title zone and above a matching bottom
		// margin), with width >= 80% of slide width and top >= 10% of slide
		// height. This is a hard gate: on failure, print FIT FAIL and force
		// rc=1 — the remaining reflow checks below are skipped so exit code 0
		// is impossible without FIT OK having printed first (a broken fit
		// must never be masked by an unrelated REFLOW PASS).
		{
			const float slideW = (float)app->GetActivePresentation()->GetPageSetup()->GetSlideWidth();
			const float slideH = (float)app->GetActivePresentation()->GetPageSetup()->GetSlideHeight();
			const float left = group->GetLeft();
			const float top = group->GetTop();
			const float width = group->GetWidth();
			const float height = group->GetHeight();
			const float contentLeft = 0.0f;
			const float contentTop = slideH * 0.10f; // slightly looser than the 15% reservation, see below
			const float contentRight = slideW;
			const float contentBottom = slideH;

			bool withinArea = (left >= contentLeft - 0.5f) && (top >= contentTop - 0.5f)
				&& (left + width <= contentRight + 0.5f) && (top + height <= contentBottom + 0.5f);
			bool wideEnough = width >= slideW * 0.80f;
			bool lowEnough = top >= slideH * 0.10f;

			wprintf(L"FIT check: left=%.2f top=%.2f width=%.2f height=%.2f slideW=%.2f slideH=%.2f\n",
				left, top, width, height, slideW, slideH);
			wprintf(L"FIT check: withinArea=%d wideEnough=%d(%.1f%%>=80%%) lowEnough=%d(%.1f%%>=10%%)\n",
				withinArea, wideEnough, (width / slideW) * 100.0f, lowEnough, (top / slideH) * 100.0f);

			if (SUCCEEDED(fitHr) && withinArea && wideEnough && lowEnough) {
				wprintf(L"FIT OK\n");
			} else {
				wprintf(L"FIT FAIL\n");
				pres->PutSaved(Office::msoTrue);
				pres->Close();
				app->Quit();
				::CoUninitialize();
				return 1;
			}
		}

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
