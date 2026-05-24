#include "gfx.h"

#include <SDL3_ttf/SDL_ttf.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_THREAD_LOCALS
#include "stb_image.h"

static SDL_Renderer* R = NULL;
static char g_font_path[1024];
static float g_dpi = 1.0f;

void gfx_set_dpi(float scale) { g_dpi = (scale > 0.5f ? scale : 1.0f); }

/* ---- font-by-size cache ------------------------------------------------ */
#define FONT_SLOTS 12
typedef struct { int size; TTF_Font* font; } FontSlot;
static FontSlot g_fonts[FONT_SLOTS];

static TTF_Font* font_for(int size)
{
    int i, free_i = -1;
    for (i = 0; i < FONT_SLOTS; i++) {
        if (g_fonts[i].font && g_fonts[i].size == size) return g_fonts[i].font;
        if (!g_fonts[i].font && free_i < 0) free_i = i;
    }
    /* Open at physical pt size so the font is crisp at the physical pixel resolution. */
    float phys_pt = (float)size * g_dpi;
    TTF_Font* f = TTF_OpenFont(g_font_path, phys_pt);
    if (!f) return NULL;
    TTF_SetFontHinting(f, TTF_HINTING_LIGHT_SUBPIXEL);
    if (free_i < 0) { TTF_CloseFont(g_fonts[0].font); free_i = 0; }
    g_fonts[free_i].size = size;
    g_fonts[free_i].font = f;
    return f;
}

/* ---- rendered-text texture cache (direct mapped) ----------------------- */
#define TXT_CAP 2048
typedef struct { char* key; SDL_Texture* tex; int w, h; } TxtSlot;
static TxtSlot g_txt[TXT_CAP];

static unsigned long fnv(const char* s)
{
    unsigned long h = 1469598103934665603UL;
    while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211UL; }
    return h;
}

static TxtSlot* txt_lookup(int size, Color c, const char* s)
{
    char key[2304];
    SDL_snprintf(key, sizeof(key), "%d|%02x%02x%02x%02x|%s",
                 size, c.r, c.g, c.b, c.a, s);
    unsigned long idx = fnv(key) % TXT_CAP;
    TxtSlot* slot = &g_txt[idx];
    if (slot->key && strcmp(slot->key, key) == 0) return slot;

    /* miss: render + replace */
    TTF_Font* f = font_for(size);
    if (!f) return NULL;
    SDL_Color col = { c.r, c.g, c.b, c.a };
    SDL_Surface* surf = TTF_RenderText_Blended(f, s, strlen(s), col);
    if (!surf) return NULL;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(R, surf);
    int w = surf->w, h = surf->h;
    SDL_DestroySurface(surf);
    if (!tex) return NULL;

    if (slot->key)  { SDL_free(slot->key); SDL_DestroyTexture(slot->tex); }
    slot->key = SDL_strdup(key);
    slot->tex = tex;
    slot->w = w;
    slot->h = h;
    return slot;
}

bool gfx_init(SDL_Renderer* r, const char* font_path)
{
    R = r;
    SDL_strlcpy(g_font_path, font_path, sizeof(g_font_path));
    if (!TTF_Init()) return false;
    return font_for(15) != NULL;
}

void gfx_shutdown(void)
{
    int i;
    for (i = 0; i < TXT_CAP; i++)
        if (g_txt[i].key) { SDL_free(g_txt[i].key); SDL_DestroyTexture(g_txt[i].tex); }
    for (i = 0; i < FONT_SLOTS; i++)
        if (g_fonts[i].font) TTF_CloseFont(g_fonts[i].font);
    TTF_Quit();
}

/* ---- primitives -------------------------------------------------------- */
void gfx_clear(Color c)
{
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a);
    SDL_RenderClear(R);
}

void gfx_fill_rect(float x, float y, float w, float h, Color c)
{
    SDL_FRect rc = { x, y, w, h };
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a);
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_RenderFillRect(R, &rc);
}

