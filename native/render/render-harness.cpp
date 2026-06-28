// Render harness: drives PowerPoint via automation, calls the same InsertGantt
// emitter the add-in uses, and exports the slide to PNG. Serves as an automated
// end-to-end emission test and produces a shareable screenshot of the native
// shapes — no manual clicking required.
//
//   pprender.exe <output.png>
#include "../PowerPlannerAddin/pch.h"
#include "../PowerPlannerAddin/GanttBuilder.h"
#include <cstdio>

int wmain(int argc, wchar_t** argv) {
	const wchar_t* outPath = (argc > 1) ? argv[1] : L"gantt.png";
	::CoInitialize(NULL);
	int rc = 0;
	try {
		PowerPoint::_ApplicationPtr app;
		app.CreateInstance(L"PowerPoint.Application");
		app->PutVisible(Office::msoTrue);  // ensures an ActiveWindow exists

		PowerPoint::PresentationsPtr presses = app->GetPresentations();
		PowerPoint::_PresentationPtr pres = presses->Add(Office::msoTrue);
		PowerPoint::SlidesPtr slides = pres->GetSlides();
		PowerPoint::_SlidePtr slide = slides->Add(1, PowerPoint::ppLayoutBlank);
		app->GetActiveWindow()->GetView()->GotoSlide(1);

		int count = 0;
		HRESULT hr = InsertGantt(app, MakeSampleDocument(), &count);
		wprintf(L"InsertGantt hr=0x%08lX shapes=%d\n", (unsigned long)hr, count);

		slide->Export(_bstr_t(outPath), _bstr_t(L"PNG"), 1600, 900);
		wprintf(L"exported %s\n", outPath);

		pres->PutSaved(Office::msoTrue);
		pres->Close();
		app->Quit();
	} catch (const _com_error& e) {
		wprintf(L"COM error 0x%08lX: %s\n", (unsigned long)e.Error(),
			e.Description().length() ? (const wchar_t*)e.Description() : L"(no description)");
		rc = 1;
	}
	::CoUninitialize();
	return rc;
}
