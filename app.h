/* Shared types, constants and theme for Memebooru SDL 1.2 edition. */
#ifndef APP_H
#define APP_H

#include <SDL/SDL.h>
#include <stddef.h>
#include <ctype.h>

/* ---- sizes ---------------------------------------------------------------- */
#if defined(__m68k__)
#  define ITEM_MAX      64
#  define TAGS_LEN     256
#elif defined(__MACOS__)   /* PPC classic */
#  define ITEM_MAX     256
#  define TAGS_LEN     512
#else
#  define ITEM_MAX    1024
#  define TAGS_LEN    2048
#endif
#define PATH_MAX_LEN  512
#define SEARCH_LEN    256
#define STATUS_LEN    512
#define SUG_MAX        32

#if defined(BUILD_FOR_11_0) || defined(BUILD_FOR_10_7)
#define WINDOW_W 1280
#define WINDOW_H 800
#elif defined(BUILD_FOR_10_4)
#define WINDOW_W 1024
#define WINDOW_H 768
#elif defined(__m68k__)
#define WINDOW_W 640
#define WINDOW_H 480
#else
#define WINDOW_W 800
#define WINDOW_H 600
#endif

static inline int ascii_strcasecmp(const char* a, const char* b)
{
    while (*a && *b) {
        int da = tolower((unsigned char)*a);
        int db = tolower((unsigned char)*b);
        if (da != db) return da - db;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static inline int ascii_strncasecmp(const char* a, const char* b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int da = tolower((unsigned char)a[i]);
        int db = tolower((unsigned char)b[i]);
        if (da != db) return da - db;
        if (a[i] == '\0' || b[i] == '\0') return da - db;
    }
    return 0;
}

/* ---- data model ---------------------------------------------------------- */
typedef struct {
    char id[64];
    char filename[PATH_MAX_LEN]; /* leaf name inside data dir */
    char tags[TAGS_LEN];         /* space-separated tags      */
    char meta[TAGS_LEN];         /* space-separated meta tags */
    char cover[PATH_MAX_LEN];    /* custom gallery cover leaf */
} Item;

typedef struct {
    Item items[ITEM_MAX];
    int  count;
} ItemList;

typedef struct {
    int cursor; /* byte offset of caret  */
    int anchor; /* byte offset of selection anchor (== cursor = no selection) */
} EditState;

typedef struct {
    char tok[128];
    int  count;
    int  kind; /* 0=unknown 1=tag 2=meta */
} TagSuggestion;

/* ---- view state ---------------------------------------------------------- */
typedef enum { VIEW_LIST, VIEW_DETAIL, VIEW_ADD } ViewMode;

typedef enum {
    FOCUS_SEARCH = 0,
    FOCUS_SEARCH_CONTENT,
    FOCUS_TAGS,
    FOCUS_PATH,
    FOCUS_COVER,
    FOCUS_TXTED,
    FOCUS_NONE
} FocusKind;

/* ---- application state --------------------------------------------------- */
typedef struct AppState {
    ItemList list;
    ViewMode view;
    FocusKind focus;

    int  selected;       /* index into list for detail view */
    int  list_scroll;    /* pixel offset for gallery scroll */
    int  gallery_cursor; /* keyboard cursor in filtered list (-1 = none) */

    char search[SEARCH_LEN];         /* tags filter */
    char search_content[SEARCH_LEN]; /* filename/body filter */
    char status_line[STATUS_LEN];

    EditState ed_search;
    EditState ed_search_content;
    EditState ed_tags;
    EditState ed_path;
    EditState ed_cover;

    /* right-click context menu */
    int       ctx_open;
    int       ctx_mx, ctx_my;
    FocusKind ctx_focus; /* which field was right-clicked */

    /* tag autocomplete suggestions */
    int          sug_open;
    int          sug_count;
    int          sug_active; /* keyboard-highlighted row */
    TagSuggestion sug[SUG_MAX];
    int          sug_x, sug_y, sug_w; /* dropdown position */

    /* add / edit form */
    int  edit_index;    /* -1 = adding new */
    char form_tags[TAGS_LEN];
    char form_path[PATH_MAX_LEN];
    char form_cover[PATH_MAX_LEN];
    char edit_original_filename[PATH_MAX_LEN];
    int  show_cover_options; /* "more options" toggle */

    /* multi-line text editor (shown in detail view for text files) */
    int  txt_cursor_line;
    int  txt_cursor_col;
    int  txt_scroll; /* first visible line */

    /* LaTeX compile */
    char compiled_pdf[PATH_MAX_LEN]; /* path of compiled .pdf */
    int  compiled_for;               /* list index when compiled */

    int active_button;

    Uint32 last_click_time;
    int    last_click_target;
} AppState;

/* ---- globals defined in main.c, used by views.c/textedit.c --------------- */
extern SDL_Surface* screen;
#ifdef USE_SDL_TTF
#include "SDL_ttf.h"
extern TTF_Font* g_ttf_normal;
extern TTF_Font* g_ttf_large;
extern int g_ttf_char_w;
extern int g_ttf_char_w2;
#endif

/* ---- drawing primitives (defined in main.c) ------------------------------ */
void fill_rect(int x, int y, int w, int h, Uint32 color);
void draw_text(int x, int y, const char* text, Uint32 color, int scale);
void draw_text_fit(int x, int y, const char* value, Uint32 color, int scale, int max_px);
void draw_button(int x, int y, int w, int h, const char* label, Uint32 bg, Uint32 fg, int pressed);
int  point_in_rect(int x, int y, int rx, int ry, int rw, int rh);

/* ---- image helpers (defined in main.c) ------------------------------------ */
SDL_Surface* load_image_surface(const char* path, int max_w, int max_h);
SDL_Surface* scale_surface(SDL_Surface* src, int max_w, int max_h);

/* ---- path utilities (defined in main.c) ---------------------------------- */
const char* path_ext(const char* path);
int  ext_equals(const char* ext, const char* needle);
int  is_image_path(const char* path);
int  is_text_path(const char* path);
int  is_pdf_path(const char* path);
int  is_tex_path(const char* path);
void get_ext_label(const char* path, char* out, size_t cap);
void get_doc_badge_colors(const char* path, Uint32* bg, Uint32* fg);
void safe_copy(char* dst, size_t cap, const char* src);

/* ---- clipboard (defined in main.c) --------------------------------------- */
int   clipboard_set_text(const char* text);
char* clipboard_get_text(void); /* caller must free() */

/* ---- Monokai theme colors ------------------------------------------------ */
/* Use these macros inside any function that has access to screen->format.    */
#define MK_COL(r,g,b)  SDL_MapRGB(screen->format, (r), (g), (b))

#define COL_BG         MK_COL( 39,  40,  34)
#define COL_PANEL      MK_COL( 39,  40,  34)
#define COL_CARD       MK_COL( 50,  50,  45)
#define COL_CARD_HI    MK_COL( 60,  61,  55)
#define COL_BORDER     MK_COL(117, 113,  94)
#define COL_BORDER_HI  MK_COL(253, 151,  31)
#define COL_TEXT       MK_COL(248, 248, 242)
#define COL_TEXT_DIM   MK_COL(117, 113,  94)
#define COL_ACCENT     MK_COL(249,  38, 114)
#define COL_SHADOW     MK_COL(  0,   0,   0)
#define COL_TAG        MK_COL(230, 219, 116)
#define COL_META_C     MK_COL(166, 226,  46)
#define COL_TAG_BAD    MK_COL(249,  38, 114)
#define COL_SELECT     MK_COL( 82, 174, 192)

#endif /* APP_H */
