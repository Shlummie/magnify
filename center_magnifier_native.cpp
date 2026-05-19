#define UNICODE
#define _UNICODE

#include <windows.h>
#include <magnification.h>
#include <mmsystem.h>
#include <shellapi.h>

#include <atomic>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <iterator>
#include <string>
#include <thread>

#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "Magnification.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Winmm.lib")

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace
{
constexpr wchar_t kHostWindowClassName[] = L"CenterMagnifierNativeHostWindow";
constexpr wchar_t kPromptWindowClassName[] = L"CenterMagnifierNativePromptWindow";
constexpr wchar_t kTriggerCaptureWindowClassName[] = L"CenterMagnifierNativeTriggerCaptureWindow";
constexpr wchar_t kCenterDotWindowClassName[] = L"CenterMagnifierNativeCenterDotWindow";
constexpr wchar_t kWindowTitle[] = L"Center Magnifier Native";
constexpr wchar_t kSingleInstanceMutexName[] = L"Local\\CenterMagnifierNativeSingleInstance";
constexpr wchar_t kConfigFileName[] = L"center_magnifier_native.ini";
constexpr wchar_t kConfigSection[] = L"Magnifier";

constexpr int kDefaultWindowWidth = 500;
constexpr int kDefaultWindowHeight = 500;
constexpr double kDefaultZoomFactor = 2.0;
constexpr int kDefaultTriggerVirtualKey = VK_XBUTTON1;
constexpr int kDefaultZoomModifierVirtualKey = VK_CONTROL;
constexpr int kDefaultTargetFps = 180;
constexpr bool kDefaultCenterDotEnabled = true;
constexpr int kDefaultCenterDotSize = 7;

constexpr int kMinWindowDimension = 150;
constexpr int kMaxWindowDimension = 2200;
constexpr double kMinZoomFactor = 1.0;
constexpr double kMaxZoomFactor = 8.0;
constexpr int kMinTargetFps = 5;
constexpr int kMaxTargetFps = 240;
constexpr int kMinCenterDotSize = 3;
constexpr int kMaxCenterDotSize = 31;

constexpr UINT_PTR kUpdateTimerId = 1;
constexpr UINT_PTR kTriggerCaptureTimerId = 2;
constexpr UINT kToggleMessage = WM_APP + 1;
constexpr UINT kShowMessage = WM_APP + 2;
constexpr UINT kHideMessage = WM_APP + 3;
constexpr UINT kTrayIconMessage = WM_APP + 4;
constexpr UINT kZoomWheelMessage = WM_APP + 5;
constexpr UINT kTrayIconId = 1;

constexpr int kMinimumTriggerVirtualKey = 0x01;
constexpr int kMaximumTriggerVirtualKey = 0xFE;

enum TrayCommand : UINT
{
    kTrayCommandToggleVisibility = 1001,
    kTrayCommandModeToggle = 1002,
    kTrayCommandModeHold = 1003,
    kTrayCommandFps60 = 1004,
    kTrayCommandFps120 = 1005,
    kTrayCommandZoom150 = 1006,
    kTrayCommandZoom200 = 1007,
    kTrayCommandZoom300 = 1008,
    kTrayCommandZoom400 = 1009,
    kTrayCommandSize400Square = 1010,
    kTrayCommandSize500Square = 1011,
    kTrayCommandSize600Square = 1012,
    kTrayCommandSize800x600 = 1013,
    kTrayCommandCustomZoom = 1014,
    kTrayCommandCustomSize = 1015,
    kTrayCommandReloadConfig = 1016,
    kTrayCommandQuit = 1017,
    kTrayCommandTriggerMouseBack = 1018,
    kTrayCommandTriggerMouseForward = 1019,
    kTrayCommandTriggerMiddleMouse = 1020,
    kTrayCommandTriggerF8 = 1021,
    kTrayCommandTriggerF9 = 1022,
    kTrayCommandTriggerF10 = 1023,
    kTrayCommandTriggerF11 = 1024,
    kTrayCommandTriggerF12 = 1025,
    kTrayCommandTriggerRebind = 1026,
    kTrayCommandFps144 = 1027,
    kTrayCommandFps180 = 1028,
    kTrayCommandFps240 = 1029,
    kTrayCommandZoomModifierCtrl = 1030,
    kTrayCommandZoomModifierAlt = 1031,
    kTrayCommandZoomModifierShift = 1032,
    kTrayCommandZoomModifierMiddleMouse = 1033,
    kTrayCommandZoomModifierMouseForward = 1034,
    kTrayCommandZoomModifierRebind = 1035,
    kTrayCommandToggleCenterDot = 1036,
};

enum PromptControlId : int
{
    kPromptControlFirstLabel = 2001,
    kPromptControlFirstEdit = 2002,
    kPromptControlSecondLabel = 2003,
    kPromptControlSecondEdit = 2004,
    kPromptControlOk = 2005,
    kPromptControlCancel = 2006,
    kCaptureControlInstruction = 3001,
    kCaptureControlStatus = 3002,
    kCaptureControlCancel = 3003,
};

enum class InputMode
{
    Toggle,
    Hold,
};

struct AppConfig
{
    int targetFps = kDefaultTargetFps;
    InputMode inputMode = InputMode::Toggle;
    int windowWidth = kDefaultWindowWidth;
    int windowHeight = kDefaultWindowHeight;
    double zoomFactor = kDefaultZoomFactor;
    int triggerVirtualKey = kDefaultTriggerVirtualKey;
    int zoomModifierVirtualKey = kDefaultZoomModifierVirtualKey;
    bool centerDotEnabled = kDefaultCenterDotEnabled;
    int centerDotSize = kDefaultCenterDotSize;
};

struct PromptDialogState
{
    std::wstring title;
    std::wstring firstLabel;
    std::wstring firstValue;
    std::wstring secondLabel;
    std::wstring secondValue;
    bool hasSecondField = false;
    bool accepted = false;
    bool done = false;
    HWND owner = nullptr;
    HWND window = nullptr;
    HWND firstEdit = nullptr;
    HWND secondEdit = nullptr;
};

struct TriggerCaptureDialogState
{
    std::wstring title = L"Rebind Trigger Button";
    std::wstring targetName = L"trigger";
    std::wstring currentLabel;
    bool waitingForRelease = true;
    bool accepted = false;
    bool done = false;
    int currentVirtualKey = kDefaultTriggerVirtualKey;
    int capturedVirtualKey = kDefaultTriggerVirtualKey;
    HWND owner = nullptr;
    HWND window = nullptr;
    HWND statusLabel = nullptr;
};

HINSTANCE g_instance = nullptr;
HWND g_hostWindow = nullptr;
HWND g_magnifierWindow = nullptr;
HWND g_centerDotWindow = nullptr;
UINT g_taskbarCreatedMessage = 0;
NOTIFYICONDATAW g_trayIconData = {};
bool g_trayIconAdded = false;
HICON g_trayIcon = nullptr;
HHOOK g_mouseHook = nullptr;
bool g_isVisible = false;
bool g_highResolutionTimerActive = false;
AppConfig g_config = {};
std::atomic<int> g_inputModeValue = static_cast<int>(InputMode::Toggle);
std::atomic<int> g_triggerVirtualKeyValue = kDefaultTriggerVirtualKey;
std::atomic<int> g_zoomModifierVirtualKeyValue = kDefaultZoomModifierVirtualKey;
std::atomic<bool> g_inputThreadStopRequested = false;
std::thread g_inputThread;

void ApplyWindowMetrics();
void ApplyZoomFactor(double zoomFactor);
void ApplyWindowSize(int width, int height);
void ApplyTriggerVirtualKey(int virtualKey);
void ApplyZoomModifierVirtualKey(int virtualKey);
void ApplyCenterDotEnabled(bool enabled);
void UpdateCenterDotWindow();
void UpdateTrayIcon();
bool StartInputMonitor();
void StopInputMonitor();
bool StartMouseWheelHook();
void StopMouseWheelHook();
LRESULT CALLBACK PromptWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK TriggerCaptureWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK CenterDotWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam);

void ShowErrorBox(const wchar_t* title, const std::wstring& message)
{
    MessageBoxW(nullptr, message.c_str(), title, MB_ICONERROR | MB_OK);
}

std::wstring GetLastErrorMessage(const wchar_t* prefix)
{
    const DWORD error = GetLastError();
    wchar_t* systemMessage = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&systemMessage),
        0,
        nullptr);

    std::wstring message(prefix);
    if (length != 0 && systemMessage != nullptr)
    {
        message += L"\n\n";
        message += systemMessage;
        LocalFree(systemMessage);
    }
    else
    {
        message += L"\n\nUnknown error.";
    }
    return message;
}

void EnableBestDpiAwareness()
{
    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);

    const HMODULE user32Module = GetModuleHandleW(L"user32.dll");
    const auto setDpiAwarenessContext =
        reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            GetProcAddress(user32Module, "SetProcessDpiAwarenessContext"));

    if (setDpiAwarenessContext &&
        setDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
    {
        return;
    }

    SetProcessDPIAware();
}

std::wstring GetExecutableDirectory()
{
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
    std::wstring path(modulePath);
    const std::size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
    {
        return L".";
    }
    return path.substr(0, separator);
}

std::wstring GetConfigPath()
{
    return GetExecutableDirectory() + L"\\" + kConfigFileName;
}

int ClampInt(int value, int minimum, int maximum)
{
    if (value < minimum)
    {
        return minimum;
    }
    if (value > maximum)
    {
        return maximum;
    }
    return value;
}

double ClampDouble(double value, double minimum, double maximum)
{
    if (value < minimum)
    {
        return minimum;
    }
    if (value > maximum)
    {
        return maximum;
    }
    return value;
}

bool IsNearlyEqual(double left, double right)
{
    return std::fabs(left - right) < 0.01;
}

bool TryParseInt(const std::wstring& text, int* value)
{
    if (value == nullptr)
    {
        return false;
    }

    wchar_t* parseEnd = nullptr;
    const long parsedValue = std::wcstol(text.c_str(), &parseEnd, 10);
    if (parseEnd == text.c_str())
    {
        return false;
    }

    while (*parseEnd != L'\0' && iswspace(*parseEnd))
    {
        ++parseEnd;
    }

    if (*parseEnd != L'\0')
    {
        return false;
    }

    *value = static_cast<int>(parsedValue);
    return true;
}

