/* Storage: index, media files, tag lists, content search. */
#include "store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <sys/stat.h>
#if defined(__MACOS__)
#include <Files.h>
#else
#include <dirent.h>
#endif

char g_store_error[256];

/* ---- paths --------------------------------------------------------------- */
static char g_data_dir[PATH_MAX_LEN];
static char g_index_path[PATH_MAX_LEN];
static char g_tag_list[TAGS_LEN * 2];   /* space-separated known tags */
static char g_meta_list[TAGS_LEN * 2];  /* space-separated known meta */

static void s_build_path(char* out, size_t cap, const char* dir, const char* leaf)
{
    snprintf(out, cap, "%s/%s", dir, leaf);
}

void store_init(void)
{
    if (g_data_dir[0]) return;
    const char* home = getenv("HOME");
    if (home && home[0])
        snprintf(g_data_dir, sizeof(g_data_dir), "%s/memebooru_data", home);
    else
        safe_copy(g_data_dir, sizeof(g_data_dir), "memebooru_data");
    s_build_path(g_index_path, sizeof(g_index_path), g_data_dir, "index.txt");
    store_ensure_dir();
    store_load_tag_lists();
}

const char* store_data_dir(void) { return g_data_dir; }

void store_full_path(const char* filename, char* out, size_t cap)
{
    s_build_path(out, cap, g_data_dir, filename);
}

int store_ensure_dir(void)
{
#if defined(__MACOS__)
    /* Use Mac Toolbox — mkdir() is not in the Retro68 C library */
    const char* leaf = strrchr(g_data_dir, ':');
    leaf = leaf ? leaf + 1 : g_data_dir;
    unsigned char pname[64];
    unsigned char ln = (unsigned char)(strlen(leaf) & 0xFF);
    pname[0] = ln; memcpy(pname + 1, leaf, ln);
    FSSpec spec; long newID;
    FSMakeFSSpec(0, 0, (ConstStr255Param)pname, &spec);
    OSErr e = FSpDirCreate(&spec, smSystemScript, &newID);
    return (e == noErr || e == dupFNErr);
#else
    struct stat st;
    if (stat(g_data_dir, &st) == 0) return 1;
    return (mkdir(g_data_dir, 0755) == 0);
#endif
}

/* ---- escaping (tab-separated format) ------------------------------------- */
static void s_escape(const char* in, char* out, size_t cap)
{
    size_t o = 0;
    for (; *in && o + 2 < cap; in++) {
        if      (*in == '\\') { out[o++]='\\'; out[o++]='\\'; }
        else if (*in == '\t') { out[o++]='\\'; out[o++]='t'; }
        else if (*in == '\n') { out[o++]='\\'; out[o++]='n'; }
        else                   out[o++] = *in;
    }
    out[o] = '\0';
}

static void s_unescape(const char* in, char* out, size_t cap)
{
    size_t o = 0;
    for (; *in && o + 1 < cap; in++) {
        if (*in == '\\' && in[1]) {
            in++;
            if      (*in == 't')  out[o++] = '\t';
            else if (*in == 'n')  out[o++] = '\n';
            else                  out[o++] = *in;
        } else {
            out[o++] = *in;
        }
    }
    out[o] = '\0';
}

/* ---- index load ---------------------------------------------------------- */
/* Detect and load the old 8-field pipe-separated format (SDL 1.2 legacy).   */
static void s_pipe_unescape(const char* in, char* out, size_t cap)
{
    size_t o = 0;
    for (; *in && o + 1 < cap; in++) {
        if (*in == '\\' && in[1]) {
            in++;
            if      (*in == 'n')  out[o++] = '\n';
            else if (*in == '|' || *in == '\\') out[o++] = *in;
            else    out[o++] = *in;
        } else { out[o++] = *in; }
    }
    out[o] = '\0';
}

static int s_load_pipe_line(const char* line, Item* it)
{
    /* fields: id|filename|source_status|source_detail|tags|text|file_type|situation */
    char fields[8][512];
    int field = 0, pos = 0;
    for (size_t i = 0; line[i] && field < 8; i++) {
        char c = line[i];
        if (c == '\\' && line[i+1]) {
            if (pos + 1 < 511) { fields[field][pos++] = c; fields[field][pos++] = line[++i]; }
            continue;
        }
        if (c == '|') { fields[field++][pos] = '\0'; pos = 0; continue; }
        if (pos < 511) fields[field][pos++] = c;
    }
    fields[field < 8 ? field : 7][pos] = '\0';
    if (field < 2) return 0;

    s_pipe_unescape(fields[0], it->id,       sizeof(it->id));
    s_pipe_unescape(fields[1], it->filename, sizeof(it->filename));
    /* fields[4]=tags, fields[5]=text (mapped to meta) */
    if (field >= 5) s_pipe_unescape(fields[4], it->tags, sizeof(it->tags));
    if (field >= 6) s_pipe_unescape(fields[5], it->meta, sizeof(it->meta));
    it->cover[0] = '\0';
    return 1;
}

