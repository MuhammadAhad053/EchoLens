#pragma once
// --- Includes ---
// All headers needed from your .h and .cpp files
#include <string>
#include <vector>
#include <set>
#include <chrono>
#include <cstddef>
#include <future>
#include <iostream>
#include <cctype>

// Lexbor headers
#include <lexbor/html/parser.h>
#include <lexbor/dom/interfaces/document.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/interfaces/node.h>
#include <lexbor/dom/interfaces/character_data.h>

// Use the std namespace as you had in your .cpp
using namespace std;
// --- Constant Definitions ---
static const std::chrono::seconds LEXBOR_TIMEOUT(20);
static const size_t MAX_CONTENT_CHARS = 20000;

// --- Function Definitions ---

inline string extractTextFromHTML_lexbor(const string& html) {
    lxb_status_t status;
    lxb_html_document_t* document = lxb_html_document_create();
    if (!document) return "";
    status = lxb_html_document_parse(document,
        (const lxb_char_t*)html.c_str(),
        html.size());
    if (status != LXB_STATUS_OK) {
        lxb_html_document_destroy(document);
        return "";
    }
    lxb_html_body_element_t* body_html = lxb_html_document_body_element(document);
    if (!body_html) {
        lxb_html_document_destroy(document);
        return "";
    }
    lxb_dom_element_t* body_el = lxb_dom_interface_element(body_html);
    string extracted;
    vector<lxb_dom_node_t*> stack;
    lxb_dom_node_t* start_node = lxb_dom_interface_node(body_el);
    if (start_node) stack.push_back(start_node);
    set<lxb_dom_node_t*> visited;
    while (!stack.empty()) {
        lxb_dom_node_t* node = stack.back();
        stack.pop_back();
        if (!node) continue;
        if (visited.find(node) != visited.end()) continue;
        visited.insert(node);
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t* el = lxb_dom_interface_element(node);
            if (!el) continue;
            if (el) {
                size_t name_len = 0;
                const lxb_char_t* tag_name = lxb_dom_element_local_name(el, &name_len);
                if (tag_name && name_len > 0) {
                    string tag((const char*)tag_name, name_len);
                    for (auto& c : tag) c = (char)tolower(c);
                    if (tag == "script" || tag == "style" || tag == "noscript" ||
                        tag == "nav" || tag == "header" || tag == "footer" ||
                        tag == "aside" || tag == "button" || tag == "form") {
                        continue;
                    }
                }
            }
        }
        if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
            size_t len = 0;
            const lxb_char_t* data = lxb_dom_node_text_content(node, &len);
            if (data && len > 0) {
                string text(reinterpret_cast<const char*>(data), len);
                auto start = text.find_first_not_of(" \t\r\n");
                auto end = text.find_last_not_of(" \t\r\n");
                if (start != string::npos && end != string::npos) {
                    string trimmed = text.substr(start, end - start + 1);
                    if (trimmed.length() > 2) {
                        extracted.append(trimmed);
                        extracted.push_back(' ');
                    }
                }
            }
        }
        if (node->last_child) {
            for (lxb_dom_node_t* child = node->last_child; child != nullptr; child = child->prev) {
                stack.push_back(child);
            }
        }
    }
    lxb_html_document_destroy(document);
    return extracted;
}

inline string extractTextFromHTML_withTimeout(const string& html, chrono::seconds timeout) {
    try {
        auto fut = std::async(std::launch::async, [&html]() -> string {
            try {
                return extractTextFromHTML_lexbor(html);
            }
            catch (...) {
                return string();
            }
            });

        if (fut.wait_for(timeout) == future_status::ready) {
            return fut.get();
        }
        else {
            return string();
        }
    }
    catch (...) {
        return string();
    }
}


