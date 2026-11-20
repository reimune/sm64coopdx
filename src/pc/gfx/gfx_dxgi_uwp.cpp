// UWP / Xbox window manager for the D3D11 renderer.
//
// Replaces gfx_dxgi.cpp (Win32 window + wndproc) when building with TARGET_UWP.
// SDL2's WinRT backend owns the window and delivers input events; this file
// creates the DXGI swap chain on the CoreWindow SDL exposes and presents it.
//
// UNTESTED: written without a UWP toolchain. Expect compile fixes on first build.

#if defined(_WIN32) && defined(TARGET_UWP)

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>

#include <windows.h>
#include <wrl/client.h>
#include <dxgi1_3.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#define DECLARE_GFX_DXGI_FUNCTIONS
#include "gfx_dxgi.h"

extern "C" {
    #include "pc/configfile.h"
    #include "pc/controller/controller_bind_mapping.h"
}

#include "pc/pc_main.h"

#include "gfx_window_manager_api.h"
#include "gfx_rendering_api.h"
#include "gfx_direct3d_common.h"
#include "gfx_screen_config.h"
#include "gfx_pc.h"

using namespace Microsoft::WRL; // For ComPtr

static struct {
    SDL_Window *wnd;
    uint32_t current_width, current_height;
    std::string window_title;

    ComPtr<IDXGIFactory2> factory;
    ComPtr<IDXGISwapChain1> swap_chain;
    uint64_t qpc_init, qpc_freq;
    UINT length_in_vsync_frames;

    kb_callback_t on_key_down;
    kb_callback_t on_key_up;
    void (*on_all_keys_up)(void);
    void (*on_text_input)(char*);
    void (*on_text_editing)(char*, int);
    void (*on_scroll)(float, float);
} dxgi;

// Size of the window in physical pixels, as SDL reports it.
void gfx_dxgi_get_window_pixel_size(uint32_t *width, uint32_t *height) {
    int w = 0, h = 0;
    if (dxgi.wnd != nullptr) { SDL_GetWindowSize(dxgi.wnd, &w, &h); }
    if (w < 1) { w = DESIRED_SCREEN_WIDTH; }
    if (h < 1) { h = DESIRED_SCREEN_HEIGHT; }
    *width = (uint32_t)w;
    *height = (uint32_t)h;
}

static void gfx_dxgi_init(const char *window_title) {
    LARGE_INTEGER qpc_init, qpc_freq;
    QueryPerformanceCounter(&qpc_init);
    QueryPerformanceFrequency(&qpc_freq);
    dxgi.qpc_init = qpc_init.QuadPart;
    dxgi.qpc_freq = qpc_freq.QuadPart;
    dxgi.window_title = window_title;

    SDL_Init(SDL_INIT_VIDEO);
    SDL_StartTextInput();

    // On WinRT the window always fills the screen; the size passed here is a hint.
    dxgi.wnd = SDL_CreateWindow(
        window_title,
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        DESIRED_SCREEN_WIDTH, DESIRED_SCREEN_HEIGHT,
        SDL_WINDOW_SHOWN
    );
    if (dxgi.wnd == nullptr) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        throw E_FAIL;
    }

    SDL_ShowCursor(SDL_DISABLE);
}

static void gfx_dxgi_set_keyboard_callbacks(kb_callback_t on_key_down, kb_callback_t on_key_up, void (*on_all_keys_up)(void),
                                            void (*on_text_input)(char*), void (*on_text_editing)(char*, int)) {
    dxgi.on_key_down = on_key_down;
    dxgi.on_key_up = on_key_up;
    dxgi.on_all_keys_up = on_all_keys_up;
    dxgi.on_text_input = on_text_input;
    dxgi.on_text_editing = on_text_editing;
}

static void gfx_dxgi_set_scroll_callback(void (*on_scroll)(float, float)) {
    dxgi.on_scroll = on_scroll;
}

static void gfx_dxgi_main_loop(void (*run_one_game_iter)(void)) {
    run_one_game_iter();
}

static void gfx_dxgi_get_dimensions(uint32_t *width, uint32_t *height) {
    *width = dxgi.current_width;
    *height = dxgi.current_height;
}

