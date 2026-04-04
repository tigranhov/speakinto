#pragma once
#include <string>
#include <functional>

namespace processor {

// Check if all dependencies (llama-server + model) are downloaded
bool isReady();

// Download dependencies. Calls onProgress(percent) during download.
bool ensureDependencies(std::function<void(int percent)> onProgress = nullptr);

// Process transcribed text. Returns refined text, or original on failure.
std::string process(const std::string& text);

// Remove all downloaded dependencies
void removeDependencies();

// Start/stop the LLM server
void start();
void stop();

}
