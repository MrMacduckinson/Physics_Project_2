/* SDL 1.2 entrypoint (refactored): wiring for store/views/textedit modules. */
#include "app.h"
#include "store.h"
#include "views.h"
#include "textedit.h"
#include "mac_file_dialog.h"

#include <SDL/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <sys/stat.h>

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_THREAD_LOCALS
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#if defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#endif
#if defined(__APPLE__) && !defined(BUILD_FOR_10_4)
#include <CoreText/CoreText.h>
#endif

#if defined(__MACOS__)
#include <StandardFile.h>
#include <Files.h>

static char g_browse_path[512];

static void s_pstr_to_cstr(const unsigned char* src, char* dst, size_t cap)
{
    size_t len = src[0]; if (len >= cap) len = cap - 1;
    memcpy(dst, src + 1, len); dst[len] = '\0';
}

static int s_path_append(char* path, size_t cap, const char* comp)
{
    size_t len = strlen(path), clen = strlen(comp);
    if (len > 0 && path[len-1] != ':') { if (len+1 >= cap) return 0; path[len++]=':'; path[len]='\0'; }
    if (len + clen >= cap) return 0;
    memcpy(path + len, comp, clen + 1); return 1;
}

static int s_fsspec_to_hfs(const FSSpec* spec, char* out, size_t cap)
{
    unsigned char volname[256]; memset(volname, 0, sizeof(volname));
    HVolumeParam vol; memset(&vol, 0, sizeof(vol));
    vol.ioVRefNum = spec->vRefNum; vol.ioNamePtr = (StringPtr)volname;
    if (PBHGetVInfo((HParmBlkPtr)&vol, false) != noErr || !volname[0]) return 0;
    char pieces[16][64]; int pc = 0; long pid = spec->parID;
    while (pid != fsRtParID && pc < 16) {
        DirInfo di; unsigned char dn[256]; memset(&di,0,sizeof(di)); memset(dn,0,sizeof(dn));
        di.ioVRefNum = spec->vRefNum; di.ioDrDirID = pid;
        di.ioFDirIndex = -1; di.ioNamePtr = (StringPtr)dn;
        if (PBGetCatInfo((CInfoPBPtr)&di, false) != noErr || !dn[0]) break;
        s_pstr_to_cstr(dn, pieces[pc], 64); pid = di.ioDrParID; pc++;
    }
    out[0] = '\0'; s_pstr_to_cstr(volname, out, cap);
    for (int i = pc-1; i >= 0; i--) if (!s_path_append(out, cap, pieces[i])) return 0;
    char fn[64]; s_pstr_to_cstr(spec->name, fn, 64);
    return s_path_append(out, cap, fn);
}

const char* mac_file_dialog(const char* title, const char* filters, const char* initial_dir)
{
    (void)title; (void)filters; (void)initial_dir;
    static char path[512];
    StandardFileReply reply; memset(&reply, 0, sizeof(reply));
    StandardGetFile(NULL, 0, NULL, &reply);
    if (!reply.sfGood) return NULL;
    if (!s_fsspec_to_hfs(&reply.sfFile, path, sizeof(path))) return NULL;
    safe_copy(g_browse_path, sizeof(g_browse_path), path);
    return path;
}

#elif defined(BUILD_FOR_10_4)
const char* mac_file_dialog(const char* title, const char* filters, const char* initial_dir)
{
    (void)title; (void)filters; (void)initial_dir;
    return NULL;
}
#endif

#ifndef KMOD_GUI
#ifdef KMOD_META
#define KMOD_GUI KMOD_META
#else
#define KMOD_GUI 0
#endif
#endif

#ifdef USE_SDL_TTF
#include "SDL_ttf.h"
TTF_Font* g_ttf_normal = NULL; /* ~14pt */
TTF_Font* g_ttf_large  = NULL; /* ~28pt */
int g_ttf_char_w  = 8;
int g_ttf_char_w2 = 14;
#endif

SDL_Surface* screen = NULL;
#if defined(BUILD_FOR_11_0) || defined(BUILD_FOR_10_7)
static SDL_Surface* g_win_surf = NULL;
static int g_resize_pending_w = 0, g_resize_pending_h = 0, g_resize_idle = 0;
#endif

