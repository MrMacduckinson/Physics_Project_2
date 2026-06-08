/*
 * Physicsbooru / Memebooru - SDL3 edition
 * Entry point, window/renderer setup, event loop and interaction logic.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>   /* provides WinMain on Windows; no-op elsewhere */
#include <SDL3_ttf/SDL_ttf.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "app.h"
#include "gfx.h"
#include "views.h"
#include "store.h"
#include "textedit.h"
#include "util.h"
#include "filedialog.h"
#include "pdfdoc.h"

static SDL_Window*   g_win;
static SDL_Renderer* g_ren;

/* ---- font resolution --------------------------------------------------- */
static bool file_exists(const char* p)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(p, &info);
}
static bool resolve_font(char* out, size_t cap)
{
    const char* base = SDL_GetBasePath();
    const char* cands[] = {
        "Roboto-Regular.ttf",
        "assets/fonts/Roboto-Regular.ttf",
        "../Resources/Roboto-Regular.ttf",
    };
    size_t i;
    for (i = 0; i < SDL_arraysize(cands); i++) {
        if (base) SDL_snprintf(out, cap, "%s%s", base, cands[i]);
        else      SDL_strlcpy(out, cands[i], cap);
        if (file_exists(out)) return true;
    }
    /* dev fallback: source tree */
    SDL_snprintf(out, cap, "%sassets/fonts/Roboto-Regular.ttf", base ? base : "");
    return file_exists(out);
}

/* ---- focus / text input ------------------------------------------------ */
static char*      focus_buf(AppState* a, size_t* cap, EditState** ed)
{
    switch (a->focus) {
        case FOCUS_SEARCH:         *cap = SEARCH_LEN; *ed = &a->ed_search;         return a->search;
        case FOCUS_SEARCH_CONTENT: *cap = SEARCH_LEN; *ed = &a->ed_search_content; return a->search_content;
        case FOCUS_TAGS:           *cap = TAGS_LEN;   *ed = &a->ed_tags;           return a->form_tags;
        case FOCUS_PATH:           *cap = PATH_LEN;   *ed = &a->ed_path;           return a->form_path;
        case FOCUS_COVER:          *cap = PATH_LEN;   *ed = &a->ed_cover;          return a->form_cover;
        default: *cap = 0; *ed = NULL; return NULL;
    }
}

static bool buf_for_target(AppState* a, HitTarget t, char** buf, size_t* cap, EditState** ed)
{
    switch (t) {
        case HIT_SEARCH:         *cap = SEARCH_LEN; *ed = &a->ed_search;         return (*buf = a->search) != NULL;
        case HIT_SEARCH_CONTENT: *cap = SEARCH_LEN; *ed = &a->ed_search_content; return (*buf = a->search_content) != NULL;
        case HIT_A_TAGS:         *cap = TAGS_LEN;   *ed = &a->ed_tags;           return (*buf = a->form_tags) != NULL;
        case HIT_A_PATH:         *cap = PATH_LEN;   *ed = &a->ed_path;           return (*buf = a->form_path) != NULL;
        case HIT_A_COVER:        *cap = PATH_LEN;   *ed = &a->ed_cover;          return (*buf = a->form_cover) != NULL;
        default: break;
    }
    *buf = NULL; *cap = 0; *ed = NULL;
    return false;
}

static bool last_token(const char* text, char* out, size_t cap)
{
    size_t n = strlen(text);
    while (n > 0 && text[n-1] == ' ') n--;
    if (n == 0) return false;
    size_t start = n;
    while (start > 0 && text[start-1] != ' ') start--;
    size_t len = n - start;
    if (len >= cap) len = cap - 1;
    memcpy(out, text + start, len);
    out[len] = '\0';
    return out[0] != '\0';
}

static void open_text_context_menu(AppState* a, float x, float y, HitTarget target)
{
    a->ctx_open = true;
    a->ctx_x = x;
    a->ctx_y = y;
    a->ctx_target = (int)target;
    a->ctx_count = 4;
    a->ctx_offset = 0;
    a->ctx_can_tag = false;
    a->ctx_token[0] = '\0';
}

