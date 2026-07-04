#pragma warning(disable: 26495)

#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3)                              \
            ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) |       \
            ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24 ))
#endif

#include <codeanalysis\warnings.h>
#pragma warning(push)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
#include "prototype_rtx.h"
#pragma warning(pop)

#include <MinHook.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdarg>

// -------------------------------------------------------
// БЕЗОПАСНЫЙ ЛОГГЕР (Защищен от вылетов)
// -------------------------------------------------------
static void DebugLog(const char* fmt, ...) {
    static int logCount = 0;
    if (logCount > 1000) return; // Пишем только первые 1000 замен, бережем FPS

    FILE* f = nullptr;
    fopen_s(&f, "prototype_tex.log", "a");
    if (!f) fopen_s(&f, "prototype_rtx.log", "a"); // Фолбэк, если tex.log заблокирован
    if (f) {
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fclose(f);
        logCount++;
    }
}

static void GetFormatStr(DWORD fmt, char* outBuf, size_t bufSize) {
    if (fmt > 255) {
        outBuf[0] = (char)(fmt & 0xFF);
        outBuf[1] = (char)((fmt >> 8) & 0xFF);
        outBuf[2] = (char)((fmt >> 16) & 0xFF);
        outBuf[3] = (char)((fmt >> 24) & 0xFF);
        outBuf[4] = '\0';
        for (int i = 0; i < 4; i++) {
            if (outBuf[i] < 32 || outBuf[i] > 126) outBuf[i] = '?';
        }
    }
    else {
        sprintf_s(outBuf, bufSize, "%lu", fmt);
    }
}

typedef HRESULT(WINAPI* PFN_SetVertexShaderConstantF)(IDirect3DDevice9*, UINT, const float*, UINT);
typedef HRESULT(WINAPI* PFN_SetVertexShader)(IDirect3DDevice9*, IDirect3DVertexShader9*);
typedef HRESULT(WINAPI* PFN_DrawPrimitive)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT(WINAPI* PFN_DrawIndexedPrimitive)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef HRESULT(WINAPI* PFN_SetFVF)(IDirect3DDevice9*, DWORD);
typedef HRESULT(WINAPI* PFN_SetStreamSourceFreq)(IDirect3DDevice9*, UINT, UINT);
typedef HRESULT(WINAPI* PFN_SetTexture)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*);
typedef HRESULT(WINAPI* PFN_SetTextureStageState)(IDirect3DDevice9*, DWORD, D3DTEXTURESTAGESTATETYPE, DWORD);

static PFN_SetVertexShaderConstantF  g_origSetVSConstF = nullptr;
static PFN_SetVertexShader           g_origSetVS = nullptr;
static PFN_DrawPrimitive             g_origDrawPrim = nullptr;
static PFN_DrawIndexedPrimitive      g_origDrawIndexed = nullptr;
static PFN_SetFVF                    g_origSetFVF = nullptr;
static PFN_SetStreamSourceFreq       g_origSetStreamSourceFreq = nullptr;
static PFN_SetTexture                g_origSetTexture = nullptr;
static PFN_SetTextureStageState      g_origSetTextureStageState = nullptr;

bool        g_bRemixReady = false;
bool        g_bHooksInstalled = false;
bool        g_bIsInstanced = false;
static bool g_bIsUI = false;

// Флаги сортировки камер
bool        g_bCameraLocked = false;
bool        g_bCurrentDrawIsPerspective = false;
bool        g_bIsShadowMap = false;

D3DXMATRIX  g_matView;
D3DXMATRIX  g_matProj;
D3DXMATRIX  g_matWorld;
D3DXMATRIX  g_matRawVP;

UINT g_TotalDrawCalls = 0;
UINT g_GlobalDrawCallCounter = 0; // БЕСКОНЕЧНЫЙ счетчик для защиты от мерцаний
UINT g_FrameCount = 0;
bool g_bLogThisFrame = false;
int  g_DumpCount = 0;

