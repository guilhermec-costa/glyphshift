#include "global_hotkey.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string_view>
#include <tuple>

namespace glyphshift::display::win32 {
namespace {
constexpr wchar_t daemon_class[] = L"GlyphShiftDaemon";
constexpr wchar_t mutex_name[] = L"Local\\GlyphShiftDaemon";
constexpr int hotkey_id = 1;

std::string lower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

// The virtual key of a key name, 0 when unknown.
UINT virtual_key(const std::string& name) {
    if (name.size() == 1 && std::isalnum(static_cast<unsigned char>(name[0])))
        return static_cast<UINT>(std::toupper(static_cast<unsigned char>(name[0])));
    if (name.size() >= 2 && name[0] == 'f' &&
        std::all_of(name.begin() + 1, name.end(), [](unsigned char c) {
            return std::isdigit(c);
        })) {
        const int number = std::stoi(name.substr(1));
        return number >= 1 && number <= 24 ? static_cast<UINT>(VK_F1 + number - 1) : 0U;
    }
    static constexpr std::pair<std::string_view, UINT> names[] = {{"insert", VK_INSERT},
        {"delete", VK_DELETE},
        {"home", VK_HOME},
        {"end", VK_END},
        {"prior", VK_PRIOR},
        {"page_up", VK_PRIOR},
        {"next", VK_NEXT},
        {"page_down", VK_NEXT},
        {"pause", VK_PAUSE},
        {"print", VK_SNAPSHOT},
        {"scroll_lock", VK_SCROLL},
        {"space", VK_SPACE},
        {"tab", VK_TAB},
        {"return", VK_RETURN},
        {"escape", VK_ESCAPE},
        {"left", VK_LEFT},
        {"right", VK_RIGHT},
        {"up", VK_UP},
        {"down", VK_DOWN}};
    for (const auto& [text, key] : names)
        if (name == text)
            return key;
    return 0;
}

// Modifiers and key of "ctrl+alt+g"; throws std::invalid_argument.
std::pair<UINT, UINT> parse(const std::string& hotkey) {
    UINT modifiers = 0, key = 0;
    std::string_view rest = hotkey;
    while (!rest.empty()) {
        const auto plus = rest.find('+');
        const auto part = lower(rest.substr(0, plus));
        rest = plus == std::string_view::npos ? std::string_view{} : rest.substr(plus + 1);
        if (part == "ctrl" || part == "control")
            modifiers |= MOD_CONTROL;
        else if (part == "shift")
            modifiers |= MOD_SHIFT;
        else if (part == "alt")
            modifiers |= MOD_ALT;
        else if (part == "super" || part == "win")
            modifiers |= MOD_WIN;
        else if (!key && rest.empty())
            key = virtual_key(part);
        else
            throw std::invalid_argument("unknown modifier in hotkey: " + part);
    }
    if (!key)
        throw std::invalid_argument("hotkey needs a known key: " + hotkey);
    return {modifiers, key};
}

std::string utf8(const std::wstring& text) {
    if (text.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
}

std::wstring wide(const std::string& text) {
    if (text.empty())
        return {};
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size);
    return result;
}

// The running daemon's hotkey, from its window's title (it may be starting).
std::string running_hotkey() {
    for (int attempt = 0; attempt < 40; ++attempt) {
        if (const HWND other = FindWindowW(daemon_class, nullptr)) {
            wchar_t title[128]{};
            GetWindowTextW(other, title, 128);
            return utf8(title);
        }
        Sleep(50);
    }
    return {};
}
}  // namespace

void GlobalHotkey::check(const std::string& hotkey) {
    parse(hotkey);
}

GlobalHotkey::GlobalHotkey(const std::string& hotkey) {
    std::tie(modifiers_, key_) = parse(hotkey);
    mutex_ = CreateMutexW(nullptr, FALSE, mutex_name);
    if (!mutex_)
        throw std::runtime_error("cannot create the daemon's session mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        other_ = running_hotkey();
        return;
    }
    WNDCLASSEXW window_class{sizeof window_class};
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = daemon_class;
    RegisterClassExW(&window_class);  // Fails harmlessly when already registered.
    // Never shown; a top-level window so a second launch can find it.
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW,
        daemon_class,
        wide(hotkey).c_str(),
        WS_POPUP,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        window_class.hInstance,
        nullptr);
    if (!window_) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
        throw std::runtime_error("cannot create the daemon's window");
    }
}

GlobalHotkey::~GlobalHotkey() {
    if (grabbed_)
        UnregisterHotKey(window_, hotkey_id);
    if (window_)
        DestroyWindow(window_);
    if (mutex_)
        CloseHandle(mutex_);
}

void GlobalHotkey::grab() {
    if (!RegisterHotKey(window_, hotkey_id, modifiers_ | MOD_NOREPEAT, key_))
        throw std::runtime_error("the hotkey is already registered by another application");
    grabbed_ = true;
}

bool GlobalHotkey::pressed(const MSG& message) {
    if (message.message != WM_HOTKEY || message.hwnd != window_ || message.wParam != hotkey_id)
        return false;
    const auto now = std::chrono::steady_clock::now();
    const bool fresh = now - last_ >= std::chrono::milliseconds(400);
    last_ = now;
    return fresh;
}

HWND focused_window() {
    const HWND foreground = GetForegroundWindow();
    if (!foreground)
        return nullptr;
    const HWND top = GetAncestor(foreground, GA_ROOT);
    if (!top || top == GetShellWindow())
        return nullptr;
    DWORD process = 0;
    GetWindowThreadProcessId(top, &process);
    wchar_t name[64]{};
    GetClassNameW(top, name, 64);
    const std::wstring_view class_name = name;
    if (process == GetCurrentProcessId() || class_name == L"Shell_TrayWnd" ||
        class_name == L"Progman" || class_name == L"WorkerW" || class_name == L"GlyphShiftOverlay")
        return nullptr;
    return top;
}

std::string window_title(HWND window) {
    wchar_t title[256]{};
    GetWindowTextW(window, title, 256);
    std::wstring text = title;
    if (text.empty())
        text = L"untitled window";
    if (text.size() > 40)
        text = text.substr(0, 39) + L"…";
    return utf8(text);
}
}  // namespace glyphshift::display::win32