static void open_tag_context_menu(AppState* a, float x, float y, const char* token)
{
    a->ctx_open = true;
    a->ctx_x = x;
    a->ctx_y = y;
    a->ctx_target = HIT_A_TAGS;
    a->ctx_count = 2;
    a->ctx_offset = 4;
    a->ctx_can_tag = true;
    SDL_strlcpy(a->ctx_token, token, sizeof(a->ctx_token));
}

static void close_context_menu(AppState* a)
{
    a->ctx_open = false;
    a->ctx_count = 0;
    a->ctx_offset = 0;
    a->ctx_can_tag = false;
    a->ctx_token[0] = '\0';
}

static bool apply_suggestion(AppState* a)
{
    if (!a->sug_open || a->sug_count <= 0) return false;
    int idx = a->sug_index;
    if (idx < 0 || idx >= a->sug_count) idx = 0;
    const char* tok = a->sug[idx].tok;
    if (!tok || !tok[0]) return false;

    size_t n = strlen(a->search);
    while (n > 0 && a->search[n-1] == ' ') n--;
    size_t start = n;
    while (start > 0 && a->search[start-1] != ' ') start--;
    a->search[start] = '\0';
    if (a->search[0]) SDL_strlcat(a->search, " ", sizeof(a->search));
    SDL_strlcat(a->search, tok, sizeof(a->search));
    SDL_strlcat(a->search, " ", sizeof(a->search));
    te_reset(&a->ed_search, a->search);
    return true;
}

static void do_ctx_action(AppState* a, HitTarget action)
{
    char* buf = NULL; size_t cap = 0; EditState* ed = NULL;
    if (!buf_for_target(a, (HitTarget)a->ctx_target, &buf, &cap, &ed)) return;

    if (action == HIT_CTX_ADD_TAG || action == HIT_CTX_ADD_META) {
        if (a->ctx_token[0]) {
            TagKind kind = (action == HIT_CTX_ADD_META) ? TAG_KIND_META : TAG_KIND_TAG;
            if (store_add_tag_to_list(a->ctx_token, kind)) {
                SDL_snprintf(a->status, sizeof(a->status),
                             "Added %s to %s list", a->ctx_token,
                             kind == TAG_KIND_META ? "metatag" : "tag");
            } else {
                SDL_snprintf(a->status, sizeof(a->status),
                             "%s is already in the %s list", a->ctx_token,
                             kind == TAG_KIND_META ? "metatag" : "tag");
            }
        }
        return;
    }

    if (action == HIT_CTX_SELECT_ALL) te_select_all(ed, buf);
    else if (action == HIT_CTX_COPY)  te_copy(buf, ed);
    else if (action == HIT_CTX_CUT)   te_cut(buf, ed);
    else if (action == HIT_CTX_PASTE) {
        char* clip = SDL_GetClipboardText();
        if (clip) { te_insert(buf, cap, ed, clip); SDL_free(clip); }
    }
}

static void set_focus(AppState* a, Focus f)
{
    if (a->focus == f) return;
    a->focus = f;
    if (f == FOCUS_NONE) SDL_StopTextInput(g_win);
    else                 SDL_StartTextInput(g_win);
}

static void set_status(AppState* a, const char* s) { SDL_strlcpy(a->status, s, sizeof(a->status)); }

static void append_token(char* out, size_t cap, const char* tok)
{
    if (!tok || !tok[0]) return;
    if (out[0]) SDL_strlcat(out, " ", cap);
    SDL_strlcat(out, tok, cap);
}

static void merge_tag_fields(const char* tags, const char* meta, char* out, size_t cap)
{
    out[0] = '\0';
    if (tags && tags[0]) SDL_strlcpy(out, tags, cap);
    if (meta && meta[0]) append_token(out, cap, meta);
}

