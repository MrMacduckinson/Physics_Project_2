/* View rendering, layout, hit-testing for Memebooru SDL 1.2 edition.
   Monokai theme, 3-panel detail view, gallery with keyboard cursor,
   tag suggestions, context menu, cover image support. */
#include "views.h"
#include "store.h"
#include "textedit.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* ---- globals ------------------------------------------------------------- */
static int g_previews_enabled = 1;

/* ---- thumb cache --------------------------------------------------------- */
#define THUMB_CACHE_MAX 32
typedef struct { int idx; char path[PATH_MAX_LEN]; SDL_Surface* surf; unsigned long stamp; } ThumbEntry;
static ThumbEntry g_thumb[THUMB_CACHE_MAX];
static unsigned long g_thumb_stamp = 1;
static SDL_Surface* g_detail_surf;
static char         g_detail_path[PATH_MAX_LEN];

void views_clear_caches(void)
{
    for (int i = 0; i < THUMB_CACHE_MAX; i++) {
        if (g_thumb[i].surf) { SDL_FreeSurface(g_thumb[i].surf); g_thumb[i].surf = NULL; }
        g_thumb[i].idx = -1; g_thumb[i].path[0] = '\0'; g_thumb[i].stamp = 0;
    }
    if (g_detail_surf) { SDL_FreeSurface(g_detail_surf); g_detail_surf = NULL; }
    g_detail_path[0] = '\0';
}

static SDL_Surface* s_get_thumb(int idx, const char* path, int sz)
{
    if (!path || !g_previews_enabled) return NULL;
    for (int i = 0; i < THUMB_CACHE_MAX; i++) {
        if (g_thumb[i].surf && g_thumb[i].idx == idx && strcmp(g_thumb[i].path, path)==0) {
            g_thumb[i].stamp = g_thumb_stamp++;
            return g_thumb[i].surf;
        }
    }
    SDL_Surface* img = load_image_surface(path, sz, sz);
    if (img && (img->w > sz || img->h > sz)) {
        SDL_Surface* sc = scale_surface(img, sz, sz);
        if (sc != img) { SDL_FreeSurface(img); img = sc; }
    }
    if (!img) return NULL;
    int slot = -1; unsigned long oldest = 0xffffffff;
    for (int i = 0; i < THUMB_CACHE_MAX; i++) {
        if (!g_thumb[i].surf) { slot = i; break; }
        if (g_thumb[i].stamp < oldest) { oldest = g_thumb[i].stamp; slot = i; }
    }
    if (g_thumb[slot].surf) SDL_FreeSurface(g_thumb[slot].surf);
    g_thumb[slot].surf = img; g_thumb[slot].idx = idx;
    safe_copy(g_thumb[slot].path, sizeof(g_thumb[slot].path), path);
    g_thumb[slot].stamp = g_thumb_stamp++;
    return img;
}

static SDL_Surface* s_get_detail(const char* path, int max_w, int max_h)
{
    if (!path || !g_previews_enabled) return NULL;
    if (g_detail_surf && strcmp(g_detail_path, path)==0) return g_detail_surf;
    if (g_detail_surf) { SDL_FreeSurface(g_detail_surf); g_detail_surf = NULL; }
    SDL_Surface* img = load_image_surface(path, max_w, max_h);
    if (img && (img->w > max_w || img->h > max_h)) {
        SDL_Surface* sc = scale_surface(img, max_w, max_h);
        if (sc != img) { SDL_FreeSurface(img); img = sc; }
    }
    g_detail_surf = img;
    safe_copy(g_detail_path, sizeof(g_detail_path), path);
    return img;
}

/* ---- layout -------------------------------------------------------------- */
#define TOP_BAR_H   64
#define STATUS_BAR_H 28
#define BTN_H        36
#define GUTTER       20
#define MARGIN       26

typedef struct { int x,y,w,h; } MbRect;
static int s_in(MbRect r, int x, int y) {
    return x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h;
}
static MbRect s_r(int x, int y, int w, int h) { MbRect r={x,y,w,h}; return r; }

/* Filtered indices */
static int s_filt[ITEM_MAX];
static int s_filt_n;
static void s_rebuild_filt(AppState* app)
{
    s_filt_n = 0;
    for (int i = 0; i < app->list.count && s_filt_n < ITEM_MAX; i++)
        if (item_matches(&app->list.items[i], i, app->search, app->search_content))
            s_filt[s_filt_n++] = i;
}

int views_gallery_cols(AppState* app)
{
    (void)app;
    int cols = (screen->w - 2*MARGIN + GUTTER) / (140 + GUTTER);
    return cols < 1 ? 1 : cols;
}

static void s_gallery_geom(AppState* app, int* cell_w, int* cell_h, int* cols)
{
    *cols   = views_gallery_cols(app);
    *cell_w = (screen->w - 2*MARGIN - (*cols-1)*GUTTER) / *cols;
    *cell_h = *cell_w + 42;
}

void views_scroll_to_cursor(AppState* app)
{
    if (app->gallery_cursor < 0) return;
    int cell_w, cell_h, cols;
    s_gallery_geom(app, &cell_w, &cell_h, &cols);
    int gallery_h = screen->h - TOP_BAR_H - STATUS_BAR_H;
    int row = app->gallery_cursor / cols;
    int gy  = TOP_BAR_H + 16;
    int item_top = gy + row * (cell_h + GUTTER) - app->list_scroll;
    if (item_top < TOP_BAR_H) app->list_scroll -= (TOP_BAR_H - item_top);
    if (item_top + cell_h > TOP_BAR_H + gallery_h) app->list_scroll += (item_top + cell_h - TOP_BAR_H - gallery_h);
    if (app->list_scroll < 0) app->list_scroll = 0;
}

