#include <iostream>
#include <string>
#include <set>
#include <vector>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

// Lexbor
#include <lexbor/html/parser.h>
#include <lexbor/dom/interfaces/document.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/interfaces/node.h>
#include <lexbor/dom/interfaces/character_data.h>

using json = nlohmann::json;

// ------------------ CURL WRITE CALLBACK ------------------
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// ------------------ URL ENCODE ------------------
std::string urlEncode(const std::string& str) {
    std::string encoded;
    char hex[4];
    for (unsigned char c : str) {
        if (isalnum(c)) {
            encoded += c;
        }
        else if (c == ' ') {
            encoded += '+';
        }
        else {
#ifdef _MSC_VER
            sprintf_s(hex, "%%%02X", c);  // MSVC
#else
            snprintf(hex, sizeof(hex), "%%%02X", c);  // GCC/Clang
#endif
            encoded += hex;
        }
    }
    return encoded;
}

// ------------------ FETCH PAGE ------------------
std::string fetchPage(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string buffer;

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

        // Optional: set a User-Agent (some sites block curl default UA)
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (compatible; MyBot/1.0)");

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "cURL error while fetching " << url << ": "
                << curl_easy_strerror(res) << std::endl;
        }
        curl_easy_cleanup(curl);
    }
    return buffer;
}

// ------------------ RECURSIVE TEXT EXTRACTION ------------------
void extractTextRecursive(lxb_dom_node_t* node, std::string& out, std::set<lxb_dom_node_t*>& visited) {
    for (; node != nullptr; node = node->next) {
        if (visited.find(node) != visited.end()) continue; // skip already processed
        visited.insert(node);

        // Skip non-content elements
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t* el = lxb_dom_interface_element(node);
            size_t name_len = 0;
            const lxb_char_t* tag_name = lxb_dom_element_local_name(el, &name_len);

            if (tag_name) {
                std::string tag((const char*)tag_name, name_len);
                for (auto& c : tag) c = tolower(c);

                if (tag == "script" || tag == "style" || tag == "noscript" ||
                    tag == "nav" || tag == "header" || tag == "footer" ||
                    tag == "aside" || tag == "button" || tag == "form") {
                    continue; // skip subtree
                }

                // Recurse only into relevant content tags
                if (tag == "p" || tag == "article" || tag == "main" ||
                    tag == "h1" || tag == "h2" || tag == "h3" ||
                    tag == "h4" || tag == "h5" || tag == "h6" || tag == "li") {
                    extractTextRecursive(node->first_child, out, visited);
                    continue; // prevent double recursion
                }
            }
        }

        // Extract text nodes
        if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
            size_t len = 0;
            const lxb_char_t* data = lxb_dom_node_text_content(node, &len);
            if (data && len > 0) {
                std::string text(reinterpret_cast<const char*>(data), len);

                // Trim whitespace
                auto start = text.find_first_not_of(" \t\r\n");
                auto end = text.find_last_not_of(" \t\r\n");
                if (start != std::string::npos && end != std::string::npos) {
                    std::string trimmed = text.substr(start, end - start + 1);

                    // Keep only meaningful text
                    if (trimmed.length() > 3) {
                        out.append(trimmed);
                        out += " ";
                    }
                }
            }
        }

        // Recurse into children if not already done
        if (node->first_child) {
            extractTextRecursive(node->first_child, out, visited);
        }
    }
}

// ------------------ MAIN TEXT EXTRACTION FUNCTION ------------------
std::string extractTextFromHTML(const std::string& html) {
    lxb_status_t status;
    lxb_html_document_t* document = lxb_html_document_create();
    if (!document) return "Failed to create document";

    status = lxb_html_document_parse(document,
        (const lxb_char_t*)html.c_str(),
        html.size());
    if (status != LXB_STATUS_OK) {
        lxb_html_document_destroy(document);
        return "Failed to parse HTML";
    }

    lxb_html_body_element_t* body_html = lxb_html_document_body_element(document);
    if (!body_html) {
        lxb_html_document_destroy(document);
        return "No <body> element found";
    }

    lxb_dom_element_t* body_el = lxb_dom_interface_element(body_html);

    std::string extracted;
    std::set<lxb_dom_node_t*> visited;
    extractTextRecursive(lxb_dom_interface_node(body_el), extracted, visited);

    lxb_html_document_destroy(document);
    return extracted;
}


// ------------------ MAIN ------------------
int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // ---- USER INPUT SECTION ----
    std::string name, university, department;

    std::cout << "Enter name (leave blank if none): ";
    std::getline(std::cin, name);

    std::cout << "Enter university (leave blank if none): ";
    std::getline(std::cin, university);

    std::cout << "Enter department (leave blank if none): ";
    std::getline(std::cin, department);

    // Build search query dynamically
    std::string query;
    if (!name.empty()) query += name + " ";
    if (!university.empty()) query += university + " ";
    if (!department.empty()) query += department + " ";

    if (query.empty()) {
        std::cout << "No input provided. Exiting." << std::endl;
        return 0;
    }

    query = urlEncode(query);

    // ---- GOOGLE CUSTOM SEARCH API CONFIG ----
    std::string apiKey = "AIzaSyCLUMLZaNTNeD3N1E2IJ2ODSPuLkdfj0Vo";   // replace with your API Key
    std::string cx = "c499c8c7c5dde46d4";         // replace with your Search Engine ID

    std::string url = "https://www.googleapis.com/customsearch/v1?q=" + query +
        "&key=" + apiKey +
        "&cx=" + cx;

    // ---- CURL REQUEST ----
    CURL* curl = curl_easy_init();
    std::string readBuffer;

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        CURLcode res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: "
                << curl_easy_strerror(res) << std::endl;
        }
        else {
            try {
                json j = json::parse(readBuffer);

                if (j.contains("error")) {
                    std::cerr << "API Error: " << j["error"]["message"] << "\n";
                    return 1;
                }

                if (j.contains("items")) {
                    std::cout << "\nSearch Results for: " << query << "\n";
                    std::cout << "------------------------------------\n";

                    int count = 1;
                    for (auto& item : j["items"]) {
                        std::string link = item.value("link", "");
                        std::cout << count++ << ". " << item.value("title", "No Title") << "\n";
                        std::cout << "   Link: " << link << "\n";
                        std::cout << "   Snippet: " << item.value("snippet", "No Snippet") << "\n";

                        if (!link.empty()) {
                            std::string pageHTML = fetchPage(link);
                            if (!pageHTML.empty()) {
                                std::string content = extractTextFromHTML(pageHTML);
                                std::cout << "   Extracted Content (first 500 chars):\n"
                                    << content.substr(0, 500) << "...\n";
                            }
                        }

                        std::cout << "------------------------------------\n";
                    }
                }
                else {
                    std::cout << "No results found.\n";
                }
            }
            catch (std::exception& e) {
                std::cerr << "JSON parsing error: " << e.what() << std::endl;
            }
        }

        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    return 0;
}