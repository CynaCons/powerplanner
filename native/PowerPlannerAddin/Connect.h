// CConnect: the COM add-in object.
//   IDTExtensibility2   - Office add-in lifecycle
//   IRibbonExtensibility - Fluent ribbon UI
// Ribbon callbacks are routed through a hand-implemented IDispatch because we
// have no wizard-generated type library describing the callback methods.
#pragma once

#include "pch.h"
#include "resource.h"

using namespace ATL;

// DISPIDs for our ribbon callbacks (above any inherited dispinterface ids).
#define DISPID_PP_ONLOAD        0x1001
#define DISPID_PP_INSERT_GANTT  0x1002

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

	CComPtr<IDispatch> m_pApp;     // PowerPoint.Application
	CComPtr<IDispatch> m_pRibbon;  // Office.IRibbonUI (from onLoad)
};

OBJECT_ENTRY_AUTO(__uuidof(CConnect), CConnect)
