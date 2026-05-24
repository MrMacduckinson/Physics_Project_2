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

/* Scale design-pixels (1× coords) to physical pixels for SDL rendering. */
#define DP(x)  ((float)(x) * g_dpi)

/* Base layout constants in design-pixels. */
#define PX_TOPBAR  68
#define PX_STATUS  28
#define PX_PAD     20

/* ---- small geometry helpers ------------------------------------------- */
typedef struct { float x, y, w, h; } R;
static bool in_r(R r, float x, float y)
{ return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

static const HitTarget g_ctx_hits[4] = {
    HIT_CTX_SELECT_ALL, HIT_CTX_COPY, HIT_CTX_CUT, HIT_CTX_PASTE
};
static const char* g_ctx_labels[4] = { "Select All", "Copy", "Cut", "Paste" };

typedef struct {
    int w, h;
    R search, search_content, add;
    /* gallery grid */
    float gx, gy, cell_w, cell_h, gut;
    int   cols;
    /* detail */
    R preview, strip, back, prev, next, edit, del, pg_prev, pg_next;
    /* add/edit form */
    R tags, path, browse, save, cancel;
    R preview_add;
    /* context menu (4 items) */
    R ctx[4];
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

    /* Two search bars in the topbar, right-aligned side by side */
    float bar_h = DP(40);
    float bar_y = (DP(PX_TOPBAR) - bar_h) * 0.5f;
    float sw = (float)w * 0.27f;
    if (sw > 340) sw = 340;
    if (sw < 130) sw = 130;
    L->search_content = (R){ (float)w - sw - DP(PX_PAD),          bar_y, sw, bar_h };
    L->search         = (R){ (float)w - sw*2 - DP(PX_PAD) - DP(8), bar_y, sw, bar_h };

    /* Floating add button, bottom-right */
    L->add = (R){ (float)w - DP(148) - DP(PX_PAD), (float)h - DP(PX_STATUS) - DP(58), DP(148), DP(46) };

    /* Gallery grid */
    float margin = DP(26), cellmin = DP(210);
    L->gut = DP(20);
    int cols = (int)((w - 2*margin + L->gut) / (cellmin + L->gut));
    if (cols < 1) cols = 1;
    L->cols   = cols;
    L->cell_w = (w - 2*margin - (cols-1)*L->gut) / cols;
    L->cell_h = L->cell_w + DP(42);
    L->gx = margin;
    L->gy = DP(PX_TOPBAR) + DP(18);

    /* Detail buttons */
    float btn_y = (float)h - DP(PX_STATUS) - DP(58);
    float bw = DP(116), bh = DP(46), gap = DP(12), bx = DP(PX_PAD);
    L->back = (R){ bx, btn_y, DP(96), bh };  bx += DP(96) + gap;
    L->prev = (R){ bx, btn_y, bw,     bh };  bx += bw + gap;
    L->next = (R){ bx, btn_y, bw,     bh };
    L->del  = (R){ (float)w - DP(PX_PAD) - DP(116), btn_y, DP(116), bh };
    L->edit = (R){ (float)w - DP(PX_PAD) - DP(232) - gap, btn_y, DP(116), bh };
    float strip_y = btn_y - DP(52);
    L->strip   = (R){ 0, strip_y, (float)w, DP(48) };
    L->preview = (R){ DP(PX_PAD), DP(PX_TOPBAR)+DP(8),
                      (float)w - 2*DP(PX_PAD),
                      strip_y - DP(PX_TOPBAR) - DP(16) };
    L->pg_prev = (R){ L->preview.x+DP(8),  L->preview.y+L->preview.h-DP(50), DP(46), DP(40) };
    L->pg_next = (R){ L->preview.x+DP(60), L->preview.y+L->preview.h-DP(50), DP(46), DP(40) };

    /* Add/edit form: left column + optional right preview */
    float fx = DP(PX_PAD)+DP(8), fw = (float)w - 2*(DP(PX_PAD)+DP(8));
    float top = DP(PX_TOPBAR)+DP(54), tags_h = DP(164);
    bool wide = ((float)w >= DP(680));
    float form_w = wide ? fw*0.54f : fw;
    L->tags   = (R){ fx, top, form_w, tags_h };
    L->path   = (R){ fx, top+tags_h+DP(58), form_w-DP(126), DP(44) };
    L->browse = (R){ fx+form_w-DP(118), top+tags_h+DP(58), DP(118), DP(44) };
    L->save   = (R){ fx, (float)h-DP(PX_STATUS)-DP(62), DP(118), DP(48) };
    L->cancel = (R){ fx+DP(118)+DP(12), (float)h-DP(PX_STATUS)-DP(62), DP(118), DP(48) };
    if (wide) {
        float px = fx+form_w+DP(18);
        L->preview_add = (R){ px, top, fw-form_w-DP(18), (float)h-top-DP(PX_STATUS)-DP(8) };
    }

    /* Context menu rects (computed from app state) */
    if (app->ctx_open) {
        float mw = DP(152), mh = DP(36);
        float mx = app->ctx_x, my = app->ctx_y;
        if (mx + mw > (float)w - DP(4)) mx = (float)w - mw - DP(4);
        if (my + 4*mh + DP(8) > (float)h) my = (float)h - 4*mh - DP(8);
        int i;
        for (i = 0; i < 4; i++)
            L->ctx[i] = (R){ mx, my + i*mh, mw, mh };
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
    Color tc = primary ? (Color){26,20,8,255} : COL_TEXT;
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

static void draw_pills(R r, const char* text, bool focused)
{
    gfx_card(r.x, r.y, r.w, r.h, COL_PANEL, focused ? COL_BORDER_Hi : COL_BORDER, false);
    float pad = DP(12), gap = DP(8), ph = DP(30);
    float x = r.x+pad, y = r.y+pad;
    int fs = 16;

    if (!text[0] && !focused) {
        gfx_text(x, r.y+(r.h-gfx_text_h(fs))*0.5f, "Type tags, separated by spaces…", fs, COL_TEXT_DIM);
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
                gfx_fill_round(x, y, pw, ph, ph*0.5f, COL_PILL_BG);
                gfx_stroke_round(x, y, pw, ph, ph*0.5f, DP(1), COL_PILL_BR);
                gfx_text(x+DP(9), y+(ph-th)*0.5f, tok, fs, COL_PILL_FG);
                x += pw + gap;
            } else {
                gfx_text(x, y+(ph-th)*0.5f, tok, fs, COL_TEXT);
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
            return;
        }
    }
    /* document / video badge */
    Color bg, fg; get_badge_colors(filename, &bg, &fg);
    float m = DP(14);
    gfx_fill_round(area.x+m, area.y+m, area.w-2*m, area.h-2*m, DP(8), bg);
    char lbl[16]; get_ext_label(filename, lbl, sizeof(lbl));
    int ltw, lth; gfx_text_size(lbl, 28, &ltw, &lth);
    gfx_text(area.x+(area.w-ltw)*0.5f, area.y+(area.h-lth)*0.5f, lbl, 28, fg);
}

/* ---- detail preview caches -------------------------------------------- */
static SDL_Texture* g_pdf_tex; static char g_pdf_key[1400]; static int g_pdf_tw, g_pdf_th;
static char g_txt_path[PATH_LEN]; static char g_txt_buf[16384];
static int  g_txt_off[400], g_txt_lines;

static void load_preview_text(const char* full)
{
    if (strcmp(g_txt_path, full) == 0) return;
    SDL_strlcpy(g_txt_path, full, sizeof(g_txt_path));
    g_txt_lines = 0; g_txt_buf[0] = '\0';
    FILE* f = fopen(full, "rb");
    if (!f) return;
    size_t n = fread(g_txt_buf, 1, sizeof(g_txt_buf)-1, f); fclose(f);
    g_txt_buf[n] = '\0';
    g_txt_off[g_txt_lines++] = 0;
    size_t i;
    for (i = 0; i < n && g_txt_lines < 399; i++) {
        if (g_txt_buf[i] == '\r' || g_txt_buf[i] == '\n') {
            if (g_txt_buf[i] == '\r' && g_txt_buf[i+1] == '\n') { g_txt_buf[i] = '\0'; i++; }
            g_txt_buf[i] = '\0';
            if (i+1 < n) g_txt_off[g_txt_lines++] = (int)(i+1);
        }
    }
}

static void draw_preview_at(AppState* app, R pv, const char* full, const char* filename)
{
    gfx_fill_round(pv.x, pv.y, pv.w, pv.h, DP(10), COL_PANEL);

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
        load_preview_text(full);
        app->pdf_page_count = 0;
        int fs = 15, lh = gfx_text_h(fs) + (int)DP(3);
        float tx = pv.x+DP(14), ty = pv.y+DP(12);
        int maxlines = (int)((pv.h-DP(24)) / lh);
        gfx_push_clip((int)pv.x, (int)pv.y, (int)pv.w, (int)pv.h);
        int i;
        for (i = 0; i < g_txt_lines && i < maxlines; i++)
            gfx_text_clip(tx, ty+i*lh, pv.w-DP(28), g_txt_buf+g_txt_off[i], fs, COL_TEXT);
        if (g_txt_lines > maxlines) gfx_text(tx, ty+maxlines*lh, "…", fs, COL_TEXT_DIM);
        gfx_pop_clip();
    }
    else {
        app->pdf_page_count = 0;
        gfx_text(pv.x+DP(16), pv.y+DP(16), "(no preview for this file type)", 16, COL_TEXT_DIM);
    }
}

static void draw_detail_preview(AppState* app, R pv, const Item* it)
{
    char full[PATH_LEN];
    store_full_path(it->filename, full, sizeof(full));
    draw_preview_at(app, pv, full, it->filename);
}

/* ---- top bar ----------------------------------------------------------- */
static void draw_topbar(int w)
{
    gfx_fill_rect(0, 0, (float)w, DP(PX_TOPBAR), COL_PANEL);
    gfx_fill_rect(0, DP(PX_TOPBAR), (float)w, DP(1), COL_BORDER);
    gfx_text(DP(PX_PAD), DP(18), APP_TITLE, 26, COL_ACCENT);
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
    /* panel background */
    float total_h = 4 * L->ctx[0].h;
    gfx_fill_round(L->ctx[0].x+DP(2), L->ctx[0].y+DP(2), L->ctx[0].w, total_h, DP(6),
                   (Color){10,9,8,200});
    gfx_fill_round(L->ctx[0].x, L->ctx[0].y, L->ctx[0].w, total_h, DP(6), COL_CARD);
    gfx_stroke_round(L->ctx[0].x, L->ctx[0].y, L->ctx[0].w, total_h, DP(6), DP(1), COL_BORDER);
    int i;
    for (i = 0; i < 4; i++) {
        R r = L->ctx[i];
        if (app->hot_button == (int)g_ctx_hits[i])
            gfx_fill_round(r.x+DP(3), r.y+DP(2), r.w-DP(6), r.h-DP(4), DP(4), COL_BORDER);
        int tw, th; gfx_text_size(g_ctx_labels[i], 15, &tw, &th);
        gfx_text(r.x+DP(14), r.y+(r.h-th)*0.5f, g_ctx_labels[i], 15, COL_TEXT);
    }
}

/* ---- public render ----------------------------------------------------- */
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
        gfx_card(c.x, c.y, c.w, c.h, COL_CARD, COL_BORDER, true);
        R thumb = { c.x+DP(6), c.y+DP(6), c.w-DP(12), c.w-DP(12) };
        draw_thumb(thumb, it->filename);
        char tagline[128];
        SDL_strlcpy(tagline, it->tags[0] ? it->tags : it->filename, sizeof(tagline));
        gfx_text_clip(c.x+DP(8), c.y+c.h-DP(28), c.w-DP(16), tagline, 14, COL_TEXT_DIM);
    }
    gfx_pop_clip();

    if (n == 0) {
        bool any = app->search[0] || app->search_content[0];
        const char* msg = any ? "No matches." : "Empty archive — click Add to import a file.";
        int tw, th; gfx_text_size(msg, 16, &tw, &th);
        gfx_text((L->w-tw)*0.5f, (L->h)*0.5f, msg, 16, COL_TEXT_DIM);
    }

    /* topbar search fields with labels */
    int ltw, lth;
    gfx_text_size("Tags", 13, &ltw, &lth);
    gfx_text(L->search.x, L->search.y - lth - DP(3), "Tags", 13, COL_TEXT_DIM);
    gfx_text_size("Content", 13, &ltw, &lth);
    gfx_text(L->search_content.x, L->search_content.y - lth - DP(3), "Content", 13, COL_TEXT_DIM);

    draw_text_field(L->search, app->search, &app->ed_search,
                    app->focus == FOCUS_SEARCH, "filter by tag…", 15);
    draw_text_field(L->search_content, app->search_content, &app->ed_search_content,
                    app->focus == FOCUS_SEARCH_CONTENT, "search contents…", 15);
    draw_button(L->add, "+  Add",
                app->hot_button==HIT_ADD, app->active_button==HIT_ADD, true);
}