bool TryParseDouble(const std::wstring& text, double* value)
{
    if (value == nullptr)
    {
        return false;
    }

    wchar_t* parseEnd = nullptr;
    const double parsedValue = std::wcstod(text.c_str(), &parseEnd);
    if (parseEnd == text.c_str() || !std::isfinite(parsedValue))
    {
        return false;
    }

    while (*parseEnd != L'\0' && iswspace(*parseEnd))
    {
        ++parseEnd;
    }

    if (*parseEnd != L'\0')
    {
        return false;
    }

    *value = parsedValue;
    return true;
}

int SanitizeTargetFps(int fps)
{
    if (fps <= 0)
    {
        fps = kDefaultTargetFps;
    }
    return ClampInt(fps, kMinTargetFps, kMaxTargetFps);
}

UINT GetUpdateIntervalMs(int fps)
{
    const int sanitizedFps = SanitizeTargetFps(fps);
    return static_cast<UINT>(ClampInt(1000 / sanitizedFps, 1, 1000));
}

int SanitizeCenterDotSize(int value)
{
    if (value <= 0)
    {
        value = kDefaultCenterDotSize;
    }
    return ClampInt(value, kMinCenterDotSize, kMaxCenterDotSize);
}

int SanitizeWindowDimension(int value, int fallback)
{
    if (value <= 0)
    {
        value = fallback;
    }
    return ClampInt(value, kMinWindowDimension, kMaxWindowDimension);
}

double SanitizeZoomFactor(double value)
{
    if (!std::isfinite(value))
    {
        value = kDefaultZoomFactor;
    }
    return ClampDouble(value, kMinZoomFactor, kMaxZoomFactor);
}

InputMode ParseInputMode(const wchar_t* text)
{
    if (text != nullptr && _wcsicmp(text, L"hold") == 0)
    {
        return InputMode::Hold;
    }
    return InputMode::Toggle;
}

const wchar_t* InputModeToConfigValue(InputMode mode)
{
    return mode == InputMode::Hold ? L"hold" : L"toggle";
}

const wchar_t* InputModeToLabel(InputMode mode)
{
    return mode == InputMode::Hold ? L"Hold To Show" : L"Toggle";
}

const wchar_t* BoolToConfigValue(bool value)
{
    return value ? L"true" : L"false";
}

bool ParseBool(const wchar_t* text, bool defaultValue)
{
    if (text == nullptr || text[0] == L'\0')
    {
        return defaultValue;
    }

    if (_wcsicmp(text, L"true") == 0 ||
        _wcsicmp(text, L"yes") == 0 ||
        _wcsicmp(text, L"on") == 0 ||
        _wcsicmp(text, L"1") == 0)
    {
        return true;
    }

    if (_wcsicmp(text, L"false") == 0 ||
        _wcsicmp(text, L"no") == 0 ||
        _wcsicmp(text, L"off") == 0 ||
        _wcsicmp(text, L"0") == 0)
    {
        return false;
    }

    return defaultValue;
}

std::wstring FormatZoomFactor(double value)
{
    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"%.2fx", value);
    return buffer;
}

int SanitizeTriggerVirtualKey(int virtualKey)
{
    if (virtualKey < kMinimumTriggerVirtualKey || virtualKey > kMaximumTriggerVirtualKey)
    {
        return kDefaultTriggerVirtualKey;
    }
    return virtualKey;
}

std::wstring GetTriggerButtonLabel(int virtualKey)
{
    switch (SanitizeTriggerVirtualKey(virtualKey))
    {
    case VK_LBUTTON:
        return L"Left Mouse";
    case VK_RBUTTON:
        return L"Right Mouse";
    case VK_CANCEL:
        return L"Cancel";
    case VK_MBUTTON:
        return L"Middle Mouse";
    case VK_XBUTTON1:
        return L"Mouse Back";
    case VK_XBUTTON2:
        return L"Mouse Forward";
    case VK_BACK:
        return L"Backspace";
    case VK_TAB:
        return L"Tab";
    case VK_RETURN:
        return L"Enter";
    case VK_SHIFT:
        return L"Shift";
    case VK_CONTROL:
        return L"Ctrl";
    case VK_MENU:
        return L"Alt";
    case VK_PAUSE:
        return L"Pause";
    case VK_CAPITAL:
        return L"Caps Lock";
    case VK_ESCAPE:
        return L"Escape";
    case VK_SPACE:
        return L"Space";
    case VK_PRIOR:
        return L"Page Up";
    case VK_NEXT:
        return L"Page Down";
    case VK_END:
        return L"End";
    case VK_HOME:
        return L"Home";
    case VK_LEFT:
        return L"Left Arrow";
    case VK_UP:
        return L"Up Arrow";
    case VK_RIGHT:
        return L"Right Arrow";
    case VK_DOWN:
        return L"Down Arrow";
    case VK_SNAPSHOT:
        return L"Print Screen";
    case VK_INSERT:
        return L"Insert";
    case VK_DELETE:
        return L"Delete";
    case VK_LWIN:
        return L"Left Windows";
    case VK_RWIN:
        return L"Right Windows";
    case VK_APPS:
        return L"Menu";
    case VK_NUMLOCK:
        return L"Num Lock";
    case VK_SCROLL:
        return L"Scroll Lock";
    case VK_LSHIFT:
        return L"Left Shift";
    case VK_RSHIFT:
        return L"Right Shift";
    case VK_LCONTROL:
        return L"Left Ctrl";
    case VK_RCONTROL:
        return L"Right Ctrl";
    case VK_LMENU:
        return L"Left Alt";
    case VK_RMENU:
        return L"Right Alt";
    default:
        break;
    }

    const int sanitizedVirtualKey = SanitizeTriggerVirtualKey(virtualKey);
    if ((sanitizedVirtualKey >= L'0' && sanitizedVirtualKey <= L'9') ||
        (sanitizedVirtualKey >= L'A' && sanitizedVirtualKey <= L'Z'))
    {
        return std::wstring(1, static_cast<wchar_t>(sanitizedVirtualKey));
    }

    if (sanitizedVirtualKey >= VK_F1 && sanitizedVirtualKey <= VK_F24)
    {
        return L"F" + std::to_wstring((sanitizedVirtualKey - VK_F1) + 1);
    }

    wchar_t keyName[64] = {};
    const UINT scanCode = MapVirtualKeyW(static_cast<UINT>(sanitizedVirtualKey), MAPVK_VK_TO_VSC);
    if (scanCode != 0)
    {
        LONG keyNameParam = static_cast<LONG>(scanCode << 16);
        if (GetKeyNameTextW(keyNameParam, keyName, static_cast<int>(std::size(keyName))) > 0)
        {
            return keyName;
        }
    }

    wchar_t fallback[32] = {};
    swprintf_s(fallback, L"VK 0x%02X", sanitizedVirtualKey);
    return fallback;
}

std::wstring TriggerVirtualKeyToConfigValue(int virtualKey)
{
    switch (SanitizeTriggerVirtualKey(virtualKey))
    {
    case VK_LBUTTON:
        return L"left_mouse";
    case VK_RBUTTON:
        return L"right_mouse";
    case VK_MBUTTON:
        return L"middle_mouse";
    case VK_XBUTTON1:
        return L"mouse_back";
    case VK_XBUTTON2:
        return L"mouse_forward";
    default:
        break;
    }

    const int sanitizedVirtualKey = SanitizeTriggerVirtualKey(virtualKey);
    if (sanitizedVirtualKey >= VK_F1 && sanitizedVirtualKey <= VK_F24)
    {
        return L"f" + std::to_wstring((sanitizedVirtualKey - VK_F1) + 1);
    }

    wchar_t buffer[16] = {};
    swprintf_s(buffer, L"vk_0x%02X", sanitizedVirtualKey);
    return buffer;
}

