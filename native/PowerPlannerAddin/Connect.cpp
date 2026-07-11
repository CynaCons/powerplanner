#include "pch.h"
#include "Connect.h"
#include "GanttBuilder.h"
#include "GanttJson.h"
#include "GanttOps.h"
#include "Overlay.h"
#include <functional>
#include <string>

// Type-library GUIDs used by the IDispatchImpl bases (named_guids is omitted in
// the #import to avoid duplicate-COMDAT; see pch.h).
//   Add-in Designer: {AC0714F2-3D04-11D1-AE7D-00A0C90F26F4}
//   Microsoft Office: {2DF8D04C-5BFA-101B-BDE5-00AA0044DE52}
extern const GUID LIBID_AddInDesigner_PP =
	{ 0xAC0714F2, 0x3D04, 0x11D1, { 0xAE, 0x7D, 0x00, 0xA0, 0xC9, 0x0F, 0x26, 0xF4 } };
extern const GUID LIBID_Office_PP =
	{ 0x2DF8D04C, 0x5BFA, 0x101B, { 0xBD, 0xE5, 0x00, 0xAA, 0x00, 0x44, 0xDE, 0x52 } };

// Append a line to %TEMP%\powerplanner-addin.log. Used to prove (from outside
// PowerPoint) that our DLL loaded into POWERPNT.EXE and the lifecycle fired.
static void PpLog(const wchar_t* msg)
{
	wchar_t path[MAX_PATH];
	DWORD n = ::GetTempPathW(MAX_PATH, path);
	if (n == 0 || n > MAX_PATH) return;
	::wcscat_s(path, MAX_PATH, L"powerplanner-addin.log");
	HANDLE h = ::CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return;
	SYSTEMTIME st; ::GetLocalTime(&st);
	wchar_t line[512];
	int len = ::swprintf_s(line, 512, L"%04d-%02d-%02d %02d:%02d:%02d  [pid %lu]  %s\r\n",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
		::GetCurrentProcessId(), msg);
	if (len > 0) {
		// Write UTF-16LE bytes (good enough for a debug log).
		DWORD written = 0;
		::WriteFile(h, line, (DWORD)(len * sizeof(wchar_t)), &written, NULL);
	}
	::CloseHandle(h);
}

// Late-bound Application.StartNewUndoEntry — same idiom as
// native/render/undo-probe.cpp's CallStartNewUndoEntry/InvokeName and
// Overlay.cpp's StartNewUndoEntryIfPossible (kept as a separate copy here
// since Connect.cpp and Overlay.cpp are different translation units with no
// shared internal-linkage helpers). Called at the START of MutateChart,
// before any mutation, so the whole ribbon/context-menu edit — including
// UpdateGantt's occasional ungroup/regroup — collapses into ONE undo entry.
// Best-effort: swallows failure, logs the HRESULT via PpLog.
static void StartNewUndoEntryIfPossible(IDispatch* appDisp) {
	if (!appDisp) return;
	try {
		DISPID dispid = 0;
		LPOLESTR name = const_cast<LPOLESTR>(L"StartNewUndoEntry");
		HRESULT hrIds = appDisp->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &dispid);
		if (FAILED(hrIds)) {
			wchar_t buf[80];
			::swprintf_s(buf, 80, L"StartNewUndoEntry: GetIDsOfNames failed hr=0x%08lX", (unsigned long)hrIds);
			PpLog(buf);
			return;
		}
		DISPPARAMS dp = {};
		EXCEPINFO ei = {};
		UINT argErr = 0;
		HRESULT hrInv = appDisp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, NULL, &ei, &argErr);
		if (FAILED(hrInv)) {
			wchar_t buf[80];
			::swprintf_s(buf, 80, L"StartNewUndoEntry: Invoke failed hr=0x%08lX", (unsigned long)hrInv);
			PpLog(buf);
		}
		if (ei.bstrDescription) ::SysFreeString(ei.bstrDescription);
		if (ei.bstrSource) ::SysFreeString(ei.bstrSource);
		if (ei.bstrHelpFile) ::SysFreeString(ei.bstrHelpFile);
	}
	catch (const _com_error& e) {
		wchar_t buf[80];
		::swprintf_s(buf, 80, L"StartNewUndoEntry: COM error 0x%08lX", (unsigned long)e.Error());
		PpLog(buf);
	}
	catch (...) {
		PpLog(L"StartNewUndoEntry: unknown error");
	}
}