static void gfx_dxgi_on_resize(void) {
    if (dxgi.swap_chain.Get() != nullptr) {
        gfx_get_current_rendering_api()->on_resize();

        DXGI_SWAP_CHAIN_DESC1 desc1;
        ThrowIfFailed(dxgi.swap_chain->GetDesc1(&desc1));
        dxgi.current_width = desc1.Width;
        dxgi.current_height = desc1.Height;
    }
}

// UWP D3D11 apps must call Trim() when they are suspended.
static void gfx_dxgi_trim(void) {
    if (dxgi.swap_chain.Get() == nullptr) { return; }
    ComPtr<IDXGIDevice3> dxgi_device;
    if (SUCCEEDED(dxgi.swap_chain->GetDevice(IID_PPV_ARGS(&dxgi_device)))) {
        dxgi_device->Trim();
    }
}

static void gfx_dxgi_handle_events(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_TEXTINPUT:
                if (dxgi.on_text_input != nullptr) { dxgi.on_text_input(event.text.text); }
                break;
            case SDL_TEXTEDITING:
                if (dxgi.on_text_editing != nullptr) { dxgi.on_text_editing(event.edit.text, event.edit.start); }
                break;
            case SDL_KEYDOWN:
                if (dxgi.on_key_down != nullptr) { dxgi.on_key_down(translate_sdl_scancode(event.key.keysym.scancode)); }
                break;
            case SDL_KEYUP:
                if (dxgi.on_key_up != nullptr) { dxgi.on_key_up(translate_sdl_scancode(event.key.keysym.scancode)); }
                break;
            case SDL_MOUSEWHEEL:
                if (dxgi.on_scroll != nullptr) { dxgi.on_scroll(event.wheel.preciseX, event.wheel.preciseY); }
                break;
            case SDL_WINDOWEVENT:
                switch (event.window.event) {
                    case SDL_WINDOWEVENT_SIZE_CHANGED:
                        gfx_dxgi_on_resize();
                        break;
                    case SDL_WINDOWEVENT_FOCUS_LOST:
                        if (dxgi.on_all_keys_up != nullptr) { dxgi.on_all_keys_up(); }
                        break;
                }
                break;
            case SDL_APP_DIDENTERBACKGROUND:
                gfx_dxgi_trim();
                break;
            case SDL_QUIT:
                game_exit();
                break;
        }
    }

    // The window is always fullscreen on Xbox; ignore window-mode setting changes.
    configWindow.reset = false;
    configWindow.settings_changed = false;
}

static bool gfx_dxgi_start_frame(void) {
    dxgi.length_in_vsync_frames = configWindow.vsync;
    return true;
}

static void gfx_dxgi_swap_buffers_begin(void) {
    ThrowIfFailed(dxgi.swap_chain->Present(dxgi.length_in_vsync_frames, 0));
}

static void gfx_dxgi_swap_buffers_end(void) {
}

static double gfx_dxgi_get_time(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - dxgi.qpc_init) / dxgi.qpc_freq;
}

void gfx_dxgi_create_factory_and_device(bool debug, int d3d_version, bool (*create_device_fn)(IDXGIAdapter1 *adapter, bool test_only)) {
    ThrowIfFailed(CreateDXGIFactory2(debug ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&dxgi.factory)));

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; dxgi.factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (desc.Flags & 2/*DXGI_ADAPTER_FLAG_SOFTWARE*/) {
            continue;
        }
        if (create_device_fn(adapter.Get(), true)) {
            break;
        }
    }
    create_device_fn(adapter.Get(), false);
}

