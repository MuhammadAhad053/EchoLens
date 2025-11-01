//#include <iostream>
//#include <string>
//#include <curl/curl.h>
//using namespace std;
//
//size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* output) {
//    size_t totalSize = size * nmemb;
//    output->append((char*)contents, totalSize);
//    return totalSize;
//}
//
//int main() {
//    CURL* curl;
//    CURLcode res;
//    string response;
//    string apiKey = "AIzaSyDH2iwDhbyDIrj7mIHjSpMbahwO6oxN6AM";
//    string url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent";
//
//    // Modify this part to perform your NLP task
//    string jsonData = R"({
//      "contents": [
//        {
//          "parts": [
//            {
//              "text": "You are an advanced Natural Language Processing system. Your task is to extract and label all possible entities, facts, and key concepts from the given text — not limited to names, places, or organizations. For each entity you find, return the following structured JSON format:  \"entity\": \"<actual entity or concept>\",  \"category\": \"<type or classification>\",  \"context\": \"<short phrase or sentence explaining its role in the text>\"}Be comprehensive and context-aware. Capture all relevant entities such as:- People, places, organizations- Dates, times, quantities- Emotions, topics, ideas- Technologies, methods, or actions- Any other noun phrase or concept that adds meaning. Now, extract entities from the following text: \"Dr. Muhammad Kamran Quantum Cryptography NED University Pakistan\""
//            }
//          ]
//        }
//      ]
//    })";
//
//    curl = curl_easy_init();
//    if (curl) {
//        struct curl_slist* headers = NULL;
//        headers = curl_slist_append(headers, "Content-Type: application/json");
//        headers = curl_slist_append(headers, ("X-goog-api-key: " + apiKey).c_str());
//
//        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
//        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
//        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
//        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
//        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
//
//        res = curl_easy_perform(curl);
//        if (res != CURLE_OK)
//            cerr << "Request failed: " << curl_easy_strerror(res) << endl;
//        else
//            cout << "API Response:\n" << response << endl;
//
//        curl_slist_free_all(headers);
//        curl_easy_cleanup(curl);
//    }
//    return 0;
//}