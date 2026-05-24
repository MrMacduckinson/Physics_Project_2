/*
 * Physicsbooru / Memebooru - SDL3 edition
 * Entry point, window/renderer setup, event loop and interaction logic.
 */
#include <SDL3/SDL.h>
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
        case FOCUS_SEARCH: *cap = SEARCH_LEN; *ed = &a->ed_search; return a->search;
        case FOCUS_TAGS:   *cap = TAGS_LEN;   *ed = &a->ed_tags;   return a->form_tags;
        case FOCUS_PATH:   *cap = PATH_LEN;   *ed = &a->ed_path;   return a->form_path;
        default: *cap = 0; *ed = NULL; return NULL;
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
}

static void open_add(AppState* a, int edit_index)
{
    a->view = VIEW_ADD;
    a->edit_index = edit_index;
    if (edit_index >= 0) {
        SDL_strlcpy(a->form_tags, a->list.items[edit_index].tags, sizeof(a->form_tags));
        SDL_strlcpy(a->form_path, a->list.items[edit_index].filename, sizeof(a->form_path));
    } else {
        a->form_tags[0] = '\0';
        a->form_path[0] = '\0';
    }
    te_reset(&a->ed_tags, a->form_tags);
    te_reset(&a->ed_path, a->form_path);
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

    if (a->edit_index >= 0) {
        Item* it = &a->list.items[a->edit_index];
        /* if path points to an external file (has separator) and exists, re-import */
        bool external = (strchr(a->form_path, '/') || strchr(a->form_path, '\\')) && file_exists(a->form_path);
        if (external) {
            char newname[PATH_LEN];
            if (store_import_file(a->form_path, newname, sizeof(newname))) {
                store_delete_file(it->filename);
                SDL_strlcpy(it->filename, newname, sizeof(it->filename));
            }
        }
        SDL_strlcpy(it->tags, a->form_tags, sizeof(it->tags));
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
        SDL_strlcpy(it->tags, a->form_tags, sizeof(it->tags));
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
        case HIT_A_BROWSE: filedialog_open(g_win); break;
        case HIT_A_SAVE:   save_form(a); break;
        case HIT_A_CANCEL: set_focus(a, FOCUS_NONE); a->view = VIEW_LIST; break;
        default: break;
    }
}

/* ---- keyboard ---------------------------------------------------------- */
static void on_key(AppState* a, SDL_Keycode key, SDL_Keymod mod)
{
    size_t cap; EditState* ed; char* buf = focus_buf(a, &cap, &ed);

    /* field-specific control keys first */
    if (buf && ed) {
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
            if (a->focus == FOCUS_SEARCH) a->list_scroll = 0;
            return;
        }
        return; /* swallow other keys while editing */
    }

    /* no field focused */
    if (a->view == VIEW_DETAIL) {
        if (key == SDLK_ESCAPE) a->view = VIEW_LIST;
        else if (key == SDLK_LEFT)  detail_navigate(a, -1);
        else if (key == SDLK_RIGHT) detail_navigate(a, +1);
        else if (key == SDLK_DELETE || key == SDLK_BACKSPACE) delete_selected(a);
    } else if (a->view == VIEW_ADD) {
        if (key == SDLK_ESCAPE) a->view = VIEW_LIST;
    } else { /* list */
        if (key == SDLK_ESCAPE && a->search[0]) { a->search[0]='\0'; a->list_scroll=0; }
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
    set_status(&app, "Ready");

    store_init();
    store_load(&app.list);

    while (app.running) {
        int pw, ph;
        SDL_GetWindowSize(g_win, &pw, &ph);
        /* Use logical window size for layout; SDL_SetRenderLogicalPresentation
         * maps our logical coords to the physical pixel buffer each frame,
         * giving correct visual sizing while still rendering at full resolution. */
        SDL_SetRenderLogicalPresentation(g_ren, pw, ph,
                                         SDL_LOGICAL_PRESENTATION_STRETCH);
        float sx = 1.0f, sy = 1.0f; (void)sy;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_EVENT_QUIT: app.running = false; break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        float mx = e.button.x * sx, my = e.button.y * sy;
                        Hit hit = views_hittest(&app, pw, ph, mx, my);
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
                        } else if (hit.target == HIT_A_TAGS) {
                            set_focus(&app, FOCUS_TAGS);
                            te_reset(&app.ed_tags, app.form_tags);
                        } else if (hit.target == HIT_A_PATH) {
                            set_focus(&app, FOCUS_PATH);
                            if (dbl) te_select_all(&app.ed_path, app.form_path);
                            else     field_click_caret(&app, pw, ph, HIT_A_PATH, mx);
                        } else if (hit.target == HIT_NONE && app.focus != FOCUS_NONE) {
                            set_focus(&app, FOCUS_NONE);
                        }
                    }
                    break;

                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (e.button.button == SDL_BUTTON_LEFT) {
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
                    }
                    break;

                case SDL_EVENT_TEXT_INPUT: {
                    size_t cap; EditState* ed; char* buf = focus_buf(&app, &cap, &ed);
                    if (buf && ed) {
                        te_insert(buf, cap, ed, e.text.text);
                        if (app.focus == FOCUS_SEARCH) app.list_scroll = 0;
                    }
                    break;
                }

                case SDL_EVENT_KEY_DOWN:
                    on_key(&app, e.key.key, e.key.mod);
                    break;

                default: break;
            }
        }

        /* pick up async file-dialog result */
        const char* picked = filedialog_take_result();
        if (picked && app.view == VIEW_ADD) {
            SDL_strlcpy(app.form_path, picked, sizeof(app.form_path));
            te_reset(&app.ed_path, app.form_path);
            set_focus(&app, FOCUS_PATH);
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
