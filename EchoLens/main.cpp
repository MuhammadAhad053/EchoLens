////// Combined: Gemini entity extraction (from trial.cpp) + Lexbor-based fact extraction & classification (from categorized.cpp)
// Single-file program. Requires:
//  - libcurl
//  - lexbor (headers + library)
//  - nlohmann/json.hpp in include path
// Build (example):
// g++ -std=c++17 -O2 -o integrated_search combined.cpp -lcurl -llexbor ...(link lexbor libs as needed)
//
// NOTE: Replace GEMINI_API_KEY, GOOGLE_CSE_API_KEY and GOOGLE_CX with your own keys/IDs.

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <algorithm>
#include <future>
#include <chrono>
#include <cctype>
#include <cstdio>

#include <curl/curl.h>
#include "nlohmann/json.hpp"

// Lexbor headers (ensure your include path is set)
#include <lexbor/html/parser.h>
#include <lexbor/dom/interfaces/document.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/interfaces/node.h>
#include <lexbor/dom/interfaces/character_data.h>

using namespace std;
using json = nlohmann::json;

// -------------------- CONFIG --------------------
static const string GEMINI_API_KEY = "AIzaSyDH2iwDhbyDIrj7mIHjSpMbahwO6oxN6AM"; // replace
static const string GEMINI_URL = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key=" + GEMINI_API_KEY;

static const string GOOGLE_CSE_API_KEY = "AIzaSyCLUMLZaNTNeD3N1E2IJ2ODSPuLkdfj0Vo"; // replace
static const string GOOGLE_CX = "c499c8c7c5dde46d4"; // replace

static const long CURL_CONNECT_TIMEOUT = 10L;      // seconds
static const long CURL_TOTAL_TIMEOUT = 30L;        // seconds
static const size_t MAX_DOWNLOAD_BYTES = 8 * 1024 * 1024; // 8 MB
static const size_t MAX_CONTENT_CHARS = 20000;    // truncate extracted content
static const size_t MAX_SCAN_TOKENS = 2000;
static const chrono::seconds LEXBOR_TIMEOUT(20);

// -------------------- Utilities --------------------
struct CurlBuffer {
    string data;
    size_t maxBytes = MAX_DOWNLOAD_BYTES;
};

static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    if (!userp) return 0;
    CurlBuffer* buf = static_cast<CurlBuffer*>(userp);
    if (buf->data.size() + realsize > buf->maxBytes) {
        return 0; // will cause cURL write error
    }
    buf->data.append(static_cast<char*>(contents), realsize);
    return realsize;
}

static size_t WriteCallbackSimple(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    if (!userp) return 0;
    pair<string*, size_t>* wd = static_cast<pair<string*, size_t>*>(userp);
    string* buffer = wd->first;
    size_t maxBytes = wd->second;
    if (buffer->size() + realsize > maxBytes) return 0;
    buffer->append(static_cast<char*>(contents), realsize);
    return realsize;
}

static string urlEncode(const string& str) {
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

static string trimPunctEdges(const string& s) {
    size_t i = 0, j = s.size();
    while (i < j && ispunct((unsigned char)s[i])) ++i;
    while (j > i && ispunct((unsigned char)s[j - 1])) --j;
    if (i >= j) return string();
    return s.substr(i, j - i);
}

static string toLowerStr(const string& s) {
    string r(s);
    transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return (char)tolower(c); });
    return r;
}

static bool containsAnyKeywordCaseInsensitive(const string& text, const vector<string>& keywords) {
    if (text.empty() || keywords.empty()) return false;
    string lowerText = toLowerStr(text);
    for (const string& kw : keywords) {
        string lowerKw = toLowerStr(kw);
        if (!lowerKw.empty() && lowerText.find(lowerKw) != string::npos) {
            return true;
        }
    }
    return false;
}

// ------------------ HTML stripping fallback ------------------
static string stripTags(const string& html) {
    string out;
    out.reserve(html.size());
    bool inTag = false;
    for (char c : html) {
        if (c == '<') inTag = true;
        else if (c == '>') inTag = false;
        else if (!inTag) {
            if (c == '\n' || c == '\r' || c == '\t') out.push_back(' ');
            else out.push_back(c);
        }
    }
    return out;
}