/* ---- draw helpers -------------------------------------------------------- */
static void s_fill(int x, int y, int w, int h, Uint32 c) { fill_rect(x,y,w,h,c); }
static void s_card(int x, int y, int w, int h, Uint32 bg, Uint32 border)
{
    s_fill(x+2, y+2, w, h, COL_SHADOW);
    s_fill(x,   y,   w, h, border);
    s_fill(x+1, y+1, w-2, h-2, bg);
}
static void s_btn(int x, int y, int w, int h, const char* lbl, int active)
{
    Uint32 bg  = active ? COL_ACCENT : COL_CARD;
    Uint32 bdr = active ? COL_ACCENT : COL_BORDER;
    s_card(x, y, w, h, bg, bdr);
    draw_button(x, y, w, h, lbl, bg, active ? MK_COL(39,40,34) : COL_TEXT, 0);
}

/* Draw a thumb (image or type badge) into rect, for a given item */
static void s_draw_thumb_item(const Item* item, int idx, int x, int y, int sz)
{
    const char* filename = (item->cover[0]) ? item->cover : item->filename;
    char full[PATH_MAX_LEN];
    store_full_path(filename, full, sizeof(full));

    if (is_image_path(full)) {
        SDL_Surface* img = s_get_thumb(idx, full, sz);
        if (img) {
            SDL_Rect dst;
            dst.x = x + (sz - img->w) / 2;
            dst.y = y + (sz - img->h) / 2;
            dst.w = img->w; dst.h = img->h;
            SDL_BlitSurface(img, NULL, screen, &dst);
            /* small ext label bottom-left for non-image files */
            if (item->cover[0] && !is_image_path(item->filename)) {
                char lbl[8]; get_ext_label(item->filename, lbl, sizeof(lbl));
                Uint32 bb, bf; get_doc_badge_colors(item->filename, &bb, &bf);
                draw_text(x+5, y+sz-14, lbl, bb, 1);
            }
            return;
        }
    }
    Uint32 bb, bf; get_doc_badge_colors(full, &bb, &bf);
    (void)bf;
    char lbl[8]; get_ext_label(full, lbl, sizeof(lbl));
    int lw = (int)strlen(lbl)*8*2;
    draw_text(x+(sz-lw)/2, y+(sz-14)/2, lbl, bb, 2);
}

/* ---- tag pill drawing ---------------------------------------------------- */
static void s_draw_pills(int fx, int fy, int fw, int fh,
                          const char* text, int cursor, int focused)
{
#ifdef USE_SDL_TTF
    int cw = g_ttf_char_w > 0 ? g_ttf_char_w : 8;
#else
    int cw = 8;
#endif
    const int pad = 7, gap = 6;
    int pill_h = fh > 36 ? 26 : fh - 8;
    if (pill_h < 14) pill_h = 14;
    int pill_y = fy + (fh - pill_h) / 2;
    int text_y = pill_y + (pill_h - 14) / 2 + 1;
    int vx     = fx + 8;
    int cursor_vx = -1;
    int len = (int)strlen(text);
    const char* p = text; const char* end = text + len;

    SDL_Rect clip_r = {fx, fy, fw, fh};
    SDL_SetClipRect(screen, &clip_r);

    while (p <= end) {
        int idx = (int)(p - text);
        if (p == end) { if (focused && cursor==idx) cursor_vx = vx; break; }
        const char* ws = p;
        while (p < end && *p != ' ') p++;
        int wlen = (int)(p - ws);
        int committed = (p < end);

        if (committed) {
            char tok[128]; int tl = wlen < 127 ? wlen : 127;
            memcpy(tok, ws, tl); tok[tl] = '\0';
            int kind = store_tag_kind(tok);
            Uint32 fg = (kind==2) ? COL_META_C : (kind==1 ? COL_TAG : COL_TAG_BAD);
            int pw = wlen * cw + pad * 2;
            char word[512]; int wl = wlen<511?wlen:511;
            memcpy(word, ws, wl); word[wl]='\0';
            draw_text(vx+pad, text_y, word, fg, 1);
            int wi = (int)(ws-text);
            if (focused && cursor>=wi && cursor<wi+wlen) cursor_vx = vx+pad+(cursor-wi)*cw;
            if (focused && cursor==wi+wlen)              cursor_vx = vx+pw+gap/2;
            vx += pw + gap; p++;
        } else {
            /* in-progress word (not yet committed with space) */
            char word[512]; int wl = wlen<511?wlen:511;
            memcpy(word, ws, wl); word[wl]='\0';
            draw_text(vx, text_y, word, COL_TEXT, 1);
            int wi = (int)(ws-text);
            if (focused && cursor>=wi && cursor<=wi+wlen) cursor_vx = vx+(cursor-wi)*cw;
        }
    }
    if (focused && cursor_vx < 0) cursor_vx = vx;
    if (focused && (SDL_GetTicks()/530)%2==0)
        fill_rect(cursor_vx, pill_y+2, 2, pill_h-4, COL_TEXT);

    SDL_SetClipRect(screen, NULL);
}

