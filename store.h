/* Storage: index file, imported media files, tag lists, content search. */
#ifndef STORE_H
#define STORE_H

#include "app.h"

/* ---- init / paths -------------------------------------------------------- */
void        store_init(void);
const char* store_data_dir(void);
void        store_full_path(const char* filename, char* out, size_t cap);
int         store_ensure_dir(void);

/* ---- index load / save --------------------------------------------------- */
int store_load(ItemList* list);
int store_save(const ItemList* list);

/* ---- file import / delete ------------------------------------------------ */
/* Copies src into data dir; writes stored leaf name into out_name.  Returns 1 ok. */
int  store_import_file(const char* src_path, char* out_name, size_t cap);
void store_delete_file(const char* filename);

/* ---- tag lists (taglist.txt / metalist.txt) ------------------------------ */
void store_load_tag_lists(void);
int  store_tag_kind(const char* tag); /* 0=unknown 1=tag 2=meta */
int  store_add_tag(const char* tag, int kind);

/* ---- search -------------------------------------------------------------- */
/* Returns 1 if item passes both filters (empty = match all).
   qtags    — whole-word match against tags + meta
   qcontent — substring match against filename + file body */
int item_matches(const Item* item, int idx,
                 const char* qtags, const char* qcontent);
void store_invalidate_content(void);

/* ---- generate unique id string ------------------------------------------ */
void store_gen_id(char* out, size_t cap);

/* Storage error buffer (read-only for callers) */
extern char g_store_error[256];

#endif /* STORE_H */
