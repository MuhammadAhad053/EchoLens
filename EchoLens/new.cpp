//#include <hpdf.h>
//#include <iostream>
//
//void error_handler(HPDF_STATUS error_no, HPDF_STATUS detail_no, void* user_data) {
//    std::cout << "HPDF Error: " << error_no << ", Detail: " << detail_no << std::endl;
//    exit(1);
//}
//
//int main() {
//    // 1. Create PDF object with error handler
//    HPDF_Doc pdf = HPDF_New(error_handler, NULL);
//    if (!pdf) {
//        std::cout << "Failed to create PDF object!" << std::endl;
//        return 1;
//    }
//
//    // 2. Add first page
//    HPDF_Page page1 = HPDF_AddPage(pdf);
//    HPDF_Page_SetSize(page1, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT);
//
//    // 3. Draw a rectangle
//    HPDF_Page_SetLineWidth(page1, 2);
//    HPDF_Page_Rectangle(page1, 50, 600, 400, 150);
//    HPDF_Page_Stroke(page1);
//
//    // 4. Add text
//    HPDF_Page_BeginText(page1);
//    HPDF_Font font = HPDF_GetFont(pdf, "Helvetica-Bold", NULL);
//    HPDF_Page_SetFontAndSize(page1, font, 24);
//    HPDF_Page_MoveTextPos(page1, 60, 700);
//    HPDF_Page_ShowText(page1, "Hello Father!");
//    HPDF_Page_EndText(page1);
//
//    // 5. Draw a line
//    HPDF_Page_MoveTo(page1, 50, 580);
//    HPDF_Page_LineTo(page1, 450, 580);
//    HPDF_Page_Stroke(page1);
//
//    // 6. Add second page
//    HPDF_Page page2 = HPDF_AddPage(pdf);
//    HPDF_Page_SetSize(page2, HPDF_PAGE_SIZE_LETTER, HPDF_PAGE_LANDSCAPE);
//
//    // 7. Add another text with different font
//    HPDF_Page_BeginText(page2);
//    HPDF_Font font2 = HPDF_GetFont(pdf, "Times-Roman", NULL);
//    HPDF_Page_SetFontAndSize(page2, font2, 20);
//    HPDF_Page_MoveTextPos(page2, 50, 500);
//    HPDF_Page_ShowText(page2, "This is the second page!");
//    HPDF_Page_EndText(page2);
//
//    // 8. Save PDF
//    HPDF_SaveToFile(pdf, "advanced_output.pdf");
//    HPDF_Free(pdf);
//
//    std::cout << "Advanced PDF created successfully!" << std::endl;
//    return 0;
//}