static void render_detail(AppState* app, Layout* L)
{
    if (app->selected < 0 || app->selected >= app->list.count) return;
    const Item* it = &app->list.items[app->selected];
    draw_detail_preview(app, L->preview, it);

    gfx_fill_rect(L->strip.x, L->strip.y, L->strip.w, L->strip.h, COL_PANEL);
    R ps = { L->strip.x+DP(PX_PAD), L->strip.y+DP(4), L->strip.w*0.6f, L->strip.h-DP(8) };
    {
        float x = ps.x, y = ps.y+DP(4);
        int fs = 15; const char* p = it->tags;
        while (*p) {
            const char* sp = strchr(p, ' ');
            int len = sp ? (int)(sp-p) : (int)strlen(p);
            if (len > 0) {
                char tok[128]; if (len > 127) len = 127;
                memcpy(tok, p, len); tok[len] = '\0';
                int tw, th; gfx_text_size(tok, fs, &tw, &th);
                gfx_fill_round(x, y, (float)(tw+DP(16)), DP(24), DP(12), COL_PILL_BG);
                gfx_text(x+DP(8), y+(DP(24)-th)*0.5f, tok, fs, COL_PILL_FG);
                x += tw+DP(22);
                if (x > ps.x+ps.w) break;
            }
            if (!sp) break; p = sp+1;
        }
    }
    gfx_text_clip(L->strip.x+L->strip.w*0.62f, L->strip.y+DP(14),
                  L->strip.w*0.38f-DP(PX_PAD), it->filename, 14, COL_TEXT_DIM);

    draw_button(L->back, "Back",    app->hot_button==HIT_D_BACK,   app->active_button==HIT_D_BACK,   false);
    draw_button(L->prev, "< Prev",  app->hot_button==HIT_D_PREV,   app->active_button==HIT_D_PREV,   false);
    draw_button(L->next, "Next >",  app->hot_button==HIT_D_NEXT,   app->active_button==HIT_D_NEXT,   false);
    draw_button(L->edit, "Edit",    app->hot_button==HIT_D_EDIT,   app->active_button==HIT_D_EDIT,   false);
    draw_button(L->del,  "Delete",  app->hot_button==HIT_D_DELETE, app->active_button==HIT_D_DELETE, false);
}

