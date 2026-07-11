// CConnect: the COM add-in object.
//   IDTExtensibility2   - Office add-in lifecycle
//   IRibbonExtensibility - Fluent ribbon UI
// Ribbon callbacks are routed through a hand-implemented IDispatch because we
// have no wizard-generated type library describing the callback methods.
#pragma once

#include "pch.h"
#include "resource.h"
#include <functional>
#include <string>

using namespace ATL;

struct PpDocument;

// DISPIDs for our ribbon callbacks (above any inherited dispinterface ids).
#define DISPID_PP_ONLOAD        0x1001
#define DISPID_PP_INSERT_GANTT  0x1002
#define DISPID_PP_PULL_GANTT    0x1003
#define DISPID_PP_REFLOW_GANTT  0x1004
#define DISPID_PP_CTX_ADD_TASK      0x1005
#define DISPID_PP_CTX_ADD_ROW       0x1006
#define DISPID_PP_CTX_DELETE        0x1007
#define DISPID_PP_CTX_NUDGE_PLUS    0x1008
#define DISPID_PP_CTX_NUDGE_MINUS   0x1009
#define DISPID_PP_CTX_PCT_PLUS      0x100A
#define DISPID_PP_CTX_PCT_MINUS     0x100B
#define DISPID_PP_CTX_SCALE_DAY     0x100C
#define DISPID_PP_CTX_SCALE_WEEK    0x100D
#define DISPID_PP_CTX_SCALE_MONTH   0x100E
#define DISPID_PP_CTX_SCALE_QUARTER 0x100F
#define DISPID_PP_CTX_SCALE_YEAR    0x1010

// LIBIDs (we omit named_guids in the #import — see pch.h). Defined in Connect.cpp.
extern const GUID LIBID_AddInDesigner_PP;
extern const GUID LIBID_Office_PP;

typedef IDispatchImpl<AddInDesignerObjects::_IDTExtensibility2,
	&__uuidof(AddInDesignerObjects::_IDTExtensibility2),
	&LIBID_AddInDesigner_PP, 1, 0> ExtBase;

typedef IDispatchImpl<Office::IRibbonExtensibility,
	&__uuidof(Office::IRibbonExtensibility),
	&LIBID_Office_PP, 2, 0> RibbonBase;

class ATL_NO_VTABLE __declspec(uuid("E329C60E-49D1-4C42-AC07-75EC1917F06F")) CConnect :
	public CComObjectRootEx<CComSingleThreadModel>,
	public CComCoClass<CConnect, &__uuidof(CConnect)>,
	public ExtBase,
	public RibbonBase
{
public:
	CConnect() {}

	DECLARE_REGISTRY_RESOURCEID(IDR_CONNECT)
	DECLARE_NOT_AGGREGATABLE(CConnect)

	BEGIN_COM_MAP(CConnect)
		COM_INTERFACE_ENTRY2(IDispatch, AddInDesignerObjects::_IDTExtensibility2)
		COM_INTERFACE_ENTRY(AddInDesignerObjects::_IDTExtensibility2)
		COM_INTERFACE_ENTRY(Office::IRibbonExtensibility)
	END_COM_MAP()

	DECLARE_PROTECT_FINAL_CONSTRUCT()
	HRESULT FinalConstruct() { return S_OK; }
	void FinalRelease() {}

	// IDTExtensibility2
	STDMETHOD(OnConnection)(IDispatch* Application, AddInDesignerObjects::ext_ConnectMode ConnectMode, IDispatch* AddInInst, SAFEARRAY** custom);
	STDMETHOD(OnDisconnection)(AddInDesignerObjects::ext_DisconnectMode RemoveMode, SAFEARRAY** custom);
	STDMETHOD(OnAddInsUpdate)(SAFEARRAY** custom);
	STDMETHOD(OnStartupComplete)(SAFEARRAY** custom);
	STDMETHOD(OnBeginShutdown)(SAFEARRAY** custom);

	// IRibbonExtensibility
	STDMETHOD(GetCustomUI)(BSTR RibbonID, BSTR* RibbonXml);

	// IDispatch override to resolve + dispatch ribbon callbacks.
	STDMETHOD(GetIDsOfNames)(REFIID riid, LPOLESTR* rgszNames, UINT cNames, LCID lcid, DISPID* rgDispId);
	STDMETHOD(Invoke)(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS* pDispParams, VARIANT* pVarResult, EXCEPINFO* pExcepInfo, UINT* puArgErr);

private:
	void DoInsertGantt();
	void DoPullGantt();
	void DoReflowGantt();
	void DoCtxAddTask();
	void DoCtxAddRow();
	void DoCtxDelete();
	void DoCtxNudgePlus();
	void DoCtxNudgeMinus();
	void DoCtxPctPlus();
	void DoCtxPctMinus();
	void DoCtxScaleDay();
	void DoCtxScaleWeek();
	void DoCtxScaleMonth();
	void DoCtxScaleQuarter();
	void DoCtxScaleYear();
	void MutateChart(const std::function<bool(PpDocument&, std::string& outSelectId)>& op);

	// SR-SMO-05 / ARC-07: PowerPoint Application (EApplication) event sink for
	// WindowSelectionChange, so a native selection change is handled INSTANTLY
	// (no 150ms poll latency). Best-effort: if Advise fails, the overlay's Tick
	// poll remains the suppression watchdog. Shared decision lives in
	// Overlay_OnNativeSelectionChanged so the poll-only harness behaves the same.
	void AdviseSelectionSink();
	void UnadviseSelectionSink();

	CComPtr<IDispatch> m_pApp;     // PowerPoint.Application
	CComPtr<IDispatch> m_pRibbon;  // Office.IRibbonUI (from onLoad)
	IDispatch* m_pSelSink = nullptr;          // EApplication event sink (our owning ref)
	CComPtr<IConnectionPoint> m_selCp;        // connection point we advised on
	DWORD m_selSinkCookie = 0;                // Advise cookie for Unadvise
	std::string m_ctxKind;
	std::string m_ctxId;
	bool m_ctxSuppressNoChangeMessage = false;
};

OBJECT_ENTRY_AUTO(__uuidof(CConnect), CConnect)
