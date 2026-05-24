/* Native-feeling single-line text editing over a char buffer + EditState. */
#ifndef TEXTEDIT_H
#define TEXTEDIT_H

#include <SDL3/SDL.h>
#include "app.h"

void te_reset(EditState* ed, const char* buf);           /* caret to end, no sel */
bool te_has_sel(const EditState* ed);
void te_sel_range(const EditState* ed, int* lo, int* hi);
void te_select_all(EditState* ed, const char* buf);
void te_set_caret(EditState* ed, int pos, bool extend);

/* Insert UTF-8 text, replacing any selection. Sanitizes control chars. */
void te_insert(char* buf, size_t cap, EditState* ed, const char* text);

/* Handle a key. Returns true if the key was consumed (an edit/nav action). */
bool te_key(char* buf, size_t cap, EditState* ed, SDL_Keycode key, SDL_Keymod mod);

/* Clipboard helpers (for context-menu cut/copy). */
void te_copy(const char* buf, const EditState* ed);
void te_cut(char* buf, EditState* ed);

#endif /* TEXTEDIT_H */
