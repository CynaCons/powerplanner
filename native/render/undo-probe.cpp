// undo-probe.cpp — discovery spike: PowerPoint Application.StartNewUndoEntry
// grouping semantics. Drives a fresh PowerPoint instance via COM, creates plain
// shapes, and empirically checks whether StartNewUndoEntry lets several
// automation edits collapse into ONE user-visible undo step.
//
// Output: undo-probe.txt next to the exe (native\build\undo-probe.txt),
// one finding per line, final line VERDICT: GROUPING_WORKS | SINGLE_STEP_ONLY | API_MISSING.
// Prints "UNDO PROBE OK" and exits 0 on clean completion regardless of verdict.
//
// COM teardown ordering copied from window-probe.cpp: release ALL COM pointers
// BEFORE CoUninitialize (releasing after CoUninitialize crashes).
// Unlike window-probe, this probe adds a detached watchdog thread
// (Sleep(120000); ExitProcess(3);) and unbuffered stdout so markers survive
// a forced exit.

#include "../PowerPlannerAddin/pch.h"

#include <cmath>
#include <comdef.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

static std::filesystem::path g_outPath;

static std::string Narrow(const std::wstring& value) {
	if (value.empty()) return std::string();
	int needed = ::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, NULL, 0, NULL, NULL);
	if (needed <= 1) return std::string();
	std::string out((size_t)needed - 1, '\0');
	::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, &out[0], needed, NULL, NULL);
	return out;
}

static std::string HrStr(HRESULT hr) {
	char buf[16] = {};
	sprintf_s(buf, "0x%08X", (unsigned)hr);
	return buf;
}

// Append one finding line to the output file (open/close per line so partial
// findings survive a watchdog ExitProcess) and echo to stdout.
static void LogFinding(const std::string& line) {
	printf("%s\n", line.c_str());
	std::ofstream out(g_outPath, std::ios::binary | std::ios::app);
	out << line << "\n";
}

static DWORD WINAPI WatchdogProc(LPVOID) {
	::Sleep(120000);
	::ExitProcess(3);
	return 0;
}

// ---------------------------------------------------------------------------
// Late-bound IDispatch helpers
// ---------------------------------------------------------------------------

// Invoke `name` on `disp`. Args must be in REVERSE order (IDispatch convention).
// Records GetIDsOfNames HRESULT separately so "API missing" is distinguishable
// from "call failed".
static HRESULT InvokeName(IDispatch* disp, LPCWSTR name, WORD flags,
                          VARIANT* args, UINT cArgs, VARIANT* result,
                          HRESULT* hrGetIds, std::string* excepOut) {
	DISPID dispid = 0;
	LPOLESTR nameOle = const_cast<LPOLESTR>(name);
	HRESULT hr = disp->GetIDsOfNames(IID_NULL, &nameOle, 1, LOCALE_USER_DEFAULT, &dispid);
	if (hrGetIds) *hrGetIds = hr;
	if (FAILED(hr)) return hr;

	DISPPARAMS dp = {};
	dp.rgvarg = args;
	dp.cArgs = cArgs;
	EXCEPINFO ei = {};
	UINT argErr = 0;
	hr = disp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, flags, &dp, result, &ei, &argErr);
	if (hr == DISP_E_EXCEPTION && excepOut) {
		std::string desc = ei.bstrDescription ? Narrow(ei.bstrDescription) : std::string();
		*excepOut = "scode=" + HrStr(ei.scode ? ei.scode : (HRESULT)ei.wCode) +
			(desc.empty() ? "" : (" desc=" + desc));
	}
	if (ei.bstrDescription) ::SysFreeString(ei.bstrDescription);
	if (ei.bstrSource) ::SysFreeString(ei.bstrSource);
	if (ei.bstrHelpFile) ::SysFreeString(ei.bstrHelpFile);
	return hr;
}

// ---------------------------------------------------------------------------
// Probe helpers
// ---------------------------------------------------------------------------

static bool Near(float a, float b) { return std::fabs(a - b) < 0.75f; }

static PowerPoint::ShapePtr ShapeByName(PowerPoint::_SlidePtr slide, const wchar_t* name) {
	try {
		return slide->GetShapes()->Item(_variant_t(name));
	} catch (...) {
		return nullptr;
	}
}