static void fill_disc(float cx, float cy, float r, Color c)
{
    if (r < 1) return;
    int ri = (int)ceilf(r);
    SDL_FRect rows[256];
    int n = 0, dy;
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a);
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    for (dy = -ri; dy <= ri && n < 256; dy++) {
        float dx = sqrtf(r * r - (float)dy * (float)dy);
        rows[n].x = cx - dx;
        rows[n].y = cy + (float)dy;
        rows[n].w = dx * 2.0f;
        rows[n].h = 1.0f;
        n++;
    }
    SDL_RenderFillRects(R, rows, n);
}

void gfx_fill_round(float x, float y, float w, float h, float r, Color c)
{
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 1) { gfx_fill_rect(x, y, w, h, c); return; }
    gfx_fill_rect(x + r, y, w - 2 * r, h, c);       /* vertical band   */
    gfx_fill_rect(x, y + r, r, h - 2 * r, c);        /* left band       */
    gfx_fill_rect(x + w - r, y + r, r, h - 2 * r, c);/* right band      */
    fill_disc(x + r,     y + r,     r, c);
    fill_disc(x + w - r, y + r,     r, c);
    fill_disc(x + r,     y + h - r, r, c);
    fill_disc(x + w - r, y + h - r, r, c);
}

void gfx_stroke_round(float x, float y, float w, float h, float r, float t, Color c)
{
    (void)r;
    gfx_fill_rect(x, y, w, t, c);
    gfx_fill_rect(x, y + h - t, w, t, c);
    gfx_fill_rect(x, y, t, h, c);
    gfx_fill_rect(x + w - t, y, t, h, c);
}

void gfx_card(float x, float y, float w, float h, Color fill, Color border, bool shadow)
{
    float rad = 8.0f;
    if (shadow) gfx_fill_round(x + 1, y + 3, w, h, rad, COL_SHADOW);
    gfx_fill_round(x, y, w, h, rad, border);
    gfx_fill_round(x + 1.5f, y + 1.5f, w - 3.0f, h - 3.0f, rad - 1.5f, fill);
}

void gfx_push_clip(int x, int y, int w, int h)
{
    SDL_Rect rc = { x, y, w, h };
    SDL_SetRenderClipRect(R, &rc);
}

void gfx_pop_clip(void) { SDL_SetRenderClipRect(R, NULL); }

/* ---- text -------------------------------------------------------------- */
int gfx_text(float x, float y, const char* s, int size, Color c)
{
    if (!s || !s[0]) return 0;
    TxtSlot* t = txt_lookup(size, c, s);
    if (!t) return 0;
    SDL_FRect dst = { x, y, (float)t->w, (float)t->h };
    SDL_RenderTexture(R, t->tex, NULL, &dst);
    return t->w;
}

void gfx_text_size(const char* s, int size, int* w, int* h)
{
    int ow = 0, oh = gfx_text_h(size);
    if (s && s[0]) {
        TTF_Font* f = font_for(size);
        if (f) TTF_GetStringSize(f, s, strlen(s), &ow, &oh);
    }
    if (w) *w = ow;
    if (h) *h = oh;
}

int gfx_text_h(int size)
{
    TTF_Font* f = font_for(size);
    return f ? TTF_GetFontHeight(f) : size;
}

