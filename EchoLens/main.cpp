#include <iostream>
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using namespace std;
using json = nlohmann::json;

// Callback for writing response into a string
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

int main() {
    CURL* curl;
    CURLcode res;
    string readBuffer;

    string apiKey = "AIzaSyCLUMLZaNTNeD3N1E2IJ2ODSPuLkdfj0Vo";      // replace with your Google API Key
    string cx = "c499c8c7c5dde46d4";               // replace with your Search Engine ID
    string query = "Dr.+Muhammad+Kamran+%20NED+University";     // example query

    string url = "https://www.googleapis.com/customsearch/v1?q=" + query +
        "&key=" + apiKey +
        "&cx=" + cx;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            cerr << "curl_easy_perform() failed: "
                << curl_easy_strerror(res) << endl;
        }
        else {
            try {
                json j = json::parse(readBuffer);

                // Print whole JSON response (pretty format)
                cout << j.dump(4) << endl;

                // Example: print first result title and link
                if (j.contains("items")) {
                    cout << "\nFirst Result:\n";
                    cout << "Title: " << j["items"][0]["title"] << endl;
                    cout << "Link: " << j["items"][0]["link"] << endl;
                }
            }
            catch (exception& e) {
                cerr << "JSON parsing error: " << e.what() << endl;
            }
        }

        curl_easy_cleanup(curl);
    }
    cin.get();
    curl_global_cleanup();
    return 0;
}