/* ---- single-line editable text ------------------------------------------ */
static void s_draw_field(int fx, int fy, int fw, int fh,
                          const char* buf, const EditState* ed, int focused)
{
#ifdef USE_SDL_TTF
    int cw = g_ttf_char_w > 0 ? g_ttf_char_w : 8;
#else
    int cw = 8;
#endif
    int max_chars = (fw - 16) / cw;
    if (max_chars < 1) max_chars = 1;
    int ty = fy + (fh - 14) / 2;
    int tx = fx + 8;

    SDL_Rect clip_r = {fx, fy, fw, fh};
    SDL_SetClipRect(screen, &clip_r);

    /* selection highlight */
    int sel_a = ed->cursor < ed->anchor ? ed->cursor : ed->anchor;
    int sel_b = ed->cursor < ed->anchor ? ed->anchor : ed->cursor;
    if (sel_b > sel_a) {
        int da = sel_a < max_chars ? sel_a : max_chars;
        int db = sel_b < max_chars ? sel_b : max_chars;
        if (db > da) fill_rect(tx + da*cw, fy+2, (db-da)*cw, fh-4, COL_SELECT);
    }

    /* truncated text */
    int len = (int)strlen(buf);
    int vis  = len < max_chars ? len : max_chars;
    char tmp[512]; if (vis > 511) vis = 511;
    memcpy(tmp, buf, (size_t)vis); tmp[vis]='\0';
    draw_text(tx, ty, tmp, COL_TEXT, 1);

    /* cursor */
    if (focused && (SDL_GetTicks()/530)%2==0) {
        int cx = ed->cursor < max_chars ? ed->cursor : max_chars;
        fill_rect(tx + cx*cw, ty, 2, 14, COL_ACCENT);
    }

    SDL_SetClipRect(screen, NULL);
}

/* ---- suggestions dropdown ------------------------------------------------ */
static void s_draw_suggestions(AppState* app)
{
    if (!app->sug_open || app->sug_count <= 0) return;
    int max_show = app->sug_count > 6 ? 6 : app->sug_count;
    int row_h = 24, pad = 8;
    int x = app->sug_x, y = app->sug_y;
    int w = app->sug_w, h = row_h * max_show + pad * 2;
    s_fill(x, y, w, h, COL_CARD);
    s_fill(x, y, w, 1, COL_BORDER); s_fill(x, y+h-1, w, 1, COL_BORDER);
    s_fill(x, y, 1, h, COL_BORDER); s_fill(x+w-1, y, 1, h, COL_BORDER);
    for (int i = 0; i < max_show; i++) {
        int ry = y + pad + i * row_h;
        if (i == app->sug_active)
            s_fill(x+2, ry-1, w-4, row_h, COL_CARD_HI);
        Uint32 fg = (app->sug[i].kind==2) ? COL_META_C : (app->sug[i].kind==1 ? COL_TAG : COL_TAG_BAD);
        char line[180];
        snprintf(line, sizeof(line), "%s (%d)", app->sug[i].tok, app->sug[i].count);
        draw_text_fit(x+pad, ry + (row_h-14)/2, line, fg, 1, w-2*pad);
    }
}

/* ---- context menu -------------------------------------------------------- */
static const char* s_ctx_labels[] = { "Select All","Copy","Cut","Paste","Add as Tag","Add as Meta" };
static const int   s_ctx_count    = 6;
static MbRect s_ctx_rects[6];

static void s_draw_ctx(AppState* app)
{
    if (!app->ctx_open) return;
    int mh = 28, pad = 10;
    int cnt = (app->ctx_focus == FOCUS_TAGS) ? s_ctx_count : 4;
    int w = 160, h = cnt * mh + 8;
    int x = app->ctx_mx, y = app->ctx_my;
    if (x + w > screen->w - 4) x = screen->w - w - 4;
    if (y + h > screen->h - 4) y = screen->h - h - 4;
    s_fill(x, y, w, h, COL_CARD);
    s_fill(x, y, w, 1, COL_BORDER); s_fill(x, y+h-1, w, 1, COL_BORDER);
    s_fill(x, y, 1, h, COL_BORDER); s_fill(x+w-1, y, 1, h, COL_BORDER);
    for (int i = 0; i < cnt; i++) {
        s_ctx_rects[i] = s_r(x, y+4+i*mh, w, mh);
        draw_text_fit(x+pad, y+4+i*mh+(mh-14)/2, s_ctx_labels[i], COL_TEXT, 1, w-2*pad);
    }
}

/* ---- top bar ------------------------------------------------------------- */
static void s_draw_top_bar(AppState* app)
{
    s_fill(0, 0, screen->w, TOP_BAR_H, COL_PANEL);
    s_fill(0, TOP_BAR_H-2, screen->w, 2, COL_BORDER_HI);

    /* Title */
    draw_text(16, (TOP_BAR_H-14)/2, "Memebooru", COL_TEXT, 1);

    /* Two search boxes: tags | content */
    int sw  = screen->w / 4;
    if (sw > 300) sw = 300;
    if (sw < 120) sw = 120;
    int gap = 10;
    int sx2 = screen->w - sw - 16;
    int sx1 = sx2 - sw - gap;
    int sy  = (TOP_BAR_H - 32) / 2;

    /* search (tags) */
    Uint32 bdr1 = (app->focus==FOCUS_SEARCH) ? COL_BORDER_HI : COL_BORDER;
    s_card(sx1, sy, sw, 32, COL_BG, bdr1);
    draw_text(sx1+8, sy+9, "tags:", COL_TEXT_DIM, 1);
    int lbl_w = 5*8+2;
    s_draw_field(sx1+lbl_w+4, sy, sw-lbl_w-12, 32,
                 app->search, &app->ed_search, app->focus==FOCUS_SEARCH);

    /* search (content) */
    Uint32 bdr2 = (app->focus==FOCUS_SEARCH_CONTENT) ? COL_BORDER_HI : COL_BORDER;
    s_card(sx2, sy, sw, 32, COL_BG, bdr2);
    draw_text(sx2+8, sy+9, "body:", COL_TEXT_DIM, 1);
    s_draw_field(sx2+lbl_w+4, sy, sw-lbl_w-12, 32,
                 app->search_content, &app->ed_search_content, app->focus==FOCUS_SEARCH_CONTENT);
}

