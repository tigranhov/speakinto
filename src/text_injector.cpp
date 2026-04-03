#include "text_injector.h"
#include <windows.h>
#include <string>

namespace injector {

static void sendCtrlV() {
    INPUT inputs[4] = {};

    // Press Ctrl
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_LCONTROL;

    // Press V
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 0x56; // VK_V

    // Release V
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 0x56;
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

    // Release Ctrl
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_LCONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(4, inputs, sizeof(INPUT));
}

void injectText(const std::string& text) {
    if (text.empty()) return;

    // Save current clipboard text
    std::string originalText;
    if (OpenClipboard(nullptr)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            wchar_t* pText = (wchar_t*)GlobalLock(hData);
            if (pText) {
                // Convert to narrow for storage (we only restore text)
                int len = WideCharToMultiByte(CP_UTF8, 0, pText, -1, nullptr, 0, nullptr, nullptr);
                originalText.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, pText, -1, originalText.data(), len, nullptr, nullptr);
                GlobalUnlock(hData);
            }
        }
        CloseClipboard();
    }

    // Set clipboard to transcription
    {
        int wideLen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wideLen * sizeof(wchar_t));
        wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, pMem, wideLen);
        GlobalUnlock(hMem);

        if (OpenClipboard(nullptr)) {
            EmptyClipboard();
            SetClipboardData(CF_UNICODETEXT, hMem);
            CloseClipboard();
        } else {
            GlobalFree(hMem);
            return;
        }
    }

    Sleep(50); // Let clipboard settle
    sendCtrlV();
    Sleep(500); // Wait for paste to complete

    // Restore original clipboard
    {
        int wideLen = MultiByteToWideChar(CP_UTF8, 0, originalText.c_str(), -1, nullptr, 0);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wideLen * sizeof(wchar_t));
        wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
        MultiByteToWideChar(CP_UTF8, 0, originalText.c_str(), -1, pMem, wideLen);
        GlobalUnlock(hMem);

        if (OpenClipboard(nullptr)) {
            EmptyClipboard();
            SetClipboardData(CF_UNICODETEXT, hMem);
            CloseClipboard();
        } else {
            GlobalFree(hMem);
        }
    }
}

}