ComPtr<IDXGISwapChain1> gfx_dxgi_create_swap_chain(IUnknown *device) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(dxgi.wnd, &info) || info.subsystem != SDL_SYSWM_WINRT) {
        fprintf(stderr, "SDL did not return a WinRT window: %s\n", SDL_GetError());
        throw E_FAIL;
    }
    // The IInspectable is the app's CoreWindow.
    IUnknown *core_window = reinterpret_cast<IUnknown *>(info.info.winrt.window);

    uint32_t width, height;
    gfx_dxgi_get_window_pixel_size(&width, &height);

    DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
    swap_chain_desc.BufferCount = 2;
    swap_chain_desc.Width = width;
    swap_chain_desc.Height = height;
    swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc.Scaling = DXGI_SCALING_NONE;
    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swap_chain_desc.SampleDesc.Count = 1;

    ThrowIfFailed(dxgi.factory->CreateSwapChainForCoreWindow(device, core_window, &swap_chain_desc, nullptr, &dxgi.swap_chain));

    ComPtr<IDXGIDevice1> device1;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device1)))) {
        device1->SetMaximumFrameLatency(1);
    }

    ThrowIfFailed(dxgi.swap_chain->GetDesc1(&swap_chain_desc));
    dxgi.current_width = swap_chain_desc.Width;
    dxgi.current_height = swap_chain_desc.Height;

    return dxgi.swap_chain;
}

void gfx_dxgi_delay(u32 ms) {
    SDL_Delay(ms);
}

static int gfx_dxgi_get_max_msaa(void) {
    return 0;
}

static void gfx_dxgi_set_window_title(const char *title) {
    SDL_SetWindowTitle(dxgi.wnd, title);
}

static void gfx_dxgi_reset_window_title(void) {
    SDL_SetWindowTitle(dxgi.wnd, TITLE);
}

static bool gfx_dxgi_has_focus(void) {
    return dxgi.wnd != nullptr && (SDL_GetWindowFlags(dxgi.wnd) & SDL_WINDOW_INPUT_FOCUS);
}

extern "C" HWND gfx_dxgi_get_h_wnd(void) {
    return nullptr; // there is no HWND on UWP
}

void gfx_dxgi_shutdown(void) {
    dxgi.swap_chain.Reset();
    dxgi.factory.Reset();
    if (dxgi.wnd != nullptr) { SDL_DestroyWindow(dxgi.wnd); dxgi.wnd = nullptr; }
    SDL_Quit();
}

void gfx_dxgi_start_text_input(void) { SDL_StartTextInput(); }
void gfx_dxgi_stop_text_input(void) { SDL_StopTextInput(); }

static char *gfx_dxgi_get_clipboard_text(void) {
    static char clipboard_buf[WAPI_CLIPBOARD_BUFSIZ];
    clipboard_buf[0] = '\0';

    char *text = SDL_GetClipboardText();
    if (text != nullptr) {
        strncpy(clipboard_buf, text, WAPI_CLIPBOARD_BUFSIZ - 1);
        SDL_free(text);
    }

    clipboard_buf[WAPI_CLIPBOARD_BUFSIZ - 1] = '\0';
    return clipboard_buf;
}

void gfx_dxgi_set_clipboard_text(const char *text) { SDL_SetClipboardText(text); }

void gfx_dxgi_set_cursor_visible(bool visible) { SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE); }

void ThrowIfFailed(HRESULT res) {
    if (FAILED(res)) {
        fprintf(stderr, "Error: 0x%08X\n", (unsigned int)res);
        throw res;
    }
}

void ThrowIfFailed(HRESULT res, HWND h_wnd, const char *message) {
    if (FAILED(res)) {
        fprintf(stderr, "%s\n\nHRESULT: 0x%08X\n", message, (unsigned int)res);
        throw res;
    }
}

struct GfxWindowManagerAPI gfx_dxgi = {
    gfx_dxgi_init,
    gfx_dxgi_set_keyboard_callbacks,
    gfx_dxgi_set_scroll_callback,
    gfx_dxgi_main_loop,
    gfx_dxgi_get_dimensions,
    gfx_dxgi_handle_events,
    gfx_dxgi_start_frame,
    gfx_dxgi_swap_buffers_begin,
    gfx_dxgi_swap_buffers_end,
    gfx_dxgi_get_time,
    gfx_dxgi_shutdown,
    gfx_dxgi_start_text_input,
    gfx_dxgi_stop_text_input,
    gfx_dxgi_get_clipboard_text,
    gfx_dxgi_set_clipboard_text,
    gfx_dxgi_set_cursor_visible,
    gfx_dxgi_delay,
    gfx_dxgi_get_max_msaa,
    gfx_dxgi_set_window_title,
    gfx_dxgi_reset_window_title,
    gfx_dxgi_has_focus
};

#endif