/* ---- status bar ---------------------------------------------------------- */
static void s_draw_status(AppState* app)
{
    int sy = screen->h - STATUS_BAR_H;
    s_fill(0, sy, screen->w, STATUS_BAR_H, COL_PANEL);
    s_fill(0, sy, screen->w, 1, COL_BORDER);
    draw_text_fit(14, sy+(STATUS_BAR_H-14)/2, app->status_line, COL_TEXT_DIM, 1, screen->w-28);
    /* Cmd+P preview toggle hint */
    if (!g_previews_enabled)
        draw_text_fit(screen->w-130, sy+(STATUS_BAR_H-14)/2, "[preview off]", COL_ACCENT, 1, 128);
}

/* ---- gallery (list view) ------------------------------------------------- */
static MbRect s_gallery_item_rect(int slot, int cell_w, int cell_h, int cols)
{
    int row = slot / cols, col = slot % cols;
    int gx  = MARGIN + col * (cell_w + GUTTER);
    int gy  = TOP_BAR_H + 16 + row * (cell_h + GUTTER);
    return s_r(gx, gy, cell_w, cell_h);
}

static void s_draw_list_view(AppState* app)
{
    s_fill(0, TOP_BAR_H, screen->w, screen->h - TOP_BAR_H - STATUS_BAR_H, COL_BG);
    s_rebuild_filt(app);

    int cell_w, cell_h, cols;
    s_gallery_geom(app, &cell_w, &cell_h, &cols);
    int thumb_sz = cell_w - 16;

    for (int slot = 0; slot < s_filt_n; slot++) {
        int idx = s_filt[slot];
        const Item* it = &app->list.items[idx];
        MbRect r = s_gallery_item_rect(slot, cell_w, cell_h, cols);
        int ry = r.y - app->list_scroll;
        if (ry + r.h < TOP_BAR_H) continue;
        if (ry > screen->h - STATUS_BAR_H) break;

        /* Card background */
        int is_sel = (idx == app->selected);
        int is_cur = (slot == app->gallery_cursor);
        Uint32 border = is_cur ? COL_BORDER_HI : (is_sel ? COL_TAG : COL_BORDER);
        s_card(r.x, ry, r.w, r.h, COL_CARD, border);

        /* Thumbnail */
        s_fill(r.x+8, ry+8, thumb_sz, thumb_sz, COL_BG);
        s_draw_thumb_item(it, idx, r.x+8, ry+8, thumb_sz);

        /* Label below thumbnail */
        int lx = r.x+8, ly = ry+8+thumb_sz+4, lw = cell_w-16;
        draw_text_fit(lx, ly, it->filename, COL_TEXT_DIM, 1, lw);
    }

    /* Add button (bottom-right) */
    int bx = screen->w - 16 - 140;
    int by = screen->h - STATUS_BAR_H - 12 - BTN_H;
    s_btn(bx, by, 140, BTN_H, "+ Add", app->active_button == HIT_LIST_ADD);
}

