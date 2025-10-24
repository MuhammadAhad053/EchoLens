//// attempt.cpp
////
//// Your patched program with token-based Hybrid Contact Detector (strict phone mode).
//// Only the contact extraction logic has been replaced (no regex).
////
//// IMPORTANT:
////  - Replace apiKey and cx if needed.
////  - Ensure lexbor, libcurl .lib files are linked in MSVC project.
////  - Ensure nlohmann/json.hpp is in your include path.
////  - Build with C++17 or later.
//
//#include <iostream>
//#include <string>
//#include <set>
//#include <vector>
//#include <regex> // kept (harmless) though not used by new detector
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
//// - Add/remove keywords in keywordFilters to change pre-filter behavior.
//// - Adjust LEXBOR_TIMEOUT to allow more time for parsing if needed.
//static int maxProcess = 3; // DEBUG default; change to 10+ for production
//static vector<string> keywordFilters = { "email", "contact", "phone", "@" };
//// ------------------------------------------------------------------
//
//// -------------------- Configuration --------------------
//static const long CURL_CONNECT_TIMEOUT = 10L;      // seconds to establish connection
//static const long CURL_TOTAL_TIMEOUT = 25L;     // overall seconds for each cURL transfer
//static const size_t MAX_DOWNLOAD_BYTES = 8 * 1024 * 1024; // 8 MB maximum to download per page
//static const size_t MAX_CONTENT_CHARS = 20000;   // truncate extracted content to this many chars
//static const size_t MAX_SCAN_LINES = 500;     // maximum lines to scan per page for contacts
//static const chrono::seconds LEXBOR_TIMEOUT(20);   // wait up to 20 seconds for Lexbor parsing
//// ------------------ end Configuration ------------------
//
//// Simple struct passed to the cURL write callback so we can enforce max-bytes
//struct WriteData {
//    string* buffer;
//    size_t maxBytes;
//};
//
//// cURL write callback that enforces maximum downloaded bytes.
//// If appending this chunk would exceed maxBytes, return 0 to cause cURL error (CURLE_WRITE_ERROR).
//static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
//    size_t realsize = size * nmemb;
//    if (!userp) return 0;
//    WriteData* wd = static_cast<WriteData*>(userp);
//    if (!wd->buffer) return 0;
//    if (wd->buffer->size() + realsize > wd->maxBytes) {
//        // Do not append and signal an error to cURL to abort the transfer
//        return 0;
//    }
//    wd->buffer->append(static_cast<char*>(contents), realsize);
//    return realsize;
//}
//
//// ------------------ URL encoding ------------------
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
//// ------------------ Fallback HTML -> text stripper ------------------
//// This is used if Lexbor times out or fails. It's simple and fast.
//static string stripTags(const string& html) {
//    string out;
//    out.reserve(min(html.size(), (size_t)65536));
//    bool inTag = false;
//    for (size_t i = 0; i < html.size(); ++i) {
//        char c = html[i];
//        if (c == '<') { inTag = true; continue; }
//        if (c == '>') { inTag = false; continue; }
//        if (!inTag) out.push_back(c);
//    }
//    // collapse whitespace (simpler view for regex scanning)
//    stringstream ss(out);
//    string token, res;
//    while (ss >> token) {
//        if (!res.empty()) res.push_back(' ');
//        res += token;
//    }
//    return res;
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
//    // Use explicit stack traversal to collect text nodes and skip undesirable tags
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
//                    // Skip subtree for tags that don't contain visible person info
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
//                    if (trimmed.length() > 3) {
//                        extracted.append(trimmed);
//                        extracted.push_back(' ');
//                    }
//                }
//            }
//        }
//
//        // push children in reverse order to preserve document order
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
//// ------------------ Wrapper: run Lexbor in async and enforce a strict timeout ------------------
//// If Lexbor does not finish within 'timeout', return an empty string so caller falls back to stripTags.
//// Note: the thread that runs lexbor may continue to run in background (can't forcibly kill), but main flow proceeds.
//static string extractTextFromHTML_withTimeout(const string& html, chrono::seconds timeout) {
//    try {
//        auto fut = std::async(std::launch::async, [&html]() -> string {
//            try {
//                return extractTextFromHTML_lexbor(html);
//            }
//            catch (...) {
//                return string();
//            }
//        });
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
//// ------------------ Utility: case-insensitive substring check ------------------
//static bool containsName(const string& text, const string& name) {
//    string lowerText = text;
//    string lowerName = name;
//    transform(lowerText.begin(), lowerText.end(), lowerText.begin(), [](unsigned char c) { return (char)tolower(c); });
//    transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) { return (char)tolower(c); });
//    return lowerText.find(lowerName) != string::npos;
//}
//
//// ------------------ Quick keyword pre-filter (case-insensitive) ------------------
//// Returns true if any of the keywords appears in 'haystack' (case-insensitive).
//static bool containsAnyKeywordCaseInsensitive(const string& haystack, const vector<string>& keywords) {
//    string lowerHay = haystack;
//    transform(lowerHay.begin(), lowerHay.end(), lowerHay.begin(), [](unsigned char c) { return (char)tolower(c); });
//    for (const auto& kw : keywords) {
//        if (kw.empty()) continue;
//        string lowerKw = kw;
//        transform(lowerKw.begin(), lowerKw.end(), lowerKw.begin(), [](unsigned char c) { return (char)tolower(c); });
//        if (lowerHay.find(lowerKw) != string::npos) return true;
//    }
//    return false;
//}
//
//// ------------------ New Hybrid Token + Context Contact Extractor (STRICT phone mode) ------------------
//static string trimPunctEdges(const string& s) {
//    size_t i = 0, j = s.size();
//    while (i < j && ispunct((unsigned char)s[i])) ++i;
//    while (j > i && ispunct((unsigned char)s[j-1])) --j;
//    if (i >= j) return string();
//    return s.substr(i, j - i);
//}
//
//static string toLowerStr(const string& s) {
//    string r(s);
//    transform(r.begin(), r.end(), r.begin(), [](unsigned char c){ return (char)tolower(c); });
//    return r;
//}
//
//static bool looksLikeEmail(const string& token) {
//    // Basic checks: contains exactly one '@' (or at least one), at least one '.' in domain part,
//    // local and domain parts non-empty and reasonable length.
//    auto atPos = token.find('@');
//    if (atPos == string::npos) return false;
//    if (atPos == 0 || atPos + 1 >= token.size()) return false;
//    string local = token.substr(0, atPos);
//    string domain = token.substr(atPos + 1);
//    if (local.empty() || domain.empty()) return false;
//    // domain must have at least one dot and parts non-empty
//    auto dotPos = domain.find('.');
//    if (dotPos == string::npos) return false;
//    if (dotPos == 0 || dotPos + 1 >= domain.size()) return false;
//    // avoid tokens that are obviously not emails (spaces, multiple @)
//    if (count(token.begin(), token.end(), '@') != 1) return false;
//    // reasonable length
//    if (token.size() < 5 || token.size() > 254) return false;
//    return true;
//}
//
//static bool looksLikeStrictPhone(const string& token) {
//    // STRICT: token must start with '+' or digit, and contain 7-15 digits total.
//    if (token.empty()) return false;
//    size_t idx = 0;
//    if (token[0] == '+') {
//        idx = 1;
//        if (idx >= token.size()) return false;
//    } else if (!isdigit((unsigned char)token[0])) {
//        return false;
//    }
//
//    int digits = 0;
//    for (char c : token) {
//        if (isdigit((unsigned char)c)) ++digits;
//        else if (c == '+' || c == '-' || c == ' ' || c == '(' || c == ')') {
//            // allowed separators
//            continue;
//        } else {
//            // other characters disqualify (letters, etc.)
//            return false;
//        }
//    }
//    return (digits >= 7 && digits <= 15);
//}
//
//static vector<string> extractContactsFromText(const string& text, const string& name) {
//    vector<string> results;
//    if (text.empty()) return results;
//
//    // 1) Normalize large flat text into smaller chunks by splitting on common separators.
//    string normalized;
//    normalized.reserve(text.size());
//    for (char c : text) {
//        if (c == '.' || c == ';' || c == ',' || c == '|' || c == '/' || c == '\\' || c == '\n' || c == '\r') {
//            normalized.push_back('\n');
//        }
//        else {
//            normalized.push_back(c);
//        }
//    }
//
//    // 2) Tokenize by whitespace, gather tokens in a vector for contextual checks.
//    vector<string> tokens;
//    {
//        istringstream iss(normalized);
//        string tok;
//        while (iss >> tok) {
//            string trimmed = trimPunctEdges(tok);
//            if (!trimmed.empty()) tokens.push_back(trimmed);
//        }
//    }
//
//    if (tokens.empty()) return results;
//
//    // Prepare lowercased tokens for quick context checks
//    vector<string> lowerTokens;
//    lowerTokens.reserve(tokens.size());
//    for (auto &t : tokens) lowerTokens.push_back(toLowerStr(t));
//
//    // Context keywords for boosting confidence (not required, but helpful)
//    set<string> contextSet = { "contact", "email", "e-mail", "tel", "telephone", "phone", "office", "reach" };
//
//    set<string> foundSet; // dedupe
//
//    // 3) Scan tokens for emails and phones (STRICT)
//    for (size_t i = 0; i < tokens.size(); ++i) {
//        const string &tok = tokens[i];
//        string low = lowerTokens[i];
//
//        // EMAIL check
//        if (tok.find('@') != string::npos) {
//            string candidate = tok;
//            // Ensure we don't keep trailing punctuation
//            candidate = trimPunctEdges(candidate);
//            if (looksLikeEmail(candidate)) {
//                foundSet.insert(string("Email: ") + candidate);
//                continue;
//            }
//        }
//
//        // PHONE check (STRICT)
//        // token must start with + or digit and contain 7-15 digits allowed with separators
//        {
//            string candidate = tok;
//            candidate = trimPunctEdges(candidate);
//            if (!candidate.empty() && looksLikeStrictPhone(candidate)) {
//                // Additional context check: if nearby tokens include "phone"/"tel"/"contact", it's higher confidence,
//                // but we include the number regardless (strict mode).
//                foundSet.insert(string("Phone: ") + candidate);
//                continue;
//            }
//        }
//
//        // Also check for mailto: or tel: patterns embedded in tokens (href extraction fallback)
//        if (low.rfind("mailto:", 0) == 0) {
//            string email = tok.substr(7);
//            email = trimPunctEdges(email);
//            if (looksLikeEmail(email)) foundSet.insert(string("Email: ") + email);
//        }
//        if (low.rfind("tel:", 0) == 0) {
//            string phone = tok.substr(4);
//            phone = trimPunctEdges(phone);
//            if (looksLikeStrictPhone(phone)) foundSet.insert(string("Phone: ") + phone);
//        }
//    }
//
//    // 4) Optionally prefer contacts that appear near the person's name
//    // If name appears, filter results that are in token windows near name; otherwise keep all found.
//    // Build list of name token positions:
//    vector<size_t> namePositions;
//    string lowerName = toLowerStr(name);
//    if (!lowerName.empty()) {
//        for (size_t i = 0; i < lowerTokens.size(); ++i) {
//            // crude contains check for name parts (split name into words)
//            if (lowerTokens[i].find(lowerName) != string::npos) {
//                namePositions.push_back(i);
//            }
//        }
//    }
//
//    // If we found name positions, prefer matches near them.
//    if (!namePositions.empty() && !foundSet.empty()) {
//        // We'll collect matches that appear within +/- 10 tokens of any name position
//        set<string> nearSet;
//        for (size_t pos : namePositions) {
//            size_t start = (pos > 10) ? pos - 10 : 0;
//            size_t end = min(pos + 10, lowerTokens.size() - 1);
//            for (size_t i = start; i <= end; ++i) {
//                // check tokens for email-like or phone-like substrings
//                string t = tokens[i];
//                string ttrim = trimPunctEdges(t);
//                if (ttrim.find('@') != string::npos && looksLikeEmail(ttrim)) {
//                    nearSet.insert(string("Email: ") + ttrim);
//                }
//                if (looksLikeStrictPhone(ttrim)) {
//                    nearSet.insert(string("Phone: ") + ttrim);
//                }
//                // also check mailto/tel forms
//                string low = lowerTokens[i];
//                if (low.rfind("mailto:", 0) == 0) {
//                    string email = t.substr(7);
//                    email = trimPunctEdges(email);
//                    if (looksLikeEmail(email)) nearSet.insert(string("Email: ") + email);
//                }
//                if (low.rfind("tel:", 0) == 0) {
//                    string phone = t.substr(4);
//                    phone = trimPunctEdges(phone);
//                    if (looksLikeStrictPhone(phone)) nearSet.insert(string("Phone: ") + phone);
//                }
//            }
//        }
//        // If nearSet not empty, use it preferentially; else fall back to foundSet
//        if (!nearSet.empty()) {
//            for (auto &s : nearSet) results.push_back(s);
//            return results;
//        }
//    }
//
//    // Otherwise return all found matches
//    for (auto &s : foundSet) results.push_back(s);
//    return results;
//}
//
//// ------------------ Fetch page via cURL with timeouts and max-bytes guard ------------------
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
//    if (apiKey == "REPLACE_WITH_YOUR_API_KEY" || cx == "REPLACE_WITH_YOUR_CX") {
//        cerr << "[ERROR] Please replace apiKey and cx with your own values in the source.\n";
//        curl_global_cleanup();
//        return 1;
//    }
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
//            // ------------------ KEYWORD PREFILTER (FAST) ------------------
//            // Check for simple contact-related keywords in the raw HTML (case-insensitive).
//            // This is very cheap and avoids invoking Lexbor or regex on pages unlikely to contain contact info.
//            if (!containsAnyKeywordCaseInsensitive(pageHTML, keywordFilters)) {
//                cout << "   [SKIP] No contact-related keywords found (email/contact/phone/@). Skipping heavy parsing.\n" << flush;
//                ++count;
//                continue;
//            }
//            else {
//                cout << "   [INFO] Contact-related keywords found — running detailed extraction...\n" << flush;
//            }
//
//            // 2) Attempt Lexbor parsing with timeout
//            cout << "   Parsing with Lexbor (timeout " << LEXBOR_TIMEOUT.count() << "s)...\n" << flush;
//            string content = extractTextFromHTML_withTimeout(pageHTML, LEXBOR_TIMEOUT);
//
//            // If Lexbor timed out/failed (content empty) -> fallback immediately
//            if (content.empty()) {
//                cout << "   [WARN] Lexbor parse timed out/failed. Using fallback stripper.\n" << flush;
//                content = stripTags(pageHTML);
//            }
//            else {
//                cout << "   [OK] Lexbor returned content (" << content.size() << " chars)\n" << flush;
//            }
//
//            // 3) Truncate content to keep regex safe
//            if (content.size() > MAX_CONTENT_CHARS) {
//                cout << "   [INFO] Truncating extracted content to " << MAX_CONTENT_CHARS << " characters for safe scanning.\n" << flush;
//                content = content.substr(0, MAX_CONTENT_CHARS);
//            }
//
//            // 4) Quick check: does the page mention the name at all?
//            if (!containsName(content, name)) {
//                cout << "   [INFO] Name not found in extracted content.\n" << flush;
//                ++count;
//                continue;
//            }
//
//            // 5) Extract contacts line-by-line (limited) -- NEW HYBRID TOKEN-BASED DETECTOR (STRICT phones)
//            auto contacts = extractContactsFromText(content, name);
//            if (contacts.empty()) {
//                cout << "   No relevant contacts found.\n" << flush;
//            }
//            else {
//                cout << "   Relevant Contacts:\n";
//                for (const auto& c : contacts) cout << "      " << c << "\n";
//                cout << flush;
//            }
//
//            ++count;
//        }
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