static bool split_tags(const char* in, char* tags, size_t tcap, char* meta, size_t mcap,
                       char* unknown, size_t ucap)
{
    tags[0] = '\0';
    meta[0] = '\0';
    unknown[0] = '\0';
    const char* p = in;
    while (*p) {
        while (*p == ' ') p++;
        const char* start = p;
        while (*p && *p != ' ') p++;
        if (p > start) {
            char tok[128];
            size_t len = (size_t)(p - start);
            if (len >= sizeof(tok)) len = sizeof(tok) - 1;
            memcpy(tok, start, len); tok[len] = '\0';
            TagKind kind = store_tag_kind(tok);
            if (kind == TAG_KIND_META) append_token(meta, mcap, tok);
            else if (kind == TAG_KIND_TAG) append_token(tags, tcap, tok);
            else {
                append_token(tags, tcap, tok);
                append_token(unknown, ucap, tok);
            }
        }
    }
    return unknown[0] != '\0';
}

/* ---- navigation helpers ------------------------------------------------ */
static int filtered_pos_of(AppState* a, int item_idx, int* fb, int n)
{
    int i; for (i = 0; i < n; i++) if (fb[i] == item_idx) return i; return -1;
}

static void detail_navigate(AppState* a, int dir)
{
    int fb[ITEM_MAX];
    int n = views_filtered(a, fb, ITEM_MAX);
    int pos = filtered_pos_of(a, a->selected, fb, n);
    if (pos < 0) return;
    pos += dir;
    if (pos < 0 || pos >= n) return;
    a->selected = fb[pos];
    a->pdf_page = 0;
    a->txt_scroll = 0;
    a->txt_cursor_line = 0;
    a->txt_cursor_col  = 0;
    if (a->focus == FOCUS_TXTED) set_focus(a, FOCUS_NONE);
}

static void open_add(AppState* a, int edit_index)
{
    a->view = VIEW_ADD;
    a->edit_index = edit_index;
    a->show_cover_options = false;
    a->dialog_for_cover = false;
    if (edit_index >= 0) {
        merge_tag_fields(a->list.items[edit_index].tags,
                         a->list.items[edit_index].meta,
                         a->form_tags, sizeof(a->form_tags));
        SDL_strlcpy(a->form_path,  a->list.items[edit_index].filename, sizeof(a->form_path));
        SDL_strlcpy(a->form_cover, a->list.items[edit_index].cover,    sizeof(a->form_cover));
    } else {
        a->form_tags[0]  = '\0';
        a->form_path[0]  = '\0';
        a->form_cover[0] = '\0';
    }
    te_reset(&a->ed_tags,  a->form_tags);
    te_reset(&a->ed_path,  a->form_path);
    te_reset(&a->ed_cover, a->form_cover);
    set_focus(a, FOCUS_TAGS);
}

static void gen_id(char* out, size_t cap)
{
    static unsigned c = 0;
    SDL_snprintf(out, cap, "%llu%u", (unsigned long long)SDL_GetTicks(), c++);
}

static void save_form(AppState* a)
{
    if (!a->form_path[0]) { set_status(a, "Please choose a file."); return; }

    char tags[TAGS_LEN];
    char meta[TAGS_LEN];
    char unknown[TAGS_LEN];
    bool has_unknown = split_tags(a->form_tags, tags, sizeof(tags), meta, sizeof(meta),
                                  unknown, sizeof(unknown));

    if (a->edit_index >= 0) {
        Item* it = &a->list.items[a->edit_index];
        bool external = (strchr(a->form_path, '/') || strchr(a->form_path, '\\')) && file_exists(a->form_path);
        if (external) {
            char newname[PATH_LEN];
            if (store_import_file(a->form_path, newname, sizeof(newname))) {
                store_delete_file(it->filename);
                SDL_strlcpy(it->filename, newname, sizeof(it->filename));
            }
        }
        SDL_strlcpy(it->tags, tags, sizeof(it->tags));
        SDL_strlcpy(it->meta, meta, sizeof(it->meta));
        /* cover */
        bool ext_cover = a->form_cover[0] && (strchr(a->form_cover,'/') || strchr(a->form_cover,'\\'))
                         && file_exists(a->form_cover);
        if (ext_cover) {
            char covname[PATH_LEN];
            if (store_import_file(a->form_cover, covname, sizeof(covname))) {
                if (it->cover[0]) store_delete_file(it->cover);
                SDL_strlcpy(it->cover, covname, sizeof(it->cover));
            }
        } else if (!a->form_cover[0]) {
            it->cover[0] = '\0';
        }
        if (has_unknown)
            SDL_snprintf(a->status, sizeof(a->status), "Saved. Unknown tags: %s", unknown);
        else
            set_status(a, "Saved.");
    } else {
        if (a->list.count >= ITEM_MAX) { set_status(a, "Archive is full."); return; }
        char newname[PATH_LEN];
        if (!store_import_file(a->form_path, newname, sizeof(newname))) {
            set_status(a, "Could not import that file."); return;
        }
        Item* it = &a->list.items[a->list.count++];
        gen_id(it->id, sizeof(it->id));
        SDL_strlcpy(it->filename, newname, sizeof(it->filename));
        SDL_strlcpy(it->tags, tags, sizeof(it->tags));
        SDL_strlcpy(it->meta, meta, sizeof(it->meta));
        it->cover[0] = '\0';
        if (a->form_cover[0] && file_exists(a->form_cover)) {
            char covname[PATH_LEN];
            if (store_import_file(a->form_cover, covname, sizeof(covname)))
                SDL_strlcpy(it->cover, covname, sizeof(it->cover));
        }
        if (has_unknown)
            SDL_snprintf(a->status, sizeof(a->status), "Added. Unknown tags: %s", unknown);
        else
            set_status(a, "Added.");
    }
    store_save(&a->list);
    store_invalidate();
    set_focus(a, FOCUS_NONE);
    a->view = VIEW_LIST;
    a->list_scroll = 0;
}