/* ---- detail view (3-panel) ----------------------------------------------- */
static void s_draw_detail_view(AppState* app)
{
    s_fill(0, TOP_BAR_H, screen->w, screen->h - TOP_BAR_H - STATUS_BAR_H, COL_BG);
    if (app->selected < 0 || app->selected >= app->list.count) return;
    const Item* cur = &app->list.items[app->selected];

    /* Layout */
    int nav_y   = TOP_BAR_H + 6, nav_h = 44;
    int acts_y  = screen->h - STATUS_BAR_H - 16 - BTN_H;
    int tags_h  = 44, tags_y = acts_y - 14 - tags_h;
    int pan_y   = nav_y + nav_h + 8;
    int pan_h   = tags_y - 10 - pan_y;
    if (pan_h < 80) pan_h = 80;
    int side_w  = screen->w * 17 / 100;
    int cen_w   = screen->w - 2*side_w - 16;
    int side_h  = pan_h / 2;
    int side_off = (pan_h - side_h) / 2;
    int cen_x   = side_w + 8;
    int right_x = cen_x + cen_w + 8;

    /* --- Nav bar --- */
    s_btn(16,             nav_y+(nav_h-BTN_H)/2, 80, BTN_H, "← Back", app->active_button==HIT_D_BACK);
    /* Item counter */
    char cnt[32];
    snprintf(cnt, sizeof(cnt), "%d / %d", app->selected+1, app->list.count);
    draw_text_fit(screen->w/2 - 30, nav_y+(nav_h-14)/2, cnt, COL_TEXT_DIM, 1, 100);

    /* --- Left panel (prev item) --- */
    int prev = -1;
    for (int i = app->selected-1; i >= 0; i--)
        if (item_matches(&app->list.items[i], i, app->search, app->search_content)) { prev = i; break; }
    MbRect lp = s_r(0, pan_y+side_off, side_w, side_h);
    s_fill(lp.x, lp.y, lp.w, lp.h, COL_CARD);
    if (prev >= 0) {
        s_draw_thumb_item(&app->list.items[prev], prev, lp.x+4, lp.y+4, side_w-8);
        draw_text(lp.x+4, lp.y+4, "←", COL_TEXT_DIM, 1);
    }

    /* --- Right panel (next item) --- */
    int next = -1;
    for (int i = app->selected+1; i < app->list.count; i++)
        if (item_matches(&app->list.items[i], i, app->search, app->search_content)) { next = i; break; }
    MbRect rp = s_r(right_x, pan_y+side_off, side_w, side_h);
    s_fill(rp.x, rp.y, rp.w, rp.h, COL_CARD);
    if (next >= 0) {
        s_draw_thumb_item(&app->list.items[next], next, rp.x+4, rp.y+4, side_w-8);
        draw_text(rp.x+rp.w-16, rp.y+4, "→", COL_TEXT_DIM, 1);
    }

    /* --- Center panel (current item) --- */
    s_fill(cen_x, pan_y, cen_w, pan_h, COL_PANEL);
    char full[PATH_MAX_LEN];
    store_full_path(cur->filename, full, sizeof(full));

    if (is_image_path(full)) {
        SDL_Surface* img = s_get_detail(full, cen_w-4, pan_h-4);
        if (img) {
            SDL_Rect dst;
            dst.x = cen_x + (cen_w - img->w) / 2;
            dst.y = pan_y  + (pan_h - img->h) / 2;
            dst.w = img->w; dst.h = img->h;
            SDL_BlitSurface(img, NULL, screen, &dst);
        } else {
            draw_text(cen_x+12, pan_y+16, "(image load failed)", COL_TEXT_DIM, 1);
        }
    } else if (is_text_path(full)) {
        txted_load(app, full);
        txted_draw(app, cen_x+2, pan_y+2, cen_w-4, pan_h-4,
                   COL_TEXT, COL_PANEL, MK_COL(210,205,190), COL_BORDER_HI);
    } else if (is_pdf_path(full)) {
        Uint32 bb, bf; get_doc_badge_colors(full, &bb, &bf);
        (void)bb;
        draw_text(cen_x+(cen_w-24)/2, pan_y+(pan_h-14)/2, "PDF", bf, 3);
        draw_text_fit(cen_x, pan_y+pan_h-20, cur->filename, COL_TEXT_DIM, 1, cen_w);
    } else {
        Uint32 bb, bf; get_doc_badge_colors(full, &bb, &bf);
        (void)bf;
        char lbl[8]; get_ext_label(full, lbl, sizeof(lbl));
        draw_text(cen_x+(cen_w-24)/2, pan_y+(pan_h-14)/2, lbl, bb, 2);
    }

    /* --- Tag strip --- */
    s_fill(0, tags_y, screen->w, tags_h, MK_COL(24,22,19));
    s_fill(0, tags_y, screen->w, 1, COL_BORDER);
    s_draw_pills(8, tags_y, screen->w-180, tags_h, cur->tags, -1, 0);
    /* filename right-aligned in strip */
    draw_text_fit(screen->w-176, tags_y+(tags_h-14)/2, cur->filename, COL_TEXT_DIM, 1, 168);

    /* --- Action buttons --- */
    int edit_x  = screen->w/2 - 128;
    int del_x   = screen->w/2 + 12;
    s_btn(edit_x, acts_y, 116, BTN_H, "Edit",    app->active_button==HIT_D_EDIT);
    s_btn(del_x,  acts_y, 116, BTN_H, "Delete",  app->active_button==HIT_D_DELETE);
}

