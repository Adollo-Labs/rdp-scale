#include <windows.h>
#include <shellscalingapi.h>
#include <detours.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>

namespace {

using GetDpiForMonitorFn = HRESULT(WINAPI*)(
    HMONITOR,
    MONITOR_DPI_TYPE,
    UINT*,
    UINT*);
using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
using GetDpiForSystemFn = UINT(WINAPI*)();
using GetDeviceCapsFn = int(WINAPI*)(HDC, int);
using GetScaleFactorForMonitorFn = HRESULT(WINAPI*)(
    HMONITOR,
    DEVICE_SCALE_FACTOR*);

GetDpiForMonitorFn g_originalGetDpiForMonitor = GetDpiForMonitor;
GetDpiForWindowFn g_originalGetDpiForWindow = nullptr;
GetDpiForSystemFn g_originalGetDpiForSystem = nullptr;
GetDeviceCapsFn g_originalGetDeviceCaps = nullptr;
GetScaleFactorForMonitorFn g_originalGetScaleFactorForMonitor = nullptr;

UINT g_effectiveDpi = 96;
wchar_t g_tracePath[32768]{};
volatile LONG g_traceLines = 0;

void Trace(const char* format, ...) {
    if (!g_tracePath[0] || InterlockedIncrement(&g_traceLines) > 2000) {
        return;
    }

    char message[1024]{};
    va_list args;
    va_start(args, format);
    const int chars = vsnprintf_s(
        message, ARRAYSIZE(message), _TRUNCATE, format, args);
    va_end(args);
    if (chars <= 0) {
        return;
    }

    HANDLE file = CreateFileW(
        g_tracePath,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD written = 0;
    WriteFile(file, message, static_cast<DWORD>(chars), &written, nullptr);
    WriteFile(file, "\r\n", 2, &written, nullptr);
    CloseHandle(file);
}

void SignalFromEnvironment(const wchar_t* variableName) {
    wchar_t eventName[512]{};
    const DWORD capacity = static_cast<DWORD>(ARRAYSIZE(eventName));
    const DWORD chars = GetEnvironmentVariableW(variableName, eventName, capacity);
    if (chars == 0 || chars >= capacity) {
        return;
    }

    HANDLE eventHandle = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName);
    if (eventHandle) {
        SetEvent(eventHandle);
        CloseHandle(eventHandle);
    }
}

bool ReadDpiFromEnvironment() {
    wchar_t dpiBuffer[32]{};
    const DWORD capacity = static_cast<DWORD>(ARRAYSIZE(dpiBuffer));
    const DWORD chars = GetEnvironmentVariableW(L"RDPSCALE_DPI", dpiBuffer, capacity);
    if (chars == 0 || chars >= capacity) {
        return false;
    }

    wchar_t* end = nullptr;
    const unsigned long dpi = std::wcstoul(dpiBuffer, &end, 10);
    if (!end || *end != L'\0' || dpi < 96 || dpi > 480) {
        return false;
    }

    g_effectiveDpi = static_cast<UINT>(dpi);
    return true;
}

void ReadTracePath() {
    const DWORD capacity = static_cast<DWORD>(ARRAYSIZE(g_tracePath));
    const DWORD chars = GetEnvironmentVariableW(
        L"RDPSCALE_TRACE_PATH", g_tracePath, capacity);
    if (chars == 0 || chars >= capacity) {
        g_tracePath[0] = L'\0';
    }
}

template <typename T>
T ResolveLoaded(const wchar_t* moduleName, const char* functionName) {
    HMODULE module = GetModuleHandleW(moduleName);
    return module
        ? reinterpret_cast<T>(GetProcAddress(module, functionName))
        : nullptr;
}

HRESULT WINAPI HookGetDpiForMonitor(
    HMONITOR monitor,
    MONITOR_DPI_TYPE dpiType,
    UINT* dpiX,
    UINT* dpiY) {

    const HRESULT hr = g_originalGetDpiForMonitor(
        monitor, dpiType, dpiX, dpiY);

    const UINT originalX = dpiX ? *dpiX : 0;
    const UINT originalY = dpiY ? *dpiY : 0;

    if (SUCCEEDED(hr) && dpiType == MDT_EFFECTIVE_DPI) {
        if (dpiX) {
            *dpiX = g_effectiveDpi;
        }
        if (dpiY) {
            *dpiY = g_effectiveDpi;
        }
    }

    Trace(
        "GetDpiForMonitor type=%d hr=0x%08lx original=%u,%u returned=%u,%u",
        static_cast<int>(dpiType),
        static_cast<unsigned long>(hr),
        originalX,
        originalY,
        dpiX ? *dpiX : 0,
        dpiY ? *dpiY : 0);

    return hr;
}

UINT WINAPI HookGetDpiForWindow(HWND hwnd) {
    const UINT value = g_originalGetDpiForWindow(hwnd);
    Trace("GetDpiForWindow hwnd=%p -> %u", hwnd, value);
    return value;
}

UINT WINAPI HookGetDpiForSystem() {
    const UINT value = g_originalGetDpiForSystem();
    Trace("GetDpiForSystem -> %u", value);
    return value;
}

int WINAPI HookGetDeviceCaps(HDC hdc, int index) {
    const int value = g_originalGetDeviceCaps(hdc, index);
    if (index == LOGPIXELSX || index == LOGPIXELSY) {
        Trace("GetDeviceCaps index=%d -> %d", index, value);
    }
    return value;
}

HRESULT WINAPI HookGetScaleFactorForMonitor(
    HMONITOR monitor,
    DEVICE_SCALE_FACTOR* factor) {

    const HRESULT hr = g_originalGetScaleFactorForMonitor(monitor, factor);
    const int originalFactor =
        (SUCCEEDED(hr) && factor) ? static_cast<int>(*factor) : -1;

    if (SUCCEEDED(hr) && factor) {
        *factor = static_cast<DEVICE_SCALE_FACTOR>(175);
    }

    Trace(
        "GetScaleFactorForMonitor hr=0x%08lx original=%d returned=%d",
        static_cast<unsigned long>(hr),
        originalFactor,
        factor ? static_cast<int>(*factor) : -1);
    return hr;
}

bool Attach(PVOID* original, PVOID hook) {
    return original && *original &&
           DetourAttach(original, hook) == NO_ERROR;
}

bool InstallHook() {
    if (!ReadDpiFromEnvironment()) {
        return false;
    }
    ReadTracePath();

    if (g_tracePath[0]) {
        g_originalGetDpiForWindow =
            ResolveLoaded<GetDpiForWindowFn>(L"user32.dll", "GetDpiForWindow");
        g_originalGetDpiForSystem =
            ResolveLoaded<GetDpiForSystemFn>(L"user32.dll", "GetDpiForSystem");
        g_originalGetDeviceCaps =
            ResolveLoaded<GetDeviceCapsFn>(L"gdi32.dll", "GetDeviceCaps");
        g_originalGetScaleFactorForMonitor =
            ResolveLoaded<GetScaleFactorForMonitorFn>(
                L"shcore.dll", "GetScaleFactorForMonitor");
    }

    if (DetourTransactionBegin() != NO_ERROR) {
        return false;
    }
    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR) {
        DetourTransactionAbort();
        return false;
    }
    if (!Attach(
            reinterpret_cast<PVOID*>(&g_originalGetDpiForMonitor),
            reinterpret_cast<PVOID>(HookGetDpiForMonitor))) {
        DetourTransactionAbort();
        return false;
    }

