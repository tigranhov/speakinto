#pragma once
#include <string>

namespace transcriber {

// Transcribe a WAV file using whisper-cli.exe.
// Returns the transcribed text, or empty string on failure.
std::string transcribe(const std::wstring& wavPath, const std::wstring& whisperExe, const std::wstring& modelPath);

}