static std::string Narrow(const wchar_t* w) {
	if (!w || !*w) return "";
	int len = (int)::wcslen(w);
	int n = ::WideCharToMultiByte(CP_UTF8, 0, w, len, NULL, 0, NULL, NULL);
	std::string s(n, '\0');
	::WideCharToMultiByte(CP_UTF8, 0, w, len, &s[0], n, NULL, NULL);
	return s;
}

static const PpTask* FindTask(const PpDocument& doc, const std::string& id) {
	for (const auto& task : doc.tasks) {
		if (task.id == id) return &task;
	}
	return nullptr;
}

static std::string FirstRowId(const PpDocument& doc) {
	return doc.rows.empty() ? "" : doc.rows.front().id;
}

static std::string RowForSelection(const PpDocument& doc, const std::string& kind, const std::string& id) {
	if (kind == "TASK" || kind == "TASK_PROGRESS") {
		const PpTask* task = FindTask(doc, id);
		if (task) return task->rowId;
	}
	if (kind == "ROW_LABEL") {
		for (const auto& row : doc.rows) {
			if (row.id == id) return row.id;
		}
	}
	return FirstRowId(doc);
}

static void DefaultTaskDates(const PpDocument& doc, const std::string& rowId, const std::string& selectedTaskId, std::string& start, std::string& end) {
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

// ---- WindowSelectionChange COM sink (SR-SMO-05 / ARC-07) -------------------
// PowerPoint fires EApplication::WindowSelectionChange whenever the slide
// selection changes. We subscribe (IConnectionPointContainer on the Application)
// so a native pick of a suppressed chart CHILD is unselected INSTANTLY — killing
// the up-to-150ms window in which the Tick poll would otherwise let a child stay
// natively selected (the M6 delete-desync race). The suppression/mirror/clear
// decision itself lives in Overlay_OnNativeSelectionChanged, the SAME handler
// the Tick poll calls, so the poll-only harness (which never loads this add-in)
// behaves identically and the poll remains a watchdog fallback here.
//
// A minimal hand-rolled IDispatch sink: the connection point invokes us by the
// event dispid, so no type info is needed. WindowSelectionChange is dispid 2001
// (frozen in the PowerPoint EApplication contract across versions).
namespace {
const DISPID DISPID_EAPP_WINDOWSELECTIONCHANGE = 2001;

class PpSelectionSink : public IDispatch {
public:
	PpSelectionSink() : m_ref(1) {}

	// IUnknown
	STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IDispatch ||
			riid == __uuidof(PowerPoint::EApplication)) {
			*ppv = static_cast<IDispatch*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	STDMETHOD_(ULONG, AddRef)() override { return (ULONG)::InterlockedIncrement(&m_ref); }
	STDMETHOD_(ULONG, Release)() override {
		LONG r = ::InterlockedDecrement(&m_ref);
		if (r == 0) delete this;
		return (ULONG)r;
	}

	// IDispatch — type-info-less; the source calls Invoke with the event dispid.
	STDMETHOD(GetTypeInfoCount)(UINT* pctinfo) override { if (pctinfo) *pctinfo = 0; return S_OK; }
	STDMETHOD(GetTypeInfo)(UINT, LCID, ITypeInfo** ppTInfo) override { if (ppTInfo) *ppTInfo = nullptr; return E_NOTIMPL; }
	STDMETHOD(GetIDsOfNames)(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
	STDMETHOD(Invoke)(DISPID dispIdMember, REFIID, LCID, WORD, DISPPARAMS* pDispParams,
		VARIANT*, EXCEPINFO*, UINT*) override {
		if (dispIdMember == DISPID_EAPP_WINDOWSELECTIONCHANGE && pDispParams && pDispParams->cArgs >= 1) {
			HandleWindowSelectionChange(pDispParams->rgvarg[pDispParams->cArgs - 1]);
		}
		return S_OK;
	}

private:
	// Sel is the last positional arg (event args are pushed in reverse in
	// rgvarg; WindowSelectionChange has a single arg, so index cArgs-1).
	void HandleWindowSelectionChange(const VARIANT& selArg) {
		try {
			// Extract the Selection via an EXPLICIT QueryInterface (no reliance on
			// _com_ptr_t's IDispatch* assignment overload) so we always hold a
			// genuine Selection vtable regardless of how PowerPoint marshals the
			// arg (VT_DISPATCH, VT_DISPATCH|VT_BYREF, or VT_UNKNOWN).
			IUnknown* unk = nullptr;
			if (selArg.vt == VT_DISPATCH) unk = selArg.pdispVal;
			else if ((selArg.vt == (VT_DISPATCH | VT_BYREF)) && selArg.ppdispVal) unk = *selArg.ppdispVal;
			else if (selArg.vt == VT_UNKNOWN) unk = selArg.punkVal;
			else if ((selArg.vt == (VT_UNKNOWN | VT_BYREF)) && selArg.ppunkVal) unk = *selArg.ppunkVal;
			if (!unk) return;

			PowerPoint::SelectionPtr sel;
			{
				PowerPoint::Selection* raw = nullptr;
				if (FAILED(unk->QueryInterface(__uuidof(PowerPoint::Selection), reinterpret_cast<void**>(&raw))) || !raw)
					return;
				sel.Attach(raw);   // take ownership of the QI ref (released on scope exit)
			}

			std::string kind, id;
			bool hasShapeSel = false;
			if (sel->GetType() == PowerPoint::ppSelectionShapes) {
				PowerPoint::ShapeRangePtr sr = sel->GetShapeRange();
				if (sr && sr->GetCount() >= 1) {
					hasShapeSel = true;
					PowerPoint::ShapePtr sh = sr->Item(_variant_t(1L));
					_bstr_t k = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
					kind = k.length() ? Narrow((const wchar_t*)k) : "";
					_bstr_t i = sh->GetTags()->Item(_bstr_t(L"PP_ID"));
					id = i.length() ? Narrow((const wchar_t*)i) : "";
				}
			}

			int action = Overlay_OnNativeSelectionChanged(kind.c_str(), id.c_str(), hasShapeSel);
			if (action == OVERLAY_NATIVE_SEL_SUPPRESS_CHILD) {
				// Instant equivalent of the Tick poll's Unselect(). Re-fires this
				// event with an empty/CHART_ROOT selection, which resolves to
				// NONE next dispatch — bounded, no recursion.
				try { sel->Unselect(); } catch (...) {}
			}
		} catch (...) {
			// Never let an event-sink exception escape into PowerPoint.
		}
	}

	LONG m_ref;
};
} // namespace

// The Fluent ribbon: a "PowerPlanner" tab with one "Insert Gantt" button.
// onLoad caches the IRibbonUI; the button's onAction reaches DoInsertGantt().
static const wchar_t* kRibbonXml =
	L"<customUI xmlns='http://schemas.microsoft.com/office/2009/07/customui' onLoad='OnRibbonLoad'>"
	L"  <ribbon>"
	L"    <tabs>"
	L"      <tab id='ppTab' label='PowerPlanner'>"
	L"        <group id='ppGroup' label='Chart'>"
	L"          <button id='ppInsert' label='Insert Gantt' size='large'"
	L"                  imageMso='ChartTypeColumnInsertGallery' onAction='OnInsertGantt'"
	L"                  screentip='Insert a Gantt chart' supertip='Emit a Gantt chart as native PowerPoint shapes on the current slide.'/>"
	L"          <button id='ppPull' label='Pull from slide' size='large'"
	L"                  imageMso='Refresh' onAction='OnPullGantt'"
	L"                  screentip='Pull from slide' supertip='Read the PowerPlanner chart embedded on the current slide back into a document.'/>"
	L"          <button id='ppReflow' label='Reflow' size='large'"
	L"                  imageMso='RecurrenceEdit' onAction='OnReflowGantt'"
	L"                  screentip='Reflow from edits' supertip='Read moved/resized task bars back into dates and reflow the chart (dependencies, summary, and embedded data stay in sync).'/>"
	L"        </group>"
	L"      </tab>"
	L"    </tabs>"
	L"  </ribbon>"
	L"  <contextMenus>"
	L"    <contextMenu idMso='ContextMenuShape'>"
	L"      <button id='ppCtxAddTask' label='Add Task' onAction='OnCtxAddTask'/>"
	L"      <button id='ppCtxAddRow' label='Add Row Below' onAction='OnCtxAddRow'/>"
	L"      <button id='ppCtxDelete' label='Delete Selected' onAction='OnCtxDelete'/>"
	L"      <button id='ppCtxNudgePlus' label='Nudge +1 Day' onAction='OnCtxNudgePlus'/>"
	L"      <button id='ppCtxNudgeMinus' label='Nudge -1 Day' onAction='OnCtxNudgeMinus'/>"
	L"      <button id='ppCtxPctPlus' label='Increase %' onAction='OnCtxPctPlus'/>"
	L"      <button id='ppCtxPctMinus' label='Decrease %' onAction='OnCtxPctMinus'/>"
	L"      <menu id='ppCtxScale' label='Change Scale'>"
	L"        <button id='ppScaleDay' label='Day' onAction='OnCtxScaleDay'/>"
	L"        <button id='ppScaleWeek' label='Week' onAction='OnCtxScaleWeek'/>"
	L"        <button id='ppScaleMonth' label='Month' onAction='OnCtxScaleMonth'/>"
	L"        <button id='ppScaleQuarter' label='Quarter' onAction='OnCtxScaleQuarter'/>"
	L"        <button id='ppScaleYear' label='Year' onAction='OnCtxScaleYear'/>"
	L"      </menu>"
	L"    </contextMenu>"
	L"  </contextMenus>"
	L"</customUI>";

// ---- IDTExtensibility2 -----------------------------------------------------

STDMETHODIMP CConnect::OnConnection(IDispatch* Application,
	AddInDesignerObjects::ext_ConnectMode /*ConnectMode*/,
	IDispatch* /*AddInInst*/, SAFEARRAY** /*custom*/)
{
	m_pApp = Application;  // PowerPoint.Application — used from N2 onward
	PpLog(L"OnConnection — add-in loaded into PowerPoint");
	OverlayStart(m_pApp);  // N4: on-slide selection overlay (polling timer)
	AdviseSelectionSink(); // SR-SMO-05: instant WindowSelectionChange suppression
	return S_OK;
}

STDMETHODIMP CConnect::OnDisconnection(AddInDesignerObjects::ext_DisconnectMode /*RemoveMode*/, SAFEARRAY** /*custom*/)
{
	UnadviseSelectionSink();
	OverlayStop();
	m_pRibbon.Release();
	m_pApp.Release();
	return S_OK;
}

STDMETHODIMP CConnect::OnAddInsUpdate(SAFEARRAY** /*custom*/) { return S_OK; }
STDMETHODIMP CConnect::OnStartupComplete(SAFEARRAY** /*custom*/) { return S_OK; }
STDMETHODIMP CConnect::OnBeginShutdown(SAFEARRAY** /*custom*/) { return S_OK; }

// ---- IRibbonExtensibility --------------------------------------------------

STDMETHODIMP CConnect::GetCustomUI(BSTR /*RibbonID*/, BSTR* RibbonXml)
{
	if (!RibbonXml) return E_POINTER;
	PpLog(L"GetCustomUI — PowerPoint requested ribbon XML");
	*RibbonXml = SysAllocString(kRibbonXml);
	return *RibbonXml ? S_OK : E_OUTOFMEMORY;
}

// ---- Ribbon callback dispatch ---------------------------------------------

STDMETHODIMP CConnect::GetIDsOfNames(REFIID riid, LPOLESTR* rgszNames, UINT cNames,
	LCID lcid, DISPID* rgDispId)
{
	if (cNames >= 1 && rgszNames && rgszNames[0])
	{
		if (_wcsicmp(rgszNames[0], L"OnRibbonLoad") == 0) { rgDispId[0] = DISPID_PP_ONLOAD; return S_OK; }
		if (_wcsicmp(rgszNames[0], L"OnInsertGantt") == 0) { rgDispId[0] = DISPID_PP_INSERT_GANTT; return S_OK; }
		if (_wcsicmp(rgszNames[0], L"OnPullGantt") == 0) { rgDispId[0] = DISPID_PP_PULL_GANTT; return S_OK; }
		if (_wcsicmp(rgszNames[0], L"OnReflowGantt") == 0) { rgDispId[0] = DISPID_PP_REFLOW_GANTT; return S_OK; }
		if (_wcsicmp(rgszNames[0], L"OnCtxAddTask") == 0) { rgDispId[0] = DISPID_PP_CTX_ADD_TASK; return S_OK; }
		if (_wcsicmp(rgszNames[0], L"OnCtxAddRow") == 0) { rgDispId[0] = DISPID_PP_CTX_ADD_ROW; return S_OK; }
		if (_wcsicmp(rgszNames[0], L"OnCtxDelete") == 0) { rgDispId[0] = DISPID_PP_CTX_DELETE; return S_OK; }
		if (_wcsicmp(rgszNames[0], L"OnCtxNudgePlus") == 0) { rgDispId[0] = DISPID_PP_CTX_NUDGE_PLUS; return S_OK; }
		if (_wcsicmp(rgszNames[0], L"OnCtxNudgeMinus") == 0) { rgDispId[0] = DISPID_PP_CTX_NUDGE_MINUS; return S_OK; }
		if (_wcsicmp(rgszNames[0], L"OnCtxPctPlus") == 0) { rgDispId[0] = DISPID_PP_CTX_PCT_PLUS; return S_OK; }
		if (_wcsicmp(rgszNames[0], L"OnCtxPctMinus") == 0) { rgDispId[0] = DISPID_PP_CTX_PCT_MINUS; return S_OK; }
		if (_wcsicmp(rgszNames[0], L"OnCtxScaleDay") == 0) { rgDispId[0] = DISPID_PP_CTX_SCALE_DAY; return S_OK; }
		if (_wcsicmp(rgszNames[0], L"OnCtxScaleWeek") == 0) { rgDispId[0] = DISPID_PP_CTX_SCALE_WEEK; return S_OK; }
		if (_wcsicmp(rgszNames[0], L"OnCtxScaleMonth") == 0) { rgDispId[0] = DISPID_PP_CTX_SCALE_MONTH; return S_OK; }
		if (_wcsicmp(rgszNames[0], L"OnCtxScaleQuarter") == 0) { rgDispId[0] = DISPID_PP_CTX_SCALE_QUARTER; return S_OK; }
		if (_wcsicmp(rgszNames[0], L"OnCtxScaleYear") == 0) { rgDispId[0] = DISPID_PP_CTX_SCALE_YEAR; return S_OK; }
	}
	return ExtBase::GetIDsOfNames(riid, rgszNames, cNames, lcid, rgDispId);
}

STDMETHODIMP CConnect::Invoke(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags,
	DISPPARAMS* pDispParams, VARIANT* pVarResult, EXCEPINFO* pExcepInfo, UINT* puArgErr)
{
	switch (dispIdMember)
	{
	case DISPID_PP_ONLOAD:
		// onLoad(ribbon as IRibbonUI)
		if (pDispParams && pDispParams->cArgs >= 1 && pDispParams->rgvarg[0].vt == VT_DISPATCH)
			m_pRibbon = pDispParams->rgvarg[0].pdispVal;
		return S_OK;

	case DISPID_PP_INSERT_GANTT:
		DoInsertGantt();
		return S_OK;

	case DISPID_PP_PULL_GANTT:
		DoPullGantt();
		return S_OK;

	case DISPID_PP_REFLOW_GANTT:
		DoReflowGantt();
		return S_OK;

	case DISPID_PP_CTX_ADD_TASK:
		DoCtxAddTask();
		return S_OK;

	case DISPID_PP_CTX_ADD_ROW:
		DoCtxAddRow();
		return S_OK;

	case DISPID_PP_CTX_DELETE:
		DoCtxDelete();
		return S_OK;

	case DISPID_PP_CTX_NUDGE_PLUS:
		DoCtxNudgePlus();
		return S_OK;

	case DISPID_PP_CTX_NUDGE_MINUS:
		DoCtxNudgeMinus();
		return S_OK;

	case DISPID_PP_CTX_PCT_PLUS:
		DoCtxPctPlus();
		return S_OK;

	case DISPID_PP_CTX_PCT_MINUS:
		DoCtxPctMinus();
		return S_OK;

	case DISPID_PP_CTX_SCALE_DAY:
		DoCtxScaleDay();
		return S_OK;

	case DISPID_PP_CTX_SCALE_WEEK:
		DoCtxScaleWeek();
		return S_OK;

	case DISPID_PP_CTX_SCALE_MONTH:
		DoCtxScaleMonth();
		return S_OK;

	case DISPID_PP_CTX_SCALE_QUARTER:
		DoCtxScaleQuarter();
		return S_OK;

	case DISPID_PP_CTX_SCALE_YEAR:
		DoCtxScaleYear();
		return S_OK;
	}
	return ExtBase::Invoke(dispIdMember, riid, lcid, wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);
}

// ---- WindowSelectionChange sink advise / unadvise --------------------------

void CConnect::AdviseSelectionSink()
{
	if (!m_pApp || m_pSelSink) return;
	try {
		CComQIPtr<IConnectionPointContainer> cpc(m_pApp);
		if (!cpc) { PpLog(L"selection sink: no IConnectionPointContainer"); return; }
		CComPtr<IConnectionPoint> cp;
		HRESULT hr = cpc->FindConnectionPoint(__uuidof(PowerPoint::EApplication), &cp);
		if (FAILED(hr) || !cp) {
			wchar_t buf[96];
			::swprintf_s(buf, 96, L"selection sink: FindConnectionPoint failed hr=0x%08lX", (unsigned long)hr);
			PpLog(buf);
			return;
		}
		IDispatch* sink = new PpSelectionSink();   // ref = 1 (ours)
		DWORD cookie = 0;
		hr = cp->Advise(sink, &cookie);            // connection point AddRefs on success
		if (FAILED(hr)) {
			wchar_t buf[96];
			::swprintf_s(buf, 96, L"selection sink: Advise failed hr=0x%08lX", (unsigned long)hr);
			PpLog(buf);
			sink->Release();                       // ref = 0, deleted
			return;
		}
		m_pSelSink = sink;
		m_selCp = cp;
		m_selSinkCookie = cookie;
		PpLog(L"selection sink: advised on EApplication.WindowSelectionChange");
	}
	catch (const _com_error&) { PpLog(L"selection sink: COM error during advise"); }
	catch (...) { PpLog(L"selection sink: exception during advise"); }
}

void CConnect::UnadviseSelectionSink()
{
	try {
		if (m_selCp && m_selSinkCookie) {
			m_selCp->Unadvise(m_selSinkCookie);    // connection point releases its ref
		}
	}
	catch (...) {}
	m_selSinkCookie = 0;
	m_selCp.Release();
	if (m_pSelSink) {
		m_pSelSink->Release();                     // release our ref (ref -> 0, deleted)
		m_pSelSink = nullptr;
	}
}

// ---- Actions ---------------------------------------------------------------

void CConnect::DoInsertGantt()
{
	PpLog(L"DoInsertGantt — ribbon button clicked");
	if (!m_pApp) {
		::MessageBoxW(NULL, L"PowerPoint application is not available.", L"PowerPlanner", MB_OK | MB_ICONWARNING);
		return;
	}

	int shapeCount = 0;
	HRESULT hr = InsertGantt(m_pApp, MakeSampleDocument(), &shapeCount);
	if (SUCCEEDED(hr)) {
		// V3-1 fit-to-slide: size/position the freshly-built CHART_ROOT to
		// fill the slide's content area (reserving the top ~15% for a native
		// title placeholder). Best-effort: a failure here still leaves a
		// valid (just naturally-sized) chart on the slide, so it does not
		// affect the success HRESULT/message below.
		HRESULT fitHr = FitChartRootToSlide(m_pApp);
		if (FAILED(fitHr)) {
			wchar_t fitMsg[96];
			::swprintf_s(fitMsg, 96, L"FitChartRootToSlide failed hr=0x%08lX (chart left at natural size)", (unsigned long)fitHr);
			PpLog(fitMsg);
		}

		wchar_t msg[160];
		::swprintf_s(msg, 160, L"Inserted a Gantt chart: %d native shapes on the current slide.", shapeCount);
		PpLog(msg);
		// No dialog on success — the shapes on the slide are the feedback.
	}
	else {
		wchar_t msg[200];
		::swprintf_s(msg, 200, L"Could not insert the chart (hr=0x%08lX after %d shapes).\n\nOpen a slide in Normal view and try again.", (unsigned long)hr, shapeCount);
		PpLog(msg);
		::MessageBoxW(NULL, msg, L"PowerPlanner", MB_OK | MB_ICONERROR);
	}
}

void CConnect::DoPullGantt()
{
	PpLog(L"DoPullGantt — ribbon button clicked");
	if (!m_pApp) {
		::MessageBoxW(NULL, L"PowerPoint application is not available.", L"PowerPlanner", MB_OK | MB_ICONWARNING);
		return;
	}
	std::string json = ReadGanttFromSlide(m_pApp);
	if (json.empty()) {
		::MessageBoxW(NULL, L"No PowerPlanner chart found on this slide.\n\nInsert one first, then Pull.", L"PowerPlanner", MB_OK | MB_ICONINFORMATION);
		return;
	}
	try {
		PpDocument doc = DocumentFromJson(json);
		wchar_t msg[256];
		::swprintf_s(msg, 256,
			L"Pulled the chart from the slide:\n\n  %zu tasks\n  %zu milestones\n  %zu dependencies\n  %zu rows",
			doc.tasks.size(), doc.milestones.size(), doc.deps.size(), doc.rows.size());
		PpLog(L"DoPullGantt — parsed PP_DOC successfully");
		::MessageBoxW(NULL, msg, L"PowerPlanner", MB_OK | MB_ICONINFORMATION);
	}
	catch (const std::exception&) {
		::MessageBoxW(NULL, L"The chart on this slide has an invalid PP_DOC tag.", L"PowerPlanner", MB_OK | MB_ICONERROR);
	}
}

void CConnect::DoReflowGantt()
{
	PpLog(L"DoReflowGantt — ribbon button clicked");
	if (!m_pApp) {
		::MessageBoxW(NULL, L"PowerPoint application is not available.", L"PowerPlanner", MB_OK | MB_ICONWARNING);
		return;
	}
	bool changed = false;
	HRESULT hr = ReflowFromSlide(m_pApp, &changed);
	if (hr == S_FALSE) {
		::MessageBoxW(NULL, L"No PowerPlanner chart found on this slide.", L"PowerPlanner", MB_OK | MB_ICONINFORMATION);
	} else if (FAILED(hr)) {
		::MessageBoxW(NULL, L"Could not reflow the chart.", L"PowerPlanner", MB_OK | MB_ICONERROR);
	} else if (changed) {
		PpLog(L"DoReflowGantt — reflowed (dates updated from moved bars)");
		::MessageBoxW(NULL, L"Reflowed: moved/resized bars were read back into dates, and the chart was rebuilt.", L"PowerPlanner", MB_OK | MB_ICONINFORMATION);
	} else {
		::MessageBoxW(NULL, L"Nothing to reflow — no bar positions changed.", L"PowerPlanner", MB_OK | MB_ICONINFORMATION);
	}
}

void CConnect::MutateChart(const std::function<bool(PpDocument&, std::string& outSelectId)>& op)
{
	if (!m_pApp) {
		::MessageBoxW(NULL, L"PowerPoint application is not available.", L"PowerPlanner", MB_OK | MB_ICONWARNING);
		return;
	}

	m_ctxKind.clear();
	m_ctxId.clear();
	m_ctxSuppressNoChangeMessage = false;

	try {
		PowerPoint::_ApplicationPtr app(m_pApp.p);
		PowerPoint::DocumentWindowPtr win = app->GetActiveWindow();
		if (win) {
			PowerPoint::SelectionPtr sel = win->GetSelection();
			if (sel && sel->GetType() == PowerPoint::ppSelectionShapes) {
				PowerPoint::ShapeRangePtr sr = sel->GetShapeRange();
				if (sr && sr->GetCount() >= 1) {
					PowerPoint::ShapePtr sh = sr->Item(_variant_t(1L));
					_bstr_t kind = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
					_bstr_t id = sh->GetTags()->Item(_bstr_t(L"PP_ID"));
					m_ctxKind = Narrow((const wchar_t*)kind);
					m_ctxId = Narrow((const wchar_t*)id);
				}
			}
		}

		std::string json = ReadGanttFromSlide(m_pApp);
		if (json.empty()) {
			::MessageBoxW(NULL, L"No PowerPlanner chart on this slide.", L"PowerPlanner", MB_OK | MB_ICONINFORMATION);
			return;
		}

		PpDocument doc = DocumentFromJson(json);
		std::string selectId;
		if (!op(doc, selectId)) {
			if (!m_ctxSuppressNoChangeMessage) {
				if (m_ctxId.empty()) {
					::MessageBoxW(NULL, L"Select a PowerPlanner chart item and try again.", L"PowerPlanner", MB_OK | MB_ICONINFORMATION);
				} else {
					::MessageBoxW(NULL, L"That action is not available for the selected chart item.", L"PowerPlanner", MB_OK | MB_ICONINFORMATION);
				}
			}
			return;
		}

		// Undo entry MUST start before any COM mutation so the whole edit
		// (including UpdateGantt's occasional ungroup/regroup on a structural
		// change) collapses into one undo step (see StartNewUndoEntryIfPossible
		// above; idiom copied from native/render/undo-probe.cpp,
		// VERDICT: GROUPING_WORKS in native/build/undo-probe.txt).
		StartNewUndoEntryIfPossible(m_pApp);

		HRESULT hr = UpdateGantt(m_pApp, doc, selectId);
		if (FAILED(hr)) {
			::MessageBoxW(NULL, L"Could not rebuild the chart after the edit.", L"PowerPlanner", MB_OK | MB_ICONERROR);
		}
	}
	catch (const std::exception&) {
		::MessageBoxW(NULL, L"The chart on this slide has an invalid PP_DOC tag.", L"PowerPlanner", MB_OK | MB_ICONERROR);
	}
	catch (const _com_error&) {
		::MessageBoxW(NULL, L"Could not edit the selected PowerPlanner chart.", L"PowerPlanner", MB_OK | MB_ICONERROR);
	}
}

void CConnect::DoCtxAddTask()
{
	PpLog(L"DoCtxAddTask — context menu clicked");
	MutateChart([this](PpDocument& doc, std::string& outSelectId) {
		std::string rowId = RowForSelection(doc, m_ctxKind, m_ctxId);
		if (rowId.empty()) return false;
		std::string start, end;
		DefaultTaskDates(doc, rowId, (m_ctxKind == "TASK" || m_ctxKind == "TASK_PROGRESS") ? m_ctxId : "", start, end);
		outSelectId = AddTask(doc, rowId, "New Task", start, end);
		return !outSelectId.empty();
	});
}

void CConnect::DoCtxAddRow()
{
	PpLog(L"DoCtxAddRow — context menu clicked");
	MutateChart([this](PpDocument& doc, std::string& outSelectId) {
		std::string afterRowId;
		if (m_ctxKind == "ROW_LABEL") {
			afterRowId = m_ctxId;
		} else if (m_ctxKind == "TASK" || m_ctxKind == "TASK_PROGRESS") {
			if (const PpTask* task = FindTask(doc, m_ctxId)) afterRowId = task->rowId;
		}
		std::string rowId = AddRow(doc, "New Row", afterRowId);
		outSelectId.clear();
		return !rowId.empty();
	});
}

void CConnect::DoCtxDelete()
{
	PpLog(L"DoCtxDelete — context menu clicked");
	MutateChart([this](PpDocument& doc, std::string& outSelectId) {
		if (m_ctxKind == "CHART_ROOT" || m_ctxId.empty()) {
			::MessageBoxW(NULL, L"Select a task or milestone to delete.", L"PowerPlanner", MB_OK | MB_ICONINFORMATION);
			m_ctxSuppressNoChangeMessage = true;
			return false;
		}
		outSelectId.clear();
		return DeleteById(doc, m_ctxId);
	});
}

void CConnect::DoCtxNudgePlus()
{
	PpLog(L"DoCtxNudgePlus — context menu clicked");
	MutateChart([this](PpDocument& doc, std::string& outSelectId) {
		if (m_ctxId.empty()) return false;
		if (!NudgeTask(doc, m_ctxId, 1)) return false;
		outSelectId = m_ctxId;
		return true;
	});
}

void CConnect::DoCtxNudgeMinus()
{
	PpLog(L"DoCtxNudgeMinus — context menu clicked");
	MutateChart([this](PpDocument& doc, std::string& outSelectId) {
		if (m_ctxId.empty()) return false;
		if (!NudgeTask(doc, m_ctxId, -1)) return false;
		outSelectId = m_ctxId;
		return true;
	});
}

void CConnect::DoCtxPctPlus()
{
	PpLog(L"DoCtxPctPlus — context menu clicked");
	MutateChart([this](PpDocument& doc, std::string& outSelectId) {
		const PpTask* task = FindTask(doc, m_ctxId);
		if (!task) return false;
		int newPct = task->percent + 10;
		if (newPct > 100) newPct = 100;
		if (newPct == task->percent) return false;
		if (!SetTaskPercent(doc, m_ctxId, newPct)) return false;
		outSelectId = m_ctxId;
		return true;
	});
}

void CConnect::DoCtxPctMinus()
{
	PpLog(L"DoCtxPctMinus — context menu clicked");
	MutateChart([this](PpDocument& doc, std::string& outSelectId) {
		const PpTask* task = FindTask(doc, m_ctxId);
		if (!task) return false;
		int newPct = task->percent - 10;
		if (newPct < 0) newPct = 0;
		if (newPct == task->percent) return false;
		if (!SetTaskPercent(doc, m_ctxId, newPct)) return false;
		outSelectId = m_ctxId;
		return true;
	});
}

void CConnect::DoCtxScaleDay()
{
	PpLog(L"DoCtxScaleDay — context menu clicked");
	MutateChart([](PpDocument& doc, std::string& outSelectId) {
		outSelectId.clear();
		return SetScale(doc, "day");
	});
}

void CConnect::DoCtxScaleWeek()
{
	PpLog(L"DoCtxScaleWeek — context menu clicked");
	MutateChart([](PpDocument& doc, std::string& outSelectId) {
		outSelectId.clear();
		return SetScale(doc, "week");
	});
}

void CConnect::DoCtxScaleMonth()
{
	PpLog(L"DoCtxScaleMonth — context menu clicked");
	MutateChart([](PpDocument& doc, std::string& outSelectId) {
		outSelectId.clear();
		return SetScale(doc, "month");
	});
}

void CConnect::DoCtxScaleQuarter()
{
	PpLog(L"DoCtxScaleQuarter — context menu clicked");
	MutateChart([](PpDocument& doc, std::string& outSelectId) {
		outSelectId.clear();
		return SetScale(doc, "quarter");
	});
}

void CConnect::DoCtxScaleYear()
{
	PpLog(L"DoCtxScaleYear — context menu clicked");
	MutateChart([](PpDocument& doc, std::string& outSelectId) {
		outSelectId.clear();
		return SetScale(doc, "year");
	});
}
