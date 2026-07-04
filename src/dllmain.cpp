#define _CRT_SECURE_NO_WARNINGS
#include "prototype_rtx.h"
#include <MinHook.h>
#include <cstdio>
#include <cstdarg>

IDirect3DDevice9* g_pDevice = nullptr;

void LogMsg(const char* fmt, ...)
{
    static FILE* f = nullptr;
    if (!f) fopen_s(&f, "prototype_rtx.log", "w");
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    fflush(f);
    va_end(args);
}

typedef HRESULT(WINAPI* PFN_Present)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
static PFN_Present g_origPresent = nullptr;

HRESULT WINAPI EarlyPresent(
    IDirect3DDevice9* pDev,
    const RECT* a, const RECT* b, HWND c, const RGNDATA* d)
{
    if (!g_pDevice) {
        g_pDevice = pDev;
        LogMsg("[PROTO-RTX] Device captured: %p\n", pDev);
        InstallHooks(pDev);
    }

    // ВАЖНО: сбрасываем состояние КАЖДЫЙ кадр
    OnFrameBegin(pDev);

    if (g_origPresent)
        return g_origPresent(pDev, a, b, c, d);
    return S_OK;
}

static DWORD WINAPI InitThread(LPVOID)
{
    LogMsg("[PROTO-RTX] Init thread started.\n");

    HMODULE hD3D9 = nullptr;
    for (int i = 0; i < 300 && !hD3D9; i++) {
        hD3D9 = GetModuleHandleA("d3d9.dll");
        if (!hD3D9) Sleep(100);
    }
    if (!hD3D9) { LogMsg("[PROTO-RTX] d3d9.dll not found!\n"); return 1; }

    typedef IDirect3D9* (WINAPI* PFN_Create9)(UINT);
    auto fnCreate = (PFN_Create9)GetProcAddress(hD3D9, "Direct3DCreate9");
    if (!fnCreate) { LogMsg("[PROTO-RTX] Direct3DCreate9 not found!\n"); return 1; }

    IDirect3D9* pD3D = fnCreate(D3D_SDK_VERSION);
    if (!pD3D) { LogMsg("[PROTO-RTX] IDirect3D9 creation failed!\n"); return 1; }

    HWND hWnd = CreateWindowA("STATIC", "", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hWnd;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;

    IDirect3DDevice9* pTmpDev = nullptr;
    HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF,
        hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &pTmpDev);

    if (FAILED(hr) || !pTmpDev) {
        LogMsg("[PROTO-RTX] Temp device failed: 0x%X\n", hr);
        pD3D->Release(); DestroyWindow(hWnd); return 1;
    }

    void* presentAddr = (*reinterpret_cast<void***>(pTmpDev))[17];
    pTmpDev->Release(); pD3D->Release(); DestroyWindow(hWnd);

    MH_Initialize();
    if (MH_CreateHook(presentAddr, &EarlyPresent,
        reinterpret_cast<void**>(&g_origPresent)) == MH_OK) {
        MH_EnableHook(presentAddr);
        LogMsg("[PROTO-RTX] Present hook installed.\n");
    }
    else {
        LogMsg("[PROTO-RTX] Present hook failed!\n");
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);
        LogMsg("[PROTO-RTX] DLL attached.\n");
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        UninstallHooks();
        LogMsg("[PROTO-RTX] DLL detached.\n");
    }
    return TRUE;
}