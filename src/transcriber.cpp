#include "transcriber.h"
#include "whisper_engine.h"
#include <vector>
#include <regex>

namespace transcriber {

static WhisperEngine* g_engine = nullptr;
static std::atomic<bool> g_cancelRequested{false};

bool isCancelRequested() { return g_cancelRequested; }
void resetCancelFlag() { g_cancelRequested = false; }

void setEngine(WhisperEngine* engine) {
    g_engine = engine;
}

static std::string cleanOutput(const std::string& text) {
    std::string cleaned = text;

    static const std::vector<std::regex> patterns = {
        std::regex(R"(\[BLANK_AUDIO\])", std::regex::icase),
        std::regex(R"(\(blank audio\))", std::regex::icase),
        std::regex(R"(\[silence\])", std::regex::icase),
        std::regex(R"(\[inaudible\])", std::regex::icase),
        std::regex(R"(\[music\])", std::regex::icase),
    };

    for (auto& pattern : patterns) {
        cleaned = std::regex_replace(cleaned, pattern, "");
    }

    static const std::regex whitespace(R"(\s+)");
    cleaned = std::regex_replace(cleaned, whitespace, " ");
    size_t start = cleaned.find_first_not_of(" \t\r\n");
    size_t end = cleaned.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return cleaned.substr(start, end - start + 1);
}

TranscribeResult transcribe(const float* samples, int n_samples,
                            const std::string& language,
                            const std::string& vocabPrompt) {
    if (!g_engine || !g_engine->isLoaded()) {
        return {"", false};
    }

    auto raw = g_engine->transcribe(samples, n_samples, language, vocabPrompt);

    if (raw.empty()) {
        return {"", !g_cancelRequested};
    }

    return {cleanOutput(raw), true};
}

void cancelCurrent() {
    g_cancelRequested = true;
    if (g_engine) {
        g_engine->cancel();
    }
}

}