/* ---- add / edit view ----------------------------------------------------- */
static void s_draw_add_view(AppState* app)
{
    s_fill(0, TOP_BAR_H, screen->w, screen->h - TOP_BAR_H - STATUS_BAR_H, COL_BG);

    int title_y = TOP_BAR_H + 14;
    draw_text(24, title_y, app->edit_index >= 0 ? "Edit meme" : "Add meme", COL_TEXT, 1);

    int fx = 24, fw = screen->w - 48;
    int top = title_y + 28;

    /* Tags field */
    int tags_h = 80;
    draw_text_fit(fx, top-18, "Tags (space-separated):", COL_TEXT_DIM, 1, fw);
    Uint32 tags_bdr = (app->focus==FOCUS_TAGS) ? COL_BORDER_HI : COL_BORDER;
    s_card(fx, top, fw, tags_h, COL_PANEL, tags_bdr);
    s_draw_pills(fx+2, top, fw-4, tags_h, app->form_tags,
                 app->ed_tags.cursor, app->focus==FOCUS_TAGS);

    /* Suggestion dropdown */
    if (app->sug_open) {
        app->sug_x = fx; app->sug_y = top + tags_h + 4; app->sug_w = fw;
        s_draw_suggestions(app);
    }

    /* Path field */
    int path_y = top + tags_h + (app->sug_open ? app->sug_count*24+12+8 : 16) + 20;
    draw_text_fit(fx, path_y-18, "File path:", COL_TEXT_DIM, 1, fw);
    int browse_w = 90;
    Uint32 path_bdr = (app->focus==FOCUS_PATH) ? COL_BORDER_HI : COL_BORDER;
    s_card(fx, path_y, fw - browse_w - 10, 36, COL_PANEL, path_bdr);
    s_draw_field(fx, path_y, fw - browse_w - 10, 36,
                 app->form_path, &app->ed_path, app->focus==FOCUS_PATH);
    s_btn(fx + fw - browse_w, path_y, browse_w, 36, "Browse…", app->active_button==HIT_A_BROWSE);

    /* More options toggle */
    int more_y = path_y + 36 + 8;
    int is_image_file = app->form_path[0] && is_image_path(app->form_path);
    if (is_image_file || app->form_cover[0]) {
        Uint32 mc = app->show_cover_options ? COL_ACCENT : COL_CARD;
        Uint32 mt = app->show_cover_options ? MK_COL(39,40,34) : COL_TEXT;
        s_card(fx, more_y, 160, 30, mc, COL_BORDER);
        draw_button(fx, more_y, 160, 30, app->show_cover_options ? "▾ Less options" : "▸ More options", mc, mt, 0);
    } else {
        more_y -= 38; /* skip the toggle if irrelevant */
    }

    /* Cover field (expanded) */
    int cover_y = more_y + 38;
    if (app->show_cover_options) {
        draw_text_fit(fx, cover_y-18, "Cover image (optional):", COL_TEXT_DIM, 1, fw);
        Uint32 cbdr = (app->focus==FOCUS_COVER) ? COL_BORDER_HI : COL_BORDER;
        s_card(fx, cover_y, fw - browse_w - 10, 36, COL_PANEL, cbdr);
        s_draw_field(fx, cover_y, fw - browse_w - 10, 36,
                     app->form_cover, &app->ed_cover, app->focus==FOCUS_COVER);
        s_btn(fx + fw - browse_w, cover_y, browse_w, 36, "Browse…", app->active_button==HIT_A_COVER_BROWSE);
        cover_y += 36 + 14;
    }

    /* Save / Cancel */
    int save_y = (app->show_cover_options ? cover_y : more_y + 38) + 8;
    s_fill(fx, save_y, 118, BTN_H, COL_ACCENT);   /* primary button */
    draw_button(fx, save_y, 118, BTN_H, "Save", COL_ACCENT, MK_COL(39,40,34), app->active_button==HIT_A_SAVE);
    s_btn(fx+118+12, save_y, 118, BTN_H, "Cancel", app->active_button==HIT_A_CANCEL);

    /* Preview panel on right (if wide enough) */
    int preview_x = 24 + fw/2 + 16;
    int preview_w = screen->w - preview_x - 16;
    if (preview_w > 100 && app->form_path[0]) {
        char full[PATH_MAX_LEN]; store_full_path(app->form_path, full, sizeof(full));
        if (is_image_path(app->form_path)) { /* try direct path first */
            SDL_Surface* img = s_get_detail(app->form_path, preview_w, screen->h - TOP_BAR_H - STATUS_BAR_H - 60);
            if (img) {
                SDL_Rect dst; dst.x = preview_x + (preview_w-img->w)/2; dst.y = TOP_BAR_H + 60;
                dst.w = img->w; dst.h = img->h;
                SDL_BlitSurface(img, NULL, screen, &dst);
            }
        }
    }
}

/* ---- main render --------------------------------------------------------- */
void views_render(AppState* app)
{
    SDL_FillRect(screen, NULL, COL_BG);
    s_draw_top_bar(app);
    switch (app->view) {
        case VIEW_LIST:   s_draw_list_view(app); break;
        case VIEW_DETAIL: s_draw_detail_view(app); break;
        case VIEW_ADD:    s_draw_add_view(app); break;
    }
    s_draw_status(app);
    /* Overlays on top */
    if (app->ctx_open)  s_draw_ctx(app);
}

