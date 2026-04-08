#pragma once
#include <string>
#include <atomic>

class WhisperEngine;

namespace transcriber {

struct TranscribeResult {
    std::string text;
    bool ok;
};

// Set the engine used by transcribe(). Must be called before first transcription.
void setEngine(WhisperEngine* engine);

// Transcribe 16kHz mono float32 samples using the WhisperEngine.
TranscribeResult transcribe(const float* samples, int n_samples,
                            const std::string& language,
                            const std::string& vocabPrompt);

// Cancel the running transcription (safe to call from any thread).
void cancelCurrent();

bool isCancelRequested();
void resetCancelFlag();

}