/* ---- helpers ------------------------------------------------------------ */
void safe_copy(char* dst, size_t cap, const char* src)
{
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

const char* path_ext(const char* path)
{
    const char* dot = strrchr(path, '.');
    return dot ? dot : "";
}

int ext_equals(const char* ext, const char* needle)
{
    return ascii_strcasecmp(ext, needle) == 0;
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

int is_pdf_path(const char* path)
{
    return ext_equals(path_ext(path), ".pdf");
}

int is_tex_path(const char* path)
{
    return ext_equals(path_ext(path), ".tex") || ext_equals(path_ext(path), ".bib");
}

void get_ext_label(const char* path, char* out, size_t cap)
{
    const char* e = path_ext(path);
    if (e[0] == '.') e++;
    size_t i = 0;
    for (; e[i] && i + 1 < cap; i++) out[i] = (char)toupper((unsigned char)e[i]);
    out[i] = '\0';
}

void get_doc_badge_colors(const char* path, Uint32* bg, Uint32* fg)
{
    const char* e = path_ext(path);
    if (ext_equals(e, ".pdf"))
        { *bg = MK_COL(249, 38, 114);  *fg = MK_COL(248, 248, 242); }
    else if (ext_equals(e, ".csv"))
        { *bg = MK_COL(166, 226, 46);  *fg = MK_COL(39, 40, 34); }
    else if (ext_equals(e, ".txt"))
        { *bg = MK_COL(102, 217, 239); *fg = MK_COL(39, 40, 34); }
    else if (ext_equals(e, ".md"))
        { *bg = MK_COL(174, 129, 255); *fg = MK_COL(248, 248, 242); }
    else if (ext_equals(e, ".tex") || ext_equals(e, ".bib"))
        { *bg = MK_COL(253, 151, 31);  *fg = MK_COL(39, 40, 34); }
    else if (ext_equals(e, ".mp4"))
        { *bg = MK_COL(102, 217, 239); *fg = MK_COL(248, 248, 242); }
    else
        { *bg = MK_COL(117, 113, 94);  *fg = MK_COL(248, 248, 242); }
}

/* ---- drawing ------------------------------------------------------------ */
void fill_rect(int x, int y, int w, int h, Uint32 color)
{
    SDL_Rect r; r.x = x; r.y = y; r.w = w; r.h = h;
    SDL_FillRect(screen, &r, color);
}

/* Fallback bitmap font for non-TTF builds. */
static const unsigned char font5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5f,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7f,0x14,0x7f,0x14},{0x24,0x2a,0x7f,0x2a,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1c,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1c,0x00},{0x14,0x08,0x3e,0x08,0x14},{0x08,0x08,0x3e,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02},{0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},
    {0x27,0x45,0x45,0x45,0x39},{0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e},{0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3e},
    {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
    {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},{0x00,0x41,0x7f,0x41,0x00},
    {0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x04,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
    {0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},{0x3f,0x40,0x40,0x40,0x3f},
    {0x1f,0x20,0x40,0x20,0x1f},{0x7f,0x20,0x18,0x20,0x7f},{0x63,0x14,0x08,0x14,0x63},
    {0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7f,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7f,0x00},{0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
    {0x7f,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7f},
    {0x38,0x54,0x54,0x54,0x18},{0x08,0x7e,0x09,0x01,0x02},{0x0c,0x52,0x52,0x52,0x3e},
    {0x7f,0x08,0x04,0x04,0x78},{0x00,0x44,0x7d,0x40,0x00},{0x20,0x40,0x44,0x3d,0x00},
    {0x7f,0x10,0x28,0x44,0x00},{0x00,0x41,0x7f,0x40,0x00},{0x7c,0x04,0x18,0x04,0x78},
    {0x7c,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7c,0x14,0x14,0x14,0x08},
    {0x08,0x14,0x14,0x18,0x7c},{0x7c,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3f,0x44,0x40,0x20},{0x3c,0x40,0x40,0x20,0x7c},{0x1c,0x20,0x40,0x20,0x1c},
    {0x3c,0x40,0x30,0x40,0x3c},{0x44,0x28,0x10,0x28,0x44},{0x0c,0x50,0x50,0x50,0x3c},
    {0x44,0x64,0x54,0x4c,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7f,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00},{0x08,0x04,0x08,0x10,0x08}
};

static void draw_char_fallback(int x, int y, char c, Uint32 color, int scale)
{
    if (c < 32 || c > 126) return;
    const unsigned char* glyph = font5x7[c - 32];
    SDL_Rect pixel; pixel.w = scale; pixel.h = scale;
    for (int col = 0; col < 5; col++) {
        unsigned char bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                pixel.x = x + col * scale;
                pixel.y = y + row * scale;
                SDL_FillRect(screen, &pixel, color);
            }
        }
    }
}

static void draw_text_fallback(int x, int y, const char* text, Uint32 color, int scale)
{
    int cx = x;
    for (size_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\n') { cx = x; y += 8 * scale; continue; }
        draw_char_fallback(cx, y, text[i], color, scale);
        cx += 6 * scale;
    }
}

void draw_text(int x, int y, const char* text, Uint32 color, int scale)
{
    if (!text || !text[0]) return;
#ifdef USE_SDL_TTF
    TTF_Font* ttf = (scale > 1) ? g_ttf_large : g_ttf_normal;
    if (ttf) {
        Uint8 r, g, b, a;
        SDL_GetRGBA(color, screen->format, &r, &g, &b, &a);
        SDL_Color c = { r, g, b, 0 };
        SDL_Surface* surf = TTF_RenderUTF8_Blended(ttf, text, c);
        if (surf) {
            SDL_SetAlpha(surf, SDL_SRCALPHA, 255);
            SDL_Rect dst; dst.x = x; dst.y = y; dst.w = surf->w; dst.h = surf->h;
            SDL_BlitSurface(surf, NULL, screen, &dst);
            SDL_FreeSurface(surf);
        }
        return;
    }
#endif
    draw_text_fallback(x, y, text, color, scale);
}

void draw_text_fit(int x, int y, const char* value, Uint32 color, int scale, int max_px)
{
    if (!value) return;
    char line[512];
    size_t len = strlen(value);
#ifdef USE_SDL_TTF
    int char_px = (scale > 1) ? g_ttf_char_w2 : g_ttf_char_w;
    if (char_px < 1) char_px = 8;
#else
    int char_px = (scale > 1) ? (scale * 7) : 8;
#endif
    int max_chars = max_px / char_px;
    if (max_chars < 1) max_chars = 1;
    if ((int)len > max_chars) {
        int out_len = max_chars;
        if (out_len >= (int)sizeof(line)) out_len = (int)sizeof(line) - 1;
        if (out_len >= 4) {
            out_len -= 3;
            memcpy(line, value, (size_t)out_len);
            line[out_len] = '.'; line[out_len + 1] = '.'; line[out_len + 2] = '.'; line[out_len + 3] = '\0';
        } else {
            memcpy(line, value, (size_t)out_len);
            line[out_len] = '\0';
        }
    } else {
        size_t out_len = len;
        if (out_len >= sizeof(line)) out_len = sizeof(line) - 1;
        memcpy(line, value, out_len);
        line[out_len] = '\0';
    }
    draw_text(x, y, line, color, scale);
}

void draw_button(int x, int y, int w, int h, const char* label, Uint32 bg, Uint32 fg, int pressed)
{
    Uint32 shadow = MK_COL(12, 10, 8);
    Uint32 border = pressed ? COL_BORDER_HI : COL_BORDER;
    Uint32 body = pressed ? COL_CARD_HI : bg;
    fill_rect(x + 2, y + 2, w, h, shadow);
    fill_rect(x, y, w, h, border);
    fill_rect(x + 1, y + 1, w - 2, h - 2, body);
    int text_w = (int)strlen(label) * 8;
#ifdef USE_SDL_TTF
    if (g_ttf_normal) TTF_SizeUTF8(g_ttf_normal, label, &text_w, NULL);
#endif
    int tx = x + (w - text_w) / 2;
    if (tx < x + 8) tx = x + 8;
    draw_text_fit(tx, y + (h - 16) / 2 + (pressed ? 1 : 0), label, fg, 1, w - 16);
}

int point_in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    return (x >= rx && y >= ry && x < rx + rw && y < ry + rh);
}

