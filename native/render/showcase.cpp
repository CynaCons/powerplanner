// S1 showcase: builds a pre-configured "Q3 Launch Plan" chart that mirrors the
// approved mockup (docs/mockup/onslide-mockup.html) and inserts it into a
// visible PowerPoint slide, then leaves PowerPoint open for the user. It also
// SaveAs-es the deck to argv[1] (if given) so the slide can be reopened later.
//
// This is a demo harness, NOT part of any gate. It deliberately builds its OWN
// document rather than touching MakeSampleDocument() (a frozen conformance /
// harness fixture).
#include "../PowerPlannerAddin/pch.h"
#include "../PowerPlannerAddin/GanttBuilder.h"
#include "../PowerPlannerAddin/GanttModel.h"
#include <cstdio>
#include <string>

static void SetHarnessDpiAwareness() {
	HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
	if (user32) {
		typedef BOOL(WINAPI * SetProcessDpiAwarenessContextFn)(DPI_AWARENESS_CONTEXT);
		auto pSetCtx = (SetProcessDpiAwarenessContextFn)::GetProcAddress(user32, "SetProcessDpiAwarenessContext");
		if (pSetCtx && pSetCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
	}
	::SetProcessDPIAware();
}

// A faithful port of the mockup's JS model (S.rows / S.tasks / ...), with day
// offsets resolved against a 2026-06-01 (Monday) origin so the two-band WEEK
// header reads exactly like the mockup.
static PpDocument MakeShowcaseDocument() {
	PpDocument doc;
	doc.title = "";              // native title placeholder fills the reserved zone
	doc.scale = "week";          // two-band months-over-weeks header

	// Rows. Phase 1 is a summary group over Research/Design. The two "task rows"
	// (Implementation / QA + polish) are top-level rows with an EMPTY row label,
	// so the rail shows only their rail task dot + name (the mockup's task-row
	// look). A single summary group keeps the layout clean.
	doc.rows = {
		{ "phase1",   "Phase 1", "" },
		{ "research", "Research", "phase1" },
		{ "design",   "Design",   "phase1" },
		{ "impl",     "",         "" },
		{ "qa",       "",         "" },
		{ "launch",   "Launch",   "" },
	};

	// Tasks. color drives the bar swatch (s1-theme-tokens); labelPlacement
	// "rail" moves the label into the rail (s1-rail-labels). Fields:
	// id, label, start, end, rowId, color, percent, labelPlacement.
	doc.tasks = {
		{ "discovery",  "Discovery",      "2026-06-01", "2026-06-12", "research", "#4355E0", 100, "" },
		{ "interviews", "Interviews",     "2026-06-08", "2026-06-19", "research", "#4355E0", 60,  "" },
		{ "wireframes", "Wireframes",     "2026-06-15", "2026-06-26", "design",   "#0E8D8A", 40,  "" },
		{ "visual",     "Visual design",  "2026-06-22", "2026-07-10", "design",   "#0E8D8A", 10,  "" },
		{ "impl_t",     "Implementation", "2026-07-06", "2026-07-31", "impl",     "#7A4FA3", 0,   "rail" },
		{ "qa_t",       "QA + polish",    "2026-07-27", "2026-08-07", "qa",       "#5B6C8F", 0,   "rail" },
	};

	doc.milestones = {
		{ "m_freeze", "Design freeze", "2026-07-10", "design", "" },
		{ "m_ship",   "Ship",          "2026-08-10", "launch", "" },
	};

	// Today (indigo) + deadline (red) markers.
	doc.markers = {
		{ "today",    "today",    "TODAY",         "2026-07-04", "" },
		{ "deadline", "deadline", "BOARD REVIEW",  "2026-07-30", "" },
	};

	// A note anchored to the Ship milestone. PpText: id, label, anchorId, rowId,
	// date, color; dx, dy.
	doc.texts = {
		{ "note1", "Go/No-Go with exec team", "m_ship", "", "", "", 14.0f, -30.0f },
	};

	return doc;
}

int wmain(int argc, wchar_t** argv) {
	const wchar_t* savePath = (argc > 1) ? argv[1] : NULL;
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

		const float slideW = (float)pres->GetPageSetup()->GetSlideWidth();
		const float slideH = (float)pres->GetPageSetup()->GetSlideHeight();

		// A native title placeholder in the reserved top ~15% zone (R1).
		PowerPoint::_SlidePtr slide = app->GetActiveWindow()->GetView()->GetSlide();
		PowerPoint::ShapesPtr shapes = slide->GetShapes();
		{
			PowerPoint::ShapePtr title = shapes->AddTextbox(Office::msoTextOrientationHorizontal,
				36.0f, 22.0f, slideW - 72.0f, slideH * 0.12f);
			PowerPoint::TextRangePtr tr = title->GetTextFrame()->GetTextRange();
			tr->PutText(_bstr_t(L"Q3 Launch Plan"));
			PowerPoint::FontPtr f = tr->GetFont();
			f->PutName(_bstr_t(L"Segoe UI Light"));
			f->PutSize(28.0f);
			f->GetColor()->PutPpRGB((Office::MsoRGBType)0x00261D1B); // ink (#1B1D26) in BGR
			title->GetLine()->PutVisible(Office::msoFalse);
			title->GetFill()->PutVisible(Office::msoFalse);
		}

		int cnt = 0;
		HRESULT hr = InsertGantt(app, MakeShowcaseDocument(), &cnt);
		wprintf(L"InsertGantt hr=0x%08lX shapes=%d\n", (unsigned long)hr, cnt);
		if (FAILED(hr)) throw _com_error(hr);

		HRESULT fitHr = FitChartRootToSlide(app);
		wprintf(L"FitChartRootToSlide hr=0x%08lX\n", (unsigned long)fitHr);

		if (savePath) {
			pres->SaveAs(_bstr_t(savePath), PowerPoint::ppSaveAsOpenXMLPresentation, Office::msoTriStateMixed);
			wprintf(L"saved -> %s\n", savePath);
		}

		wprintf(L"SHOWCASE OK (PowerPoint left open)\n");
		rc = 0;
		// Intentionally do NOT Close/Quit: leave the visible deck up for the user.
	}
	catch (const _com_error& e) {
		wprintf(L"COM error 0x%08lX: %s\n", (unsigned long)e.Error(),
			e.Description().length() ? (const wchar_t*)e.Description() : L"");
	}
	::CoUninitialize();
	return rc;
}
