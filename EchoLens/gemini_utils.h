#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <nlohmann/json.hpp> 

// Include the other header-only utils this file depends on
#include "fact_extractor.h"
#include "http_utils.h" 

// --- Namespaces ---
using namespace std;
using json = nlohmann::json;
extern const string GEMINI_URL;

inline string askGeminiForEntities(const string& prompt) {
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

inline string extractFirstJsonObject(const string& text) {
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

inline json parseGeminiJson(const string& geminiText) {
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

inline json refineAndValidateWithGemini(const vector<ExtractedFact>& kb) {
    if (kb.empty()) {
        cerr << "[Refinement] Knowledge base empty. Skipping refinement.\n";
        return json();
    }

    stringstream ss;
    ss << "You are a fact validator and refiner.\n";
    ss << "Below is a list of extracted facts about a person from various web sources.\n";
    ss << "Each fact has a category, raw value, and source.\n";
    ss << "Your task: clean, deduplicate, validate, and merge them into a single structured JSON.\n";
    ss << "If facts are incomplete or redundant, fix them(you can add facts aswell). If something important seems missing, add a fact for it through your own knowledge.\n";
    ss << "Return JSON only, with keys: name, department, university, designation, contact_emails, contact_phones, research_interests, education, awards, location, and others.\n";
    ss << "Respond with ONE and ONLY ONE valid JSON object.";
    ss << "Do not include explanations, comments, text, or code fences.";
    ss << "Do not wrap the JSON in ```json or any other formatting.";
    ss << "Output MUST start with '{' and end with '}'.";
    ss << "If you need to add facts, insert them directly into the JSON.";
    ss << "Never output multiple JSON objects or trailing text.";
    ss << "Follow this JSON schema exactly:\n";
    ss << "{\n"
        << "  \"name\": string,\n"
        << "  \"department\": string,\n"
        << "  \"university\": string,\n"
        << "  \"designation\": string,\n"
        << "  \"contact_emails\": [string],\n"
        << "  \"contact_phones\": [string],\n"
        << "  \"research_interests\": [string],\n"
        << "  \"education\": [string],\n"
        << "  \"awards\": [string],\n"
        << "  \"location\": string,\n"
        << "  \"others\": [string]\n"
        << "}";

    ss << "Facts:\n";
    for (const auto& f : kb)
        ss << "- [" << f.category << "] " << f.value << " (source: " << f.sourceURL << ")\n";

    string resp = askGeminiForEntities(ss.str());

    std::cout << "=== RAW GEMINI OUTPUT ===\n" << resp << "\n=========================\n";

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
