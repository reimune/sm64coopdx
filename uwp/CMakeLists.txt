# UWP / Xbox Series X Dev Mode project for sm64coopdx.
#
# UNTESTED scaffold: written without a Windows/UWP toolchain available.
# See uwp/README.md for the full workflow.
#
# Configure from a Visual Studio 2022 "x64 Native Tools" prompt:
#   cmake -S uwp -B build-uwp -G "Visual Studio 17 2022" -A x64 ^
#         -DSDL2_SOURCE_DIR=C:/path/to/SDL2 ^
#         -DSDL2_WINRT_LIB_DIR=C:/path/to/SDL2/build/x64/Release ^
#         -DLUA_LIB=C:/path/to/lua53-uwp/lua53.lib ^
#         -DLUA_INCLUDE_DIR=C:/path/to/lua-5.3/src ^
#         -DGENERATED_DIR=C:/path/to/baseline/build/us_pc ^
#         -DD3DCOMPILER_DLL="C:/Program Files (x86)/Windows Kits/10/Redist/D3D/x64/d3dcompiler_47.dll"

cmake_minimum_required(VERSION 3.24)

set(CMAKE_SYSTEM_NAME WindowsStore)
set(CMAKE_SYSTEM_VERSION 10.0)

project(sm64coopdx_xbox C CXX)

