#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <thread>
#include <chrono>
#include <cstdio>
#include "keyboard_hook.h"
#include "tray.h"
#include "audio_capture.h"
#include "wav_writer.h"
#include "transcriber.h"
#include "text_injector.h"
#include "model_manager.h"
#include "overlay.h"
#include "settings.h"
#include "processor.h"
#include "updater.h"
#include "version.h"
#include "cuda_manager.h"
#include "whisper_engine.h"

// App state
enum class AppState { Initializing, Idle, Recording, Transcribing };
static AppState g_state = AppState::Initializing;
static HWND g_hwnd = nullptr;
static HINSTANCE g_hInstance = nullptr;
static std::chrono::steady_clock::time_point g_recordStartTime;
static std::vector<tray::AudioDevice> g_devices;
static int g_selectedDeviceIndex = -1; // -1 = default
static constexpr int MIN_RECORDING_MS = 500;

// Repeat-press mode
static settings::RepeatPressMode g_repeatMode = settings::RepeatPressMode::Queue;

// Model
static model::ModelSize g_modelSize = model::ModelSize::Small;
static bool g_downloading = false;

// Transcription options
static std::string g_language = "en";
static bool g_vocabPromptEnabled = false;

// Processor
static bool g_processorEnabled = false;

// Update
static updater::UpdateInfo g_updateInfo;
static std::wstring g_updateInstallerPath;

// Custom messages for async operations
constexpr UINT WM_TRANSCRIPTION_DONE = WM_APP + 20;
// WM_APP + 21 was WM_GPU_FALLBACK, now unused
constexpr UINT WM_MODEL_READY = WM_APP + 22;
constexpr UINT WM_MODEL_PROGRESS = WM_APP + 23;
constexpr UINT WM_PROCESSOR_READY = WM_APP + 24;
constexpr UINT WM_UPDATE_CHECK_DONE = WM_APP + 30;
constexpr UINT WM_UPDATE_DOWNLOAD_DONE = WM_APP + 31;
constexpr UINT WM_CUBLAS_READY = WM_APP + 32;

// Whisper engine (direct library link, no subprocess)
static WhisperEngine g_engine;
static std::wstring g_appDir;     // directory containing speakinto.exe
static std::wstring g_modelPath;

static void log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    // Also write to stderr for console debugging
    fprintf(stderr, "%s\n", buf);
}

static void refreshDevices() {
    g_devices = audio::enumerateDevices();
    log("Found %d audio devices", (int)g_devices.size());
    for (size_t i = 0; i < g_devices.size(); i++) {
        log("  [%d] %ls", (int)i, g_devices[i].name.c_str());
    }
}

static bool fileExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static std::wstring getAppDir() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir(exePath);
    return dir.substr(0, dir.find_last_of(L'\\') + 1);
}

// Find the CUDA DLL directory: check {appDir}\cuda\ (installer) then %APPDATA%\speakinto\cuda\ (downloaded)
static std::wstring findCudaDllDir() {
    // Installer path
    std::wstring installerCuda = g_appDir + L"cuda";
    if (fileExists(installerCuda + L"\\whisper.dll")) {
        return installerCuda;
    }
    // Dev build path
    std::wstring devCuda = g_appDir + L"..\\..\\bin\\cuda";
    if (fileExists(devCuda + L"\\whisper.dll")) {
        return devCuda;
    }
    // Downloaded CUDA path
    std::wstring downloadedCuda = cuda::getCudaDllDir();
    if (!downloadedCuda.empty() && fileExists(downloadedCuda + L"\\whisper.dll")) {
        return downloadedCuda;
    }
    return L"";
}

// Find the CPU DLL directory
static std::wstring findCpuDllDir() {
    if (fileExists(g_appDir + L"whisper.dll")) {
        return g_appDir;
    }
    std::wstring devDir = g_appDir + L"..\\..\\bin\\Release";
    if (fileExists(devDir + L"\\whisper.dll")) {
        return devDir;
    }
    return g_appDir; // fallback
}

