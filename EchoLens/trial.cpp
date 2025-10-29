//// enhanced_fact_engine_v2.cpp
//// Enhanced with multi-priority filtering: Name (high), University/Department (medium)
//// Ready to compile with C++17, Lexbor, libcurl, and nlohmann/json
//
//#include <iostream>
//#include <string>
//#include <set>
//#include <vector>
//#include <map>
//#include <tuple>
//#include <sstream>
//#include <algorithm>
//#include <future>
//#include <chrono>
//#include <cctype>
//#include <cstdio>
//#include <curl/curl.h>
//#include <nlohmann/json.hpp>
//
//// Lexbor headers
//#include <lexbor/html/parser.h>
//#include <lexbor/dom/interfaces/document.h>
//#include <lexbor/dom/interfaces/element.h>
//#include <lexbor/dom/interfaces/node.h>
//#include <lexbor/dom/interfaces/character_data.h>
//
//using namespace std;
//using json = nlohmann::json;
//
//// ---------------------- Configuration ----------------------
//static int maxProcess = 3; // DEBUG default; change to 10+ for production
//static vector<string> keywordFilters = { "email", "contact", "phone", "@" };
//
//static const long CURL_CONNECT_TIMEOUT = 10L;
//static const long CURL_TOTAL_TIMEOUT = 25L;
//static const size_t MAX_DOWNLOAD_BYTES = 8 * 1024 * 1024;
//static const size_t MAX_CONTENT_CHARS = 20000;
//static const size_t MAX_SCAN_TOKENS = 2000;
//static const chrono::seconds LEXBOR_TIMEOUT(20);
//// ------------------ end Configuration ------------------
//
//// ---------- Data structures for facts ----------
//struct ExtractedFact {
//    string category;
//    string value;
//    string sourceURL;
//    int confidence = 1; // 1=low, 2=medium, 3=high
//    int relevance_score = 0; // Combined relevance score
//};
//
//struct WriteData {
//    string* buffer;
//    size_t maxBytes;
//};
//
//// ---------- Global aggregator ----------
//static vector<ExtractedFact> globalFactsCollector;
//
//// ------------------ cURL write callback ------------------
//static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
//    size_t realsize = size * nmemb;
//    if (!userp) return 0;
//    WriteData* wd = static_cast<WriteData*>(userp);
//    if (!wd->buffer) return 0;
//    if (wd->buffer->size() + realsize > wd->maxBytes) {
//        return 0;
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
//// ------------------ Enhanced Multi-Priority Filtering Strategy ------------------
//static vector<string> splitNameParts(const string& name) {
//    vector<string> parts;
//    istringstream iss(name);
//    string w;
//    while (iss >> w) {
//        string p = toLowerStr(trimPunctEdges(w));
//        if (p.size() >= 2) parts.push_back(p);
//    }
//    return parts;
//}
//
//static bool containsExactMatch(const string& text, const string& term) {
//    if (term.empty()) return false;
//    string lowerText = toLowerStr(text);
//    string lowerTerm = toLowerStr(term);
//    return lowerText.find(lowerTerm) != string::npos;
//}
//
//static bool containsMultipleParts(const string& text, const string& term) {
//    if (term.empty()) return false;
//
//    string lowerText = toLowerStr(text);
//    auto termParts = splitNameParts(term);
//
//    if (termParts.size() >= 2) {
//        int matches = 0;
//        for (const auto& part : termParts) {
//            if (lowerText.find(part) != string::npos) {
//                matches++;
//            }
//        }
//        return (termParts.size() == 2 && matches == 2) || (termParts.size() >= 3 && matches >= 2);
//    }
//
//    return false;
//}
//
//// Calculate relevance score: Name (highest), University/Department (medium)
//static int calculateRelevanceScore(const string& text, const string& name, const string& university, const string& department) {
//    int score = 0;
//
//    // NAME: Highest priority (40 points max)
//    if (!name.empty()) {
//        if (containsExactMatch(text, name)) {
//            score += 30; // Exact name match
//        }
//        if (containsMultipleParts(text, name)) {
//            score += 20; // Multiple name parts
//        }
//
//        // Additional bonus for name with title context
//        string lowerText = toLowerStr(text);
//        if (lowerText.find("professor") != string::npos && containsExactMatch(text, name)) {
//            score += 10;
//        }
//        if (lowerText.find("dr.") != string::npos && containsExactMatch(text, name)) {
//            score += 10;
//        }
//    }
//
//    // UNIVERSITY: Medium priority (20 points max)
//    if (!university.empty()) {
//        if (containsExactMatch(text, university)) {
//            score += 15;
//        }
//        if (containsMultipleParts(text, university)) {
//            score += 10;
//        }
//
//        // Bonus for university in context with name
//        if (!name.empty() && containsExactMatch(text, name) && containsExactMatch(text, university)) {
//            score += 5;
//        }
//    }
//
//    // DEPARTMENT: Medium priority (20 points max)
//    if (!department.empty()) {
//        if (containsExactMatch(text, department)) {
//            score += 15;
//        }
//        if (containsMultipleParts(text, department)) {
//            score += 10;
//        }
//
//        // Bonus for department in context with name
//        if (!name.empty() && containsExactMatch(text, name) && containsExactMatch(text, department)) {
//            score += 5;
//        }
//    }
//
//    // Contextual bonus for academic/professional terms
//    string lowerText = toLowerStr(text);
//    vector<string> academicTerms = { "faculty", "research", "publication", "conference", "journal" };
//    for (const auto& term : academicTerms) {
//        if (lowerText.find(term) != string::npos) {
//            score += 2;
//        }
//    }
//
//    return min(score, 100); // Cap at 100
//}
//
//static bool isRelevantToTarget(const string& text, const string& name, const string& university, const string& department, int& out_score) {
//    // If no filters provided, accept everything with low score
//    if (name.empty() && university.empty() && department.empty()) {
//        out_score = 10;
//        return true;
//    }
//
//    out_score = calculateRelevanceScore(text, name, university, department);
//
//    // Acceptance thresholds:
//    // - High: Name match (score >= 20)
//    // - Medium: University/Department match (score >= 15) 
//    // - Low: Any academic context (score >= 10)
//    return out_score >= 10;
//}
//
//static bool isNearby(const string& haystack, size_t pos, const string& needle, size_t window = 300) {
//    if (needle.empty()) return true;
//    if (haystack.empty()) return false;
//    string lower = toLowerStr(haystack);
//    string lowNeedle = toLowerStr(needle);
//
//    size_t found = lower.find(lowNeedle);
//    while (found != string::npos) {
//        size_t startWindow = (pos > window) ? pos - window : 0;
//        size_t endWindow = min(pos + window, lower.size() - 1);
//        if (found >= startWindow && found <= endWindow) return true;
//        found = lower.find(lowNeedle, found + 1);
//    }
//
//    auto parts = splitNameParts(needle);
//    for (auto& p : parts) {
//        size_t fp = lower.find(p);
//        while (fp != string::npos) {
//            size_t startWindow = (pos > window) ? pos - window : 0;
//            size_t endWindow = min(pos + window, lower.size() - 1);
//            if (fp >= startWindow && fp <= endWindow) return true;
//            fp = lower.find(p, fp + 1);
//        }
//    }
//    return false;
//}
//
//// Enhanced proximity check considering all search terms
//static bool isNearbyAnyTerm(const string& haystack, size_t pos, const string& name, const string& university, const string& department, size_t window = 300) {
//    if (name.empty() && university.empty() && department.empty()) return true;
//
//    if (!name.empty() && isNearby(haystack, pos, name, window)) return true;
//    if (!university.empty() && isNearby(haystack, pos, university, window)) return true;
//    if (!department.empty() && isNearby(haystack, pos, department, window)) return true;
//
//    return false;
//}
//
//// ------------------ Enhanced Context Window ------------------
//static string extractTargetContext(const string& content, const string& name, const string& university, const string& department) {
//    if (content.empty() || (name.empty() && university.empty() && department.empty()))
//        return content;
//
//    vector<string> paragraphs;
//    string currentPara;
//
//    for (char c : content) {
//        currentPara.push_back(c);
//        if (c == '\n' || currentPara.size() > 500) {
//            if (!currentPara.empty()) {
//                paragraphs.push_back(currentPara);
//                currentPara.clear();
//            }
//        }
//    }
//    if (!currentPara.empty()) paragraphs.push_back(currentPara);
//
//    string focusedContent;
//    for (const auto& para : paragraphs) {
//        int score = 0;
//        if (isRelevantToTarget(para, name, university, department, score) && score >= 15) {
//            focusedContent += para + " ";
//        }
//    }
//
//    return focusedContent.empty() ? content : focusedContent;
//}
//
//// ------------------ Noise Phrase Removal ------------------
//static string removeNoisePhrases(string text) {
//    static const vector<string> noisePhrases = {
//        "skip to main content",
//        "back to faculty profiles",
//        "return to faculty profiles",
//        "faculty profiles",
//        "home page",
//        "site navigation",
//        "privacy policy",
//        "terms of use",
//        "all rights reserved",
//        "cookie policy",
//        "university sitemap",
//        "search this site",
//        "content may not be reproduced"
//    };
//
//    string lower = text;
//    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
//
//    for (const auto& phrase : noisePhrases) {
//        size_t pos = 0;
//        while ((pos = lower.find(phrase, pos)) != string::npos) {
//            text.erase(pos, phrase.size());
//            lower.erase(pos, phrase.size());
//        }
//    }
//
//    return text;
//}
//
//// ------------------ Fast raw HTML scan for mailto: / tel: ------------------
//static vector<tuple<string, string, size_t>> scanHTMLForMailtoTelWithPos(const string& html) {
//    vector<tuple<string, string, size_t>> results;
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
//        if (!candidate.empty()) results.emplace_back(string("Email"), candidate, mpos);
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
//        if (!candidate.empty()) results.emplace_back(string("Phone"), candidate, tpos);
//        pos = end;
//    }
//    return results;
//}
//
//// ------------------ Lexbor-based extraction ------------------
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
//// ------------------ Contact detection helpers ------------------
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
//    if (token.size() > 40) return false;
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
//// ------------------ Token-based hybrid detector ------------------
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
//            if (tokens.size() >= MAX_SCAN_TOKENS) break;
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
//static pair<string, int> scoreLineAgainstCategories(const string& line) {
//    string low = toLowerStr(line);
//    int bestScore = 0;
//    string bestCategory;
//    for (const auto& p : keywordCategoryMap) {
//        int score = 0;
//        for (const auto& kw : p.second) {
//            if (low.find(kw) != string::npos) {
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
//static vector<string> splitIntoCandidateLines(const string& content) {
//    vector<string> lines;
//    if (content.empty()) return lines;
//    string cur;
//    cur.reserve(256);
//    for (size_t i = 0; i < content.size(); ++i) {
//        char c = content[i];
//        cur.push_back(c);
//        if (c == '.' || c == '?' || c == '!' || c == '\n' || c == ';') {
//            size_t a = 0, b = cur.size();
//            while (a < b && isspace((unsigned char)cur[a])) ++a;
//            while (b > a && isspace((unsigned char)cur[b - 1])) --b;
//            if (b > a) lines.push_back(cur.substr(a, b - a));
//            cur.clear();
//        }
//        if (cur.size() > 1000) {
//            lines.push_back(cur);
//            cur.clear();
//        }
//    }
//    if (!cur.empty()) {
//        size_t a = 0, b = cur.size();
//        while (a < b && isspace((unsigned char)cur[a])) ++a;
//        while (b > a && isspace((unsigned char)cur[b - 1])) --b;
//        if (b > a) lines.push_back(cur.substr(a, b - a));
//    }
//    return lines;
//}
//
//static void scanForFacts(const string& content, const string& sourceURL, vector<ExtractedFact>& knowledgeBase,
//    const string& name, const string& university, const string& department) {
//    if (content.empty()) return;
//    vector<string> lines = splitIntoCandidateLines(content);
//
//    for (const auto& line : lines) {
//        if (line.size() < 6) continue;
//
//        // MULTI-PRIORITY FILTER: Calculate relevance score
//        int relevance_score = 0;
//        if (!isRelevantToTarget(line, name, university, department, relevance_score)) {
//            continue;
//        }
//
//        auto scored = scoreLineAgainstCategories(line);
//        string category = scored.first;
//        int category_score = scored.second;
//
//        // Set confidence based on relevance score
//        int confidence = 1;
//        if (relevance_score >= 30) {
//            confidence = 3; // High confidence (name match)
//        }
//        else if (relevance_score >= 20) {
//            confidence = 2; // Medium confidence (university/department + context)
//        }
//
//        bool accept = false;
//        if (category_score >= 2) accept = true;
//
//        // Higher priority for lines with good relevance
//        if (relevance_score >= 15) {
//            accept = true;
//            if (category.empty()) category = "Misc";
//        }
//
//        if (accept) {
//            string cat = category.empty() ? "Misc" : category;
//            ExtractedFact f{ cat, line, sourceURL, confidence, relevance_score };
//            knowledgeBase.push_back(f);
//        }
//        else {
//            string low = toLowerStr(line);
//            if ((low.find("phd") != string::npos || low.find("ph.d") != string::npos || low.find("cv") != string::npos)) {
//                int phd_relevance = 0;
//                if (isRelevantToTarget(line, name, university, department, phd_relevance) && phd_relevance >= 10) {
//                    ExtractedFact f{ "Education", line, sourceURL, 2, phd_relevance };
//                    knowledgeBase.push_back(f);
//                }
//            }
//        }
//    }
//}
//
//// ------------------ Post-Processing Filter ------------------
//static void filterIrrelevantFacts(vector<ExtractedFact>& facts, const string& name, const string& university, const string& department) {
//    if (name.empty() && university.empty() && department.empty()) return;
//
//    vector<ExtractedFact> filtered;
//    for (const auto& fact : facts) {
//        int score = 0;
//        if (isRelevantToTarget(fact.value, name, university, department, score) && score >= 10) {
//            filtered.push_back(fact);
//        }
//    }
//    facts = filtered;
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
//static void printSummary(const vector<ExtractedFact>& knowledgeBase) {
//    if (knowledgeBase.empty()) {
//        cout << "[SUMMARY] No facts extracted.\n";
//        return;
//    }
//    map<string, vector<pair<string, string>>> grouped;
//    for (const auto& f : knowledgeBase) {
//        grouped[f.category].push_back({ f.value, f.sourceURL });
//    }
//
//    cout << "\n===== Extracted Knowledge Summary =====\n";
//    for (const auto& p : grouped) {
//        cout << "\n-> " << p.first << ":\n";
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
//// ------------------ FUSION + NARRATIVE ENGINE ------------------
//struct UnifiedFact {
//    string category;
//    string value;
//    set<string> sources;
//    int confidence = 1;
//    int relevance_score = 0;
//};
//
//static string toLowerTrim(const string& s) {
//    string out;
//    out.reserve(s.size());
//    for (char c : s)
//        if (!isspace((unsigned char)c))
//            out.push_back((char)tolower(c));
//    return out;
//}
//
//static bool roughlySame(const string& a, const string& b) {
//    if (a == b) return true;
//    string lowA = toLowerTrim(a), lowB = toLowerTrim(b);
//    if (lowA.find(lowB) != string::npos || lowB.find(lowA) != string::npos)
//        return true;
//
//    istringstream sa(lowA), sb(lowB);
//    set<string> ta, tb;
//    string w;
//    while (sa >> w) ta.insert(w);
//    while (sb >> w) tb.insert(w);
//    if (ta.empty() || tb.empty()) return false;
//    int match = 0;
//    for (auto& t : ta)
//        if (tb.count(t)) ++match;
//    double overlap = (double)match / max(ta.size(), tb.size());
//    return overlap > 0.6;
//}
//
//static vector<UnifiedFact> fuseFacts(const vector<ExtractedFact>& rawFacts) {
//    vector<UnifiedFact> fused;
//    for (auto& f : rawFacts) {
//        bool merged = false;
//        for (auto& uf : fused) {
//            if (uf.category == f.category && roughlySame(uf.value, f.value)) {
//                uf.sources.insert(f.sourceURL);
//                uf.confidence = max(uf.confidence, f.confidence);
//                uf.relevance_score = max(uf.relevance_score, f.relevance_score);
//                merged = true;
//                break;
//            }
//        }
//        if (!merged) {
//            UnifiedFact u;
//            u.category = f.category;
//            u.value = f.value;
//            u.sources.insert(f.sourceURL);
//            u.confidence = f.confidence;
//            u.relevance_score = f.relevance_score;
//            fused.push_back(u);
//        }
//    }
//    return fused;
//}
//
//static string generateSummaryParagraph(const vector<UnifiedFact>& facts, const string& name, const string& university, const string& department) {
//    stringstream ss;
//    string person = name;
//    if (person.empty()) person = "This individual";
//
//    string designation, dept, education;
//    vector<string> researches;
//    vector<string> contacts;
//    vector<string> misc;
//
//    // Sort facts by relevance score (highest first)
//    vector<UnifiedFact> sortedFacts = facts;
//    sort(sortedFacts.begin(), sortedFacts.end(), [](const UnifiedFact& a, const UnifiedFact& b) {
//        return a.relevance_score > b.relevance_score;
//        });
//
//    for (const auto& f : sortedFacts) {
//        string cat = toLowerStr(f.category);
//        string val = f.value;
//
//        if (cat.find("designation") != string::npos || cat.find("role") != string::npos)
//            designation = val;
//        else if (cat.find("department") != string::npos)
//            dept = val;
//        else if (cat.find("education") != string::npos || cat.find("degree") != string::npos)
//            education = val;
//        else if (cat.find("research") != string::npos || cat.find("special") != string::npos)
//            researches.push_back(val);
//        else if (cat.find("contact") != string::npos)
//            contacts.push_back(val);
//        else if (cat.find("honor") != string::npos || cat.find("award") != string::npos)
//            misc.push_back(val);
//    }
//
//    ss << person;
//
//    if (!designation.empty()) {
//        string lowDes = toLowerStr(designation);
//        if (lowDes.find("professor") != string::npos || lowDes.find("lecturer") != string::npos ||
//            lowDes.find("assistant") != string::npos || lowDes.find("researcher") != string::npos) {
//            ss << " is " << designation;
//        }
//        else {
//            ss << " serves as " << designation;
//        }
//        if (!dept.empty()) ss << " in " << dept;
//        else if (!department.empty()) ss << " in " << department;
//        ss << ". ";
//    }
//    else if (!dept.empty() || !department.empty()) {
//        ss << " is associated with the " << (dept.empty() ? department : dept);
//        if (!university.empty()) ss << " at " << university;
//        ss << ". ";
//    }
//    else if (!university.empty()) {
//        ss << " is affiliated with " << university << ". ";
//    }
//    else {
//        ss << " has public professional information available. ";
//    }
//
//    if (!researches.empty()) {
//        set<string> seen;
//        vector<string> uniq;
//        for (auto& r : researches) {
//            if (seen.insert(r).second) uniq.push_back(r);
//        }
//        if (!uniq.empty()) {
//            ss << "Their research interests include ";
//            for (size_t i = 0; i < uniq.size(); ++i) {
//                ss << uniq[i];
//                if (i + 1 < uniq.size()) ss << ", ";
//            }
//            ss << ". ";
//        }
//    }
//
//    if (!education.empty()) {
//        ss << "They hold " << education << ". ";
//    }
//
//    if (!contacts.empty()) {
//        ss << "Contact information: ";
//        for (size_t i = 0; i < contacts.size(); ++i) {
//            ss << contacts[i];
//            if (i + 1 < contacts.size()) ss << ", ";
//        }
//        ss << ". ";
//    }
//
//    if (!misc.empty()) {
//        ss << "Notable achievements: ";
//        for (size_t i = 0; i < misc.size(); ++i) {
//            ss << misc[i];
//            if (i + 1 < misc.size()) ss << "; ";
//        }
//    }
//
//    return ss.str();
//}
//
//static void printSummaryWithFusion(const vector<ExtractedFact>& rawFacts, const string& name, const string& university, const string& department) {
//    cout << "\n\n====================\n";
//    cout << "📋 Original Extracted Facts (Sorted by Relevance)\n";
//    cout << "====================\n";
//    if (rawFacts.empty()) {
//        cout << "No extracted facts available.\n";
//    }
//    else {
//        // Sort by relevance score
//        vector<ExtractedFact> sortedFacts = rawFacts;
//        sort(sortedFacts.begin(), sortedFacts.end(), [](const ExtractedFact& a, const ExtractedFact& b) {
//            return a.relevance_score > b.relevance_score;
//            });
//
//        map<string, vector<pair<string, string>>> grouped;
//        for (const auto& f : sortedFacts) {
//            grouped[f.category].push_back({ f.value + " [Score: " + to_string(f.relevance_score) + "]", f.sourceURL });
//        }
//        for (const auto& p : grouped) {
//            cout << "\n-> " << p.first << ":\n";
//            set<string> seen;
//            for (const auto& valsrc : p.second) {
//                string val = valsrc.first;
//                string src = valsrc.second;
//                if (seen.find(val) != seen.end()) continue;
//                seen.insert(val);
//                cout << "   - " << val << "  (source: " << src << ")\n";
//            }
//        }
//    }
//
//    auto fused = fuseFacts(rawFacts);
//    cout << "\n\n====================\n";
//    cout << "🔄 Unified Facts (After Fusion - Sorted by Relevance)\n";
//    cout << "====================\n";
//    if (fused.empty()) {
//        cout << "(no unified facts produced)\n";
//    }
//    else {
//        // Sort by relevance score
//        sort(fused.begin(), fused.end(), [](const UnifiedFact& a, const UnifiedFact& b) {
//            return a.relevance_score > b.relevance_score;
//            });
//
//        for (auto& uf : fused) {
//            cout << "[" << uf.category << "] " << uf.value << "  (relevance: " << uf.relevance_score << ", sources: " << uf.sources.size() << ")\n";
//            for (auto& s : uf.sources) cout << "     ↳ " << s << "\n";
//        }
//    }
//
//    cout << "\n\n====================\n";
//    cout << "📝 Narrative Summary Paragraph\n";
//    cout << "====================\n";
//    string paragraph = generateSummaryParagraph(fused, name, university, department);
//    if (paragraph.empty()) {
//        cout << "(no narrative could be generated)\n";
//    }
//    else {
//        cout << paragraph << "\n";
//    }
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
//    cout << "[INFO] Search terms - Name: '" << name << "', University: '" << university << "', Department: '" << department << "'\n" << flush;
//
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
//    vector<ExtractedFact> knowledgeBase;
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
//            cout << "   Processing..." << endl << flush;
//
//            if (link.empty()) {
//                cout << "   [WARN] empty link. Skipping.\n" << flush;
//                ++count;
//                continue;
//            }
//
//            string pageHTML = fetchPage(link, CURL_CONNECT_TIMEOUT, CURL_TOTAL_TIMEOUT, MAX_DOWNLOAD_BYTES);
//            if (pageHTML.empty()) {
//                cout << "   [WARN] Failed to fetch or download too large. Skipping.\n" << flush;
//                ++count;
//                continue;
//            }
//            cout << "   [OK] fetched " << pageHTML.size() << " bytes\n" << flush;
//
//            auto rawFoundWithPos = scanHTMLForMailtoTelWithPos(pageHTML);
//            if (!rawFoundWithPos.empty()) {
//                bool anyPrinted = false;
//                for (auto& t : rawFoundWithPos) {
//                    string label = get<0>(t);
//                    string candidate = get<1>(t);
//                    size_t posInHtml = get<2>(t);
//                    bool nearby = true;
//                    if (!name.empty() || !university.empty() || !department.empty()) {
//                        nearby = isNearbyAnyTerm(pageHTML, posInHtml, name, university, department, 300);
//                    }
//                    if (!nearby) continue;
//                    if (!anyPrinted) {
//                        cout << "   [RAWSCAN] Found contacts in HTML attributes (near search terms):\n" << flush;
//                        anyPrinted = true;
//                    }
//                    cout << "      " << label << ": " << candidate << "\n";
//                    ExtractedFact f;
//                    if (label == "Email") { f.category = "Contact/Email"; f.value = candidate; }
//                    else { f.category = "Contact/Phone"; f.value = candidate; }
//                    f.sourceURL = link;
//                    // Calculate relevance score for contact
//                    int contact_score = 0;
//                    isRelevantToTarget(candidate, name, university, department, contact_score);
//                    f.relevance_score = contact_score;
//                    knowledgeBase.push_back(f);
//                }
//                if (anyPrinted) cout << flush;
//            }
//
//            if (!containsAnyKeywordCaseInsensitive(pageHTML, keywordFilters)) {
//                cout << "   [SKIP] No contact-related keywords found (email/contact/phone/@). Skipping heavy parsing.\n" << flush;
//                ++count;
//                continue;
//            }
//            else {
//                cout << "   [INFO] Contact-related keywords found — running detailed extraction...\n" << flush;
//            }
//
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
//            if (!name.empty() || !university.empty() || !department.empty()) {
//                content = extractTargetContext(content, name, university, department);
//                cout << "   [CONTEXT] Extracted " << content.size() << " chars around search terms\n" << flush;
//            }
//
//            content = removeNoisePhrases(content);
//
//            if (content.size() > MAX_CONTENT_CHARS) {
//                cout << "   [INFO] Truncating extracted content to " << MAX_CONTENT_CHARS << " characters for safe scanning.\n" << flush;
//                content = content.substr(0, MAX_CONTENT_CHARS);
//            }
//
//            scanForFacts(content, link, knowledgeBase, name, university, department);
//
//            auto contacts = extractContactsTokenBased(content, name);
//            for (auto& s : contacts) {
//                string key, candidate;
//                if (s.rfind("Email:", 0) == 0) { key = "Contact/Email"; candidate = s.substr(7); }
//                else if (s.rfind("Phone:", 0) == 0) { key = "Contact/Phone"; candidate = s.substr(7); }
//                else { key = "Contact"; candidate = s; }
//
//                bool accept = true;
//                if (!name.empty() || !university.empty() || !department.empty()) {
//                    string lowerC = toLowerStr(candidate);
//                    string lowerContent = toLowerStr(content);
//                    size_t pos = lowerContent.find(lowerC);
//
//                    if (pos == string::npos) {
//                        string frag = candidate;
//                        if (frag.size() > 8) frag = frag.substr(0, 8);
//                        size_t fpos = lowerContent.find(toLowerStr(frag));
//                        if (fpos != string::npos) pos = fpos;
//                    }
//                    if (pos == string::npos) {
//                        accept = false;
//                    }
//                    else {
//                        accept = isNearbyAnyTerm(content, pos, name, university, department, 100);
//                        if (accept) {
//                            size_t contextStart = (pos > 200) ? pos - 200 : 0;
//                            size_t contextEnd = min(pos + 200, content.size());
//                            string contactContext = content.substr(contextStart, contextEnd - contextStart);
//                            int context_score = 0;
//                            accept = isRelevantToTarget(contactContext, name, university, department, context_score) && context_score >= 10;
//                        }
//                    }
//                }
//                if (!accept) continue;
//                ExtractedFact f;
//                f.category = key;
//                f.value = candidate;
//                f.sourceURL = link;
//                int contact_score = 0;
//                isRelevantToTarget(candidate, name, university, department, contact_score);
//                f.relevance_score = contact_score;
//                knowledgeBase.push_back(f);
//            }
//
//            cout << "   [INFO] Extraction finished for this URL. Results added to knowledge base.\n" << flush;
//            ++count;
//        }
//
//        filterIrrelevantFacts(knowledgeBase, name, university, department);
//
//        printSummary(knowledgeBase);
//        printSummaryWithFusion(knowledgeBase, name, university, department);
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