get_filename_component(SM64_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# ---- Inputs you must provide -------------------------------------------------
set(SDL2_SOURCE_DIR   "" CACHE PATH "SDL2 source tree (for SDL_winrt_main_NonXAML.cpp and headers)")
set(SDL2_WINRT_LIB_DIR "" CACHE PATH "Folder with SDL2.lib/SDL2.dll built from SDL's VisualC-WinRT project (x64)")
set(LUA_LIB           "" CACHE FILEPATH "Lua 5.3 static lib built for UWP (the repo only ships MinGW builds)")
set(LUA_INCLUDE_DIR   "" CACHE PATH "Lua 5.3 headers")
set(GENERATED_DIR     "" CACHE PATH "build/us_pc folder from a native PC baseline build (generated sources and headers)")
set(D3DCOMPILER_DLL   "" CACHE FILEPATH "d3dcompiler_47.dll (x64) to ship in the package")

foreach(var SDL2_SOURCE_DIR SDL2_WINRT_LIB_DIR LUA_LIB LUA_INCLUDE_DIR GENERATED_DIR D3DCOMPILER_DLL)
  if("${${var}}" STREQUAL "")
    message(FATAL_ERROR "${var} is required. See uwp/README.md.")
  endif()
endforeach()

# ---- Source list (mirrors SRC_DIRS in the Makefile) ---------------------------
set(SRC_DIRS
  src src/engine src/game src/audio src/menu src/buffers
  actors levels bin data assets sound lib
  src/pc src/pc/gfx src/pc/audio src/pc/controller src/pc/fs src/pc/fs/packtypes
  src/pc/mods src/pc/dev src/pc/network src/pc/network/packets src/pc/network/socket
  src/pc/utils src/pc/utils/miniz src/pc/djui src/pc/lua src/pc/lua/utils src/pc/os
  lib/src lib/src/math
  src/goddard src/goddard/dynlists
  dynos
)

set(GAME_SOURCES "")
foreach(dir ${SRC_DIRS})
  file(GLOB c_files   "${SM64_ROOT}/${dir}/*.c")
  file(GLOB cpp_files "${SM64_ROOT}/${dir}/*.cpp")
  list(APPEND GAME_SOURCES ${c_files} ${cpp_files})
endforeach()

# Level data, like LEVEL_C_FILES in the Makefile.
file(GLOB level_c
  "${SM64_ROOT}/levels/*/leveldata.c"
  "${SM64_ROOT}/levels/*/script.c"
  "${SM64_ROOT}/levels/*/geo.c")
list(APPEND GAME_SOURCES ${level_c})

# Desktop-only files that do not belong in the UWP build.
# gfx_dxgi_uwp.cpp replaces gfx_dxgi.cpp; OpenGL and the SDL GL window are unused.
set(EXCLUDED
  "${SM64_ROOT}/src/pc/gfx/gfx_sdl.c"
  "${SM64_ROOT}/src/pc/gfx/gfx_opengl.c"
  "${SM64_ROOT}/src/pc/gfx/gfx_dxgi.cpp"
  "${SM64_ROOT}/src/pc/crash_handler.c"   # DbgHelp; needs a stub (see README)
  "${SM64_ROOT}/src/pc/update_checker.c"  # UPDATER=0; needs a stub
)
list(REMOVE_ITEM GAME_SOURCES ${EXCLUDED})

# ---- Target ------------------------------------------------------------------
add_executable(sm64coopdx WIN32
  ${GAME_SOURCES}
  "${CMAKE_CURRENT_LIST_DIR}/winrt_shim.cpp"
  "${SDL2_SOURCE_DIR}/src/main/winrt/SDL_winrt_main_NonXAML.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/Package.appxmanifest"
)

# One C++/CX file is needed so the linker emits a .winmd (SDL WinRT requirement).
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/winrt_shim.cpp" PROPERTIES COMPILE_FLAGS "/ZW")
set_source_files_properties("${SDL2_SOURCE_DIR}/src/main/winrt/SDL_winrt_main_NonXAML.cpp" PROPERTIES COMPILE_FLAGS "/ZW")
set_target_properties(sm64coopdx PROPERTIES VS_WINRT_COMPONENT TRUE)

target_include_directories(sm64coopdx PRIVATE
  "${SM64_ROOT}/include"
  "${GENERATED_DIR}"
  "${GENERATED_DIR}/include"
  "${SM64_ROOT}/src"
  "${SM64_ROOT}"
  "${SM64_ROOT}/sound"
  "${SM64_ROOT}/lib/coopnet/include"
  "${LUA_INCLUDE_DIR}"
  "${SDL2_SOURCE_DIR}/include"
)

# The Makefile builds the US version with the F3DEX2 microcode family. Verify these
# against `make -n WINDOWS_BUILD=1` output on your PC baseline and copy any others.
target_compile_definitions(sm64coopdx PRIVATE
  TARGET_UWP=1
  _LANGUAGE_C
  HAVE_SDL2=1
  WINSOCK
  VERSION_US
  F3DEX_GBI_2E=1
  F3DEX_GBI_SHARED=1
  NON_MATCHING=1
  AVOID_UB=1
  BITS_64=1
  # Feature flags: Discord SDK, the self-updater and CoopNet are off for the first boot.
  # DISCORD_SDK, COOPNET and UPDATER are intentionally NOT defined.
)

target_compile_options(sm64coopdx PRIVATE /utf-8 /wd4996)

target_link_directories(sm64coopdx PRIVATE "${SDL2_WINRT_LIB_DIR}")
target_link_libraries(sm64coopdx PRIVATE
  SDL2
  "${LUA_LIB}"
  d3d11 dxgi dxguid
  ws2_32
)

# ---- Package content -----------------------------------------------------------
# Read-only content ships inside the package; deploy it next to the exe.
set(CONTENT_DIRS mods lang dynos palettes res)
foreach(content ${CONTENT_DIRS})
  if(EXISTS "${SM64_ROOT}/${content}")
    file(GLOB_RECURSE files RELATIVE "${SM64_ROOT}" "${SM64_ROOT}/${content}/*")
    foreach(f ${files})
      target_sources(sm64coopdx PRIVATE "${SM64_ROOT}/${f}")
      set_source_files_properties("${SM64_ROOT}/${f}" PROPERTIES
        VS_DEPLOYMENT_CONTENT 1
        VS_DEPLOYMENT_LOCATION "${content}")
    endforeach()
  endif()
endforeach()

# d3dcompiler_47.dll: loaded with LoadPackagedLibrary at runtime.
target_sources(sm64coopdx PRIVATE "${D3DCOMPILER_DLL}")
set_source_files_properties("${D3DCOMPILER_DLL}" PROPERTIES VS_DEPLOYMENT_CONTENT 1)

# SDL2.dll from the WinRT build.
target_sources(sm64coopdx PRIVATE "${SDL2_WINRT_LIB_DIR}/SDL2.dll")
set_source_files_properties("${SDL2_WINRT_LIB_DIR}/SDL2.dll" PROPERTIES VS_DEPLOYMENT_CONTENT 1)

# Logo assets required by the manifest (see Package.appxmanifest).
file(GLOB asset_files "${CMAKE_CURRENT_LIST_DIR}/Assets/*.png")
foreach(a ${asset_files})
  target_sources(sm64coopdx PRIVATE "${a}")
  set_source_files_properties("${a}" PROPERTIES
    VS_DEPLOYMENT_CONTENT 1
    VS_DEPLOYMENT_LOCATION "Assets")
endforeach()
