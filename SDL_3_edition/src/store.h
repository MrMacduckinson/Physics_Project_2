/* Local archive storage: index file + imported media, plus search index. */
#ifndef STORE_H
#define STORE_H

#include <stdbool.h>
#include <stddef.h>
#include "app.h"

void        store_init(void);
const char* store_data_dir(void);
void        store_full_path(const char* filename, char* out, size_t cap);

bool store_load(ItemList* list);
bool store_save(const ItemList* list);

/* Copy src into the data dir; writes the stored leaf name into out_name. */
bool store_import_file(const char* src_path, char* out_name, size_t cap);
void store_delete_file(const char* filename);

/* Lazily-built lowercased text for an item: filename + document body (no tags). */
const char* store_content(const Item* item, int idx);
void        store_invalidate(void);

/* Returns true if the item passes both filters (empty filter = match all).
 * qtags   — matched only against item->tags
 * qcontent — matched against filename + document body text */
bool item_matches(const Item* item, int idx, const char* qtags, const char* qcontent);

#endif /* STORE_H */