/* ---- image helpers ------------------------------------------------------ */
SDL_Surface* load_image_surface(const char* path, int max_w, int max_h)
{
    (void)max_w; (void)max_h;
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f); rewind(f);
    if (size <= 0) { fclose(f); return NULL; }
    unsigned char* buf = (unsigned char*)malloc((size_t)size);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) { fclose(f); free(buf); return NULL; }
    fclose(f);

    int w = 0, h = 0, n = 0;
    unsigned char* pixels = stbi_load_from_memory(buf, (int)size, &w, &h, &n, 4);
    free(buf);
    if (!pixels || w <= 0 || h <= 0) { if (pixels) stbi_image_free(pixels); return NULL; }

#if defined(BUILD_FOR_11_0) || defined(BUILD_FOR_10_7)
    /* sdl12-compat on Apple Silicon flips y when presenting via Metal. */
    if (h > 1) {
        unsigned char* tmp = (unsigned char*)malloc((size_t)w * 4);
        if (tmp) {
            for (int row = 0; row < h / 2; row++) {
                unsigned char* top = pixels + row * w * 4;
                unsigned char* bot = pixels + (h - 1 - row) * w * 4;
                memcpy(tmp, top, (size_t)w * 4);
                memcpy(top, bot, (size_t)w * 4);
                memcpy(bot, tmp, (size_t)w * 4);
            }
            free(tmp);
        }
    }
#endif

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    Uint32 rmask = 0xff000000, gmask = 0x00ff0000, bmask = 0x0000ff00, amask = 0x000000ff;
#else
    Uint32 rmask = 0x000000ff, gmask = 0x0000ff00, bmask = 0x00ff0000, amask = 0xff000000;
#endif
    SDL_Surface* surf = SDL_CreateRGBSurface(0, w, h, 32, rmask, gmask, bmask, amask);
    if (!surf) { stbi_image_free(pixels); return NULL; }
    SDL_LockSurface(surf);
    for (int row = 0; row < h; row++) {
        unsigned char* dst = (unsigned char*)surf->pixels + row * surf->pitch;
        unsigned char* src = pixels + row * w * 4;
        memcpy(dst, src, (size_t)w * 4);
    }
    SDL_UnlockSurface(surf);
    stbi_image_free(pixels);
    return surf;
}

SDL_Surface* scale_surface(SDL_Surface* src, int max_w, int max_h)
{
    if (!src) return NULL;
    float sx = (float)max_w / (float)src->w;
    float sy = (float)max_h / (float)src->h;
    float s = (sx < sy) ? sx : sy;
    if (s >= 1.0f) return src;
    int nw = (int)(src->w * s); if (nw < 1) nw = 1;
    int nh = (int)(src->h * s); if (nh < 1) nh = 1;
    SDL_Surface* dst = SDL_CreateRGBSurface(0, nw, nh,
        src->format->BitsPerPixel, src->format->Rmask,
        src->format->Gmask, src->format->Bmask, src->format->Amask);
    if (!dst) return src;
    SDL_SoftStretch(src, NULL, dst, NULL);
    return dst;
}