/* ---- hit testing --------------------------------------------------------- */
Hit views_hittest(AppState* app, int mx, int my)
{
    Hit h = { HIT_NONE, -1 };

    /* Context menu takes priority */
    if (app->ctx_open) {
        int cnt = (app->ctx_focus == FOCUS_TAGS) ? s_ctx_count : 4;
        for (int i = 0; i < cnt; i++) {
            if (s_in(s_ctx_rects[i], mx, my)) {
                h.target = (HitTarget)(HIT_CTX_SELECT_ALL + i);
                return h;
            }
        }
        h.target = HIT_CTX_DISMISS; return h;
    }

    /* Top bar search fields */
    int sw  = screen->w / 4; if (sw>300) sw=300; if (sw<120) sw=120;
    int gap = 10;
    int sx2 = screen->w - sw - 16;
    int sx1 = sx2 - sw - gap;
    int sy  = (TOP_BAR_H - 32) / 2;
    if (point_in_rect(mx, my, sx1, sy, sw, 32)) { h.target = HIT_SEARCH; return h; }
    if (point_in_rect(mx, my, sx2, sy, sw, 32)) { h.target = HIT_SEARCH_CONTENT; return h; }

    /* Suggestion dropdown */
    if (app->sug_open && app->sug_count > 0) {
        int max_show = app->sug_count > 6 ? 6 : app->sug_count;
        int row_h = 24, pad = 8;
        int sh = row_h * max_show + pad * 2;
        if (point_in_rect(mx, my, app->sug_x, app->sug_y, app->sug_w, sh)) {
            int row = (my - app->sug_y - pad) / row_h;
            if (row >= 0 && row < app->sug_count) { app->sug_active = row; }
            h.target = HIT_A_TAGS; return h; /* focus stays */
        }
        app->sug_open = 0;
    }

    if (app->view == VIEW_LIST) {
        /* Add button */
        int bx = screen->w - 16 - 140;
        int by = screen->h - STATUS_BAR_H - 12 - BTN_H;
        if (point_in_rect(mx, my, bx, by, 140, BTN_H)) { h.target = HIT_LIST_ADD; return h; }
        /* Gallery items */
        s_rebuild_filt(app);
        int cell_w, cell_h, cols;
        s_gallery_geom(app, &cell_w, &cell_h, &cols);
        for (int slot = 0; slot < s_filt_n; slot++) {
            MbRect r = s_gallery_item_rect(slot, cell_w, cell_h, cols);
            int ry = r.y - app->list_scroll;
            if (ry + r.h < TOP_BAR_H) continue;
            if (ry > screen->h - STATUS_BAR_H) break;
            if (point_in_rect(mx, my, r.x, ry, r.w, r.h)) {
                h.target = HIT_ITEM; h.index = s_filt[slot]; return h;
            }
        }
    } else if (app->view == VIEW_DETAIL) {
        int nav_y  = TOP_BAR_H + 6, nav_h = 44;
        int acts_y = screen->h - STATUS_BAR_H - 16 - BTN_H;
        int tags_h = 44, tags_y = acts_y - 14 - tags_h;
        int pan_y  = nav_y + nav_h + 8;
        int pan_h  = tags_y - 10 - pan_y;
        if (pan_h < 80) pan_h = 80;
        int side_w = screen->w * 17 / 100;
        int cen_w  = screen->w - 2*side_w - 16;
        int cen_x  = side_w + 8;
        int right_x = cen_x + cen_w + 8;
        int side_h = pan_h/2, side_off = (pan_h-side_h)/2;

        if (point_in_rect(mx, my, 16, nav_y+(nav_h-BTN_H)/2, 80, BTN_H))  { h.target = HIT_D_BACK;   return h; }
        if (point_in_rect(mx, my, 0, pan_y+side_off, side_w, side_h))      { h.target = HIT_D_PREV;   return h; }
        if (point_in_rect(mx, my, right_x, pan_y+side_off, side_w, side_h)){ h.target = HIT_D_NEXT;   return h; }
        if (point_in_rect(mx, my, cen_x, pan_y, cen_w, pan_h))             { h.target = HIT_D_CENTER; return h; }
        int edit_x = screen->w/2-128, del_x = screen->w/2+12;
        if (point_in_rect(mx, my, edit_x, acts_y, 116, BTN_H))  { h.target = HIT_D_EDIT;    return h; }
        if (point_in_rect(mx, my, del_x,  acts_y, 116, BTN_H))  { h.target = HIT_D_DELETE;  return h; }
        /* tag strip — click a tag to search */
        if (app->selected >= 0 && app->selected < app->list.count) {
            int acts_y2 = screen->h - STATUS_BAR_H - 16 - BTN_H;
            int tags_h2 = 44, tags_y2 = acts_y2 - 14 - tags_h2;
            if (point_in_rect(mx, my, 0, tags_y2, screen->w - 180, tags_h2)) {
                const char* tags = app->list.items[app->selected].tags;
                const char* tgt = tags;
                const char* tend = tags + strlen(tags);
                int vx = 16; /* fx=8, vx = fx+8 = 16 in s_draw_pills */
                const int cw = 8, pad = 7, gap = 6;
                while (tgt <= tend) {
                    if (tgt == tend) break;
                    const char* ws = tgt;
                    while (tgt < tend && *tgt != ' ') tgt++;
                    int wlen = (int)(tgt - ws);
                    int committed = (tgt < tend);
                    int pw = wlen * cw + (committed ? pad * 2 : 0);
                    if (pw > 0 && point_in_rect(mx, my, vx, tags_y2, pw, tags_h2)) {
                        h.target = HIT_D_TAG;
                        h.index  = (int)(ws - tags);
                        return h;
                    }
                    if (committed) { vx += pw + gap; tgt++; }
                    else break;
                    if (vx > screen->w - 180) break;
                }
            }
        }
    } else if (app->view == VIEW_ADD) {
        int fx = 24, fw = screen->w - 48;
        int top = TOP_BAR_H + 14 + 28;
        int tags_h = 80;
        if (point_in_rect(mx, my, fx, top, fw, tags_h))             { h.target = HIT_A_TAGS;   return h; }
        int path_y = top + tags_h + 36;
        int browse_w = 90;
        if (point_in_rect(mx, my, fx, path_y, fw-browse_w-10, 36)) { h.target = HIT_A_PATH;   return h; }
        if (point_in_rect(mx, my, fx+fw-browse_w, path_y, browse_w, 36)) { h.target = HIT_A_BROWSE; return h; }
        int more_y = path_y + 44;
        int is_img = app->form_path[0] && is_image_path(app->form_path);
        if ((is_img || app->form_cover[0]) && point_in_rect(mx, my, fx, more_y, 160, 30))
            { h.target = HIT_A_MORE_OPTIONS; return h; }
        if (app->show_cover_options) {
            int cover_y = more_y + 38;
            if (point_in_rect(mx, my, fx, cover_y, fw-browse_w-10, 36)) { h.target = HIT_A_COVER;        return h; }
            if (point_in_rect(mx, my, fx+fw-browse_w, cover_y, browse_w, 36)) { h.target = HIT_A_COVER_BROWSE; return h; }
        }
        int save_y = more_y + (app->show_cover_options ? 38+50 : 38) + 8;
        if (point_in_rect(mx, my, fx, save_y, 118, BTN_H))         { h.target = HIT_A_SAVE;   return h; }
        if (point_in_rect(mx, my, fx+130, save_y, 118, BTN_H))     { h.target = HIT_A_CANCEL; return h; }
    }
    return h;
}

/* ---- tag suggestions ----------------------------------------------------- */
static int s_prefix_match(const char* tok, const char* prefix)
{
    size_t n = strlen(prefix);
    return n > 0 && ascii_strncasecmp(tok, prefix, n) == 0;
}

static void s_add_sug(TagSuggestion* out, int* n, const char* tok, int kind)
{
    for (int i = 0; i < *n; i++) {
        if (ascii_strcasecmp(out[i].tok, tok)==0) { out[i].count++; if (!out[i].kind && kind) out[i].kind=kind; return; }
    }
    if (*n >= SUG_MAX) return;
    safe_copy(out[*n].tok, sizeof(out[*n].tok), tok);
    out[*n].count = 1; out[*n].kind = kind; (*n)++;
}

