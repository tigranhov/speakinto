#include "processor.h"
#include "processor_llm.h"

namespace processor {

bool isReady() {
    return processor_llm::isReady();
}

bool ensureDependencies(std::function<void(int percent)> onProgress) {
    return processor_llm::ensureDependencies(onProgress);
}

std::string process(const std::string& text) {
    if (!isReady()) return text;
    auto result = processor_llm::process(text);
    return result.empty() ? text : result;
}

void start() {
    processor_llm::start();
}

void stop() {
    processor_llm::stop();
}

void removeDependencies() {
    processor_llm::stop();
    processor_llm::removeDependencies();
}

}
