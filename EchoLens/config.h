#pragma once
#pragma once

#include <string>
#include <chrono>

using namespace std;

// Configuration constants
static const string GEMINI_API_KEY = "AIzaSyDH2iwDhbyDIrj7mIHjSpMbahwO6oxN6AM";
static const string GEMINI_URL = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key=" + GEMINI_API_KEY;
static const string GOOGLE_CSE_API_KEY = "AIzaSyCLUMLZaNTNeD3N1E2IJ2ODSPuLkdfj0Vo";
static const string GOOGLE_CX = "c499c8c7c5dde46d4";

// Timeouts and limits
static const long CURL_CONNECT_TIMEOUT = 10L;
static const long CURL_TOTAL_TIMEOUT = 30L;
static const size_t MAX_DOWNLOAD_BYTES = 8 * 1024 * 1024;
static const size_t MAX_CONTENT_CHARS = 20000;
static const size_t MAX_SCAN_TOKENS = 2000;
static const chrono::seconds LEXBOR_TIMEOUT(20);