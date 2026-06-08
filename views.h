/* View rendering, layout, hit-testing, gallery helpers. */
#ifndef VIEWS_H
#define VIEWS_H

#include "app.h"

/* ---- button / area IDs returned by views_hittest ------------------------- */
typedef enum {
    HIT_NONE = 0,
    /* list view */
    HIT_SEARCH,
    HIT_SEARCH_CONTENT,
    HIT_ITEM,           /* index returned in views_hittest */
    HIT_LIST_ADD,
    /* detail view */
    HIT_D_BACK,
    HIT_D_PREV,         /* click left panel → go to prev item */
    HIT_D_NEXT,         /* click right panel → go to next item */
    HIT_D_EDIT,
    HIT_D_DELETE,
    HIT_D_CENTER,       /* click center panel */
    HIT_D_TAG,          /* click a tag in detail view strip; index=byte offset in tags string */
    /* add / edit form */
    HIT_A_TAGS,
    HIT_A_PATH,
    HIT_A_BROWSE,
    HIT_A_COVER,
    HIT_A_COVER_BROWSE,
    HIT_A_MORE_OPTIONS,
    HIT_A_SAVE,
    HIT_A_CANCEL,
    /* context menu */
    HIT_CTX_SELECT_ALL,
    HIT_CTX_COPY,
    HIT_CTX_CUT,
    HIT_CTX_PASTE,
    HIT_CTX_ADD_TAG,    /* add right-clicked token to taglist */
    HIT_CTX_ADD_META,   /* add right-clicked token to metalist */
    HIT_CTX_DISMISS
} HitTarget;

typedef struct { HitTarget target; int index; } Hit;

/* ---- main API ------------------------------------------------------------ */
void views_render(AppState* app);
Hit  views_hittest(AppState* app, int mx, int my);

void views_scroll_to_cursor(AppState* app);
int  views_gallery_cols(AppState* app);

/* Compute tag suggestions for the current tags-field prefix. */
void views_update_suggestions(AppState* app);

/* Text-field editing helpers (used from main.c event loop) */
void ed_clamp(EditState* ed, const char* buf);
void ed_select_all(EditState* ed, const char* buf);
void ed_move(EditState* ed, const char* buf, int delta, int shift);
void ed_home(EditState* ed, int shift);
void ed_end(EditState* ed, const char* buf, int shift);
void ed_delete_sel(EditState* ed, char* buf);
void ed_backspace(EditState* ed, char* buf);
void ed_delete_fwd(EditState* ed, char* buf);
void ed_insert(EditState* ed, char* buf, size_t cap, const char* text);
void ed_copy(const EditState* ed, const char* buf);
void ed_cut(EditState* ed, char* buf);
void ed_paste(EditState* ed, char* buf, size_t cap);

/* Clear all image caches (call after add/delete/edit). */
void views_clear_caches(void);

#endif /* VIEWS_H */