/* Returns 1 if the filename extension is a supported media type. */
static int s_is_media(const char* name)
{
    const char* e = strrchr(name, '.');
    if (!e) return 0;
    const char* known[] = {
        ".png",".jpg",".jpeg",".gif",
        ".pdf",".txt",".csv",".md",".tex",".bib",".mp4",
        NULL
    };
    for (int i = 0; known[i]; i++) {
        const char* k = known[i]; size_t kl = strlen(k), el = strlen(e);
        if (kl == el) {
            int eq = 1;
            for (size_t j = 0; j < kl; j++)
                if ((e[j]|32) != (k[j]|32)) { eq=0; break; }
            if (eq) return 1;
        }
    }
    return 0;
}

/* Returns 1 if filename is already present in the list. */
static int s_in_list(const ItemList* list, const char* name)
{
    for (int i = 0; i < list->count; i++)
        if (strcmp(list->items[i].filename, name) == 0) return 1;
    return 0;
}

/* Scans the data dir for files not yet in the index and appends them.
   Returns the number of new items added (caller should save if > 0). */
static int s_scan_unregistered(ItemList* list)
{
    int added = 0;
#if defined(__MACOS__)
    /* Mac Toolbox directory iteration */
    CInfoPBRec pb;
    unsigned char pname[256];
    memset(&pb, 0, sizeof(pb));
    pb.hFileInfo.ioNamePtr = (StringPtr)pname;
    pb.hFileInfo.ioVRefNum = 0; /* default volume */
    short idx = 1;
    while (list->count < ITEM_MAX) {
        memset(pname, 0, sizeof(pname));
        pb.hFileInfo.ioDirID  = 0;
        pb.hFileInfo.ioFDirIndex = idx++;
        /* walk from the data dir — use dirID stored in g_data_dir is tricky;
           iterate relative to default dir (CWD = app folder) */
        if (PBGetCatInfo(&pb, false) != noErr) break;
        if (pb.hFileInfo.ioFlAttrib & ioDirMask) continue; /* skip subdirs */
        char cname[64]; size_t nl = pname[0]; if (nl >= 63) nl = 62;
        memcpy(cname, pname + 1, nl); cname[nl] = '\0';
        if (!s_is_media(cname)) continue;
        if (s_in_list(list, cname)) continue;
        Item* it = &list->items[list->count];
        memset(it, 0, sizeof(*it));
        store_gen_id(it->id, sizeof(it->id));
        safe_copy(it->filename, sizeof(it->filename), cname);
        list->count++; added++;
    }
#else
    DIR* d = opendir(g_data_dir);
    if (!d) return 0;
    struct dirent* de;
    while ((de = readdir(d)) != NULL && list->count < ITEM_MAX) {
        if (de->d_name[0] == '.') continue;
        if (!s_is_media(de->d_name)) continue;
        if (s_in_list(list, de->d_name)) continue;
        Item* it = &list->items[list->count];
        memset(it, 0, sizeof(*it));
        store_gen_id(it->id, sizeof(it->id));
        safe_copy(it->filename, sizeof(it->filename), de->d_name);
        list->count++; added++;
    }
    closedir(d);
#endif
    return added;
}

int store_load(ItemList* list)
{
    list->count = 0;
    store_invalidate_content();
    FILE* f = fopen(g_index_path, "r");
    if (!f) return 1; /* empty archive is fine */

    char line[TAGS_LEN * 2 + PATH_MAX_LEN * 2 + 64];
    while (fgets(line, sizeof(line), f) && list->count < ITEM_MAX) {
        /* strip trailing CR/LF */
        size_t n = strlen(line);
        while (n && (line[n-1]=='\n' || line[n-1]=='\r')) line[--n] = '\0';
        if (!n) continue;

        Item* it = &list->items[list->count];
        memset(it, 0, sizeof(*it));

        /* Detect format: new = tabs, old = pipes */
        if (strchr(line, '\t')) {
            /* New tab-separated format: id\tfilename\ttags\tmeta\tcover */
            char* t1 = strchr(line, '\t');    if (!t1) continue;
            *t1 = '\0';
            char* t2 = strchr(t1+1, '\t');    /* may be NULL */
            if (t2) *t2 = '\0';
            char* t3 = t2 ? strchr(t2+1, '\t') : NULL;
            if (t3) *t3 = '\0';
            char* t4 = t3 ? strchr(t3+1, '\t') : NULL;
            if (t4) *t4 = '\0';
            s_unescape(line,    it->id,       sizeof(it->id));
            s_unescape(t1+1,    it->filename, sizeof(it->filename));
            if (t2) s_unescape(t2+1, it->tags,  sizeof(it->tags));
            if (t3) s_unescape(t3+1, it->meta,  sizeof(it->meta));
            if (t4) s_unescape(t4+1, it->cover, sizeof(it->cover));
        } else {
            /* Old pipe-separated format */
            if (!s_load_pipe_line(line, it)) continue;
        }
        list->count++;
    }
    fclose(f);
    if (s_scan_unregistered(list) > 0)
        store_save(list);
    return 1;
}

