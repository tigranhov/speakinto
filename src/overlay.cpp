#include "overlay.h"

namespace overlay {

static HWND g_hwnd = nullptr;
static State g_state = State::Idle;
static HFONT g_font = nullptr;
static constexpr int WIDTH = 80;
static constexpr int HEIGHT = 30;
static constexpr int CORNER_RADIUS = 15;

static void positionOnActiveMonitor() {
    HMONITOR hMon = nullptr;

    HWND fg = GetForegroundWindow();
    if (fg && fg != g_hwnd) {
        hMon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
    } else {
        POINT pt = {0, 0};
        hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    }

    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfo(hMon, &mi)) return;

    int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - WIDTH) / 2;
    int y = mi.rcWork.top + 10;

    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, WIDTH, HEIGHT,
                 SWP_NOACTIVATE | SWP_NOSIZE);
}

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);

        // Fill with black (becomes transparent via color key)
        HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(hdc, &rc, blackBrush);
        DeleteObject(blackBrush);

        // Choose color and text based on state
        COLORREF fillColor;
        const wchar_t* text;

        if (g_state == State::Recording) {
            fillColor = RGB(220, 50, 50);  // Red
            text = L"REC";
        } else if (g_state == State::Transcribing) {
            fillColor = RGB(50, 120, 220); // Blue
            text = L"...";
        } else {
            EndPaint(hwnd, &ps);
            return 0;
        }

        // Draw rounded pill
        HBRUSH fillBrush = CreateSolidBrush(fillColor);
        HPEN nullPen = CreatePen(PS_NULL, 0, 0);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, fillBrush);
        HPEN oldPen = (HPEN)SelectObject(hdc, nullPen);

        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, CORNER_RADIUS, CORNER_RADIUS);

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(fillBrush);
        DeleteObject(nullPen);

        // Draw text
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        HFONT oldFont = (HFONT)SelectObject(hdc, g_font);
        DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);

        EndPaint(hwnd, &ps);
        return 0;
    }

    // Make click-through: return HTTRANSPARENT for all hit tests
    if (msg == WM_NCHITTEST) {
        return HTTRANSPARENT;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void create(HINSTANCE hInstance) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"WisperOverlayClass";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"WisperOverlayClass",
        L"",
        WS_POPUP,
        0, 0, WIDTH, HEIGHT,
        nullptr, nullptr, hInstance, nullptr
    );

    // Black pixels become transparent
    SetLayeredWindowAttributes(g_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    // Create font
    g_font = CreateFontW(
        16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );
}

void destroy() {
    if (g_hwnd) {
        DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
    }
    if (g_font) {
        DeleteObject(g_font);
        g_font = nullptr;
    }
}

void setState(State state) {
    g_state = state;
    if (!g_hwnd) return;

    if (state == State::Idle) {
        ShowWindow(g_hwnd, SW_HIDE);
    } else {
        positionOnActiveMonitor();
        InvalidateRect(g_hwnd, nullptr, TRUE);
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    }
}

}
