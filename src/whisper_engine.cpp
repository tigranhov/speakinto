#include "whisper_engine.h"
#include "vendor/whisper.h"
#include <cstdio>

static void log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    fprintf(stderr, "%s\n", buf);
}

static std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string utf8(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], len, nullptr, nullptr);
    return utf8;
}

// Static abort callback checked by whisper during inference
static bool whisperAbortCallback(void* user_data) {
    auto* flag = static_cast<std::atomic<bool>*>(user_data);
    return flag->load(std::memory_order_relaxed);
}

WhisperEngine::~WhisperEngine() {
    shutdown();
}

bool WhisperEngine::loadDll(const std::wstring& dllDir) {
    // Set DLL search directory so implicit deps (ggml.dll, ggml-cuda.dll, etc.) resolve
    if (!dllDir.empty()) {
        SetDllDirectoryW(dllDir.c_str());
    }

    std::wstring dllPath = dllDir.empty() ? L"whisper.dll" : (dllDir + L"\\whisper.dll");
    m_dll = LoadLibraryW(dllPath.c_str());

    SetDllDirectoryW(nullptr);

    if (!m_dll) {
        log("Failed to load %ls: error %lu", dllPath.c_str(), GetLastError());
        return false;
    }

    m_contextDefaultParams = (fn_context_default_params)GetProcAddress(m_dll, "whisper_context_default_params");
    m_initFromFile = (fn_init_from_file)GetProcAddress(m_dll, "whisper_init_from_file_with_params");
    m_fullDefaultParams = (fn_full_default_params)GetProcAddress(m_dll, "whisper_full_default_params");
    m_full = (fn_full)GetProcAddress(m_dll, "whisper_full");
    m_fullNSegments = (fn_full_n_segments)GetProcAddress(m_dll, "whisper_full_n_segments");
    m_fullGetSegmentText = (fn_full_get_segment_text)GetProcAddress(m_dll, "whisper_full_get_segment_text");
    m_whisperFree = (fn_free)GetProcAddress(m_dll, "whisper_free");

    if (!m_contextDefaultParams || !m_initFromFile || !m_fullDefaultParams ||
        !m_full || !m_fullNSegments || !m_fullGetSegmentText || !m_whisperFree) {
        log("Failed to resolve whisper function pointers");
        FreeLibrary(m_dll);
        m_dll = nullptr;
        return false;
    }

    log("Loaded whisper DLL: %ls", dllPath.c_str());
    return true;
}

void WhisperEngine::unloadDll() {
    if (m_dll) {
        FreeLibrary(m_dll);
        m_dll = nullptr;
    }
    m_contextDefaultParams = nullptr;
    m_initFromFile = nullptr;
    m_fullDefaultParams = nullptr;
    m_full = nullptr;
    m_fullNSegments = nullptr;
    m_fullGetSegmentText = nullptr;
    m_whisperFree = nullptr;
}

bool WhisperEngine::initModel(const std::string& modelPathUtf8, bool useGpu) {
    auto cparams = m_contextDefaultParams();
    cparams.use_gpu = useGpu;
    cparams.flash_attn = false;
    cparams.gpu_device = 0;

    m_ctx = m_initFromFile(modelPathUtf8.c_str(), cparams);
    if (!m_ctx) {
        log("Failed to init whisper model (gpu=%d): %s", useGpu, modelPathUtf8.c_str());
        return false;
    }

    m_usingGpu = useGpu;
    log("Whisper model loaded (gpu=%d): %s", useGpu, modelPathUtf8.c_str());
    return true;
}

bool WhisperEngine::init(const std::wstring& modelPath, bool tryGpu,
                          const std::wstring& cudaDllDir, const std::wstring& cpuDllDir) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string modelUtf8 = wideToUtf8(modelPath);

    // Try CUDA DLL first — loadDll handles missing files gracefully
    if (tryGpu && !cudaDllDir.empty()) {
        if (loadDll(cudaDllDir)) {
            if (initModel(modelUtf8, true)) {
                return true;
            }
            log("GPU init failed, trying CPU mode with CUDA DLL");
            if (initModel(modelUtf8, false)) {
                return true;
            }
            unloadDll();
        }
    }

    // Fall back to CPU DLL
    if (loadDll(cpuDllDir)) {
        if (initModel(modelUtf8, false)) {
            return true;
        }
        unloadDll();
    }

    log("WhisperEngine init failed completely");
    return false;
}

std::string WhisperEngine::transcribe(const float* samples, int n_samples,
                                       const std::string& language,
                                       const std::string& prompt) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_ctx || !m_full) {
        return "";
    }

    m_abort = false;

    auto params = m_fullDefaultParams(0 /*WHISPER_SAMPLING_GREEDY*/);
    params.n_threads = 4;
    params.no_timestamps = true;
    params.no_context = true;
    params.print_special = false;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.single_segment = false;
    params.language = language.c_str();
    params.translate = false;
    params.suppress_blank = true;

    if (!prompt.empty()) {
        params.initial_prompt = prompt.c_str();
    }

    // Set abort callback for cancellation
    params.abort_callback = whisperAbortCallback;
    params.abort_callback_user_data = &m_abort;

    int ret = m_full(m_ctx, params, samples, n_samples);
    if (ret != 0) {
        if (m_abort.load()) {
            log("Transcription cancelled");
        } else {
            log("whisper_full failed: %d", ret);
        }
        return "";
    }

    // Collect all segment text
    int n_segments = m_fullNSegments(m_ctx);
    std::string result;
    for (int i = 0; i < n_segments; i++) {
        const char* text = m_fullGetSegmentText(m_ctx, i);
        if (text) {
            result += text;
        }
    }

    return result;
}

void WhisperEngine::cancel() {
    m_abort = true;
}

void WhisperEngine::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_ctx && m_whisperFree) {
        m_whisperFree(m_ctx);
        m_ctx = nullptr;
    }
    m_usingGpu = false;
    unloadDll();
}

bool WhisperEngine::reloadModel(const std::wstring& modelPath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_dll || !m_whisperFree || !m_initFromFile || !m_contextDefaultParams) {
        return false;
    }

    if (m_ctx) {
        m_whisperFree(m_ctx);
        m_ctx = nullptr;
    }

    std::string modelUtf8 = wideToUtf8(modelPath);
    return initModel(modelUtf8, m_usingGpu);
}
