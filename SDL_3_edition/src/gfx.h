/* SDL3 + SDL_ttf rendering helpers: vector text, primitives, image textures. */
#ifndef GFX_H
#define GFX_H

#include <SDL3/SDL.h>
#include "app.h"

bool gfx_init(SDL_Renderer* r, const char* font_path);
void gfx_shutdown(void);
/* Set physical-pixel DPI scale (e.g. 2.0 on Retina).
 * Fonts will be opened at size*scale pts so they render crisp at physical res. */
void gfx_set_dpi(float scale);

/* primitives */
void gfx_clear(Color c);
void gfx_fill_rect(float x, float y, float w, float h, Color c);
void gfx_fill_round(float x, float y, float w, float h, float radius, Color c);
void gfx_stroke_round(float x, float y, float w, float h, float radius, float t, Color c);
void gfx_card(float x, float y, float w, float h, Color fill, Color border, bool shadow);
void gfx_push_clip(int x, int y, int w, int h);
void gfx_pop_clip(void);

/* text (size = pixel height target). Returns advance width drawn. */
int  gfx_text(float x, float y, const char* s, int size, Color c);
void gfx_text_clip(float x, float y, float maxw, const char* s, int size, Color c);
void gfx_text_size(const char* s, int size, int* w, int* h);
int  gfx_text_h(int size);
/* pixel width of the first n bytes of s (caret placement) */
int  gfx_prefix_w(const char* s, int n, int size);
/* byte index in s nearest to local pixel x (click -> caret) */
int  gfx_index_at_x(const char* s, int size, float px);

/* draw a texture scaled into a rect */
void gfx_blit(SDL_Texture* t, float x, float y, float w, float h);

/* images: cached texture by path (RGBA via stb_image). NULL on failure. */
SDL_Texture* gfx_image(const char* path, int* w, int* h);
void gfx_image_cache_drop(const char* path);

/* raw RGB(A) pixels -> owned texture (caller frees with SDL_DestroyTexture) */
SDL_Texture* gfx_texture_from_rgb(const unsigned char* rgb, int w, int h, int channels);

#endif /* GFX_H */