// Text readback may carry a trailing \r; compare ignoring it.
static bool TextEquals(const _bstr_t& actual, const wchar_t* expected) {
	std::wstring value = actual.length() ? (const wchar_t*)actual : L"";
	while (!value.empty() && (value.back() == L'\r' || value.back() == L'\n')) value.pop_back();
	return value == expected;
}

static std::string YesNo(bool v) { return v ? "YES" : "NO"; }

// Call Application.StartNewUndoEntry late-bound. Returns Invoke HRESULT,
// GetIDsOfNames HRESULT via out-param.
static HRESULT CallStartNewUndoEntry(IDispatch* appDisp, HRESULT* hrGetIds, std::string* excep) {
	return InvokeName(appDisp, L"StartNewUndoEntry", DISPATCH_METHOD, NULL, 0, NULL, hrGetIds, excep);
}

// Execute a single Undo. Tries CommandBars.ExecuteMso("Undo") first, falls
// back to CommandBars.FindControl(Type:=msoControlButton, Id:=128).Execute.
// Logs which mechanism was used. Returns true on success.
static bool ExecuteUndoOnce(IDispatch* appDisp, std::string& mechanismUsed) {
	_variant_t cbVar;
	HRESULT hrIds = S_OK;
	std::string excep;
	HRESULT hr = InvokeName(appDisp, L"CommandBars", DISPATCH_PROPERTYGET, NULL, 0, &cbVar, &hrIds, &excep);
	if (FAILED(hr) || cbVar.vt != VT_DISPATCH || !cbVar.pdispVal) {
		LogFinding("UNDO_EXEC_COMMANDBARS_GET_HR=" + HrStr(hr) + (excep.empty() ? "" : (" " + excep)));
		mechanismUsed = "NONE";
		return false;
	}
	IDispatchPtr cb = cbVar.pdispVal;

	// Attempt 1: CommandBars.ExecuteMso("Undo")
	{
		VARIANT arg;
		::VariantInit(&arg);
		arg.vt = VT_BSTR;
		arg.bstrVal = ::SysAllocString(L"Undo");
		excep.clear();
		hr = InvokeName(cb, L"ExecuteMso", DISPATCH_METHOD, &arg, 1, NULL, &hrIds, &excep);
		::VariantClear(&arg);
		LogFinding("UNDO_EXEC_EXECUTEMSO_GETIDS_HR=" + HrStr(hrIds) + " INVOKE_HR=" + HrStr(hr) +
			(excep.empty() ? "" : (" " + excep)));
		if (SUCCEEDED(hr)) {
			mechanismUsed = "CommandBars.ExecuteMso(\"Undo\")";
			return true;
		}
	}

	// Attempt 2: CommandBars.FindControl(Type:=1 msoControlButton, Id:=128).Execute
	{
		// IDispatch positional args are reversed: rgvarg[0]=Id, rgvarg[1]=Type.
		VARIANT fcArgs[2];
		::VariantInit(&fcArgs[0]);
		::VariantInit(&fcArgs[1]);
		fcArgs[0].vt = VT_I4; fcArgs[0].lVal = 128; // Id: standard Undo control
		fcArgs[1].vt = VT_I4; fcArgs[1].lVal = 1;   // Type: msoControlButton
		_variant_t ctrlVar;
		excep.clear();
		hr = InvokeName(cb, L"FindControl", DISPATCH_METHOD, fcArgs, 2, &ctrlVar, &hrIds, &excep);
		LogFinding("UNDO_EXEC_FINDCONTROL_HR=" + HrStr(hr) + (excep.empty() ? "" : (" " + excep)));
		if (SUCCEEDED(hr) && ctrlVar.vt == VT_DISPATCH && ctrlVar.pdispVal) {
			IDispatchPtr ctrl = ctrlVar.pdispVal;
			excep.clear();
			hr = InvokeName(ctrl, L"Execute", DISPATCH_METHOD, NULL, 0, NULL, &hrIds, &excep);
			LogFinding("UNDO_EXEC_FINDCONTROL_EXECUTE_HR=" + HrStr(hr) + (excep.empty() ? "" : (" " + excep)));
			if (SUCCEEDED(hr)) {
				mechanismUsed = "CommandBars.FindControl(Id=128).Execute";
				return true;
			}
		} else {
			LogFinding("UNDO_EXEC_FINDCONTROL_NO_CONTROL=YES");
		}
	}

	mechanismUsed = "NONE";
	return false;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int wmain() {
	setvbuf(stdout, NULL, _IONBF, 0);
	::SetProcessDPIAware();

	// Output file lives next to the exe so cwd does not matter.
	{
		wchar_t exePath[MAX_PATH] = {};
		::GetModuleFileNameW(NULL, exePath, MAX_PATH);
		g_outPath = std::filesystem::path(exePath).parent_path() / L"undo-probe.txt";
		std::ofstream truncate(g_outPath, std::ios::binary | std::ios::trunc);
	}

	// Detached watchdog: hard exit after 120s no matter what.
	{
		HANDLE watchdog = ::CreateThread(NULL, 0, WatchdogProc, NULL, 0, NULL);
		if (watchdog) ::CloseHandle(watchdog);
	}

	HRESULT coHr = ::CoInitialize(NULL);
	bool coInitialized = SUCCEEDED(coHr);

	// Verdict evidence, tracked outside try so the catch path can still verdict.
	bool snueExists = false;       // GetIDsOfNames found StartNewUndoEntry
	bool snueCallable = false;     // Invoke succeeded
	bool probe2AllReverted = false;
	bool probe2bAllReverted = false;

	PowerPoint::_ApplicationPtr app;
	PowerPoint::_PresentationPtr pres;
	try {
		if (FAILED(app.CreateInstance(L"PowerPoint.Application")) || app == nullptr) {
			LogFinding("ERROR: PowerPoint.Application CreateInstance failed");
			LogFinding("VERDICT: API_MISSING");
			if (coInitialized) ::CoUninitialize();
			printf("UNDO PROBE OK\n");
			return 0;
		}
		app->PutVisible(Office::msoTrue);
		try { LogFinding("PPT_VERSION=" + Narrow((const wchar_t*)app->GetVersion())); } catch (...) {}

		// Fresh presentation with one blank slide and three plain shapes.
		pres = app->GetPresentations()->Add(Office::msoTrue);
		PowerPoint::SlidesPtr slides = pres->GetSlides();
		PowerPoint::_SlidePtr slide = slides->Add(1, PowerPoint::ppLayoutBlank);
		PowerPoint::ShapesPtr shapes = slide->GetShapes();

		PowerPoint::ShapePtr shapeA = shapes->AddShape(Office::msoShapeRectangle, 100.0f, 100.0f, 120.0f, 80.0f);
		shapeA->PutName(_bstr_t(L"PP_A"));
		PowerPoint::ShapePtr shapeB = shapes->AddShape(Office::msoShapeOval, 300.0f, 100.0f, 120.0f, 80.0f);
		shapeB->PutName(_bstr_t(L"PP_B"));
		PowerPoint::ShapePtr shapeC = shapes->AddTextbox(Office::msoTextOrientationHorizontal, 100.0f, 300.0f, 240.0f, 60.0f);
		shapeC->PutName(_bstr_t(L"PP_C"));
		shapeC->GetTextFrame()->GetTextRange()->PutText(_bstr_t(L"Original"));
		::Sleep(500);

		IDispatchPtr appDisp = app;

		// -------------------------------------------------------------------
		// Probe 1: does Application.StartNewUndoEntry exist and succeed?
		// (This first call also seals the setup edits into a prior undo entry.)
		// -------------------------------------------------------------------
		{
			HRESULT hrIds = E_FAIL;
			std::string excep;
			HRESULT hrInv = CallStartNewUndoEntry(appDisp, &hrIds, &excep);
			snueExists = SUCCEEDED(hrIds);
			snueCallable = SUCCEEDED(hrInv);
			LogFinding("START_NEW_UNDO_ENTRY_GETIDS_HR=" + HrStr(hrIds));
			LogFinding("START_NEW_UNDO_ENTRY_INVOKE_HR=" + HrStr(hrInv) + (excep.empty() ? "" : (" " + excep)));
		}

		std::string undoMechanism = "NONE";

		if (snueCallable) {
			// ---------------------------------------------------------------
			// Probe 2: SNUE once -> move A, move B, retitle C -> ONE Undo.
			// ---------------------------------------------------------------
			shapeA->PutLeft(200.0f);
			shapeB->PutTop(220.0f);
			shapeC->GetTextFrame()->GetTextRange()->PutText(_bstr_t(L"Edited"));
			::Sleep(300);

			bool editsApplied = Near(shapeA->GetLeft(), 200.0f) &&
				Near(shapeB->GetTop(), 220.0f) &&
				TextEquals(shapeC->GetTextFrame()->GetTextRange()->GetText(), L"Edited");
			LogFinding("PROBE2_EDITS_APPLIED=" + YesNo(editsApplied));

			bool undoOk = ExecuteUndoOnce(appDisp, undoMechanism);
			LogFinding("UNDO_MECHANISM=" + undoMechanism);
			::Sleep(500);

			shapeA = ShapeByName(slide, L"PP_A");
			shapeB = ShapeByName(slide, L"PP_B");
			shapeC = ShapeByName(slide, L"PP_C");
			bool aReverted = shapeA != nullptr && Near(shapeA->GetLeft(), 100.0f);
			bool bReverted = shapeB != nullptr && Near(shapeB->GetTop(), 100.0f);
			bool cReverted = shapeC != nullptr &&
				TextEquals(shapeC->GetTextFrame()->GetTextRange()->GetText(), L"Original");
			int revertedCount = (aReverted ? 1 : 0) + (bReverted ? 1 : 0) + (cReverted ? 1 : 0);
			LogFinding("PROBE2_AFTER_UNDO_A_MOVE_REVERTED=" + YesNo(aReverted));
			LogFinding("PROBE2_AFTER_UNDO_B_MOVE_REVERTED=" + YesNo(bReverted));
			LogFinding("PROBE2_AFTER_UNDO_C_TEXT_REVERTED=" + YesNo(cReverted));
			char countLine[64];
			sprintf_s(countLine, "PROBE2_REVERTED_COUNT=%d/3", revertedCount);
			LogFinding(countLine);
			probe2AllReverted = undoOk && revertedCount == 3;
			LogFinding(std::string("PROBE2_RESULT=") +
				(!undoOk ? "UNDO_EXEC_FAILED" :
					revertedCount == 3 ? "ALL_REVERTED" :
					revertedCount == 0 ? "NONE_REVERTED" : "PARTIAL_REVERTED"));

			// ---------------------------------------------------------------
			// Probe 2b: same edits but with a TRAILING SNUE before Undo, in
			// case grouping requires sealing the entry explicitly.
			// ---------------------------------------------------------------
			if (undoOk && shapeA != nullptr && shapeB != nullptr && shapeC != nullptr) {
				float baseLeft = shapeA->GetLeft();
				float baseTop = shapeB->GetTop();
				_bstr_t baseText = shapeC->GetTextFrame()->GetTextRange()->GetText();

				HRESULT hrIds = E_FAIL;
				CallStartNewUndoEntry(appDisp, &hrIds, NULL);
				shapeA->PutLeft(baseLeft + 37.0f);
				shapeB->PutTop(baseTop + 37.0f);
				shapeC->GetTextFrame()->GetTextRange()->PutText(_bstr_t(L"Edited2b"));
				CallStartNewUndoEntry(appDisp, &hrIds, NULL); // trailing seal
				::Sleep(300);

				std::string mech2b;
				bool undo2bOk = ExecuteUndoOnce(appDisp, mech2b);
				::Sleep(500);

				shapeA = ShapeByName(slide, L"PP_A");
				shapeB = ShapeByName(slide, L"PP_B");
				shapeC = ShapeByName(slide, L"PP_C");
				bool a2b = shapeA != nullptr && Near(shapeA->GetLeft(), baseLeft);
				bool b2b = shapeB != nullptr && Near(shapeB->GetTop(), baseTop);
				bool c2b = shapeC != nullptr &&
					shapeC->GetTextFrame()->GetTextRange()->GetText() == baseText;
				int count2b = (a2b ? 1 : 0) + (b2b ? 1 : 0) + (c2b ? 1 : 0);
				probe2bAllReverted = undo2bOk && count2b == 3;
				char line2b[128];
				sprintf_s(line2b, "PROBE2B_TRAILING_SNUE_REVERTED_COUNT=%d/3", count2b);
				LogFinding(line2b);
				LogFinding(std::string("PROBE2B_RESULT=") +
					(!undo2bOk ? "UNDO_EXEC_FAILED" :
						count2b == 3 ? "ALL_REVERTED" :
						count2b == 0 ? "NONE_REVERTED" : "PARTIAL_REVERTED"));
			} else {
				LogFinding("PROBE2B_RESULT=SKIPPED");
			}

			// ---------------------------------------------------------------
			// Probe 3: SNUE -> delete PP_B, add PP_D -> ONE Undo. What's back?
			// Uses trailing SNUE only if probe 2 needed it.
			// ---------------------------------------------------------------
			if (undoOk) {
				bool useTrailing = !probe2AllReverted && probe2bAllReverted;
				HRESULT hrIds = E_FAIL;
				CallStartNewUndoEntry(appDisp, &hrIds, NULL);
				PowerPoint::ShapePtr doomed = ShapeByName(slide, L"PP_B");
				if (doomed != nullptr) doomed->Delete();
				doomed = nullptr;
				PowerPoint::ShapePtr shapeD = shapes->AddShape(Office::msoShapeRectangle, 500.0f, 300.0f, 120.0f, 80.0f);
				shapeD->PutName(_bstr_t(L"PP_D"));
				shapeD = nullptr;
				if (useTrailing) CallStartNewUndoEntry(appDisp, &hrIds, NULL);
				::Sleep(300);
				LogFinding(std::string("PROBE3_TRAILING_SNUE_USED=") + YesNo(useTrailing));

				std::string mech3;
				bool undo3Ok = ExecuteUndoOnce(appDisp, mech3);
				::Sleep(500);

				std::string names;
				long shapeCount = shapes->GetCount();
				for (long i = 1; i <= shapeCount; ++i) {
					_bstr_t name = shapes->Item(_variant_t(i))->GetName();
					if (!names.empty()) names += ",";
					names += Narrow(name.length() ? (const wchar_t*)name : L"");
				}
				LogFinding("PROBE3_SHAPES_AFTER_UNDO=" + names);
				bool bRestored = ShapeByName(slide, L"PP_B") != nullptr;
				bool dRemoved = ShapeByName(slide, L"PP_D") == nullptr;
				LogFinding("PROBE3_DELETED_SHAPE_RESTORED=" + YesNo(bRestored));
				LogFinding("PROBE3_ADDED_SHAPE_REMOVED=" + YesNo(dRemoved));
				LogFinding(std::string("PROBE3_RESULT=") +
					(!undo3Ok ? "UNDO_EXEC_FAILED" :
						(bRestored && dRemoved) ? "DELETE_AND_ADD_GROUPED" :
						(!bRestored && dRemoved) ? "ONLY_ADD_UNDONE" :
						(bRestored && !dRemoved) ? "ONLY_DELETE_UNDONE" : "NOTHING_UNDONE"));
			} else {
				LogFinding("PROBE3_RESULT=SKIPPED_UNDO_EXEC_FAILED");
			}
		} else {
			LogFinding("PROBE2_RESULT=SKIPPED_API_UNAVAILABLE");
			LogFinding("PROBE2B_RESULT=SKIPPED_API_UNAVAILABLE");
			LogFinding("PROBE3_RESULT=SKIPPED_API_UNAVAILABLE");
		}

		// Verdict.
		std::string verdict;
		if (!snueExists || !snueCallable) verdict = "API_MISSING";
		else if (probe2AllReverted || probe2bAllReverted) verdict = "GROUPING_WORKS";
		else verdict = "SINGLE_STEP_ONLY";
		LogFinding("VERDICT: " + verdict);

		// Cleanup (all exit paths quit + release before CoUninitialize).
		try { pres->PutSaved(Office::msoTrue); } catch (...) {}
		try { pres->Close(); } catch (...) {}
		try { app->Quit(); } catch (...) {}
	} catch (_com_error& err) {
		LogFinding(std::string("ERROR: _com_error ") + HrStr(err.Error()));
		LogFinding(std::string("VERDICT: ") + (snueExists && snueCallable ? "SINGLE_STEP_ONLY" : "API_MISSING"));
		try { if (pres) { pres->PutSaved(Office::msoTrue); pres->Close(); } } catch (...) {}
		try { if (app) app->Quit(); } catch (...) {}
	} catch (...) {
		LogFinding("ERROR: unknown exception");
		LogFinding(std::string("VERDICT: ") + (snueExists && snueCallable ? "SINGLE_STEP_ONLY" : "API_MISSING"));
		try { if (pres) { pres->PutSaved(Office::msoTrue); pres->Close(); } } catch (...) {}
		try { if (app) app->Quit(); } catch (...) {}
	}
	pres = nullptr;
	app = nullptr;
	if (coInitialized) ::CoUninitialize();
	printf("UNDO PROBE OK\n");
	return 0;
}
