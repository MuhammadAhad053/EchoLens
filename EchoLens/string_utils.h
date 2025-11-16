#pragma once

// --- Includes ---
// All headers needed from your .h and .cpp files
#include <string>
#include <vector>
#include <cctype>    // For ispunct, isspace, tolower
#include <algorithm> // For transform

// Use the std namespace as you had in your .cpp
using namespace std;

// --- Function Definitions ---
// All functions MUST be 'inline'

inline string toLowerStr(const string& s) {
    string r(s);
    transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return (char)tolower(c); });
    return r;
}


inline string stripTags(const string& html) {
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

inline string trimPunctEdges(const string& s) {
    size_t i = 0, j = s.size();
    while (i < j && ispunct((unsigned char)s[i])) ++i;
    while (j > i && ispunct((unsigned char)s[j - 1])) --j;
    if (i >= j) return string();
    return s.substr(i, j - i);
}

inline bool containsAnyKeywordCaseInsensitive(const string& text, const vector<string>& keywords) {
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