static float g_VShaderConstants[256 * 4] = { 0.0f };
static IDirect3DBaseTexture9* g_CurrentTextures[8] = { nullptr };
static UINT g_TextureLastSetDrawCall[8] = { 0 };

static IDirect3DBaseTexture9* g_pSavedStage0Tex = nullptr;
static D3DXMATRIX             g_SavedTexMatrix;
static bool                   g_bStage0Overridden = false;
static thread_local bool      t_bInTextureHook = false;

static DWORD g_MaxVPWidth = 0;

namespace VTIdx {
    enum {
        SetTexture = 65,
        SetTextureStageState = 67,
        DrawPrimitive = 81,
        DrawIndexedPrimitive = 82,
        SetFVF = 89,
        SetVertexShader = 92,
        SetVertexShaderConstantF = 94,
        SetStreamSourceFreq = 102,
    };
}

static void* GetVTableFunc(IDirect3DDevice9* pDev, UINT index) {
    return (*reinterpret_cast<void***>(pDev))[index];
}

bool IsMatrixSafe(const D3DXMATRIX& mat) {
    float sum = 0.0f;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float val = mat.m[i][j];
            if (std::isnan(val) || std::isinf(val)) return false;
            sum += fabs(val);
        }
    }
    if (sum < 0.01f) return false;
    return true;
}

