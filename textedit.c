/* Multi-line text editor for detail view (text / markdown / LaTeX files). */
#include "textedit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TXT_BUF_CAP   131072 /* 128 KB */
#define TXT_LINES_CAP   4000

static char g_txt_buf[TXT_BUF_CAP];
static char g_txt_path[PATH_MAX_LEN];
static int  g_txt_off[TXT_LINES_CAP];  /* byte offset of line start in buf */
static int  g_txt_len[TXT_LINES_CAP];  /* byte length of line (excl \r\n)  */
static int  g_txt_lines;
static int  g_txt_modified;

/* Rebuild the line-offset table after any edit. */
static void txted_rebuild(void)
{
    g_txt_lines = 0;
    int n = (int)strlen(g_txt_buf);
    int start = 0;
    for (int i = 0; i <= n && g_txt_lines < TXT_LINES_CAP - 1; i++) {
        if (i == n || g_txt_buf[i] == '\n') {
            int end = i;
            if (end > start && g_txt_buf[end-1] == '\r') end--;
            g_txt_off[g_txt_lines] = start;
            g_txt_len[g_txt_lines] = end - start;
            g_txt_lines++;
            start = i + 1;
        }
    }
    if (g_txt_lines == 0) { g_txt_off[0] = 0; g_txt_len[0] = 0; g_txt_lines = 1; }
}

void txted_load(AppState* app, const char* full_path)
{
    if (strcmp(g_txt_path, full_path) == 0) return;
    safe_copy(g_txt_path, sizeof(g_txt_path), full_path);
    g_txt_buf[0] = '\0';
    g_txt_modified = 0;

    FILE* f = fopen(full_path, "rb");
    if (f) {
        size_t n = fread(g_txt_buf, 1, TXT_BUF_CAP - 1, f);
        fclose(f);
        g_txt_buf[n] = '\0';
    }
    txted_rebuild();
    app->txt_cursor_line = 0;
    app->txt_cursor_col  = 0;
    app->txt_scroll      = 0;
}

int txted_save(void)
{
    if (!g_txt_path[0] || !g_txt_modified) return 1;
    FILE* f = fopen(g_txt_path, "wb");
    if (!f) return 0;
    fwrite(g_txt_buf, 1, strlen(g_txt_buf), f);
    fclose(f);
    g_txt_modified = 0;
    return 1;
}

int txted_is_modified(void) { return g_txt_modified; }

static void txted_autoscroll(AppState* app, int vis_lines)
{
    if (app->txt_cursor_line < app->txt_scroll)
        app->txt_scroll = app->txt_cursor_line;
    if (vis_lines > 0 && app->txt_cursor_line >= app->txt_scroll + vis_lines)
        app->txt_scroll = app->txt_cursor_line - vis_lines + 1;
    if (app->txt_scroll < 0) app->txt_scroll = 0;
}

int txted_key(AppState* app, int sym, int shift, int cmd)
{
    if (app->txt_cursor_line >= g_txt_lines)
        app->txt_cursor_line = g_txt_lines > 0 ? g_txt_lines - 1 : 0;
    int line = app->txt_cursor_line;
    int col  = app->txt_cursor_col;
    int llen = (line < g_txt_lines) ? g_txt_len[line] : 0;
    if (col > llen) col = llen;
    (void)shift; (void)cmd;

    /* SDL 1.2 key symbols */
    enum { K_LEFT=276, K_RIGHT=275, K_UP=273, K_DOWN=274,
           K_HOME=278, K_END=279, K_PGUP=280, K_PGDN=281,
           K_RETURN=13, K_BACKSPACE=8, K_DELETE=127 };

    if (sym == K_LEFT) {
        if (col > 0) col--; else if (line > 0) { line--; col = g_txt_len[line]; }
    } else if (sym == K_RIGHT) {
        if (col < g_txt_len[line]) col++;
        else if (line + 1 < g_txt_lines) { line++; col = 0; }
    } else if (sym == K_UP) {
        if (line > 0) { line--; if (col > g_txt_len[line]) col = g_txt_len[line]; }
    } else if (sym == K_DOWN) {
        if (line + 1 < g_txt_lines) { line++; if (col > g_txt_len[line]) col = g_txt_len[line]; }
    } else if (sym == K_HOME) {
        col = 0;
    } else if (sym == K_END) {
        col = g_txt_len[line];
    } else if (sym == K_PGUP) {
        line -= 20; if (line < 0) line = 0;
        if (col > g_txt_len[line]) col = g_txt_len[line];
    } else if (sym == K_PGDN) {
        line += 20; if (line >= g_txt_lines) line = g_txt_lines - 1;
        if (col > g_txt_len[line]) col = g_txt_len[line];
    } else if (sym == K_RETURN) {
        int byte = g_txt_off[line] + col;
        int n    = (int)strlen(g_txt_buf);
        if (n + 1 < TXT_BUF_CAP) {
            memmove(g_txt_buf + byte + 1, g_txt_buf + byte, (size_t)(n - byte + 1));
            g_txt_buf[byte] = '\n';
            g_txt_modified = 1;
            txted_rebuild();
            line++; col = 0;
        }
    } else if (sym == K_BACKSPACE) {
        int byte = g_txt_off[line] + col;
        if (byte > 0) {
            int n = (int)strlen(g_txt_buf);
            memmove(g_txt_buf + byte - 1, g_txt_buf + byte, (size_t)(n - byte + 1));
            g_txt_modified = 1;
            txted_rebuild();
            /* find new position */
            byte--;
            int li;
            for (li = 0; li < g_txt_lines; li++)
                if (li + 1 >= g_txt_lines || g_txt_off[li+1] > byte) break;
            line = li; col = byte - g_txt_off[li];
        }
    } else if (sym == K_DELETE) {
        int byte = g_txt_off[line] + col;
        int n    = (int)strlen(g_txt_buf);
        if (byte < n) {
            memmove(g_txt_buf + byte, g_txt_buf + byte + 1, (size_t)(n - byte));
            g_txt_modified = 1;
            txted_rebuild();
            if (col > g_txt_len[line]) col = g_txt_len[line];
        }
    } else {
        return 0; /* not consumed */
    }

    app->txt_cursor_line = line;
    app->txt_cursor_col  = col;
    txted_autoscroll(app, 20);
    return 1;
}

