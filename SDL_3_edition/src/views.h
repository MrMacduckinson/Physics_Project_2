/* View rendering + hit-testing for list / detail / add screens. */
#ifndef VIEWS_H
#define VIEWS_H

#include "app.h"

typedef enum {
    HIT_NONE, HIT_SEARCH, HIT_SEARCH_CONTENT, HIT_SEARCH_SUG, HIT_ADD, HIT_ITEM,
    HIT_D_BACK, HIT_D_PREV, HIT_D_NEXT, HIT_D_EDIT, HIT_D_DELETE,
    HIT_D_PGPREV, HIT_D_PGNEXT,
    HIT_D_TAG,    /* click a tag in detail view strip; index=byte offset in tags string */
    HIT_A_TAGS, HIT_A_PATH, HIT_A_BROWSE, HIT_A_SAVE, HIT_A_CANCEL,
    HIT_A_COVER, HIT_A_COVER_BROWSE, HIT_A_MORE_OPTIONS,
    HIT_D_CURR,  /* click in current-file preview panel (text edit entry) */
    /* right-click context menu (base items + optional tag actions) */
    HIT_CTX_SELECT_ALL, HIT_CTX_COPY, HIT_CTX_CUT, HIT_CTX_PASTE,
    HIT_CTX_ADD_TAG, HIT_CTX_ADD_META,
    HIT_CTX_DISMISS
} HitTarget;

typedef struct { HitTarget target; int index; } Hit;
typedef struct { float x, y, w, h; } FRectL;

/* Call once per frame before views_render with the current DPI scale. */
void views_set_dpi(float d);

void views_render(AppState* app, int w, int h);
Hit  views_hittest(AppState* app, int w, int h, float mx, float my);

/* Filtered item indices (those matching app->search). Returns count. */
int  views_filtered(AppState* app, int* out, int cap);

/* Geometry of a plain text field (search/path) for click-to-caret math.
 * Returns false if target isn't a plain field. */
bool views_field_geom(AppState* app, int w, int h, HitTarget t,
                      FRectL* field, float* text_x, int* font_size);

bool views_tag_token_at(AppState* app, int w, int h, float mx, float my,
                        char* out, size_t cap);

void views_clamp_scroll(AppState* app, int w, int h);
int  views_gallery_cols(AppState* app, int w, int h);
void views_scroll_to_cursor(AppState* app, int w, int h);

/* Multi-line text editor in the detail preview. */
void views_txted_load(AppState* app, const char* full_path);
bool views_txted_key(AppState* app, SDL_Keycode key, SDL_Keymod mod);
void views_txted_input(AppState* app, const char* text);
bool views_txted_click(AppState* app, int w, int h, float mx, float my);
bool views_txted_save(AppState* app);
bool views_txted_is_modified(void);

#endif /* VIEWS_H */
