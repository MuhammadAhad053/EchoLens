#pragma once
// --- Includes ---
// All headers needed by the functions below
#include <string>
#include <vector>
#include <map>
#include <cctype>
#include <cstddef>
#include <algorithm>
#include <sstream>
#include <set>
#include <utility> // For std::pair

// Include the header-only utils this file depends on
#include "string_utils.h" // For toLowerStr() and trimPunctEdges()
#include "html_parser.h"
using namespace std;

// --- Structs ---
struct ExtractedFact {
    std::string category;
    std::string value;
    std::string sourceURL;
};

// --- Constants ---
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


// --- Function Definitions ---

inline pair<string, int> scoreLineAgainstCategories(const string& line) {
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

inline vector<string> splitIntoCandidateLines(const string& content) {
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

inline void scanForFacts(const string& content, const string& sourceURL, vector<ExtractedFact>& knowledgeBase, const string& name) {

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

inline bool isNearName(const string& html, size_t position, const string& name, int vicinity = 200) {
    if (name.empty()) return true;
    string lowerHtml = toLowerStr(html);
    string lowerName = toLowerStr(name);
    size_t namePos = lowerHtml.find(lowerName);
    if (namePos == string::npos) return false;
    return (position >= (namePos > vicinity ? namePos - vicinity : 0) &&
        position <= namePos + lowerName.size() + vicinity);
}

inline vector<string> scanHTMLForMailtoTel(const string& html, const string& name = "") {
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

inline bool looksLikeEmail(const string& token) {
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

inline bool looksLikeStrictPhone(const std::string& token) {
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


inline vector<string> extractContactsTokenBased(const string& content, const string& name) {
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

inline string extractVicinityText(const string& content, const string& name, int radius) {
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