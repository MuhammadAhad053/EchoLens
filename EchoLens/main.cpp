////// Combined: Gemini entity extraction (from trial.cpp) + Lexbor-based fact extraction & classification (from categorized.cpp)
// Single-file program. Requires:
//  - libcurl
//  - lexbor (headers + library)
//  - nlohmann/json.hpp in include path
// Build (example):
// g++ -std=c++17 -O2 -o integrated_search combined.cpp -lcurl -llexbor ...(link lexbor libs as needed)
//
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

//header files 
#include "http_utils.h"
#include "string_utils.h"
#include "gemini_utils.h"
#include "fact_extractor.h" 
#include  "html_parser.h"
#include "summary_utils.h"

using namespace std;
using json = nlohmann::json;

// -------------------- CONFIG --------------------
static const string GEMINI_API_KEY = "AIzaSyDH2iwDhbyDIrj7mIHjSpMbahwO6oxN6AM"; // replace
static const string GEMINI_URL = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key=" + GEMINI_API_KEY;
static const string GOOGLE_CSE_API_KEY = "AIzaSyCLUMLZaNTNeD3N1E2IJ2ODSPuLkdfj0Vo"; // replace
static const string GOOGLE_CX = "c499c8c7c5dde46d4"; // replace




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
        printRefinedJSON(refined);
    cout << "=====================================\n";


    curl_global_cleanup();
    cout << "[DONE]\n";
    return 0;
}
