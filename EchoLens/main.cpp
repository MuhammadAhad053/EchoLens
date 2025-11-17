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
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>

#include <curl/curl.h>
#include "nlohmann/json.hpp"

// Lexbor headers
#include <lexbor/html/parser.h>
#include <lexbor/dom/interfaces/document.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/interfaces/node.h>
#include <lexbor/dom/interfaces/character_data.h>

#include <hpdf.h>

// Project headers
#include "config.h"
#include "http_utils.h"
#include "string_utils.h"
#include "gemini_utils.h"
#include "fact_extractor.h"
#include "html_parser.h"
#include "summary_utils.h"
#include "pdf_utils.h"

using namespace std;
using json = nlohmann::json;

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
    cout << "[1/4] Performing Entity Extraction...\n";
    string geminiResp = askGeminiForEntities(prompt);
    if (geminiResp.empty()) {
        cerr << "[WARN] Couldn't extract entities from the prompt. Continuing with raw prompt as fallback.\n";
        geminiResp = prompt;
    }
    else {
        cout << "[OK] Entity Extraction Complete\n";
    }

    json geminiParsed = parseGeminiJson(geminiResp);
    string name = geminiParsed.value("name", "");
    string department = geminiParsed.value("department", "");
    string university = geminiParsed.value("university", "");
    string affiliation = geminiParsed.value("affiliation", "");
    string location = geminiParsed.value("location", "");
    string others = geminiParsed.value("others", "");

    cout << "[Parsed Entities]\n";
    cout << " Name: " << name << "\n";
    cout << " Department: " << department << "\n";
    cout << " University: " << university << "\n";
    cout << " Affiliation: " << affiliation << "\n";
    cout << " Location: " << location << "\n";
    cout << " Others: " << others << "\n";

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

    cout << "[2/4] Searching the web (Google Custom Search) for: " << query << "\n";
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

    cout << "[3/4] Fetching and extracting facts from top 3 results...\n";
    vector<ExtractedFact> knowledgeBase;
    int processed = 0;
    vector<string> keywordFilters = { "email", "contact", "phone", "@" };

    for (auto& res : results) {
        if (processed >= 5) break; // process up to 3 results
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

    // 4) Refinement
    cout << "\n[4/4] Refining extracted data...\n";
    json refined = refineAndValidateWithGemini(knowledgeBase);

    cout << "\n===== Refined Structured Output =====\n";
    if (refined.contains("raw_response"))
        // print raw text if JSON parsing failed
        cout << refined["raw_response"].get<string>() << "\n";
    else
        // print parsed JSON nicely
        printRefinedJSON(refined);
    cout << "=====================================\n";

    string readableText = refinedToReadableText(refined);

    cout << "\nWould you like a PDF of the refined output? (y/n): ";
    char ans;
    cin >> ans;

    if (ans == 'y' || ans == 'Y') {
        string pdfName = makePdfName(refined.value("name", ""));
        string savePath = askUserForSaveLocation(pdfName);

        if (!savePath.empty()) {
            createRefinedPDF(savePath, readableText);
            ShellExecuteA(NULL, "open", savePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
        else {
            cout << "PDF saving canceled.\n";
        }
    }

    cout << "[DONE]\n";

    curl_global_cleanup();
    return 0;
}