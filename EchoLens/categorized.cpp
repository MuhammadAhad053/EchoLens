//// attempt_fact_engine.cpp
////
//// Full patched program integrating:
////  - multi-pass contact extraction (raw mailto/tel, Lexbor, token-based detector)
////  - FACT SCORING ENGINE that classifies extracted textual snippets into semantic categories
////  - Source attribution for each fact and grouped summary output
////  - Streaming per-URL output (prints results as each URL is processed)
////  - MSVC-friendly, C++17
////
//// IMPORTANT:
////  - Replace apiKey and cx with your Google Custom Search API key and CX ID before running.
////  - Link lexbor and libcurl .lib files in your MSVC project.
////  - Place nlohmann/json.hpp in your include path.
////  - Build with C++17 or later.
////
//// This file is self-contained as an end-to-end sample with safe defaults for timeouts and sizes.
//
//#include <iostream>
//#include <string>
//#include <set>
//#include <vector>
//#include <map>
//#include <sstream>
//#include <algorithm>
//#include <future>
//#include <chrono>
//#include <cctype>
//#include <cstdio>
//
//#include <curl/curl.h>
//#include <nlohmann/json.hpp>
//
//// Lexbor headers (ensure MSVC include/lib paths are set in your project)
//#include <lexbor/html/parser.h>
//#include <lexbor/dom/interfaces/document.h>
//#include <lexbor/dom/interfaces/element.h>
//#include <lexbor/dom/interfaces/node.h>
//#include <lexbor/dom/interfaces/character_data.h>
//
//using namespace std;
//using json = nlohmann::json;
//
//// ---------------------- TODO (quick tunables) ----------------------
//// - Increase maxProcess to process more results (default 3 for debug).
//// - Add/remove keywords in keywordCategoryMap to change classification behavior.
//// - Adjust LEXBOR_TIMEOUT to allow more time for parsing if needed.
//static int maxProcess = 3; // DEBUG default; change to 10+ for production
//static vector<string> keywordFilters = { "email", "contact", "phone", "@" };
//// ------------------------------------------------------------------
//
//// -------------------- Configuration --------------------
//static const long CURL_CONNECT_TIMEOUT = 10L;      // seconds to establish connection
//static const long CURL_TOTAL_TIMEOUT = 25L;       // overall seconds for each cURL transfer
//static const size_t MAX_DOWNLOAD_BYTES = 8 * 1024 * 1024; // 8 MB maximum to download per page
//static const size_t MAX_CONTENT_CHARS = 20000;    // truncate extracted content to this many chars
//static const size_t MAX_SCAN_TOKENS = 2000;       // safety limit on tokens scanned
//static const chrono::seconds LEXBOR_TIMEOUT(20);  // wait up to 20 seconds for Lexbor parsing
//// ------------------ end Configuration ------------------
//
//// ---------- Data structures for facts ----------
//struct ExtractedFact {
//    string category;   // e.g. "Designation", "Education", "Research Interest"
//    string value;      // the actual snippet / fact text
//    string sourceURL;  // which page it came from
//};
//
//struct WriteData {
//    string* buffer;
//    size_t maxBytes;
//};
//
//// ------------------ cURL write callback ------------------
//static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
//    size_t realsize = size * nmemb;
//    if (!userp) return 0;
//    WriteData* wd = static_cast<WriteData*>(userp);
//    if (!wd->buffer) return 0;
//    if (wd->buffer->size() + realsize > wd->maxBytes) {
//        return 0; // signal write error -> cURL abort
//    }
//    wd->buffer->append(static_cast<char*>(contents), realsize);
//    return realsize;
//}
//
//// ------------------ Utility functions ------------------
//static string urlEncode(const string& str) {
//    string encoded;
//    char hex[8];
//    for (unsigned char c : str) {
//        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
//            encoded.push_back((char)c);
//        }
//        else if (c == ' ') {
//            encoded.push_back('+');
//        }
//        else {
//#ifdef _MSC_VER
//            sprintf_s(hex, "%%%02X", c);
//#else
//            snprintf(hex, sizeof(hex), "%%%02X", c);
//#endif
//            encoded += hex;
//        }
//    }
//    return encoded;
//}
//
//static string trimPunctEdges(const string& s) {
//    size_t i = 0, j = s.size();
//    while (i < j && ispunct((unsigned char)s[i])) ++i;
//    while (j > i && ispunct((unsigned char)s[j - 1])) --j;
//    if (i >= j) return string();
//    return s.substr(i, j - i);
//}
//
//static string toLowerStr(const string& s) {
//    string r(s);
//    transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return (char)tolower(c); });
//    return r;
//}
//
//// Utility: case-insensitive keyword lookup in a block of text
//static bool containsAnyKeywordCaseInsensitive(const string& text, const vector<string>& keywords) {
//    if (text.empty() || keywords.empty()) return false;
//    string lowerText = toLowerStr(text);
//    for (const string& kw : keywords) {
//        string lowerKw = toLowerStr(kw);
//        if (!lowerKw.empty() && lowerText.find(lowerKw) != string::npos) {
//            return true;
//        }
//    }
//    return false;
//}
//
//// Fallback HTML stripper (VERY FAST) used only if Lexbor fails
//static string stripTags(const string& html) {
//    string out;
//    out.reserve(html.size());
//    bool inTag = false;
//    for (char c : html) {
//        if (c == '<') {
//            inTag = true;
//        }
//        else if (c == '>') {
//            inTag = false;
//        }
//        else if (!inTag) {
//            // Normalize newlines and tabs to spaces for clean tokenizing
//            if (c == '\n' || c == '\r' || c == '\t') {
//                out.push_back(' ');
//            }
//            else {
//                out.push_back(c);
//            }
//        }
//    }
//    return out;
//}
//
//// ------------------ Fast raw HTML scan for mailto: / tel: ------------------
//// Extracts mailto: and tel: patterns from raw HTML (cheap, fast).
//static vector<string> scanHTMLForMailtoTel(const string& html) {
//    vector<string> results;
//    string lower = toLowerStr(html);
//    size_t pos = 0;
//    while (true) {
//        size_t mpos = lower.find("mailto:", pos);
//        if (mpos == string::npos) break;
//        size_t start = mpos + 7;
//        size_t end = start;
//        while (end < html.size() && html[end] != '"' && html[end] != '\'' && html[end] != '>' && !isspace((unsigned char)html[end])) ++end;
//        string candidate = html.substr(start, end - start);
//        candidate = trimPunctEdges(candidate);
//        if (!candidate.empty()) results.push_back(string("Email: ") + candidate);
//        pos = end;
//    }
//    pos = 0;
//    while (true) {
//        size_t tpos = lower.find("tel:", pos);
//        if (tpos == string::npos) break;
//        size_t start = tpos + 4;
//        size_t end = start;
//        while (end < html.size() && html[end] != '"' && html[end] != '\'' && html[end] != '>' && !isspace((unsigned char)html[end])) ++end;
//        string candidate = html.substr(start, end - start);
//        candidate = trimPunctEdges(candidate);
//        if (!candidate.empty()) results.push_back(string("Phone: ") + candidate);
//        pos = end;
//    }
//    return results;
//}
//
//// ------------------ Lexbor-based extraction (walk DOM and collect visible text) ------------------
//static string extractTextFromHTML_lexbor(const string& html) {
//    lxb_status_t status;
//    lxb_html_document_t* document = lxb_html_document_create();
//    if (!document) return "";
//
//    status = lxb_html_document_parse(document,
//        (const lxb_char_t*)html.c_str(),
//        html.size());
//    if (status != LXB_STATUS_OK) {
//        lxb_html_document_destroy(document);
//        return "";
//    }
//
//    lxb_html_body_element_t* body_html = lxb_html_document_body_element(document);
//    if (!body_html) {
//        lxb_html_document_destroy(document);
//        return "";
//    }
//
//    lxb_dom_element_t* body_el = lxb_dom_interface_element(body_html);
//    string extracted;
//
//    vector<lxb_dom_node_t*> stack;
//    lxb_dom_node_t* start_node = lxb_dom_interface_node(body_el);
//    if (start_node) stack.push_back(start_node);
//
//    set<lxb_dom_node_t*> visited;
//    while (!stack.empty()) {
//        lxb_dom_node_t* node = stack.back();
//        stack.pop_back();
//        if (!node) continue;
//        if (visited.find(node) != visited.end()) continue;
//        visited.insert(node);
//
//        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
//            lxb_dom_element_t* el = lxb_dom_interface_element(node);
//            if (el) {
//                size_t name_len = 0;
//                const lxb_char_t* tag_name = lxb_dom_element_local_name(el, &name_len);
//                if (tag_name && name_len > 0) {
//                    string tag((const char*)tag_name, name_len);
//                    for (auto& c : tag) c = (char)tolower(c);
//                    if (tag == "script" || tag == "style" || tag == "noscript" ||
//                        tag == "nav" || tag == "header" || tag == "footer" ||
//                        tag == "aside" || tag == "button" || tag == "form") {
//                        continue;
//                    }
//                }
//            }
//        }
//
//        if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
//            size_t len = 0;
//            const lxb_char_t* data = lxb_dom_node_text_content(node, &len);
//            if (data && len > 0) {
//                string text(reinterpret_cast<const char*>(data), len);
//                auto start = text.find_first_not_of(" \t\r\n");
//                auto end = text.find_last_not_of(" \t\r\n");
//                if (start != string::npos && end != string::npos) {
//                    string trimmed = text.substr(start, end - start + 1);
//                    if (trimmed.length() > 2) {
//                        extracted.append(trimmed);
//                        extracted.push_back(' ');
//                    }
//                }
//            }
//        }
//
//        if (node->last_child) {
//            for (lxb_dom_node_t* child = node->last_child; child != nullptr; child = child->prev) {
//                stack.push_back(child);
//            }
//        }
//    }
//
//    lxb_html_document_destroy(document);
//    return extracted;
//}
//
//static string extractTextFromHTML_withTimeout(const string& html, chrono::seconds timeout) {
//    try {
//        auto fut = std::async(std::launch::async, [&html]() -> string {
//            try {
//                return extractTextFromHTML_lexbor(html);
//            }
//            catch (...) {
//                return string();
//            }
//            });
//
//        if (fut.wait_for(timeout) == future_status::ready) {
//            return fut.get();
//        }
//        else {
//            return string();
//        }
//    }
//    catch (...) {
//        return string();
//    }
//}
//
//// ------------------ Contact detection helpers (strict phones) ------------------
//static bool looksLikeEmail(const string& token) {
//    auto atPos = token.find('@');
//    if (atPos == string::npos) return false;
//    if (atPos == 0 || atPos + 1 >= token.size()) return false;
//    string local = token.substr(0, atPos);
//    string domain = token.substr(atPos + 1);
//    if (local.empty() || domain.empty()) return false;
//    auto dotPos = domain.find('.');
//    if (dotPos == string::npos) return false;
//    if (dotPos == 0 || dotPos + 1 >= domain.size()) return false;
//    if (count(token.begin(), token.end(), '@') != 1) return false;
//    if (token.size() < 5 || token.size() > 254) return false;
//    return true;
//}
//
//static bool looksLikeStrictPhone(const string& token) {
//    if (token.empty()) return false;
//    if (token.size() > 40) return false; // reject absurd tokens
//    size_t idx = 0;
//    if (token[0] == '+') {
//        idx = 1;
//        if (idx >= token.size()) return false;
//    }
//    else if (!isdigit((unsigned char)token[0])) {
//        return false;
//    }
//    int digits = 0;
//    for (char c : token) {
//        if (isdigit((unsigned char)c)) ++digits;
//        else if (c == '+' || c == '-' || c == ' ' || c == '(' || c == ')') continue;
//        else return false;
//    }
//    return (digits >= 7 && digits <= 15);
//}
//
//// ------------------ Token-based hybrid detector (strict) ------------------
//// Tokenizes by whitespace; preserves internal punctuation for emails and phone tokens.
//// Returns list of "Email: x" and "Phone: y" (deduped).
//static vector<string> extractContactsTokenBased(const string& content, const string& name) {
//    vector<string> results;
//    if (content.empty()) return results;
//
//    vector<string> tokens;
//    tokens.reserve(512);
//    {
//        istringstream iss(content);
//        string tok;
//        while (iss >> tok) {
//            string trimmed = trimPunctEdges(tok);
//            if (!trimmed.empty()) tokens.push_back(trimmed);
//            if (tokens.size() >= MAX_SCAN_TOKENS) break; // safety cap
//        }
//    }
//
//    if (tokens.empty()) return results;
//
//    set<string> foundSet;
//    vector<string> lowerTokens;
//    lowerTokens.reserve(tokens.size());
//    for (auto& t : tokens) lowerTokens.push_back(toLowerStr(t));
//
//    for (size_t i = 0; i < tokens.size(); ++i) {
//        string tok = tokens[i];
//        string low = lowerTokens[i];
//
//        if (low.rfind("mailto:", 0) == 0) {
//            string email = tok.substr(7);
//            email = trimPunctEdges(email);
//            if (looksLikeEmail(email)) foundSet.insert(string("Email: ") + email);
//            continue;
//        }
//        if (low.rfind("tel:", 0) == 0) {
//            string phone = tok.substr(4);
//            phone = trimPunctEdges(phone);
//            if (looksLikeStrictPhone(phone)) foundSet.insert(string("Phone: ") + phone);
//            continue;
//        }
//
//        if (tok.find('@') != string::npos) {
//            string candidate = trimPunctEdges(tok);
//            if (looksLikeEmail(candidate)) foundSet.insert(string("Email: ") + candidate);
//            continue;
//        }
//
//        string candidate = trimPunctEdges(tok);
//        if (!candidate.empty() && looksLikeStrictPhone(candidate)) {
//            foundSet.insert(string("Phone: ") + candidate);
//            continue;
//        }
//    }
//
//    for (auto& s : foundSet) results.push_back(s);
//    return results;
//}
//
//// ------------------ FACT SCORING ENGINE ------------------
//
//// category -> list of trigger keywords (lowercase)
//static const map<string, vector<string>> keywordCategoryMap = {
//    {"Designation", {"professor", "lecturer", "assistant professor", "associate professor", "assistant", "associate", "hod", "head of", "dean", "chair", "faculty", "postdoc", "researcher", "instructor"}},
//    {"Department", {"department", "dept.", "csit", "computer science", "computer & it", "computer science & it", "informatics", "electrical", "mechanical", "mathematics", "physics"}},
//    {"Education", {"phd", "ph.d", "doctorate", "ms", "msc", "m.sc", "bs", "bsc", "b.s.", "b.s", "degree", "graduat", "master", "bachelor"}},
//    {"Research Interest", {"research", "interest", "specializ", "specialise", "focus", "quantum", "cryptography", "iot", "machine learning", "deep learning", "computer vision"}},
//    {"Timeline", {"joined", "appointed", "since", "from", "started", "began", "effective", "onward", "promoted", "served as"}},
//    {"Family", {"son of", "s/o", "father", "mother", "parents", "parent", "wife", "husband"}},
//    {"Profile Links", {"google scholar", "scholar.google", "researchgate", "linkedin", "orcid", "cv", "resume"}},
//    {"Honors/Awards", {"award", "fellow", "honor", "distinction", "prize", "awardee"}}
//};
//
//// Score a sentence/line against category keywords and return the best category and score.
//// This is simple bag-of-keywords scoring. Returns pair<bestCategory, bestScore>.
//static pair<string, int> scoreLineAgainstCategories(const string& line) {
//    string low = toLowerStr(line);
//    int bestScore = 0;
//    string bestCategory;
//    for (const auto& p : keywordCategoryMap) {
//        int score = 0;
//        for (const auto& kw : p.second) {
//            if (low.find(kw) != string::npos) {
//                // weight: longer keywords get slightly more weight
//                score += (int)kw.size() / 4 + 1;
//            }
//        }
//        if (score > bestScore) {
//            bestScore = score;
//            bestCategory = p.first;
//        }
//    }
//    return { bestCategory, bestScore };
//}
//
//// split content into candidate lines (sentence-like) for scanning
//static vector<string> splitIntoCandidateLines(const string& content) {
//    vector<string> lines;
//    if (content.empty()) return lines;
//    string cur;
//    cur.reserve(256);
//    for (size_t i = 0; i < content.size(); ++i) {
//        char c = content[i];
//        cur.push_back(c);
//        // sentence delimiters: ., ?, !, newline, semicolon
//        if (c == '.' || c == '?' || c == '!' || c == '\n' || c == ';') {
//            // trim
//            size_t a = 0, b = cur.size();
//            while (a < b && isspace((unsigned char)cur[a])) ++a;
//            while (b > a && isspace((unsigned char)cur[b - 1])) --b;
//            if (b > a) lines.push_back(cur.substr(a, b - a));
//            cur.clear();
//        }
//        // safety: if current chunk grows very large, flush it
//        if (cur.size() > 1000) {
//            lines.push_back(cur);
//            cur.clear();
//        }
//    }
//    if (!cur.empty()) {
//        // trim
//        size_t a = 0, b = cur.size();
//        while (a < b && isspace((unsigned char)cur[a])) ++a;
//        while (b > a && isspace((unsigned char)cur[b - 1])) --b;
//        if (b > a) lines.push_back(cur.substr(a, b - a));
//    }
//    return lines;
//}
//
//// scan content for facts and append to knowledgeBase
//static void scanForFacts(const string& content, const string& sourceURL, vector<ExtractedFact>& knowledgeBase, const string& name) {
//    if (content.empty()) return;
//    vector<string> lines = splitIntoCandidateLines(content);
//    for (const auto& line : lines) {
//        // ignore very short lines
//        if (line.size() < 6) continue;
//
//        // Score line
//        auto scored = scoreLineAgainstCategories(line);
//        string category = scored.first;
//        int score = scored.second;
//
//        // simple thresholds: we accept category if score >= 2 or if it contains an explicit marker
//        bool accept = false;
//        if (score >= 2) accept = true;
//
//        // additionally, treat lines containing name as higher priority
//        if (!name.empty()) {
//            string lowerLine = toLowerStr(line);
//            string lowerName = toLowerStr(name);
//            if (lowerLine.find(lowerName) != string::npos) {
//                accept = true;
//                if (category.empty()) category = "Misc";
//            }
//        }
//
//        // If we accepted this line, map it to a category (or Misc if none)
//        if (accept) {
//            string cat = category.empty() ? "Misc" : category;
//            ExtractedFact f{ cat, line, sourceURL };
//            knowledgeBase.push_back(f);
//        }
//        else {
//            // Heuristic: lines that explicitly mention common degree words but had low score still useful
//            string low = toLowerStr(line);
//            if (low.find("phd") != string::npos || low.find("ph.d") != string::npos || low.find("cv") != string::npos) {
//                ExtractedFact f{ "Education", line, sourceURL };
//                knowledgeBase.push_back(f);
//            }
//        }
//    }
//}
//
//// ------------------ cURL fetch with guards ------------------
//static string fetchPage(const string& url, long connectTimeout = CURL_CONNECT_TIMEOUT, long totalTimeout = CURL_TOTAL_TIMEOUT, size_t maxBytes = MAX_DOWNLOAD_BYTES) {
//    CURL* curl = curl_easy_init();
//    if (!curl) {
//        cerr << "[fetchPage] curl_easy_init failed\n";
//        return "";
//    }
//
//    string buffer;
//    WriteData wd{ &buffer, maxBytes };
//
//    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
//    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
//    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
//    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &wd);
//    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (compatible; MyBot/1.0)");
//    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connectTimeout);
//    curl_easy_setopt(curl, CURLOPT_TIMEOUT, totalTimeout);
//
//    CURLcode res = curl_easy_perform(curl);
//    if (res != CURLE_OK) {
//        if (res == CURLE_WRITE_ERROR) {
//            cerr << "[fetchPage] Download aborted: exceeded max size (" << maxBytes << " bytes)\n";
//        }
//        else {
//            cerr << "[fetchPage] cURL error for " << url << " : " << curl_easy_strerror(res) << "\n";
//        }
//        curl_easy_cleanup(curl);
//        return "";
//    }
//
//    curl_easy_cleanup(curl);
//    return buffer;
//}
//
//// ------------------ SUMMARY GENERATOR ------------------
//// Group knowledgeBase by category, then print a readable summary with source attribution.
//static void printSummary(const vector<ExtractedFact>& knowledgeBase) {
//    if (knowledgeBase.empty()) {
//        cout << "[SUMMARY] No facts extracted.\n";
//        return;
//    }
//    // Group facts by category
//    map<string, vector<pair<string, string>>> grouped;
//    for (const auto& f : knowledgeBase) {
//        grouped[f.category].push_back({ f.value, f.sourceURL });
//    }
//
//    cout << "\n===== Extracted Knowledge Summary =====\n";
//    for (const auto& p : grouped) {
//        cout << "\n-> " << p.first << ":\n";
//        // dedupe by value
//        set<string> seen;
//        for (const auto& valsrc : p.second) {
//            string val = valsrc.first;
//            string src = valsrc.second;
//            if (seen.find(val) != seen.end()) continue;
//            seen.insert(val);
//            cout << "   - " << val << "  (source: " << src << ")\n";
//        }
//    }
//    cout << "\n=======================================\n";
//}
//
//// ------------------ MAIN ------------------
//int main() {
//    ios::sync_with_stdio(false);
//    cin.tie(nullptr);
//
//    curl_global_init(CURL_GLOBAL_DEFAULT);
//
//    string name, university, department;
//    cout << "Enter name (leave blank if none): ";
//    getline(cin, name);
//    cout << "Enter university (leave blank if none): ";
//    getline(cin, university);
//    cout << "Enter department (leave blank if none): ";
//    getline(cin, department);
//
//    string query;
//    if (!name.empty()) query += name + " ";
//    if (!university.empty()) query += university + " ";
//    if (!department.empty()) query += department + " ";
//    if (query.empty()) {
//        cout << "No input provided. Exiting.\n";
//        curl_global_cleanup();
//        return 0;
//    }
//
//    string enc = urlEncode(query);
//
//    // ------------------ Google Custom Search API settings ------------------
//    string apiKey = "AIzaSyCLUMLZaNTNeD3N1E2IJ2ODSPuLkdfj0Vo";
//    string cx = "c499c8c7c5dde46d4";
//
//    string apiUrl = "https://www.googleapis.com/customsearch/v1?q=" + enc + "&key=" + apiKey + "&cx=" + cx;
//
//    cout << "[INFO] Querying Google Custom Search API...\n" << flush;
//
//    // Perform API request (simple, with timeouts)
//    CURL* curl = curl_easy_init();
//    if (!curl) {
//        cerr << "[ERROR] curl init failed for API request\n";
//        curl_global_cleanup();
//        return 1;
//    }
//    string apiResponse;
//    WriteData apiWd{ &apiResponse, MAX_DOWNLOAD_BYTES };
//    curl_easy_setopt(curl, CURLOPT_URL, apiUrl.c_str());
//    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
//    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &apiWd);
//    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (compatible; MyBot/1.0)");
//    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CURL_CONNECT_TIMEOUT);
//    curl_easy_setopt(curl, CURLOPT_TIMEOUT, CURL_TOTAL_TIMEOUT);
//
//    CURLcode res = curl_easy_perform(curl);
//    if (res != CURLE_OK) {
//        cerr << "[ERROR] cURL API request failed: " << curl_easy_strerror(res) << "\n";
//        curl_easy_cleanup(curl);
//        curl_global_cleanup();
//        return 1;
//    }
//    curl_easy_cleanup(curl);
//
//    // Parse JSON response from Google Custom Search
//    vector<ExtractedFact> knowledgeBase; // global aggregator for all pages
//
//    try {
//        json j = json::parse(apiResponse);
//        if (j.contains("error")) {
//            cerr << "[API ERROR] " << j["error"]["message"].get<string>() << "\n";
//            curl_global_cleanup();
//            return 1;
//        }
//        if (!j.contains("items")) {
//            cout << "[INFO] No results returned by API.\n";
//            curl_global_cleanup();
//            return 0;
//        }
//
//        cout << "[INFO] Number of results: " << j["items"].size() << "\n" << flush;
//
//        int count = 1;
//        for (auto& item : j["items"]) {
//            if (count > maxProcess) break;
//
//            string title = item.value("title", "No Title");
//            string link = item.value("link", "");
//            cout << "\nResult " << count << ": " << title << "\n";
//            cout << "   Link: " << link << "\n";
//            cout << "   Processing..." << endl << flush; // indicate work started for this URL
//
//            if (link.empty()) {
//                cout << "   [WARN] empty link. Skipping.\n" << flush;
//                ++count;
//                continue;
//            }
//
//            // 1) Fetch page with cURL (timeouts + max size)
//            string pageHTML = fetchPage(link, CURL_CONNECT_TIMEOUT, CURL_TOTAL_TIMEOUT, MAX_DOWNLOAD_BYTES);
//            if (pageHTML.empty()) {
//                cout << "   [WARN] Failed to fetch or download too large. Skipping.\n" << flush;
//                ++count;
//                continue;
//            }
//            cout << "   [OK] fetched " << pageHTML.size() << " bytes\n" << flush;
//
//            // 1a) LAYER 1: raw HTML quick scan for mailto: / tel:
//            auto rawFound = scanHTMLForMailtoTel(pageHTML);
//            if (!rawFound.empty()) {
//                bool nameFoundRaw = false;
//                if (!name.empty()) nameFoundRaw = toLowerStr(pageHTML).find(toLowerStr(name)) != string::npos;
//                string conf = nameFoundRaw ? "[HIGH CONFIDENCE]" : "[LOW CONFIDENCE]";
//                cout << "   [RAWSCAN] Found contacts in HTML attributes:\n" << flush;
//                for (auto& s : rawFound) {
//                    cout << "      " << s << " " << conf << "\n";
//                    // push into knowledgeBase as contact facts
//                    ExtractedFact f;
//                    if (s.rfind("Email:", 0) == 0) { f.category = "Contact/Email"; f.value = s.substr(7); }
//                    else if (s.rfind("Phone:", 0) == 0) { f.category = "Contact/Phone"; f.value = s.substr(7); }
//                    else { f.category = "Contact"; f.value = s; }
//                    f.sourceURL = link;
//                    knowledgeBase.push_back(f);
//                }
//                cout << flush;
//                // continue to try deeper extraction as well
//            }
//
//            // ------------------ KEYWORD PREFILTER (FAST) ------------------
//            if (!containsAnyKeywordCaseInsensitive(pageHTML, keywordFilters)) {
//                cout << "   [SKIP] No contact-related keywords found (email/contact/phone/@). Skipping heavy parsing.\n" << flush;
//                ++count;
//                continue;
//            }
//            else {
//                cout << "   [INFO] Contact-related keywords found  running detailed extraction...\n" << flush;
//            }
//
//            // 2) Attempt Lexbor parsing with timeout
//            cout << "   Parsing with Lexbor (timeout " << LEXBOR_TIMEOUT.count() << "s)...\n" << flush;
//            string content = extractTextFromHTML_withTimeout(pageHTML, LEXBOR_TIMEOUT);
//
//            if (content.empty()) {
//                cout << "   [WARN] Lexbor parse timed out/failed. Using fallback stripper.\n" << flush;
//                content = stripTags(pageHTML);
//            }
//            else {
//                cout << "   [OK] Lexbor returned content (" << content.size() << " chars)\n" << flush;
//            }
//
//            // Truncate content for safety
//            if (content.size() > MAX_CONTENT_CHARS) {
//                cout << "   [INFO] Truncating extracted content to " << MAX_CONTENT_CHARS << " characters for safe scanning.\n" << flush;
//                content = content.substr(0, MAX_CONTENT_CHARS);
//            }
//
//            // 3) FACT SCAN (Layer 2 & 3)
//            // 3a) Scan content for semantic facts and record them with source attribution
//            scanForFacts(content, link, knowledgeBase, name+" "+university+" "+department);
//            // 3b) Token-based contact detection
//            auto contacts = extractContactsTokenBased(content, name + " " + university + " " + department);
//            for (auto& s : contacts) {
//                ExtractedFact f;
//                if (s.rfind("Email:", 0) == 0) { f.category = "Contact/Email"; f.value = s.substr(7); }
//                else if (s.rfind("Phone:", 0) == 0) { f.category = "Contact/Phone"; f.value = s.substr(7); }
//                else { f.category = "Contact"; f.value = s; }
//                f.sourceURL = link;
//                knowledgeBase.push_back(f);
//            }
//
//            cout << "   [INFO] Extraction finished for this URL. Results added to knowledge base.\n" << flush;
//
//            ++count;
//        }
//
//        // After processing pages, print final summary grouped by category
//        printSummary(knowledgeBase);
//
//    }
//    catch (const std::exception& e) {
//        cerr << "[ERROR] Failed to parse API JSON: " << e.what() << "\n";
//        curl_global_cleanup();
//        return 1;
//    }
//
//    curl_global_cleanup();
//    cout << "[DONE]\n";
//    return 0;
//}