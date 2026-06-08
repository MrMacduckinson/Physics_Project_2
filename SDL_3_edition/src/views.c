#include "views.h"
#include "gfx.h"
#include "util.h"
#include "store.h"
#include "pdfdoc.h"
#include "textedit.h"

#include <SDL3/SDL.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ---- DPI scale --------------------------------------------------------- */
static float g_dpi = 1.0f;
void views_set_dpi(float d) { g_dpi = (d > 0.5f ? d : 1.0f); }

#define DP(x)  ((float)(x) * g_dpi)

#define PX_TOPBAR  68
#define PX_STATUS  28
#define PX_PAD     20

/* ---- small geometry helpers ------------------------------------------- */
typedef struct { float x, y, w, h; } R;
static bool in_r(R r, float x, float y)
{ return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

static void draw_pills(R r, const char* text, bool focused, const char* placeholder);
static void draw_thumb(R area, const char* filename);

static const HitTarget g_ctx_hits[6] = {
    HIT_CTX_SELECT_ALL, HIT_CTX_COPY, HIT_CTX_CUT, HIT_CTX_PASTE,
    HIT_CTX_ADD_TAG, HIT_CTX_ADD_META
};
static const char* g_ctx_labels[6] = {
    "Select All", "Copy", "Cut", "Paste", "Add as Tag", "Add as Meta"
};

typedef struct {
    int w, h;
    /* list view */
    R search, search_content, add;
    float gx, gy, cell_w, cell_h, gut;
    int   cols;
    /* detail view — new 3-panel layout */
    R d_back, d_add;                        /* nav row (below topbar) */
    R d_prev, d_curr, d_next;               /* three preview panels */
    R d_tags;                               /* tag strip below panels */
    R d_edit, d_del, d_compile;             /* action buttons */
    R d_pg_prev, d_pg_next;                 /* PDF page nav inside d_curr */
    /* add/edit form */
    R tags, path, browse, save, cancel;
    R cover, cover_browse, more_options;
    R preview_add;
    /* context menu (up to 6 items) */
    int ctx_count;
    R ctx[6];
} Layout;

static bool caret_on(void) { return (SDL_GetTicks() / 530) % 2 == 0; }

/* ---- filtered list ----------------------------------------------------- */
int views_filtered(AppState* app, int* out, int cap)
{
    int i, n = 0;
    for (i = 0; i < app->list.count && n < cap; i++)
        if (item_matches(&app->list.items[i], i, app->search, app->search_content))
            out[n++] = i;
    return n;
}

/* ---- layout ------------------------------------------------------------ */
static R item_rect(const Layout* L, int slot, float scroll)
{
    int row = slot / L->cols, col = slot % L->cols;
    R r;
    r.x = L->gx + col * (L->cell_w + L->gut);
    r.y = L->gy + row * (L->cell_h + L->gut) - scroll;
    r.w = L->cell_w; r.h = L->cell_h;
    return r;
}

static void compute_layout(AppState* app, int w, int h, Layout* L)
{
    memset(L, 0, sizeof(*L));
    L->w = w; L->h = h;

    /* ---- list view ---------------------------------------------------- */
    float bar_h = DP(40);
    float bar_y = (DP(PX_TOPBAR) - bar_h) * 0.5f + DP(8);
    float sw = (float)w * 0.27f;
    if (sw > 340) sw = 340;
    if (sw < 130) sw = 130;
    L->search_content = (R){ (float)w - sw - DP(PX_PAD),           bar_y, sw, bar_h };
    L->search         = (R){ (float)w - sw*2 - DP(PX_PAD) - DP(8), bar_y, sw, bar_h };
    L->add = (R){ (float)w - DP(148) - DP(PX_PAD), (float)h - DP(PX_STATUS) - DP(58), DP(148), DP(46) };

    float margin = DP(26), cellmin = DP(210);
    L->gut = DP(20);
    int cols = (int)((w - 2*margin + L->gut) / (cellmin + L->gut));
    if (cols < 1) cols = 1;
    L->cols   = cols;
    L->cell_w = (w - 2*margin - (cols-1)*L->gut) / cols;
    L->cell_h = L->cell_w + DP(42);
    L->gx = margin;
    L->gy = DP(PX_TOPBAR) + DP(18);

    /* ---- detail view — 3-panel layout --------------------------------- */
    float nav_y  = DP(PX_TOPBAR) + DP(6);
    float nav_h  = DP(44);
    float btn_h  = DP(40);
    float tags_h = DP(44);
    float gap    = DP(8);

    L->d_back = (R){ DP(PX_PAD), nav_y + (nav_h - btn_h)*0.5f, DP(80), btn_h };
    L->d_add  = (R){ (float)w - DP(PX_PAD) - DP(100), nav_y + (nav_h - btn_h)*0.5f, DP(100), btn_h };

    float panels_y = nav_y + nav_h + gap;
    /* pin action row from the bottom, derive panel height upward */
    float acts_y   = (float)h - DP(PX_STATUS) - DP(16) - btn_h;
    float tags_y   = acts_y - DP(14) - tags_h;
    float panels_h = tags_y - DP(10) - panels_y;
    if (panels_h < DP(80)) panels_h = DP(80);

    float side_frac = 0.17f;
    float side_w    = (float)w * side_frac;
    float center_w  = (float)w - 2.0f*side_w - 2.0f*gap;
    /* side panels are ~50% height, centered vertically in their column */
    float side_h    = panels_h * 0.50f;
    float side_off  = (panels_h - side_h) * 0.5f;

    L->d_prev = (R){ 0,                        panels_y + side_off, side_w,   side_h };
    L->d_curr = (R){ side_w + gap,             panels_y,           center_w, panels_h };
    L->d_next = (R){ side_w+gap+center_w+gap,  panels_y + side_off, side_w,   side_h };

    L->d_tags = (R){ DP(PX_PAD), tags_y, (float)w - 2*DP(PX_PAD), tags_h };

    L->d_edit    = (R){ (float)w*0.5f - DP(128), acts_y, DP(116), btn_h };
    L->d_del     = (R){ (float)w*0.5f + DP(12),  acts_y, DP(116), btn_h };
    L->d_compile = (R){ (float)w*0.5f + DP(140), acts_y, DP(140), btn_h };

    /* PDF page nav inside d_curr */
    L->d_pg_prev = (R){ L->d_curr.x+DP(8),  L->d_curr.y+L->d_curr.h-DP(50), DP(46), DP(40) };
    L->d_pg_next = (R){ L->d_curr.x+DP(60), L->d_curr.y+L->d_curr.h-DP(50), DP(46), DP(40) };

    /* ---- add/edit form ------------------------------------------------ */
    float fx = DP(PX_PAD)+DP(8), fw = (float)w - 2*(DP(PX_PAD)+DP(8));
    float top = DP(PX_TOPBAR)+DP(74), tags_fh = DP(164);
    bool wide = ((float)w >= DP(680));

    /* In wide mode: preview LEFT, form fields RIGHT.
       In narrow: form full-width, preview below. */
    float form_x, form_w;
    if (wide) {
        float prev_w = fw * 0.42f;
        form_x = fx + prev_w + DP(18);
        form_w = fw - prev_w - DP(18);
        L->preview_add = (R){ fx, top, prev_w, (float)h - top - DP(PX_STATUS) - DP(8) };
    } else {
        form_x = fx;
        form_w = fw;
    }

    L->tags   = (R){ form_x, top, form_w, tags_fh };
    L->path   = (R){ form_x, top+tags_fh+DP(58), form_w-DP(126), DP(44) };
    L->browse = (R){ form_x+form_w-DP(118), top+tags_fh+DP(58), DP(118), DP(44) };

    /* Cover field — shown for all files; hidden for images when !show_cover_options */
    float cover_y = L->path.y + L->path.h + DP(52);
    L->cover        = (R){ form_x, cover_y, form_w-DP(126), DP(44) };
    L->cover_browse = (R){ form_x+form_w-DP(118), cover_y, DP(118), DP(44) };
    /* "More options" toggle for images */
    L->more_options = (R){ form_x, L->path.y + L->path.h + DP(8), DP(150), DP(36) };

    float save_y = cover_y + DP(44) + DP(16);
    L->save   = (R){ form_x, save_y, DP(118), DP(48) };
    L->cancel = (R){ form_x+DP(118)+DP(12), save_y, DP(118), DP(48) };

    if (!wide) {
        float py = save_y + DP(60);
        float maxh = (float)h - DP(PX_STATUS) - DP(70) - py;
        if (maxh > DP(120))
            L->preview_add = (R){ fx, py, fw, maxh };
    }

    /* ---- context menu ------------------------------------------------- */
    if (app->ctx_open) {
        float mw = DP(168), mh = DP(36);
        int count = app->ctx_count > 0 ? app->ctx_count : 4;
        L->ctx_count = count;
        float mx = app->ctx_x, my = app->ctx_y;
        if (mx + mw > (float)w - DP(4)) mx = (float)w - mw - DP(4);
        if (my + count*mh + DP(8) > (float)h) my = (float)h - count*mh - DP(8);
        int i;
        for (i = 0; i < count; i++)
            L->ctx[i] = (R){ mx, my + i*mh, mw, mh };
    } else {
        L->ctx_count = 0;
    }
}

/* ---- reusable widgets -------------------------------------------------- */
static void draw_button(R r, const char* label, bool hot, bool active, bool primary)
{
    Color fill = primary ? COL_ACCENT : COL_CARD;
    if (hot)    fill = primary ? COL_ACCENT_Hi : COL_CARD_Hi;
    if (active) fill = primary ? COL_ACCENT    : COL_BORDER;
    Color border = primary ? COL_ACCENT : COL_BORDER;
    gfx_card(r.x, r.y, r.w, r.h, fill, border, false);
    Color tc = primary ? (Color){39,40,34,255} : COL_TEXT;
    int tw, th; gfx_text_size(label, 16, &tw, &th);
    gfx_text(r.x + (r.w-tw)*0.5f, r.y + (r.h-th)*0.5f, label, 16, tc);
}

static void draw_text_field(R r, const char* buf, EditState* ed, bool focused,
                            const char* placeholder, int fs)
{
    gfx_card(r.x, r.y, r.w, r.h, COL_PANEL, focused ? COL_BORDER_Hi : COL_BORDER, false);
    float pad = DP(10), inner_x = r.x+pad, inner_w = r.w-2*pad;
    float ty = r.y + (r.h - gfx_text_h(fs)) * 0.5f;

    if (!buf[0] && !focused) { gfx_text(inner_x, ty, placeholder, fs, COL_TEXT_DIM); return; }

    int caret_px = gfx_prefix_w(buf, ed->cursor, fs);
    if (caret_px - ed->scroll > (int)inner_w) ed->scroll = caret_px - (int)inner_w;
    if (caret_px - ed->scroll < 0)            ed->scroll = caret_px;
    if (ed->scroll < 0) ed->scroll = 0;

    gfx_push_clip((int)inner_x, (int)r.y, (int)inner_w, (int)r.h);
    float draw_x = inner_x - ed->scroll;

    if (focused && te_has_sel(ed)) {
        int lo, hi; te_sel_range(ed, &lo, &hi);
        int xlo = gfx_prefix_w(buf, lo, fs), xhi = gfx_prefix_w(buf, hi, fs);
        gfx_fill_rect(draw_x+xlo, r.y+DP(5), (float)(xhi-xlo), r.h-DP(10), COL_SELECT);
    }
    gfx_text(draw_x, ty, buf, fs, COL_TEXT);
    if (focused && caret_on())
        gfx_fill_rect(draw_x+caret_px, ty+DP(1), DP(2), (float)gfx_text_h(fs)-DP(2), COL_ACCENT_Hi);
    gfx_pop_clip();
}

static void draw_tag_search_field(R r, const char* buf, EditState* ed, bool focused)
{
    (void)ed;
    draw_pills(r, buf, focused, "filter by tag…");
}

static void pick_tag_colors(const char* tok, Color* border, Color* fg)
{
    TagKind kind = store_tag_kind(tok);
    if (kind == TAG_KIND_META) { *border = COL_META; *fg = COL_META; return; }
    if (kind == TAG_KIND_TAG)  { *border = COL_TAG;  *fg = COL_TAG;  return; }
    *border = COL_TAG_BAD; *fg = COL_TAG_BAD;
}

static bool collect_unknown_tags(const char* text, char* out, size_t cap)
{
    out[0] = '\0';
    const char* p = text;
    while (*p) {
        while (*p == ' ') p++;
        const char* start = p;
        while (*p && *p != ' ') p++;
        if (p > start) {
            char tok[128];
            size_t len = (size_t)(p - start);
            if (len >= sizeof(tok)) len = sizeof(tok) - 1;
            memcpy(tok, start, len); tok[len] = '\0';
            if (store_tag_kind(tok) == TAG_KIND_UNKNOWN) {
                if (out[0]) SDL_strlcat(out, " ", cap);
                SDL_strlcat(out, tok, cap);
            }
        }
    }
    return out[0] != '\0';
}

static bool last_prefix(const char* text, char* out, size_t cap)
{
    size_t n = strlen(text);
    if (n == 0) return false;
    if (text[n-1] == ' ') return false;
    size_t start = n;
    while (start > 0 && text[start-1] != ' ') start--;
    size_t len = n - start;
    if (len >= cap) len = cap - 1;
    memcpy(out, text + start, len);
    out[len] = '\0';
    return out[0] != '\0';
}

static bool prefix_match(const char* tok, const char* prefix)
{
    size_t n = strlen(prefix);
    return n > 0 && SDL_strncasecmp(tok, prefix, n) == 0;
}

static void add_suggestion(TagSuggestion* out, int* n, int cap, const char* tok, TagKind kind)
{
    int i;
    for (i = 0; i < *n; i++) {
        if (SDL_strcasecmp(out[i].tok, tok) == 0) {
            out[i].count++;
            if (out[i].kind == TAG_KIND_UNKNOWN && kind != TAG_KIND_UNKNOWN)
                out[i].kind = kind;
            return;
        }
    }
    if (*n >= cap) return;
    SDL_strlcpy(out[*n].tok, tok, sizeof(out[*n].tok));
    out[*n].count = 1;
    out[*n].kind = kind;
    (*n)++;
}

static int tag_suggestions(const AppState* app, const char* prefix, TagSuggestion* out, int cap)
{
    int n = 0, i;
    for (i = 0; i < app->list.count; i++) {
        const Item* it = &app->list.items[i];
        const char* p = it->tags;
        while (*p) {
            while (*p == ' ') p++;
            const char* start = p;
            while (*p && *p != ' ') p++;
            if (p > start) {
                char tok[128];
                size_t len = (size_t)(p - start);
                if (len >= sizeof(tok)) len = sizeof(tok) - 1;
                memcpy(tok, start, len); tok[len] = '\0';
                if (prefix_match(tok, prefix))
                    add_suggestion(out, &n, cap, tok, store_tag_kind(tok));
            }
        }
        p = it->meta;
        while (*p) {
            while (*p == ' ') p++;
            const char* start = p;
            while (*p && *p != ' ') p++;
            if (p > start) {
                char tok[128];
                size_t len = (size_t)(p - start);
                if (len >= sizeof(tok)) len = sizeof(tok) - 1;
                memcpy(tok, start, len); tok[len] = '\0';
                if (prefix_match(tok, prefix))
                    add_suggestion(out, &n, cap, tok, store_tag_kind(tok));
            }
        }
    }
    int a, b;
    for (a = 0; a < n; a++)
        for (b = a + 1; b < n; b++)
            if (out[b].count > out[a].count ||
                (out[b].count == out[a].count && SDL_strcasecmp(out[b].tok, out[a].tok) < 0)) {
                TagSuggestion tmp = out[a]; out[a] = out[b]; out[b] = tmp;
            }
    return n;
}

static Color color_for_kind(TagKind kind)
{
    if (kind == TAG_KIND_META) return COL_META;
    if (kind == TAG_KIND_TAG)  return COL_TAG;
    return COL_TAG_BAD;
}

static void draw_tag_dropdown(R anchor, const TagSuggestion* items, int count, int active)
{
    if (count <= 0) return;
    int max_show = count > 6 ? 6 : count;
    float row_h = DP(24), pad = DP(8);
    float w = anchor.w, h = row_h * max_show + pad * 2;
    float x = anchor.x, y = anchor.y + anchor.h + DP(6);
    gfx_fill_rect(x, y, w, h, COL_CARD);
    gfx_stroke_round(x, y, w, h, 0, DP(1), COL_BORDER);
    int i;
    for (i = 0; i < max_show; i++) {
        if (i == active)
            gfx_fill_rect(x + DP(2), y + pad + i * row_h - DP(2), w - DP(4), row_h, COL_CARD_Hi);
        char line[160];
        SDL_snprintf(line, sizeof(line), "%s (%d)", items[i].tok, items[i].count);
        gfx_text_clip(x + pad, y + pad + i * row_h, w - 2*pad, line, 14, color_for_kind(items[i].kind));
    }
}

static void draw_pills(R r, const char* text, bool focused, const char* placeholder)
{
    gfx_card(r.x, r.y, r.w, r.h, COL_PANEL, focused ? COL_BORDER_Hi : COL_BORDER, false);
    float pad = DP(12), gap = DP(8), ph = DP(30);
    float x = r.x+pad, y = r.y+pad;
    int fs = 16;

    if (!text[0] && !focused) {
        gfx_text(x, r.y+(r.h-gfx_text_h(fs))*0.5f, placeholder, fs, COL_TEXT_DIM);
        return;
    }

    gfx_push_clip((int)r.x, (int)r.y, (int)r.w, (int)r.h);
    const char* p = text;
    while (*p) {
        const char* sp = strchr(p, ' ');
        int len = sp ? (int)(sp-p) : (int)strlen(p);
        if (len > 0) {
            char tok[256]; if (len > 255) len = 255;
            memcpy(tok, p, len); tok[len] = '\0';
            int tw, th; gfx_text_size(tok, fs, &tw, &th);
            float pw = tw + DP(18);
            if (x+pw > r.x+r.w-pad) { x = r.x+pad; y += ph+gap; }
            if (sp) {
                Color br, fg; pick_tag_colors(tok, &br, &fg);
                gfx_text(x, y+(ph-th)*0.5f, tok, fs, fg);
                x += tw + DP(16);
            } else {
                gfx_text(x, y+(ph-th)*0.5f, tok, fs, COL_TEXT_DIM);
                if (focused && caret_on())
                    gfx_fill_rect(x+tw+DP(1), y+DP(3), DP(2), ph-DP(6), COL_ACCENT_Hi);
                x += tw;
            }
        }
        if (!sp) break;
        p = sp+1;
        if (!*p && focused && caret_on()) {
            if (x+DP(4) > r.x+r.w-pad) { x = r.x+pad; y += ph+gap; }
            gfx_fill_rect(x+DP(1), y+DP(3), DP(2), ph-DP(6), COL_ACCENT_Hi);
        }
    }
    gfx_pop_clip();
}

bool views_tag_token_at(AppState* app, int w, int h, float mx, float my,
                        char* out, size_t cap)
{
    Layout L; compute_layout(app, w, h, &L);
    if (!in_r(L.tags, mx, my)) return false;

    float pad = DP(12), ph = DP(30);
    float x = L.tags.x + pad, y = L.tags.y + pad;
    int fs = 16;

    const char* p = app->form_tags;
    while (*p) {
        const char* sp = strchr(p, ' ');
        int len = sp ? (int)(sp-p) : (int)strlen(p);
        if (len > 0) {
            char tok[128]; if (len > 127) len = 127;
            memcpy(tok, p, len); tok[len] = '\0';
            int tw, th; gfx_text_size(tok, fs, &tw, &th); (void)th;
            float pw = tw + DP(16);
            if (x + pw > L.tags.x + L.tags.w - pad) { x = L.tags.x + pad; y += ph + DP(8); }
            if (mx >= x && mx <= x + pw && my >= y && my <= y + ph) {
                SDL_strlcpy(out, tok, cap);
                return true;
            }
            if (!sp) break;
            x += pw;
        }
        if (!sp) break;
        p = sp + 1;
    }
    return false;
}

/* ---- PDF gallery thumbnail cache --------------------------------------- */
#define PDF_THUMB_CAP 64
typedef struct { char key[PATH_LEN+16]; SDL_Texture* tex; int w, h; } PdfThumb;
static PdfThumb g_pthumb[PDF_THUMB_CAP];
static int g_pthumb_evict = 0;

static SDL_Texture* pdf_gallery_thumb(const char* full, int box, int* tw, int* th)
{
    char key[PATH_LEN+16];
    SDL_snprintf(key, sizeof(key), "%s|%d", full, box/40);
    int i;
    for (i = 0; i < PDF_THUMB_CAP; i++) {
        if (g_pthumb[i].tex && strcmp(g_pthumb[i].key, key) == 0) {
            *tw = g_pthumb[i].w; *th = g_pthumb[i].h; return g_pthumb[i].tex;
        }
    }
    int rw, rh, pc;
    unsigned char* rgb = pdf_render_page(full, 0, box-8, box-8, &rw, &rh, &pc);
    if (!rgb) return NULL;
    SDL_Texture* tex = gfx_texture_from_rgb(rgb, rw, rh, 3); free(rgb);
    if (!tex) return NULL;
    int slot = g_pthumb_evict % PDF_THUMB_CAP; g_pthumb_evict++;
    if (g_pthumb[slot].tex) SDL_DestroyTexture(g_pthumb[slot].tex);
    SDL_strlcpy(g_pthumb[slot].key, key, sizeof(g_pthumb[slot].key));
    g_pthumb[slot].tex = tex; g_pthumb[slot].w = rw; g_pthumb[slot].h = rh;
    *tw = rw; *th = rh; return tex;
}

static void draw_thumb(R area, const char* filename)
{
    char full[PATH_LEN];
    store_full_path(filename, full, sizeof(full));

    if (is_image_path(filename)) {
        int iw, ih;
        SDL_Texture* t = gfx_image(full, &iw, &ih);
        if (t && iw > 0 && ih > 0) {
            float s = area.w/iw < area.h/ih ? area.w/iw : area.h/ih;
            float dw = iw*s, dh = ih*s;
            gfx_blit(t, area.x+(area.w-dw)*0.5f, area.y+(area.h-dh)*0.5f, dw, dh);
            return;
        }
    }
    if (is_pdf_path(filename)) {
        int tw, th;
        SDL_Texture* t = pdf_gallery_thumb(full, (int)area.w, &tw, &th);
        if (t && tw > 0 && th > 0) {
            float s = area.w/tw < area.h/th ? area.w/tw : area.h/th;
            float dw = tw*s, dh = th*s;
            gfx_blit(t, area.x+(area.w-dw)*0.5f, area.y+(area.h-dh)*0.5f, dw, dh);
            /* ext label bottom-left */
            char lbl[16]; get_ext_label(filename, lbl, sizeof(lbl));
            Color bg, fg; get_badge_colors(filename, &bg, &fg);
            int lw, lh; gfx_text_size(lbl, 11, &lw, &lh);
            float bpad = DP(3), bw2 = lw + DP(8), bh2 = (float)lh + DP(4);
            bg.a = 210;
            gfx_fill_rect(area.x+bpad, area.y+area.h-bh2-bpad, bw2, bh2, bg);
            gfx_text(area.x+bpad+DP(4), area.y+area.h-bh2-bpad+(bh2-lh)*0.5f, lbl, 11, fg);
            return;
        }
    }
    Color bg, fg; get_badge_colors(filename, &bg, &fg); (void)fg;
    char lbl[16]; get_ext_label(filename, lbl, sizeof(lbl));
    int ltw, lth; gfx_text_size(lbl, 28, &ltw, &lth);
    gfx_text(area.x+(area.w-ltw)*0.5f, area.y+(area.h-lth)*0.5f, lbl, 28, bg);
}

/* Draw item thumbnail: if item has a custom cover, show that + small badge */
static void draw_item_thumb(R area, const Item* item)
{
    if (item->cover[0]) {
        char full[PATH_LEN];
        store_full_path(item->cover, full, sizeof(full));
        int iw, ih;
        SDL_Texture* t = gfx_image(full, &iw, &ih);
        if (t && iw > 0 && ih > 0) {
            float s = area.w/iw < area.h/ih ? area.w/iw : area.h/ih;
            float dw = iw*s, dh = ih*s;
            gfx_blit(t, area.x+(area.w-dw)*0.5f, area.y+(area.h-dh)*0.5f, dw, dh);
            /* small extension label bottom-left for non-image files */
            if (!is_image_path(item->filename)) {
                char lbl[16]; get_ext_label(item->filename, lbl, sizeof(lbl));
                Color bg2, fg2; get_badge_colors(item->filename, &bg2, &fg2); (void)fg2;
                int tw, th; gfx_text_size(lbl, 11, &tw, &th);
                float bpad = DP(3);
                gfx_text(area.x+bpad+DP(4), area.y+area.h-(float)th-bpad, lbl, 11, bg2);
            }
            return;
        }
    }
    draw_thumb(area, item->filename);
}

/* ---- multi-line text editor ------------------------------------------- */
#define TXT_BUF_CAP 262144
#define TXT_LINES_CAP 8000
static char g_txt_buf[TXT_BUF_CAP];
static char g_txt_path[PATH_LEN];
static int  g_txt_off[TXT_LINES_CAP];   /* byte offset of line start */
static int  g_txt_len[TXT_LINES_CAP];   /* byte length of line (excl \r\n) */
static int  g_txt_lines;
static bool g_txt_modified;
static int  g_txted_vis_lines = 30; /* updated each render frame; used for keyboard auto-scroll */

static void txted_rebuild(void)
{
    g_txt_lines = 0;
    int n = (int)strlen(g_txt_buf);
    int start = 0, i;
    for (i = 0; i <= n && g_txt_lines < TXT_LINES_CAP - 1; i++) {
        if (i == n || g_txt_buf[i] == '\n') {
            int end = i;
            if (end > start && g_txt_buf[end-1] == '\r') end--;
            g_txt_off[g_txt_lines] = start;
            g_txt_len[g_txt_lines] = end - start;
            g_txt_lines++;
            start = i + 1;
        }
    }
}

void views_txted_load(AppState* app, const char* full_path)
{
    if (strcmp(g_txt_path, full_path) == 0) return;
    SDL_strlcpy(g_txt_path, full_path, sizeof(g_txt_path));
    g_txt_buf[0] = '\0'; g_txt_modified = false;
    FILE* f = fopen(full_path, "rb");
    if (!f) { txted_rebuild(); return; }
    size_t n = fread(g_txt_buf, 1, TXT_BUF_CAP - 1, f); fclose(f);
    g_txt_buf[n] = '\0';
    txted_rebuild();
    app->txt_cursor_line = 0;
    app->txt_cursor_col  = 0;
    app->txt_scroll      = 0;
}

bool views_txted_is_modified(void) { return g_txt_modified; }

bool views_txted_save(AppState* app)
{
    if (!g_txt_path[0] || !g_txt_modified) return true;
    (void)app;
    FILE* f = fopen(g_txt_path, "wb");
    if (!f) return false;
    size_t n = strlen(g_txt_buf);
    fwrite(g_txt_buf, 1, n, f);
    fclose(f);
    g_txt_modified = false;
    return true;
}

void views_txted_input(AppState* app, const char* text)
{
    if (!text || !text[0]) return;
    if (app->txt_cursor_line >= g_txt_lines) return;
    int byte = g_txt_off[app->txt_cursor_line] + app->txt_cursor_col;
    size_t n = strlen(g_txt_buf);
    size_t tlen = strlen(text);
    if (n + tlen >= TXT_BUF_CAP - 1) return;
    memmove(g_txt_buf + byte + tlen, g_txt_buf + byte, n - byte + 1);
    memcpy(g_txt_buf + byte, text, tlen);
    g_txt_modified = true;
    txted_rebuild();
    /* advance cursor past inserted text */
    int new_byte = byte + (int)tlen;
    /* find which line this byte lands on */
    int li;
    for (li = 0; li < g_txt_lines; li++)
        if (li + 1 >= g_txt_lines || g_txt_off[li+1] > new_byte) break;
    app->txt_cursor_line = li;
    app->txt_cursor_col  = new_byte - g_txt_off[li];
}

static void txted_autoscroll(AppState* app)
{
    if (app->txt_cursor_line < app->txt_scroll)
        app->txt_scroll = app->txt_cursor_line;
    if (g_txted_vis_lines > 0 &&
        app->txt_cursor_line >= app->txt_scroll + g_txted_vis_lines)
        app->txt_scroll = app->txt_cursor_line - g_txted_vis_lines + 1;
    if (app->txt_scroll < 0) app->txt_scroll = 0;
}

bool views_txted_key(AppState* app, SDL_Keycode key, SDL_Keymod mod)
{
    if (app->txt_cursor_line >= g_txt_lines) app->txt_cursor_line = g_txt_lines > 0 ? g_txt_lines-1 : 0;
    int line = app->txt_cursor_line;
    int col  = app->txt_cursor_col;
    int llen = (line < g_txt_lines) ? g_txt_len[line] : 0;
    if (col > llen) col = llen;

    (void)mod;
    if (key == SDLK_LEFT) {
        if (col > 0) col--; else if (line > 0) { line--; col = g_txt_len[line]; }
    } else if (key == SDLK_RIGHT) {
        if (col < g_txt_len[line]) col++;
        else if (line + 1 < g_txt_lines) { line++; col = 0; }
    } else if (key == SDLK_UP) {
        if (line > 0) { line--; if (col > g_txt_len[line]) col = g_txt_len[line]; }
    } else if (key == SDLK_DOWN) {
        if (line + 1 < g_txt_lines) { line++; if (col > g_txt_len[line]) col = g_txt_len[line]; }
    } else if (key == SDLK_HOME) {
        col = 0;
    } else if (key == SDLK_END) {
        col = g_txt_len[line];
    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        views_txted_input(app, "\n");
        txted_autoscroll(app);
        return true;
    } else if (key == SDLK_BACKSPACE) {
        int byte = g_txt_off[line] + col;
        if (byte > 0) {
            int del = 1;
            /* skip back over multi-byte UTF-8 continuation bytes */
            while (del < byte && ((unsigned char)g_txt_buf[byte-del] & 0xC0) == 0x80) del++;
            size_t n = strlen(g_txt_buf);
            memmove(g_txt_buf + byte - del, g_txt_buf + byte, n - byte + 1);
            g_txt_modified = true;
            txted_rebuild();
            int new_byte = byte - del;
            int li;
            for (li = 0; li < g_txt_lines; li++)
                if (li + 1 >= g_txt_lines || g_txt_off[li+1] > new_byte) break;
            app->txt_cursor_line = li;
            app->txt_cursor_col  = new_byte - g_txt_off[li];
            txted_autoscroll(app);
            return true;
        }
    } else if (key == SDLK_DELETE) {
        int byte = g_txt_off[line] + col;
        size_t n = strlen(g_txt_buf);
        if (byte < (int)n) {
            int del = 1;
            while (byte + del < (int)n && ((unsigned char)g_txt_buf[byte+del] & 0xC0) == 0x80) del++;
            memmove(g_txt_buf + byte, g_txt_buf + byte + del, n - byte - del + 1);
            g_txt_modified = true;
            txted_rebuild();
            if (line >= g_txt_lines) line = g_txt_lines - 1;
            if (col > g_txt_len[line]) col = g_txt_len[line];
            app->txt_cursor_line = line;
            app->txt_cursor_col  = col;
            txted_autoscroll(app);
            return true;
        }
    } else {
        return false; /* not handled */
    }
    app->txt_cursor_line = line;
    app->txt_cursor_col  = col;
    txted_autoscroll(app);
    return true;
}

bool views_txted_click(AppState* app, int w, int h, float mx, float my)
{
    Layout L; compute_layout(app, w, h, &L);
    R pv = L.d_curr;
    if (!in_r(pv, mx, my)) return false;
    int fs = 15, lh = gfx_text_h(fs) + (int)DP(3);
    float ty = pv.y + DP(12);
    float gutter_w = DP(48);
    float text_x = pv.x + gutter_w;
    int clicked_line = (int)((my - ty) / lh) + app->txt_scroll;
    if (clicked_line < 0) clicked_line = 0;
    if (clicked_line >= g_txt_lines) clicked_line = g_txt_lines > 0 ? g_txt_lines-1 : 0;
    app->txt_cursor_line = clicked_line;
    /* find column from x */
    int llen = g_txt_len[clicked_line];
    char linebuf[2048];
    if (llen > 2047) llen = 2047;
    memcpy(linebuf, g_txt_buf + g_txt_off[clicked_line], llen);
    linebuf[llen] = '\0';
    app->txt_cursor_col = gfx_index_at_x(linebuf, fs, mx - text_x);
    if (app->txt_cursor_col > g_txt_len[app->txt_cursor_line])
        app->txt_cursor_col = g_txt_len[app->txt_cursor_line];
    return true;
}

/* ---- detail preview caches -------------------------------------------- */
static SDL_Texture* g_pdf_tex; static char g_pdf_key[1400]; static int g_pdf_tw, g_pdf_th;

/* Draw current-file preview into pv rect.
 * is_editor: true → render text as editable (cursor, active bg). */
static void draw_preview_at(AppState* app, R pv, const char* full, const char* filename,
                             bool is_editor)
{
    gfx_fill_rect(pv.x, pv.y, pv.w, pv.h, COL_PANEL);

    if (is_image_path(filename)) {
        int iw, ih;
        SDL_Texture* t = gfx_image(full, &iw, &ih);
        if (t && iw > 0 && ih > 0) {
            float m = DP(8);
            float s = (pv.w-m*2)/iw < (pv.h-m*2)/ih ? (pv.w-m*2)/iw : (pv.h-m*2)/ih;
            if (s > 1) s = 1;
            gfx_blit(t, pv.x+(pv.w-iw*s)*0.5f, pv.y+(pv.h-ih*s)*0.5f, iw*s, ih*s);
        } else {
            gfx_text(pv.x+DP(16), pv.y+DP(16), "(failed to load image)", 16, COL_TEXT_DIM);
        }
        app->pdf_page_count = 0;
    }
    else if (is_pdf_path(filename)) {
        int boxw = (int)pv.w-(int)DP(16), boxh = (int)pv.h-(int)DP(16);
        char key[1400];
        SDL_snprintf(key, sizeof(key), "%s|%d|%d|%d", full, app->pdf_page, boxw/24, boxh/24);
        if (strcmp(key, g_pdf_key) != 0) {
            SDL_strlcpy(g_pdf_key, key, sizeof(g_pdf_key));
            if (g_pdf_tex) { SDL_DestroyTexture(g_pdf_tex); g_pdf_tex = NULL; }
            int rw, rh, pc = 0;
            unsigned char* rgb = pdf_render_page(full, app->pdf_page, boxw, boxh, &rw, &rh, &pc);
            app->pdf_page_count = pc;
            if (rgb) { g_pdf_tex = gfx_texture_from_rgb(rgb, rw, rh, 3); g_pdf_tw=rw; g_pdf_th=rh; free(rgb); }
        }
        if (g_pdf_tex)
            gfx_blit(g_pdf_tex, pv.x+(pv.w-g_pdf_tw)*0.5f, pv.y+(pv.h-g_pdf_th)*0.5f,
                     (float)g_pdf_tw, (float)g_pdf_th);
        else
            gfx_text(pv.x+DP(16), pv.y+DP(16), "(failed to render PDF)", 16, COL_TEXT_DIM);

        if (app->pdf_page_count > 1) {
            draw_button((R){pv.x+DP(8),  pv.y+pv.h-DP(50), DP(46), DP(40)}, "<", false,false,false);
            draw_button((R){pv.x+DP(60), pv.y+pv.h-DP(50), DP(46), DP(40)}, ">", false,false,false);
            char pg[32];
            SDL_snprintf(pg, sizeof(pg), "Page %d / %d", app->pdf_page+1, app->pdf_page_count);
            gfx_text(pv.x+DP(114), pv.y+pv.h-DP(38), pg, 14, COL_TEXT_DIM);
        }
    }
    else if (is_text_path(filename)) {
        views_txted_load(app, full);
        app->pdf_page_count = 0;

        int fs = 15, lh = gfx_text_h(fs) + (int)DP(3);
        float ty = pv.y + DP(12);
        int maxlines = (int)((pv.h - DP(24)) / lh);
        int scroll = app->txt_scroll;
        if (scroll < 0) scroll = 0;
        if (scroll >= g_txt_lines) scroll = g_txt_lines > 0 ? g_txt_lines-1 : 0;
        app->txt_scroll = scroll;
        g_txted_vis_lines = maxlines; /* used by keyboard auto-scroll */

        /* clamp cursor */
        if (app->txt_cursor_line >= g_txt_lines && g_txt_lines > 0)
            app->txt_cursor_line = g_txt_lines - 1;
        if (app->txt_cursor_col > g_txt_len[app->txt_cursor_line])
            app->txt_cursor_col = g_txt_len[app->txt_cursor_line];

        gfx_push_clip((int)pv.x, (int)pv.y, (int)pv.w, (int)pv.h);

        /* gutter */
        float gutter_w = DP(48);
        float text_x   = pv.x + gutter_w;
        float text_w   = pv.w - gutter_w - DP(12);
        gfx_fill_rect(pv.x, pv.y, gutter_w, pv.h, (Color){33,34,29,255});
        gfx_fill_rect(text_x - DP(1), pv.y, DP(1), pv.h, (Color){60,62,55,255});

        char linebuf[2048];
        int i;
        for (i = 0; i < maxlines && scroll+i < g_txt_lines; i++) {
            int lo = g_txt_off[scroll+i], ll = g_txt_len[scroll+i];
            if (ll > 2047) ll = 2047;
            memcpy(linebuf, g_txt_buf + lo, ll); linebuf[ll] = '\0';
            float ly = ty + i*lh;
            /* line number right-aligned in gutter */
            char lnum[12]; SDL_snprintf(lnum, sizeof(lnum), "%d", scroll+i+1);
            int lnw, lnh; gfx_text_size(lnum, 12, &lnw, &lnh);
            gfx_text(text_x - lnw - DP(6), ly + (lh-lnh)*0.5f, lnum, 12, COL_TEXT_DIM);
            /* current-line highlight */
            if (is_editor && scroll+i == app->txt_cursor_line)
                gfx_fill_rect(text_x, ly, text_w, (float)lh, (Color){55,56,50,255});
            /* cursor */
            if (is_editor && scroll+i == app->txt_cursor_line && caret_on()) {
                int ccol = app->txt_cursor_col; if (ccol > ll) ccol = ll;
                char tmp[2048]; memcpy(tmp, linebuf, ccol); tmp[ccol] = '\0';
                int cx = gfx_prefix_w(tmp, ccol, fs);
                gfx_fill_rect(text_x + cx, ly + DP(1), DP(2), (float)(lh - (int)DP(2)), COL_ACCENT_Hi);
            }
            gfx_text_clip(text_x, ly, text_w, linebuf, fs, COL_TEXT);
        }
        if (scroll + maxlines < g_txt_lines)
            gfx_text(text_x, ty + maxlines*lh, "...", fs, COL_TEXT_DIM);
        gfx_pop_clip();

        /* scrollbar */
        if (g_txt_lines > maxlines) {
            float sb_x = pv.x + pv.w - DP(6);
            float sb_h = pv.h - DP(8);
            float thumb_h = sb_h * maxlines / g_txt_lines;
            if (thumb_h < DP(20)) thumb_h = DP(20);
            float thumb_y = pv.y + DP(4) + (sb_h - thumb_h) * scroll / (g_txt_lines - maxlines);
            gfx_fill_rect(sb_x, pv.y+DP(4), DP(4), sb_h, (Color){60,60,55,255});
            gfx_fill_rect(sb_x, thumb_y, DP(4), thumb_h, COL_BORDER);
        }
    }
    else {
        app->pdf_page_count = 0;
        gfx_text(pv.x+DP(16), pv.y+DP(16), "(no preview for this file type)", 16, COL_TEXT_DIM);
    }
}

static void draw_detail_preview(AppState* app, R pv, const Item* it, bool is_editor)
{
    if (app->show_compiled && app->compiled_for == app->selected && app->compiled_pdf[0]) {
        draw_preview_at(app, pv, app->compiled_pdf, app->compiled_pdf, false);
        return;
    }
    char full[PATH_LEN];
    store_full_path(it->filename, full, sizeof(full));
    draw_preview_at(app, pv, full, it->filename, is_editor);
}

/* ---- top bar ----------------------------------------------------------- */
static void draw_topbar(AppState* app, int w)
{
    gfx_fill_rect(0, 0, (float)w, DP(PX_TOPBAR), COL_PANEL);
    gfx_fill_rect(0, DP(PX_TOPBAR), (float)w, DP(1), COL_BORDER);
    gfx_text(DP(PX_PAD), DP(18), APP_TITLE, 26, COL_ACCENT);
    if (app->view == VIEW_DETAIL) {
        /* show modified indicator when editing text */
        if (g_txt_modified) {
            int tw, th; gfx_text_size("(unsaved)", 13, &tw, &th);
            gfx_text((float)w*0.5f - tw*0.5f, DP(PX_TOPBAR)*0.5f - th*0.5f, "(unsaved)", 13, COL_ACCENT);
        }
    }
}

static void draw_status(AppState* app, int w, int h)
{
    gfx_fill_rect(0, (float)h-DP(PX_STATUS), (float)w, DP(PX_STATUS), COL_PANEL);
    gfx_fill_rect(0, (float)h-DP(PX_STATUS), (float)w, DP(1), COL_BORDER);
    gfx_text_clip(DP(PX_PAD), (float)h-DP(PX_STATUS)+DP(5),
                  (float)w-2*DP(PX_PAD), app->status, 14, COL_TEXT_DIM);
}

/* ---- context menu ------------------------------------------------------ */
static void draw_context_menu(AppState* app, const Layout* L)
{
    if (!app->ctx_open) return;
    float total_h = (float)L->ctx_count * L->ctx[0].h;
    /* shadow */
    gfx_fill_rect(L->ctx[0].x+DP(2), L->ctx[0].y+DP(2), L->ctx[0].w, total_h, (Color){0,0,0,200});
    gfx_fill_rect(L->ctx[0].x, L->ctx[0].y, L->ctx[0].w, total_h, COL_CARD);
    gfx_stroke_round(L->ctx[0].x, L->ctx[0].y, L->ctx[0].w, total_h, 0, DP(1), COL_BORDER);
    int i;
    for (i = 0; i < L->ctx_count; i++) {
        int idx = app->ctx_offset + i;
        R r = L->ctx[i];
        if (app->hot_button == (int)g_ctx_hits[idx])
            gfx_fill_rect(r.x+DP(3), r.y+DP(2), r.w-DP(6), r.h-DP(4), COL_BORDER);
        int tw, th; gfx_text_size(g_ctx_labels[idx], 15, &tw, &th);
        gfx_text(r.x+DP(14), r.y+(r.h-th)*0.5f, g_ctx_labels[idx], 15, COL_TEXT);
    }
}

/* ---- scroll clamp ------------------------------------------------------ */
void views_clamp_scroll(AppState* app, int w, int h)
{
    Layout L; compute_layout(app, w, h, &L);
    static int fb[ITEM_MAX];
    int n = views_filtered(app, fb, ITEM_MAX);
    int rows = (n + L.cols - 1) / L.cols;
    float content_h = rows * (L.cell_h + L.gut);
    float view_h = (float)h - L.gy - DP(PX_STATUS) - DP(8);
    float maxs = content_h - view_h;
    if (maxs < 0) maxs = 0;
    if (app->list_scroll > (int)maxs) app->list_scroll = (int)maxs;
    if (app->list_scroll < 0) app->list_scroll = 0;
}

int views_gallery_cols(AppState* app, int w, int h)
{
    Layout L; compute_layout(app, w, h, &L);
    return L.cols;
}

void views_scroll_to_cursor(AppState* app, int w, int h)
{
    if (app->gallery_cursor < 0) return;
    Layout L; compute_layout(app, w, h, &L);
    static int fb[ITEM_MAX];
    int n = views_filtered(app, fb, ITEM_MAX);
    if (app->gallery_cursor >= n) return;
    int slot = app->gallery_cursor;
    float unscrolled_y = L.gy + (float)(slot / L.cols) * (L.cell_h + L.gut);
    float unscrolled_bot = unscrolled_y + L.cell_h;
    float view_top = DP(PX_TOPBAR);
    float view_h   = (float)h - DP(PX_STATUS) - DP(8) - view_top;
    float new_s    = (float)app->list_scroll;
    if (unscrolled_y - new_s < view_top)
        new_s = unscrolled_y - view_top;
    if (unscrolled_bot - new_s > view_h)
        new_s = unscrolled_bot - view_h;
    app->list_scroll = (int)(new_s + 0.5f);
    views_clamp_scroll(app, w, h);
}

/* ---- render_list ------------------------------------------------------- */
static void render_list(AppState* app, Layout* L)
{
    static int fb[ITEM_MAX];
    int n = views_filtered(app, fb, ITEM_MAX);

    gfx_push_clip(0, (int)DP(PX_TOPBAR)+1, L->w,
                  L->h - (int)DP(PX_TOPBAR) - (int)DP(PX_STATUS) - 1);
    int slot;
    for (slot = 0; slot < n; slot++) {
        R c = item_rect(L, slot, (float)app->list_scroll);
        if (c.y+c.h < DP(PX_TOPBAR) || c.y > L->h-DP(PX_STATUS)) continue;
        const Item* it = &app->list.items[fb[slot]];
        bool is_img = is_image_path(it->filename);
        bool has_cover = it->cover[0] != '\0';
        if (slot == app->gallery_cursor)
            gfx_fill_rect(c.x - DP(3), c.y - DP(3), c.w + DP(6), c.h + DP(6), COL_BORDER_Hi);
        if (!is_img || has_cover) gfx_card(c.x, c.y, c.w, c.h, COL_CARD, COL_BORDER, true);
        R thumb = (is_img && !has_cover) ? (R){ c.x, c.y, c.w, c.w }
                                         : (R){ c.x+DP(6), c.y+DP(6), c.w-DP(12), c.w-DP(12) };
        draw_item_thumb(thumb, it);
        char tagline[128];
        if (it->tags[0] || it->meta[0]) {
            SDL_strlcpy(tagline, it->tags, sizeof(tagline));
            if (it->meta[0]) {
                if (tagline[0]) SDL_strlcat(tagline, " ", sizeof(tagline));
                SDL_strlcat(tagline, it->meta, sizeof(tagline));
            }
        } else {
            SDL_strlcpy(tagline, it->filename, sizeof(tagline));
        }
        gfx_text_clip(c.x+DP(8), c.y+c.h-DP(28), c.w-DP(16), tagline, 14, COL_TEXT_DIM);
    }
    gfx_pop_clip();

    if (n == 0) {
        bool any = app->search[0] || app->search_content[0];
        const char* msg = any ? "No matches." : "Empty archive — click Add to import a file.";
        int tw, th; gfx_text_size(msg, 16, &tw, &th);
        gfx_text((L->w-tw)*0.5f, (L->h)*0.5f, msg, 16, COL_TEXT_DIM);
    }

    draw_tag_search_field(L->search, app->search, &app->ed_search, app->focus == FOCUS_SEARCH);
    if (app->focus == FOCUS_SEARCH) {
        char prefix[128];
        if (last_prefix(app->search, prefix, sizeof(prefix))) {
            if (SDL_strcasecmp(app->sug_prefix, prefix) != 0) {
                SDL_strlcpy(app->sug_prefix, prefix, sizeof(app->sug_prefix));
                app->sug_index = 0;
            }
            app->sug_count = tag_suggestions(app, prefix, app->sug, SUG_MAX);
            app->sug_open = (app->sug_count > 0);
            if (app->sug_open) {
                if (app->sug_index < 0) app->sug_index = 0;
                if (app->sug_index >= app->sug_count) app->sug_index = app->sug_count - 1;
                app->sug_row_h = DP(24);
                app->sug_x = L->search.x;
                app->sug_y = L->search.y + L->search.h + DP(6);
                app->sug_w = L->search.w;
                draw_tag_dropdown(L->search, app->sug, app->sug_count, app->sug_index);
            }
        } else {
            app->sug_open = false; app->sug_count = 0; app->sug_prefix[0] = '\0';
        }
    } else {
        app->sug_open = false; app->sug_count = 0; app->sug_prefix[0] = '\0';
    }
    draw_text_field(L->search_content, app->search_content, &app->ed_search_content,
                    app->focus == FOCUS_SEARCH_CONTENT, "search contents…", 15);
    draw_button(L->add, "+  Add", app->hot_button==HIT_ADD, app->active_button==HIT_ADD, true);
}

/* ---- render_detail ----------------------------------------------------- */
/* Draw a side nav panel: thumbnail of an adjacent item, or empty/dim. */
static void draw_nav_panel(R r, const Item* item, bool hot, bool active)
{
    (void)active;
    if (!item) return;  /* no adjacent item — draw nothing */

    float m = DP(6);
    R tr = { r.x+m, r.y+m, r.w-2*m, r.h-2*m };
    draw_item_thumb(tr, item);

    /* dim overlay over thumbnail only; clears on hover */
    if (!hot)
        gfx_fill_rect(tr.x, tr.y, tr.w, tr.h, (Color){39,40,34,110});
}

static void render_detail(AppState* app, Layout* L)
{
    if (app->selected < 0 || app->selected >= app->list.count) return;
    const Item* it = &app->list.items[app->selected];

    /* find adjacent items */
    static int fb[ITEM_MAX];
    int fn = views_filtered(app, fb, ITEM_MAX);
    const Item* prev_item = NULL, *next_item = NULL;
    int pos = -1, ni;
    for (ni = 0; ni < fn; ni++) { if (fb[ni] == app->selected) { pos = ni; break; } }
    if (pos > 0)              prev_item = &app->list.items[fb[pos-1]];
    if (pos >= 0 && pos < fn-1) next_item = &app->list.items[fb[pos+1]];

    /* three panels */
    draw_nav_panel(L->d_prev, prev_item,
                   app->hot_button==HIT_D_PREV, app->active_button==HIT_D_PREV);
    bool is_txted = (app->focus == FOCUS_TXTED) && is_text_path(it->filename);
    draw_detail_preview(app, L->d_curr, it, is_txted);
    /* border around current */
    gfx_fill_rect(L->d_curr.x, L->d_curr.y, L->d_curr.w, DP(1), COL_BORDER);
    gfx_fill_rect(L->d_curr.x, L->d_curr.y+L->d_curr.h-DP(1), L->d_curr.w, DP(1), COL_BORDER);
    draw_nav_panel(L->d_next, next_item,
                   app->hot_button==HIT_D_NEXT, app->active_button==HIT_D_NEXT);

    /* hint overlay on side panels to indicate they're clickable */
    if (prev_item) {
        int tw, th; gfx_text_size("<", 20, &tw, &th);
        gfx_text(L->d_prev.x + DP(6), L->d_prev.y + L->d_prev.h - th - DP(8), "<", 20, COL_TEXT_DIM);
    }
    if (next_item) {
        int tw, th; gfx_text_size(">", 20, &tw, &th);
        gfx_text(L->d_next.x + L->d_next.w - tw - DP(6),
                 L->d_next.y + L->d_next.h - th - DP(8), ">", 20, COL_TEXT_DIM);
    }

    /* tag strip — centered */
    {
        R ts = L->d_tags;
        int fs = 15;
        float avail = ts.w - DP(244); /* reserve right side for filename */
        /* measure total render width for centering */
        float total_tw = 0;
        {
            const char* p2 = it->tags;
            while (*p2) {
                const char* sp = strchr(p2, ' ');
                int len = sp ? (int)(sp-p2) : (int)strlen(p2);
                if (len > 0) {
                    char tok[128]; if (len > 127) len = 127;
                    memcpy(tok, p2, len); tok[len] = '\0';
                    int tw2, th2; gfx_text_size(tok, fs, &tw2, &th2); (void)th2;
                    total_tw += tw2 + DP(16);
                }
                if (!sp) break; p2 = sp+1;
            }
            p2 = it->meta;
            while (*p2) {
                const char* sp = strchr(p2, ' ');
                int len = sp ? (int)(sp-p2) : (int)strlen(p2);
                if (len > 0) {
                    char tok[128]; if (len > 127) len = 127;
                    memcpy(tok, p2, len); tok[len] = '\0';
                    int tw2, th2; gfx_text_size(tok, fs, &tw2, &th2); (void)th2;
                    total_tw += tw2 + DP(16);
                }
                if (!sp) break; p2 = sp+1;
            }
            if (total_tw > DP(4)) total_tw -= DP(16);
        }
        if (total_tw > avail) total_tw = avail;
        float x = ts.x + (avail - total_tw) * 0.5f;
        float y = ts.y + (ts.h - DP(24))*0.5f;
        const char* p = it->tags;
        while (*p) {
            const char* sp = strchr(p, ' ');
            int len = sp ? (int)(sp-p) : (int)strlen(p);
            if (len > 0) {
                char tok[128]; if (len > 127) len = 127;
                memcpy(tok, p, len); tok[len] = '\0';
                int tw, th; gfx_text_size(tok, fs, &tw, &th);
                Color br, fg; pick_tag_colors(tok, &br, &fg); (void)br;
                gfx_text(x, y+(DP(24)-th)*0.5f, tok, fs, fg);
                x += tw + DP(16);
                if (x > ts.x + avail) break;
            }
            if (!sp) break; p = sp+1;
        }
        p = it->meta;
        while (*p && x <= ts.x+avail) {
            const char* sp = strchr(p, ' ');
            int len = sp ? (int)(sp-p) : (int)strlen(p);
            if (len > 0) {
                char tok[128]; if (len > 127) len = 127;
                memcpy(tok, p, len); tok[len] = '\0';
                int tw, th; gfx_text_size(tok, fs, &tw, &th);
                Color br, fg; pick_tag_colors(tok, &br, &fg); (void)br;
                gfx_text(x, y+(DP(24)-th)*0.5f, tok, fs, fg);
                x += tw + DP(16);
                if (x > ts.x+avail) break;
            }
            if (!sp) break; p = sp+1;
        }
        /* filename on far right */
        gfx_text_clip(ts.x+ts.w-DP(240), ts.y+(ts.h-gfx_text_h(13))*0.5f,
                      DP(236), it->filename, 13, COL_TEXT_DIM);
    }

    /* nav row */
    draw_button(L->d_back, "Back",
                app->hot_button==HIT_D_BACK, app->active_button==HIT_D_BACK, false);
    draw_button(L->d_add,  "+ Add",
                app->hot_button==HIT_ADD, app->active_button==HIT_ADD, true);

    /* action buttons */
    draw_button(L->d_edit, "Edit",
                app->hot_button==HIT_D_EDIT, app->active_button==HIT_D_EDIT, false);
    draw_button(L->d_del,  "Delete",
                app->hot_button==HIT_D_DELETE, app->active_button==HIT_D_DELETE, false);

    /* text edit mode hint */
    if (is_text_path(it->filename) && !app->show_compiled) {
        if (app->focus != FOCUS_TXTED) {
            int tw, th; gfx_text_size("Click to edit", 13, &tw, &th);
            gfx_text(L->d_curr.x + L->d_curr.w - tw - DP(10),
                     L->d_curr.y + DP(8), "Click to edit", 13, COL_TEXT_DIM);
        } else {
            /* show Ctrl+S hint and modified indicator */
            const char* hint = g_txt_modified ? "Ctrl+S to save  |  Esc to stop editing"
                                              : "Esc to stop editing";
            int tw, th; gfx_text_size(hint, 13, &tw, &th);
            gfx_text(L->d_curr.x + L->d_curr.w - tw - DP(10),
                     L->d_curr.y + DP(8), hint, 13, COL_BORDER_Hi);
        }
    }
}

/* ---- render_add -------------------------------------------------------- */
static void render_add(AppState* app, Layout* L)
{
    gfx_text(L->tags.x, DP(PX_TOPBAR)+DP(18),
             app->edit_index >= 0 ? "Edit file" : "Add file", 22, COL_TEXT);

    gfx_text(L->tags.x, L->tags.y-DP(24), "Tags", 15, COL_TEXT_DIM);
    draw_pills(L->tags, app->form_tags, app->focus == FOCUS_TAGS,
               "Type tags, separated by spaces…");
    char unknown[256];
    if (collect_unknown_tags(app->form_tags, unknown, sizeof(unknown))) {
        char msg[320];
        SDL_snprintf(msg, sizeof(msg), "Unknown tags: %s (right-click to add)", unknown);
        gfx_text_clip(L->tags.x, L->tags.y+L->tags.h+DP(4), L->tags.w, msg, 13, COL_TAG_BAD);
    }

    gfx_text(L->path.x, L->path.y-DP(24), "File path", 15, COL_TEXT_DIM);
    draw_text_field(L->path, app->form_path, &app->ed_path, app->focus == FOCUS_PATH,
                    "Path to image / PDF / TXT / CSV / TEX / MD…", 15);
    draw_button(L->browse, "Browse…",
                app->hot_button==HIT_A_BROWSE, app->active_button==HIT_A_BROWSE, false);

    /* Cover field: always shown for non-images; shown for images only when expanded */
    bool cur_is_img = is_image_path(app->form_path);
    bool show_cover = !cur_is_img || app->show_cover_options;

    if (cur_is_img) {
        const char* mo_label = app->show_cover_options ? "Options ^" : "More options...";
        draw_button(L->more_options, mo_label,
                    app->hot_button==HIT_A_MORE_OPTIONS, app->active_button==HIT_A_MORE_OPTIONS, false);
    }

    if (show_cover) {
        gfx_text(L->cover.x, L->cover.y-DP(22), "Custom cover image (optional)", 13, COL_TEXT_DIM);
        draw_text_field(L->cover, app->form_cover, &app->ed_cover, app->focus == FOCUS_COVER,
                        "Path to cover image (PNG/JPG)…", 15);
        draw_button(L->cover_browse, "Browse…",
                    app->hot_button==HIT_A_COVER_BROWSE, app->active_button==HIT_A_COVER_BROWSE, false);
    }

    draw_button(L->cancel, "Cancel",
                app->hot_button==HIT_A_CANCEL, app->active_button==HIT_A_CANCEL, false);
    draw_button(L->save, "Save",
                app->hot_button==HIT_A_SAVE, app->active_button==HIT_A_SAVE, true);

    /* live preview panel */
    if (L->preview_add.w > 0) {
        if (app->form_path[0]) {
            bool is_abs = (app->form_path[0] == '/'
#ifdef _WIN32
                           || (app->form_path[0] && app->form_path[1] == ':')
#endif
                          );
            const char* full = app->form_path;
            char resolved[PATH_LEN];
            if (!is_abs) { store_full_path(app->form_path, resolved, sizeof(resolved)); full = resolved; }
            char pdf_add_key[PATH_LEN+32];
            if (is_pdf_path(app->form_path)) {
                int boxw = (int)L->preview_add.w-(int)DP(16);
                int boxh = (int)L->preview_add.h-(int)DP(16);
                SDL_snprintf(pdf_add_key, sizeof(pdf_add_key), "add|%s|%d|%d", full, boxw/24, boxh/24);
                if (strcmp(pdf_add_key, g_pdf_key) != 0) {
                    SDL_strlcpy(g_pdf_key, pdf_add_key, sizeof(g_pdf_key));
                    if (g_pdf_tex) { SDL_DestroyTexture(g_pdf_tex); g_pdf_tex = NULL; }
                    int rw, rh, pc;
                    unsigned char* rgb = pdf_render_page(full, 0, boxw, boxh, &rw, &rh, &pc);
                    if (rgb) { g_pdf_tex = gfx_texture_from_rgb(rgb, rw, rh, 3); g_pdf_tw=rw; g_pdf_th=rh; free(rgb); }
                }
            }
            int save_page = app->pdf_page; app->pdf_page = 0;
            draw_preview_at(app, L->preview_add, full, app->form_path, false);
            app->pdf_page = save_page;
            app->pdf_page_count = 0;
        } else {
            gfx_fill_rect(L->preview_add.x, L->preview_add.y,
                          L->preview_add.w, L->preview_add.h, COL_PANEL);
            int tw, th; gfx_text_size("Preview", 16, &tw, &th);
            gfx_text(L->preview_add.x+(L->preview_add.w-tw)*0.5f,
                     L->preview_add.y+(L->preview_add.h-th)*0.5f, "Preview", 16, COL_TEXT_DIM);
        }
    }
}

/* ---- views_render ------------------------------------------------------ */
void views_render(AppState* app, int w, int h)
{
    Layout L; compute_layout(app, w, h, &L);
    gfx_clear(COL_BG);
    draw_topbar(app, w);
    if      (app->view == VIEW_LIST)   render_list(app, &L);
    else if (app->view == VIEW_DETAIL) render_detail(app, &L);
    else                               render_add(app, &L);
    draw_status(app, w, h);
    draw_context_menu(app, &L);
}

/* ---- hit testing ------------------------------------------------------- */
Hit views_hittest(AppState* app, int w, int h, float mx, float my)
{
    Layout L; compute_layout(app, w, h, &L);
    Hit hit = { HIT_NONE, -1 };

    /* context menu — highest priority */
    if (app->ctx_open) {
        int i;
        for (i = 0; i < L.ctx_count; i++)
            if (in_r(L.ctx[i], mx, my)) {
                hit.target = g_ctx_hits[app->ctx_offset + i];
                return hit;
            }
        hit.target = HIT_CTX_DISMISS;
        return hit;
    }

    /* tag suggestion dropdown */
    if (app->focus == FOCUS_SEARCH && app->sug_open && app->sug_count > 0) {
        int max_show = app->sug_count > 6 ? 6 : app->sug_count;
        float row_h = DP(24), pad = DP(8);
        float sx = L.search.x, sy = L.search.y + L.search.h + DP(6);
        float sw2 = L.search.w, sh = row_h * max_show + pad * 2;
        if (mx >= sx && mx < sx+sw2 && my >= sy && my < sy+sh) {
            int idx = (int)((my - sy - pad) / row_h);
            if (idx >= 0 && idx < max_show) {
                hit.target = HIT_SEARCH_SUG; hit.index = idx; return hit;
            }
        }
    }

    if (app->view == VIEW_LIST) {
        if (in_r(L.add, mx, my))            { hit.target = HIT_ADD; return hit; }
        if (in_r(L.search, mx, my))         { hit.target = HIT_SEARCH; return hit; }
        if (in_r(L.search_content, mx, my)) { hit.target = HIT_SEARCH_CONTENT; return hit; }
        if (my > DP(PX_TOPBAR) && my < h-DP(PX_STATUS)) {
            static int fb[ITEM_MAX];
            int n = views_filtered(app, fb, ITEM_MAX), slot;
            for (slot = 0; slot < n; slot++) {
                R c = item_rect(&L, slot, (float)app->list_scroll);
                if (in_r(c, mx, my)) { hit.target = HIT_ITEM; hit.index = fb[slot]; return hit; }
            }
        }
    } else if (app->view == VIEW_DETAIL) {
        /* nav row */
        if (in_r(L.d_back, mx, my)) { hit.target = HIT_D_BACK; return hit; }
        if (in_r(L.d_add,  mx, my)) { hit.target = HIT_ADD;    return hit; }
        /* panels */
        if (in_r(L.d_prev, mx, my)) { hit.target = HIT_D_PREV; return hit; }
        if (in_r(L.d_next, mx, my)) { hit.target = HIT_D_NEXT; return hit; }
        /* action buttons */
        if (in_r(L.d_edit, mx, my)) { hit.target = HIT_D_EDIT;   return hit; }
        if (in_r(L.d_del,  mx, my)) { hit.target = HIT_D_DELETE; return hit; }
        /* PDF page nav */
        if (app->pdf_page_count > 1 && in_r(L.d_pg_prev, mx, my)) { hit.target = HIT_D_PGPREV; return hit; }
        if (app->pdf_page_count > 1 && in_r(L.d_pg_next, mx, my)) { hit.target = HIT_D_PGNEXT; return hit; }
        /* tag strip — click a tag to search */
        if (app->selected >= 0 && app->selected < app->list.count && in_r(L.d_tags, mx, my)) {
            const Item* hit_it = &app->list.items[app->selected];
            R ts = L.d_tags;
            int fs = 15;
            float avail = ts.w - DP(244);
            /* replicate render: measure total width to get centered start x */
            float total_tw = 0;
            {
                const char* p2 = hit_it->tags;
                while (*p2) {
                    const char* sp = strchr(p2, ' ');
                    int len = sp ? (int)(sp-p2) : (int)strlen(p2);
                    if (len > 0) {
                        char tok[128]; if (len > 127) len = 127;
                        memcpy(tok, p2, len); tok[len] = '\0';
                        int tw2, th2; gfx_text_size(tok, fs, &tw2, &th2); (void)th2;
                        total_tw += tw2 + DP(16);
                    }
                    if (!sp) break; p2 = sp+1;
                }
                p2 = hit_it->meta;
                while (*p2) {
                    const char* sp = strchr(p2, ' ');
                    int len = sp ? (int)(sp-p2) : (int)strlen(p2);
                    if (len > 0) {
                        char tok[128]; if (len > 127) len = 127;
                        memcpy(tok, p2, len); tok[len] = '\0';
                        int tw2, th2; gfx_text_size(tok, fs, &tw2, &th2); (void)th2;
                        total_tw += tw2 + DP(16);
                    }
                    if (!sp) break; p2 = sp+1;
                }
                if (total_tw > DP(4)) total_tw -= DP(16);
            }
            if (total_tw > avail) total_tw = avail;
            float x = ts.x + (avail - total_tw) * 0.5f;
            float y = ts.y + (ts.h - DP(24)) * 0.5f;
            /* scan tags */
            const char* p = hit_it->tags;
            while (*p && x <= ts.x + avail) {
                const char* sp = strchr(p, ' ');
                int len = sp ? (int)(sp-p) : (int)strlen(p);
                if (len > 0) {
                    char tok[128]; if (len > 127) len = 127;
                    memcpy(tok, p, len); tok[len] = '\0';
                    int tw, th; gfx_text_size(tok, fs, &tw, &th);
                    if (mx >= x && mx <= x + tw + DP(16) && my >= y && my <= y + DP(24)) {
                        hit.target = HIT_D_TAG;
                        hit.index  = (int)(p - hit_it->tags);
                        return hit;
                    }
                    x += tw + DP(16);
                }
                if (!sp) break; p = sp+1;
            }
            /* scan meta (encode as ~offset so handler knows it's meta) */
            p = hit_it->meta;
            while (*p && x <= ts.x + avail) {
                const char* sp = strchr(p, ' ');
                int len = sp ? (int)(sp-p) : (int)strlen(p);
                if (len > 0) {
                    char tok[128]; if (len > 127) len = 127;
                    memcpy(tok, p, len); tok[len] = '\0';
                    int tw, th; gfx_text_size(tok, fs, &tw, &th);
                    if (mx >= x && mx <= x + tw + DP(16) && my >= y && my <= y + DP(24)) {
                        hit.target = HIT_D_TAG;
                        hit.index  = ~(int)(p - hit_it->meta);
                        return hit;
                    }
                    x += tw + DP(16);
                }
                if (!sp) break; p = sp+1;
            }
        }
        /* current panel — text editor click entry */
        if (in_r(L.d_curr, mx, my)) { hit.target = HIT_D_CURR; return hit; }
    } else {
        if      (in_r(L.tags,         mx, my)) hit.target = HIT_A_TAGS;
        else if (in_r(L.path,         mx, my)) hit.target = HIT_A_PATH;
        else if (in_r(L.browse,       mx, my)) hit.target = HIT_A_BROWSE;
        else if (in_r(L.save,         mx, my)) hit.target = HIT_A_SAVE;
        else if (in_r(L.cancel,       mx, my)) hit.target = HIT_A_CANCEL;
        else if (in_r(L.cover,        mx, my)) hit.target = HIT_A_COVER;
        else if (in_r(L.cover_browse, mx, my)) hit.target = HIT_A_COVER_BROWSE;
        else if (in_r(L.more_options, mx, my)) hit.target = HIT_A_MORE_OPTIONS;
    }
    return hit;
}

bool views_field_geom(AppState* app, int w, int h, HitTarget t,
                      FRectL* field, float* text_x, int* font_size)
{
    Layout L; compute_layout(app, w, h, &L);
    R r;
    if      (t == HIT_SEARCH)         r = L.search;
    else if (t == HIT_SEARCH_CONTENT) r = L.search_content;
    else if (t == HIT_A_PATH)         r = L.path;
    else if (t == HIT_A_COVER)        r = L.cover;
    else return false;
    field->x = r.x; field->y = r.y; field->w = r.w; field->h = r.h;
    *text_x    = r.x + DP(10);
    *font_size = 15;
    return true;
}
