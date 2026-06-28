#include "pch.h"
#include "Connect.h"
#include "GanttBuilder.h"

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
	L"        </group>"
	L"      </tab>"
	L"    </tabs>"
	L"  </ribbon>"
	L"</customUI>";

// ---- IDTExtensibility2 -----------------------------------------------------

STDMETHODIMP CConnect::OnConnection(IDispatch* Application,
	AddInDesignerObjects::ext_ConnectMode /*ConnectMode*/,
	IDispatch* /*AddInInst*/, SAFEARRAY** /*custom*/)
{
	m_pApp = Application;  // PowerPoint.Application — used from N2 onward
	PpLog(L"OnConnection — add-in loaded into PowerPoint");
	return S_OK;
}

STDMETHODIMP CConnect::OnDisconnection(AddInDesignerObjects::ext_DisconnectMode /*RemoveMode*/, SAFEARRAY** /*custom*/)
{
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
	}
	return ExtBase::Invoke(dispIdMember, riid, lcid, wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);
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
