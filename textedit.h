/* Multi-line text editor used in the detail view for text files. */
#ifndef TEXTEDIT_H
#define TEXTEDIT_H

#include "app.h"

/* Load a file into the editor buffer.  No-op if same path already loaded. */
void txted_load(AppState* app, const char* full_path);

/* Save if modified.  Returns 1 on success (or not modified). */
int  txted_save(void);

int  txted_is_modified(void);

/* Handle a key-down event; returns 1 if consumed. */
int  txted_key(AppState* app, int sym, int mod_shift, int mod_cmd);

/* Insert a UTF-8 character string at the cursor. */
void txted_input(AppState* app, const char* text);

/* Render the editor into the given screen rect. Returns visible line count. */
int  txted_draw(AppState* app, int x, int y, int w, int h,
                Uint32 fg, Uint32 bg, Uint32 line_fg, Uint32 cursor_col);

#endif /* TEXTEDIT_H */
