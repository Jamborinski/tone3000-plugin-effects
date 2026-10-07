// src/MingwComCompat.cpp
// MinGW-w64 cross-compilation compatibility shims (MinGW only; inert on MSVC).
//
// JUCE's Windows COM code relies on MSVC features MinGW does not provide. Three symbols end up
// undefined at link, so we supply definitions here. Signatures were reverse-matched against the
// exact mangled names the JUCE objects reference (juce_gui_basics.cpp / juce_graphics.cpp):
//
//   (1) `__uuidof(ptr)` where ptr is a local `juce::ComSmartPtr<IDXGI*>`. MinGW's <_mingw.h> does
//       `#define __uuidof(t) __mingw_uuidof<__typeof(t)>()`, and (because __uuidof is already
//       defined) JUCE's own UUIDGetter fallback is skipped. MinGW provides __mingw_uuidof<T>() for
//       __CRT_UUID_DECL interfaces (IDXGIDevice, ...) but not for the ComSmartPtr<T> wrapper, so
//       provide those two explicit specializations (IIDs from <dxgi.h>). A void* "keepalive" reference
//       forces GCC to EMIT the (otherwise unreferenced-in-TU) explicit specializations as external
//       symbols so the linker finds them.
//
//   (2) IDWriteFactory::CreateCustomRenderingParams -- JUCE calls it with a QUALIFIED name
//       (factory->IDWriteFactory::CreateCustom...), a NON-virtual call. MinGW's <dwrite.h> declares
//       it `= 0` with no body, so the qualified call has nothing to bind to. Provide an out-of-line
//       definition (exact 6-arg signature) whose body forwards through a plain (virtual) call, which
//       dispatches to the concrete implementation in dwrite.dll.
//
//   (3) TaskDialogIndirect -- a Windows Vista+ API in comdlg32.dll used by JUCE's native message box.
//       MinGW's stock libcomdlg32.a does not export it. Linking the generated "comdlg32_taskdlg"
//       import lib for it (the previous approach) creates a HARD load-time import of
//       comdlg32.dll!TaskDialogIndirect that some systems refuse to resolve, so the OS fails to start
//       the exe. Instead: resolve the real function by name at runtime (GetProcAddress) with a
//       MessageBox fallback. JUCE's call site compiles TaskDialogIndirect as __declspec(dllimport)
//       (commctrl.h's WINCOMMCTRLAPI), so it references the import-thunk symbol __imp_TaskDialogIndirect;
//       we provide that thunk as a thin forwarder to the local implementation. The real logic lives in a
//       static helper so the thunk does NOT recurse through the (dllimport-declared) plain name.

#include <windows.h>
#include <dxgi.h>
#include <dwrite.h>
#include <commctrl.h>

namespace juce { template <typename T> class ComSmartPtr; }

// (1) __uuidof -> __mingw_uuidof<T>() explicit specializations for JUCE's ComSmartPtr wrappers.
template <> const GUID & __mingw_uuidof<juce::ComSmartPtr<IDXGIDevice>> () noexcept { return __uuidof (IDXGIDevice); }
template <> const GUID & __mingw_uuidof<juce::ComSmartPtr<IDXGISurface>> () noexcept { return __uuidof (IDXGISurface); }

// Force emission of the (unreferenced-in-TU) explicit specializations as external symbols.
__attribute__((used)) static const void *g_mingw_uuidof_device  = reinterpret_cast<const void*> (&__mingw_uuidof<juce::ComSmartPtr<IDXGIDevice>>);
__attribute__((used)) static const void *g_mingw_uuidof_surface = reinterpret_cast<const void*> (&__mingw_uuidof<juce::ComSmartPtr<IDXGISurface>>);

// (2) Out-of-line definition for the qualified (non-virtual) call; body forwards via virtual dispatch.
HRESULT IDWriteFactory::CreateCustomRenderingParams (FLOAT gamma, FLOAT enhancedContrast, FLOAT cleartype_level, DWRITE_PIXEL_GEOMETRY geometry, DWRITE_RENDERING_MODE mode, IDWriteRenderingParams **params)
{
    return CreateCustomRenderingParams (gamma, enhancedContrast, cleartype_level, geometry, mode, params);
}

// (3) TaskDialogIndirect: resolve the real one by name; fall back to a plain message box.
static HRESULT WINAPI TaskDialogIndirect_local (const TASKDIALOGCONFIG *pTaskConfig, int *pnButton, int *pnRadioButton, WINBOOL *pfVerificationFlagChecked)
{
    using TDI = HRESULT (WINAPI *)(const TASKDIALOGCONFIG*, int*, int*, WINBOOL*);
    TDI real = nullptr;
    if (HMODULE h = GetModuleHandleW (L"comdlg32.dll"))
        real = reinterpret_cast<TDI> (GetProcAddress (h, "TaskDialogIndirect"));
    if (real == nullptr)
    {
        HMODULE h2 = LoadLibraryW (L"comdlg32.dll");
        if (h2 != nullptr)
            real = reinterpret_cast<TDI> (GetProcAddress (h2, "TaskDialogIndirect"));
    }
    if (real != nullptr)
        return real (pTaskConfig, pnButton, pnRadioButton, pfVerificationFlagChecked);

    // Genuinely unavailable (pre-Vista): best-effort fallback so a message is still shown.
    if (pnButton != nullptr) *pnButton = 0;
    if (pTaskConfig != nullptr)
        MessageBoxW (pTaskConfig->hwndParent,
                     pTaskConfig->pszContent != nullptr ? pTaskConfig->pszContent : L"Error",
                     pTaskConfig->pszWindowTitle,
                     MB_OK | MB_ICONERROR);
    return 0;
}

extern "C" HRESULT WINAPI TaskDialogIndirect (const TASKDIALOGCONFIG *pTaskConfig, int *pnButton, int *pnRadioButton, WINBOOL *pfVerificationFlagChecked)
{
    return TaskDialogIndirect_local (pTaskConfig, pnButton, pnRadioButton, pfVerificationFlagChecked);
}

// Import-thunk symbol for __declspec(dllimport) callers (JUCE); forward to the local implementation.
extern "C" HRESULT WINAPI __imp_TaskDialogIndirect (const TASKDIALOGCONFIG *pTaskConfig, int *pnButton, int *pnRadioButton, WINBOOL *pfVerificationFlagChecked)
{
    return TaskDialogIndirect_local (pTaskConfig, pnButton, pnRadioButton, pfVerificationFlagChecked);
}
