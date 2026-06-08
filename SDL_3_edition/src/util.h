/* Small path / file-type helpers shared across modules. */
#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include "app.h"

const char* path_ext(const char* path);              /* ".pdf" or "" */
const char* path_leaf(const char* path);             /* filename after / or \ */
int  ext_equals(const char* ext, const char* needle);/* case-insensitive */

int  is_image_path(const char* path);   /* png/jpg/jpeg */
int  is_text_path(const char* path);    /* txt/csv/md/tex/bib */
int  is_tex_path(const char* path);     /* tex only */
int  is_pdf_path(const char* path);
int  is_supported_path(const char* path);

void get_ext_label(const char* path, char* out, size_t cap);   /* "PDF", "CSV" */
void get_badge_colors(const char* path, Color* bg, Color* fg);

void str_lower(char* s);

#endif /* UTIL_H */
