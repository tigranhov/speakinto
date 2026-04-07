#pragma once
#include <string>
#include <functional>

namespace cuda {

// Get path to CUDA whisper-cli in %APPDATA%/wisper-agent/cuda/
std::wstring getWhisperExePath();

// Check if full CUDA setup is ready (whisper-cli + DLLs + cuBLAS)
bool isReady();

// Download full CUDA whisper setup to %APPDATA%/wisper-agent/cuda/.
// Blocking — call from background thread.
bool ensureSetup(std::function<void(int percent)> onProgress = nullptr);

}