int ParseTriggerVirtualKey(const wchar_t* text)
{
    if (text == nullptr || text[0] == L'\0')
    {
        return kDefaultTriggerVirtualKey;
    }

    if (_wcsicmp(text, L"left_mouse") == 0 ||
        _wcsicmp(text, L"lbutton") == 0 ||
        _wcsicmp(text, L"left") == 0)
    {
        return VK_LBUTTON;
    }

    if (_wcsicmp(text, L"right_mouse") == 0 ||
        _wcsicmp(text, L"rbutton") == 0 ||
        _wcsicmp(text, L"right") == 0)
    {
        return VK_RBUTTON;
    }

    if (_wcsicmp(text, L"mouse_back") == 0 ||
        _wcsicmp(text, L"xbutton1") == 0 ||
        _wcsicmp(text, L"back") == 0)
    {
        return VK_XBUTTON1;
    }

    if (_wcsicmp(text, L"mouse_forward") == 0 ||
        _wcsicmp(text, L"xbutton2") == 0 ||
        _wcsicmp(text, L"forward") == 0)
    {
        return VK_XBUTTON2;
    }

    if (_wcsicmp(text, L"middle_mouse") == 0 ||
        _wcsicmp(text, L"middle") == 0 ||
        _wcsicmp(text, L"mbutton") == 0)
    {
        return VK_MBUTTON;
    }

    if (_wcsicmp(text, L"space") == 0)
    {
        return VK_SPACE;
    }
    if (_wcsicmp(text, L"tab") == 0)
    {
        return VK_TAB;
    }
    if (_wcsicmp(text, L"enter") == 0 || _wcsicmp(text, L"return") == 0)
    {
        return VK_RETURN;
    }
    if (_wcsicmp(text, L"escape") == 0 || _wcsicmp(text, L"esc") == 0)
    {
        return VK_ESCAPE;
    }
    if (_wcsicmp(text, L"shift") == 0)
    {
        return VK_SHIFT;
    }
    if (_wcsicmp(text, L"ctrl") == 0 || _wcsicmp(text, L"control") == 0)
    {
        return VK_CONTROL;
    }
    if (_wcsicmp(text, L"alt") == 0)
    {
        return VK_MENU;
    }
    if (_wcsicmp(text, L"left_shift") == 0)
    {
        return VK_LSHIFT;
    }
    if (_wcsicmp(text, L"right_shift") == 0)
    {
        return VK_RSHIFT;
    }
    if (_wcsicmp(text, L"left_ctrl") == 0 || _wcsicmp(text, L"left_control") == 0)
    {
        return VK_LCONTROL;
    }
    if (_wcsicmp(text, L"right_ctrl") == 0 || _wcsicmp(text, L"right_control") == 0)
    {
        return VK_RCONTROL;
    }
    if (_wcsicmp(text, L"left_alt") == 0)
    {
        return VK_LMENU;
    }
    if (_wcsicmp(text, L"right_alt") == 0)
    {
        return VK_RMENU;
    }

    if (text[0] != L'\0' && text[1] == L'\0')
    {
        const wchar_t character = towupper(text[0]);
        if ((character >= L'0' && character <= L'9') || (character >= L'A' && character <= L'Z'))
        {
            return static_cast<int>(character);
        }
    }

    if ((text[0] == L'f' || text[0] == L'F') && text[1] != L'\0')
    {
        int functionKeyNumber = 0;
        if (TryParseInt(std::wstring(text + 1), &functionKeyNumber) &&
            functionKeyNumber >= 1 &&
            functionKeyNumber <= 24)
        {
            return VK_F1 + functionKeyNumber - 1;
        }
    }

    const wchar_t* numericText = text;
    int numericBase = 10;
    if (_wcsnicmp(text, L"vk_", 3) == 0)
    {
        numericText = text + 3;
        numericBase = 16;
    }
    if (_wcsnicmp(numericText, L"0x", 2) == 0)
    {
        numericText += 2;
        numericBase = 16;
    }

    wchar_t* parseEnd = nullptr;
    const long parsedVirtualKey = std::wcstol(numericText, &parseEnd, numericBase);
    if (parseEnd != numericText)
    {
        while (*parseEnd != L'\0' && iswspace(*parseEnd))
        {
            ++parseEnd;
        }

        if (*parseEnd == L'\0')
        {
            return SanitizeTriggerVirtualKey(static_cast<int>(parsedVirtualKey));
        }
    }

    int numericVirtualKey = 0;
    if (TryParseInt(text, &numericVirtualKey))
    {
        return SanitizeTriggerVirtualKey(numericVirtualKey);
    }

    return kDefaultTriggerVirtualKey;
}

bool IsVirtualKeyDown(int virtualKey)
{
    return (GetAsyncKeyState(SanitizeTriggerVirtualKey(virtualKey)) & 0x8000) != 0;
}

int GetPressedTriggerVirtualKey()
{
    for (int virtualKey = kMinimumTriggerVirtualKey; virtualKey <= kMaximumTriggerVirtualKey; ++virtualKey)
    {
        if (IsVirtualKeyDown(virtualKey))
        {
            return virtualKey;
        }
    }
    return 0;
}

double ReadDoubleFromIni(const wchar_t* key, double defaultValue, const std::wstring& configPath)
{
    wchar_t defaultBuffer[32] = {};
    swprintf_s(defaultBuffer, L"%.2f", defaultValue);

    wchar_t valueBuffer[64] = {};
    GetPrivateProfileStringW(
        kConfigSection,
        key,
        defaultBuffer,
        valueBuffer,
        static_cast<DWORD>(std::size(valueBuffer)),
        configPath.c_str());

    wchar_t* parseEnd = nullptr;
    const double parsedValue = std::wcstod(valueBuffer, &parseEnd);
    if (parseEnd == valueBuffer)
    {
        return defaultValue;
    }
    return parsedValue;
}

void SaveConfig()
{
    const std::wstring configPath = GetConfigPath();
    const std::wstring targetFps = std::to_wstring(g_config.targetFps);
    const std::wstring windowWidth = std::to_wstring(g_config.windowWidth);
    const std::wstring windowHeight = std::to_wstring(g_config.windowHeight);
    const std::wstring triggerButton = TriggerVirtualKeyToConfigValue(g_config.triggerVirtualKey);
    const std::wstring zoomModifierButton = TriggerVirtualKeyToConfigValue(g_config.zoomModifierVirtualKey);
    const std::wstring centerDotSize = std::to_wstring(g_config.centerDotSize);

    wchar_t zoomBuffer[32] = {};
    swprintf_s(zoomBuffer, L"%.2f", g_config.zoomFactor);

    WritePrivateProfileStringW(kConfigSection, L"TargetFps", targetFps.c_str(), configPath.c_str());
    WritePrivateProfileStringW(
        kConfigSection,
        L"InputMode",
        InputModeToConfigValue(g_config.inputMode),
        configPath.c_str());
    WritePrivateProfileStringW(kConfigSection, L"WindowWidth", windowWidth.c_str(), configPath.c_str());
    WritePrivateProfileStringW(kConfigSection, L"WindowHeight", windowHeight.c_str(), configPath.c_str());
    WritePrivateProfileStringW(kConfigSection, L"ZoomFactor", zoomBuffer, configPath.c_str());
    WritePrivateProfileStringW(kConfigSection, L"TriggerButton", triggerButton.c_str(), configPath.c_str());
    WritePrivateProfileStringW(kConfigSection, L"ZoomModifierButton", zoomModifierButton.c_str(), configPath.c_str());
    WritePrivateProfileStringW(
        kConfigSection,
        L"CenterDotEnabled",
        BoolToConfigValue(g_config.centerDotEnabled),
        configPath.c_str());
    WritePrivateProfileStringW(kConfigSection, L"CenterDotSize", centerDotSize.c_str(), configPath.c_str());
}

void LoadConfig()
{
    const std::wstring configPath = GetConfigPath();
    const bool fileExists = GetFileAttributesW(configPath.c_str()) != INVALID_FILE_ATTRIBUTES;

    g_config.targetFps = SanitizeTargetFps(
        static_cast<int>(GetPrivateProfileIntW(kConfigSection, L"TargetFps", kDefaultTargetFps, configPath.c_str())));
    g_config.windowWidth = SanitizeWindowDimension(
        static_cast<int>(GetPrivateProfileIntW(kConfigSection, L"WindowWidth", kDefaultWindowWidth, configPath.c_str())),
        kDefaultWindowWidth);
    g_config.windowHeight = SanitizeWindowDimension(
        static_cast<int>(GetPrivateProfileIntW(kConfigSection, L"WindowHeight", kDefaultWindowHeight, configPath.c_str())),
        kDefaultWindowHeight);
    g_config.zoomFactor = SanitizeZoomFactor(
        ReadDoubleFromIni(L"ZoomFactor", kDefaultZoomFactor, configPath));
    g_config.centerDotSize = SanitizeCenterDotSize(
        static_cast<int>(GetPrivateProfileIntW(kConfigSection, L"CenterDotSize", kDefaultCenterDotSize, configPath.c_str())));

    wchar_t inputModeBuffer[16] = {};
    GetPrivateProfileStringW(
        kConfigSection,
        L"InputMode",
        L"toggle",
        inputModeBuffer,
        static_cast<DWORD>(std::size(inputModeBuffer)),
        configPath.c_str());
    g_config.inputMode = ParseInputMode(inputModeBuffer);
    g_inputModeValue.store(static_cast<int>(g_config.inputMode), std::memory_order_relaxed);

    wchar_t triggerButtonBuffer[32] = {};
    GetPrivateProfileStringW(
        kConfigSection,
        L"TriggerButton",
        L"mouse_back",
        triggerButtonBuffer,
        static_cast<DWORD>(std::size(triggerButtonBuffer)),
        configPath.c_str());
    g_config.triggerVirtualKey = ParseTriggerVirtualKey(triggerButtonBuffer);
    g_triggerVirtualKeyValue.store(g_config.triggerVirtualKey, std::memory_order_relaxed);

    wchar_t zoomModifierButtonBuffer[32] = {};
    GetPrivateProfileStringW(
        kConfigSection,
        L"ZoomModifierButton",
        L"ctrl",
        zoomModifierButtonBuffer,
        static_cast<DWORD>(std::size(zoomModifierButtonBuffer)),
        configPath.c_str());
    g_config.zoomModifierVirtualKey = ParseTriggerVirtualKey(zoomModifierButtonBuffer);
    g_zoomModifierVirtualKeyValue.store(g_config.zoomModifierVirtualKey, std::memory_order_relaxed);

    wchar_t centerDotEnabledBuffer[16] = {};
    GetPrivateProfileStringW(
        kConfigSection,
        L"CenterDotEnabled",
        BoolToConfigValue(kDefaultCenterDotEnabled),
        centerDotEnabledBuffer,
        static_cast<DWORD>(std::size(centerDotEnabledBuffer)),
        configPath.c_str());
    g_config.centerDotEnabled = ParseBool(centerDotEnabledBuffer, kDefaultCenterDotEnabled);

    if (!fileExists)
    {
        SaveConfig();
    }
}

RECT GetPrimaryMonitorRect()
{
    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);

    const HMONITOR primaryMonitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    GetMonitorInfoW(primaryMonitor, &monitorInfo);
    return monitorInfo.rcMonitor;
}

void CenterHostWindow()
{
    if (!g_hostWindow)
    {
        return;
    }

    const RECT monitorRect = GetPrimaryMonitorRect();
    const int monitorWidth = monitorRect.right - monitorRect.left;
    const int monitorHeight = monitorRect.bottom - monitorRect.top;
    const int windowX = monitorRect.left + ((monitorWidth - g_config.windowWidth) / 2);
    const int windowY = monitorRect.top + ((monitorHeight - g_config.windowHeight) / 2);

    SetWindowPos(
        g_hostWindow,
        HWND_TOPMOST,
        windowX,
        windowY,
        g_config.windowWidth,
        g_config.windowHeight,
        SWP_NOACTIVATE);
}

