#pragma once
#include <string>
#include <functional>

namespace processor_llm {

bool isReady();
bool ensureDependencies(std::function<void(int percent)> onProgress = nullptr);
std::string process(const std::string& text);
void start();
void stop();
void removeDependencies();

}