static void render_add(AppState* app, Layout* L)
{
    gfx_text(DP(PX_PAD)+DP(8), DP(PX_TOPBAR)+DP(18),
             app->edit_index >= 0 ? "Edit file" : "Add file", 22, COL_TEXT);

    gfx_text(L->tags.x, L->tags.y-DP(24), "Tags", 15, COL_TEXT_DIM);
    draw_pills(L->tags, app->form_tags, app->focus == FOCUS_TAGS);

    gfx_text(L->path.x, L->path.y-DP(24), "File path", 15, COL_TEXT_DIM);
    draw_text_field(L->path, app->form_path, &app->ed_path, app->focus == FOCUS_PATH,
                    "Path to image / PDF / TXT / CSV / TEX / MD…", 15);
    draw_button(L->browse, "Browse…",
                app->hot_button==HIT_A_BROWSE, app->active_button==HIT_A_BROWSE, false);
    draw_button(L->cancel, "Cancel",
                app->hot_button==HIT_A_CANCEL, app->active_button==HIT_A_CANCEL, false);
    draw_button(L->save,   "Save",
                app->hot_button==HIT_A_SAVE,   app->active_button==HIT_A_SAVE,   true);

    /* live preview panel on the right */
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
            /* use add|... prefix to avoid colliding with detail-view pdf cache */
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
            draw_preview_at(app, L->preview_add, full, app->form_path);
            app->pdf_page = save_page;
            app->pdf_page_count = 0; /* no page nav in add view */
        } else {
            gfx_fill_round(L->preview_add.x, L->preview_add.y,
                           L->preview_add.w, L->preview_add.h, DP(10), COL_PANEL);
            int tw, th; gfx_text_size("Preview", 16, &tw, &th);
            gfx_text(L->preview_add.x+(L->preview_add.w-tw)*0.5f,
                     L->preview_add.y+(L->preview_add.h-th)*0.5f, "Preview", 16, COL_TEXT_DIM);
        }
    }
}

