#include "settings.h"
#include <shlobj.h>
#include <fstream>
#include <string>

namespace settings {

static std::atomic<bool> g_dialogOpen{false};

bool isDialogOpen() { return g_dialogOpen; }

// --- Paths ---

static std::wstring getSettingsDir() {
    wchar_t appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        return L"";
    }
    return std::wstring(appdata) + L"\\wisper-agent";
}

static std::wstring getSettingsPath() {
    auto dir = getSettingsDir();
    if (dir.empty()) return L"";
    return dir + L"\\settings.json";
}

// --- Minimal JSON helpers ---

static std::string findValue(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    pos++;

    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r'))
        pos++;

    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        auto end = json.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos + 1, end - pos - 1);
    } else {
        auto end = json.find_first_of(",}\n\r ", pos);
        if (end == std::string::npos) end = json.size();
        return json.substr(pos, end - pos);
    }
}

static RepeatPressMode parseModeString(const std::string& s) {
    if (s == "flash") return RepeatPressMode::Flash;
    if (s == "cancel") return RepeatPressMode::Cancel;
    return RepeatPressMode::Queue;
}

static const char* modeToString(RepeatPressMode mode) {
    switch (mode) {
        case RepeatPressMode::Flash: return "flash";
        case RepeatPressMode::Cancel: return "cancel";
        default: return "queue";
    }
}

// --- Load / Save ---

