#include "wav_writer.h"

namespace wav {

static constexpr uint32_t TARGET_SAMPLE_RATE = 16000;

// Downmix to mono by averaging channels
static std::vector<float> toMono(const std::vector<float>& samples, uint32_t channels) {
    if (channels == 1) return samples;
    size_t frameCount = samples.size() / channels;
    std::vector<float> mono(frameCount);
    for (size_t i = 0; i < frameCount; i++) {
        float sum = 0.0f;
        for (uint32_t ch = 0; ch < channels; ch++) {
            sum += samples[i * channels + ch];
        }
        mono[i] = sum / (float)channels;
    }
    return mono;
}

// Simple linear interpolation resampling
static std::vector<float> resample(const std::vector<float>& input, uint32_t srcRate, uint32_t dstRate) {
    if (srcRate == dstRate) return input;
    double ratio = (double)srcRate / (double)dstRate;
    size_t outLen = (size_t)((double)input.size() / ratio);
    std::vector<float> output(outLen);
    for (size_t i = 0; i < outLen; i++) {
        double srcIdx = (double)i * ratio;
        size_t idx0 = (size_t)srcIdx;
        size_t idx1 = idx0 + 1;
        if (idx1 >= input.size()) idx1 = input.size() - 1;
        double frac = srcIdx - (double)idx0;
        output[i] = (float)((1.0 - frac) * input[idx0] + frac * input[idx1]);
    }
    return output;
}

std::vector<float> prepareForWhisper(const std::vector<float>& samples, uint32_t srcSampleRate, uint32_t srcChannels) {
    if (samples.empty()) return {};
    auto mono = toMono(samples, srcChannels);
    return resample(mono, srcSampleRate, TARGET_SAMPLE_RATE);
}

}
