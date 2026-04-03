#include "model_manager.h"
#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <fstream>
#include <cstdio>

#pragma comment(lib, "winhttp.lib")

namespace model {

static const wchar_t* MODEL_FILENAME = L"ggml-base.bin";
static const wchar_t* MODEL_HOST = L"huggingface.co";
static const wchar_t* MODEL_PATH = L"/ggerganov/whisper.cpp/resolve/main/ggml-base.bin";
static constexpr size_t MIN_MODEL_SIZE = 100 * 1024 * 1024; // 100MB minimum

static void log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    fprintf(stderr, "%s\n", buf);
}

static std::wstring getModelDir() {
    wchar_t appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        return L"";
    }
    return std::wstring(appdata) + L"\\wisper-agent\\models";
}

std::wstring getModelPath() {
    auto dir = getModelDir();
    if (dir.empty()) return L"";
    return dir + L"\\" + MODEL_FILENAME;
}

bool modelExists() {
    auto path = getModelPath();
    if (path.empty()) return false;

    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fileInfo)) {
        return false;
    }

    // Check file size (base model should be ~148MB)
    ULONGLONG size = ((ULONGLONG)fileInfo.nFileSizeHigh << 32) | fileInfo.nFileSizeLow;
    return size >= MIN_MODEL_SIZE;
}

static bool ensureDirectory(const std::wstring& dir) {
    DWORD attrs = GetFileAttributesW(dir.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) return true;

    // Create parent directories recursively
    size_t pos = dir.find_last_of(L'\\');
    if (pos != std::wstring::npos) {
        ensureDirectory(dir.substr(0, pos));
    }
    return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool downloadModel(std::function<void(int percent)> onProgress) {
    auto modelDir = getModelDir();
    auto modelPath = getModelPath();
    if (modelDir.empty() || modelPath.empty()) {
        log("Failed to determine model path");
        return false;
    }

    if (!ensureDirectory(modelDir)) {
        log("Failed to create model directory: %ls", modelDir.c_str());
        return false;
    }

    log("Downloading model to: %ls", modelPath.c_str());

    // Open WinHTTP session
    HINTERNET hSession = WinHttpOpen(L"WisperAgent/1.0",
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        log("WinHttpOpen failed: %lu", GetLastError());
        return false;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, MODEL_HOST,
                                         INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        log("WinHttpConnect failed: %lu", GetLastError());
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", MODEL_PATH,
                                             nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        log("WinHttpOpenRequest failed: %lu", GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // Follow redirects (HuggingFace redirects to CDN)
    DWORD maxRedirects = 10;
    for (DWORD redirect = 0; redirect < maxRedirects; redirect++) {
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            log("WinHttpSendRequest failed: %lu", GetLastError());
            break;
        }

        if (!WinHttpReceiveResponse(hRequest, nullptr)) {
            log("WinHttpReceiveResponse failed: %lu", GetLastError());
            break;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                            WINHTTP_NO_HEADER_INDEX);

        if (statusCode == 200) break;

        if (statusCode == 301 || statusCode == 302 || statusCode == 307) {
            wchar_t newUrl[2048] = {};
            DWORD urlSize = sizeof(newUrl);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION,
                                WINHTTP_HEADER_NAME_BY_INDEX, newUrl, &urlSize,
                                WINHTTP_NO_HEADER_INDEX);

            log("Redirect to: %ls", newUrl);

            // Parse the redirect URL
            URL_COMPONENTS urlComp = {};
            urlComp.dwStructSize = sizeof(urlComp);
            wchar_t hostBuf[256] = {};
            wchar_t pathBuf[2048] = {};
            urlComp.lpszHostName = hostBuf;
            urlComp.dwHostNameLength = 256;
            urlComp.lpszUrlPath = pathBuf;
            urlComp.dwUrlPathLength = 2048;

            if (!WinHttpCrackUrl(newUrl, 0, 0, &urlComp)) break;

            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);

            hConnect = WinHttpConnect(hSession, hostBuf, urlComp.nPort, 0);
            if (!hConnect) break;

            DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
            hRequest = WinHttpOpenRequest(hConnect, L"GET", pathBuf,
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
            if (!hRequest) break;
            continue;
        }

        log("Unexpected status code: %lu", statusCode);
        break;
    }

    // Get content length
    DWORD contentLength = 0;
    DWORD clSize = sizeof(contentLength);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &clSize,
                        WINHTTP_NO_HEADER_INDEX);

    log("Content length: %lu bytes", contentLength);

    // Download to temp file first, then rename
    std::wstring tempPath = modelPath + L".tmp";
    std::ofstream outFile(tempPath, std::ios::binary);
    if (!outFile.is_open()) {
        log("Failed to create temp file: %ls", tempPath.c_str());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD totalRead = 0;
    int lastPercent = -1;
    char buffer[65536];
    DWORD bytesRead = 0;
    bool success = true;

    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        outFile.write(buffer, bytesRead);
        totalRead += bytesRead;

        if (contentLength > 0 && onProgress) {
            int percent = (int)((ULONGLONG)totalRead * 100 / contentLength);
            if (percent != lastPercent) {
                lastPercent = percent;
                onProgress(percent);
            }
        }
    }

    outFile.close();

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    // Verify size
    if (totalRead < MIN_MODEL_SIZE) {
        log("Downloaded file too small: %lu bytes", totalRead);
        DeleteFileW(tempPath.c_str());
        return false;
    }

    // Rename temp to final
    DeleteFileW(modelPath.c_str()); // Remove old if exists
    if (!MoveFileW(tempPath.c_str(), modelPath.c_str())) {
        log("Failed to rename temp file: %lu", GetLastError());
        DeleteFileW(tempPath.c_str());
        return false;
    }

    log("Model downloaded successfully: %lu bytes", totalRead);
    return true;
}

}
