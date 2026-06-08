#include "store.h"
#include "util.h"
#include "pdfdoc.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <time.h>

static char g_data_dir[PATH_LEN];
static char g_tag_list[TAGLIST_LEN];
static char g_meta_list[TAGLIST_LEN];

static void taglist_path(char* out, size_t cap, const char* leaf)
{
    SDL_snprintf(out, cap, "%s%s", g_data_dir, leaf);
}

static void strip_line(char* s)
{
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\t'))
        s[--n] = '\0';
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) memmove(s, s + i, n - i + 1);
}

static bool list_has_token(const char* list, const char* tok)
{
    if (!tok || !tok[0]) return false;
    size_t tlen = strlen(tok);
    const char* p = list;
    while (*p) {
        while (*p == ' ') p++;
        const char* start = p;
        while (*p && *p != ' ') p++;
        size_t len = (size_t)(p - start);
        if (len == tlen && SDL_strncasecmp(start, tok, tlen) == 0) return true;
    }
    return false;
}

static void list_add_token(char* list, size_t cap, const char* tok)
{
    if (!tok || !tok[0]) return;
    if (list_has_token(list, tok)) return;
    size_t len = strlen(list);
    size_t tlen = strlen(tok);
    if (len + tlen + 2 > cap) return;
    if (len > 0) { list[len++] = ' '; list[len] = '\0'; }
    SDL_strlcpy(list + len, tok, cap - len);
}

static void list_load_file(const char* path, char* list, size_t cap)
{
    list[0] = '\0';
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        strip_line(line);
        if (!line[0]) continue;
        list_add_token(list, cap, line);
    }
    fclose(f);
}

static void list_save_file(const char* path, const char* list)
{
    FILE* f = fopen(path, "w");
    if (!f) return;
    const char* p = list;
    while (*p) {
        while (*p == ' ') p++;
        const char* start = p;
        while (*p && *p != ' ') p++;
        if (p > start) {
            fwrite(start, 1, (size_t)(p - start), f);
            fputc('\n', f);
        }
    }
    fclose(f);
}

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
    store_load_tag_lists();
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

static int s_in_list(const ItemList* list, const char* name)
{
    for (int i = 0; i < list->count; i++)
        if (strcmp(list->items[i].filename, name) == 0) return 1;
    return 0;
}

static int s_scan_unregistered(ItemList* list)
{
    static unsigned cnt = 0;
    int added = 0;
    DIR* d = opendir(g_data_dir);
    if (!d) return 0;
    struct dirent* de;
    while ((de = readdir(d)) != NULL && list->count < ITEM_MAX) {
        if (de->d_name[0] == '.') continue;
        if (!is_supported_path(de->d_name)) continue;
        if (s_in_list(list, de->d_name)) continue;
        Item* it = &list->items[list->count];
        memset(it, 0, sizeof(*it));
        SDL_snprintf(it->id, sizeof(it->id), "%lu_%u",
                     (unsigned long)time(NULL), cnt++);
        SDL_strlcpy(it->filename, de->d_name, sizeof(it->filename));
        list->count++; added++;
    }
    closedir(d);
    return added;
}

bool store_load(ItemList* list)
{
    list->count = 0;
    store_invalidate();
    char ip[PATH_LEN];
    index_path(ip, sizeof(ip));
    FILE* f = fopen(ip, "r");
    if (!f) return true;  /* empty archive is fine */

    char line[TAGS_LEN*2 + PATH_LEN + ID_LEN + 64];
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
        char* t3 = strchr(t2 + 1, '\t');
        if (t3) *t3 = '\0';
        char* t4 = t3 ? strchr(t3 + 1, '\t') : NULL;
        if (t4) *t4 = '\0';

        Item* it = &list->items[list->count++];
        unescape(line,    it->id,       sizeof(it->id));
        unescape(t1 + 1,  it->filename, sizeof(it->filename));
        unescape(t2 + 1,  it->tags,     sizeof(it->tags));
        if (t3) unescape(t3 + 1, it->meta,  sizeof(it->meta));
        else    it->meta[0] = '\0';
        if (t4) unescape(t4 + 1, it->cover, sizeof(it->cover));
        else    it->cover[0] = '\0';
    }
    fclose(f);
    if (s_scan_unregistered(list) > 0)
        store_save(list);
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
        char a[ID_LEN*2], b[PATH_LEN*2], c[TAGS_LEN*2], d[TAGS_LEN*2], e[PATH_LEN*2];
        escape(list->items[i].id,       a, sizeof(a));
        escape(list->items[i].filename, b, sizeof(b));
        escape(list->items[i].tags,     c, sizeof(c));
        escape(list->items[i].meta,     d, sizeof(d));
        escape(list->items[i].cover,    e, sizeof(e));
        fprintf(f, "%s\t%s\t%s\t%s\t%s\n", a, b, c, d, e);
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
        char q[SEARCH_LEN];
        SDL_strlcpy(q, qtags, sizeof(q));
        size_t n = strlen(q);
        while (n > 0 && q[n-1] == ' ') q[--n] = '\0';
        if (qtags[strlen(qtags)-1] != ' ') {
            char* last = strrchr(q, ' ');
            if (last) *last = '\0';
            else q[0] = '\0';
        }
        const char* p = q;
        while (*p) {
            while (*p == ' ') p++;
            const char* start = p;
            while (*p && *p != ' ') p++;
            if (p > start) {
                char tok[SEARCH_LEN];
                size_t len = (size_t)(p - start);
                if (len >= sizeof(tok)) len = sizeof(tok) - 1;
                memcpy(tok, start, len); tok[len] = '\0';
                if (!list_has_token(item->tags, tok) && !list_has_token(item->meta, tok))
                    return false;
            }
        }
    }
    if (qcontent && qcontent[0]) {
        char q[SEARCH_LEN];
        SDL_strlcpy(q, qcontent, sizeof(q));
        str_lower(q);
        if (!strstr(store_content(item, idx), q)) return false;
    }
    return true;
}

void store_load_tag_lists(void)
{
    char tp[PATH_LEN], mp[PATH_LEN];
    taglist_path(tp, sizeof(tp), "tags.txt");
    taglist_path(mp, sizeof(mp), "metatags.txt");
    list_load_file(tp, g_tag_list, sizeof(g_tag_list));
    list_load_file(mp, g_meta_list, sizeof(g_meta_list));
}

TagKind store_tag_kind(const char* tag)
{
    if (list_has_token(g_meta_list, tag)) return TAG_KIND_META;
    if (list_has_token(g_tag_list, tag)) return TAG_KIND_TAG;
    return TAG_KIND_UNKNOWN;
}

bool store_add_tag_to_list(const char* tag, TagKind kind)
{
    if (!tag || !tag[0]) return false;
    char tp[PATH_LEN], mp[PATH_LEN];
    taglist_path(tp, sizeof(tp), "tags.txt");
    taglist_path(mp, sizeof(mp), "metatags.txt");
    if (kind == TAG_KIND_META) {
        if (list_has_token(g_meta_list, tag)) return false;
        list_add_token(g_meta_list, sizeof(g_meta_list), tag);
        list_save_file(mp, g_meta_list);
        return true;
    }
    if (kind == TAG_KIND_TAG) {
        if (list_has_token(g_tag_list, tag)) return false;
        list_add_token(g_tag_list, sizeof(g_tag_list), tag);
        list_save_file(tp, g_tag_list);
        return true;
    }
    return false;
}
