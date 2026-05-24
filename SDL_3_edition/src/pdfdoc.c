#include "pdfdoc.h"
#include <mupdf/fitz.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static fz_context* g_ctx = NULL;

static fz_context* ctx(void)
{
    if (!g_ctx) {
        g_ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
        if (g_ctx) {
            fz_try(g_ctx)
                fz_register_document_handlers(g_ctx);
            fz_catch(g_ctx) {
                fz_drop_context(g_ctx);
                g_ctx = NULL;
            }
        }
    }
    return g_ctx;
}

void pdf_shutdown(void)
{
    if (g_ctx) { fz_drop_context(g_ctx); g_ctx = NULL; }
}

unsigned char* pdf_render_page(const char* path, int page_no,
                               int box_w, int box_h,
                               int* out_w, int* out_h, int* page_count)
{
    fz_context* c = ctx();
    if (!c) return NULL;

    fz_document* doc = NULL;
    fz_page* page = NULL;
    fz_pixmap* pix = NULL;
    unsigned char* result = NULL;

    fz_try(c) {
        doc = fz_open_document(c, path);
        int n = fz_count_pages(c, doc);
        if (page_count) *page_count = n;
        if (n <= 0) fz_throw(c, FZ_ERROR_GENERIC, "no pages");
        if (page_no < 0) page_no = 0;
        if (page_no >= n) page_no = n - 1;

        page = fz_load_page(c, doc, page_no);
        fz_rect rc = fz_bound_page(c, page);
        float pw = rc.x1 - rc.x0, ph = rc.y1 - rc.y0;
        if (pw < 1) pw = 1;
        if (ph < 1) ph = 1;

        float zx = (float)box_w / pw, zy = (float)box_h / ph;
        float zoom = zx < zy ? zx : zy;
        if (zoom > 4.0f) zoom = 4.0f;   /* cap so huge boxes don't explode memory */

        fz_matrix m = fz_scale(zoom, zoom);
        pix = fz_new_pixmap_from_page(c, page, m, fz_device_rgb(c), 0);

        int w = pix->w, h = pix->h, np = pix->n;
        result = (unsigned char*)malloc((size_t)w * h * 3);
        if (result) {
            int y, x;
            for (y = 0; y < h; y++) {
                const unsigned char* src = pix->samples + (size_t)y * pix->stride;
                unsigned char* dst = result + (size_t)y * w * 3;
                if (np == 3) {
                    memcpy(dst, src, (size_t)w * 3);
                } else {
                    for (x = 0; x < w; x++) {
                        dst[x*3+0] = src[x*np+0];
                        dst[x*3+1] = src[x*np+1];
                        dst[x*3+2] = src[x*np+2];
                    }
                }
            }
            if (out_w) *out_w = w;
            if (out_h) *out_h = h;
        }
    }
    fz_always(c) {
        if (pix)  fz_drop_pixmap(c, pix);
        if (page) fz_drop_page(c, page);
        if (doc)  fz_drop_document(c, doc);
    }
    fz_catch(c) {
        free(result);
        return NULL;
    }
    return result;
}

int pdf_page_count(const char* path)
{
    fz_context* c = ctx();
    if (!c) return 0;
    fz_document* doc = NULL;
    int n = 0;
    fz_try(c) {
        doc = fz_open_document(c, path);
        n = fz_count_pages(c, doc);
    }
    fz_always(c) { if (doc) fz_drop_document(c, doc); }
    fz_catch(c) { return 0; }
    return n;
}

size_t pdf_extract_text(const char* path, char* out, size_t cap)
{
    fz_context* c = ctx();
    if (!c || cap == 0) return 0;
    out[0] = '\0';

    fz_document* doc = NULL;
    size_t used = 0;

    fz_try(c) {
        doc = fz_open_document(c, path);
        int pages = fz_count_pages(c, doc);
        int maxpages = pages < 20 ? pages : 20;
        int i;
        for (i = 0; i < maxpages && used < cap - 8; i++) {
            fz_stext_options opts;
            memset(&opts, 0, sizeof(opts));
            fz_stext_page* tp = fz_new_stext_page_from_page_number(c, doc, i, &opts);
            fz_stext_block* block;
            for (block = tp->first_block; block && used < cap - 8; block = block->next) {
                if (block->type != FZ_STEXT_BLOCK_TEXT) continue;
                fz_stext_line* line;
                for (line = block->u.t.first_line; line && used < cap - 8; line = line->next) {
                    fz_stext_char* ch;
                    for (ch = line->first_char; ch && used < cap - 8; ch = ch->next) {
                        char tmp[8];
                        int len = fz_runetochar(tmp, ch->c);
                        if (used + (size_t)len < cap - 8) {
                            memcpy(out + used, tmp, len);
                            used += len;
                        }
                    }
                    out[used++] = ' ';
                }
            }
            fz_drop_stext_page(c, tp);
        }
        out[used] = '\0';
    }
    fz_always(c) { if (doc) fz_drop_document(c, doc); }
    fz_catch(c) { out[used] = '\0'; }
    return used;
}
