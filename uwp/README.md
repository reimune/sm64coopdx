# sm64coopdx on Xbox Series X (UWP, Dev Mode)

**Status: untested.** This patch set was written without a Windows or UWP toolchain, so it has never been compiled. Expect compile errors and runtime fixes on the first Visual Studio build. It is for personal use on your own Dev Mode console with your own legally dumped ROM. Do not distribute a built package or the ROM.

## What is in the patch

| Area | Change |
| --- | --- |
| `src/pc/gfx/gfx_dxgi_uwp.cpp` (new) | Window manager for `TARGET_UWP`. SDL owns the window and events; the swap chain is created with `CreateSwapChainForCoreWindow`. Handles resize and `Trim()` on suspend. |
| `src/pc/gfx/gfx_dxgi.cpp` | Disabled when `TARGET_UWP` is defined (replaced by the file above). |
| `src/pc/gfx/gfx_direct3d11.cpp` | Links `D3D11CreateDevice` directly, loads `d3dcompiler_47.dll` with `LoadPackagedLibrary`, no message boxes, CoreWindow-aware `ResizeBuffers`. |
| `src/pc/platform.c` | UWP versions of `sys_user_path` (`SDL_GetPrefPath`), exe/resource path (`SDL_GetBasePath`), short-path helper and fatal handler. |
| `src/pc/pc_main.c` | Forces the D3D11 backend and drops OpenGL under `TARGET_UWP`. |
| `src/pc/fs/fs.c`, `utils/misc.c`, `mods/*`, `rom_checker.cpp`, `controller/*`, `mumble/mumble.c` | Guards around calls UWP does not allow (`GetFileAttributesA`, `ShellExecute`, `SetFileAttributes`, cursor APIs, shared memory). |
| `uwp/` (new) | CMake project, manifest template, C++/CX shim. |

The controller path is unchanged. CoopDX already reads Xbox controllers through SDL, and SDL's WinRT backend feeds that same API.

## What you still have to do

1. **Baseline PC build.** Build CoopDX natively (`make WINDOWS_BUILD=1`, MinGW) with your ROM. This produces the generated sources and headers under `build/us_pc` that the CMake project points at with `GENERATED_DIR`. Run `make -n WINDOWS_BUILD=1` and compare its `-D` flags with `target_compile_definitions` in `uwp/CMakeLists.txt`.
2. **SDL2 for WinRT.** Build SDL2 from its `VisualC-WinRT` project (x64, Release). Set `SDL2_SOURCE_DIR` and `SDL2_WINRT_LIB_DIR`.
3. **Lua 5.3 for UWP.** The repo only ships MinGW builds in `lib/lua`. Compile Lua 5.3 sources as a static lib with the UWP toolset and set `LUA_LIB` and `LUA_INCLUDE_DIR`.
4. **Stubs.** `crash_handler.c` (DbgHelp) and `update_checker.c` are excluded. Add small stub files that define the functions the rest of the code calls, or guard their callers with `TARGET_UWP`. The linker will list what is missing.
5. **Discord, CoopNet.** Left undefined for the first boot. The Discord SDK is a Win32 DLL and cannot load. CoopNet needs its own UWP build of the library in `lib/coopnet`.
6. **Manifest and logos.** Fill in `Identity` and `Publisher` from Partner Center and add logo PNGs under `uwp/Assets` (`StoreLogo`, `Square150x150Logo`, `Square44x44Logo`, `Wide310x150Logo`, `SplashScreen`).
7. **Configure, build, deploy.**

   ```
   cmake -S uwp -B build-uwp -G "Visual Studio 17 2022" -A x64 ^
         -DSDL2_SOURCE_DIR=... -DSDL2_WINRT_LIB_DIR=... ^
         -DLUA_LIB=... -DLUA_INCLUDE_DIR=... ^
         -DGENERATED_DIR=... -DD3DCOMPILER_DLL=...
   ```

   Open the generated solution, build x64 Release, and deploy to the console from Visual Studio (Remote Machine, Universal authentication) or through Device Portal.
8. **ROM.** Upload your ROM through Device Portal's File Explorer into the app's LocalState folder. Check the file name the game expects in the CoopDX README.

## Known gaps and risks

- **Runtime shader compile.** CoopDX compiles HLSL per color combiner at runtime. This patch ships `d3dcompiler_47.dll` in the package. If the certification kit or the console rejects that, the fallback is to precompile the shaders at build time, which is a larger change in `gfx_direct3d11.cpp`.
- **Text entry.** Typing a join IP or name needs the on-screen keyboard. SDL text input on WinRT may or may not raise it; pre-fill values in `sm64config.txt` as a workaround.
- **Mods folder.** The package folder is read-only. Mods you add at runtime land in the user path; check that the mod loader scans it.
- **Window size and DPI.** The swap chain uses the size SDL reports. Check for blurry or letterboxed output at 1080p and 4K.
- **Memory.** Microsoft documents a 5 GB limit for UWP games (1 GB for apps) and the limit is not enforced under the debugger, so test a normal launch.
- **Banned APIs.** Run the Windows App Certification Kit on the package to list remaining banned Win32 calls.