void views_update_suggestions(AppState* app)
{
    /* Find the prefix of the last (incomplete) token */
    const char* txt = app->form_tags;
    size_t n = strlen(txt);
    if (n == 0 || txt[n-1] == ' ') { app->sug_open = 0; app->sug_count = 0; return; }
    size_t start = n;
    while (start > 0 && txt[start-1] != ' ') start--;
    char prefix[128];
    size_t plen = n - start;
    if (plen >= sizeof(prefix)) plen = sizeof(prefix)-1;
    memcpy(prefix, txt+start, plen); prefix[plen]='\0';

    app->sug_count = 0;
    /* Scan all items */
    for (int i = 0; i < app->list.count; i++) {
        for (int m = 0; m < 2; m++) {
            const char* src = m==0 ? app->list.items[i].tags : app->list.items[i].meta;
            int k = m==0 ? 1 : 2;
            const char* p = src;
            while (*p) {
                while (*p==' ') p++;
                const char* ws = p;
                while (*p && *p!=' ') p++;
                if (p > ws) {
                    char tok[128]; size_t tl = (size_t)(p-ws); if (tl>127) tl=127;
                    memcpy(tok, ws, tl); tok[tl]='\0';
                    if (s_prefix_match(tok, prefix)) s_add_sug(app->sug, &app->sug_count, tok, k);
                }
            }
        }
    }
    /* Sort by count desc */
    for (int a = 0; a < app->sug_count; a++)
        for (int b = a+1; b < app->sug_count; b++)
            if (app->sug[b].count > app->sug[a].count) {
                TagSuggestion tmp = app->sug[a]; app->sug[a] = app->sug[b]; app->sug[b] = tmp;
            }

    app->sug_open   = (app->sug_count > 0);
    app->sug_active = 0;
}

/* ---- EditState helpers --------------------------------------------------- */
void ed_clamp(EditState* ed, const char* buf)
{
    int len = (int)strlen(buf);
    if (ed->cursor < 0) ed->cursor = 0;
    if (ed->cursor > len) ed->cursor = len;
    if (ed->anchor < 0) ed->anchor = 0;
    if (ed->anchor > len) ed->anchor = len;
}
void ed_select_all(EditState* ed, const char* buf)
{ ed->anchor = 0; ed->cursor = (int)strlen(buf); }
void ed_move(EditState* ed, const char* buf, int delta, int shift)
{
    int len = (int)strlen(buf);
    ed->cursor += delta;
    if (ed->cursor < 0) ed->cursor = 0;
    if (ed->cursor > len) ed->cursor = len;
    if (!shift) ed->anchor = ed->cursor;
}
void ed_home(EditState* ed, int shift)
{ ed->cursor = 0; if (!shift) ed->anchor = 0; }
void ed_end(EditState* ed, const char* buf, int shift)
{ ed->cursor = (int)strlen(buf); if (!shift) ed->anchor = ed->cursor; }
void ed_delete_sel(EditState* ed, char* buf)
{
    int a = ed->cursor < ed->anchor ? ed->cursor : ed->anchor;
    int b = ed->cursor < ed->anchor ? ed->anchor : ed->cursor;
    if (a == b) return;
    int len = (int)strlen(buf);
    memmove(buf+a, buf+b, (size_t)(len-b+1));
    ed->cursor = ed->anchor = a;
}
void ed_backspace(EditState* ed, char* buf)
{
    if (ed->cursor != ed->anchor) { ed_delete_sel(ed, buf); return; }
    if (ed->cursor <= 0) return;
    int len = (int)strlen(buf);
    memmove(buf+ed->cursor-1, buf+ed->cursor, (size_t)(len-ed->cursor+1));
    ed->cursor--; ed->anchor = ed->cursor;
}
void ed_delete_fwd(EditState* ed, char* buf)
{
    if (ed->cursor != ed->anchor) { ed_delete_sel(ed, buf); return; }
    int len = (int)strlen(buf);
    if (ed->cursor >= len) return;
    memmove(buf+ed->cursor, buf+ed->cursor+1, (size_t)(len-ed->cursor));
}
void ed_insert(EditState* ed, char* buf, size_t cap, const char* text)
{
    if (!text || !text[0]) return;
    if (ed->cursor != ed->anchor) ed_delete_sel(ed, buf);
    for (size_t i = 0; text[i]; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c < 32 && c != '\n') continue;
        int len = (int)strlen(buf);
        if (len+1 >= (int)cap) break;
        memmove(buf+ed->cursor+1, buf+ed->cursor, (size_t)(len-ed->cursor+1));
        buf[ed->cursor++] = (char)c; ed->anchor = ed->cursor;
    }
}
void ed_copy(const EditState* ed, const char* buf)
{
    int a = ed->cursor < ed->anchor ? ed->cursor : ed->anchor;
    int b = ed->cursor < ed->anchor ? ed->anchor : ed->cursor;
    if (a == b) return;
    int len = b - a;
    char* tmp = (char*)malloc((size_t)len+1);
    if (!tmp) return;
    memcpy(tmp, buf+a, (size_t)len); tmp[len]='\0';
    clipboard_set_text(tmp); free(tmp);
}
void ed_cut(EditState* ed, char* buf) { ed_copy(ed, buf); ed_delete_sel(ed, buf); }
void ed_paste(EditState* ed, char* buf, size_t cap)
{
    char* clip = clipboard_get_text();
    if (!clip) return;
    ed_insert(ed, buf, cap, clip);
    free(clip);
}
