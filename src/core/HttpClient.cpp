#include "HttpClient.hpp"
#include <iostream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

namespace SincroPro {

#ifdef _WIN32
// Helper to convert std::string to std::wstring
static std::wstring toWString(const std::string& str) {
    if (str.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// Helper to convert std::wstring to std::string
static std::string toString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// Simple URL Parser for WinHTTP
static bool parseUrl(const std::string& url, bool& isHttps, std::wstring& host, int& port, std::wstring& path) {
    std::string schemeStr = "http://";
    std::string secureSchemeStr = "https://";
    size_t pos = 0;
    if (url.compare(0, secureSchemeStr.length(), secureSchemeStr) == 0) {
        isHttps = true;
        pos = secureSchemeStr.length();
    } else if (url.compare(0, schemeStr.length(), schemeStr) == 0) {
        isHttps = false;
        pos = schemeStr.length();
    } else {
        return false;
    }

    size_t slashPos = url.find('/', pos);
    std::string hostPort;
    std::string pathStr;
    if (slashPos == std::string::npos) {
        hostPort = url.substr(pos);
        pathStr = "/";
    } else {
        hostPort = url.substr(pos, slashPos - pos);
        pathStr = url.substr(slashPos);
    }

    size_t colonPos = hostPort.find(':');
    std::string hostStr;
    if (colonPos == std::string::npos) {
        hostStr = hostPort;
        port = isHttps ? 443 : 80;
    } else {
        hostStr = hostPort.substr(0, colonPos);
        try {
            port = std::stoi(hostPort.substr(colonPos + 1));
        } catch (...) {
            port = isHttps ? 443 : 80;
        }
    }

    host = toWString(hostStr);
    path = toWString(pathStr);
    return true;
}
#else
// Curl write callback
static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}
#endif

std::string HttpClient::get(const std::string& url, const std::map<std::string, std::string>& headers) {
    return request("GET", url, "", headers);
}

std::string HttpClient::post(const std::string& url, const std::string& body, const std::map<std::string, std::string>& headers) {
    return request("POST", url, body, headers);
}

std::string HttpClient::put(const std::string& url, const std::string& body, const std::map<std::string, std::string>& headers) {
    return request("PUT", url, body, headers);
}

std::string HttpClient::request(const std::string& method, const std::string& url, const std::string& body, const std::map<std::string, std::string>& headers) {
#ifdef _WIN32
    bool isHttps = false;
    std::wstring host, path;
    int port = 80;

    if (!parseUrl(url, isHttps, host, port, path)) {
        std::cerr << "WinHTTP: Failed to parse URL: " << url << std::endl;
        return "";
    }

    HINTERNET hSession = WinHttpOpen(L"SincroPro/1.0", 
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                                    WINHTTP_NO_PROXY_NAME, 
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return "";
    }

    std::wstring wMethod = toWString(method);
    DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, wMethod.c_str(), path.c_str(), 
                                           NULL, WINHTTP_NO_REFERER, 
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    // Prepare headers
    std::wstring wHeaders;
    for (const auto& pair : headers) {
        wHeaders += toWString(pair.first) + L": " + toWString(pair.second) + L"\r\n";
    }

    if (!wHeaders.empty()) {
        WinHttpAddRequestHeaders(hRequest, wHeaders.c_str(), (DWORD)-1, 
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    // Send Request
    BOOL bResults = WinHttpSendRequest(hRequest, 
                                       WINHTTP_NO_ADDITIONAL_HEADERS, 0, 
                                       (LPVOID)(body.empty() ? nullptr : body.c_str()), 
                                       (DWORD)body.length(), (DWORD)body.length(), 0);

    // Receive Response
    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
    }

    std::string responseString;
    if (bResults) {
        DWORD dwSize = 0;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
                std::cerr << "WinHTTP: Error querying data availability: " << GetLastError() << std::endl;
                break;
            }

            if (dwSize == 0) break;

            std::vector<char> buffer(dwSize + 1, 0);
            DWORD dwDownloaded = 0;
            if (WinHttpReadData(hRequest, (LPVOID)&buffer[0], dwSize, &dwDownloaded)) {
                responseString.append(&buffer[0], dwDownloaded);
            } else {
                std::cerr << "WinHTTP: Error reading data: " << GetLastError() << std::endl;
                break;
            }
        } while (dwSize > 0);
    } else {
        std::cerr << "WinHTTP: Request failed with error code: " << GetLastError() << std::endl;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return responseString;
#else
    // Linux libcurl implementation
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string responseBuffer;
    struct curl_slist* chunk = nullptr;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);

    // Build headers
    for (const auto& pair : headers) {
        std::string headerStr = pair.first + ": " + pair.second;
        chunk = curl_slist_append(chunk, headerStr.c_str());
    }
    if (chunk) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    }

    if (!body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "Curl failed: " << curl_easy_strerror(res) << std::endl;
    }

    if (chunk) {
        curl_slist_free_all(chunk);
    }
    curl_easy_cleanup(curl);
    return responseBuffer;
#endif
}

} // namespace SincroPro
