#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace wav {

// Resample multi-channel float PCM to 16kHz mono and write as 16-bit WAV file.
// Returns the path to the temp file, or empty string on failure.
std::wstring writeTemp(const std::vector<float>& samples, uint32_t srcSampleRate, uint32_t srcChannels);

}
