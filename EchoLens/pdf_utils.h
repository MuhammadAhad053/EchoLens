#pragma once

#include <string>
#include <hpdf.h>
#include <windows.h>
#include <commdlg.h>
#include "nlohmann/json.hpp"

using namespace std;
using json = nlohmann::json;

inline void pdfWritePrettyWrapped(
    HPDF_Doc pdf,
    HPDF_Page page,
    HPDF_Font font,
    const string& text,
    float fontSize,
    float margin) {

    HPDF_Page_SetFontAndSize(page, font, fontSize);

    float width = HPDF_Page_GetWidth(page);
    float height = HPDF_Page_GetHeight(page);
    float maxWidth = width - 2 * margin;

    float x = margin;
    float y = height - margin;

    istringstream iss(text);
    string rawline;

    while (getline(iss, rawline)) {
        string line = rawline;

        while (!line.empty()) {
            const HPDF_BYTE* data = (const HPDF_BYTE*)line.c_str();
            HPDF_UINT len = (HPDF_UINT)line.size();
            HPDF_REAL realWidth = 0;

            HPDF_UINT cut = HPDF_Font_MeasureText(font, data, len, maxWidth, fontSize, 0, 0, HPDF_FALSE, &realWidth);

            if (cut == 0) break;

            string segment = line.substr(0, cut);
            line = line.substr(cut);

            HPDF_Page_BeginText(page);
            HPDF_Page_TextOut(page, x, y, segment.c_str());
            HPDF_Page_EndText(page);

            y -= (fontSize + 5);

            if (y < margin) {
                page = HPDF_AddPage(pdf);
                HPDF_Page_SetFontAndSize(page, font, fontSize);
                y = height - margin;
            }
        }

        // extra spacing between paragraphs
        y -= (fontSize + 5);

        if (y < margin) {
            page = HPDF_AddPage(pdf);
            HPDF_Page_SetFontAndSize(page, font, fontSize);
            y = height - margin;
        }
    }
}

inline void createRefinedPDF(const string& filename, const string& readableText) {
    HPDF_Doc pdf = HPDF_New(NULL, NULL);
    HPDF_Page page = HPDF_AddPage(pdf);

    HPDF_Page_SetSize(page, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT);
    HPDF_Font font = HPDF_GetFont(pdf, "Helvetica", NULL);

    float margin = 40;
    float fontSize = 12;

    pdfWritePrettyWrapped(pdf, page, font, readableText, fontSize, margin);

    HPDF_SaveToFile(pdf, filename.c_str());
    HPDF_Free(pdf);

    cout << "[PDF] Created: " << filename << "\n";
}

inline string refinedToReadableText(const json& j) {
    stringstream out;

    auto section = [&](const string& title) {
        out << "\n==================== " << title << " ====================\n";
        };

    auto writeValue = [&](const string& label, const string& value) {
        if (!value.empty())
            out << label << ": " << value << "\n";
        };

    auto writeArray = [&](const string& label, const json& arr) {
        if (arr.is_array() && !arr.empty()) {
            out << label << ":\n";
            for (auto& v : arr)
                out << "   - " << v.get<string>() << "\n";
            out << "\n";
        }
        };

    section("PERSON INFORMATION");
    writeValue("Name", j.value("name", ""));
    writeValue("Department", j.value("department", ""));
    writeValue("University", j.value("university", ""));
    writeValue("Designation", j.value("designation", ""));
    writeValue("Location", j.value("location", ""));

    if (!j["contact_emails"].empty() || !j["contact_phones"].empty()) {
        section("CONTACT INFORMATION");
        writeArray("Emails", j.value("contact_emails", json::array()));
        writeArray("Phones", j.value("contact_phones", json::array()));
    }

    if (!j["education"].empty()) {
        section("EDUCATION");
        writeArray("Education", j.value("education", json::array()));
    }

    if (!j["research_interests"].empty()) {
        section("RESEARCH INTERESTS");
        writeArray("Research Interests", j.value("research_interests", json::array()));
    }

    if (!j["awards"].empty()) {
        section("AWARDS");
        writeArray("Awards", j["awards"]);
    }

    if (!j["others"].empty()) {
        section("OTHER INFORMATION");
        writeArray("Others", j.value("others", json::array()));
    }

    return out.str();
}

inline string makePdfName(const string& name) {
    string clean;

    for (char c : name) {
        if (isalnum((unsigned char)c))  // keep letters/numbers only
            clean += c;
    }

    if (clean.empty())
        clean = "Person";

    return clean + "Info.pdf";
}

inline string askUserForSaveLocation(const string& suggestedName) {
    char filename[MAX_PATH] = { 0 };

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "PDF Files\0*.pdf\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Save Generated PDF As...";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = "pdf";

    // Suggested filename:
    strcpy_s(filename, suggestedName.c_str());

    if (GetSaveFileNameA(&ofn))
        return string(filename);

    return ""; // user cancelled
}