/* ---- clipboard ---------------------------------------------------------- */
#if defined(__APPLE__) && !defined(__MACOS__)
int clipboard_set_text(const char* text)
{
    PasteboardRef pb = NULL;
    if (PasteboardCreate(kPasteboardClipboard, &pb) != noErr || !pb) return 0;
    PasteboardClear(pb);
    PasteboardSynchronize(pb);
    CFDataRef data = CFDataCreate(NULL, (const UInt8*)text, (CFIndex)strlen(text));
    if (!data) { CFRelease(pb); return 0; }
    OSStatus status = PasteboardPutItemFlavor(pb, (PasteboardItemID)1, CFSTR("public.utf8-plain-text"), data, 0);
    CFRelease(data);
    CFRelease(pb);
    return (status == noErr);
}

char* clipboard_get_text(void)
{
    PasteboardRef pb = NULL;
    if (PasteboardCreate(kPasteboardClipboard, &pb) != noErr || !pb) return NULL;
    PasteboardSynchronize(pb);

    ItemCount count = 0;
    if (PasteboardGetItemCount(pb, &count) != noErr || count == 0) { CFRelease(pb); return NULL; }

    for (ItemCount i = 1; i <= count; i++) {
        PasteboardItemID item = 0;
        if (PasteboardGetItemIdentifier(pb, i, &item) != noErr) continue;
        CFDataRef data = NULL;
        if (PasteboardCopyItemFlavorData(pb, item, CFSTR("public.utf8-plain-text"), &data) != noErr || !data) continue;
        CFIndex len = CFDataGetLength(data);
        const UInt8* bytes = CFDataGetBytePtr(data);
        char* out = (char*)malloc((size_t)len + 1);
        if (out) { memcpy(out, bytes, (size_t)len); out[len] = '\0'; }
        CFRelease(data); CFRelease(pb);
        return out;
    }
    CFRelease(pb);
    return NULL;
}
#else
int clipboard_set_text(const char* text) { (void)text; return 0; }
char* clipboard_get_text(void) { return NULL; }
#endif

/* ---- app helpers -------------------------------------------------------- */
static void set_status(AppState* app, const char* msg)
{
    safe_copy(app->status_line, sizeof(app->status_line), msg);
}

static int file_exists(const char* path)
{
    struct stat st;
    return (path && stat(path, &st) == 0);
}

static void merge_tag_fields(const char* tags, const char* meta, char* out, size_t cap)
{
    out[0] = '\0';
    if (tags && tags[0]) safe_copy(out, cap, tags);
    if (meta && meta[0]) {
        if (out[0]) strncat(out, " ", cap - strlen(out) - 1);
        strncat(out, meta, cap - strlen(out) - 1);
    }
}

static void split_tags(const char* in, char* tags, size_t tcap, char* meta, size_t mcap)
{
    tags[0] = '\0'; meta[0] = '\0';
    const char* p = in;
    while (*p) {
        while (*p == ' ') p++;
        const char* start = p;
        while (*p && *p != ' ') p++;
        if (p > start) {
            char tok[128]; size_t len = (size_t)(p - start);
            if (len >= sizeof(tok)) len = sizeof(tok) - 1;
            memcpy(tok, start, len); tok[len] = '\0';
            int kind = store_tag_kind(tok);
            if (kind == 2) {
                if (meta[0]) strncat(meta, " ", mcap - strlen(meta) - 1);
                strncat(meta, tok, mcap - strlen(meta) - 1);
            } else {
                if (tags[0]) strncat(tags, " ", tcap - strlen(tags) - 1);
                strncat(tags, tok, tcap - strlen(tags) - 1);
            }
        }
    }
}

static int next_visible_item(const AppState* app, int from, int dir)
{
    int i = from + dir;
    while (i >= 0 && i < app->list.count) {
        if (item_matches(&app->list.items[i], i, app->search, app->search_content)) return i;
        i += dir;
    }
    return -1;
}

static void open_add(AppState* app, int edit_index)
{
    app->view = VIEW_ADD;
    app->edit_index = edit_index;
    app->show_cover_options = 0;
    if (edit_index >= 0) {
        const Item* it = &app->list.items[edit_index];
        merge_tag_fields(it->tags, it->meta, app->form_tags, sizeof(app->form_tags));
        safe_copy(app->form_path, sizeof(app->form_path), it->filename);
        safe_copy(app->form_cover, sizeof(app->form_cover), it->cover);
    } else {
        app->form_tags[0] = '\0';
        app->form_path[0] = '\0';
        app->form_cover[0] = '\0';
    }
    app->focus = FOCUS_TAGS;
    ed_select_all(&app->ed_tags, app->form_tags);
    views_update_suggestions(app);
}

