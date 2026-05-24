#include "textedit.h"
#include <string.h>
#include <stdlib.h>

#if defined(__APPLE__)
  #define PRIMARY_MOD SDL_KMOD_GUI   /* Cmd */
  #define WORD_MOD    SDL_KMOD_ALT   /* Option */
#else
  #define PRIMARY_MOD SDL_KMOD_CTRL
  #define WORD_MOD    SDL_KMOD_CTRL
#endif

static int slen(const char* b) { return (int)strlen(b); }

static int prev_char(const char* b, int i)
{
    if (i <= 0) return 0;
    i--;
    while (i > 0 && ((unsigned char)b[i] & 0xC0) == 0x80) i--;
    return i;
}
static int next_char(const char* b, int i)
{
    int n = slen(b);
    if (i >= n) return n;
    i++;
    while (i < n && ((unsigned char)b[i] & 0xC0) == 0x80) i++;
    return i;
}
static bool is_word(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || (unsigned char)c >= 0x80;
}
static int prev_word(const char* b, int i)
{
    while (i > 0 && !is_word(b[prev_char(b, i)])) i = prev_char(b, i);
    while (i > 0 &&  is_word(b[prev_char(b, i)])) i = prev_char(b, i);
    return i;
}
static int next_word(const char* b, int i)
{
    int n = slen(b);
    while (i < n && !is_word(b[i])) i = next_char(b, i);
    while (i < n &&  is_word(b[i])) i = next_char(b, i);
    return i;
}

void te_reset(EditState* ed, const char* buf)
{
    ed->cursor = ed->anchor = slen(buf);
    ed->scroll = 0;
}
bool te_has_sel(const EditState* ed) { return ed->cursor != ed->anchor; }
void te_sel_range(const EditState* ed, int* lo, int* hi)
{
    if (ed->cursor < ed->anchor) { *lo = ed->cursor; *hi = ed->anchor; }
    else                         { *lo = ed->anchor; *hi = ed->cursor; }
}
void te_select_all(EditState* ed, const char* buf)
{
    ed->anchor = 0;
    ed->cursor = slen(buf);
}
void te_set_caret(EditState* ed, int pos, bool extend)
{
    ed->cursor = pos;
    if (!extend) ed->anchor = pos;
}

static void delete_sel(char* buf, EditState* ed)
{
    if (!te_has_sel(ed)) return;
    int lo, hi; te_sel_range(ed, &lo, &hi);
    memmove(buf + lo, buf + hi, slen(buf) - hi + 1);
    ed->cursor = ed->anchor = lo;
}

void te_insert(char* buf, size_t cap, EditState* ed, const char* text)
{
    delete_sel(buf, ed);
    char clean[1024];
    int n = 0;
    for (const char* p = text; *p && n < (int)sizeof(clean) - 1; p++)
        clean[n++] = ((unsigned char)*p < 0x20) ? ' ' : *p;
    clean[n] = '\0';

    int len = slen(buf);
    int room = (int)cap - 1 - len;
    if (room <= 0) return;
    if (n > room) { n = room; clean[n] = '\0'; }
    memmove(buf + ed->cursor + n, buf + ed->cursor, len - ed->cursor + 1);
    memcpy(buf + ed->cursor, clean, n);
    ed->cursor += n;
    ed->anchor = ed->cursor;
}

static void copy_sel(const char* buf, const EditState* ed)
{
    te_copy(buf, ed);
}

void te_copy(const char* buf, const EditState* ed)
{
    if (!te_has_sel(ed)) return;
    int lo, hi; te_sel_range(ed, &lo, &hi);
    char tmp[TAGS_LEN];
    int n = hi - lo;
    if (n > (int)sizeof(tmp) - 1) n = (int)sizeof(tmp) - 1;
    memcpy(tmp, buf + lo, n);
    tmp[n] = '\0';
    SDL_SetClipboardText(tmp);
}

void te_cut(char* buf, EditState* ed)
{
    te_copy(buf, ed);
    if (te_has_sel(ed)) {
        int lo, hi; te_sel_range(ed, &lo, &hi);
        memmove(buf + lo, buf + hi, slen(buf) - hi + 1);
        ed->cursor = ed->anchor = lo;
    }
}

bool te_key(char* buf, size_t cap, EditState* ed, SDL_Keycode key, SDL_Keymod mod)
{
    bool primary = (mod & PRIMARY_MOD) != 0;
    bool word    = (mod & WORD_MOD) != 0;
    bool shift   = (mod & SDL_KMOD_SHIFT) != 0;
    int len = slen(buf);

    if (primary) {
        switch (key) {
            case SDLK_A: te_select_all(ed, buf); return true;
            case SDLK_C: copy_sel(buf, ed); return true;
            case SDLK_X: copy_sel(buf, ed); delete_sel(buf, ed); return true;
            case SDLK_V: {
                char* clip = SDL_GetClipboardText();
                if (clip) { te_insert(buf, cap, ed, clip); SDL_free(clip); }
                return true;
            }
#if defined(__APPLE__)
            case SDLK_LEFT:  te_set_caret(ed, 0, shift);   return true;
            case SDLK_RIGHT: te_set_caret(ed, len, shift); return true;
#endif
            default: break;
        }
    }

    switch (key) {
        case SDLK_LEFT:
            if (te_has_sel(ed) && !shift) { int lo,hi; te_sel_range(ed,&lo,&hi); te_set_caret(ed, lo, false); }
            else te_set_caret(ed, word ? prev_word(buf, ed->cursor) : prev_char(buf, ed->cursor), shift);
            return true;
        case SDLK_RIGHT:
            if (te_has_sel(ed) && !shift) { int lo,hi; te_sel_range(ed,&lo,&hi); te_set_caret(ed, hi, false); }
            else te_set_caret(ed, word ? next_word(buf, ed->cursor) : next_char(buf, ed->cursor), shift);
            return true;
        case SDLK_HOME: te_set_caret(ed, 0, shift); return true;
        case SDLK_END:  te_set_caret(ed, len, shift); return true;
        case SDLK_BACKSPACE:
            if (te_has_sel(ed)) delete_sel(buf, ed);
            else if (ed->cursor > 0) {
                int to = word ? prev_word(buf, ed->cursor) : prev_char(buf, ed->cursor);
                memmove(buf + to, buf + ed->cursor, len - ed->cursor + 1);
                ed->cursor = ed->anchor = to;
            }
            return true;
        case SDLK_DELETE:
            if (te_has_sel(ed)) delete_sel(buf, ed);
            else if (ed->cursor < len) {
                int to = word ? next_word(buf, ed->cursor) : next_char(buf, ed->cursor);
                memmove(buf + ed->cursor, buf + to, len - to + 1);
            }
            return true;
        default: break;
    }
    return false;
}