int store_save(const ItemList* list)
{
    if (!store_ensure_dir()) { safe_copy(g_store_error, sizeof(g_store_error), "Cannot create data dir"); return 0; }
    FILE* f = fopen(g_index_path, "w");
    if (!f) { snprintf(g_store_error, sizeof(g_store_error), "Cannot open index for writing"); return 0; }
    for (int i = 0; i < list->count; i++) {
        char a[PATH_MAX_LEN*2], b[PATH_MAX_LEN*2], c[TAGS_LEN*2], d[TAGS_LEN*2], e[PATH_MAX_LEN*2];
        s_escape(list->items[i].id,       a, sizeof(a));
        s_escape(list->items[i].filename, b, sizeof(b));
        s_escape(list->items[i].tags,     c, sizeof(c));
        s_escape(list->items[i].meta,     d, sizeof(d));
        s_escape(list->items[i].cover,    e, sizeof(e));
        fprintf(f, "%s\t%s\t%s\t%s\t%s\n", a, b, c, d, e);
    }
    fclose(f);
    return 1;
}

/* ---- file import --------------------------------------------------------- */
static unsigned g_import_counter = 0;

int store_import_file(const char* src_path, char* out_name, size_t cap)
{
    if (!store_ensure_dir()) return 0;
    /* Derive original leaf filename (handles both POSIX '/' and HFS ':') */
    const char* leaf = strrchr(src_path, '/');
    const char* leaf2 = strrchr(src_path, ':');
    if (!leaf || (leaf2 && leaf2 > leaf)) leaf = leaf2;
    leaf = leaf ? leaf + 1 : src_path;
    unsigned long stamp = (unsigned long)time(NULL);
    char name[PATH_MAX_LEN];
    snprintf(name, sizeof(name), "%lu_%u_%s", stamp, g_import_counter++, leaf);

    char dst[PATH_MAX_LEN];
    store_full_path(name, dst, sizeof(dst));

    FILE* in  = fopen(src_path, "rb");  if (!in)  return 0;
    FILE* out = fopen(dst,      "wb");  if (!out) { fclose(in); return 0; }
    char buf[8192]; size_t r;
    while ((r = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, r, out);
    fclose(in); fclose(out);
    safe_copy(out_name, cap, name);
    return 1;
}

void store_delete_file(const char* filename)
{
    char p[PATH_MAX_LEN];
    store_full_path(filename, p, sizeof(p));
    remove(p);
}

/* ---- id generation ------------------------------------------------------- */
void store_gen_id(char* out, size_t cap)
{
    static unsigned cnt = 0;
    snprintf(out, cap, "%lu_%u", (unsigned long)time(NULL), cnt++);
}

/* ---- tag lists ----------------------------------------------------------- */
static void s_list_strip(char* s)
{
    size_t n = strlen(s);
    while (n && (s[n-1]=='\n'||s[n-1]=='\r'||s[n-1]==' '||s[n-1]=='\t')) s[--n]='\0';
    size_t i = 0;
    while (s[i]==' '||s[i]=='\t') i++;
    if (i) memmove(s, s+i, n-i+1);
}

static int s_list_has(const char* list, const char* tok)
{
    if (!tok || !tok[0]) return 0;
    size_t tlen = strlen(tok);
    const char* p = list;
    while (*p) {
        while (*p==' ') p++;
        const char* start = p;
        while (*p && *p!=' ') p++;
        if ((size_t)(p-start)==tlen && ascii_strncasecmp(start,tok,tlen)==0) return 1;
    }
    return 0;
}

static void s_list_add(char* list, size_t cap, const char* tok)
{
    if (!tok||!tok[0]) return;
    if (s_list_has(list, tok)) return;
    size_t len = strlen(list), tlen = strlen(tok);
    if (len+tlen+2 > cap) return;
    if (len) { list[len++]=' '; list[len]='\0'; }
    strncat(list, tok, cap-len-1);
}

static void s_list_load(const char* path, char* list, size_t cap)
{
    list[0] = '\0';
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) { s_list_strip(line); if (line[0]) s_list_add(list, cap, line); }
    fclose(f);
}

static void s_list_save(const char* path, const char* list)
{
    FILE* f = fopen(path, "w");
    if (!f) return;
    const char* p = list;
    while (*p) {
        while (*p==' ') p++;
        const char* start = p;
        while (*p && *p!=' ') p++;
        if (p > start) { fwrite(start, 1, (size_t)(p-start), f); fputc('\n', f); }
    }
    fclose(f);
}