static int save_form(AppState* app)
{
    if (!app->form_path[0]) { set_status(app, "Please choose a file."); return 0; }

    char tags[TAGS_LEN], meta[TAGS_LEN];
    split_tags(app->form_tags, tags, sizeof(tags), meta, sizeof(meta));

    if (app->edit_index >= 0) {
        Item* it = &app->list.items[app->edit_index];
        int external = ((strchr(app->form_path, '/') || strchr(app->form_path, '\\')) && file_exists(app->form_path));
        if (external) {
            char newname[PATH_MAX_LEN];
            if (store_import_file(app->form_path, newname, sizeof(newname))) {
                store_delete_file(it->filename);
                safe_copy(it->filename, sizeof(it->filename), newname);
            }
        }
        safe_copy(it->tags, sizeof(it->tags), tags);
        safe_copy(it->meta, sizeof(it->meta), meta);
        safe_copy(it->cover, sizeof(it->cover), app->form_cover);
        store_save(&app->list);
        store_invalidate_content();
        set_status(app, "Saved.");
    } else {
        if (app->list.count >= ITEM_MAX) { set_status(app, "Archive is full."); return 0; }
        char newname[PATH_MAX_LEN];
        if (!store_import_file(app->form_path, newname, sizeof(newname))) {
            set_status(app, "Could not import that file.");
            return 0;
        }
        Item* it = &app->list.items[app->list.count++];
        store_gen_id(it->id, sizeof(it->id));
        safe_copy(it->filename, sizeof(it->filename), newname);
        safe_copy(it->tags, sizeof(it->tags), tags);
        safe_copy(it->meta, sizeof(it->meta), meta);
        safe_copy(it->cover, sizeof(it->cover), app->form_cover);
        store_save(&app->list);
        store_invalidate_content();
        set_status(app, "Added.");
    }
    views_clear_caches();
    app->view = VIEW_LIST;
    app->focus = FOCUS_SEARCH;
    app->list_scroll = 0;
    return 1;
}

static void delete_selected(AppState* app)
{
    if (app->selected < 0 || app->selected >= app->list.count) return;
    store_delete_file(app->list.items[app->selected].filename);
    for (int i = app->selected; i < app->list.count - 1; i++)
        app->list.items[i] = app->list.items[i + 1];
    app->list.count--;
    if (app->selected >= app->list.count) app->selected = app->list.count - 1;
    store_save(&app->list);
    store_invalidate_content();
    views_clear_caches();
    set_status(app, "Deleted.");
}

static char* focus_buf(AppState* app, size_t* cap, EditState** ed)
{
    switch (app->focus) {
        case FOCUS_SEARCH:         *cap = SEARCH_LEN; *ed = &app->ed_search; return app->search;
        case FOCUS_SEARCH_CONTENT: *cap = SEARCH_LEN; *ed = &app->ed_search_content; return app->search_content;
        case FOCUS_TAGS:           *cap = TAGS_LEN;   *ed = &app->ed_tags; return app->form_tags;
        case FOCUS_PATH:           *cap = PATH_MAX_LEN; *ed = &app->ed_path; return app->form_path;
        case FOCUS_COVER:          *cap = PATH_MAX_LEN; *ed = &app->ed_cover; return app->form_cover;
        default: break;
    }
    *cap = 0; *ed = NULL; return NULL;
}

static void apply_suggestion(AppState* app)
{
    if (!app->sug_open || app->sug_count <= 0) return;
    int idx = app->sug_active;
    if (idx < 0 || idx >= app->sug_count) idx = 0;
    const char* tok = app->sug[idx].tok;
    if (!tok || !tok[0]) return;

    size_t n = strlen(app->form_tags);
    while (n > 0 && app->form_tags[n-1] == ' ') n--;
    size_t start = n;
    while (start > 0 && app->form_tags[start-1] != ' ') start--;
    app->form_tags[start] = '\0';
    if (app->form_tags[0]) strncat(app->form_tags, " ", sizeof(app->form_tags) - strlen(app->form_tags) - 1);
    strncat(app->form_tags, tok, sizeof(app->form_tags) - strlen(app->form_tags) - 1);
    strncat(app->form_tags, " ", sizeof(app->form_tags) - strlen(app->form_tags) - 1);
    ed_end(&app->ed_tags, app->form_tags, 0);
    views_update_suggestions(app);
}

static void open_ctx(AppState* app, FocusKind f, int x, int y)
{
    app->ctx_open = 1;
    app->ctx_focus = f;
    app->ctx_mx = x;
    app->ctx_my = y;
}

static void close_ctx(AppState* app) { app->ctx_open = 0; }