Settings load() {
    Settings s;
    auto path = getSettingsPath();
    if (path.empty()) return s;

    std::ifstream file(path);
    if (!file.is_open()) return s; // fresh install — struct defaults apply (Small)

    std::string json((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
    file.close();

    auto modeStr = findValue(json, "repeatPressMode");
    if (!modeStr.empty()) {
        s.repeatPressMode = parseModeString(modeStr);
    }

    auto micStr = findValue(json, "selectedMicIndex");
    if (!micStr.empty()) {
        try { s.selectedMicIndex = std::stoi(micStr); }
        catch (...) {}
    }

    auto modelStr = findValue(json, "modelSize");
    if (!modelStr.empty()) {
        s.modelSize = model::modelSizeFromString(modelStr);
    } else {
        // Existing user with old settings file — keep Base to avoid surprise download
        s.modelSize = model::ModelSize::Base;
    }

    return s;
}

void save(const Settings& s) {
    auto dir = getSettingsDir();
    if (dir.empty()) return;

    CreateDirectoryW(dir.c_str(), nullptr);

    auto path = getSettingsPath();

    auto tmpPath = path + L".tmp";
    std::ofstream file(tmpPath);
    if (!file.is_open()) return;

    file << "{\n";
    file << "  \"repeatPressMode\": \"" << modeToString(s.repeatPressMode) << "\",\n";
    file << "  \"selectedMicIndex\": " << s.selectedMicIndex << ",\n";
    file << "  \"modelSize\": \"" << model::modelSizeString(s.modelSize) << "\"\n";
    file << "}\n";
    file.close();

    MoveFileExW(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// --- Settings Dialog ---

static const wchar_t* SETTINGS_CLASS = L"WisperSettingsClass";
static Settings* g_dlgSettings = nullptr;
static bool g_dlgResult = false;
static bool g_dlgClosed = false;
static HWND g_dlgHwnd = nullptr;

// Control IDs — repeat press mode
static constexpr int ID_RADIO_FLASH  = 101;
static constexpr int ID_RADIO_QUEUE  = 102;
static constexpr int ID_RADIO_CANCEL = 103;
// Control IDs — model size
static constexpr int ID_RADIO_TINY   = 201;
static constexpr int ID_RADIO_BASE   = 202;
static constexpr int ID_RADIO_SMALL  = 203;
static constexpr int ID_RADIO_MEDIUM = 204;
// Buttons
static constexpr int ID_OK           = IDOK;
static constexpr int ID_CANCEL       = IDCANCEL;

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == ID_OK) {
                // Read repeat-press mode
                if (SendDlgItemMessageW(hwnd, ID_RADIO_FLASH, BM_GETCHECK, 0, 0) == BST_CHECKED)
                    g_dlgSettings->repeatPressMode = RepeatPressMode::Flash;
                else if (SendDlgItemMessageW(hwnd, ID_RADIO_CANCEL, BM_GETCHECK, 0, 0) == BST_CHECKED)
                    g_dlgSettings->repeatPressMode = RepeatPressMode::Cancel;
                else
                    g_dlgSettings->repeatPressMode = RepeatPressMode::Queue;

                // Read model size
                if (SendDlgItemMessageW(hwnd, ID_RADIO_TINY, BM_GETCHECK, 0, 0) == BST_CHECKED)
                    g_dlgSettings->modelSize = model::ModelSize::Tiny;
                else if (SendDlgItemMessageW(hwnd, ID_RADIO_BASE, BM_GETCHECK, 0, 0) == BST_CHECKED)
                    g_dlgSettings->modelSize = model::ModelSize::Base;
                else if (SendDlgItemMessageW(hwnd, ID_RADIO_MEDIUM, BM_GETCHECK, 0, 0) == BST_CHECKED)
                    g_dlgSettings->modelSize = model::ModelSize::Medium;
                else
                    g_dlgSettings->modelSize = model::ModelSize::Small;

                g_dlgResult = true;
                DestroyWindow(hwnd);
            } else if (id == ID_CANCEL) {
                g_dlgResult = false;
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_CLOSE:
            g_dlgResult = false;
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            g_dlgHwnd = nullptr;
            g_dlgClosed = true;
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool showSettingsDialog(HINSTANCE hInstance, Settings& s, const wchar_t* backendInfo) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = SETTINGS_CLASS;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    g_dlgSettings = &s;
    g_dlgResult = false;

    int dlgW = 340, dlgH = 400;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - dlgW) / 2;
    int y = (screenH - dlgH) / 2;

    g_dlgHwnd = CreateWindowExW(
        WS_EX_TOPMOST,
        SETTINGS_CLASS,
        L"Wisper Agent Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, dlgW, dlgH,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_dlgHwnd) return false;

    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    // --- Repeat press mode group ---
    HWND hGroup1 = CreateWindowExW(0, L"BUTTON", L"When hotkey pressed during transcription:",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        10, 10, 310, 130,
        g_dlgHwnd, nullptr, hInstance, nullptr);
    SendMessageW(hGroup1, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hFlash = CreateWindowExW(0, L"BUTTON", L"Flash overlay (acknowledge only)",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
        25, 35, 280, 22,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_RADIO_FLASH, hInstance, nullptr);
    SendMessageW(hFlash, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hQueue = CreateWindowExW(0, L"BUTTON", L"Queue next recording",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        25, 65, 280, 22,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_RADIO_QUEUE, hInstance, nullptr);
    SendMessageW(hQueue, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hCancelRb = CreateWindowExW(0, L"BUTTON", L"Cancel and re-record",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        25, 95, 280, 22,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_RADIO_CANCEL, hInstance, nullptr);
    SendMessageW(hCancelRb, WM_SETFONT, (WPARAM)hFont, TRUE);

    switch (s.repeatPressMode) {
        case RepeatPressMode::Flash:
            SendMessageW(hFlash, BM_SETCHECK, BST_CHECKED, 0); break;
        case RepeatPressMode::Cancel:
            SendMessageW(hCancelRb, BM_SETCHECK, BST_CHECKED, 0); break;
        default:
            SendMessageW(hQueue, BM_SETCHECK, BST_CHECKED, 0); break;
    }

    // --- Model size group ---
    HWND hGroup2 = CreateWindowExW(0, L"BUTTON", L"Transcription model:",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        10, 150, 310, 140,
        g_dlgHwnd, nullptr, hInstance, nullptr);
    SendMessageW(hGroup2, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hTiny = CreateWindowExW(0, L"BUTTON", L"Tiny (~75MB)",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
        25, 175, 280, 22,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_RADIO_TINY, hInstance, nullptr);
    SendMessageW(hTiny, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hBase = CreateWindowExW(0, L"BUTTON", L"Base (~150MB)",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        25, 200, 280, 22,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_RADIO_BASE, hInstance, nullptr);
    SendMessageW(hBase, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hSmall = CreateWindowExW(0, L"BUTTON", L"Small (~500MB, recommended)",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        25, 225, 280, 22,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_RADIO_SMALL, hInstance, nullptr);
    SendMessageW(hSmall, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hMedium = CreateWindowExW(0, L"BUTTON", L"Medium (~1.5GB)",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        25, 250, 280, 22,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_RADIO_MEDIUM, hInstance, nullptr);
    SendMessageW(hMedium, WM_SETFONT, (WPARAM)hFont, TRUE);

    switch (s.modelSize) {
        case model::ModelSize::Tiny:
            SendMessageW(hTiny, BM_SETCHECK, BST_CHECKED, 0); break;
        case model::ModelSize::Base:
            SendMessageW(hBase, BM_SETCHECK, BST_CHECKED, 0); break;
        case model::ModelSize::Medium:
            SendMessageW(hMedium, BM_SETCHECK, BST_CHECKED, 0); break;
        default:
            SendMessageW(hSmall, BM_SETCHECK, BST_CHECKED, 0); break;
    }

    // Backend info line
    HWND hInfo = CreateWindowExW(0, L"STATIC", backendInfo,
        WS_CHILD | WS_VISIBLE,
        10, 298, 310, 18,
        g_dlgHwnd, nullptr, hInstance, nullptr);
    SendMessageW(hInfo, WM_SETFONT, (WPARAM)hFont, TRUE);

    // OK / Cancel buttons
    HWND hOk = CreateWindowExW(0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        150, 325, 80, 28,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_OK, hInstance, nullptr);
    SendMessageW(hOk, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hCancelBtn = CreateWindowExW(0, L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        240, 325, 80, 28,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_CANCEL, hInstance, nullptr);
    SendMessageW(hCancelBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_dialogOpen = true;
    ShowWindow(g_dlgHwnd, SW_SHOW);
    SetForegroundWindow(g_dlgHwnd);

    g_dlgClosed = false;
    MSG msg;
    while (!g_dlgClosed) {
        if (!GetMessage(&msg, nullptr, 0, 0)) break;
        if (g_dlgHwnd && IsDialogMessageW(g_dlgHwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_dialogOpen = false;
    g_dlgSettings = nullptr;

    return g_dlgResult;
}

}
