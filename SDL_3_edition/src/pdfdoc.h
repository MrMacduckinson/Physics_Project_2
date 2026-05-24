/* Thin MuPDF wrapper: render a page to packed RGB, and extract text. */
#ifndef PDFDOC_H
#define PDFDOC_H

#include <stddef.h>

/* Render page `page_no` scaled to fit within box_w x box_h.
 * Returns a malloc'd packed RGB24 buffer (caller frees with free()),
 * sets out_w and out_h to the rendered size and page_count to total pages.
 * Returns NULL on failure. */
unsigned char* pdf_render_page(const char* path, int page_no,
                               int box_w, int box_h,
                               int* out_w, int* out_h, int* page_count);

/* Number of pages, or 0 on failure. */
int pdf_page_count(const char* path);

/* Extract up to `cap`-1 bytes of text (across pages) into out. Returns bytes. */
size_t pdf_extract_text(const char* path, char* out, size_t cap);

void pdf_shutdown(void);

#endif /* PDFDOC_H */