void views_render(AppState* app, int w, int h)
{
    Layout L; compute_layout(app, w, h, &L);
    gfx_clear(COL_BG);
    draw_topbar(w);
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

    /* context menu has highest priority */
    if (app->ctx_open) {
        int i;
        for (i = 0; i < 4; i++)
            if (in_r(L.ctx[i], mx, my)) { hit.target = g_ctx_hits[i]; return hit; }
        hit.target = HIT_CTX_DISMISS;
        return hit;
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
        if      (in_r(L.back, mx, my)) hit.target = HIT_D_BACK;
        else if (in_r(L.prev, mx, my)) hit.target = HIT_D_PREV;
        else if (in_r(L.next, mx, my)) hit.target = HIT_D_NEXT;
        else if (in_r(L.edit, mx, my)) hit.target = HIT_D_EDIT;
        else if (in_r(L.del,  mx, my)) hit.target = HIT_D_DELETE;
        else if (app->pdf_page_count > 1 && in_r(L.pg_prev, mx, my)) hit.target = HIT_D_PGPREV;
        else if (app->pdf_page_count > 1 && in_r(L.pg_next, mx, my)) hit.target = HIT_D_PGNEXT;
    } else {
        if      (in_r(L.tags,   mx, my)) hit.target = HIT_A_TAGS;
        else if (in_r(L.path,   mx, my)) hit.target = HIT_A_PATH;
        else if (in_r(L.browse, mx, my)) hit.target = HIT_A_BROWSE;
        else if (in_r(L.save,   mx, my)) hit.target = HIT_A_SAVE;
        else if (in_r(L.cancel, mx, my)) hit.target = HIT_A_CANCEL;
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
    else return false;
    field->x = r.x; field->y = r.y; field->w = r.w; field->h = r.h;
    *text_x    = r.x + DP(10);
    *font_size = 15;
    return true;
}
