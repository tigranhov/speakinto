#include "keyboard_hook.h"
#include <thread>
#include <atomic>

namespace keyboard {

static HHOOK g_hook = nullptr;
static std::thread g_hookThread;
static std::atomic<bool> g_running{false};
static std::atomic<bool> g_ctrlHeld{false};
static std::atomic<bool> g_winHeld{false};
static DWORD g_hookThreadId = 0;
static HWND g_hwndMain = nullptr;

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode != HC_ACTION) {
        return CallNextHookEx(g_hook, nCode, wParam, lParam);
    }

    auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    DWORD vk = kb->vkCode;
    bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool isKeyUp   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);

    // Track Ctrl state
    if (vk == VK_LCONTROL || vk == VK_RCONTROL) {
        if (isKeyDown) g_ctrlHeld = true;
        if (isKeyUp) {
            g_ctrlHeld = false;
            // If Win was held when Ctrl releases, end combo
            if (g_winHeld) {
                g_winHeld = false;
                PostMessage(g_hwndMain, WM_COMBO_UP, 0, 0);
            }
        }
        return CallNextHookEx(g_hook, nCode, wParam, lParam);
    }

    // Track Win key — suppress when Ctrl is held
    if (vk == VK_LWIN || vk == VK_RWIN) {
        if (isKeyDown && g_ctrlHeld) {
            if (!g_winHeld) {
                g_winHeld = true;
                PostMessage(g_hwndMain, WM_COMBO_DOWN, 0, 0);
            }
            return 1; // Suppress Win key
        }
        if (isKeyUp && g_winHeld) {
            g_winHeld = false;
            PostMessage(g_hwndMain, WM_COMBO_UP, 0, 0);
            return 1; // Suppress Win key release (prevents Start menu)
        }
        return CallNextHookEx(g_hook, nCode, wParam, lParam);
    }

    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

static void hookThreadProc() {
    g_hookThreadId = GetCurrentThreadId();
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, nullptr, 0);
    if (!g_hook) return;

    MSG msg;
    while (g_running && GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
}

void start(HWND hwndMain) {
    if (g_running) return;
    g_hwndMain = hwndMain;
    g_running = true;
    g_ctrlHeld = false;
    g_winHeld = false;
    g_hookThread = std::thread(hookThreadProc);
}

void stop() {
    if (!g_running) return;
    g_running = false;
    if (g_hookThreadId != 0) {
        PostThreadMessage(g_hookThreadId, WM_QUIT, 0, 0);
    }
    if (g_hookThread.joinable()) {
        g_hookThread.join();
    }
    g_hookThreadId = 0;
}

}
