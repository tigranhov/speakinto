#pragma once
#include <string>
#include <functional>

namespace updater {

struct UpdateInfo {
    bool available = false;
    std::string latestVersion;   // e.g. "0.4.0"
    std::string changelog;       // release body text
    std::string downloadUrl;     // browser_download_url for correct variant
    std::string htmlUrl;         // release page URL (fallback)
    std::string error;           // non-empty on failure
};

// Check GitHub Releases API for a newer version. Blocking — call from background thread.
UpdateInfo checkForUpdates();

// Download installer to %TEMP%. Returns path to downloaded file, or empty on failure.
std::wstring downloadInstaller(const std::string& url,
                               std::function<void(int percent)> onProgress = nullptr);

}
