//// try_final.cpp
//// Final patched, MSVC-friendly version with:
////  - Lexbor parsing attempted inside std::async with a hard timeout (fallback guaranteed)
////  - Fallback tag-stripping extractor if Lexbor times out/fails
////  - cURL timeouts and max-download-size guard
////  - Content truncation + line-by-line scanning to avoid catastrophic regex slowdowns
////  - maxProcess = 3 (quick debug runs)
////  - Comments explaining safety mechanisms
////
//// IMPORTANT:
////  - Replace apiKey and cx with your Google Custom Search API key and CX ID.
////  - Add Lexbor and libcurl .lib files to your MSVC project linker settings.
////  - Provide nlohmann/json.hpp in include path (single-header library).
////  - Compile with C++17 or later.
//
//#include <iostream>
//#include <string>
//#include <set>
//#include <vector>
//#include <regex>
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
//// -------------------- Configuration --------------------
//static const long CURL_CONNECT_TIMEOUT = 5L;      // seconds to establish connection
//static const long CURL_TOTAL_TIMEOUT = 15L;     // overall seconds for each cURL transfer
//static const size_t MAX_DOWNLOAD_BYTES = 5 * 1024 * 1024; // 5 MB maximum to download per page
//static const size_t MAX_CONTENT_CHARS = 20000;   // truncate extracted content to this many chars
//static const size_t MAX_SCAN_LINES = 500;     // maximum lines to scan per page for contacts
//static const chrono::seconds LEXBOR_TIMEOUT(5);   // wait up to 5 seconds for Lexbor parsing
//// ------------------ end Configuration ------------------
//
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
//// This is the "advanced" extractor. It can be slower or fail on some pages; thus we run it inside async.
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
//        // Launch lexbor parsing in a separate thread
//        auto fut = std::async(std::launch::async, [&html]() -> string {
//            try {
//                return extractTextFromHTML_lexbor(html);
//            }
//            catch (...) {
//                return string();
//            }
//            });
//
//        // wait_for gives us a hard timeout; if it's not ready, we'll skip lexbor and fallback
//        if (fut.wait_for(timeout) == future_status::ready) {
//            // Lexbor finished in time; get its result
//            return fut.get();
//        }
//        else {
//            // Timed out — do NOT block waiting; return empty to signal fallback.
//            return string();
//        }
//    }
//    catch (...) {
//        // Any exception => fallback
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
//// ------------------ Extract emails and phone numbers (line-by-line) ------------------
//// We scan up to MAX_SCAN_LINES lines to avoid huge regex operations on one giant string.
//static vector<string> extractContactsFromText(const string& text, const string& name) {
//    vector<string> results;
//    if (text.empty()) return results;
//
//    // Regexes (basic). These are intentionally conservative to avoid catastrophic backtracking.
//    regex phoneRegex(R"((\+?\d{1,3}[\s\-\(\)]*)?(\d{2,4}[\s\-\(\)]*)?\d{3,4}[\s\-]?\d{3,4})");
//    regex emailRegex(R"(([\w\.-]+)@([\w-]+\.)+[\w-]{2,4})");
//
//    istringstream iss(text);
//    string line;
//    size_t lines = 0;
//    set<string> foundSet; // dedupe
//
//    while (getline(iss, line) && lines < MAX_SCAN_LINES) {
//        ++lines;
//        // quick filter: only scan lines that mention the name (case-insensitive)
//        if (!containsName(line, name)) continue;
//
//        smatch m;
//        string::const_iterator startIt(line.cbegin());
//        // phone numbers
//        while (regex_search(startIt, line.cend(), m, phoneRegex)) {
//            string s = m.str();
//            // basic digit count filter: require at least 7 digits to avoid false positives
//            int digits = 0;
//            for (char c : s) if (isdigit((unsigned char)c)) ++digits;
//            if (digits >= 7) foundSet.insert(string("Phone: ") + s);
//            startIt = m.suffix().first;
//        }
//
//        // emails
//        startIt = line.cbegin();
//        while (regex_search(startIt, line.cend(), m, emailRegex)) {
//            foundSet.insert(string("Email: ") + m.str());
//            startIt = m.suffix().first;
//        }
//    }
//
//    for (auto& s : foundSet) results.push_back(s);
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
//    cout << "[INFO] Querying Google Custom Search API...\n";
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
//        cout << "[INFO] Number of results: " << j["items"].size() << "\n";
//
//        int count = 1;
//        int maxProcess = 3; // DEBUG: process only first 3 results for quicker runs (as requested)
//        for (auto& item : j["items"]) {
//            if (count > maxProcess) break;
//
//            string title = item.value("title", "No Title");
//            string link = item.value("link", "");
//            cout << "\nResult " << count << ": " << title << "\n";
//            cout << "   Link: " << link << "\n";
//
//            if (link.empty()) {
//                cout << "   [WARN] empty link. Skipping.\n";
//                ++count;
//                continue;
//            }
//
//            // 1) Fetch page with cURL (timeouts + max size)
//            cout << "   Fetching page ...\n";
//            string pageHTML = fetchPage(link, CURL_CONNECT_TIMEOUT, CURL_TOTAL_TIMEOUT, MAX_DOWNLOAD_BYTES);
//            if (pageHTML.empty()) {
//                cout << "   [WARN] Failed to fetch or download too large. Skipping.\n";
//                ++count;
//                continue;
//            }
//            cout << "   [OK] fetched " << pageHTML.size() << " bytes\n";
//
//            // 2) Attempt Lexbor parsing with a strict timeout (non-blocking)
//            cout << "   Parsing with Lexbor (timeout " << LEXBOR_TIMEOUT.count() << "s)...\n";
//            string content = extractTextFromHTML_withTimeout(pageHTML, LEXBOR_TIMEOUT);
//
//            // If Lexbor timed out/failed (content empty) -> fallback immediately
//            if (content.empty()) {
//                cout << "   [WARN] Lexbor parse timed out/failed. Using fallback stripper.\n";
//                content = stripTags(pageHTML);
//            }
//
//            // 3) Truncate content to keep regex safe (prevents huge scanning)
//            if (content.size() > MAX_CONTENT_CHARS) {
//                cout << "   [INFO] Truncating extracted content to " << MAX_CONTENT_CHARS << " characters for safe scanning.\n";
//                content = content.substr(0, MAX_CONTENT_CHARS);
//            }
//
//            // 4) Quick check: does the page mention the name at all?
//            if (!containsName(content, name)) {
//                cout << "   [INFO] Name not found on page.\n";
//                ++count;
//                continue;
//            }
//
//            // 5) Extract contacts line-by-line (limited)
//            auto contacts = extractContactsFromText(content, name);
//            if (contacts.empty()) {
//                cout << "   No relevant contacts found.\n";
//            }
//            else {
//                cout << "   Relevant Contacts:\n";
//                for (const auto& c : contacts) cout << "      " << c << "\n";
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
