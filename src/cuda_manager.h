#pragma once
#include <string>
#include <functional>

namespace cuda {

// Get path to CUDA DLL directory (%APPDATA%/speakinto/cuda/)
std::wstring getCudaDllDir();

// Check if CUDA DLLs are ready (whisper.dll + ggml-cuda.dll + cuBLAS)
bool isReady();

// Download CUDA whisper DLLs to %APPDATA%/speakinto/cuda/.
// Blocking — call from background thread.
bool ensureSetup(std::function<void(int percent)> onProgress = nullptr);

}