static void delete_selected(AppState* a)
{
    if (a->selected < 0 || a->selected >= a->list.count) return;
    store_delete_file(a->list.items[a->selected].filename);
    int i;
    for (i = a->selected; i < a->list.count - 1; i++)
        a->list.items[i] = a->list.items[i + 1];
    a->list.count--;
    store_save(&a->list);
    store_invalidate();
    a->view = VIEW_LIST;
    set_status(a, "Deleted.");
}

/* ---- click-to-caret for plain fields ----------------------------------- */
static void field_click_caret(AppState* a, int w, int h, HitTarget t, float mx)
{
    FRectL fr; float tx; int fs;
    if (!views_field_geom(a, w, h, t, &fr, &tx, &fs)) return;
    size_t cap; EditState* ed; char* buf = focus_buf(a, &cap, &ed);
    if (!buf || !ed) return;
    float local = mx - tx + ed->scroll;
    int idx = gfx_index_at_x(buf, fs, local);
    te_set_caret(ed, idx, false);
}

/* ---- mouse actions ----------------------------------------------------- */
static void do_click(AppState* a, int w, int h, Hit hit)
{
    switch (hit.target) {
        case HIT_ADD:      open_add(a, -1); break;
        case HIT_ITEM:
            a->selected = hit.index; a->pdf_page = 0;
            a->view = VIEW_DETAIL; set_focus(a, FOCUS_NONE);
            break;
        case HIT_D_BACK:   a->view = VIEW_LIST; break;
        case HIT_D_PREV:   detail_navigate(a, -1); break;
        case HIT_D_NEXT:   detail_navigate(a, +1); break;
        case HIT_D_EDIT:   open_add(a, a->selected); break;
        case HIT_D_DELETE: delete_selected(a); break;
        case HIT_D_PGPREV: if (a->pdf_page > 0) a->pdf_page--; break;
        case HIT_D_PGNEXT: if (a->pdf_page + 1 < a->pdf_page_count) a->pdf_page++; break;
        case HIT_D_TAG: {
            if (a->selected < 0 || a->selected >= a->list.count) break;
            const char* src;
            int off = hit.index;
            if (off < 0) { src = a->list.items[a->selected].meta;  off = ~off; }
            else          { src = a->list.items[a->selected].tags; }
            const char* start = src + off;
            while (*start == ' ') start++;
            const char* end2 = start;
            while (*end2 && *end2 != ' ') end2++;
            size_t len = (size_t)(end2 - start);
            if (len > 0 && len < sizeof(a->search) - 1) {
                SDL_strlcpy(a->search, start, len + 1);
                a->view = VIEW_LIST;
                a->list_scroll = 0;
                a->gallery_cursor = -1;
                set_focus(a, FOCUS_SEARCH);
            }
            break;
        }
        case HIT_A_BROWSE:
            a->dialog_for_cover = false;
            filedialog_open(g_win);
            break;
        case HIT_A_COVER_BROWSE:
            a->dialog_for_cover = true;
            filedialog_open(g_win);
            break;
        case HIT_A_MORE_OPTIONS:
            a->show_cover_options = !a->show_cover_options;
            break;
        case HIT_A_SAVE:   save_form(a); break;
        case HIT_A_CANCEL: set_focus(a, FOCUS_NONE); a->view = VIEW_LIST; break;
        case HIT_D_CURR:
            /* enter text-editor mode when clicking on text file preview */
            if (a->selected >= 0 && a->selected < a->list.count) {
                const Item* cit = &a->list.items[a->selected];
                if (is_text_path(cit->filename) && !a->show_compiled) {
                    set_focus(a, FOCUS_TXTED);
                }
            }
            break;
        default: break;
    }
}