RECT GetCenteredSourceRect()
{
    const RECT monitorRect = GetPrimaryMonitorRect();
    const int monitorWidth = monitorRect.right - monitorRect.left;
    const int monitorHeight = monitorRect.bottom - monitorRect.top;
    const int sourceWidth = ClampInt(
        static_cast<int>(static_cast<double>(g_config.windowWidth) / g_config.zoomFactor),
        1,
        monitorWidth);
    const int sourceHeight = ClampInt(
        static_cast<int>(static_cast<double>(g_config.windowHeight) / g_config.zoomFactor),
        1,
        monitorHeight);

    RECT sourceRect = {};
    sourceRect.left = monitorRect.left + ((monitorWidth - sourceWidth) / 2);
    sourceRect.top = monitorRect.top + ((monitorHeight - sourceHeight) / 2);
    sourceRect.right = sourceRect.left + sourceWidth;
    sourceRect.bottom = sourceRect.top + sourceHeight;
    return sourceRect;
}

bool ApplyMagnifierTransform()
{
    if (!g_magnifierWindow)
    {
        return false;
    }

    MAGTRANSFORM transform = {};
    transform.v[0][0] = static_cast<float>(g_config.zoomFactor);
    transform.v[1][1] = static_cast<float>(g_config.zoomFactor);
    transform.v[2][2] = 1.0f;
    return MagSetWindowTransform(g_magnifierWindow, &transform) == TRUE;
}

void ApplyAntiMirroring()
{
    if (!g_magnifierWindow || !g_hostWindow)
    {
        return;
    }

    HWND excludedWindows[2] = {};
    int excludedWindowCount = 0;
    excludedWindows[excludedWindowCount++] = g_hostWindow;
    if (g_centerDotWindow)
    {
        excludedWindows[excludedWindowCount++] = g_centerDotWindow;
    }

    MagSetWindowFilterList(
        g_magnifierWindow,
        MW_FILTERMODE_EXCLUDE,
        excludedWindowCount,
        excludedWindows);

    SetWindowDisplayAffinity(g_hostWindow, WDA_EXCLUDEFROMCAPTURE);
    if (g_centerDotWindow)
    {
        SetWindowDisplayAffinity(g_centerDotWindow, WDA_EXCLUDEFROMCAPTURE);
    }
}

bool UpdateMagnifierSource()
{
    if (!g_magnifierWindow)
    {
        return false;
    }

    const RECT sourceRect = GetCenteredSourceRect();
    const bool sourceUpdated = MagSetWindowSource(g_magnifierWindow, sourceRect) == TRUE;
    if (sourceUpdated)
    {
        InvalidateRect(g_magnifierWindow, nullptr, FALSE);
    }
    return sourceUpdated;
}

void EnableHighResolutionTimer()
{
    if (!g_highResolutionTimerActive && timeBeginPeriod(1) == TIMERR_NOERROR)
    {
        g_highResolutionTimerActive = true;
    }
}

void DisableHighResolutionTimer()
{
    if (g_highResolutionTimerActive)
    {
        timeEndPeriod(1);
        g_highResolutionTimerActive = false;
    }
}

std::wstring BuildTrayToolTip()
{
    std::wstring tip = L"Center Magnifier Native - ";
    tip += g_isVisible ? L"Visible" : L"Hidden";
    tip += L" - ";
    tip += GetTriggerButtonLabel(g_config.triggerVirtualKey);
    tip += L" / Wheel: ";
    tip += GetTriggerButtonLabel(g_config.zoomModifierVirtualKey);
    tip += L" - ";
    tip += FormatZoomFactor(g_config.zoomFactor);
    tip += L" - ";
    tip += std::to_wstring(g_config.windowWidth);
    tip += L"x";
    tip += std::to_wstring(g_config.windowHeight);
    tip += L" - ";
    tip += g_config.centerDotEnabled ? L"Dot On" : L"Dot Off";
    tip += L" - ";
    tip += std::to_wstring(g_config.targetFps);
    tip += L" Hz";
    return tip;
}

