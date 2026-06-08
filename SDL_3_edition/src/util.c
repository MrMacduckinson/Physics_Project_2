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
int is_tex_path(const char* path) { return ext_equals(path_ext(path), ".tex"); }
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
    if      (ext_equals(e, ".pdf")) { b = (Color){249,38,114,255};  f = (Color){248,248,242,255}; }
    else if (ext_equals(e, ".csv")) { b = (Color){166,226,46,255};  f = (Color){39,40,34,255}; }
    else if (ext_equals(e, ".txt")) { b = (Color){102,217,239,255}; f = (Color){39,40,34,255}; }
    else if (ext_equals(e, ".md"))  { b = (Color){174,129,255,255}; f = (Color){248,248,242,255}; }
    else if (ext_equals(e, ".tex") || ext_equals(e, ".bib"))
                                    { b = (Color){253,151,31,255};  f = (Color){39,40,34,255}; }
    else if (ext_equals(e, ".mp4")) { b = (Color){102,217,239,255}; f = (Color){248,248,242,255}; }
    else                            { b = (Color){117,113,94,255};  f = (Color){248,248,242,255}; }
    *bg = b; *fg = f;
}

void str_lower(char* s)
{
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}
