// DLL entry points + ATL module. Registration is per-user (no admin needed):
// the COM class lands under HKCU\Software\Classes and the PowerPoint load entry
// under HKCU\...\PowerPoint\AddIns.
#include "pch.h"
#include "Connect.h"

class CPowerPlannerModule : public ATL::CAtlDllModuleT<CPowerPlannerModule>
{
public:
};

CPowerPlannerModule _AtlModule;

extern "C" BOOL WINAPI DllMain(HINSTANCE /*hInstance*/, DWORD dwReason, LPVOID lpReserved)
{
	return _AtlModule.DllMain(dwReason, lpReserved);
}

STDAPI DllCanUnloadNow(void)
{
	return _AtlModule.DllCanUnloadNow();
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
	return _AtlModule.DllGetClassObject(rclsid, riid, ppv);
}

STDAPI DllRegisterServer(void)
{
	AtlSetPerUserRegistration(true);
	return _AtlModule.DllRegisterServer(FALSE);
}

STDAPI DllUnregisterServer(void)
{
	AtlSetPerUserRegistration(true);
	return _AtlModule.DllUnregisterServer(FALSE);
}
