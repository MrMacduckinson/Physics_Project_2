/* Wrapper around SDL3's native async file-open dialog. */
#ifndef FILEDIALOG_H
#define FILEDIALOG_H

#include <SDL3/SDL.h>

void filedialog_open(SDL_Window* win);
/* Returns a freshly-picked path once (then NULL), or NULL if none pending. */
const char* filedialog_take_result(void);

#endif /* FILEDIALOG_H */
