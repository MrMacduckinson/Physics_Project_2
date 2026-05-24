/* View rendering + hit-testing for list / detail / add screens. */
#ifndef VIEWS_H
#define VIEWS_H

#include "app.h"

typedef enum {
    HIT_NONE, HIT_SEARCH, HIT_SEARCH_CONTENT, HIT_ADD, HIT_ITEM,
    HIT_D_BACK, HIT_D_PREV, HIT_D_NEXT, HIT_D_EDIT, HIT_D_DELETE,
    HIT_D_PGPREV, HIT_D_PGNEXT,
    HIT_A_TAGS, HIT_A_PATH, HIT_A_BROWSE, HIT_A_SAVE, HIT_A_CANCEL,
    /* right-click context menu (4 items + dismiss) */
    HIT_CTX_SELECT_ALL, HIT_CTX_COPY, HIT_CTX_CUT, HIT_CTX_PASTE,
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

void views_clamp_scroll(AppState* app, int w, int h);

#endif /* VIEWS_H */
