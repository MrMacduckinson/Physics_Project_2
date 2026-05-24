#include "util.h"
#include <string.h>
#include <ctype.h>

const char* path_ext(const char* path)
{
    const char* dot = strrchr(path, '.');
    return dot ? dot : "";
}

const char* path_leaf(const char* path)
{
    const char* a = strrchr(path, '/');
    const char* b = strrchr(path, '\\');
    const char* p = a > b ? a : b;
    return p ? p + 1 : path;
}

int ext_equals(const char* ext, const char* needle)
{
    return SDL_strcasecmp(ext, needle) == 0;
}

int is_image_path(const char* path)
{
    const char* e = path_ext(path);
    return ext_equals(e, ".png") || ext_equals(e, ".jpg") || ext_equals(e, ".jpeg");
}
int is_text_path(const char* path)
{
    const char* e = path_ext(path);
    return ext_equals(e, ".txt") || ext_equals(e, ".csv") || ext_equals(e, ".md") ||
           ext_equals(e, ".tex") || ext_equals(e, ".bib");
}
int is_pdf_path(const char* path) { return ext_equals(path_ext(path), ".pdf"); }

int is_supported_path(const char* path)
{
    return is_image_path(path) || is_text_path(path) || is_pdf_path(path) ||
           ext_equals(path_ext(path), ".mp4");
}

void get_ext_label(const char* path, char* out, size_t cap)
{
    const char* e = path_ext(path);
    if (e[0] == '.') e++;
    size_t i = 0;
    for (; e[i] && i + 1 < cap; i++) out[i] = (char)toupper((unsigned char)e[i]);
    out[i] = '\0';
}

void get_badge_colors(const char* path, Color* bg, Color* fg)
{
    const char* e = path_ext(path);
    Color b, f;
    if      (ext_equals(e, ".pdf")) { b = (Color){150,35,25,255};  f = (Color){255,215,205,255}; }
    else if (ext_equals(e, ".csv")) { b = (Color){25,95,45,255};   f = (Color){195,245,210,255}; }
    else if (ext_equals(e, ".txt")) { b = (Color){50,70,115,255};  f = (Color){205,215,245,255}; }
    else if (ext_equals(e, ".md"))  { b = (Color){45,45,75,255};   f = (Color){195,195,240,255}; }
    else if (ext_equals(e, ".tex") || ext_equals(e, ".bib"))
                                    { b = (Color){90,60,20,255};   f = (Color){255,225,170,255}; }
    else if (ext_equals(e, ".mp4")) { b = (Color){40,40,55,255};   f = (Color){180,180,210,255}; }
    else                            { b = (Color){50,48,44,255};   f = (Color){190,185,175,255}; }
    *bg = b; *fg = f;
}

void str_lower(char* s)
{
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}
