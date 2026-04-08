#pragma once
#include <windows.h>
#include <string>
#include <mutex>
#include <atomic>

struct whisper_context;
struct whisper_context_params;
struct whisper_full_params;

class WhisperEngine {
public:
    WhisperEngine() = default;
    ~WhisperEngine();

    WhisperEngine(const WhisperEngine&) = delete;
    WhisperEngine& operator=(const WhisperEngine&) = delete;

    // Initialize: load DLL and model. tryGpu attempts CUDA first.
    // cudaDllDir: directory containing CUDA whisper.dll + deps (empty to skip GPU).
    // cpuDllDir: directory containing CPU whisper.dll + deps.
    // Returns true on success (GPU or CPU).
    bool init(const std::wstring& modelPath, bool tryGpu,
              const std::wstring& cudaDllDir, const std::wstring& cpuDllDir);

    // Transcribe 16kHz mono float32 PCM samples.
    // Returns transcribed text, or empty string on failure.
    std::string transcribe(const float* samples, int n_samples,
                           const std::string& language,
                           const std::string& prompt);

    // Request abort of in-progress transcription. Safe to call from any thread.
    void cancel();

    // Free model and unload DLL.
    void shutdown();

    // Reload just the model without unloading the DLL (e.g. user changed model size).
    // Returns false if reload fails (engine remains shut down).
    bool reloadModel(const std::wstring& modelPath);

    bool isLoaded() const { return m_ctx != nullptr; }
    bool isUsingGpu() const { return m_usingGpu; }

private:
    // Function pointer types
    using fn_context_default_params = whisper_context_params(*)();
    using fn_init_from_file = whisper_context*(*)(const char*, whisper_context_params);
    using fn_full_default_params = whisper_full_params(*)(int /*whisper_sampling_strategy*/);
    using fn_full = int(*)(whisper_context*, whisper_full_params, const float*, int);
    using fn_full_n_segments = int(*)(whisper_context*);
    using fn_full_get_segment_text = const char*(*)(whisper_context*, int);
    using fn_free = void(*)(whisper_context*);

    bool loadDll(const std::wstring& dllDir);
    void unloadDll();
    bool initModel(const std::string& modelPathUtf8, bool useGpu);

    HMODULE m_dll = nullptr;
    whisper_context* m_ctx = nullptr;
    bool m_usingGpu = false;
    std::mutex m_mutex;
    std::atomic<bool> m_abort{false};

    // Resolved function pointers
    fn_context_default_params m_contextDefaultParams = nullptr;
    fn_init_from_file m_initFromFile = nullptr;
    fn_full_default_params m_fullDefaultParams = nullptr;
    fn_full m_full = nullptr;
    fn_full_n_segments m_fullNSegments = nullptr;
    fn_full_get_segment_text m_fullGetSegmentText = nullptr;
    fn_free m_whisperFree = nullptr;
};