// ------------------ Gemeni entity extraction (from trial.cpp) ------------------
static string httpPostJson(const string& url, const json& payload, const vector<string>& extraHeaders = {}) {
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

static string httpGetSimple(const string& url, size_t maxBytes = MAX_DOWNLOAD_BYTES) {
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

static string askGeminiForEntities(const string& prompt) {
    string geminiPrompt;
    geminiPrompt += "You are an advanced NLP extractor. Given the input text, output ONLY a single JSON object (no prose, no backticks).\n";
    geminiPrompt += "The JSON keys should include: \"name\", \"department\", \"university\", \"affiliation\", \"location\", \"others\".\n";
    geminiPrompt += "Fill any field you can infer from the text; use empty string \"\" if unknown. Also include an \"original\" field with the original text.\n";
    geminiPrompt += "Example output:\n";
    geminiPrompt += "{\"name\":\"Dr. John Doe\",\"department\":\"Computer Science\",\"university\":\"Example University\",\"affiliation\":\"\",\"location\":\"\", \"others\":\"\", \"original\":\"...\"}\n";
    geminiPrompt += "Now extract from the following input text (produce valid JSON ONLY):\n";
    geminiPrompt += prompt;
    geminiPrompt += "\n";

    json body;
    body["contents"] = json::array();
    json contentObj;
    contentObj["parts"] = json::array();
    json part;
    part["text"] = geminiPrompt;
    contentObj["parts"].push_back(part);
    body["contents"].push_back(contentObj);

    vector<string> headers;
    string resp = httpPostJson(GEMINI_URL, body, headers);
    if (resp.empty()) {
        cerr << "[askGeminiForEntities] Empty response from Gemini.\n";
        return "";
    }

    try {
        auto top = json::parse(resp);
        if (top.contains("outputs") && top["outputs"].is_array() && !top["outputs"].empty()) {
            for (auto& out : top["outputs"]) {
                if (out.is_object() && out.contains("content") && out["content"].is_array()) {
                    for (auto& p : out["content"]) {
                        if (p.is_object() && p.contains("text") && p["text"].is_string()) {
                            return p["text"].get<string>();
                        }
                    }
                }
                if (out.is_object() && out.contains("text") && out["text"].is_string()) {
                    return out["text"].get<string>();
                }
            }
        }
        if (top.contains("candidates") && top["candidates"].is_array() && !top["candidates"].empty()) {
            for (auto& c : top["candidates"]) {
                if (!c.is_object()) continue;
                if (c.contains("content")) {
                    if (c["content"].is_string()) return c["content"].get<string>();
                    if (c["content"].is_object() && c["content"].contains("parts") && c["content"]["parts"].is_array()) {
                        for (auto& p : c["content"]["parts"]) {
                            if (p.is_object() && p.contains("text") && p["text"].is_string()) {
                                return p["text"].get<string>();
                            }
                        }
                    }
                }
                if (c.contains("text") && c["text"].is_string()) return c["text"].get<string>();
            }
        }
        if (top.contains("content") && top["content"].is_string()) return top["content"].get<string>();
        if (top.contains("message") && top["message"].is_string()) return top["message"].get<string>();
        return resp;
    }
    catch (...) {
        return resp;
    }
}

// Helper: extract first JSON object substring from text
static string extractFirstJsonObject(const string& text) {
    string clean = text;

    // 1. Decode common escaped sequences like "\n", "\t", etc.
    string unescaped;
    unescaped.reserve(clean.size());
    for (size_t i = 0; i < clean.size(); ++i) {
        if (clean[i] == '\\' && i + 1 < clean.size()) {
            char next = clean[i + 1];
            if (next == 'n') { unescaped.push_back('\n'); ++i; continue; }
            if (next == 't') { unescaped.push_back('\t'); ++i; continue; }
            if (next == 'r') { unescaped.push_back('\r'); ++i; continue; }
            if (next == '\\') { unescaped.push_back('\\'); ++i; continue; }
            if (next == '"') { unescaped.push_back('"'); ++i; continue; }
        }
        unescaped.push_back(clean[i]);
    }
    clean = unescaped;

    // 2. Remove Markdown-style ```json or ``` fences if present
    size_t fencePos = clean.find("```");
    if (fencePos != string::npos) {
        size_t endFence = clean.find("```", fencePos + 3);
        if (endFence != string::npos) {
            clean = clean.substr(fencePos + 3, endFence - (fencePos + 3));
        }
    }

    // 3. Trim whitespace
    size_t startTrim = clean.find_first_not_of(" \n\r\t");
    if (startTrim != string::npos) clean = clean.substr(startTrim);
    size_t endTrim = clean.find_last_not_of(" \n\r\t");
    if (endTrim != string::npos) clean = clean.substr(0, endTrim + 1);

    // 4. Find first JSON object {...}
    size_t start = clean.find('{');
    if (start == string::npos) return "";
    int depth = 0;
    for (size_t i = start; i < clean.size(); ++i) {
        if (clean[i] == '{') ++depth;
        else if (clean[i] == '}') {
            --depth;
            if (depth == 0) {
                return clean.substr(start, i - start + 1);
            }
        }
    }
    return "";
}



static json parseGeminiJson(const string& geminiText) {
    json out;
    out["name"] = "";
    out["department"] = "";
    out["university"] = "";
    out["affiliation"] = "";
    out["location"] = "";
    out["others"] = "";
    out["original"] = geminiText;

    string maybeJson = extractFirstJsonObject(geminiText);
    if (!maybeJson.empty()) {
        try {
            json parsed = json::parse(maybeJson);
            for (auto& k : { "name","department","university","affiliation","location","others","original" }) {
                if (parsed.contains(k) && parsed[k].is_string()) out[k] = parsed[k].get<string>();
            }
            return out;
        }
        catch (...) {
            // fall through to heuristics
        }
    }

    istringstream iss(geminiText);
    string line;
    auto trim = [](string& s) {
        size_t a = 0; while (a < s.size() && isspace((unsigned char)s[a])) ++a;
        size_t b = s.size(); while (b > a && isspace((unsigned char)s[b - 1])) --b;
        if (a >= b) return string();
        return s.substr(a, b - a);
        };
    while (getline(iss, line)) {
        string lower = line;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        size_t col = line.find(':');
        if (col == string::npos) continue;
        string key = trim(lower.substr(0, col));
        string val = trim(line.substr(col + 1));
        if (key.find("name") != string::npos && out["name"].get<string>().empty()) out["name"] = val;
        else if ((key.find("dept") != string::npos || key.find("department") != string::npos) && out["department"].get<string>().empty()) out["department"] = val;
        else if ((key.find("univ") != string::npos || key.find("university") != string::npos) && out["university"].get<string>().empty()) out["university"] = val;
        else if (key.find("affil") != string::npos && out["affiliation"].get<string>().empty()) out["affiliation"] = val;
        else if (key.find("loc") != string::npos && out["location"].get<string>().empty()) out["location"] = val;
        else if (key.find("other") != string::npos && out["others"].get<string>().empty()) out["others"] = val;
    }

    return out;
}

// ------------------ categorized.cpp engines (Lexbor, scanning, scoring) ------------------

// Contact and fact structures
struct ExtractedFact {
    string category;
    string value;
    string sourceURL;
};

// category -> list of trigger keywords (lowercase)
static const map<string, vector<string>> keywordCategoryMap = {
    {"Designation", {"professor", "lecturer", "assistant professor", "associate professor", "assistant", "associate", "hod", "head of", "dean", "chair", "faculty", "postdoc", "researcher", "instructor"}},
    {"Department", {"department", "dept.", "csit", "computer science", "computer & it", "computer science & it", "informatics", "electrical", "mechanical", "mathematics", "physics"}},
    {"Education", {"phd", "ph.d", "doctorate", "ms", "msc", "m.sc", "bs", "bsc", "b.s.", "b.s", "degree", "graduat", "master", "bachelor"}},
    {"Research Interest", {"research", "interest", "specializ", "specialise", "focus", "quantum", "cryptography", "iot", "machine learning", "deep learning", "computer vision"}},
    {"Timeline", {"joined", "appointed", "since", "from", "started", "began", "effective", "onward", "promoted", "served as"}},
    {"Family", {"son of", "s/o", "father", "mother", "parents", "parent", "wife", "husband"}},
    {"Profile Links", {"google scholar", "scholar.google", "researchgate", "linkedin", "orcid", "cv", "resume"}},
    {"Honors/Awards", {"award", "fellow", "honor", "distinction", "prize", "awardee"}}
};

static pair<string, int> scoreLineAgainstCategories(const string& line) {
    string low = toLowerStr(line);
    int bestScore = 0;
    string bestCategory;
    for (const auto& p : keywordCategoryMap) {
        int score = 0;
        for (const auto& kw : p.second) {
            if (low.find(kw) != string::npos) {
                score += (int)kw.size() / 4 + 1;
            }
        }
        if (score > bestScore) {
            bestScore = score;
            bestCategory = p.first;
        }
    }
    return { bestCategory, bestScore };
}

static vector<string> splitIntoCandidateLines(const string& content) {
    vector<string> lines;
    if (content.empty()) return lines;
    string cur;
    cur.reserve(256);
    for (size_t i = 0; i < content.size(); ++i) {
        char c = content[i];
        cur.push_back(c);
        if (c == '.' || c == '?' || c == '!' || c == '\n' || c == ';') {
            size_t a = 0, b = cur.size();
            while (a < b && isspace((unsigned char)cur[a])) ++a;
            while (b > a && isspace((unsigned char)cur[b - 1])) --b;
            if (b > a) lines.push_back(cur.substr(a, b - a));
            cur.clear();
        }
        if (cur.size() > 1000) {
            lines.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) {
        size_t a = 0, b = cur.size();
        while (a < b && isspace((unsigned char)cur[a])) ++a;
        while (b > a && isspace((unsigned char)cur[b - 1])) --b;
        if (b > a) lines.push_back(cur.substr(a, b - a));
    }
    return lines;
}

// scan content for facts and append to knowledgeBase
static void scanForFacts(const string& content, const string& sourceURL, vector<ExtractedFact>& knowledgeBase, const string& name) {
    if (content.empty()) return;
    vector<string> lines = splitIntoCandidateLines(content);
    for (const auto& line : lines) {
        if (line.size() < 6) continue;
        auto scored = scoreLineAgainstCategories(line);
        string category = scored.first;
        int score = scored.second;
        bool accept = false;
        if (score >= 2) accept = true;
        if (!name.empty()) {
            string lowerLine = toLowerStr(line);
            string lowerName = toLowerStr(name);
            if (lowerLine.find(lowerName) != string::npos) {
                accept = true;
                if (category.empty()) category = "Misc";
            }
        }
        if (accept) {
            string cat = category.empty() ? "Misc" : category;
            ExtractedFact f{ cat, line, sourceURL };
            knowledgeBase.push_back(f);
        }
        else {
            string low = toLowerStr(line);
            if (low.find("phd") != string::npos || low.find("ph.d") != string::npos || low.find("cv") != string::npos) {
                ExtractedFact f{ "Education", line, sourceURL };
                knowledgeBase.push_back(f);
            }
        }
    }
}

static bool isNearName(const string& html, size_t position, const string& name, int vicinity = 200) {
    if (name.empty()) return true;
    string lowerHtml = toLowerStr(html);
    string lowerName = toLowerStr(name);
    size_t namePos = lowerHtml.find(lowerName);
    if (namePos == string::npos) return false;
    return (position >= (namePos > vicinity ? namePos - vicinity : 0) &&
        position <= namePos + lowerName.size() + vicinity);
}

// ------------------ Fast raw HTML scan for mailto: / tel: ------------------
static vector<string> scanHTMLForMailtoTel(const string& html, const string& name = "") {
    vector<string> results;
    if (html.empty()) return results;

    string lowerHtml = toLowerStr(html);
    string lowerName = toLowerStr(name);

    // --- Step 1: find all occurrences of the name ---
    vector<size_t> namePositions;
    if (!lowerName.empty()) {
        size_t np = lowerHtml.find(lowerName);
        while (np != string::npos) {
            namePositions.push_back(np);
            np = lowerHtml.find(lowerName, np + lowerName.size());
        }
    }

    // --- Step 2: if no name, process whole HTML (fallback) ---
    if (namePositions.empty()) namePositions.push_back(0);

    const int vicinity = 800; // search radius around each name

    // --- Step 3: scan around each name occurrence ---
    for (size_t np : namePositions) {
        size_t start = (np > (size_t)vicinity ? np - vicinity : 0);
        size_t end = min(html.size(), np + lowerName.size() + vicinity);
        string segment = html.substr(start, end - start);
        string lowerSegment = toLowerStr(segment);

        // --- 3a. mailto: links ---
        size_t pos = 0;
        while (true) {
            size_t mpos = lowerSegment.find("mailto:", pos);
            if (mpos == string::npos) break;

            size_t startMail = mpos + 7;
            size_t endMail = startMail;
            while (endMail < segment.size() &&
                !isspace((unsigned char)segment[endMail]) &&
                segment[endMail] != '"' &&
                segment[endMail] != '\'' &&
                segment[endMail] != '>') endMail++;

            string candidate = trimPunctEdges(segment.substr(startMail, endMail - startMail));
            if (!candidate.empty() && candidate.find('@') != string::npos)
                results.push_back("Email: " + candidate);

            pos = endMail;
        }

        // --- 3b. plain '@' text emails ---
        pos = 0;
        while (true) {
            size_t atPos = segment.find('@', pos);
            if (atPos == string::npos) break;

            // find left boundary
            size_t left = atPos;
            while (left > 0 &&
                (isalnum((unsigned char)segment[left - 1]) ||
                    segment[left - 1] == '.' ||
                    segment[left - 1] == '_' ||
                    segment[left - 1] == '-'))
                left--;

            // find right boundary
            size_t right = atPos + 1;
            while (right < segment.size() &&
                (isalnum((unsigned char)segment[right]) ||
                    segment[right] == '.' ||
                    segment[right] == '-'))
                right++;

            string candidate = trimPunctEdges(segment.substr(left, right - left));
            if (candidate.find('@') != string::npos)
                results.push_back("Email: " + candidate);

            pos = right;
        }

        // --- 3c. tel: links ---
        pos = 0;
        while (true) {
            size_t tpos = lowerSegment.find("tel:", pos);
            if (tpos == string::npos) break;

            size_t startTel = tpos + 4;
            size_t endTel = startTel;
            while (endTel < segment.size() &&
                !isspace((unsigned char)segment[endTel]) &&
                segment[endTel] != '"' &&
                segment[endTel] != '\'' &&
                segment[endTel] != '>') endTel++;

            string candidate = trimPunctEdges(segment.substr(startTel, endTel - startTel));
            if (!candidate.empty())
                results.push_back("Phone: " + candidate);

            pos = endTel;
        }
    }

    // --- Step 4: deduplicate results ---
    sort(results.begin(), results.end());
    results.erase(unique(results.begin(), results.end()), results.end());
    return results;
}

// ------------------ Contact detection helpers ------------------
static bool looksLikeEmail(const string& token) {
    auto atPos = token.find('@');
    if (atPos == string::npos) return false;
    if (atPos == 0 || atPos + 1 >= token.size()) return false;
    string local = token.substr(0, atPos);
    string domain = token.substr(atPos + 1);
    if (local.empty() || domain.empty()) return false;
    auto dotPos = domain.find('.');
    if (dotPos == string::npos) return false;
    if (dotPos == 0 || dotPos + 1 >= domain.size()) return false;
    if (count(token.begin(), token.end(), '@') != 1) return false;
    if (token.size() < 5 || token.size() > 254) return false;
    return true;
}

static bool looksLikeStrictPhone(const string& token) {
    if (token.empty()) return false;
    if (token.size() > 40) return false;
    size_t idx = 0;
    if (token[0] == '+') {
        idx = 1;
        if (idx >= token.size()) return false;
    }
    else if (!isdigit((unsigned char)token[0])) {
        return false;
    }
    int digits = 0;
    for (char c : token) {
        if (isdigit((unsigned char)c)) ++digits;
        else if (c == '+' || c == '-' || c == ' ' || c == '(' || c == ')') continue;
        else return false;
    }
    return (digits >= 7 && digits <= 15);
}

// Token-based hybrid detector (strict)
static vector<string> extractContactsTokenBased(const string& content, const string& name) {
    vector<string> results;
    if (content.empty()) return results;
    vector<string> tokens;
    tokens.reserve(512);
    {
        istringstream iss(content);
        string tok;
        while (iss >> tok) {
            string trimmed = trimPunctEdges(tok);
            if (!trimmed.empty()) tokens.push_back(trimmed);
            if (tokens.size() >= MAX_SCAN_TOKENS) break;
        }
    }
    if (tokens.empty()) return results;
    set<string> foundSet;
    vector<string> lowerTokens;
    lowerTokens.reserve(tokens.size());
    for (auto& t : tokens) lowerTokens.push_back(toLowerStr(t));

    for (size_t i = 0; i < tokens.size(); ++i) {
        string tok = tokens[i];
        string low = lowerTokens[i];

        if (low.rfind("mailto:", 0) == 0) {
            string email = tok.substr(7);
            email = trimPunctEdges(email);
            if (looksLikeEmail(email)) foundSet.insert(string("Email: ") + email);
            continue;
        }
        if (low.rfind("tel:", 0) == 0) {
            string phone = tok.substr(4);
            phone = trimPunctEdges(phone);
            if (looksLikeStrictPhone(phone)) foundSet.insert(string("Phone: ") + phone);
            continue;
        }
        if (tok.find('@') != string::npos) {
            string candidate = trimPunctEdges(tok);
            if (looksLikeEmail(candidate)) foundSet.insert(string("Email: ") + candidate);
            continue;
        }
        string candidate = trimPunctEdges(tok);
        if (!candidate.empty() && looksLikeStrictPhone(candidate)) {
            foundSet.insert(string("Phone: ") + candidate);
            continue;
        }
    }
    for (auto& s : foundSet) results.push_back(s);
    return results;
}

// ------------------ Lexbor-based extraction ------------------
static string extractTextFromHTML_lexbor(const string& html) {
    lxb_status_t status;
    lxb_html_document_t* document = lxb_html_document_create();
    if (!document) return "";

    status = lxb_html_document_parse(document,
        (const lxb_char_t*)html.c_str(),
        html.size());
    if (status != LXB_STATUS_OK) {
        lxb_html_document_destroy(document);
        return "";
    }

    lxb_html_body_element_t* body_html = lxb_html_document_body_element(document);
    if (!body_html) {
        lxb_html_document_destroy(document);
        return "";
    }

    lxb_dom_element_t* body_el = lxb_dom_interface_element(body_html);
    string extracted;

    vector<lxb_dom_node_t*> stack;
    lxb_dom_node_t* start_node = lxb_dom_interface_node(body_el);
    if (start_node) stack.push_back(start_node);

    set<lxb_dom_node_t*> visited;
    while (!stack.empty()) {
        lxb_dom_node_t* node = stack.back();
        stack.pop_back();
        if (!node) continue;
        if (visited.find(node) != visited.end()) continue;
        visited.insert(node);

        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t* el = lxb_dom_interface_element(node);
            if (!el) continue;
            if (el) {
                size_t name_len = 0;
                const lxb_char_t* tag_name = lxb_dom_element_local_name(el, &name_len);
                if (tag_name && name_len > 0) {
                    string tag((const char*)tag_name, name_len);
                    for (auto& c : tag) c = (char)tolower(c);
                    if (tag == "script" || tag == "style" || tag == "noscript" ||
                        tag == "nav" || tag == "header" || tag == "footer" ||
                        tag == "aside" || tag == "button" || tag == "form") {
                        continue;
                    }
                }
            }
        }

        if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
            size_t len = 0;
            const lxb_char_t* data = lxb_dom_node_text_content(node, &len);
            if (data && len > 0) {
                string text(reinterpret_cast<const char*>(data), len);
                auto start = text.find_first_not_of(" \t\r\n");
                auto end = text.find_last_not_of(" \t\r\n");
                if (start != string::npos && end != string::npos) {
                    string trimmed = text.substr(start, end - start + 1);
                    if (trimmed.length() > 2) {
                        extracted.append(trimmed);
                        extracted.push_back(' ');
                    }
                }
            }
        }

        if (node->last_child) {
            for (lxb_dom_node_t* child = node->last_child; child != nullptr; child = child->prev) {
                stack.push_back(child);
            }
        }
    }

    lxb_html_document_destroy(document);
    return extracted;
}

static string extractTextFromHTML_withTimeout(const string& html, chrono::seconds timeout) {
    try {
        auto fut = std::async(std::launch::async, [&html]() -> string {
            try {
                return extractTextFromHTML_lexbor(html);
            }
            catch (...) {
                return string();
            }
            });

        if (fut.wait_for(timeout) == future_status::ready) {
            return fut.get();
        }
        else {
            return string();
        }
    }
    catch (...) {
        return string();
    }
}

// ------------------ Fetch page with guards ------------------
static string fetchPage(const string& url, long connectTimeout = CURL_CONNECT_TIMEOUT, long totalTimeout = CURL_TOTAL_TIMEOUT, size_t maxBytes = MAX_DOWNLOAD_BYTES) {
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

// --- existing includes and code above remain unchanged ---

// ------------------ Summary printer ------------------
static void printSummary(const vector<ExtractedFact>& knowledgeBase) {
    if (knowledgeBase.empty()) {
        std::cout << "[SUMMARY] No facts extracted.\n";
        return;
    }
    map<string, vector<pair<string, string>>> grouped;
    for (const auto& f : knowledgeBase) {
        grouped[f.category].push_back({ f.value, f.sourceURL });
    }

    std::cout << "\n===== Extracted Knowledge Summary =====\n";
    for (const auto& p : grouped) {
        std::cout << "\n-> " << p.first << ":\n";
        set<string> seen;
        for (const auto& valsrc : p.second) {
            string val = valsrc.first;
            string src = valsrc.second;
            if (seen.find(val) != seen.end()) continue;
            seen.insert(val);
            std::cout << "   - " << val << "  (source: " << src << ")\n";
        }
    }
    std::cout << "\n=======================================\n";
}

// ------------------ extractVicinityText ------------------
static string extractVicinityText(const string& content, const string& name, int radius) {
    if (name.empty() || content.empty()) return content;
    string lowerContent = toLowerStr(content);
    string lowerName = toLowerStr(name);
    size_t pos = lowerContent.find(lowerName);
    if (pos == string::npos)
        return content.substr(0, min<size_t>(content.size(), MAX_CONTENT_CHARS));

    size_t start = (pos > (size_t)radius) ? pos - radius : 0;
    size_t end = min(content.size(), pos + name.size() + radius);
    return content.substr(start, end - start);
}

// ------------------ NEW: Gemini Refinement Layer ------------------
static json refineAndValidateWithGemini(const vector<ExtractedFact>& kb) {
    if (kb.empty()) {
        cerr << "[Refinement] Knowledge base empty. Skipping refinement.\n";
        return json();
    }

    stringstream ss;
    ss << "You are a fact validator and refiner.\n";
    ss << "Below is a list of extracted facts about a person from various web sources.\n";
    ss << "Each fact has a category, raw value, and source.\n";
    ss << "Your task: clean, deduplicate, validate, and merge them into a single structured JSON.\n";
    ss << "If facts are incomplete or redundant, fix them. If something important seems missing, infer it logically.\n";
    ss << "Return JSON only, with keys: name, department, university, designation, contact_emails, contact_phones, research_interests, education, awards, location, and others.\n";
    ss << "Facts:\n";
    for (const auto& f : kb)
        ss << "- [" << f.category << "] " << f.value << " (source: " << f.sourceURL << ")\n";

    string resp = askGeminiForEntities(ss.str());
    if (resp.empty()) {
        cerr << "[Refinement] Gemini returned empty response.\n";
        return json();
    }

    string jsonPart = extractFirstJsonObject(resp);
    if (jsonPart.empty()) {
        cerr << "[Refinement] No JSON object found in Gemini response. Returning raw text.\n";
        json fallback;
        fallback["raw_response"] = resp;
        return fallback;
    }

    try {
        json refined = json::parse(jsonPart);
        std::cout << "[Refinement] Gemini refinement successful.\n";
        return refined;
    }
    catch (...) {
        cerr << "[Refinement] Failed to parse Gemini JSON. Returning raw text.\n";
        json fallback;
        fallback["raw_response"] = resp;
        return fallback;
    }
}

// ------------------ MAIN ------------------
int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    cout << "Enter prompt (one line)\n> ";
    string prompt;
    getline(cin, prompt);
    if (prompt.empty()) {
        cerr << "No prompt provided. Exiting.\n";
        return 1;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    // 1) Ask Gemini for entities
    cout << "[1/5] Sending prompt to Gemini for entity extraction...\n";
    string geminiResp = askGeminiForEntities(prompt);
    if (geminiResp.empty()) {
        cerr << "[WARN] Gemini response empty. Continuing with raw prompt as fallback.\n";
        geminiResp = prompt;
    }
    else {
        cout << "[OK] Received response from Gemini.\n";
    }

    json geminiParsed = parseGeminiJson(geminiResp);
    string name = geminiParsed.value("name", "");
    string department = geminiParsed.value("department", "");
    string university = geminiParsed.value("university", "");
    string affiliation = geminiParsed.value("affiliation", "");
    string location = geminiParsed.value("location", "");
    string others = geminiParsed.value("others", "");

    cout << "[Parsed Entities]\n";
    cout << " name: " << name << "\n";
    cout << " department: " << department << "\n";
    cout << " university: " << university << "\n";
    cout << " affiliation: " << affiliation << "\n";
    cout << " location: " << location << "\n";
    cout << " others: " << others << "\n";

    // 2) Build search query
    string query;
    if (!name.empty()) query += name + " ";
    if (!department.empty()) query += department + " ";
    if (!university.empty()) query += university + " ";
    if (!affiliation.empty()) query += affiliation + " ";
    if (!location.empty()) query += location + " ";
    if (!others.empty()) query += others + " ";
    if (query.empty()) query = prompt;
    while (!query.empty() && isspace((unsigned char)query.back())) query.pop_back();

    cout << "[2/5] Searching the web (Google Custom Search) for: " << query << "\n";
    string enc = urlEncode(query);
    string apiUrl = "https://www.googleapis.com/customsearch/v1?q=" + enc + "&key=" + GOOGLE_CSE_API_KEY + "&cx=" + GOOGLE_CX + "&num=8";

    cout << "[INFO] Performing Google CSE request...\n";
    string apiResponse = httpGetSimple(apiUrl, MAX_DOWNLOAD_BYTES);
    if (apiResponse.empty()) {
        cerr << "[ERROR] Google CSE request failed or returned empty. Exiting.\n";
        curl_global_cleanup();
        return 1;
    }

    vector<pair<string, string>> results; // (title, link)
    try {
        json j = json::parse(apiResponse);
        if (j.contains("error")) {
            cerr << "[Google API ERROR] " << j["error"].dump() << "\n";
            curl_global_cleanup();
            return 1;
        }
        if (!j.contains("items") || !j["items"].is_array()) {
            cerr << "[INFO] No search results returned.\n";
        }
        else {
            for (auto& it : j["items"]) {
                string title = it.value("title", string());
                string link = it.value("link", string());
                if (!link.empty()) results.push_back({ title, link });
            }
        }
    }
    catch (const std::exception& e) {
        cerr << "[ERROR] Failed to parse Google API JSON: " << e.what() << "\n";
        curl_global_cleanup();
        return 1;
    }

    if (results.empty()) {
        cout << "[INFO] No search results to process.\n";
        curl_global_cleanup();
        return 0;
    }

    cout << "[3/5] Fetching and extracting facts from top results...\n";
    vector<ExtractedFact> knowledgeBase;
    int processed = 0;
    vector<string> keywordFilters = { "email", "contact", "phone", "@" };

    for (auto& res : results) {
        if (processed >= 3) break; // process up to 3 results by default
        string title = res.first;
        string link = res.second;
        cout << "\nResult " << (processed + 1) << ": " << title << "\n";
        cout << " Link: " << link << "\n";
        cout << " Fetching page...\n";
        string pageHtml = fetchPage(link, CURL_CONNECT_TIMEOUT, CURL_TOTAL_TIMEOUT, MAX_DOWNLOAD_BYTES);
        if (pageHtml.empty()) {
            cout << "  [WARN] failed to fetch page or empty content. Skipping.\n";
            ++processed;
            continue;
        }

        auto rawFound = scanHTMLForMailtoTel(pageHtml, name);
        if (!rawFound.empty()) {
            bool nameFoundRaw = false;
            if (!name.empty()) nameFoundRaw = toLowerStr(pageHtml).find(toLowerStr(name)) != string::npos;
            string conf = nameFoundRaw ? "[HIGH CONFIDENCE]" : "[LOW CONFIDENCE]";
            cout << "  [RAWSCAN] Found contacts in HTML attributes:\n";
            for (auto& s : rawFound) {
                cout << "     " << s << " " << conf << "\n";
                ExtractedFact f;
                if (s.rfind("Email:", 0) == 0) { f.category = "Contact/Email"; f.value = s.substr(7); }
                else if (s.rfind("Phone:", 0) == 0) { f.category = "Contact/Phone"; f.value = s.substr(7); }
                else { f.category = "Contact"; f.value = s; }
                f.sourceURL = link;
                knowledgeBase.push_back(f);
            }
        }

        if (!containsAnyKeywordCaseInsensitive(pageHtml, keywordFilters)) {
            cout << "  [SKIP] No contact-related keywords found (email/contact/phone/@). Running lighter extraction.\n";
            string contentFallback = stripTags(pageHtml);
            if (contentFallback.size() > MAX_CONTENT_CHARS) contentFallback = contentFallback.substr(0, MAX_CONTENT_CHARS);
            scanForFacts(contentFallback, link, knowledgeBase, name + " " + university + " " + department);
            ++processed;
            continue;
        }
        else {
            cout << "  [INFO] Contact-related keywords found — running Lexbor parsing...\n";
        }

        string content = extractTextFromHTML_withTimeout(pageHtml, LEXBOR_TIMEOUT);
        if (content.empty()) {
            cout << "  [WARN] Lexbor parse timed out/failed. Using fallback stripper.\n";
            content = stripTags(pageHtml);
        }
        else {
            cout << "  [OK] Lexbor returned content (" << content.size() << " chars)\n";
        }

        if (content.size() > MAX_CONTENT_CHARS) {
            cout << "  [INFO] Truncating extracted content to " << MAX_CONTENT_CHARS << " characters for safe scanning.\n";
            content = content.substr(0, MAX_CONTENT_CHARS);
        }

        string focusedContent = extractVicinityText(content, name, 300);
        scanForFacts(focusedContent, link, knowledgeBase, name + " " + university + " " + department);

        auto contacts = extractContactsTokenBased(content, name + " " + university + " " + department);
        for (auto& s : contacts) {
            ExtractedFact f;
            if (s.rfind("Phone:", 0) == 0) { f.category = "Contact/Phone"; f.value = s.substr(7); }
            f.sourceURL = link;
            knowledgeBase.push_back(f);
        }

        cout << "  [INFO] Extraction finished for this URL. Results added to knowledge base.\n";
        ++processed;
    }

    // 4) Print summary
    cout << "\n[4/5] Summary:\n";
    printSummary(knowledgeBase);

    // 5) NEW: Gemini-based refinement
    cout << "\n[5/5] Refining extracted data with Gemini...\n";
    json refined = refineAndValidateWithGemini(knowledgeBase);

    cout << "\n===== Refined Structured Output =====\n";
    if (refined.contains("raw_response"))
        // print raw Gemini text if JSON parsing failed
        cout << refined["raw_response"].get<string>() << "\n";
    else
        // print parsed JSON nicely
        cout << refined.dump(4) << "\n";
    cout << "=====================================\n";


    curl_global_cleanup();
    cout << "[DONE]\n";
    return 0;
}
