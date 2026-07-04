#include "prototype_rtx.h"

static HMODULE g_hRemixD3D9 = nullptr;
typedef IDirect3D9* (WINAPI* PFN_Direct3DCreate9)(UINT);
static PFN_Direct3DCreate9 g_origDirect3DCreate9 = nullptr;

class ProxyD3D9 : public IDirect3D9
{
    IDirect3D9* m_real;
public:
    explicit ProxyD3D9(IDirect3D9* real) : m_real(real) {}

    HRESULT WINAPI QueryInterface(REFIID riid, void** ppv) override { return m_real->QueryInterface(riid, ppv); }
    ULONG WINAPI AddRef()  override { return m_real->AddRef(); }
    ULONG WINAPI Release() override {
        ULONG r = m_real->Release();
        if (r == 0) delete this;
        return r;
    }

    HRESULT WINAPI RegisterSoftwareDevice(void* p) override { return m_real->RegisterSoftwareDevice(p); }
    UINT WINAPI GetAdapterCount() override { return m_real->GetAdapterCount(); }
    HRESULT WINAPI GetAdapterIdentifier(UINT a, DWORD b, D3DADAPTER_IDENTIFIER9* c) override { return m_real->GetAdapterIdentifier(a, b, c); }
    UINT WINAPI GetAdapterModeCount(UINT a, D3DFORMAT b) override { return m_real->GetAdapterModeCount(a, b); }
    HRESULT WINAPI EnumAdapterModes(UINT a, D3DFORMAT b, UINT c, D3DDISPLAYMODE* d) override { return m_real->EnumAdapterModes(a, b, c, d); }
    HRESULT WINAPI GetAdapterDisplayMode(UINT a, D3DDISPLAYMODE* b) override { return m_real->GetAdapterDisplayMode(a, b); }
    HRESULT WINAPI CheckDeviceType(UINT a, D3DDEVTYPE b, D3DFORMAT c, D3DFORMAT d, BOOL e) override { return m_real->CheckDeviceType(a, b, c, d, e); }
    HRESULT WINAPI CheckDeviceFormat(UINT a, D3DDEVTYPE b, D3DFORMAT c, DWORD d, D3DRESOURCETYPE e, D3DFORMAT f) override { return m_real->CheckDeviceFormat(a, b, c, d, e, f); }
    HRESULT WINAPI CheckDeviceMultiSampleType(UINT a, D3DDEVTYPE b, D3DFORMAT c, BOOL d, D3DMULTISAMPLE_TYPE e, DWORD* f) override { return m_real->CheckDeviceMultiSampleType(a, b, c, d, e, f); }
    HRESULT WINAPI CheckDepthStencilMatch(UINT a, D3DDEVTYPE b, D3DFORMAT c, D3DFORMAT d, D3DFORMAT e) override { return m_real->CheckDepthStencilMatch(a, b, c, d, e); }
    HRESULT WINAPI CheckDeviceFormatConversion(UINT a, D3DDEVTYPE b, D3DFORMAT c, D3DFORMAT d) override { return m_real->CheckDeviceFormatConversion(a, b, c, d); }
    HRESULT WINAPI GetDeviceCaps(UINT a, D3DDEVTYPE b, D3DCAPS9* c) override { return m_real->GetDeviceCaps(a, b, c); }
    HMONITOR WINAPI GetAdapterMonitor(UINT a) override { return m_real->GetAdapterMonitor(a); }

    HRESULT WINAPI CreateDevice(
        UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
        D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface) override
    {
        HRESULT hr = m_real->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);
        if (SUCCEEDED(hr) && ppReturnedDeviceInterface && *ppReturnedDeviceInterface) {
            g_pDevice = *ppReturnedDeviceInterface;
            LogMsg("[PROTO-RTX] CreateDevice captured device: %p\n", g_pDevice);
        }
        return hr;
    }
};

IDirect3D9* WINAPI Direct3DCreate9(UINT SDKVersion)
{
    if (!g_hRemixD3D9) {
        g_hRemixD3D9 = LoadLibraryA("d3d9_remix.dll");
        if (!g_hRemixD3D9) {
            LogMsg("[PROTO-RTX] FATAL: d3d9_remix.dll not found!\n");
            MessageBoxA(nullptr, "prototype_rtx: d3d9_remix.dll not found!\nRename RTX Remix's d3d9.dll to d3d9_remix.dll.", "PROTOTYPE RTX", MB_ICONERROR);
            return nullptr;
        }
        g_origDirect3DCreate9 = reinterpret_cast<PFN_Direct3DCreate9>(GetProcAddress(g_hRemixD3D9, "Direct3DCreate9"));
    }

    IDirect3D9* pReal = g_origDirect3DCreate9(SDKVersion);
    if (!pReal) return nullptr;

    LogMsg("[PROTO-RTX] Direct3DCreate9 intercepted, wrapping...\n");
    return new ProxyD3D9(pReal);
}