void gfx_text_clip(float x, float y, float maxw, const char* s, int size, Color c)
{
    int w, h;
    if (!s || !s[0]) return;
    gfx_text_size(s, size, &w, &h);
    if (w <= (int)maxw) { gfx_text(x, y, s, size, c); return; }

    /* truncate with ellipsis */
    char buf[1024];
    int len = (int)strlen(s);
    int lo = 0, hi = len, best = 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (mid > (int)sizeof(buf) - 4) { hi = mid - 1; continue; }
        memcpy(buf, s, mid);
        buf[mid] = '.'; buf[mid+1] = '.'; buf[mid+2] = '.'; buf[mid+3] = '\0';
        int bw; gfx_text_size(buf, size, &bw, &h);
        if (bw <= (int)maxw) { best = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    memcpy(buf, s, best);
    buf[best] = '.'; buf[best+1] = '.'; buf[best+2] = '.'; buf[best+3] = '\0';
    gfx_text(x, y, buf, size, c);
}

int gfx_prefix_w(const char* s, int n, int size)
{
    if (!s || n <= 0) return 0;
    int len = (int)strlen(s);
    if (n > len) n = len;
    TTF_Font* f = font_for(size);
    if (!f) return 0;
    int w = 0, h = 0;
    TTF_GetStringSize(f, s, (size_t)n, &w, &h);
    return w;
}

static int utf8_next(const char* s, int i)
{
    int len = (int)strlen(s);
    if (i >= len) return len;
    i++;
    while (i < len && ((unsigned char)s[i] & 0xC0) == 0x80) i++;
    return i;
}

int gfx_index_at_x(const char* s, int size, float px)
{
    if (!s || !s[0] || px <= 0) return 0;
    int len = (int)strlen(s);
    int i = 0, prev = 0, prevw = 0;
    while (i <= len) {
        int w = gfx_prefix_w(s, i, size);
        if (w >= px) {
            return (px - prevw < w - px) ? prev : i;
        }
        prev = i; prevw = w;
        if (i == len) break;
        i = utf8_next(s, i);
    }
    return len;
}

void gfx_blit(SDL_Texture* t, float x, float y, float w, float h)
{
    if (!t) return;
    SDL_FRect dst = { x, y, w, h };
    SDL_RenderTexture(R, t, NULL, &dst);
}

/* ---- images ------------------------------------------------------------ */
SDL_Texture* gfx_texture_from_rgb(const unsigned char* rgb, int w, int h, int channels)
{
    SDL_PixelFormat fmt = (channels == 4) ? SDL_PIXELFORMAT_RGBA32 : SDL_PIXELFORMAT_RGB24;
    SDL_Surface* surf = SDL_CreateSurfaceFrom(w, h, fmt, (void*)rgb, w * channels);
    if (!surf) return NULL;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(R, surf);
    SDL_DestroySurface(surf);
    if (tex) SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    return tex;
}

#define IMG_CAP 48
typedef struct { char path[1024]; SDL_Texture* tex; int w, h; unsigned long stamp; } ImgSlot;
static ImgSlot g_img[IMG_CAP];
static unsigned long g_img_stamp = 1;

SDL_Texture* gfx_image(const char* path, int* w, int* h)
{
    int i, lru = 0;
    unsigned long oldest = (unsigned long)-1;
    for (i = 0; i < IMG_CAP; i++) {
        if (g_img[i].tex && strcmp(g_img[i].path, path) == 0) {
            g_img[i].stamp = ++g_img_stamp;
            if (w) *w = g_img[i].w;
            if (h) *h = g_img[i].h;
            return g_img[i].tex;
        }
        if (g_img[i].stamp < oldest) { oldest = g_img[i].stamp; lru = i; }
    }
    int iw, ih, n;
    unsigned char* px = stbi_load(path, &iw, &ih, &n, 4);
    if (!px) return NULL;
    SDL_Texture* tex = gfx_texture_from_rgb(px, iw, ih, 4);
    stbi_image_free(px);
    if (!tex) return NULL;

    if (g_img[lru].tex) SDL_DestroyTexture(g_img[lru].tex);
    SDL_strlcpy(g_img[lru].path, path, sizeof(g_img[lru].path));
    g_img[lru].tex = tex;
    g_img[lru].w = iw;
    g_img[lru].h = ih;
    g_img[lru].stamp = ++g_img_stamp;
    if (w) *w = iw;
    if (h) *h = ih;
    return tex;
}

void gfx_image_cache_drop(const char* path)
{
    int i;
    for (i = 0; i < IMG_CAP; i++)
        if (g_img[i].tex && strcmp(g_img[i].path, path) == 0) {
            SDL_DestroyTexture(g_img[i].tex);
            g_img[i].tex = NULL;
            g_img[i].path[0] = '\0';
            g_img[i].stamp = 0;
        }
}
