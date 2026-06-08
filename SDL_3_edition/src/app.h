/*
 * Physicsbooru / Memebooru - SDL3 edition
 * Shared types, constants and theme.
 */
#ifndef APP_H
#define APP_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>

#define APP_TITLE       "Physicsbooru"
#define WINDOW_W        1100
#define WINDOW_H        720

#define ITEM_MAX        1024
#define ID_LEN          64
#define PATH_LEN        1024
#define TAGS_LEN        2048
#define TAGLIST_LEN     4096
#define SEARCH_LEN      256
#define STATUS_LEN      512
#define SUG_MAX         32

typedef struct {
    char tok[128];
    int  count;
    int  kind;
} TagSuggestion;

/* ---- Data model -------------------------------------------------------- */
typedef struct {
    char id[ID_LEN];
    char filename[PATH_LEN];   /* file copied into the data dir */
    char tags[TAGS_LEN];
    char meta[TAGS_LEN];
    char cover[PATH_LEN];      /* custom gallery cover image (leaf name in data dir) */
} Item;

typedef struct {
    Item items[ITEM_MAX];
    int  count;
} ItemList;

typedef enum { VIEW_LIST, VIEW_DETAIL, VIEW_ADD } ViewMode;
typedef enum { FOCUS_SEARCH, FOCUS_SEARCH_CONTENT, FOCUS_TAGS, FOCUS_PATH, FOCUS_COVER, FOCUS_TXTED, FOCUS_NONE } Focus;

/* A reusable, native-feeling editable text field state. */
typedef struct {
    int cursor;   /* byte offset of caret  */
    int anchor;   /* byte offset of selection anchor (== cursor: no selection) */
    int scroll;   /* horizontal pixel scroll for single-line fields */
} EditState;

typedef struct {
    ItemList list;
    ViewMode view;
    Focus    focus;

    int  selected;       /* index into list for detail view */
    int  list_scroll;    /* pixel scroll offset of gallery  */
    int  gallery_cursor; /* keyboard-focus index in filtered list; -1 = none */
    char search[SEARCH_LEN];         /* tags-only filter */
    char search_content[SEARCH_LEN]; /* filename + body filter */
    char status[STATUS_LEN];

    EditState ed_search;
    EditState ed_search_content;
    EditState ed_tags;
    EditState ed_path;

    /* right-click context menu */
    bool  ctx_open;
    float ctx_x, ctx_y;
    int   ctx_target;
    int   ctx_count;
    int   ctx_offset;
    bool  ctx_can_tag;
    char  ctx_token[128];

    /* tag search suggestions */
    bool  sug_open;
    int   sug_count;
    int   sug_index;
    float sug_x, sug_y, sug_w, sug_row_h;
    char  sug_prefix[128];
    TagSuggestion sug[SUG_MAX];

    /* add / edit form */
    int  edit_index;     /* -1 = adding new */
    char form_tags[TAGS_LEN];
    char form_path[PATH_LEN];
    char form_cover[PATH_LEN];  /* custom cover for this item */
    EditState ed_cover;
    bool show_cover_options;    /* expand "more options" for image files */
    bool dialog_for_cover;      /* true when file dialog is selecting a cover */

    int  pdf_page;       /* current page in detail PDF view  */
    int  pdf_page_count;

    /* LaTeX compilation */
    char compiled_pdf[PATH_LEN]; /* path to compiled PDF from .tex file */
    int  compiled_for;           /* app->selected index when compile ran */
    bool show_compiled;          /* true: show compiled PDF, false: source */

    /* multi-line text editor (detail view, text files) */
    int  txt_cursor_line;
    int  txt_cursor_col;
    int  txt_scroll;        /* first visible line */

    int  hot_button;     /* button hovered  */
    int  active_button;  /* button pressed (mouse down) */

    double last_click_time;
    int    last_click_target;

    bool running;
} AppState;

/* ---- Theme (Monokai) ------------------------------------ */
typedef struct { Uint8 r, g, b, a; } Color;

static const Color COL_BG        = {  39,  40,  34, 255 };
static const Color COL_PANEL     = {  39,  40,  34, 255 };
static const Color COL_CARD      = {  50,  50,  45, 255 };
static const Color COL_CARD_Hi   = {  60,  61,  55, 255 };
static const Color COL_BORDER    = { 117, 113,  94, 255 };
static const Color COL_BORDER_Hi = { 253, 151,  31, 255 };
static const Color COL_TEXT      = { 248, 248, 242, 255 };
static const Color COL_TEXT_DIM  = { 117, 113,  94, 255 };
static const Color COL_ACCENT    = { 249,  38, 114, 255 };
static const Color COL_ACCENT_Hi = { 249,  38, 114, 255 };
static const Color COL_SELECT    = { 102, 217, 239, 180 }; /* text selection highlight */
static const Color COL_SHADOW    = {   0,   0,   0, 255 };

static const Color COL_PILL_BG   = {  39,  40,  34, 255 };
static const Color COL_PILL_BR   = { 230, 219, 116, 255 };
static const Color COL_PILL_FG   = { 230, 219, 116, 255 };
static const Color COL_TAG       = { 230, 219, 116, 255 };
static const Color COL_META      = { 166, 226,  46, 255 };
static const Color COL_TAG_BAD   = { 249,  38, 114, 255 };

/* file-type badge colors (bg, fg) chosen per extension at runtime */

#endif /* APP_H */
