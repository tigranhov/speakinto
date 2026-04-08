#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace wav {

// Resample multi-channel float PCM to 16kHz mono float32 (no file I/O).
// Returns the buffer suitable for passing directly to whisper_full().
std::vector<float> prepareForWhisper(const std::vector<float>& samples, uint32_t srcSampleRate, uint32_t srcChannels);

}