void store_load_tag_lists(void)
{
    char path[PATH_MAX_LEN];
    s_build_path(path, sizeof(path), g_data_dir, "tags.txt");
    s_list_load(path, g_tag_list, sizeof(g_tag_list));
    s_build_path(path, sizeof(path), g_data_dir, "metatags.txt");
    s_list_load(path, g_meta_list, sizeof(g_meta_list));
}

int store_tag_kind(const char* tag)
{
    if (s_list_has(g_tag_list,  tag)) return 1; /* known tag  */
    if (s_list_has(g_meta_list, tag)) return 2; /* known meta */
    return 0;                                    /* unknown    */
}

int store_add_tag(const char* tag, int kind)
{
    char path[PATH_MAX_LEN];
    if (kind == 1) {
        s_list_add(g_tag_list, sizeof(g_tag_list), tag);
        s_build_path(path, sizeof(path), g_data_dir, "tags.txt");
        s_list_save(path, g_tag_list);
        return 1;
    }
    if (kind == 2) {
        s_list_add(g_meta_list, sizeof(g_meta_list), tag);
        s_build_path(path, sizeof(path), g_data_dir, "metatags.txt");
        s_list_save(path, g_meta_list);
        return 1;
    }
    return 0;
}

/* ---- content search cache ------------------------------------------------ */
typedef struct { char id[64]; char* text; } ContentSlot;
static ContentSlot g_content[ITEM_MAX];

void store_invalidate_content(void)
{
    for (int i = 0; i < ITEM_MAX; i++) {
        if (g_content[i].text) { free(g_content[i].text); g_content[i].text = NULL; }
        g_content[i].id[0] = '\0';
    }
}

static const char* s_get_content(const Item* item, int idx)
{
    if (idx < 0 || idx >= ITEM_MAX) return "";
    if (g_content[idx].id[0] && strcmp(g_content[idx].id, item->id)==0 && g_content[idx].text)
        return g_content[idx].text;

    /* (re)build content: read file body if text/md/tex/csv/bib */
    char full[PATH_MAX_LEN];
    store_full_path(item->filename, full, sizeof(full));
    char* text = NULL;
    if (is_text_path(full)) {
        FILE* f = fopen(full, "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f); rewind(f);
            if (sz > 0 && sz < 512*1024) {
                text = (char*)malloc((size_t)sz + 1);
                if (text) { size_t r = fread(text, 1, (size_t)sz, f); text[r] = '\0'; }
            }
            fclose(f);
        }
    }
    if (!text) { text = (char*)malloc(1); if (text) text[0]='\0'; }
    /* lowercase in-place for case-insensitive search */
    if (text) { for (char* p = text; *p; p++) *p = (char)tolower((unsigned char)*p); }

    if (g_content[idx].text) free(g_content[idx].text);
    safe_copy(g_content[idx].id, sizeof(g_content[idx].id), item->id);
    g_content[idx].text = text;
    return text ? text : "";
}

/* Whole-word token search: does 'text' contain token 'tok' as a whole word? */
static int s_has_token(const char* haystack, const char* tok)
{
    size_t tlen = strlen(tok);
    if (!tlen) return 1;
    const char* p = haystack;
    while (*p) {
        while (*p==' ') p++;
        const char* start = p;
        while (*p && *p!=' ') p++;
        if ((size_t)(p-start)==tlen && ascii_strncasecmp(start,tok,tlen)==0) return 1;
    }
    return 0;
}

int item_matches(const Item* item, int idx, const char* qtags, const char* qcontent)
{
    if (qtags && qtags[0]) {
        /* Each space-separated token in qtags must appear in tags OR meta */
        char q[SEARCH_LEN]; safe_copy(q, sizeof(q), qtags);
        char* tok = strtok(q, " ");
        while (tok) {
            if (!s_has_token(item->tags, tok) && !s_has_token(item->meta, tok))
                return 0;
            tok = strtok(NULL, " ");
        }
    }
    if (qcontent && qcontent[0]) {
        char lowq[SEARCH_LEN]; safe_copy(lowq, sizeof(lowq), qcontent);
        for (char* p = lowq; *p; p++) *p = (char)tolower((unsigned char)*p);
        /* Check filename */
        char low_fn[PATH_MAX_LEN]; safe_copy(low_fn, sizeof(low_fn), item->filename);
        for (char* p = low_fn; *p; p++) *p = (char)tolower((unsigned char)*p);
        if (!strstr(low_fn, lowq)) {
            /* Check file content */
            const char* body = s_get_content(item, idx);
            if (!strstr(body, lowq)) return 0;
        }
    }
    return 1;
}
