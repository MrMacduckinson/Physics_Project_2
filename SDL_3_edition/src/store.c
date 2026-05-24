#include "store.h"
#include "util.h"
#include "pdfdoc.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char g_data_dir[PATH_LEN];

void store_init(void)
{
    char* pref = SDL_GetPrefPath("Physicsbooru", "archive");
    if (pref) {
        SDL_strlcpy(g_data_dir, pref, sizeof(g_data_dir));
        SDL_free(pref);
    } else {
        SDL_strlcpy(g_data_dir, "./archive/", sizeof(g_data_dir));
        SDL_CreateDirectory(g_data_dir);
    }
}

const char* store_data_dir(void) { return g_data_dir; }

void store_full_path(const char* filename, char* out, size_t cap)
{
    SDL_snprintf(out, cap, "%s%s", g_data_dir, filename);
}

static void index_path(char* out, size_t cap)
{
    SDL_snprintf(out, cap, "%sindex.txt", g_data_dir);
}

/* ---- field escaping ---------------------------------------------------- */
static void escape(const char* in, char* out, size_t cap)
{
    size_t o = 0;
    for (; *in && o + 2 < cap; in++) {
        if (*in == '\\') { out[o++]='\\'; out[o++]='\\'; }
        else if (*in == '\t') { out[o++]='\\'; out[o++]='t'; }
        else if (*in == '\n') { out[o++]='\\'; out[o++]='n'; }
        else out[o++] = *in;
    }
    out[o] = '\0';
}
static void unescape(const char* in, char* out, size_t cap)
{
    size_t o = 0;
    for (; *in && o + 1 < cap; in++) {
        if (*in == '\\' && in[1]) {
            in++;
            if (*in == 't') out[o++]='\t';
            else if (*in == 'n') out[o++]='\n';
            else out[o++]=*in;
        } else out[o++]=*in;
    }
    out[o] = '\0';
}

bool store_load(ItemList* list)
{
    list->count = 0;
    store_invalidate();
    char ip[PATH_LEN];
    index_path(ip, sizeof(ip));
    FILE* f = fopen(ip, "r");
    if (!f) return true;  /* empty archive is fine */

    char line[TAGS_LEN + PATH_LEN + ID_LEN + 32];
    while (fgets(line, sizeof(line), f) && list->count < ITEM_MAX) {
        size_t n = strlen(line);
        while (n && (line[n-1]=='\n' || line[n-1]=='\r')) line[--n] = '\0';
        if (!n) continue;

        char* t1 = strchr(line, '\t');
        if (!t1) continue;
        *t1 = '\0';
        char* t2 = strchr(t1 + 1, '\t');
        if (!t2) continue;
        *t2 = '\0';

        Item* it = &list->items[list->count++];
        unescape(line,    it->id,       sizeof(it->id));
        unescape(t1 + 1,  it->filename, sizeof(it->filename));
        unescape(t2 + 1,  it->tags,     sizeof(it->tags));
    }
    fclose(f);
    return true;
}

bool store_save(const ItemList* list)
{
    char ip[PATH_LEN];
    index_path(ip, sizeof(ip));
    FILE* f = fopen(ip, "w");
    if (!f) return false;
    int i;
    for (i = 0; i < list->count; i++) {
        char a[ID_LEN*2], b[PATH_LEN*2], c[TAGS_LEN*2];
        escape(list->items[i].id,       a, sizeof(a));
        escape(list->items[i].filename, b, sizeof(b));
        escape(list->items[i].tags,     c, sizeof(c));
        fprintf(f, "%s\t%s\t%s\n", a, b, c);
    }
    fclose(f);
    return true;
}

bool store_import_file(const char* src_path, char* out_name, size_t cap)
{
    static unsigned counter = 0;
    const char* leaf = path_leaf(src_path);
    unsigned long stamp = (unsigned long)time(NULL);
    char name[PATH_LEN];
    SDL_snprintf(name, sizeof(name), "%lu_%u_%s", stamp, counter++, leaf);

    char dst[PATH_LEN];
    store_full_path(name, dst, sizeof(dst));

    FILE* in = fopen(src_path, "rb");
    if (!in) return false;
    FILE* out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    char buf[8192];
    size_t r;
    while ((r = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, r, out);
    fclose(in);
    fclose(out);

    SDL_strlcpy(out_name, name, cap);
    return true;
}

void store_delete_file(const char* filename)
{
    char p[PATH_LEN];
    store_full_path(filename, p, sizeof(p));
    SDL_RemovePath(p);
}

/* ---- search content cache --------------------------------------------- */
typedef struct { char id[ID_LEN]; char* text; } ContentSlot;
static ContentSlot g_content[ITEM_MAX];

void store_invalidate(void)
{
    int i;
    for (i = 0; i < ITEM_MAX; i++) {
        if (g_content[i].text) { free(g_content[i].text); g_content[i].text = NULL; }
        g_content[i].id[0] = '\0';
    }
}

static size_t read_file_text(const char* path, char* out, size_t cap)
{
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = '\0';
    return n;
}

const char* store_content(const Item* item, int idx)
{
    if (idx < 0 || idx >= ITEM_MAX) return item->tags;
    ContentSlot* s = &g_content[idx];
    if (s->text && strcmp(s->id, item->id) == 0) return s->text;

    if (s->text) { free(s->text); s->text = NULL; }

    size_t cap = 64 * 1024;
    char* body = (char*)malloc(cap);
    if (!body) return item->tags;
    body[0] = '\0';

    char full[PATH_LEN];
    store_full_path(item->filename, full, sizeof(full));
    if (is_text_path(item->filename))      read_file_text(full, body, cap);
    else if (is_pdf_path(item->filename))  pdf_extract_text(full, body, cap);

    size_t need = strlen(item->filename) + strlen(body) + 4;
    char* all = (char*)malloc(need);
    if (!all) { free(body); return item->filename; }
    SDL_snprintf(all, need, "%s %s", item->filename, body);
    free(body);
    str_lower(all);

    SDL_strlcpy(s->id, item->id, sizeof(s->id));
    s->text = all;
    return all;
}

bool item_matches(const Item* item, int idx, const char* qtags, const char* qcontent)
{
    if (qtags && qtags[0]) {
        char q[SEARCH_LEN], tags[TAGS_LEN];
        SDL_strlcpy(q,    qtags,      sizeof(q));
        SDL_strlcpy(tags, item->tags, sizeof(tags));
        str_lower(q); str_lower(tags);
        if (!strstr(tags, q)) return false;
    }
    if (qcontent && qcontent[0]) {
        char q[SEARCH_LEN];
        SDL_strlcpy(q, qcontent, sizeof(q));
        str_lower(q);
        if (!strstr(store_content(item, idx), q)) return false;
    }
    return true;
}
