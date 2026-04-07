#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include "tray.h"

namespace audio {

struct CaptureResult {
    std::vector<float> samples;
    UINT32 sampleRate;
    UINT32 channels;
};

// Enumerate available audio input devices
std::vector<tray::AudioDevice> enumerateDevices();

// Pre-initialize audio client for the given device (call at startup or after device change).
// Makes startCapture() near-instant by doing all COM work upfront.
bool prepare(const std::wstring& deviceId = L"");

// Start capturing (must call prepare() first, or will auto-prepare with default device)
bool startCapture(const std::wstring& deviceId = L"");

// Stop capturing and return the accumulated PCM samples
CaptureResult stopCapture();

// Cleanup COM resources
void cleanup();

}