static bool initWhisperEngine() {
    std::wstring cudaDir = findCudaDllDir();
    std::wstring cpuDir = findCpuDllDir();
    bool tryGpu = !cudaDir.empty();

    log("CPU DLL dir: %ls", cpuDir.c_str());
    if (tryGpu) log("CUDA DLL dir: %ls", cudaDir.c_str());

    bool ok = g_engine.init(g_modelPath, tryGpu, cudaDir, cpuDir);
    if (ok) {
        log("Whisper engine ready (%s)", g_engine.isUsingGpu() ? "GPU" : "CPU");
        transcriber::setEngine(&g_engine);
    } else {
        log("Whisper engine init failed");
    }
    return ok;
}

static void saveCurrentSettings() {
    settings::Settings cfg;
    cfg.repeatPressMode = g_repeatMode;
    cfg.selectedMicIndex = g_selectedDeviceIndex;
    cfg.modelSize = g_modelSize;
    cfg.processorEnabled = g_processorEnabled;
    cfg.vocabPromptEnabled = g_vocabPromptEnabled;
    cfg.language = g_language;
    settings::save(cfg);
}

static void onComboDown() {
    // Suppress during initialization
    if (g_state == AppState::Initializing) {
        overlay::setState(overlay::State::Initializing);
        return;
    }

    // Suppress while settings dialog is open
    if (settings::isDialogOpen()) return;

    // Handle repeat press during transcription
    if (g_state == AppState::Transcribing) {
        switch (g_repeatMode) {
            case settings::RepeatPressMode::Flash:
                overlay::flash();
                return;
            case settings::RepeatPressMode::Cancel:
                transcriber::cancelCurrent();
                // fall through to start recording immediately
                break;
            case settings::RepeatPressMode::Queue:
                // fall through to start recording immediately
                break;
        }
        // Queue and Cancel: start recording now, old transcription continues in background
    } else if (g_state != AppState::Idle) {
        return;
    }

    std::wstring deviceId = L"";
    if (g_selectedDeviceIndex >= 0 && g_selectedDeviceIndex < (int)g_devices.size()) {
        deviceId = g_devices[g_selectedDeviceIndex].id;
    }

    if (!audio::startCapture(deviceId)) {
        log("Failed to start audio capture — no microphone available");
        tray::setState(tray::State::Error);
        overlay::setState(overlay::State::Error);
        return;
    }

    g_state = AppState::Recording;
    g_recordStartTime = std::chrono::steady_clock::now();
    tray::setState(tray::State::Recording);
    overlay::setState(overlay::State::Recording);
}

