//#include <iostream>
//#include <string>
//#include <set>
//#include <vector>
//#include <regex>
//#include <sstream>
//#include <curl/curl.h>
//#include <nlohmann/json.hpp>
//
//// Lexbor
//#include <lexbor/html/parser.h>
//#include <lexbor/dom/interfaces/document.h>
//#include <lexbor/dom/interfaces/element.h>
//#include <lexbor/dom/interfaces/node.h>
//#include <lexbor/dom/interfaces/character_data.h>
//
//using namespace std;
//using json = nlohmann::json;
//
//// ------------------ CURL WRITE CALLBACK ------------------
//static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
//    ((string*)userp)->append((char*)contents, size * nmemb);
//    return size * nmemb;
//}
//
//// ------------------ URL ENCODE ------------------
//string urlEncode(const string& str) {
//    string encoded;
//    char hex[4];
//    for (unsigned char c : str) {
//        if (isalnum(c)) {
//            encoded += c;
//        }
//        else if (c == ' ') {
//            encoded += '+';
//        }
//        else {
//#ifdef _MSC_VER
//            sprintf_s(hex, "%%%02X", c);  // MSVC
//#else
//            snprintf(hex, sizeof(hex), "%%%02X", c);  // GCC/Clang
//#endif
//            encoded += hex;
//        }
//    }
//    return encoded;
//}
//
//// ------------------ FETCH PAGE ------------------
//string fetchPage(const string& url) {
//    CURL* curl = curl_easy_init();
//    string buffer;
//
//    if (curl) {
//        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
//        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
//        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
//        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
//        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (compatible; MyBot/1.0)");
//
//        CURLcode res = curl_easy_perform(curl);
//        if (res != CURLE_OK) {
//            cerr << "cURL error while fetching " << url << ": "
//                << curl_easy_strerror(res) << endl;
//        }
//        curl_easy_cleanup(curl);
//    }
//    return buffer;
//}
//
//// ------------------ RECURSIVE TEXT EXTRACTION ------------------
//void extractTextRecursive(lxb_dom_node_t* node, string& out, set<lxb_dom_node_t*>& visited) {
//    for (; node != nullptr; node = node->next) {
//        if (visited.find(node) != visited.end()) continue;
//        visited.insert(node);
//
//        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
//            lxb_dom_element_t* el = lxb_dom_interface_element(node);
//            size_t name_len = 0;
//            const lxb_char_t* tag_name = lxb_dom_element_local_name(el, &name_len);
//
//            if (tag_name) {
//                string tag((const char*)tag_name, name_len);
//                for (auto& c : tag) c = tolower(c);
//
//                if (tag == "script" || tag == "style" || tag == "noscript" ||
//                    tag == "nav" || tag == "header" || tag == "footer" ||
//                    tag == "aside" || tag == "button" || tag == "form") {
//                    continue; // skip subtree
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
//                        out.append(trimmed);
//                        out += " ";
//                    }
//                }
//            }
//        }
//
//        if (node->first_child) {
//            extractTextRecursive(node->first_child, out, visited);
//        }
//    }
//}
//
//// ------------------ MAIN TEXT EXTRACTION FUNCTION ------------------
//string extractTextFromHTML(const string& html) {
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
//
//    string extracted;
//    set<lxb_dom_node_t*> visited;
//    extractTextRecursive(lxb_dom_interface_node(body_el), extracted, visited);
//
//    lxb_html_document_destroy(document);
//    return extracted;
//}
//
//// ------------------ FILTER FUNCTION ------------------
//bool containsName(const string& text, const string& name) {
//    string lowerText = text, lowerName = name;
//    transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);
//    transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
//    return lowerText.find(lowerName) != string::npos;
//}
//
//vector<string> extractContacts(const string& text, const string& name) {
//    vector<string> results;
//
//    if (!containsName(text, name)) return results;
//
//    regex phoneRegex(R"((\+?\d{1,3}[\s\-\(\)]*)?(\d{2,4}[\s\-\(\)]*)?\d{3,4}[\s\-]?\d{3,4})");
//    regex emailRegex(R"(([\w\.-]+)@([\w-]+\.)+[\w-]{2,4})");
//
//    smatch match;
//    string::const_iterator searchStart(text.cbegin());
//
//    while (regex_search(searchStart, text.cend(), match, phoneRegex)) {
//        results.push_back("Phone: " + match.str());
//        searchStart = match.suffix().first;
//    }
//
//    searchStart = text.cbegin();
//    while (regex_search(searchStart, text.cend(), match, emailRegex)) {
//        results.push_back("Email: " + match.str());
//        searchStart = match.suffix().first;
//    }
//
//    return results;
//}
//
//// ------------------ MAIN ------------------
//int main() {
//    curl_global_init(CURL_GLOBAL_DEFAULT);
//
//    string name, university, department;
//
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
//        cout << "No input provided. Exiting." << endl;
//        return 0;
//    }
//
//    query = urlEncode(query);
//
//    // ---- GOOGLE CUSTOM SEARCH API CONFIG ----
//    string apiKey = "AIzaSyCLUMLZaNTNeD3N1E2IJ2ODSPuLkdfj0Vo";   // ⚠️ Replace with your key
//    string cx = "c499c8c7c5dde46d4";         // ⚠️ Replace with your CX ID
//
//    string url = "https://www.googleapis.com/customsearch/v1?q=" + query +
//        "&key=" + apiKey + "&cx=" + cx;
//
//    CURL* curl = curl_easy_init();
//    string readBuffer;
//
//    if (curl) {
//        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
//        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
//        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
//
//        CURLcode res = curl_easy_perform(curl);
//
//        if (res != CURLE_OK) {
//            cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
//        }
//        else {
//            try {
//                json j = json::parse(readBuffer);
//
//                if (j.contains("error")) {
//                    cerr << "API Error: " << j["error"]["message"] << "\n";
//                    return 1;
//                }
//
//                if (j.contains("items")) {
//                    cout << "\nSearch Results for: " << name << "\n";
//                    cout << "------------------------------------\n";
//
//                    int count = 1;
//                    for (auto& item : j["items"]) {
//                        string link = item.value("link", "");
//                        cout << count++ << ". " << item.value("title", "No Title") << "\n";
//                        cout << "   Link: " << link << "\n";
//
//                        if (!link.empty()) {
//                            string pageHTML = fetchPage(link);
//                            if (!pageHTML.empty()) {
//                                string content = extractTextFromHTML(pageHTML);
//
//                                // Split into lines and check each for contacts
//                                istringstream iss(content);
//                                string line;
//                                set<string> contacts;
//                                while (getline(iss, line)) {
//                                    auto found = extractContacts(line, name);
//                                    contacts.insert(found.begin(), found.end());
//                                }
//
//                                if (!contacts.empty()) {
//                                    cout << "   Relevant Contacts:\n";
//                                    for (const auto& c : contacts) {
//                                        cout << "      " << c << "\n";
//                                    }
//                                }
//                                else {
//                                    cout << "   No relevant contacts found.\n";
//                                }
//                            }
//                        }
//
//                        cout << "------------------------------------\n";
//                    }
//                }
//                else {
//                    cout << "No results found.\n";
//                }
//            }
//            catch (exception& e) {
//                cerr << "JSON parsing error: " << e.what() << endl;
//            }
//        }
//        curl_easy_cleanup(curl);
//    }
//
//    curl_global_cleanup();
//    return 0;
//}
