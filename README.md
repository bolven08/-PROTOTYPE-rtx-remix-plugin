# Prototype (2009) — RTX Remix Compatibility Plugin

> **Note:** All C++ code, reverse-engineering logic of the graphics pipeline, and technical engine analysis were completely developed using AI (Gemini). Project integration, building, environment setup, and in-game testing were performed by the repository author.

A custom D3D9 compatibility plugin for **Prototype (2009)** that enables full RTX Remix ray tracing support. The game uses the proprietary **Titanium Engine** with non-standard rendering techniques that break Remix's geometry capture and lighting — this plugin intercepts and fixes all of them at runtime.

---

## How It Works

RTX Remix was designed for fixed-function pipeline (FFP) games. Prototype uses fully shader-based rendering, which means Remix can't automatically detect the camera, world geometry, or textures. The plugin bridges this gap by hooking D3D9 function calls and translating them into a form Remix understands.

### Camera Matrix Capture

Prototype never calls `SetTransform(D3DTS_VIEW)` — it passes camera data through vertex shader constants via `SetVertexShaderConstantF`. The plugin intercepts these calls and:

1. Identifies the ViewProjection (VP) matrix in registers `c0–c3` (when `c4–c7` is Identity)
2. Mathematically decomposes `VP` into a separate `View` matrix and `Projection` matrix
3. Passes them to Remix via `SetTransform(D3DTS_VIEW)` and `SetTransform(D3DTS_PROJECTION)`
4. Locks the main camera for the current frame (`g_bCameraLocked`) to prevent shadow map cameras or secondary viewports from overwriting it

Shadow maps are detected by their square aspect ratio (`sx ≈ sy`) and filtered out. Secondary cameras are detected by viewport size — only the viewport within 80% of the maximum observed width is treated as the main camera.

### Texture Recovery

The Titanium Engine frequently binds a tiny placeholder texture (or nothing at all) to stage 0, while the real diffuse texture sits on stage 1, 2, or 3. Since Remix reads albedo from stage 0, this causes most geometry to render white.

The plugin solves this by:

1. Tracking all textures bound to stages 0–3 via a `SetTexture` hook
2. Scoring each texture using format and size — Normal maps (ATI1/ATI2/3DCP), render targets, depth buffers, and textures smaller than 32×32 are rejected
3. If stage 0 scores below `64×64`, finding the highest-scoring texture from stages 1–3 and temporarily binding it to stage 0 before the draw call
4. Restoring the original stage 0 texture after the draw call completes
5. Using a global draw call counter (`g_GlobalDrawCallCounter`) to reject "stale" textures from previous objects — preventing cross-object texture bleeding that caused flickering

All texture operations use `__try/__except` to safely handle cases where streaming has already unloaded a texture pointer.

### Texture Stage State

Because Prototype uses HLSL shaders, it never sets fixed-function `TextureStageState` values that Remix relies on to read stage 0. The plugin injects the correct states before every draw call:

```
D3DTSS_COLOROP   = MODULATE
D3DTSS_COLORARG1 = TEXTURE
D3DTSS_COLORARG2 = DIFFUSE
D3DTSS_ALPHAOP   = SELECTARG1
D3DTSS_ALPHAARG1 = TEXTURE
```

### UI and 2D Separation

The plugin detects 2D draw calls via the `D3DFVF_XYZRHW` flag in `SetFVF` or a null vertex shader in `SetVertexShader`, and passes orthographic Identity matrices to those draw calls so the HUD and UI elements are not ray-traced.

### Instancing

GPU instancing (used for crowds and repeated geometry) is detected via `SetStreamSourceFreq` and skipped entirely — Remix does not support instanced draws.

### Crash Protection

Matrices are validated for NaN/Inf before being passed to Remix. Drawcalls with no bound texture that aren't part of UI or shadow rendering are skipped to prevent white flash artifacts.

---

## Installation

1. Download archive `[PROTOTYPE] RTX Remix Compatibility` from [Releases](../../releases)
2. In the archive from the `files` folder, drag the files to the game folder next to `prototypef.exe`

---

## Building from Source

**Requirements:**
- Visual Studio 2022
- DirectX SDK June 2010
- [MinHook](https://github.com/TsudaKageyu/minhook)

```bat
git clone https://github.com/TsudaKageyu/minhook deps/minhook
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A Win32
cmake --build . --config Release
```

Output: `build/Release/dinput8.dll` — rename to `prototype_rtx.asi` before use.

---

## Known Limitations

- **World matrix is Identity for all objects** — per-object transforms are not yet extracted. Remix operates in "fused world-view" mode to compensate
- **GPU instancing is skipped** — instanced crowds and repeated geometry are not ray-traced
- **Animated characters** — skeletal animation uses bone matrices in non-standard register layouts; full per-bone world matrix extraction is not implemented yet

---

## Contributing

The plugin is written in C++ using MinHook for D3D9 vtable hooking. Pull requests are welcome, especially for:

- Correct per-object World matrix extraction from WVP shader constants
- Improved shadow map detection
- Skeletal animation support