static void onComboUp() {
    if (g_state != AppState::Recording) return;

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_recordStartTime).count();

    auto result = audio::stopCapture();

    if (duration < MIN_RECORDING_MS) {
        log("Recording discarded (%lldms < %dms)", duration, MIN_RECORDING_MS);
        g_state = AppState::Idle;
        tray::setState(tray::State::Idle);
        overlay::setState(overlay::State::Idle);
        return;
    }

    log("Recording stopped: %zu samples, %uHz, %uch, %lldms",
        result.samples.size(), result.sampleRate, result.channels, duration);

    if (result.samples.empty()) {
        log("No audio data captured");
        g_state = AppState::Idle;
        tray::setState(tray::State::Idle);
        overlay::setState(overlay::State::Idle);
        return;
    }

    g_state = AppState::Transcribing;
    tray::setState(tray::State::Transcribing);
    overlay::setState(overlay::State::Transcribing);

    // Run transcription on background thread
    HWND hwnd = g_hwnd;
    bool vocabPrompt = g_vocabPromptEnabled;
    auto lang = g_language;
    std::thread([result = std::move(result), hwnd, vocabPrompt, lang]() {
        transcriber::resetCancelFlag();

        // Resample to 16kHz mono float32 (no temp file)
        auto prepared = wav::prepareForWhisper(result.samples, result.sampleRate, result.channels);
        if (prepared.empty()) {
            log("Failed to prepare audio");
            PostMessage(hwnd, WM_TRANSCRIPTION_DONE, 0, 0);
            return;
        }

        std::string prompt;
        if (vocabPrompt) {
            prompt = "JavaScript TypeScript Python C++ C# Rust Go Java Kotlin Swift"
                " React Angular Vue Node.js Django Flask"
                " API REST GraphQL HTTP HTTPS JSON XML HTML CSS SCSS WebSocket OAuth JWT endpoint middleware webhook"
                " Docker Kubernetes AWS Azure GCP GitHub GitLab CI/CD pipeline deploy Nginx Redis PostgreSQL MongoDB MySQL"
                " function variable boolean integer string array object null undefined async await promise callback"
                " interface enum class struct commit merge branch pull request"
                " refactor debug compile runtime linter formatter ESLint component module dependency import";
        }

        auto txResult = transcriber::transcribe(prepared.data(), (int)prepared.size(), lang, prompt);

        // Check for cancellation before injecting text
        if (txResult.text.empty() || transcriber::isCancelRequested()) {
            if (!txResult.text.empty()) log("Transcription cancelled, discarding text");
            PostMessage(hwnd, WM_TRANSCRIPTION_DONE, 0, 0);
            return;
        }

        log("Transcribed: %s", txResult.text.c_str());

        // Run processor if enabled
        std::string finalText = txResult.text;
        bool processorRan = false;
        if (g_processorEnabled && processor::isReady()) {
            auto refined = processor::process(txResult.text);
            processorRan = true;
            log("Processor returned: %s", refined.c_str());
            if (!refined.empty() && refined != txResult.text) {
                finalText = refined;
            }
        }

        // Write comparison log to %APPDATA%\speakinto\transcription.log
        {
            wchar_t appdata[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
                std::wstring logPath = std::wstring(appdata) + L"\\speakinto\\transcription.log";
                FILE* f = _wfopen(logPath.c_str(), L"a");
                if (f) {
                    SYSTEMTIME st;
                    GetLocalTime(&st);
                    // Escape newlines in text so each log field stays on one line
                    auto escape = [](const std::string& s) {
                        std::string r;
                        for (char c : s) {
                            if (c == '\n') r += "\\n";
                            else if (c == '\r') continue;
                            else r += c;
                        }
                        return r;
                    };
                    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] | %s | %s | %s\n",
                        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                        escape(txResult.text).c_str(),
                        escape(finalText).c_str(),
                        processorRan ? "LLM" : "raw");
                    fclose(f);
                }
            }
        }

        injector::injectText(finalText);

        // Return to Idle immediately so user can record again
        PostMessage(hwnd, WM_TRANSCRIPTION_DONE, 0, 0);

        // Restore clipboard after target app has read it
        injector::restoreClipboard();
    }).detach();
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case keyboard::WM_COMBO_DOWN:
            onComboDown();
            return 0;

        case keyboard::WM_COMBO_UP:
            onComboUp();
            return 0;

        case WM_PROCESSOR_READY:
            g_downloading = false;
            overlay::setState(overlay::State::Idle);
            tray::setState(tray::State::Idle);
            settings::notifyProcessorDownloadComplete(lParam != 0);
            if (lParam) {
                if (g_processorEnabled && processor::isReady()) {
                    processor::start();
                    log("AI processor started after download");
                }
            } else {
                log("Failed to download AI model");
            }
            return 0;

        case WM_CUBLAS_READY: {
            bool success = lParam != 0;
            if (!g_downloading) {
                overlay::setState(overlay::State::Idle);
                tray::setState(tray::State::Idle);
            }
            if (success && !g_engine.isUsingGpu()) {
                // CUDA DLLs now available — reinit engine to try GPU
                log("CUDA DLLs downloaded, reinitializing engine with GPU");
                g_engine.shutdown();
                initWhisperEngine();
            } else if (!success) {
                log("GPU download failed, using CPU");
            }
            return 0;
        }

        case WM_UPDATE_CHECK_DONE:
            settings::notifyUpdateCheckComplete(
                g_updateInfo.available,
                g_updateInfo.latestVersion.c_str(),
                g_updateInfo.changelog.c_str());
            if (g_updateInfo.available && !settings::isDialogOpen()) {
                log("Update available: %s", g_updateInfo.latestVersion.c_str());
            }
            return 0;

        case WM_UPDATE_DOWNLOAD_DONE: {
            bool success = lParam != 0;
            settings::notifyUpdateDownloadComplete(success);
            if (success && !g_updateInstallerPath.empty()) {
                settings::closeDialog();
                ShellExecuteW(nullptr, L"open", g_updateInstallerPath.c_str(),
                              nullptr, nullptr, SW_SHOWNORMAL);
                PostQuitMessage(0);
            } else if (!success && !g_updateInfo.htmlUrl.empty()) {
                // Offer to open release page as fallback
                int wlen = MultiByteToWideChar(CP_UTF8, 0, g_updateInfo.htmlUrl.c_str(), -1, nullptr, 0);
                std::vector<wchar_t> wurl(wlen);
                MultiByteToWideChar(CP_UTF8, 0, g_updateInfo.htmlUrl.c_str(), -1, wurl.data(), wlen);
                ShellExecuteW(nullptr, L"open", wurl.data(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            return 0;
        }

        case WM_MODEL_PROGRESS:
            overlay::setState(overlay::State::Downloading, (int)wParam);
            return 0;

        case WM_MODEL_READY: {
            // wParam = old model size (-1 if startup/no previous model)
            // lParam = 1 on success, 0 on failure
            g_downloading = false;
            overlay::setState(overlay::State::Idle);
            tray::setState(tray::State::Idle);
            bool success = lParam != 0;
            auto oldSize = (int)wParam;
            bool isStartup = (oldSize == -1);

            if (!success) {
                log("Model download failed");
                if (!isStartup) {
                    // Revert to old working model so transcription keeps working
                    g_modelSize = (model::ModelSize)oldSize;
                    g_modelPath = model::getModelPath(g_modelSize);
                    saveCurrentSettings();
                    log("Reverted to model: %s", model::modelSizeName(g_modelSize));
                }
            } else if (model::modelExists(g_modelSize)) {
                g_modelPath = model::getModelPath(g_modelSize);
                model::deleteAllExcept(g_modelSize);
                if (g_engine.isLoaded()) {
                    g_engine.reloadModel(g_modelPath);
                } else {
                    initWhisperEngine();
                }
                if (isStartup) {
                    g_state = AppState::Idle;
                }
                log("Model ready: %s", model::modelSizeName(g_modelSize));
            }
            // If processor was enabled during model download, start its download now
            if (g_processorEnabled && !processor::isReady() && !g_downloading) {
                g_downloading = true;
                tray::setState(tray::State::Downloading);
                overlay::setState(overlay::State::Downloading, 0);
                std::thread([hwndMsg = g_hwnd]() {
                    bool ok = processor::ensureDependencies([hwndMsg](int percent) {
                        PostMessage(hwndMsg, WM_MODEL_PROGRESS, percent, 0);
                    });
                    PostMessage(hwndMsg, WM_PROCESSOR_READY, 0, ok ? 1 : 0);
                }).detach();
            }
            return 0;
        }

        case WM_TRANSCRIPTION_DONE:
            // If already recording a new one (Queue/Cancel started it during
            // old transcription), don't reset — old transcription already
            // injected its text from the background thread.
            if (g_state == AppState::Recording) return 0;
            g_state = AppState::Idle;
            tray::setState(tray::State::Idle);
            overlay::setState(overlay::State::Idle);
            return 0;

        case tray::WM_TRAY_ICON:
            if (lParam == WM_RBUTTONUP) {
                refreshDevices();
                tray::showContextMenu(hwnd, g_devices, g_selectedDeviceIndex);
            }
            return 0;

        case WM_COMMAND: {
            UINT id = LOWORD(wParam);
            if (id == tray::IDM_QUIT) {
                PostQuitMessage(0);
            } else if (id == tray::IDM_SETTINGS) {
                settings::Settings cfg;
                cfg.repeatPressMode = g_repeatMode;
                cfg.selectedMicIndex = g_selectedDeviceIndex;
                cfg.modelSize = g_modelSize;
                cfg.processorEnabled = g_processorEnabled;
                const wchar_t* backendInfo = g_engine.isUsingGpu()
                    ? L"Transcription backend: CUDA (GPU)"
                    : L"Transcription backend: CPU";
                settings::ProcessorCallbacks procCb;
                procCb.isReady = []() { return processor::isReady(); };
                procCb.requestDownload = [hwndMsg = g_hwnd]() {
                    if (g_downloading) return;
                    g_downloading = true;
                    tray::setState(tray::State::Downloading);
                    overlay::setState(overlay::State::Downloading, 0);
                    std::thread([hwndMsg]() {
                        bool ok = processor::ensureDependencies([hwndMsg](int percent) {
                            PostMessage(hwndMsg, WM_MODEL_PROGRESS, percent, 0);
                        });
                        PostMessage(hwndMsg, WM_PROCESSOR_READY, 0, ok ? 1 : 0);
                    }).detach();
                };
                procCb.requestRemove = []() {
                    processor::removeDependencies();
                };

                settings::UpdateCallbacks updateCb;
                updateCb.requestCheck = [hwndMsg = g_hwnd]() {
                    std::thread([hwndMsg]() {
                        g_updateInfo = updater::checkForUpdates();
                        PostMessage(hwndMsg, WM_UPDATE_CHECK_DONE, 0, 0);
                    }).detach();
                };
                updateCb.requestInstall = [hwndMsg = g_hwnd]() {
                    std::thread([hwndMsg]() {
                        auto path = updater::downloadInstaller(g_updateInfo.downloadUrl,
                            [hwndMsg](int percent) {
                                PostMessage(hwndMsg, WM_MODEL_PROGRESS, percent, 0);
                            });
                        g_updateInstallerPath = path;
                        PostMessage(hwndMsg, WM_UPDATE_DOWNLOAD_DONE, 0, path.empty() ? 0 : 1);
                    }).detach();
                };
                if (settings::showSettingsDialog(g_hInstance, cfg, backendInfo, procCb, updateCb)) {
                    g_repeatMode = cfg.repeatPressMode;
                    g_selectedDeviceIndex = cfg.selectedMicIndex;

                    // Handle model change
                    if (cfg.modelSize != g_modelSize) {
                        if (!g_downloading) {
                            model::ModelSize oldSize = g_modelSize;
                            g_modelSize = cfg.modelSize;

                            if (model::modelExists(g_modelSize)) {
                                g_modelPath = model::getModelPath(g_modelSize);
                                model::deleteAllExcept(g_modelSize);
                                g_engine.reloadModel(g_modelPath);
                                log("Model switched to %s", model::modelSizeName(g_modelSize));
                            } else {
                                g_downloading = true;
                                tray::setState(tray::State::Downloading);
                                overlay::setState(overlay::State::Downloading, 0);
                                std::thread([newSize = g_modelSize, oldSize, hwndMsg = g_hwnd]() {
                                    bool ok = model::downloadModel(newSize, [hwndMsg](int percent) {
                                        PostMessage(hwndMsg, WM_MODEL_PROGRESS, percent, 0);
                                    });
                                    PostMessage(hwndMsg, WM_MODEL_READY, (WPARAM)oldSize, ok ? 1 : 0);
                                }).detach();
                            }
                        }
                    }

                    g_vocabPromptEnabled = cfg.vocabPromptEnabled;
                    g_language = cfg.language;

                    // Handle processor toggle — deps already managed via button
                    if (cfg.processorEnabled != g_processorEnabled) {
                        g_processorEnabled = cfg.processorEnabled;
                        if (g_processorEnabled && processor::isReady()) {
                            processor::start();
                        } else if (!g_processorEnabled) {
                            processor::stop();
                        }
                    }

                    saveCurrentSettings();
                }
            } else if (id >= tray::IDM_MIC_BASE && id < tray::IDM_MIC_BASE + 100) {
                g_selectedDeviceIndex = id - tray::IDM_MIC_BASE;
                log("Selected mic: %ls", g_devices[g_selectedDeviceIndex].name.c_str());
                saveCurrentSettings();
            }
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // Single instance check
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"SpeakIntoMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    g_hInstance = hInstance;

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"SpeakIntoClass";
    RegisterClassExW(&wc);

    // Create hidden message-only window
    g_hwnd = CreateWindowExW(0, L"SpeakIntoClass", L"SpeakInto",
                              0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);

    g_appDir = getAppDir();
    refreshDevices();

    // Load settings
    settings::Settings cfg = settings::load();
    g_selectedDeviceIndex = cfg.selectedMicIndex;
    g_repeatMode = cfg.repeatPressMode;
    g_modelSize = cfg.modelSize;
    g_modelPath = model::getModelPath(g_modelSize);
    g_processorEnabled = cfg.processorEnabled;
    g_vocabPromptEnabled = cfg.vocabPromptEnabled;
    g_language = cfg.language;

    tray::create(g_hwnd);
    overlay::create(hInstance);

    // Start hotkey early so it's responsive during model download
    keyboard::start(g_hwnd);
    tray::setState(tray::State::Initializing);

    // Check if CUDA DLLs need downloading (NVIDIA variant without local CUDA setup)
    {
        std::wstring cudaDir = findCudaDllDir();
        bool hasCudaExe = !cudaDir.empty(); // Has CUDA DLLs already
        // Check if this is a CUDA-capable build (has ggml-cuda.dll or cuda/ subdir nearby)
        bool isCudaVariant = hasCudaExe || fileExists(g_appDir + L"ggml-cuda.dll") || fileExists(g_appDir + L"cuda\\ggml-cuda.dll");
        if (isCudaVariant && !hasCudaExe && !cuda::isReady()) {
            log("CUDA DLLs not found, downloading...");
            overlay::setState(overlay::State::Downloading, 0);
            std::thread([hwnd = g_hwnd]() {
                bool ok = cuda::ensureSetup([hwnd](int percent) {
                    PostMessage(hwnd, WM_MODEL_PROGRESS, percent, 0);
                });
                PostMessage(hwnd, WM_CUBLAS_READY, 0, ok ? 1 : 0);
            }).detach();
        }
    }

    // Check and download model if needed (async to keep UI responsive)
    if (!model::modelExists(g_modelSize)) {
        log("Model %s not found, downloading...", model::modelSizeName(g_modelSize));
        g_downloading = true;
        overlay::setState(overlay::State::Downloading, 0);
        std::thread([size = g_modelSize, hwnd = g_hwnd]() {
            bool ok = model::downloadModel(size, [hwnd](int percent) {
                PostMessage(hwnd, WM_MODEL_PROGRESS, percent, 0);
            });
            // wParam = -1 (no old model), lParam = success flag, special startup case
            PostMessage(hwnd, WM_MODEL_READY, (WPARAM)(-1), ok ? 1 : 0);
        }).detach();
    } else {
        // Model already present — init engine and go to ready
        initWhisperEngine();
        g_state = AppState::Idle;
        tray::setState(tray::State::Idle);
        overlay::setState(overlay::State::Idle);
    }

    // Start processor if enabled and ready
    if (g_processorEnabled && processor::isReady()) {
        processor::start();
        log("AI processor started");
    }

    log("SpeakInto is running. Hold Ctrl+` to record.");

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup — cancel any in-flight transcription before freeing the model
    g_engine.cancel();
    g_engine.shutdown();
    processor::stop();
    keyboard::stop();
    overlay::destroy();
    tray::destroy();
    audio::cleanup();
    CoUninitialize();
    ReleaseMutex(mutex);
    CloseHandle(mutex);

    return 0;
}