void ApplyDialogFont(HWND window)
{
    if (!window)
    {
        return;
    }

    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

bool ShowPromptDialog(PromptDialogState* state)
{
    if (state == nullptr || !g_hostWindow)
    {
        return false;
    }

    state->owner = g_hostWindow;
    state->done = false;
    state->accepted = false;

    const RECT monitorRect = GetPrimaryMonitorRect();
    const int dialogWidth = 340;
    const int dialogHeight = state->hasSecondField ? 210 : 170;
    const int dialogX = monitorRect.left + (((monitorRect.right - monitorRect.left) - dialogWidth) / 2);
    const int dialogY = monitorRect.top + (((monitorRect.bottom - monitorRect.top) - dialogHeight) / 2);

    EnableWindow(state->owner, FALSE);

    state->window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        kPromptWindowClassName,
        state->title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        dialogX,
        dialogY,
        dialogWidth,
        dialogHeight,
        state->owner,
        nullptr,
        g_instance,
        state);

    if (!state->window)
    {
        EnableWindow(state->owner, TRUE);
        return false;
    }

    ShowWindow(state->window, SW_SHOWNORMAL);
    UpdateWindow(state->window);

    MSG message = {};
    while (!state->done && GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (message.message == WM_QUIT)
        {
            PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }

        if (!IsDialogMessageW(state->window, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (IsWindow(state->owner))
    {
        EnableWindow(state->owner, TRUE);
        SetForegroundWindow(state->owner);
    }

    return state->accepted;
}

void PromptForCustomZoom()
{
    while (true)
    {
        PromptDialogState state = {};
        state.title = L"Custom Zoom";
        state.firstLabel = L"Zoom factor (1.0 - 8.0)";
        state.firstValue = std::to_wstring(g_config.zoomFactor);

        if (!ShowPromptDialog(&state))
        {
            return;
        }

        double zoomFactor = 0.0;
        if (!TryParseDouble(state.firstValue, &zoomFactor))
        {
            ShowErrorBox(L"Custom Zoom", L"Enter a valid number such as 1.75 or 2.50.");
            continue;
        }

        ApplyZoomFactor(zoomFactor);
        return;
    }
}

void PromptForCustomWindowSize()
{
    while (true)
    {
        PromptDialogState state = {};
        state.title = L"Custom Window Size";
        state.firstLabel = L"Width (150 - 2200)";
        state.firstValue = std::to_wstring(g_config.windowWidth);
        state.secondLabel = L"Height (150 - 2200)";
        state.secondValue = std::to_wstring(g_config.windowHeight);
        state.hasSecondField = true;

        if (!ShowPromptDialog(&state))
        {
            return;
        }

        int width = 0;
        int height = 0;
        if (!TryParseInt(state.firstValue, &width) || !TryParseInt(state.secondValue, &height))
        {
            ShowErrorBox(L"Custom Window Size", L"Enter whole-number width and height values.");
            continue;
        }

        ApplyWindowSize(width, height);
        return;
    }
}

bool ShowTriggerCaptureDialog(TriggerCaptureDialogState* state)
{
    if (state == nullptr || !g_hostWindow)
    {
        return false;
    }

    state->owner = g_hostWindow;
    state->done = false;
    state->accepted = false;
    state->waitingForRelease = true;
    state->capturedVirtualKey = state->currentVirtualKey;

    const RECT monitorRect = GetPrimaryMonitorRect();
    const int dialogWidth = 430;
    const int dialogHeight = 170;
    const int dialogX = monitorRect.left + (((monitorRect.right - monitorRect.left) - dialogWidth) / 2);
    const int dialogY = monitorRect.top + (((monitorRect.bottom - monitorRect.top) - dialogHeight) / 2);

    EnableWindow(state->owner, FALSE);

    state->window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        kTriggerCaptureWindowClassName,
        state->title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        dialogX,
        dialogY,
        dialogWidth,
        dialogHeight,
        state->owner,
        nullptr,
        g_instance,
        state);

    if (!state->window)
    {
        EnableWindow(state->owner, TRUE);
        return false;
    }

    ShowWindow(state->window, SW_SHOWNORMAL);
    UpdateWindow(state->window);

    MSG message = {};
    while (!state->done && GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (message.message == WM_QUIT)
        {
            PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }

        if (!IsDialogMessageW(state->window, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (IsWindow(state->owner))
    {
        EnableWindow(state->owner, TRUE);
        SetForegroundWindow(state->owner);
    }

    return state->accepted;
}

void PromptForTriggerButton()
{
    TriggerCaptureDialogState state = {};
    state.title = L"Rebind Trigger Button";
    state.targetName = L"trigger";
    state.currentLabel = GetTriggerButtonLabel(g_config.triggerVirtualKey);
    state.currentVirtualKey = g_config.triggerVirtualKey;

    if (ShowTriggerCaptureDialog(&state))
    {
        ApplyTriggerVirtualKey(state.capturedVirtualKey);
    }
}

void PromptForZoomModifierButton()
{
    TriggerCaptureDialogState state = {};
    state.title = L"Rebind Zoom Modifier";
    state.targetName = L"zoom modifier";
    state.currentLabel = GetTriggerButtonLabel(g_config.zoomModifierVirtualKey);
    state.currentVirtualKey = g_config.zoomModifierVirtualKey;

    if (ShowTriggerCaptureDialog(&state))
    {
        ApplyZoomModifierVirtualKey(state.capturedVirtualKey);
    }
}

LRESULT CALLBACK PromptWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<PromptDialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message)
    {
    case WM_NCCREATE:
    {
        const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
        return TRUE;
    }

    case WM_CREATE:
    {
        state = reinterpret_cast<PromptDialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state == nullptr)
        {
            return -1;
        }

        state->window = window;

        const int labelX = 16;
        const int editX = 16;
        const int controlWidth = 292;
        const int firstLabelY = 16;
        const int firstEditY = 38;
        const int secondLabelY = 76;
        const int secondEditY = 98;
        const int buttonY = state->hasSecondField ? 142 : 102;

        HWND firstLabel = CreateWindowExW(
            0,
            L"STATIC",
            state->firstLabel.c_str(),
            WS_CHILD | WS_VISIBLE,
            labelX,
            firstLabelY,
            controlWidth,
            18,
            window,
            reinterpret_cast<HMENU>(kPromptControlFirstLabel),
            g_instance,
            nullptr);
        ApplyDialogFont(firstLabel);

        state->firstEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            state->firstValue.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            editX,
            firstEditY,
            controlWidth,
            24,
            window,
            reinterpret_cast<HMENU>(kPromptControlFirstEdit),
            g_instance,
            nullptr);
        ApplyDialogFont(state->firstEdit);

        if (state->hasSecondField)
        {
            HWND secondLabel = CreateWindowExW(
                0,
                L"STATIC",
                state->secondLabel.c_str(),
                WS_CHILD | WS_VISIBLE,
                labelX,
                secondLabelY,
                controlWidth,
                18,
                window,
                reinterpret_cast<HMENU>(kPromptControlSecondLabel),
                g_instance,
                nullptr);
            ApplyDialogFont(secondLabel);

            state->secondEdit = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                state->secondValue.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                editX,
                secondEditY,
                controlWidth,
                24,
                window,
                reinterpret_cast<HMENU>(kPromptControlSecondEdit),
                g_instance,
                nullptr);
            ApplyDialogFont(state->secondEdit);
        }

        HWND okButton = CreateWindowExW(
            0,
            L"BUTTON",
            L"OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            140,
            buttonY,
            80,
            28,
            window,
            reinterpret_cast<HMENU>(kPromptControlOk),
            g_instance,
            nullptr);
        ApplyDialogFont(okButton);

        HWND cancelButton = CreateWindowExW(
            0,
            L"BUTTON",
            L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            228,
            buttonY,
            80,
            28,
            window,
            reinterpret_cast<HMENU>(kPromptControlCancel),
            g_instance,
            nullptr);
        ApplyDialogFont(cancelButton);

        SetFocus(state->firstEdit);
        SendMessageW(state->firstEdit, EM_SETSEL, 0, -1);
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            DestroyWindow(window);
            return 0;
        }
        if (wParam == VK_RETURN)
        {
            PostMessageW(window, WM_COMMAND, MAKEWPARAM(kPromptControlOk, BN_CLICKED), 0);
            return 0;
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == kPromptControlOk)
        {
            if (state == nullptr)
            {
                return 0;
            }

            wchar_t firstBuffer[64] = {};
            GetWindowTextW(state->firstEdit, firstBuffer, static_cast<int>(std::size(firstBuffer)));
            state->firstValue = firstBuffer;

            if (state->hasSecondField && state->secondEdit)
            {
                wchar_t secondBuffer[64] = {};
                GetWindowTextW(state->secondEdit, secondBuffer, static_cast<int>(std::size(secondBuffer)));
                state->secondValue = secondBuffer;
            }

            state->accepted = true;
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(wParam) == kPromptControlCancel)
        {
            DestroyWindow(window);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        if (state != nullptr)
        {
            state->done = true;
        }
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK TriggerCaptureWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<TriggerCaptureDialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message)
    {
    case WM_NCCREATE:
    {
        const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
        return TRUE;
    }

    case WM_CREATE:
    {
        state = reinterpret_cast<TriggerCaptureDialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state == nullptr)
        {
            return -1;
        }

        state->window = window;

        std::wstring instruction =
            L"Current " + state->targetName + L": " + state->currentLabel +
            L"\nPress any keyboard key or mouse button.";

        HWND instructionLabel = CreateWindowExW(
            0,
            L"STATIC",
            instruction.c_str(),
            WS_CHILD | WS_VISIBLE,
            16,
            16,
            390,
            42,
            window,
            reinterpret_cast<HMENU>(kCaptureControlInstruction),
            g_instance,
            nullptr);
        ApplyDialogFont(instructionLabel);

        state->statusLabel = CreateWindowExW(
            0,
            L"STATIC",
            (L"Release any held " + state->targetName + L", then press the new " + state->targetName + L".").c_str(),
            WS_CHILD | WS_VISIBLE,
            16,
            70,
            390,
            20,
            window,
            reinterpret_cast<HMENU>(kCaptureControlStatus),
            g_instance,
            nullptr);
        ApplyDialogFont(state->statusLabel);

        HWND cancelButton = CreateWindowExW(
            0,
            L"BUTTON",
            L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            324,
            104,
            80,
            28,
            window,
            reinterpret_cast<HMENU>(kCaptureControlCancel),
            g_instance,
            nullptr);
        ApplyDialogFont(cancelButton);

        SetTimer(window, kTriggerCaptureTimerId, 20, nullptr);
        return 0;
    }

    case WM_TIMER:
        if (wParam == kTriggerCaptureTimerId && state != nullptr)
        {
            const int pressedVirtualKey = GetPressedTriggerVirtualKey();
            if (state->waitingForRelease)
            {
                if (pressedVirtualKey == 0)
                {
                    state->waitingForRelease = false;
                    if (state->statusLabel)
                    {
                        const std::wstring waitingText = L"Waiting for the new " + state->targetName + L"...";
                        SetWindowTextW(state->statusLabel, waitingText.c_str());
                    }
                }
                return 0;
            }

            if (pressedVirtualKey != 0)
            {
                state->capturedVirtualKey = pressedVirtualKey;
                state->accepted = true;
                DestroyWindow(window);
                return 0;
            }
        }
        break;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            DestroyWindow(window);
            return 0;
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == kCaptureControlCancel)
        {
            DestroyWindow(window);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        KillTimer(window, kTriggerCaptureTimerId);
        if (state != nullptr)
        {
            state->done = true;
        }
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

HICON CreateTrayIcon()
{
    constexpr int iconSize = 32;

    HDC screenDc = GetDC(nullptr);
    if (!screenDc)
    {
        return nullptr;
    }

    HDC memoryDc = CreateCompatibleDC(screenDc);
    HBITMAP colorBitmap = CreateCompatibleBitmap(screenDc, iconSize, iconSize);
    HBITMAP maskBitmap = CreateBitmap(iconSize, iconSize, 1, 1, nullptr);
    if (!memoryDc || !colorBitmap || !maskBitmap)
    {
        if (maskBitmap)
        {
            DeleteObject(maskBitmap);
        }
        if (colorBitmap)
        {
            DeleteObject(colorBitmap);
        }
        if (memoryDc)
        {
            DeleteDC(memoryDc);
        }
        ReleaseDC(nullptr, screenDc);
        return nullptr;
    }

    HGDIOBJ oldBitmap = SelectObject(memoryDc, colorBitmap);

    HBRUSH backgroundBrush = CreateSolidBrush(RGB(18, 24, 33));
    RECT iconRect = {0, 0, iconSize, iconSize};
    FillRect(memoryDc, &iconRect, backgroundBrush);
    DeleteObject(backgroundBrush);

    HPEN rimPen = CreatePen(PS_SOLID, 2, RGB(243, 247, 255));
    HBRUSH lensBrush = CreateSolidBrush(RGB(51, 144, 255));
    HGDIOBJ oldPen = SelectObject(memoryDc, rimPen);
    HGDIOBJ oldBrush = SelectObject(memoryDc, lensBrush);
    Ellipse(memoryDc, 5, 4, 23, 22);

    HPEN handlePen = CreatePen(PS_SOLID, 4, RGB(243, 247, 255));
    SelectObject(memoryDc, handlePen);
    MoveToEx(memoryDc, 20, 20, nullptr);
    LineTo(memoryDc, 28, 28);

    HPEN plusPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    SelectObject(memoryDc, plusPen);
    MoveToEx(memoryDc, 14, 9, nullptr);
    LineTo(memoryDc, 14, 17);
    MoveToEx(memoryDc, 10, 13, nullptr);
    LineTo(memoryDc, 18, 13);

    SelectObject(memoryDc, oldBrush);
    SelectObject(memoryDc, oldPen);
    DeleteObject(plusPen);
    DeleteObject(handlePen);
    DeleteObject(lensBrush);
    DeleteObject(rimPen);
    SelectObject(memoryDc, oldBitmap);

    HDC maskDc = CreateCompatibleDC(screenDc);
    HGDIOBJ oldMaskBitmap = SelectObject(maskDc, maskBitmap);
    HBRUSH opaqueBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(maskDc, &iconRect, opaqueBrush);
    DeleteObject(opaqueBrush);
    SelectObject(maskDc, oldMaskBitmap);
    DeleteDC(maskDc);

    ICONINFO iconInfo = {};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = colorBitmap;
    iconInfo.hbmMask = maskBitmap;
    HICON icon = CreateIconIndirect(&iconInfo);

    DeleteObject(maskBitmap);
    DeleteObject(colorBitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);

    return icon;
}

void UpdateTrayIcon()
{
    if (!g_hostWindow)
    {
        return;
    }

    g_trayIconData.cbSize = sizeof(g_trayIconData);
    g_trayIconData.hWnd = g_hostWindow;
    g_trayIconData.uID = kTrayIconId;
    g_trayIconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_trayIconData.uCallbackMessage = kTrayIconMessage;
    if (!g_trayIcon)
    {
        g_trayIcon = CreateTrayIcon();
    }
    g_trayIconData.hIcon = g_trayIcon ? g_trayIcon : LoadIconW(nullptr, IDI_APPLICATION);

    const std::wstring tip = BuildTrayToolTip();
    wcsncpy_s(g_trayIconData.szTip, tip.c_str(), _TRUNCATE);

    if (!g_trayIconAdded)
    {
        g_trayIconAdded = Shell_NotifyIconW(NIM_ADD, &g_trayIconData) == TRUE;
    }
    else
    {
        Shell_NotifyIconW(NIM_MODIFY, &g_trayIconData);
    }
}

void RemoveTrayIcon()
{
    if (!g_trayIconAdded)
    {
        return;
    }

    Shell_NotifyIconW(NIM_DELETE, &g_trayIconData);
    g_trayIconAdded = false;
}

void StartMagnifierUpdates()
{
    if (!g_hostWindow)
    {
        return;
    }

    KillTimer(g_hostWindow, kUpdateTimerId);
    EnableHighResolutionTimer();
    SetTimer(g_hostWindow, kUpdateTimerId, GetUpdateIntervalMs(g_config.targetFps), nullptr);
}

void StopMagnifierUpdates()
{
    if (g_hostWindow)
    {
        KillTimer(g_hostWindow, kUpdateTimerId);
    }
    DisableHighResolutionTimer();
}

void UpdateCenterDotWindow()
{
    if (!g_centerDotWindow)
    {
        return;
    }

    const int dotSize = SanitizeCenterDotSize(g_config.centerDotSize);
    const RECT monitorRect = GetPrimaryMonitorRect();
    const int monitorWidth = monitorRect.right - monitorRect.left;
    const int monitorHeight = monitorRect.bottom - monitorRect.top;
    const int windowX = monitorRect.left + ((monitorWidth - dotSize) / 2);
    const int windowY = monitorRect.top + ((monitorHeight - dotSize) / 2);

    SetWindowPos(
        g_centerDotWindow,
        HWND_TOPMOST,
        windowX,
        windowY,
        dotSize,
        dotSize,
        SWP_NOACTIVATE);

    InvalidateRect(g_centerDotWindow, nullptr, TRUE);
    ShowWindow(g_centerDotWindow, g_config.centerDotEnabled ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void ApplyWindowMetrics()
{
    if (!g_hostWindow)
    {
        return;
    }

    CenterHostWindow();
    UpdateCenterDotWindow();

    if (g_magnifierWindow)
    {
        MoveWindow(g_magnifierWindow, 0, 0, g_config.windowWidth, g_config.windowHeight, TRUE);
        ApplyMagnifierTransform();
    }

    ApplyAntiMirroring();

    if (g_isVisible)
    {
        UpdateMagnifierSource();
    }
}

void SetMagnifierVisible(bool visible)
{
    if (!g_hostWindow || g_isVisible == visible)
    {
        return;
    }

    if (visible)
    {
        ApplyWindowMetrics();
        UpdateMagnifierSource();
        ShowWindow(g_hostWindow, SW_SHOWNOACTIVATE);
        StartMagnifierUpdates();
    }
    else
    {
        StopMagnifierUpdates();
        ShowWindow(g_hostWindow, SW_HIDE);
    }

    g_isVisible = visible;
    UpdateTrayIcon();
}

void ApplyInputMode(InputMode mode)
{
    g_config.inputMode = mode;
    g_inputModeValue.store(static_cast<int>(g_config.inputMode), std::memory_order_relaxed);
    SaveConfig();
    UpdateTrayIcon();
}

void ApplyTriggerVirtualKey(int virtualKey)
{
    g_config.triggerVirtualKey = SanitizeTriggerVirtualKey(virtualKey);
    g_triggerVirtualKeyValue.store(g_config.triggerVirtualKey, std::memory_order_relaxed);
    SaveConfig();
    UpdateTrayIcon();
}

void ApplyZoomModifierVirtualKey(int virtualKey)
{
    g_config.zoomModifierVirtualKey = SanitizeTriggerVirtualKey(virtualKey);
    g_zoomModifierVirtualKeyValue.store(g_config.zoomModifierVirtualKey, std::memory_order_relaxed);
    SaveConfig();
    UpdateTrayIcon();
}

void ApplyCenterDotEnabled(bool enabled)
{
    g_config.centerDotEnabled = enabled;
    SaveConfig();
    UpdateCenterDotWindow();
    UpdateTrayIcon();
}

void ApplyTargetFps(int fps)
{
    g_config.targetFps = SanitizeTargetFps(fps);
    SaveConfig();
    if (g_isVisible)
    {
        StartMagnifierUpdates();
    }
    UpdateTrayIcon();
}

void ApplyZoomFactor(double zoomFactor)
{
    g_config.zoomFactor = SanitizeZoomFactor(zoomFactor);
    SaveConfig();
    ApplyWindowMetrics();
    if (g_isVisible)
    {
        UpdateMagnifierSource();
    }
    UpdateTrayIcon();
}

void ApplyWindowSize(int width, int height)
{
    g_config.windowWidth = SanitizeWindowDimension(width, kDefaultWindowWidth);
    g_config.windowHeight = SanitizeWindowDimension(height, kDefaultWindowHeight);
    SaveConfig();
    ApplyWindowMetrics();
    UpdateTrayIcon();
}

void ReloadConfigFromDisk()
{
    LoadConfig();
    ApplyWindowMetrics();
    if (g_isVisible)
    {
        StartMagnifierUpdates();
    }
    UpdateTrayIcon();
}

void ShowTrayMenu()
{
    HMENU rootMenu = CreatePopupMenu();
    HMENU inputModeMenu = CreatePopupMenu();
    HMENU triggerButtonMenu = CreatePopupMenu();
    HMENU zoomModifierMenu = CreatePopupMenu();
    HMENU refreshRateMenu = CreatePopupMenu();
    HMENU zoomMenu = CreatePopupMenu();
    HMENU sizeMenu = CreatePopupMenu();

    if (!rootMenu || !inputModeMenu || !triggerButtonMenu || !zoomModifierMenu || !refreshRateMenu || !zoomMenu || !sizeMenu)
    {
        if (sizeMenu)
        {
            DestroyMenu(sizeMenu);
        }
        if (zoomMenu)
        {
            DestroyMenu(zoomMenu);
        }
        if (refreshRateMenu)
        {
            DestroyMenu(refreshRateMenu);
        }
        if (triggerButtonMenu)
        {
            DestroyMenu(triggerButtonMenu);
        }
        if (zoomModifierMenu)
        {
            DestroyMenu(zoomModifierMenu);
        }
        if (inputModeMenu)
        {
            DestroyMenu(inputModeMenu);
        }
        if (rootMenu)
        {
            DestroyMenu(rootMenu);
        }
        return;
    }

    AppendMenuW(
        inputModeMenu,
        MF_STRING | (g_config.inputMode == InputMode::Toggle ? MF_CHECKED : 0),
        kTrayCommandModeToggle,
        L"Toggle");
    AppendMenuW(
        inputModeMenu,
        MF_STRING | (g_config.inputMode == InputMode::Hold ? MF_CHECKED : 0),
        kTrayCommandModeHold,
        L"Hold To Show");

    AppendMenuW(
        triggerButtonMenu,
        MF_STRING | (g_config.triggerVirtualKey == VK_XBUTTON1 ? MF_CHECKED : 0),
        kTrayCommandTriggerMouseBack,
        L"Mouse Back");
    AppendMenuW(
        triggerButtonMenu,
        MF_STRING | (g_config.triggerVirtualKey == VK_XBUTTON2 ? MF_CHECKED : 0),
        kTrayCommandTriggerMouseForward,
        L"Mouse Forward");
    AppendMenuW(
        triggerButtonMenu,
        MF_STRING | (g_config.triggerVirtualKey == VK_MBUTTON ? MF_CHECKED : 0),
        kTrayCommandTriggerMiddleMouse,
        L"Middle Mouse");
    AppendMenuW(triggerButtonMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(
        triggerButtonMenu,
        MF_STRING | (g_config.triggerVirtualKey == VK_F8 ? MF_CHECKED : 0),
        kTrayCommandTriggerF8,
        L"F8");
    AppendMenuW(
        triggerButtonMenu,
        MF_STRING | (g_config.triggerVirtualKey == VK_F9 ? MF_CHECKED : 0),
        kTrayCommandTriggerF9,
        L"F9");
    AppendMenuW(
        triggerButtonMenu,
        MF_STRING | (g_config.triggerVirtualKey == VK_F10 ? MF_CHECKED : 0),
        kTrayCommandTriggerF10,
        L"F10");
    AppendMenuW(
        triggerButtonMenu,
        MF_STRING | (g_config.triggerVirtualKey == VK_F11 ? MF_CHECKED : 0),
        kTrayCommandTriggerF11,
        L"F11");
    AppendMenuW(
        triggerButtonMenu,
        MF_STRING | (g_config.triggerVirtualKey == VK_F12 ? MF_CHECKED : 0),
        kTrayCommandTriggerF12,
        L"F12");
    AppendMenuW(triggerButtonMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(triggerButtonMenu, MF_STRING, kTrayCommandTriggerRebind, L"Press a Key/Button...");

    AppendMenuW(
        zoomModifierMenu,
        MF_STRING | (g_config.zoomModifierVirtualKey == VK_CONTROL ? MF_CHECKED : 0),
        kTrayCommandZoomModifierCtrl,
        L"Ctrl");
    AppendMenuW(
        zoomModifierMenu,
        MF_STRING | (g_config.zoomModifierVirtualKey == VK_MENU ? MF_CHECKED : 0),
        kTrayCommandZoomModifierAlt,
        L"Alt");
    AppendMenuW(
        zoomModifierMenu,
        MF_STRING | (g_config.zoomModifierVirtualKey == VK_SHIFT ? MF_CHECKED : 0),
        kTrayCommandZoomModifierShift,
        L"Shift");
    AppendMenuW(zoomModifierMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(
        zoomModifierMenu,
        MF_STRING | (g_config.zoomModifierVirtualKey == VK_MBUTTON ? MF_CHECKED : 0),
        kTrayCommandZoomModifierMiddleMouse,
        L"Middle Mouse");
    AppendMenuW(
        zoomModifierMenu,
        MF_STRING | (g_config.zoomModifierVirtualKey == VK_XBUTTON2 ? MF_CHECKED : 0),
        kTrayCommandZoomModifierMouseForward,
        L"Mouse Forward");
    AppendMenuW(zoomModifierMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(zoomModifierMenu, MF_STRING, kTrayCommandZoomModifierRebind, L"Press a Key/Button...");

    AppendMenuW(
        refreshRateMenu,
        MF_STRING | (g_config.targetFps == 60 ? MF_CHECKED : 0),
        kTrayCommandFps60,
        L"60 Hz");
    AppendMenuW(
        refreshRateMenu,
        MF_STRING | (g_config.targetFps == 120 ? MF_CHECKED : 0),
        kTrayCommandFps120,
        L"120 Hz");
    AppendMenuW(
        refreshRateMenu,
        MF_STRING | (g_config.targetFps == 144 ? MF_CHECKED : 0),
        kTrayCommandFps144,
        L"144 Hz");
    AppendMenuW(
        refreshRateMenu,
        MF_STRING | (g_config.targetFps == 180 ? MF_CHECKED : 0),
        kTrayCommandFps180,
        L"180 Hz");
    AppendMenuW(
        refreshRateMenu,
        MF_STRING | (g_config.targetFps == 240 ? MF_CHECKED : 0),
        kTrayCommandFps240,
        L"240 Hz");

    AppendMenuW(
        zoomMenu,
        MF_STRING | (IsNearlyEqual(g_config.zoomFactor, 1.5) ? MF_CHECKED : 0),
        kTrayCommandZoom150,
        L"1.5x");
    AppendMenuW(
        zoomMenu,
        MF_STRING | (IsNearlyEqual(g_config.zoomFactor, 2.0) ? MF_CHECKED : 0),
        kTrayCommandZoom200,
        L"2.0x");
    AppendMenuW(
        zoomMenu,
        MF_STRING | (IsNearlyEqual(g_config.zoomFactor, 3.0) ? MF_CHECKED : 0),
        kTrayCommandZoom300,
        L"3.0x");
    AppendMenuW(
        zoomMenu,
        MF_STRING | (IsNearlyEqual(g_config.zoomFactor, 4.0) ? MF_CHECKED : 0),
        kTrayCommandZoom400,
        L"4.0x");
    AppendMenuW(zoomMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(zoomMenu, MF_STRING, kTrayCommandCustomZoom, L"Custom...");

    AppendMenuW(
        sizeMenu,
        MF_STRING | ((g_config.windowWidth == 400 && g_config.windowHeight == 400) ? MF_CHECKED : 0),
        kTrayCommandSize400Square,
        L"400x400");
    AppendMenuW(
        sizeMenu,
        MF_STRING | ((g_config.windowWidth == 500 && g_config.windowHeight == 500) ? MF_CHECKED : 0),
        kTrayCommandSize500Square,
        L"500x500");
    AppendMenuW(
        sizeMenu,
        MF_STRING | ((g_config.windowWidth == 600 && g_config.windowHeight == 600) ? MF_CHECKED : 0),
        kTrayCommandSize600Square,
        L"600x600");
    AppendMenuW(
        sizeMenu,
        MF_STRING | ((g_config.windowWidth == 800 && g_config.windowHeight == 600) ? MF_CHECKED : 0),
        kTrayCommandSize800x600,
        L"800x600");
    AppendMenuW(sizeMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sizeMenu, MF_STRING, kTrayCommandCustomSize, L"Custom...");

    AppendMenuW(
        rootMenu,
        MF_STRING,
        kTrayCommandToggleVisibility,
        g_isVisible ? L"Hide Magnifier" : L"Show Magnifier");
    AppendMenuW(
        rootMenu,
        MF_STRING | (g_config.centerDotEnabled ? MF_CHECKED : 0),
        kTrayCommandToggleCenterDot,
        L"Center Dot");
    const std::wstring triggerMenuTitle = L"Trigger Button: " + GetTriggerButtonLabel(g_config.triggerVirtualKey);
    const std::wstring zoomModifierMenuTitle =
        L"Wheel Zoom Modifier: " + GetTriggerButtonLabel(g_config.zoomModifierVirtualKey);
    AppendMenuW(rootMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(rootMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(inputModeMenu), L"Trigger Mode");
    AppendMenuW(rootMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(triggerButtonMenu), triggerMenuTitle.c_str());
    AppendMenuW(rootMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(zoomModifierMenu), zoomModifierMenuTitle.c_str());
    AppendMenuW(rootMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(refreshRateMenu), L"Refresh Rate");
    AppendMenuW(rootMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(zoomMenu), L"Zoom");
    AppendMenuW(rootMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(sizeMenu), L"Window Size");
    AppendMenuW(rootMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(rootMenu, MF_STRING, kTrayCommandReloadConfig, L"Reload Config File");
    AppendMenuW(rootMenu, MF_STRING, kTrayCommandQuit, L"Quit");

    POINT cursor = {};
    GetCursorPos(&cursor);
    SetForegroundWindow(g_hostWindow);
    const UINT command = TrackPopupMenu(
        rootMenu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        cursor.x,
        cursor.y,
        0,
        g_hostWindow,
        nullptr);

    DestroyMenu(rootMenu);

    if (command != 0)
    {
        PostMessageW(g_hostWindow, WM_COMMAND, MAKEWPARAM(command, 0), 0);
    }

    PostMessageW(g_hostWindow, WM_NULL, 0, 0);
}

void InputMonitorLoop()
{
    bool wasDown = false;

    while (!g_inputThreadStopRequested.load(std::memory_order_relaxed))
    {
        const int triggerVirtualKey = g_triggerVirtualKeyValue.load(std::memory_order_relaxed);

        if (g_hostWindow && !IsWindowEnabled(g_hostWindow))
        {
            wasDown = IsVirtualKeyDown(triggerVirtualKey);
            Sleep(4);
            continue;
        }

        const bool isDown = IsVirtualKeyDown(triggerVirtualKey);
        const InputMode inputMode = static_cast<InputMode>(g_inputModeValue.load(std::memory_order_relaxed));

        if (isDown && !wasDown)
        {
            if (g_hostWindow)
            {
                if (inputMode == InputMode::Hold)
                {
                    PostMessageW(g_hostWindow, kShowMessage, 0, 0);
                }
                else
                {
                    PostMessageW(g_hostWindow, kToggleMessage, 0, 0);
                }
            }
        }
        else if (!isDown && wasDown)
        {
            if (g_hostWindow && inputMode == InputMode::Hold)
            {
                PostMessageW(g_hostWindow, kHideMessage, 0, 0);
            }
        }

        wasDown = isDown;
        Sleep(4);
    }
}

bool StartInputMonitor()
{
    if (g_inputThread.joinable())
    {
        return true;
    }

    g_inputThreadStopRequested.store(false, std::memory_order_relaxed);

    try
    {
        g_inputThread = std::thread(InputMonitorLoop);
    }
    catch (...)
    {
        return false;
    }

    return true;
}

void StopInputMonitor()
{
    g_inputThreadStopRequested.store(true, std::memory_order_relaxed);
    if (g_inputThread.joinable())
    {
        g_inputThread.join();
    }
}

LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && (wParam == WM_MOUSEWHEEL || wParam == WM_MOUSEHWHEEL))
    {
        const int zoomModifierVirtualKey = g_zoomModifierVirtualKeyValue.load(std::memory_order_relaxed);
        if (g_hostWindow && IsWindowEnabled(g_hostWindow) && IsVirtualKeyDown(zoomModifierVirtualKey))
        {
            const auto* mouseHook = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
            const int wheelDelta = GET_WHEEL_DELTA_WPARAM(mouseHook->mouseData);
            if (wheelDelta != 0)
            {
                PostMessageW(g_hostWindow, kZoomWheelMessage, 0, static_cast<LPARAM>(wheelDelta));
                return 1;
            }
        }
    }

    return CallNextHookEx(g_mouseHook, code, wParam, lParam);
}

bool StartMouseWheelHook()
{
    if (g_mouseHook)
    {
        return true;
    }

    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, g_instance, 0);
    return g_mouseHook != nullptr;
}

void StopMouseWheelHook()
{
    if (g_mouseHook)
    {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }
}

LRESULT CALLBACK CenterDotWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    (void)lParam;

    switch (message)
    {
    case WM_CREATE:
        SetLayeredWindowAttributes(window, RGB(0, 0, 0), 255, LWA_COLORKEY | LWA_ALPHA);
        return 0;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_NCHITTEST:
        return HTTRANSPARENT;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT paint = {};
        HDC dc = BeginPaint(window, &paint);
        RECT clientRect = {};
        GetClientRect(window, &clientRect);

        HBRUSH transparentBrush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(dc, &clientRect, transparentBrush);
        DeleteObject(transparentBrush);

        HBRUSH dotBrush = CreateSolidBrush(RGB(0, 255, 0));
        HPEN dotPen = CreatePen(PS_SOLID, 1, RGB(0, 255, 0));
        HGDIOBJ oldBrush = SelectObject(dc, dotBrush);
        HGDIOBJ oldPen = SelectObject(dc, dotPen);
        Ellipse(dc, clientRect.left, clientRect.top, clientRect.right, clientRect.bottom);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(dotPen);
        DeleteObject(dotBrush);

        EndPaint(window, &paint);
        return 0;
    }
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK HostWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == g_taskbarCreatedMessage)
    {
        g_trayIconAdded = false;
        UpdateTrayIcon();
        return 0;
    }

    switch (message)
    {
    case WM_CREATE:
    {
        g_magnifierWindow = CreateWindowExW(
            WS_EX_TRANSPARENT,
            WC_MAGNIFIER,
            L"",
            WS_CHILD | WS_VISIBLE,
            0,
            0,
            g_config.windowWidth,
            g_config.windowHeight,
            window,
            nullptr,
            g_instance,
            nullptr);

        if (!g_magnifierWindow)
        {
            ShowErrorBox(L"Center Magnifier Native", GetLastErrorMessage(L"Failed to create the magnifier window."));
            return -1;
        }

        if (!ApplyMagnifierTransform())
        {
            ShowErrorBox(L"Center Magnifier Native", GetLastErrorMessage(L"Failed to set the magnifier transform."));
            return -1;
        }

        SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA);
        ApplyWindowMetrics();
        UpdateTrayIcon();
        ShowWindow(window, SW_HIDE);
        return 0;
    }

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_NCHITTEST:
        return HTTRANSPARENT;

    case WM_SIZE:
        if (g_magnifierWindow)
        {
            MoveWindow(g_magnifierWindow, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case kTrayCommandToggleVisibility:
            SetMagnifierVisible(!g_isVisible);
            return 0;

        case kTrayCommandToggleCenterDot:
            ApplyCenterDotEnabled(!g_config.centerDotEnabled);
            return 0;

        case kTrayCommandModeToggle:
            ApplyInputMode(InputMode::Toggle);
            return 0;

        case kTrayCommandModeHold:
            ApplyInputMode(InputMode::Hold);
            return 0;

        case kTrayCommandTriggerMouseBack:
            ApplyTriggerVirtualKey(VK_XBUTTON1);
            return 0;

        case kTrayCommandTriggerMouseForward:
            ApplyTriggerVirtualKey(VK_XBUTTON2);
            return 0;

        case kTrayCommandTriggerMiddleMouse:
            ApplyTriggerVirtualKey(VK_MBUTTON);
            return 0;

        case kTrayCommandTriggerF8:
            ApplyTriggerVirtualKey(VK_F8);
            return 0;

        case kTrayCommandTriggerF9:
            ApplyTriggerVirtualKey(VK_F9);
            return 0;

        case kTrayCommandTriggerF10:
            ApplyTriggerVirtualKey(VK_F10);
            return 0;

        case kTrayCommandTriggerF11:
            ApplyTriggerVirtualKey(VK_F11);
            return 0;

        case kTrayCommandTriggerF12:
            ApplyTriggerVirtualKey(VK_F12);
            return 0;

        case kTrayCommandTriggerRebind:
            PromptForTriggerButton();
            return 0;

        case kTrayCommandZoomModifierCtrl:
            ApplyZoomModifierVirtualKey(VK_CONTROL);
            return 0;

        case kTrayCommandZoomModifierAlt:
            ApplyZoomModifierVirtualKey(VK_MENU);
            return 0;

        case kTrayCommandZoomModifierShift:
            ApplyZoomModifierVirtualKey(VK_SHIFT);
            return 0;

        case kTrayCommandZoomModifierMiddleMouse:
            ApplyZoomModifierVirtualKey(VK_MBUTTON);
            return 0;

        case kTrayCommandZoomModifierMouseForward:
            ApplyZoomModifierVirtualKey(VK_XBUTTON2);
            return 0;

        case kTrayCommandZoomModifierRebind:
            PromptForZoomModifierButton();
            return 0;

        case kTrayCommandFps60:
            ApplyTargetFps(60);
            return 0;

        case kTrayCommandFps120:
            ApplyTargetFps(120);
            return 0;

        case kTrayCommandFps144:
            ApplyTargetFps(144);
            return 0;

        case kTrayCommandFps180:
            ApplyTargetFps(180);
            return 0;

        case kTrayCommandFps240:
            ApplyTargetFps(240);
            return 0;

        case kTrayCommandZoom150:
            ApplyZoomFactor(1.5);
            return 0;

        case kTrayCommandZoom200:
            ApplyZoomFactor(2.0);
            return 0;

        case kTrayCommandZoom300:
            ApplyZoomFactor(3.0);
            return 0;

        case kTrayCommandZoom400:
            ApplyZoomFactor(4.0);
            return 0;

        case kTrayCommandCustomZoom:
            PromptForCustomZoom();
            return 0;

        case kTrayCommandSize400Square:
            ApplyWindowSize(400, 400);
            return 0;

        case kTrayCommandSize500Square:
            ApplyWindowSize(500, 500);
            return 0;

        case kTrayCommandSize600Square:
            ApplyWindowSize(600, 600);
            return 0;

        case kTrayCommandSize800x600:
            ApplyWindowSize(800, 600);
            return 0;

        case kTrayCommandCustomSize:
            PromptForCustomWindowSize();
            return 0;

        case kTrayCommandReloadConfig:
            ReloadConfigFromDisk();
            return 0;

        case kTrayCommandQuit:
            DestroyWindow(window);
            return 0;
        }
        break;

    case WM_TIMER:
        if (wParam == kUpdateTimerId)
        {
            UpdateMagnifierSource();
            return 0;
        }
        break;

    case WM_DISPLAYCHANGE:
        ApplyWindowMetrics();
        if (g_isVisible)
        {
            UpdateMagnifierSource();
        }
        return 0;

    case kToggleMessage:
        SetMagnifierVisible(!g_isVisible);
        return 0;

    case kShowMessage:
        SetMagnifierVisible(true);
        return 0;

    case kHideMessage:
        SetMagnifierVisible(false);
        return 0;

    case kZoomWheelMessage:
    {
        const int wheelDelta = static_cast<int>(lParam);
        const double zoomStep = 0.25 * (static_cast<double>(wheelDelta) / static_cast<double>(WHEEL_DELTA));
        ApplyZoomFactor(g_config.zoomFactor + zoomStep);
        return 0;
    }

    case kTrayIconMessage:
        if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK)
        {
            SetMagnifierVisible(!g_isVisible);
            return 0;
        }
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
        {
            ShowTrayMenu();
            return 0;
        }
        break;

    case WM_DESTROY:
        StopMagnifierUpdates();
        StopMouseWheelHook();
        RemoveTrayIcon();
        if (g_centerDotWindow)
        {
            DestroyWindow(g_centerDotWindow);
            g_centerDotWindow = nullptr;
        }
        if (g_trayIcon)
        {
            DestroyIcon(g_trayIcon);
            g_trayIcon = nullptr;
        }
        StopInputMonitor();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

bool RegisterHostWindowClass()
{
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = HostWindowProc;
    windowClass.hInstance = g_instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kHostWindowClassName;

    return RegisterClassExW(&windowClass) != 0;
}

bool RegisterPromptWindowClass()
{
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = PromptWindowProc;
    windowClass.hInstance = g_instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kPromptWindowClassName;

    return RegisterClassExW(&windowClass) != 0;
}

bool RegisterTriggerCaptureWindowClass()
{
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = TriggerCaptureWindowProc;
    windowClass.hInstance = g_instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kTriggerCaptureWindowClassName;

    return RegisterClassExW(&windowClass) != 0;
}

bool RegisterCenterDotWindowClass()
{
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = CenterDotWindowProc;
    windowClass.hInstance = g_instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kCenterDotWindowClassName;

    return RegisterClassExW(&windowClass) != 0;
}

bool CreateHostWindow()
{
    const RECT monitorRect = GetPrimaryMonitorRect();
    const int monitorWidth = monitorRect.right - monitorRect.left;
    const int monitorHeight = monitorRect.bottom - monitorRect.top;
    const int windowX = monitorRect.left + ((monitorWidth - g_config.windowWidth) / 2);
    const int windowY = monitorRect.top + ((monitorHeight - g_config.windowHeight) / 2);

    g_hostWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        kHostWindowClassName,
        kWindowTitle,
        WS_POPUP,
        windowX,
        windowY,
        g_config.windowWidth,
        g_config.windowHeight,
        nullptr,
        nullptr,
        g_instance,
        nullptr);

    return g_hostWindow != nullptr;
}

bool CreateCenterDotWindow()
{
    const RECT monitorRect = GetPrimaryMonitorRect();
    const int monitorWidth = monitorRect.right - monitorRect.left;
    const int monitorHeight = monitorRect.bottom - monitorRect.top;
    const int dotSize = SanitizeCenterDotSize(g_config.centerDotSize);
    const int windowX = monitorRect.left + ((monitorWidth - dotSize) / 2);
    const int windowY = monitorRect.top + ((monitorHeight - dotSize) / 2);

    g_centerDotWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        kCenterDotWindowClassName,
        L"Center Magnifier Native Center Dot",
        WS_POPUP,
        windowX,
        windowY,
        dotSize,
        dotSize,
        nullptr,
        nullptr,
        g_instance,
        nullptr);

    if (!g_centerDotWindow)
    {
        return false;
    }

    UpdateCenterDotWindow();
    return true;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    g_instance = instance;
    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

    HANDLE singleInstanceMutex = CreateMutexW(nullptr, TRUE, kSingleInstanceMutexName);
    if (!singleInstanceMutex)
    {
        ShowErrorBox(L"Center Magnifier Native", GetLastErrorMessage(L"Failed to create the single-instance guard."));
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND existingWindow = FindWindowW(kHostWindowClassName, kWindowTitle);
        if (existingWindow)
        {
            PostMessageW(existingWindow, kToggleMessage, 0, 0);
        }
        CloseHandle(singleInstanceMutex);
        return 0;
    }

    EnableBestDpiAwareness();
    LoadConfig();

    if (!MagInitialize())
    {
        ShowErrorBox(L"Center Magnifier Native", GetLastErrorMessage(L"MagInitialize failed."));
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    if (!RegisterHostWindowClass())
    {
        ShowErrorBox(L"Center Magnifier Native", GetLastErrorMessage(L"Failed to register the host window class."));
        MagUninitialize();
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    if (!RegisterPromptWindowClass())
    {
        ShowErrorBox(L"Center Magnifier Native", GetLastErrorMessage(L"Failed to register the prompt window class."));
        MagUninitialize();
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    if (!RegisterTriggerCaptureWindowClass())
    {
        ShowErrorBox(
            L"Center Magnifier Native",
            GetLastErrorMessage(L"Failed to register the trigger capture window class."));
        MagUninitialize();
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    if (!RegisterCenterDotWindowClass())
    {
        ShowErrorBox(L"Center Magnifier Native", GetLastErrorMessage(L"Failed to register the center dot window class."));
        MagUninitialize();
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    if (!CreateHostWindow())
    {
        ShowErrorBox(L"Center Magnifier Native", GetLastErrorMessage(L"Failed to create the host window."));
        MagUninitialize();
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    if (!CreateCenterDotWindow())
    {
        ShowErrorBox(L"Center Magnifier Native", GetLastErrorMessage(L"Failed to create the center dot window."));
        DestroyWindow(g_hostWindow);
        g_hostWindow = nullptr;
        MagUninitialize();
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    ApplyAntiMirroring();

    if (!StartMouseWheelHook())
    {
        ShowErrorBox(L"Center Magnifier Native", GetLastErrorMessage(L"Failed to start the wheel zoom hook."));
        DestroyWindow(g_hostWindow);
        g_hostWindow = nullptr;
        MagUninitialize();
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    if (!StartInputMonitor())
    {
        ShowErrorBox(L"Center Magnifier Native", L"Failed to start the back-button input monitor.");
        StopMouseWheelHook();
        DestroyWindow(g_hostWindow);
        g_hostWindow = nullptr;
        MagUninitialize();
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    MagUninitialize();
    CloseHandle(singleInstanceMutex);
    return static_cast<int>(message.wParam);
}
