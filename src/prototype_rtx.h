#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>

// Глобальные переменные состояния, общие для прокси и хуков
extern IDirect3DDevice9* g_pDevice;
extern bool g_bRemixReady;
extern bool g_bHooksInstalled;
extern D3DXMATRIX g_matView;
extern D3DXMATRIX g_matProj;
extern D3DXMATRIX g_matWorld;

// Интерфейс управления хуками
bool InstallHooks(IDirect3DDevice9* pDevice);
void UninstallHooks();
void OnFrameBegin(IDirect3DDevice9* pDevice);

// Функция безопасного логирования
void LogMsg(const char* fmt, ...);