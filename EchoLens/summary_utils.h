#pragma once

// --- Includes ---
// All headers needed from your .h and .cpp files
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <utility> // For std::pair

// Include headers this module depends on
#include "fact_extractor.h"  // For ExtractedFact
#include "nlohmann/json.hpp" // For json

// --- Namespaces ---
using namespace std;
using json = nlohmann::json;

// --- Function Definitions ---


inline void printSummary(const vector<ExtractedFact>& knowledgeBase) {
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

inline void printRefinedJSON(const json& j) {
    auto printBlock = [&](const string& title, const json& val) {
        // skip empties
        if (val.is_null()) return;
        if (val.is_string() && val.get<string>().empty()) return;
        if (val.is_array() && val.empty()) return;
        if (val.is_object() && val.empty()) return;

        cout << "-> " << title << ":\n";

        if (val.is_array()) {
            for (const auto& v : val) {
                cout << "   - " << v.get<string>() << "\n";
            }
        }
        else if (val.is_string()) {
            cout << "   - " << val.get<string>() << "\n";
        }

        cout << "\n";
        };

    printBlock("name", j.value("name", ""));
    printBlock("department", j.value("department", ""));
    printBlock("university", j.value("university", ""));
    printBlock("designation", j.value("designation", ""));
    printBlock("contact_emails", j.value("contact_emails", json::array()));
    printBlock("contact_phones", j.value("contact_phones", json::array()));
    printBlock("research_interests", j.value("research_interests", json::array()));
    printBlock("education", j.value("education", json::array()));
    printBlock("awards", j.value("awards", json::array()));
    printBlock("location", j.value("location", ""));
    printBlock("others", j.value("others", json::array()));

}
