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
#define SEARCH_LEN      256
#define STATUS_LEN      512

/* ---- Data model -------------------------------------------------------- */
typedef struct {
    char id[ID_LEN];
    char filename[PATH_LEN];   /* file copied into the data dir */
    char tags[TAGS_LEN];
} Item;

typedef struct {
    Item items[ITEM_MAX];
    int  count;
} ItemList;

typedef enum { VIEW_LIST, VIEW_DETAIL, VIEW_ADD } ViewMode;
typedef enum { FOCUS_SEARCH, FOCUS_SEARCH_CONTENT, FOCUS_TAGS, FOCUS_PATH, FOCUS_NONE } Focus;

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

    /* add / edit form */
    int  edit_index;     /* -1 = adding new */
    char form_tags[TAGS_LEN];
    char form_path[PATH_LEN];

    int  pdf_page;       /* current page in detail PDF view  */
    int  pdf_page_count;

    int  hot_button;     /* button hovered  */
    int  active_button;  /* button pressed (mouse down) */

    double last_click_time;
    int    last_click_target;

    bool running;
} AppState;

/* ---- Theme (warm dark, amber/sepia) ------------------------------------ */
typedef struct { Uint8 r, g, b, a; } Color;

static const Color COL_BG        = {  20,  19,  17, 255 };
static const Color COL_PANEL     = {  28,  26,  23, 255 };
static const Color COL_CARD      = {  34,  31,  27, 255 };
static const Color COL_CARD_Hi   = {  44,  40,  34, 255 };
static const Color COL_BORDER    = {  60,  54,  44, 255 };
static const Color COL_BORDER_Hi = { 215, 145,  30, 255 };
static const Color COL_TEXT      = { 234, 226, 211, 255 };
static const Color COL_TEXT_DIM  = { 156, 146, 130, 255 };
static const Color COL_ACCENT    = { 226, 154,  40, 255 };
static const Color COL_ACCENT_Hi = { 245, 184,  70, 255 };
static const Color COL_SELECT    = {  74,  98, 140, 180 }; /* text selection highlight */
static const Color COL_SHADOW    = {  10,   9,   8, 255 };

static const Color COL_PILL_BG   = {  70,  50,  12, 255 };
static const Color COL_PILL_BR   = { 240, 165,  20, 255 };
static const Color COL_PILL_FG   = { 255, 210,  80, 255 };

/* file-type badge colors (bg, fg) chosen per extension at runtime */

#endif /* APP_H */
