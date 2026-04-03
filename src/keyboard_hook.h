#pragma once
#include <windows.h>

namespace keyboard {

// Custom window messages sent to main thread
constexpr UINT WM_COMBO_DOWN = WM_APP + 1;
constexpr UINT WM_COMBO_UP   = WM_APP + 2;

void start(HWND hwndMain);
void stop();

}