/* ---- keyboard ---------------------------------------------------------- */
static void on_key(AppState* a, SDL_Keycode key, SDL_Keymod mod, int pw, int ph)
{
    size_t cap; EditState* ed; char* buf = focus_buf(a, &cap, &ed);

    /* multi-line text editor */
    if (a->focus == FOCUS_TXTED) {
        bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
        if (ctrl && key == SDLK_S) {
            if (views_txted_save(a)) set_status(a, "Saved.");
            else                     set_status(a, "Save failed.");
            return;
        }
        if (key == SDLK_ESCAPE) { set_focus(a, FOCUS_NONE); return; }
        views_txted_key(a, key, mod);
        return;
    }

    /* field-specific control keys first */
    if (buf && ed) {
        if (a->focus == FOCUS_SEARCH && a->sug_open && a->sug_count > 0) {
            if (key == SDLK_DOWN) {
                a->sug_index = (a->sug_index + 1) % a->sug_count;
                return;
            }
            if (key == SDLK_UP) {
                a->sug_index = (a->sug_index - 1 + a->sug_count) % a->sug_count;
                return;
            }
            if (key == SDLK_TAB || key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                if (apply_suggestion(a)) return;
            }
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (a->view == VIEW_ADD) save_form(a);
            else set_focus(a, FOCUS_NONE);
            return;
        }
        if (key == SDLK_TAB && a->view == VIEW_ADD) {
            set_focus(a, a->focus == FOCUS_TAGS ? FOCUS_PATH : FOCUS_TAGS);
            return;
        }
        if (key == SDLK_ESCAPE) {
            if (a->view == VIEW_ADD) { set_focus(a, FOCUS_NONE); a->view = VIEW_LIST; }
            else set_focus(a, FOCUS_NONE);
            return;
        }
        if (te_key(buf, cap, ed, key, mod)) {
            if (a->focus == FOCUS_SEARCH) { a->list_scroll = 0; a->gallery_cursor = -1; }
            return;
        }
        return; /* swallow other keys while editing */
    }

    /* no field focused */
    if (a->view == VIEW_DETAIL) {
        if (key == SDLK_ESCAPE)                              a->view = VIEW_LIST;
        else if (key == SDLK_LEFT)                           detail_navigate(a, -1);
        else if (key == SDLK_RIGHT)                          detail_navigate(a, +1);
        else if (key == SDLK_DELETE || key == SDLK_BACKSPACE) delete_selected(a);
    } else if (a->view == VIEW_ADD) {
        if (key == SDLK_ESCAPE) a->view = VIEW_LIST;
    } else { /* list */
        if (key == SDLK_ESCAPE && a->search[0]) {
            a->search[0] = '\0'; a->list_scroll = 0; a->gallery_cursor = -1;
        } else {
            int fb[ITEM_MAX];
            int n = views_filtered(a, fb, ITEM_MAX);
            if (n > 0) {
                int old = a->gallery_cursor;
                int cols = views_gallery_cols(a, pw, ph);
                if (key == SDLK_DOWN) {
                    if (a->gallery_cursor < 0) a->gallery_cursor = 0;
                    else { int nxt = a->gallery_cursor + cols; a->gallery_cursor = nxt < n ? nxt : n-1; }
                } else if (key == SDLK_UP) {
                    if (a->gallery_cursor < 0) a->gallery_cursor = 0;
                    else { int prv = a->gallery_cursor - cols; a->gallery_cursor = prv >= 0 ? prv : 0; }
                } else if (key == SDLK_RIGHT || key == SDLK_TAB) {
                    if (a->gallery_cursor < 0) a->gallery_cursor = 0;
                    else if (a->gallery_cursor + 1 < n) a->gallery_cursor++;
                } else if (key == SDLK_LEFT) {
                    if (a->gallery_cursor <= 0) a->gallery_cursor = 0;
                    else a->gallery_cursor--;
                } else if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) &&
                           a->gallery_cursor >= 0 && a->gallery_cursor < n) {
                    a->selected = fb[a->gallery_cursor];
                    a->pdf_page = 0;
                    a->view = VIEW_DETAIL;
                    set_focus(a, FOCUS_NONE);
                }
                if (a->gallery_cursor != old && a->gallery_cursor >= 0)
                    views_scroll_to_cursor(a, pw, ph);
            }
        }
    }
}