// -------------------------------------------------------
// ИДЕАЛЬНАЯ ФИЛЬТРАЦИЯ (С поддержкой NPC)
// -------------------------------------------------------
static int GetTextureScore(IDirect3DBaseTexture9* pTex, bool isStage0, char* outLogStr = nullptr, size_t logSize = 0) {
    if (!pTex) {
        if (outLogStr) sprintf_s(outLogStr, logSize, "[NULL]");
        return -1;
    }

    __try {
        if (pTex->GetType() != D3DRTYPE_TEXTURE) {
            if (outLogStr) sprintf_s(outLogStr, logSize, "[NOT_A_2D_TEXTURE]");
            return -1;
        }

        D3DSURFACE_DESC desc = {};
        if (FAILED(static_cast<IDirect3DTexture9*>(pTex)->GetLevelDesc(0, &desc))) return -1;

        if (outLogStr) {
            char fmtBuf[16];
            GetFormatStr(desc.Format, fmtBuf, sizeof(fmtBuf));
            sprintf_s(outLogStr, logSize, "%dx%d Fmt:%s", desc.Width, desc.Height, fmtBuf);
        }

        // 1. Отсекаем буферы (Служебный мусор)
        if (desc.Usage & D3DUSAGE_RENDERTARGET) return -1;
        if (desc.Usage & D3DUSAGE_DEPTHSTENCIL) return -1;

        DWORD f = desc.Format;
        if (f == D3DFMT_D24X8 || f == D3DFMT_D24S8 || f == D3DFMT_D16 || f == D3DFMT_D32 ||
            f == D3DFMT_R32F || f == D3DFMT_R16F) return -1;

        // 2. ЖЕСТКО отсекаем Карты Нормалей и Рельефа (ATI2, 3DCP, V8U8 и т.д.)
        if (f == 60 || f == 62 || f == 63 || f == 64 ||
            f == MAKEFOURCC('A', 'T', 'I', '1') || f == MAKEFOURCC('A', 'T', 'I', '2') ||
            f == MAKEFOURCC('3', 'D', 'C', 'P')) return -1;

        // 3. БЕЛЫЙ СПИСОК (Только цвета)
        bool isColor = false;
        if (f == MAKEFOURCC('D', 'X', 'T', '1') || f == MAKEFOURCC('D', 'X', 'T', '2') ||
            f == MAKEFOURCC('D', 'X', 'T', '3') || f == MAKEFOURCC('D', 'X', 'T', '4') ||
            f == MAKEFOURCC('D', 'X', 'T', '5')) {
            isColor = true;
        }
        else if (f == D3DFMT_A8R8G8B8 || f == D3DFMT_X8R8G8B8 ||
            f == D3DFMT_A8B8G8R8 || f == D3DFMT_X8B8G8R8 ||
            f == D3DFMT_R5G6B5 || f == D3DFMT_A1R5G5B5 || f == D3DFMT_A4R4G4B4 ||
            f == 21 || f == 22 ||
            f == D3DFMT_A8 || f == D3DFMT_L8) { // Добавлены A8/L8 для NPC!
            isColor = true;
        }

        if (!isColor) return -1;

        // 4. Защита от мелких пиксельных масок (меньше 32x32)
        if (desc.Width < 32 || desc.Height < 32) return -1;

        return (int)(desc.Width * desc.Height);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// -------------------------------------------------------
// СОРТИРОВКА КАМЕР И ПОДМЕНА ТЕКСТУР
// -------------------------------------------------------
static void ApplyStatesToRemix(IDirect3DDevice9* pDev)
{
    g_bStage0Overridden = false;
    g_pSavedStage0Tex = nullptr;

    D3DVIEWPORT9 vp = {};
    pDev->GetViewport(&vp);
    if (vp.Width > g_MaxVPWidth) g_MaxVPWidth = vp.Width;
    bool isMainCamera = (g_MaxVPWidth > 0 && vp.Width >= g_MaxVPWidth * 8 / 10);

    // СОРТИРОВКА КАМЕР (Защита от крашей и расплющивания партиклов)
    if (g_bIsUI)
    {
        D3DXMATRIX id, ortho;
        D3DXMatrixIdentity(&id);
        D3DXMatrixOrthoLH(&ortho, 2.0f, 2.0f, -100.0f, 100.0f);
        pDev->SetTransform(D3DTS_VIEW, &id);
        pDev->SetTransform(D3DTS_PROJECTION, &ortho);
        pDev->SetTransform(D3DTS_WORLD, &id);
    }
    else if (!isMainCamera || g_bIsShadowMap)
    {
        D3DXMATRIX id;
        D3DXMatrixIdentity(&id);
        pDev->SetTransform(D3DTS_VIEW, &id);
        if (IsMatrixSafe(g_matRawVP)) pDev->SetTransform(D3DTS_PROJECTION, &g_matRawVP);
        pDev->SetTransform(D3DTS_WORLD, &g_matWorld);
    }
    else
    {
        if (g_bRemixReady) {
            pDev->SetTransform(D3DTS_VIEW, &g_matView);
            pDev->SetTransform(D3DTS_PROJECTION, &g_matProj);
            pDev->SetTransform(D3DTS_WORLD, &g_matWorld);
        }
    }

    // --- УМНАЯ ПОДМЕНА ТЕКСТУР ---
    char log0[128] = { 0 };
    int stage0Score = GetTextureScore(g_CurrentTextures[0], true, log0, sizeof(log0));

    // Ищем замену только если на 0-м слоте нет отличной текстуры
    if (stage0Score < 64 * 64)
    {
        int bestStage = -1;
        int maxScore = stage0Score > 0 ? stage0Score : 0;
        char logBest[128] = { 0 };

        for (int i = 1; i <= 3; ++i) {
            if (g_CurrentTextures[i] && g_CurrentTextures[i] != g_CurrentTextures[0]) {

                // ЗАЩИТА ОТ МЕРЦАНИЯ: Отсекаем текстуры, залипшие от прошлых зданий!
                if ((g_GlobalDrawCallCounter - g_TextureLastSetDrawCall[i]) > 3) continue;

                char logI[128] = { 0 };
                int score = GetTextureScore(g_CurrentTextures[i], false, logI, sizeof(logI));

                // АККУРАТНЫЙ МНОЖИТЕЛЬ (x2): Спасает машины от динамической смены текстур,
                // но всё ещё позволяет вытаскивать настоящую текстуру когтей со слота 1.
                if (i == 1) score *= 2;

                if (score > maxScore) {
                    maxScore = score;
                    bestStage = i;
                    strcpy_s(logBest, sizeof(logBest), logI);
                }
            }
        }

        if (bestStage != -1) {
            DebugLog("[OVERRIDE] Call: %d | Stage0: %s | Replaced with: %s\n", g_GlobalDrawCallCounter, log0, logBest);

            g_pSavedStage0Tex = g_CurrentTextures[0];
            pDev->GetTransform(D3DTS_TEXTURE0, &g_SavedTexMatrix);
            g_bStage0Overridden = true;

            t_bInTextureHook = true;
            g_origSetTexture(pDev, 0, g_CurrentTextures[bestStage]);

            // Проброс UV-развертки (Текстуры лягут ровно)
            D3DXMATRIX targetUVMatrix;
            if (SUCCEEDED(pDev->GetTransform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + bestStage), &targetUVMatrix))) {
                pDev->SetTransform(D3DTS_TEXTURE0, &targetUVMatrix);
            }
            else {
                D3DXMatrixIdentity(&targetUVMatrix);
                pDev->SetTransform(D3DTS_TEXTURE0, &targetUVMatrix);
            }
            t_bInTextureHook = false;
        }
    }

    g_origSetTextureStageState(pDev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    g_origSetTextureStageState(pDev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    g_origSetTextureStageState(pDev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    g_origSetTextureStageState(pDev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    g_origSetTextureStageState(pDev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
}

static void RestoreStatesAfterDraw(IDirect3DDevice9* pDev)
{
    if (g_bStage0Overridden) {
        t_bInTextureHook = true;
        g_origSetTexture(pDev, 0, g_pSavedStage0Tex);
        pDev->SetTransform(D3DTS_TEXTURE0, &g_SavedTexMatrix);
        t_bInTextureHook = false;
        g_bStage0Overridden = false;
    }
    g_bIsUI = false;
}

// -------------------------------------------------------
// ХУКИ
// -------------------------------------------------------
HRESULT WINAPI Hooked_SetTexture(IDirect3DDevice9* pDev, DWORD Stage, IDirect3DBaseTexture9* pTexture)
{
    if (!t_bInTextureHook && Stage < 8) {
        g_CurrentTextures[Stage] = pTexture;
        g_TextureLastSetDrawCall[Stage] = g_GlobalDrawCallCounter; // Засекаем свежесть
    }
    return g_origSetTexture(pDev, Stage, pTexture);
}

HRESULT WINAPI Hooked_SetTextureStageState(IDirect3DDevice9* pDev, DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value)
{
    return g_origSetTextureStageState(pDev, Stage, Type, Value);
}

HRESULT WINAPI Hooked_SetVertexShaderConstantF(
    IDirect3DDevice9* pDev, UINT StartRegister,
    const float* pConstantData, UINT Vector4fCount)
{
    if (!pConstantData) return g_origSetVSConstF(pDev, StartRegister, pConstantData, Vector4fCount);

    if ((StartRegister + Vector4fCount) <= 256)
        memcpy(g_VShaderConstants + StartRegister * 4, pConstantData, Vector4fCount * 16);

    D3DXMATRIX matVP = {}, matW = {};
    bool bHit = false;

    if (StartRegister == 7 && Vector4fCount >= 4) {
        memcpy(&matVP, pConstantData, sizeof(D3DXMATRIX));
        D3DXMatrixIdentity(&matW);
        bool isPure = (fabs(pConstantData[3]) < 0.01f && fabs(pConstantData[7]) < 0.01f &&
            fabs(pConstantData[11]) < 0.01f && fabs(pConstantData[15] - 1.f) < 0.01f);
        if (isPure) bHit = true;
    }

    if (!bHit && StartRegister == 0 && Vector4fCount >= 4) {
        memcpy(&matVP, pConstantData, sizeof(D3DXMATRIX));
        memcpy(&matW, g_VShaderConstants + 16, sizeof(D3DXMATRIX));
        bHit = true;
    }

    if (!bHit && StartRegister == 4 && Vector4fCount >= 4) {
        memcpy(&matW, pConstantData, sizeof(D3DXMATRIX));
        memcpy(&matVP, g_VShaderConstants, sizeof(D3DXMATRIX));
        bHit = true;
    }

    if (bHit)
    {
        g_matRawVP = matVP;
        g_bCurrentDrawIsPerspective = (fabs(matVP._14) > 0.001f || fabs(matVP._24) > 0.001f || fabs(matVP._34) > 0.001f);

        const float eps = 0.01f;
        bool isIdentW =
            fabs(matW._11 - 1.f) < eps && fabs(matW._12) < eps && fabs(matW._13) < eps && fabs(matW._14) < eps &&
            fabs(matW._21) < eps && fabs(matW._22 - 1.f) < eps && fabs(matW._23) < eps && fabs(matW._24) < eps &&
            fabs(matW._31) < eps && fabs(matW._32) < eps && fabs(matW._33 - 1.f) < eps && fabs(matW._34) < eps &&
            fabs(matW._41) < eps && fabs(matW._42) < eps && fabs(matW._43) < eps && fabs(matW._44 - 1.f) < eps;

        if (isIdentW && g_bCurrentDrawIsPerspective && !g_bCameraLocked)
        {
            float sx = sqrtf(matVP._11 * matVP._11 + matVP._21 * matVP._21 + matVP._31 * matVP._31);
            float sy = sqrtf(matVP._12 * matVP._12 + matVP._22 * matVP._22 + matVP._32 * matVP._32);

            g_bIsShadowMap = (sx > 0.01f && sy > 0.01f && fabs(sy / sx - 1.f) < 0.05f);

            if (!g_bIsShadowMap && sx > 0.01f && sy > 0.01f)
            {
                float A = 1.f;
                if (fabs(matVP._14) > 0.01f) A = matVP._13 / matVP._14;
                else if (fabs(matVP._24) > 0.01f) A = matVP._23 / matVP._24;
                else if (fabs(matVP._34) > 0.01f) A = matVP._33 / matVP._34;
                float B = matVP._43 - matVP._44 * A;

                g_matView._11 = matVP._11 / sx; g_matView._12 = matVP._12 / sy; g_matView._13 = matVP._14; g_matView._14 = 0.f;
                g_matView._21 = matVP._21 / sx; g_matView._22 = matVP._22 / sy; g_matView._23 = matVP._24; g_matView._24 = 0.f;
                g_matView._31 = matVP._31 / sx; g_matView._32 = matVP._32 / sy; g_matView._33 = matVP._34; g_matView._34 = 0.f;
                g_matView._41 = matVP._41 / sx; g_matView._42 = matVP._42 / sy; g_matView._43 = matVP._44; g_matView._44 = 1.f;

                D3DXMatrixIdentity(&g_matProj);
                g_matProj._11 = sx; g_matProj._22 = sy;
                g_matProj._33 = A;  g_matProj._34 = 1.f;
                g_matProj._43 = B;  g_matProj._44 = 0.f;

                D3DXMatrixIdentity(&g_matWorld);
                g_bRemixReady = true;
                g_bCameraLocked = true;
            }
        }
        else if (!isIdentW)
        {
            g_matWorld = matW;
        }
    }

    return g_origSetVSConstF(pDev, StartRegister, pConstantData, Vector4fCount);
}

HRESULT WINAPI Hooked_SetFVF(IDirect3DDevice9* pDev, DWORD FVF)
{
    g_bIsUI = (FVF & D3DFVF_XYZRHW) != 0;
    return g_origSetFVF(pDev, FVF);
}

HRESULT WINAPI Hooked_SetStreamSourceFreq(IDirect3DDevice9* pDev, UINT StreamNumber, UINT Setting)
{
    g_bIsInstanced = (Setting & D3DSTREAMSOURCE_INSTANCEDATA) != 0;
    return g_origSetStreamSourceFreq(pDev, StreamNumber, Setting);
}

HRESULT WINAPI Hooked_SetVertexShader(IDirect3DDevice9* pDev, IDirect3DVertexShader9* pShader)
{
    if (!pShader) g_bIsUI = true;
    return g_origSetVS(pDev, pShader);
}

HRESULT WINAPI Hooked_DrawIndexedPrimitive(
    IDirect3DDevice9* pDev, D3DPRIMITIVETYPE Type,
    INT BaseVertexIndex, UINT MinVertexIndex,
    UINT NumVertices, UINT StartIndex, UINT PrimitiveCount)
{
    // АНТИ-ВСПЫШКА: Если нет текстуры, и это не интерфейс - просто отменяем отрисовку!
    if (g_CurrentTextures[0] == nullptr && !g_bIsUI && !g_bIsShadowMap && NumVertices < 10) {
        return D3D_OK;
    }

    g_TotalDrawCalls++;
    g_GlobalDrawCallCounter++; // Бесконечный таймер свежести
    ApplyStatesToRemix(pDev);
    HRESULT hr = g_origDrawIndexed(pDev, Type, BaseVertexIndex, MinVertexIndex, NumVertices, StartIndex, PrimitiveCount);
    RestoreStatesAfterDraw(pDev);
    return hr;
}

HRESULT WINAPI Hooked_DrawPrimitive(
    IDirect3DDevice9* pDev, D3DPRIMITIVETYPE Type,
    UINT StartVertex, UINT PrimitiveCount)
{
    if (g_CurrentTextures[0] == nullptr && !g_bIsUI && !g_bIsShadowMap && PrimitiveCount < 10) {
        return D3D_OK;
    }

    g_TotalDrawCalls++;
    g_GlobalDrawCallCounter++;
    ApplyStatesToRemix(pDev);
    HRESULT hr = g_origDrawPrim(pDev, Type, StartVertex, PrimitiveCount);
    RestoreStatesAfterDraw(pDev);
    return hr;
}

void OnFrameBegin(IDirect3DDevice9* pDevice)
{
    g_FrameCount++;
    g_TotalDrawCalls = 0;
    g_bIsUI = false;
    g_bIsInstanced = false;
    g_bRemixReady = false;
    g_bCameraLocked = false;
}

bool InstallHooks(IDirect3DDevice9* pDevice)
{
    if (g_bHooksInstalled) return true;

    MH_Initialize();

    D3DXMatrixIdentity(&g_matProj);
    D3DXMatrixIdentity(&g_matWorld);
    D3DXMatrixIdentity(&g_matView);
    memset(g_VShaderConstants, 0, sizeof(g_VShaderConstants));
    for (int i = 0; i < 8; ++i) g_CurrentTextures[i] = nullptr;

    auto Hook = [](void* tgt, void* det, void** orig) {
        if (MH_CreateHook(tgt, det, orig) == MH_OK) MH_EnableHook(tgt);
        };

    Hook(GetVTableFunc(pDevice, VTIdx::SetVertexShaderConstantF), &Hooked_SetVertexShaderConstantF, (void**)&g_origSetVSConstF);
    Hook(GetVTableFunc(pDevice, VTIdx::SetVertexShader), &Hooked_SetVertexShader, (void**)&g_origSetVS);
    Hook(GetVTableFunc(pDevice, VTIdx::SetFVF), &Hooked_SetFVF, (void**)&g_origSetFVF);
    Hook(GetVTableFunc(pDevice, VTIdx::SetStreamSourceFreq), &Hooked_SetStreamSourceFreq, (void**)&g_origSetStreamSourceFreq);
    Hook(GetVTableFunc(pDevice, VTIdx::SetTexture), &Hooked_SetTexture, (void**)&g_origSetTexture);
    Hook(GetVTableFunc(pDevice, VTIdx::SetTextureStageState), &Hooked_SetTextureStageState, (void**)&g_origSetTextureStageState);
    Hook(GetVTableFunc(pDevice, VTIdx::DrawIndexedPrimitive), &Hooked_DrawIndexedPrimitive, (void**)&g_origDrawIndexed);
    Hook(GetVTableFunc(pDevice, VTIdx::DrawPrimitive), &Hooked_DrawPrimitive, (void**)&g_origDrawPrim);

    g_bHooksInstalled = true;
    return true;
}

void UninstallHooks()
{
    if (!g_bHooksInstalled) return;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_RemoveHook(MH_ALL_HOOKS);
    g_bHooksInstalled = false;
}