    if (g_tracePath[0]) {
        if (g_originalGetDpiForWindow &&
            !Attach(
                reinterpret_cast<PVOID*>(&g_originalGetDpiForWindow),
                reinterpret_cast<PVOID>(HookGetDpiForWindow))) {
            DetourTransactionAbort();
            return false;
        }
        if (g_originalGetDpiForSystem &&
            !Attach(
                reinterpret_cast<PVOID*>(&g_originalGetDpiForSystem),
                reinterpret_cast<PVOID>(HookGetDpiForSystem))) {
            DetourTransactionAbort();
            return false;
        }
        if (g_originalGetDeviceCaps &&
            !Attach(
                reinterpret_cast<PVOID*>(&g_originalGetDeviceCaps),
                reinterpret_cast<PVOID>(HookGetDeviceCaps))) {
            DetourTransactionAbort();
            return false;
        }
        if (g_originalGetScaleFactorForMonitor &&
            !Attach(
                reinterpret_cast<PVOID*>(&g_originalGetScaleFactorForMonitor),
                reinterpret_cast<PVOID>(HookGetScaleFactorForMonitor))) {
            DetourTransactionAbort();
            return false;
        }
    }

    return DetourTransactionCommit() == NO_ERROR;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (DetourIsHelperProcess()) {
        return TRUE;
    }

    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);

        // DetourCreateProcessWithDllEx temporarily modifies mstsc's in-memory
        // import table. Restore that temporary scaffolding before installing
        // the actual DPI detours.
        DetourRestoreAfterWith();

        if (!InstallHook()) {
            SignalFromEnvironment(L"RDPSCALE_FAILED_EVENT");
            return FALSE;
        }

        SignalFromEnvironment(L"RDPSCALE_READY_EVENT");
    }

    return TRUE;
}