static void do_ctx_action(AppState* app, HitTarget t)
{
    char* buf = NULL; size_t cap = 0; EditState* ed = NULL;
    if (app->ctx_focus == FOCUS_SEARCH)         { buf = app->search;         cap = SEARCH_LEN; ed = &app->ed_search; }
    else if (app->ctx_focus == FOCUS_SEARCH_CONTENT) { buf = app->search_content; cap = SEARCH_LEN; ed = &app->ed_search_content; }
    else if (app->ctx_focus == FOCUS_TAGS)      { buf = app->form_tags;      cap = TAGS_LEN;   ed = &app->ed_tags; }
    else if (app->ctx_focus == FOCUS_PATH)      { buf = app->form_path;      cap = PATH_MAX_LEN; ed = &app->ed_path; }
    else if (app->ctx_focus == FOCUS_COVER)     { buf = app->form_cover;     cap = PATH_MAX_LEN; ed = &app->ed_cover; }
    if (!buf || !ed) return;

    if (t == HIT_CTX_ADD_TAG || t == HIT_CTX_ADD_META) {
        char tok[128];
        size_t n = strlen(app->form_tags);
        while (n > 0 && app->form_tags[n-1] == ' ') n--;
        size_t start = n;
        while (start > 0 && app->form_tags[start-1] != ' ') start--;
        size_t len = n - start;
        if (len > 0 && len < sizeof(tok)) {
            memcpy(tok, app->form_tags + start, len); tok[len] = '\0';
            int kind = (t == HIT_CTX_ADD_META) ? 2 : 1;
            if (store_tag_kind(tok) == 0) {
                store_add_tag(tok, kind);
                set_status(app, kind == 2 ? "Added metatag" : "Added tag");
                views_update_suggestions(app);
            }
        }
        return;
    }

    if (t == HIT_CTX_SELECT_ALL) ed_select_all(ed, buf);
    else if (t == HIT_CTX_COPY)  ed_copy(ed, buf);
    else if (t == HIT_CTX_CUT)   ed_cut(ed, buf);
    else if (t == HIT_CTX_PASTE) {
        char* clip = clipboard_get_text();
        if (clip) { ed_insert(ed, buf, cap, clip); free(clip); }
    }
}

