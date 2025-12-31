#pragma once

// --- Includes ---
// All headers needed from your .h and .cpp files
#include <string>
#include <vector>
#include <cstddef> // For size_t
#include <utility> // For std::pair
#include <iostream>
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <cctype>
#include <cstdio>

// --- Namespaces ---
// (Note: 'using namespace std' in a .h is generally discouraged,
// but this matches your .cpp file's style)
using namespace std;
using json = nlohmann::json;

// --- Structs ---
struct CurlBuffer {
    std::string data;
    size_t maxBytes;
};

// --- Function Definitions ---
// All functions MUST be 'inline'


inline size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    if (!userp) return 0;
    CurlBuffer* buf = static_cast<CurlBuffer*>(userp);
    if (buf->data.size() + realsize > buf->maxBytes) {
        return 0; // will cause cURL write error
    }
    buf->data.append(static_cast<char*>(contents), realsize);
    return realsize;
}

inline size_t WriteCallbackSimple(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    if (!userp) return 0;
    pair<string*, size_t>* wd = static_cast<pair<string*, size_t>*>(userp);
    string* buffer = wd->first;
    size_t maxBytes = wd->second;
    if (buffer->size() + realsize > maxBytes) return 0;
    buffer->append(static_cast<char*>(contents), realsize);
    return realsize;
}

inline string urlEncode(const string& str) {
    string encoded;
    char hex[8];
    for (unsigned char c : str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded.push_back((char)c);
        }
        else if (c == ' ') {
            encoded.push_back('+');
        }
        else {
#ifdef _MSC_VER
            sprintf_s(hex, "%%%02X", c);
#else
            snprintf(hex, sizeof(hex), "%%%02X", c);
#endif
            encoded += hex;
        }
    }
    return encoded;
}

inline string httpPostJson(const string& url, const json& payload, const vector<string>& extraHeaders) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        cerr << "[httpPostJson] curl_easy_init failed\n";
        return "";
    }
    CurlBuffer buf;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    for (const auto& h : extraHeaders) headers = curl_slist_append(headers, h.c_str());

    string payloadStr = payload.dump();

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payloadStr.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)payloadStr.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "integrated-search/1.0");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CURL_CONNECT_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, CURL_TOTAL_TIMEOUT);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        cerr << "[httpPostJson] curl error: " << curl_easy_strerror(res) << "\n";
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return "";
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return buf.data;
}

inline string httpGetSimple(const string& url, size_t maxBytes = MAX_DOWNLOAD_BYTES) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        cerr << "[httpGet] curl_easy_init failed\n";
        return "";
    }
    string response;
    pair<string*, size_t> wd{ &response, maxBytes };

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallbackSimple);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &wd);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "integrated-search/1.0");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CURL_CONNECT_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, CURL_TOTAL_TIMEOUT);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        cerr << "[httpGet] curl error: " << curl_easy_strerror(res) << "\n";
        curl_easy_cleanup(curl);
        return "";
    }
    curl_easy_cleanup(curl);
    return response;
}


inline string fetchPage(const string& url, long connectTimeout, long totalTimeout, size_t maxBytes) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        cerr << "[fetchPage] curl_easy_init failed\n";
        return "";
    }

    string buffer;
    pair<string*, size_t> wd{ &buffer, maxBytes };

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallbackSimple);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &wd);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (compatible; MyBot/1.0)");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connectTimeout);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, totalTimeout);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        if (res == CURLE_WRITE_ERROR) {
            cerr << "[fetchPage] Download aborted: exceeded max size (" << maxBytes << " bytes)\n";
        }
        else {
            cerr << "[fetchPage] cURL error for " << url << " : " << curl_easy_strerror(res) << "\n";
        }
        curl_easy_cleanup(curl);
        return "";
    }

    curl_easy_cleanup(curl);
    return buffer;
}
