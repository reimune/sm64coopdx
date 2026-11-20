// C++/CX shim required by SDL's WinRT port.
//
// The linker only produces a .winmd when at least one translation unit is compiled
// with /ZW. SDL_winrt_main_NonXAML.cpp is also compiled with /ZW; this file keeps
// the requirement satisfied even if SDL's main shim changes.

#include <wrl/client.h>

using namespace Platform;

namespace sm64coopdx_uwp {
    // Referenced by nothing; exists so this file contributes a C++/CX type.
    public ref class WinRtShim sealed {
    public:
        WinRtShim() {}
    };
}