/* ---- main --------------------------------------------------------------- */
int sdl_app(int argc, char* argv[])
{
    (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    atexit(SDL_Quit);
    SDL_EnableUNICODE(1);

#if defined(BUILD_FOR_11_0) || defined(BUILD_FOR_10_7)
    g_win_surf = SDL_SetVideoMode(WINDOW_W, WINDOW_H, 0, SDL_SWSURFACE | SDL_RESIZABLE);
    if (!g_win_surf) {
        fprintf(stderr, "Failed to open window: %s\n", SDL_GetError());
        return 1;
    }
    screen = SDL_CreateRGBSurface(SDL_SWSURFACE, WINDOW_W, WINDOW_H,
        g_win_surf->format->BitsPerPixel,
        g_win_surf->format->Rmask, g_win_surf->format->Gmask,
        g_win_surf->format->Bmask, 0);
    if (!screen) {
        fprintf(stderr, "Failed to create canvas: %s\n", SDL_GetError());
        return 1;
    }
#else
    screen = SDL_SetVideoMode(WINDOW_W, WINDOW_H, 0, SDL_SWSURFACE);
    if (!screen) {
        fprintf(stderr, "Failed to open window: %s\n", SDL_GetError());
        return 1;
    }
#endif
    SDL_WM_SetCaption("Memebooru", NULL);

#ifdef USE_SDL_TTF
    if (TTF_Init() >= 0) {
        const char* font_path = "/System/Library/Fonts/Helvetica.ttc";
        g_ttf_normal = TTF_OpenFontIndex(font_path, 14, 0);
        g_ttf_large  = TTF_OpenFontIndex(font_path, 28, 0);
        if (g_ttf_normal) {
            int w = 0, h = 0; TTF_SizeUTF8(g_ttf_normal, "MMMMMMMMMM", &w, &h);
            if (w > 0) g_ttf_char_w = w / 10;
        }
        if (g_ttf_large) {
            int w = 0, h = 0; TTF_SizeUTF8(g_ttf_large, "MMMMMMMMMM", &w, &h);
            if (w > 0) g_ttf_char_w2 = w / 10;
        }
    }
#endif

    static AppState app;
    memset(&app, 0, sizeof(app));
    app.view = VIEW_LIST;
    app.focus = FOCUS_SEARCH;
    app.selected = -1;
    app.gallery_cursor = -1;
    app.sug_open = 0;
    set_status(&app, "Ready");

    store_init();
    store_load(&app.list);
    if (app.list.count > 0) { app.selected = 0; app.gallery_cursor = 0; }

    int running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = 0; break; }
            if (e.type == SDL_MOUSEBUTTONDOWN) {
                int mx = e.button.x, my = e.button.y;
                if (e.button.button == 4 || e.button.button == 5) {
                    int delta = (e.button.button == 4) ? -48 : 48;
                    if (app.view == VIEW_LIST) {
                        app.list_scroll += delta;
                        if (app.list_scroll < 0) app.list_scroll = 0;
                    }
                    continue;
                }
                if (e.button.button == SDL_BUTTON_RIGHT) {
                    Hit hit = views_hittest(&app, mx, my);
                    if (hit.target == HIT_SEARCH) open_ctx(&app, FOCUS_SEARCH, mx, my);
                    else if (hit.target == HIT_SEARCH_CONTENT) open_ctx(&app, FOCUS_SEARCH_CONTENT, mx, my);
                    else if (hit.target == HIT_A_TAGS) open_ctx(&app, FOCUS_TAGS, mx, my);
                    else if (hit.target == HIT_A_PATH) open_ctx(&app, FOCUS_PATH, mx, my);
                    else if (hit.target == HIT_A_COVER) open_ctx(&app, FOCUS_COVER, mx, my);
                    else close_ctx(&app);
                }
                if (e.button.button == SDL_BUTTON_LEFT) {
                    Hit hit = views_hittest(&app, mx, my);
                    app.active_button = hit.target;
                    if (app.ctx_open) {
                        if (hit.target == HIT_CTX_DISMISS) close_ctx(&app);
                        else if (hit.target >= HIT_CTX_SELECT_ALL && hit.target <= HIT_CTX_ADD_META) {
                            do_ctx_action(&app, hit.target);
                            close_ctx(&app);
                        }
                        continue;
                    }

                    if (app.sug_open && hit.target == HIT_A_TAGS && my >= app.sug_y) {
                        apply_suggestion(&app);
                        continue;
                    }

                    if (hit.target == HIT_LIST_ADD) open_add(&app, -1);
                    else if (hit.target == HIT_ITEM) { app.selected = hit.index; app.view = VIEW_DETAIL; app.focus = FOCUS_NONE; }
                    else if (hit.target == HIT_D_BACK) { app.view = VIEW_LIST; app.focus = FOCUS_SEARCH; }
                    else if (hit.target == HIT_D_PREV) {
                        int p = next_visible_item(&app, app.selected, -1);
                        if (p >= 0) app.selected = p;
                    }
                    else if (hit.target == HIT_D_NEXT) {
                        int n = next_visible_item(&app, app.selected, 1);
                        if (n >= 0) app.selected = n;
                    }
                    else if (hit.target == HIT_D_EDIT) { if (app.selected >= 0) open_add(&app, app.selected); }
                    else if (hit.target == HIT_D_DELETE) { delete_selected(&app); app.view = VIEW_LIST; app.focus = FOCUS_SEARCH; }
                    else if (hit.target == HIT_D_TAG) {
                        const char* tags = app.list.items[app.selected].tags;
                        int off = hit.index;
                        if (off < 0) off = 0;
                        const char* start = tags + off;
                        while (*start == ' ') start++;
                        const char* end = start;
                        while (*end && *end != ' ') end++;
                        size_t len = (size_t)(end - start);
                        if (len > 0 && len < sizeof(app.search) - 1) {
                            memcpy(app.search, start, len);
                            app.search[len] = '\0';
                            app.view = VIEW_LIST;
                            app.list_scroll = 0;
                            app.focus = FOCUS_SEARCH;
                        }
                    }
                    else if (hit.target == HIT_A_TAGS) { app.focus = FOCUS_TAGS; ed_select_all(&app.ed_tags, app.form_tags); }
                    else if (hit.target == HIT_A_PATH) { app.focus = FOCUS_PATH; ed_select_all(&app.ed_path, app.form_path); }
                    else if (hit.target == HIT_A_COVER) { app.focus = FOCUS_COVER; ed_select_all(&app.ed_cover, app.form_cover); }
                    else if (hit.target == HIT_A_MORE_OPTIONS) { app.show_cover_options = !app.show_cover_options; }
                    else if (hit.target == HIT_A_BROWSE) {
                        const char* path = mac_file_dialog("Select file", "png;jpg;jpeg;mp4;pdf;txt;csv;md;tex;bib", NULL);
                        if (path) { safe_copy(app.form_path, sizeof(app.form_path), path); app.focus = FOCUS_PATH; }
                    }
                    else if (hit.target == HIT_A_COVER_BROWSE) {
                        const char* path = mac_file_dialog("Select cover", "png;jpg;jpeg", NULL);
                        if (path) { safe_copy(app.form_cover, sizeof(app.form_cover), path); app.focus = FOCUS_COVER; }
                    }
                    else if (hit.target == HIT_A_SAVE) { save_form(&app); }
                    else if (hit.target == HIT_A_CANCEL) { app.view = VIEW_LIST; app.focus = FOCUS_SEARCH; }
                    else if (hit.target == HIT_D_CENTER) { app.focus = FOCUS_TXTED; }
                }
            } else if (e.type == SDL_MOUSEBUTTONUP) {
                app.active_button = HIT_NONE;
            } else if (e.type == SDL_KEYDOWN) {
                SDLKey sym = e.key.keysym.sym;
                Uint16 mod = e.key.keysym.mod;
                int shift = (mod & KMOD_SHIFT) != 0;
                int primary = ((mod & KMOD_GUI) != 0) || ((mod & KMOD_CTRL) != 0);

                if (app.focus == FOCUS_TXTED) {
                    if (txted_key(&app, sym, shift, primary)) continue;
                    Uint16 uni = e.key.keysym.unicode;
                    if (uni >= 32 && uni < 127) {
                        char ch[2] = { (char)uni, '\0' };
                        txted_input(&app, ch);
                    }
                    continue;
                }

                char* buf = NULL; size_t cap = 0; EditState* ed = NULL;
                buf = focus_buf(&app, &cap, &ed);

                if (buf && ed) {
                    if (primary) {
                        if (sym == SDLK_a) { ed_select_all(ed, buf); continue; }
                        if (sym == SDLK_c) { ed_copy(ed, buf); continue; }
                        if (sym == SDLK_x) { ed_cut(ed, buf); continue; }
                        if (sym == SDLK_v) { char* clip = clipboard_get_text(); if (clip) { ed_insert(ed, buf, cap, clip); free(clip); } continue; }
                    }

                    if (app.focus == FOCUS_TAGS && app.sug_open && app.sug_count > 0) {
                        if (sym == SDLK_DOWN) { app.sug_active = (app.sug_active + 1) % app.sug_count; continue; }
                        if (sym == SDLK_UP)   { app.sug_active = (app.sug_active + app.sug_count - 1) % app.sug_count; continue; }
                        if (sym == SDLK_TAB || sym == SDLK_RETURN) { apply_suggestion(&app); continue; }
                    }

                    if (sym == SDLK_LEFT)      { ed_move(ed, buf, -1, shift); continue; }
                    if (sym == SDLK_RIGHT)     { ed_move(ed, buf,  1, shift); continue; }
                    if (sym == SDLK_HOME)      { ed_home(ed, shift); continue; }
                    if (sym == SDLK_END)       { ed_end(ed, buf, shift); continue; }
                    if (sym == SDLK_BACKSPACE) { ed_backspace(ed, buf); goto after_edit; }
                    if (sym == SDLK_DELETE)    { ed_delete_fwd(ed, buf); goto after_edit; }

                    if (sym == SDLK_RETURN && app.view == VIEW_ADD) {
                        if (app.focus == FOCUS_TAGS) app.focus = FOCUS_PATH;
                        else if (app.focus == FOCUS_PATH) save_form(&app);
                        continue;
                    }

                    Uint16 uni = e.key.keysym.unicode;
                    if (uni >= 32 && uni < 127) {
                        char ch[2] = { (char)uni, '\0' };
                        ed_insert(ed, buf, cap, ch);
                    }
                }

after_edit:
                if (app.focus == FOCUS_TAGS && app.view == VIEW_ADD)
                    views_update_suggestions(&app);
                if (app.focus == FOCUS_SEARCH)
                    app.list_scroll = 0;

                if (app.view == VIEW_LIST && !buf) {
                    if (sym == SDLK_DOWN) { if (app.gallery_cursor + 1 < app.list.count) app.gallery_cursor++; views_scroll_to_cursor(&app); }
                    if (sym == SDLK_UP)   { if (app.gallery_cursor > 0) app.gallery_cursor--; views_scroll_to_cursor(&app); }
                    if (sym == SDLK_RETURN && app.gallery_cursor >= 0) { app.selected = app.gallery_cursor; app.view = VIEW_DETAIL; }
                }
            }
#if defined(BUILD_FOR_11_0) || defined(BUILD_FOR_10_7)
            else if (e.type == SDL_VIDEORESIZE) {
                int nw = e.resize.w, nh = e.resize.h;
                if (!screen || screen->w != nw || screen->h != nh) {
                    SDL_Surface* old_s = screen;
                    Uint32 rm = g_win_surf ? g_win_surf->format->Rmask : 0x000000ff;
                    Uint32 gm = g_win_surf ? g_win_surf->format->Gmask : 0x0000ff00;
                    Uint32 bm = g_win_surf ? g_win_surf->format->Bmask : 0x00ff0000;
                    int   bpp = g_win_surf ? g_win_surf->format->BitsPerPixel : 32;
                    screen = SDL_CreateRGBSurface(SDL_SWSURFACE, nw, nh, bpp, rm, gm, bm, 0);
                    if (old_s) SDL_FreeSurface(old_s);
                }
                g_resize_pending_w = nw;
                g_resize_pending_h = nh;
                g_resize_idle = 0;
            }
#endif
        }

        views_render(&app);

