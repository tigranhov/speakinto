#pragma once
#include <string>
#include <functional>

namespace model {

// Get the expected model path: %APPDATA%/wisper-agent/models/ggml-base.bin
std::wstring getModelPath();

// Check if the model file exists and is valid (>100MB)
bool modelExists();

// Download the model from HuggingFace. Calls onProgress(percent) during download.
// Returns true on success.
bool downloadModel(std::function<void(int percent)> onProgress = nullptr);

}
