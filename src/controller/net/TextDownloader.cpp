#include "TextDownloader.h"
#include <windows.h>
#include <winhttp.h>
#include "../../utils/FileUtils.h"
#include "../file/FileWriter.h"

namespace {
    constexpr int kResolveTimeoutMs = 10000;
    constexpr int kConnectTimeoutMs = 10000;
    constexpr int kSendTimeoutMs = 30000;
    constexpr int kReceiveTimeoutMs = 30000;
}

std::optional<std::string> http::download(
    FileWriter& writer, const std::stop_token& st, std::atomic<float>& outProgress, ResponseInfo& outInfo,
    const std::string& url, const std::filesystem::path& targetPath
) {
    // convert std::string url to std::wstring
    std::wstring urlWide;
    if (!url.empty()) {
        const int len = MultiByteToWideChar(CP_UTF8, 0, url.data(), static_cast<int>(url.size()), nullptr, 0);
        if (len > 0) {
            urlWide = std::wstring(static_cast<size_t>(len), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, url.data(), static_cast<int>(url.size()), urlWide.data(), len);
        }
    }
    if (urlWide.empty())
        return "Invalid URL";

    // crack url
    struct CrackedUrl {
        std::wstring host;
        std::wstring path;
        INTERNET_PORT port = 0;
        bool secure = false;
    } crackedUrl;
    {
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(urlWide.c_str(), 0, 0, &uc) || uc.dwHostNameLength == 0)
            return "Invalid URL";

        crackedUrl.host.assign(uc.lpszHostName, uc.dwHostNameLength);
        if (uc.dwUrlPathLength)
            crackedUrl.path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
        if (uc.dwExtraInfoLength)
            crackedUrl.path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
        if (crackedUrl.path.empty())
            crackedUrl.path = L"/";
        crackedUrl.port = uc.nPort;
        crackedUrl.secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    }

    struct InternetHandle {
        ~InternetHandle() { if (handle) WinHttpCloseHandle(handle); }
        const HINTERNET handle;
    };

    const InternetHandle sessionHandle(WinHttpOpen(
        L"TextReader/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0
    ));
    if (!sessionHandle.handle)
        return "Cannot initialize WinHTTP";

    // without this, the default resolve timeout is "wait forever"
    // https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpsettimeouts
    WinHttpSetTimeouts(sessionHandle.handle, kResolveTimeoutMs, kConnectTimeoutMs, kSendTimeoutMs, kReceiveTimeoutMs);

    const InternetHandle connectionHandle(WinHttpConnect(sessionHandle.handle, crackedUrl.host.c_str(), crackedUrl.port, 0));
    if (!connectionHandle.handle)
        return "Cannot connect to the server";

    const InternetHandle requestHandle(WinHttpOpenRequest(
        connectionHandle.handle, L"GET", crackedUrl.path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        crackedUrl.secure ? WINHTTP_FLAG_SECURE : 0u
    ));
    if (!requestHandle.handle)
        return "Cannot create the request";

    if (st.stop_requested())
        return std::nullopt;
    if (!WinHttpSendRequest(
        requestHandle.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA,
        0, 0, 0
    )) {
        return "Cannot reach the server";
    }
    if (st.stop_requested())
        return std::nullopt;

    if (!WinHttpReceiveResponse(requestHandle.handle, nullptr))
        return "No response from the server";

    {
        DWORD status = 0;
        DWORD size = sizeof(DWORD);
        if (!WinHttpQueryHeaders(
            requestHandle.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX
        ))
            return "Cannot read the response status";
        if (status < 200 || status >= 300)
            return "Server returned HTTP " + std::to_string(status);
    }

    // query content length
    {
        outInfo.totalBytes = 0;

        ULONGLONG length = 0;
        DWORD size = sizeof(length);
        if (WinHttpQueryHeaders(
            requestHandle.handle, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER64,
            WINHTTP_HEADER_NAME_BY_INDEX, &length, &size, WINHTTP_NO_HEADER_INDEX
        )) {
            outInfo.totalBytes = length;
        }
    }

    if (outInfo.totalBytes)
        if (auto errorOpt = utils::checkDiskSpace(targetPath, outInfo.totalBytes))
            return errorOpt;

    const auto totalFlt = static_cast<float>(outInfo.totalBytes);
    if (outInfo.totalBytes)
        outProgress.store(0.0f, std::memory_order_relaxed);

    const auto buffer = writer.getBuffer();
    const size_t bufferSize = writer.getBufferSize();
    size_t used = 0;
    uint64_t received = 0;

    while (true) {
        if (st.stop_requested())
            return std::nullopt; // cancelled - the caller throws the temp file away

        DWORD read = 0;
        if (!WinHttpReadData(requestHandle.handle, buffer + used, static_cast<DWORD>(bufferSize - used), &read))
            return "The download was interrupted";
        if (read == 0)
            break; // end

        used += read;
        received += read;

        if (used == bufferSize) {
            if (!writer.writeBuffer())
                return "Cannot write the downloading file";
            used = 0;
        }

        if (outInfo.totalBytes)
            outProgress.store(static_cast<float>(received) / totalFlt, std::memory_order_relaxed);
    }

    if (!writer.finish(used))
        return "Cannot write the downloaded file";

    if (received == 0)
        return "The server returned an empty response";

    outProgress.store(1.0f, std::memory_order_relaxed);
    return std::nullopt;
}