#if defined(BUILD_FOR_11_0) || defined(BUILD_FOR_10_7)
        if (g_win_surf && screen) {
            if (g_resize_pending_w > 0) {
                g_resize_idle++;
                if (g_resize_idle >= 2) {
                    Uint32 keep = g_win_surf->flags & SDL_FULLSCREEN;
                    SDL_Surface* nw = SDL_SetVideoMode(g_resize_pending_w, g_resize_pending_h,
                        0, SDL_SWSURFACE | SDL_RESIZABLE | keep);
                    if (nw) g_win_surf = nw;
                    g_resize_pending_w = g_resize_pending_h = g_resize_idle = 0;
                }
            }
            SDL_BlitSurface(screen, NULL, g_win_surf, NULL);
            SDL_Flip(g_win_surf);
        }
#else
        SDL_Flip(screen);
#endif
        SDL_Delay(16);
    }

#ifdef USE_SDL_TTF
    if (g_ttf_normal) { TTF_CloseFont(g_ttf_normal); g_ttf_normal = NULL; }
    if (g_ttf_large)  { TTF_CloseFont(g_ttf_large);  g_ttf_large  = NULL; }
    TTF_Quit();
#endif
    return 0;
}

#if defined(BUILD_FOR_10_4) || defined(BUILD_FOR_10_7) || defined(BUILD_FOR_11_0)
#ifdef USE_SDL_MAIN
int SDL_main(int argc, char* argv[]) { return sdl_app(argc, argv); }
#else
#undef main
int main(int argc, char* argv[]) { return sdl_app(argc, argv); }
#endif
#elif defined(__MACOS__)
/* Classic Mac OS (m68k/ppc): libSDL.a provides main(), we provide SDL_main() */
int SDL_main(int argc, char* argv[]) { return sdl_app(argc, argv); }
#else
#undef main
int main(int argc, char* argv[]) { return sdl_app(argc, argv); }
#endif