/* ---- main -------------------------------------------------------------- */
int main(int argc, char* argv[])
{
    (void)argc; (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    g_win = SDL_CreateWindow(APP_TITLE, WINDOW_W, WINDOW_H,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!g_win) { SDL_Log("CreateWindow: %s", SDL_GetError()); return 1; }
    g_ren = SDL_CreateRenderer(g_win, NULL);
    if (!g_ren) { SDL_Log("CreateRenderer: %s", SDL_GetError()); return 1; }
    SDL_SetRenderVSync(g_ren, 1);

    char font_path[1024];
    if (!resolve_font(font_path, sizeof(font_path))) {
        SDL_Log("Could not find Roboto-Regular.ttf");
        return 1;
    }
    if (!gfx_init(g_ren, font_path)) { SDL_Log("gfx_init failed"); return 1; }

    static AppState app;
    memset(&app, 0, sizeof(app));
    app.view = VIEW_LIST;
    app.focus = FOCUS_NONE;
    app.selected = -1;
    app.edit_index = -1;
    app.running = true;
    app.ctx_target = HIT_NONE;
    app.ctx_count = 0;
    app.ctx_offset = 0;
    app.ctx_can_tag = false;
    app.ctx_token[0] = '\0';
    app.sug_open = false;
    app.sug_count = 0;
    app.sug_index = 0;
    app.sug_prefix[0] = '\0';
    app.compiled_pdf[0] = '\0';
    app.compiled_for = -1;
    app.show_compiled = false;
    app.form_cover[0] = '\0';
    app.show_cover_options = false;
    app.dialog_for_cover = false;
    app.txt_cursor_line = 0;
    app.txt_cursor_col  = 0;
    app.txt_scroll      = 0;
    app.gallery_cursor  = -1;
    set_status(&app, "Ready");

    store_init();
    store_load(&app.list);

    while (app.running) {
        int ww, wh, pw, ph;
        SDL_GetWindowSize(g_win, &ww, &wh);
        SDL_GetWindowSizeInPixels(g_win, &pw, &ph);
        if (pw <= 0 || ph <= 0) continue;
        float sx = (ww > 0) ? (float)pw / ww : 1.0f;
        float sy = (wh > 0) ? (float)ph / wh : 1.0f;
        float dpi = SDL_GetWindowDisplayScale(g_win);
        if (dpi <= 0.0f) dpi = (sx + sy) * 0.5f;
        gfx_set_dpi(dpi);
        views_set_dpi(dpi);
        /* Render/layout in physical pixels for crisp text on HiDPI displays. */
        SDL_SetRenderLogicalPresentation(g_ren, pw, ph,
                                         SDL_LOGICAL_PRESENTATION_STRETCH);

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_EVENT_QUIT: app.running = false; break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (e.button.button == SDL_BUTTON_RIGHT) {
                        float mx = e.button.x * sx, my = e.button.y * sy;
                        Hit hit = views_hittest(&app, pw, ph, mx, my);
                        if (hit.target == HIT_SEARCH) {
                            set_focus(&app, FOCUS_SEARCH);
                            open_text_context_menu(&app, mx, my, hit.target);
                        } else if (hit.target == HIT_SEARCH_CONTENT) {
                            set_focus(&app, FOCUS_SEARCH_CONTENT);
                            open_text_context_menu(&app, mx, my, hit.target);
                        } else if (hit.target == HIT_A_TAGS) {
                            char tok[128];
                            if (views_tag_token_at(&app, pw, ph, mx, my, tok, sizeof(tok)) &&
                                store_tag_kind(tok) == TAG_KIND_UNKNOWN) {
                                set_focus(&app, FOCUS_TAGS);
                                open_tag_context_menu(&app, mx, my, tok);
                            } else {
                                close_context_menu(&app);
                            }
                        } else if (hit.target == HIT_A_PATH) {
                            set_focus(&app, FOCUS_PATH);
                            open_text_context_menu(&app, mx, my, hit.target);
                        } else {
                            close_context_menu(&app);
                        }
                    } else if (e.button.button == SDL_BUTTON_LEFT) {
                        float mx = e.button.x * sx, my = e.button.y * sy;
                        Hit hit = views_hittest(&app, pw, ph, mx, my);
                        if (hit.target == HIT_SEARCH_SUG) {
                            app.sug_index = hit.index;
                            apply_suggestion(&app);
                            break;
                        }
                        if (app.ctx_open) {
                            if (hit.target == HIT_CTX_DISMISS) close_context_menu(&app);
                            else if (hit.target >= HIT_CTX_SELECT_ALL && hit.target <= HIT_CTX_ADD_META) {
                                do_ctx_action(&app, hit.target);
                                close_context_menu(&app);
                            }
                            break;
                        }
                        app.active_button = hit.target;
                        /* double-click detection (within 350 ms on same target) */
                        double now = SDL_GetTicks() / 1000.0;
                        bool dbl = (now - app.last_click_time < 0.35 &&
                                    app.last_click_target == (int)hit.target);
                        app.last_click_time  = now;
                        app.last_click_target = (int)hit.target;
                        if (hit.target == HIT_SEARCH) {
                            set_focus(&app, FOCUS_SEARCH);
                            if (dbl) te_select_all(&app.ed_search, app.search);
                            else     field_click_caret(&app, pw, ph, HIT_SEARCH, mx);
                        } else if (hit.target == HIT_SEARCH_CONTENT) {
                            set_focus(&app, FOCUS_SEARCH_CONTENT);
                            if (dbl) te_select_all(&app.ed_search_content, app.search_content);
                            else     field_click_caret(&app, pw, ph, HIT_SEARCH_CONTENT, mx);
                        } else if (hit.target == HIT_A_TAGS) {
                            set_focus(&app, FOCUS_TAGS);
                            te_reset(&app.ed_tags, app.form_tags);
                        } else if (hit.target == HIT_A_PATH) {
                            set_focus(&app, FOCUS_PATH);
                            if (dbl) te_select_all(&app.ed_path, app.form_path);
                            else     field_click_caret(&app, pw, ph, HIT_A_PATH, mx);
                        } else if (hit.target == HIT_A_COVER) {
                            set_focus(&app, FOCUS_COVER);
                            if (dbl) te_select_all(&app.ed_cover, app.form_cover);
                            else     field_click_caret(&app, pw, ph, HIT_A_COVER, mx);
                        } else if (hit.target == HIT_D_CURR) {
                            /* handled in do_click; also position caret */
                            views_txted_click(&app, pw, ph, mx, my);
                        } else if (hit.target == HIT_NONE && app.focus != FOCUS_NONE) {
                            set_focus(&app, FOCUS_NONE);
                        }
                    }
                    break;

                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        if (app.ctx_open) break;
                        float mx = e.button.x * sx, my = e.button.y * sy;
                        Hit hit = views_hittest(&app, pw, ph, mx, my);
                        if (hit.target == app.active_button && hit.target != HIT_NONE)
                            do_click(&app, pw, ph, hit);
                        app.active_button = HIT_NONE;
                    }
                    break;

                case SDL_EVENT_MOUSE_MOTION: {
                    float mx = e.motion.x * sx, my = e.motion.y * sy;
                    app.hot_button = views_hittest(&app, pw, ph, mx, my).target;
                    /* drag-to-select in single-line text fields */
                    if (e.motion.state & SDL_BUTTON_LMASK) {
                        HitTarget ft = HIT_NONE;
                        if      (app.focus == FOCUS_SEARCH) ft = HIT_SEARCH;
                        else if (app.focus == FOCUS_PATH)   ft = HIT_A_PATH;
                        if (ft != HIT_NONE) {
                            FRectL fr; float tx; int fs;
                            if (views_field_geom(&app, pw, ph, ft, &fr, &tx, &fs)) {
                                size_t cap; EditState* ed; char* buf = focus_buf(&app, &cap, &ed);
                                if (buf && ed) {
                                    float lx = mx - tx + (float)ed->scroll;
                                    if (lx < 0) lx = 0;
                                    te_set_caret(ed, gfx_index_at_x(buf, fs, lx), true);
                                }
                            }
                        }
                    }
                    break;
                }

                case SDL_EVENT_MOUSE_WHEEL:
                    if (app.view == VIEW_LIST) {
                        app.list_scroll -= (int)(e.wheel.y * 64);
                        views_clamp_scroll(&app, pw, ph);
                    } else if (app.view == VIEW_DETAIL) {
                        /* scroll text preview or PDF page */
                        if (app.pdf_page_count > 0) {
                            if (e.wheel.y < 0 && app.pdf_page + 1 < app.pdf_page_count) app.pdf_page++;
                            else if (e.wheel.y > 0 && app.pdf_page > 0) app.pdf_page--;
                        } else {
                            app.txt_scroll -= (int)e.wheel.y * 3;
                            if (app.txt_scroll < 0) app.txt_scroll = 0;
                        }
                    }
                    break;

                case SDL_EVENT_TEXT_INPUT: {
                    if (app.focus == FOCUS_TXTED) {
                        views_txted_input(&app, e.text.text);
                    } else {
                        size_t cap; EditState* ed; char* buf = focus_buf(&app, &cap, &ed);
                        if (buf && ed) {
                            te_insert(buf, cap, ed, e.text.text);
                            if (app.focus == FOCUS_SEARCH) { app.list_scroll = 0; app.gallery_cursor = -1; }
                        }
                    }
                    break;
                }

                case SDL_EVENT_DROP_FILE:
                    if (e.drop.data) {
                        if (is_supported_path(e.drop.data)) {
                            open_add(&app, -1);
                            SDL_strlcpy(app.form_path, e.drop.data, sizeof(app.form_path));
                            te_reset(&app.ed_path, app.form_path);
                            set_focus(&app, FOCUS_TAGS);
                        } else {
                            set_status(&app, "Unsupported file type.");
                        }
                        SDL_free((void*)e.drop.data);
                    }
                    break;

                case SDL_EVENT_KEY_DOWN:
                    on_key(&app, e.key.key, e.key.mod, pw, ph);
                    break;

                default: break;
            }
        }

        /* pick up async file-dialog result */
        const char* picked = filedialog_take_result();
        if (picked && app.view == VIEW_ADD) {
            if (app.dialog_for_cover) {
                SDL_strlcpy(app.form_cover, picked, sizeof(app.form_cover));
                te_reset(&app.ed_cover, app.form_cover);
                set_focus(&app, FOCUS_COVER);
            } else {
                SDL_strlcpy(app.form_path, picked, sizeof(app.form_path));
                te_reset(&app.ed_path, app.form_path);
                set_focus(&app, FOCUS_PATH);
            }
        }

        if (app.view == VIEW_LIST) views_clamp_scroll(&app, pw, ph);
        views_render(&app, pw, ph);
        SDL_RenderPresent(g_ren);
    }

    gfx_shutdown();
    pdf_shutdown();
    SDL_DestroyRenderer(g_ren);
    SDL_DestroyWindow(g_win);
    SDL_Quit();
    return 0;
}