void txted_input(AppState* app, const char* text)
{
    if (!text || !text[0]) return;
    if (app->txt_cursor_line >= g_txt_lines) return;
    int byte  = g_txt_off[app->txt_cursor_line] + app->txt_cursor_col;
    int n     = (int)strlen(g_txt_buf);
    int tlen  = (int)strlen(text);
    if (n + tlen >= TXT_BUF_CAP - 1) return;
    memmove(g_txt_buf + byte + tlen, g_txt_buf + byte, (size_t)(n - byte + 1));
    memcpy(g_txt_buf  + byte, text, (size_t)tlen);
    g_txt_modified = 1;
    txted_rebuild();
    int new_byte = byte + tlen;
    int li;
    for (li = 0; li < g_txt_lines; li++)
        if (li + 1 >= g_txt_lines || g_txt_off[li+1] > new_byte) break;
    app->txt_cursor_line = li;
    app->txt_cursor_col  = new_byte - g_txt_off[li];
    txted_autoscroll(app, 20);
}

int txted_draw(AppState* app, int x, int y, int w, int h,
               Uint32 fg, Uint32 bg, Uint32 line_fg, Uint32 cursor_col)
{
    (void)bg; /* background already drawn by caller */
    if (!g_txt_lines) return 0;

    /* line height: 16px is a safe fixed value for 14pt TTF or CoreText */
    const int LINE_H = 16;
    int vis = h / LINE_H;
    if (vis < 1) vis = 1;

    /* clamp scroll */
    if (app->txt_scroll < 0) app->txt_scroll = 0;
    if (app->txt_scroll + vis > g_txt_lines) {
        app->txt_scroll = g_txt_lines - vis;
        if (app->txt_scroll < 0) app->txt_scroll = 0;
    }

    /* clip to the rect */
    SDL_Rect clip_r; clip_r.x = x; clip_r.y = y; clip_r.w = w; clip_r.h = h;
    SDL_SetClipRect(screen, &clip_r);

    int max_chars = (w - 8) / 8;
    if (max_chars < 4) max_chars = 4;
    if (max_chars > 1023) max_chars = 1023;

    /* Header: show filename */
    char hdr[PATH_MAX_LEN + 4];
    const char* _leaf = strrchr(g_txt_path, '/');
    snprintf(hdr, sizeof(hdr), "[ %s ]", _leaf ? _leaf + 1 : g_txt_path);
    draw_text_fit(x + 4, y, hdr, cursor_col, 1, w - 8);

    int ry = y + LINE_H;
    for (int li = app->txt_scroll; li < g_txt_lines && ry + LINE_H <= y + h; li++, ry += LINE_H) {
        /* Draw cursor line highlight */
        if (li == app->txt_cursor_line) {
            SDL_Rect hl; hl.x = x; hl.y = ry; hl.w = w; hl.h = LINE_H;
            SDL_FillRect(screen, &hl, SDL_MapRGB(screen->format, 55, 56, 49));
        }

        const char* lp = g_txt_buf + g_txt_off[li];
        int         ll = g_txt_len[li];
        char row[1024];
        int  rlen = (ll < max_chars) ? ll : max_chars;
        if  (rlen < 0) rlen = 0;
        if  (rlen > (int)sizeof(row) - 1) rlen = (int)sizeof(row) - 1;
        memcpy(row, lp, (size_t)rlen);
        row[rlen] = '\0';
        draw_text(x + 4, ry, row, line_fg, 1);

        /* Draw cursor */
        if (li == app->txt_cursor_line && (SDL_GetTicks() / 530) % 2 == 0) {
            int cx = app->txt_cursor_col;
            if (cx > max_chars) cx = max_chars;
            fill_rect(x + 4 + cx * 8, ry + 1, 2, LINE_H - 2, cursor_col);
        }
    }

    SDL_SetClipRect(screen, NULL);

    /* Modified indicator */
    if (g_txt_modified) {
        draw_text(x + w - 80, y, "[modified]", fg, 1);
    }

    return vis;
}
