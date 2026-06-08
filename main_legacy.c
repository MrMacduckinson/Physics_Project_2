/*
This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <sys/stat.h>
#include <strings.h>
#include <math.h>

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_THREAD_LOCALS
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Mac OS X 10.4 specific includes for bundle/app init
#ifdef BUILD_FOR_10_4
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#endif

#include <SDL/SDL.h>

#if defined(__APPLE__) && !defined(BUILD_FOR_10_4)
#include <ApplicationServices/ApplicationServices.h>
#include <CoreText/CoreText.h>
#endif

#if defined(USE_SDL_TTF)
#include "SDL_ttf.h"
static TTF_Font* g_ttf_normal = NULL; /* ~14pt */
static TTF_Font* g_ttf_large  = NULL; /* ~28pt */
static int g_ttf_char_w  = 8;   /* avg advance for normal font */
static int g_ttf_char_w2 = 14;  /* avg advance for large font */
#endif

#if defined(__MACOS__) && !defined(BUILD_FOR_10_4)
#include <StandardFile.h>
#include <Files.h>
#include <Quickdraw.h>
#include <QDOffscreen.h>
#include <ImageCompression.h>
#include <Movies.h>
#include <Gestalt.h>
#endif

#include "mac_file_dialog.h"

static char g_browse_path[512];
#if defined(__MACOS__) && !defined(BUILD_FOR_10_4)
static FSSpec g_browse_spec;
static int g_browse_spec_valid = 0;
static short storage_vrefnum;
static long storage_dirid;

static int cstr_to_pstr(const char* src, unsigned char* dst, size_t cap)
{
	size_t len = strlen(src);
	if (len > 255) len = 255;
	if (len + 1 > cap) return 0;
	dst[0] = (unsigned char)len;
	memcpy(dst + 1, src, len);
	return 1;
}

static const char* path_leaf_name(const char* path)
{
	const char* base = strrchr(path, ':');
	if (!base) base = strrchr(path, '/');
	return base ? base + 1 : path;
}

static int storage_leaf_spec(short vrefnum, long dirid, const char* leaf, FSSpec* spec)
{
	unsigned char name[256];
	if (!leaf || !spec) return 0;
	if (!cstr_to_pstr(leaf, name, sizeof(name))) return 0;
	return FSMakeFSSpec(vrefnum, dirid, (ConstStr255Param)name, spec) == noErr;
}
#endif

static int path_append_component(char* path, size_t cap, const char* component)
{
	size_t len = strlen(path);
	size_t comp_len = strlen(component);
	if (len > 0 && path[len - 1] != ':')
	{
		if (len + 1 >= cap) return 0;
		path[len++] = ':';
		path[len] = '\0';
	}
	if (len + comp_len >= cap) return 0;
	memcpy(path + len, component, comp_len + 1);
	return 1;
}

static void build_storage_path(char* out, size_t cap, const char* dir, const char* leaf)
{
#if defined(__MACOS__) && !defined(BUILD_FOR_10_4)
	snprintf(out, cap, "%s:%s", dir, leaf);
#else
	snprintf(out, cap, "%s/%s", dir, leaf);
#endif
}

#if defined(__MACOS__) && !defined(BUILD_FOR_10_4)
static void pstr_to_cstr(const unsigned char* src, char* dst, size_t cap)
{
	size_t len = src[0];
	if (len >= cap) len = cap - 1;
	memcpy(dst, src + 1, len);
	dst[len] = '\0';
}

static int fsspec_to_hfs_path(const FSSpec* spec, char* out, size_t cap)
{
	if (!spec || !out || cap == 0) return 0;

	out[0] = '\0';

	unsigned char volume_name[256];
	memset(volume_name, 0, sizeof(volume_name));

	HVolumeParam vol;
	memset(&vol, 0, sizeof(vol));
	vol.ioVRefNum = spec->vRefNum;
	vol.ioNamePtr = (StringPtr)volume_name;
	if (PBHGetVInfo((HParmBlkPtr)&vol, false) != noErr || volume_name[0] == 0)
		return 0;

	char pieces[16][64];
	int piece_count = 0;
	long parent_id = spec->parID;

	while (parent_id != fsRtParID && piece_count < 16)
	{
		DirInfo dir;
		unsigned char dir_name[256];
		memset(&dir, 0, sizeof(dir));
		memset(dir_name, 0, sizeof(dir_name));

		dir.ioVRefNum = spec->vRefNum;
		dir.ioDrDirID = parent_id;
		dir.ioFDirIndex = -1;
		dir.ioNamePtr = (StringPtr)dir_name;

		if (PBGetCatInfo((CInfoPBPtr)&dir, false) != noErr || dir_name[0] == 0)
			break;

		pstr_to_cstr(dir_name, pieces[piece_count], sizeof(pieces[piece_count]));
		parent_id = dir.ioDrParID;
		piece_count++;
	}

	pstr_to_cstr(volume_name, out, cap);
	for (int i = piece_count - 1; i >= 0; i--)
	{
		if (!path_append_component(out, cap, pieces[i]))
			return 0;
	}

	char file_name[64];
	pstr_to_cstr(spec->name, file_name, sizeof(file_name));
	return path_append_component(out, cap, file_name);
}

const char* mac_file_dialog(const char* title, const char* filters, const char* initial_dir)
{
	(void)title;
	(void)filters;
	(void)initial_dir;

	static char path[512];
	StandardFileReply reply;
	memset(&reply, 0, sizeof(reply));

	StandardGetFile(NULL, 0, NULL, &reply);
	if (!reply.sfGood) return NULL;
	if (!fsspec_to_hfs_path(&reply.sfFile, path, sizeof(path))) return NULL;
	strncpy(g_browse_path, path, sizeof(g_browse_path) - 1);
	g_browse_path[sizeof(g_browse_path) - 1] = '\0';
	g_browse_spec = reply.sfFile;
	g_browse_spec_valid = 1;
	return path;
}
#elif defined(BUILD_FOR_10_4)
const char* mac_file_dialog(const char* title, const char* filters, const char* initial_dir)
{
	(void)title;
	(void)filters;
	(void)initial_dir;
	g_browse_path[0] = '\0';
	return NULL;
}
#endif

#ifndef USE_SDL_MAIN
#undef main
#endif

#define MEME_MAX 512
#define PATH_MAX_LEN 512
#define TAGS_MAX_LEN 512
#define TEXT_MAX_LEN 512
#define SMALL_MAX_LEN 128

#if defined(BUILD_FOR_11_0) || defined(BUILD_FOR_10_7)
#define WINDOW_W 1280
#define WINDOW_H 800
#define TOP_BAR_H 52
#define STATUS_BAR_H 32
#elif defined(BUILD_FOR_10_4)
#define WINDOW_W 1024
#define WINDOW_H 768
#define TOP_BAR_H 50
#define STATUS_BAR_H 30
#else
#define WINDOW_W 800
#define WINDOW_H 600
#define TOP_BAR_H 48
#define STATUS_BAR_H 30
#endif

#define BTN_H 38

#if defined(BUILD_FOR_11_0) || defined(BUILD_FOR_10_7)
#define BTN_W 180
#define BTN_GAP 20
#elif defined(BUILD_FOR_10_4)
#define BTN_W 155
#define BTN_GAP 14
#else
#define BTN_W 140
#define BTN_GAP 10
#endif
#define BTN_START_X 30

#if defined(BUILD_FOR_11_0) || defined(BUILD_FOR_10_7)
#define THUMB_SIZE 120
#define THUMB_SPACING 144
#elif defined(BUILD_FOR_10_4)
#define THUMB_SIZE 96
#define THUMB_SPACING 116
#else
#define THUMB_SIZE 80
#define THUMB_SPACING 100
#endif

static SDL_Surface* screen;       /* logical/canvas surface, matches window size */
#if defined(BUILD_FOR_11_0) || defined(BUILD_FOR_10_7)
static SDL_Surface* g_win_surf;   /* actual OS window surface */
static int g_resize_pending_w = 0, g_resize_pending_h = 0, g_resize_idle = 0;
#endif
static void draw_text_fit(int x, int y, const char* value, Uint32 color, int scale, int max_px);
static int ensure_data_dir(void);
static void set_image_error(const char* msg);
static void set_image_error_code(const char* step, int err);
static void clear_image_error(void);
#if defined(__MACOS__) && !defined(BUILD_FOR_10_4)
static int g_quicktime_ready = 0;
static ComponentInstance g_jpeg_gi = NULL;
#endif

/* ====================================================================== */
static void fill_rect(int x, int y, int w, int h, Uint32 color)
{
	SDL_Rect r;
	r.x = x;
	r.y = y;
	r.w = w;
	r.h = h;
	SDL_FillRect(screen, &r, color);
}


/* Fallback bitmap font for 10.4 builds. */
static const unsigned char font5x7[95][5] = {
	{0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5f,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00},
	{0x14,0x7f,0x14,0x7f,0x14}, {0x24,0x2a,0x7f,0x2a,0x12}, {0x23,0x13,0x08,0x64,0x62},
	{0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00}, {0x00,0x1c,0x22,0x41,0x00},
	{0x00,0x41,0x22,0x1c,0x00}, {0x14,0x08,0x3e,0x08,0x14}, {0x08,0x08,0x3e,0x08,0x08},
	{0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00},
	{0x20,0x10,0x08,0x04,0x02}, {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
	{0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31}, {0x18,0x14,0x12,0x7f,0x10},
	{0x27,0x45,0x45,0x45,0x39}, {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
	{0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e}, {0x00,0x36,0x36,0x00,0x00},
	{0x00,0x56,0x36,0x00,0x00}, {0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14},
	{0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x51,0x09,0x06}, {0x32,0x49,0x79,0x41,0x3e},
	{0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36}, {0x3e,0x41,0x41,0x41,0x22},
	{0x7f,0x41,0x41,0x22,0x1c}, {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
	{0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f}, {0x00,0x41,0x7f,0x41,0x00},
	{0x20,0x40,0x41,0x3f,0x01}, {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
	{0x7f,0x02,0x04,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f}, {0x3e,0x41,0x41,0x41,0x3e},
	{0x7f,0x09,0x09,0x09,0x06}, {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
	{0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01}, {0x3f,0x40,0x40,0x40,0x3f},
	{0x1f,0x20,0x40,0x20,0x1f}, {0x7f,0x20,0x18,0x20,0x7f}, {0x63,0x14,0x08,0x14,0x63},
	{0x03,0x04,0x78,0x04,0x03}, {0x61,0x51,0x49,0x45,0x43}, {0x00,0x7f,0x41,0x41,0x00},
	{0x02,0x04,0x08,0x10,0x20}, {0x00,0x41,0x41,0x7f,0x00}, {0x04,0x02,0x01,0x02,0x04},
	{0x40,0x40,0x40,0x40,0x40}, {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78},
	{0x7f,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20}, {0x38,0x44,0x44,0x48,0x7f},
	{0x38,0x54,0x54,0x54,0x18}, {0x08,0x7e,0x09,0x01,0x02}, {0x0c,0x52,0x52,0x52,0x3e},
	{0x7f,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7d,0x40,0x00}, {0x20,0x40,0x44,0x3d,0x00},
	{0x7f,0x10,0x28,0x44,0x00}, {0x00,0x41,0x7f,0x40,0x00}, {0x7c,0x04,0x18,0x04,0x78},
	{0x7c,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38}, {0x7c,0x14,0x14,0x14,0x08},
	{0x08,0x14,0x14,0x18,0x7c}, {0x7c,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20},
	{0x04,0x3f,0x44,0x40,0x20}, {0x3c,0x40,0x40,0x20,0x7c}, {0x1c,0x20,0x40,0x20,0x1c},
	{0x3c,0x40,0x30,0x40,0x3c}, {0x44,0x28,0x10,0x28,0x44}, {0x0c,0x50,0x50,0x50,0x3c},
	{0x44,0x64,0x54,0x4c,0x44}, {0x00,0x08,0x36,0x41,0x00}, {0x00,0x00,0x7f,0x00,0x00},
	{0x00,0x41,0x36,0x08,0x00}, {0x08,0x04,0x08,0x10,0x08}
};

static void draw_char_fallback(int x, int y, char c, Uint32 color, int scale)
{
	if (c < 32 || c > 126) return;
	const unsigned char* glyph = font5x7[c - 32];
	SDL_Rect pixel;
	pixel.w = scale;
	pixel.h = scale;
	for (int col = 0; col < 5; col++)
	{
		unsigned char bits = glyph[col];
		for (int row = 0; row < 7; row++)
		{
			if (bits & (1 << row))
			{
				pixel.x = x + col * scale;
				pixel.y = y + row * scale;
				SDL_FillRect(screen, &pixel, color);
			}
		}
	}
}

static void draw_text_fallback(int x, int y, const char* text, Uint32 color, int scale)
{
	int cx = x;
	for (size_t i = 0; text[i] != '\0'; i++)
	{
		if (text[i] == '\n')
		{
			cx = x;
			y += 8 * scale;
			continue;
		}
		draw_char_fallback(cx, y, text[i], color, scale);
		cx += 6 * scale;
	}
}

static void draw_text(int x, int y, const char* text, Uint32 color, int scale)
{
	if (!text || !text[0]) return;

#if defined(__APPLE__) && !defined(BUILD_FOR_10_4)
	unsigned char r, g, b, a;
	SDL_GetRGBA(color, screen->format, &r, &g, &b, &a);

#if defined(USE_SDL_TTF)
	TTF_Font* ttf = (scale > 1) ? g_ttf_large : g_ttf_normal;
	if (ttf) {
		SDL_Color fc; fc.r = r; fc.g = g; fc.b = b; fc.unused = 0;
		SDL_Surface* surf = TTF_RenderUTF8_Blended(ttf, text, fc);
		if (surf) {
			SDL_SetAlpha(surf, SDL_SRCALPHA, 255);
			SDL_Rect dst_r; dst_r.x = x; dst_r.y = y; dst_r.w = surf->w; dst_r.h = surf->h;
			SDL_BlitSurface(surf, NULL, screen, &dst_r);
			SDL_FreeSurface(surf);
		}
		return;
	}
#endif

	CFStringRef cfText = CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8);
	if (!cfText) return;

	/* Render at 2× size and box-filter down for crisp antialiasing. */
	const int RSCALE = 2;
	CTFontRef font = CTFontCreateWithName(CFSTR("Helvetica Neue"), 15.0f * (float)scale * (float)RSCALE, NULL);
	if (!font) { CFRelease(cfText); return; }

	CGFloat comps[4] = { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
	CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
	CGColorRef cgColor = CGColorCreate(cs, comps);

	CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFDictionarySetValue(attrs, kCTFontAttributeName, font);
	CFDictionarySetValue(attrs, kCTForegroundColorAttributeName, cgColor);

	CFAttributedStringRef attrStr = CFAttributedStringCreate(NULL, cfText, attrs);
	CTLineRef line = CTLineCreateWithAttributedString(attrStr);

	CGFloat ascent = 0, descent = 0, leading = 0;
	double width = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
	size_t w = (size_t)ceil(width);
	size_t h = (size_t)ceil(ascent + descent);

	if (w == 0 || h == 0)
	{
		CFRelease(line);
		CFRelease(attrStr);
		CFRelease(attrs);
		CGColorRelease(cgColor);
		CGColorSpaceRelease(cs);
		CFRelease(font);
		CFRelease(cfText);
		return;
	}

	size_t bytes_per_row = w * 4;
	unsigned char* data = (unsigned char*)calloc(h, bytes_per_row);
	if (!data)
	{
		CFRelease(line);
		CFRelease(attrStr);
		CFRelease(attrs);
		CGColorRelease(cgColor);
		CGColorSpaceRelease(cs);
		CFRelease(font);
		CFRelease(cfText);
		return;
	}

	CGContextRef ctx = CGBitmapContextCreate(data, w, h, 8, bytes_per_row, cs, kCGImageAlphaPremultipliedLast);
	if (!ctx)
	{
		free(data);
		CFRelease(line);
		CFRelease(attrStr);
		CFRelease(attrs);
		CGColorRelease(cgColor);
		CGColorSpaceRelease(cs);
		CFRelease(font);
		CFRelease(cfText);
		return;
	}

	CGContextTranslateCTM(ctx, 0, (CGFloat)h);
	CGContextScaleCTM(ctx, 1.0, -1.0);
	CGContextSetShouldAntialias(ctx, true);
	CGContextSetAllowsAntialiasing(ctx, true);
	CGContextSetShouldSmoothFonts(ctx, true);
	CGContextSetAllowsFontSmoothing(ctx, true);
	CGContextSetTextPosition(ctx, 0, descent);
	CTLineDraw(line, ctx);
	CGContextRelease(ctx);

	/* Box-filter downsample RSCALE:1 */
	int ow = (int)w / RSCALE;
	int oh = (int)h / RSCALE;
	if (ow < 1) ow = 1;
	if (oh < 1) oh = 1;
	size_t dst_bpr = (size_t)ow * 4;
	unsigned char* ds = (unsigned char*)calloc((size_t)oh, dst_bpr);
	if (ds)
	{
		for (int dy = 0; dy < oh; dy++) {
			for (int dx = 0; dx < ow; dx++) {
				int rr=0, gg=0, bb=0, aa=0;
				for (int oy = 0; oy < RSCALE; oy++) {
					const unsigned char* row = data + ((size_t)(dy*RSCALE+oy)) * bytes_per_row;
					for (int ox = 0; ox < RSCALE; ox++) {
						const unsigned char* p = row + (size_t)(dx*RSCALE+ox)*4;
						rr+=p[0]; gg+=p[1]; bb+=p[2]; aa+=p[3];
					}
				}
				int n = RSCALE*RSCALE;
				unsigned char* d = ds + (size_t)dy*dst_bpr + (size_t)dx*4;
				d[0]=(unsigned char)(rr/n); d[1]=(unsigned char)(gg/n);
				d[2]=(unsigned char)(bb/n); d[3]=(unsigned char)(aa/n);
			}
		}
		/* CoreText renders premultiplied alpha; SDL_SRCALPHA expects straight alpha. */
		for (int i = 0; i < ow * oh; i++) {
			unsigned char* p = ds + i * 4;
			unsigned char a = p[3];
			if (a > 0 && a < 255) {
				p[0] = (unsigned char)(SDL_min(255u, (unsigned)p[0] * 255u / a));
				p[1] = (unsigned char)(SDL_min(255u, (unsigned)p[1] * 255u / a));
				p[2] = (unsigned char)(SDL_min(255u, (unsigned)p[2] * 255u / a));
			}
		}
	}

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	Uint32 rmask = 0xff000000;
	Uint32 gmask = 0x00ff0000;
	Uint32 bmask = 0x0000ff00;
	Uint32 amask = 0x000000ff;
#else
	Uint32 rmask = 0x000000ff;
	Uint32 gmask = 0x0000ff00;
	Uint32 bmask = 0x00ff0000;
	Uint32 amask = 0xff000000;
#endif
	{
		unsigned char* surf_data = ds ? ds : data;
		int surf_w = ds ? ow : (int)w;
		int surf_h = ds ? oh : (int)h;
		int surf_pitch = ds ? (int)dst_bpr : (int)bytes_per_row;
		SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(surf_data, surf_w, surf_h, 32, surf_pitch, rmask, gmask, bmask, amask);
		if (surf)
		{
			SDL_SetAlpha(surf, SDL_SRCALPHA, 255);
			SDL_Rect dst_r;
			dst_r.x = x;
			dst_r.y = y;
			SDL_BlitSurface(surf, NULL, screen, &dst_r);
			SDL_FreeSurface(surf);
		}
	}
	free(ds);
	free(data);

	CFRelease(line);
	CFRelease(attrStr);
	CFRelease(attrs);
	CGColorRelease(cgColor);
	CGColorSpaceRelease(cs);
	CFRelease(font);
	CFRelease(cfText);
#else
	draw_text_fallback(x, y, text, color, scale);
#endif
}

static int point_in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
	return (x >= rx && x <= rx + rw && y >= ry && y <= ry + rh);
}

static void draw_button(int x, int y, int w, int h, const char* label, Uint32 bg, Uint32 fg, int pressed)
{
	Uint32 shadow = SDL_MapRGB(screen->format, 15, 19, 31);
	Uint32 border = pressed ? SDL_MapRGB(screen->format, 175, 110, 20) : SDL_MapRGB(screen->format, 80, 99, 133);
	Uint32 body = pressed ? SDL_MapRGB(screen->format, 60, 45, 15) : bg;
	fill_rect(x + 2, y + 2, w, h, shadow);
	fill_rect(x, y, w, h, border);
	fill_rect(x + 1, y + 1, w - 2, h - 2, body);
#if defined(USE_SDL_TTF)
	int text_w = 0, _th = 0;
	if (g_ttf_normal) TTF_SizeUTF8(g_ttf_normal, label, &text_w, &_th);
	else text_w = (int)strlen(label) * 8;
#else
	int text_w = (int)strlen(label) * 8;
#endif
	int tx = x + (w - text_w) / 2;
	if (tx < x + 8) tx = x + 8;
	draw_text_fit(tx, y + (h - 16) / 2 + (pressed ? 1 : 0), label, fg, 1, w - 16);
}

static const char* path_ext(const char* path)
{
	const char* ext = strrchr(path, '.');
	return ext ? ext : "";
}

static int ext_equals(const char* ext, const char* needle)
{
	return (strcasecmp(ext, needle) == 0);
}

static int is_image_path(const char* path)
{
	const char* ext = path_ext(path);
	return ext_equals(ext, ".png") || ext_equals(ext, ".jpg") || ext_equals(ext, ".jpeg");
}

static int is_text_path(const char* path)
{
	const char* ext = path_ext(path);
	return ext_equals(ext, ".txt") || ext_equals(ext, ".csv") || ext_equals(ext, ".md") ||
	       ext_equals(ext, ".tex") || ext_equals(ext, ".bib");
}

static int is_pdf_path(const char* path)
{
	return ext_equals(path_ext(path), ".pdf");
}

static int is_supported_path(const char* path)
{
	return is_image_path(path) || is_text_path(path) || is_pdf_path(path) ||
	       ext_equals(path_ext(path), ".mp4");
}

/* Returns display label (uppercase, no dot) for any supported file type. */
static void get_ext_label(const char* path, char* out, size_t cap)
{
	const char* ext = path_ext(path);
	if (ext && ext[0] == '.') ext++;
	size_t i;
	for (i = 0; ext && ext[i] && i + 1 < cap; i++)
		out[i] = (char)toupper((unsigned char)ext[i]);
	out[i] = '\0';
}

/* Fill *bg and *fg with badge colors for the file type. */
static void get_doc_badge_colors(const char* path, Uint32* bg, Uint32* fg)
{
	const char* ext = path_ext(path);
	if (ext_equals(ext, ".pdf"))
		{ *bg = SDL_MapRGB(screen->format, 150, 35, 25);  *fg = SDL_MapRGB(screen->format, 255, 215, 205); }
	else if (ext_equals(ext, ".csv"))
		{ *bg = SDL_MapRGB(screen->format, 25, 95, 45);   *fg = SDL_MapRGB(screen->format, 195, 245, 210); }
	else if (ext_equals(ext, ".txt"))
		{ *bg = SDL_MapRGB(screen->format, 50, 70, 115);  *fg = SDL_MapRGB(screen->format, 205, 215, 245); }
	else if (ext_equals(ext, ".md"))
		{ *bg = SDL_MapRGB(screen->format, 45, 45, 75);   *fg = SDL_MapRGB(screen->format, 195, 195, 240); }
	else if (ext_equals(ext, ".tex") || ext_equals(ext, ".bib"))
		{ *bg = SDL_MapRGB(screen->format, 90, 60, 20);   *fg = SDL_MapRGB(screen->format, 255, 225, 170); }
	else if (ext_equals(ext, ".mp4"))
		{ *bg = SDL_MapRGB(screen->format, 40, 40, 55);   *fg = SDL_MapRGB(screen->format, 180, 180, 210); }
	else
		{ *bg = SDL_MapRGB(screen->format, 50, 48, 44);   *fg = SDL_MapRGB(screen->format, 190, 185, 175); }
}

static void set_file_type_from_path(char* dst, size_t cap, const char* path)
{
	const char* ext = path_ext(path);
	if (!ext || ext[0] == '\0') { dst[0] = '\0'; return; }
	if (ext[0] == '.') ext++;
	size_t len = strlen(ext);
	if (len >= cap) len = cap - 1;
	for (size_t i = 0; i < len; i++)
		dst[i] = (char)tolower((unsigned char)ext[i]);
	dst[len] = '\0';
}

#if defined(__MACOS__) && !defined(BUILD_FOR_10_4)
static void qt_atexit_wrapper(void)
{
	if (g_jpeg_gi) { CloseComponent(g_jpeg_gi); g_jpeg_gi = NULL; }
	ExitMovies();
}

static void init_quicktime(void)
{
	long qt_version = 0;
	if (Gestalt(gestaltQuickTimeVersion, &qt_version) != noErr || qt_version == 0)
		return;
	if (EnterMovies() != noErr)
		return;
	g_quicktime_ready = 1;
	atexit(qt_atexit_wrapper);
}

static SDL_Surface* load_image_qt_jpeg(const FSSpec* spec)
{
	GWorldPtr gworld = NULL;
	SDL_Surface* surf = NULL;
	OSErr err;

	if (g_jpeg_gi == NULL)
	{
		/* First call: let QT find the right importer for this file type.
		   Expensive (component scan), but only happens once. */
		err = GetGraphicsImporterForFile(spec, &g_jpeg_gi);
		if (err != noErr || g_jpeg_gi == NULL)
		{
			g_jpeg_gi = NULL;
			set_image_error_code("QT: GetGraphicsImporterForFile failed", (int)err);
			return NULL;
		}
	}
	else
	{
		/* Subsequent calls: just point the cached importer at the new file. */
		err = GraphicsImportSetDataFile(g_jpeg_gi, spec);
		if (err != noErr)
		{
			set_image_error_code("QT: SetDataFile failed", (int)err);
			return NULL;
		}
	}

	Rect bounds;
	memset(&bounds, 0, sizeof(bounds));
	if (GraphicsImportGetNaturalBounds(g_jpeg_gi, &bounds) != noErr)
	{
		set_image_error("QT: GetNaturalBounds failed");
		return NULL;
	}

	int img_w = bounds.right - bounds.left;
	int img_h = bounds.bottom - bounds.top;
	if (img_w <= 0 || img_h <= 0)
	{
		set_image_error("QT: bad image dimensions");
		return NULL;
	}

	Rect gw_bounds;
	gw_bounds.top    = 0;
	gw_bounds.left   = 0;
	gw_bounds.bottom = (short)img_h;
	gw_bounds.right  = (short)img_w;

	err = NewGWorld(&gworld, 32, &gw_bounds, NULL, NULL, 0);
	if (err != noErr || gworld == NULL)
	{
		set_image_error_code("QT: NewGWorld failed", (int)err);
		return NULL;
	}

	GraphicsImportSetGWorld(g_jpeg_gi, (CGrafPtr)gworld, GetGWorldDevice(gworld));
	GraphicsImportSetBoundsRect(g_jpeg_gi, &gw_bounds);

	PixMapHandle pm = GetGWorldPixMap(gworld);
	LockPixels(pm);
	err = GraphicsImportDraw(g_jpeg_gi);
	if (err != noErr)
	{
		UnlockPixels(pm);
		DisposeGWorld(gworld);
		set_image_error_code("QT: GraphicsImportDraw failed", (int)err);
		return NULL;
	}

	Ptr base = GetPixBaseAddr(pm);
	long rowbytes = GetPixRowBytes(pm) & 0x3FFF;

	/* GWorld pixel format on big-endian Mac is ARGB: [A][R][G][B] per pixel.
	   SDL big-endian RGBA masks expect [R][G][B][A] in memory. Rotate bytes left. */
	surf = SDL_CreateRGBSurface(0, img_w, img_h, 32,
		0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff);
	if (surf)
	{
		SDL_LockSurface(surf);
		int row, col;
		for (row = 0; row < img_h; row++)
		{
			const unsigned char* src_row = (const unsigned char*)base + row * rowbytes;
			unsigned char* dst_row = (unsigned char*)surf->pixels + row * surf->pitch;
			for (col = 0; col < img_w; col++)
			{
				dst_row[0] = src_row[1]; /* R */
				dst_row[1] = src_row[2]; /* G */
				dst_row[2] = src_row[3]; /* B */
				dst_row[3] = src_row[0]; /* A */
				src_row += 4;
				dst_row += 4;
			}
		}
		SDL_UnlockSurface(surf);
		clear_image_error();
	}
	else
	{
		set_image_error("QT: SDL surface creation failed");
	}

	UnlockPixels(pm);
	DisposeGWorld(gworld);
	return surf;
}
#endif /* __MACOS__ && !BUILD_FOR_10_4 */

static SDL_Surface* load_image_surface(const char* path, int max_w, int max_h)
{
	(void)max_w;
	(void)max_h;

	unsigned char* file_buf = NULL;
	long fsize = 0;

#if defined(__MACOS__) && !defined(BUILD_FOR_10_4)
	/* Classic Mac OS: read via Mac Toolbox (HFS paths use colons) */
	const char* leaf = path_leaf_name(path);
	unsigned char name_pstr[256];
	FSSpec spec;
	short ref = 0;
	long eof = 0;

	if (!ensure_data_dir())
	{
		set_image_error("IMG: storage not ready");
		return NULL;
	}
	set_image_error("IMG: unknown failure");

	if (!cstr_to_pstr(leaf, name_pstr, sizeof(name_pstr)))
	{
		set_image_error("IMG: filename too long");
		return NULL;
	}

	OSErr err = FSMakeFSSpec(storage_vrefnum, storage_dirid, (ConstStr255Param)name_pstr, &spec);
	if (err != noErr)
	{
		set_image_error_code("FSMakeFSSpec failed", (int)err);
		return NULL;
	}

	/* Try QuickTime first for JPEG — much faster than stb_image on slow CPUs */
	if (g_quicktime_ready)
	{
		const char* ext = path_ext(path);
		if (ext_equals(ext, ".jpg") || ext_equals(ext, ".jpeg"))
		{
			SDL_Surface* qt_surf = load_image_qt_jpeg(&spec);
			if (qt_surf) return qt_surf;
		}
	}

	err = FSpOpenDF(&spec, fsRdPerm, &ref);
	if (err != noErr)
	{
		set_image_error_code("Open failed", (int)err);
		return NULL;
	}

	err = GetEOF(ref, &eof);
	if (err != noErr || eof <= 0)
	{
		FSClose(ref);
		set_image_error("IMG: empty file");
		return NULL;
	}

#if defined(__m68k__)
	/* stb_image fallback: cap file size for PNGs (JPEG handled by QuickTime above) */
	if (eof > 256L * 1024L)
	{
		FSClose(ref);
		set_image_error("IMG: file too large for m68k stb fallback");
		return NULL;
	}
#endif

	file_buf = (unsigned char*)malloc((size_t)eof);
	if (!file_buf)
	{
		FSClose(ref);
		set_image_error("IMG: out of memory");
		return NULL;
	}

	long remaining = eof;
	unsigned char* wptr = file_buf;
	while (remaining > 0)
	{
		long chunk = remaining;
		err = FSRead(ref, &chunk, wptr);
		if (err != noErr && err != eofErr)
		{
			FSClose(ref);
			free(file_buf);
			set_image_error_code("Read failed", (int)err);
			return NULL;
		}
		wptr += chunk;
		remaining -= chunk;
		if (err == eofErr) break;
	}
	FSClose(ref);
	fsize = eof;

#else
	/* All other builds: standard POSIX I/O */
	FILE* f = fopen(path, "rb");
	if (!f)
	{
		set_image_error("IMG: failed to open file");
		return NULL;
	}
	if (fseek(f, 0, SEEK_END) != 0)
	{
		fclose(f);
		set_image_error("IMG: seek failed");
		return NULL;
	}
	fsize = ftell(f);
	rewind(f);
	if (fsize <= 0)
	{
		fclose(f);
		set_image_error("IMG: empty file");
		return NULL;
	}
	file_buf = (unsigned char*)malloc((size_t)fsize);
	if (!file_buf)
	{
		fclose(f);
		set_image_error("IMG: out of memory");
		return NULL;
	}
	if ((long)fread(file_buf, 1, (size_t)fsize, f) != fsize)
	{
		fclose(f);
		free(file_buf);
		set_image_error("IMG: read error");
		return NULL;
	}
	fclose(f);

#endif /* __MACOS__ && !BUILD_FOR_10_4 */

#if defined(__m68k__)
	{
		int _iw = 0, _ih = 0, _ic = 0;
		if (!stbi_info_from_memory(file_buf, (int)fsize, &_iw, &_ih, &_ic)
			|| (long)_iw * _ih > 160L * 120L)
		{
			free(file_buf);
			set_image_error("IMG: image too large for m68k (max 160x120 px)");
			return NULL;
		}
	}
#endif

	int w = 0, h = 0, n = 0;
	unsigned char* pixels = stbi_load_from_memory(file_buf, (int)fsize, &w, &h, &n, 4);
	free(file_buf);
#if defined(BUILD_FOR_11_0) || defined(BUILD_FOR_10_7)
	/* sdl12-compat on Apple Silicon flips y when presenting via Metal.
	   CoreText compensates via CTM; flip image rows here to match. */
	if (pixels && h > 1) {
		unsigned char* tmp = (unsigned char*)malloc((size_t)w * 4);
		if (tmp) {
			for (int row = 0; row < h / 2; row++) {
				unsigned char* top = pixels + row * w * 4;
				unsigned char* bot = pixels + (h - 1 - row) * w * 4;
				memcpy(tmp, top, (size_t)w * 4);
				memcpy(top, bot, (size_t)w * 4);
				memcpy(bot, tmp, (size_t)w * 4);
			}
			free(tmp);
		}
	}
#endif
	if (!pixels || w <= 0 || h <= 0)
	{
		const char* reason = stbi_failure_reason();
		char msg[256];
		snprintf(msg, sizeof(msg), "IMG: decode failed (%s)", reason ? reason : "unknown");
		set_image_error(msg);
		if (pixels) stbi_image_free(pixels);
		return NULL;
	}

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	Uint32 rmask = 0xff000000;
	Uint32 gmask = 0x00ff0000;
	Uint32 bmask = 0x0000ff00;
	Uint32 amask = 0x000000ff;
#else
	Uint32 rmask = 0x000000ff;
	Uint32 gmask = 0x0000ff00;
	Uint32 bmask = 0x00ff0000;
	Uint32 amask = 0xff000000;
#endif
	SDL_Surface* surf = SDL_CreateRGBSurface(0, w, h, 32, rmask, gmask, bmask, amask);
	if (!surf)
	{
		stbi_image_free(pixels);
		set_image_error("IMG: SDL surface creation failed");
		return NULL;
	}

	SDL_LockSurface(surf);
	for (int row = 0; row < h; row++)
	{
		unsigned char* dst = (unsigned char*)surf->pixels + row * surf->pitch;
		unsigned char* src = pixels + row * w * 4;
		memcpy(dst, src, (size_t)w * 4);
	}
	SDL_UnlockSurface(surf);
	stbi_image_free(pixels);
	clear_image_error();
	return surf;
}

static SDL_Surface* scale_surface(SDL_Surface* src, int max_w, int max_h)
{
	if (!src) return NULL;
	float sx = (float)max_w / (float)src->w;
	float sy = (float)max_h / (float)src->h;
	float s = (sx < sy) ? sx : sy;
	if (s >= 1.0f) return src;
	int nw = (int)(src->w * s);
	int nh = (int)(src->h * s);
	if (nw < 1) nw = 1;
	if (nh < 1) nh = 1;
	SDL_Surface* dst = SDL_CreateRGBSurface(0, nw, nh,
		src->format->BitsPerPixel, src->format->Rmask,
		src->format->Gmask, src->format->Bmask, src->format->Amask);
	if (!dst) return src;
#if defined(__APPLE__)
	{
		int bpp = src->format->BytesPerPixel;
		if (SDL_MUSTLOCK(src)) SDL_LockSurface(src);
		SDL_LockSurface(dst);
		for (int dy = 0; dy < nh; dy++) {
			float fy = (dy + 0.5f) * (float)src->h / (float)nh - 0.5f;
			int sy0 = (int)fy; if (sy0 < 0) sy0 = 0; if (sy0 >= src->h) sy0 = src->h-1;
			int sy1 = sy0+1;   if (sy1 >= src->h) sy1 = src->h-1;
			float wy = fy - (float)sy0;
			for (int dx = 0; dx < nw; dx++) {
				float fx = (dx + 0.5f) * (float)src->w / (float)nw - 0.5f;
				int sx0 = (int)fx; if (sx0 < 0) sx0 = 0; if (sx0 >= src->w) sx0 = src->w-1;
				int sx1 = sx0+1;   if (sx1 >= src->w) sx1 = src->w-1;
				float wx = fx - (float)sx0;
				const Uint8* p00 = (const Uint8*)src->pixels + sy0*src->pitch + sx0*bpp;
				const Uint8* p10 = (const Uint8*)src->pixels + sy0*src->pitch + sx1*bpp;
				const Uint8* p01 = (const Uint8*)src->pixels + sy1*src->pitch + sx0*bpp;
				const Uint8* p11 = (const Uint8*)src->pixels + sy1*src->pitch + sx1*bpp;
				Uint8* d = (Uint8*)dst->pixels + dy*dst->pitch + dx*bpp;
				for (int c = 0; c < bpp; c++) {
					float v = (float)p00[c]*(1.0f-wx)*(1.0f-wy)
					        + (float)p10[c]*wx*(1.0f-wy)
					        + (float)p01[c]*(1.0f-wx)*wy
					        + (float)p11[c]*wx*wy;
					d[c] = (Uint8)(v + 0.5f);
				}
			}
		}
		SDL_UnlockSurface(dst);
		if (SDL_MUSTLOCK(src)) SDL_UnlockSurface(src);
	}
#else
	SDL_SoftStretch(src, NULL, dst, NULL);
#endif
	return dst;
}

/* ====================================================================== */
/* Storage model */

typedef struct Meme {
	char id[SMALL_MAX_LEN];
	char filename[PATH_MAX_LEN];
	char source_status[SMALL_MAX_LEN];
	char source_detail[PATH_MAX_LEN];
	char tags[TAGS_MAX_LEN];
	char text[TEXT_MAX_LEN];
	char file_type[SMALL_MAX_LEN];
	char situation[SMALL_MAX_LEN];
} Meme;

typedef struct MemeList {
	Meme items[MEME_MAX];
	int count;
} MemeList;

static char data_dir[PATH_MAX_LEN];
static char index_path[PATH_MAX_LEN];
static short storage_vrefnum = 0;
static long storage_dirid = 0;
static int storage_ready = 0;
static char last_storage_error[256];
static char last_image_error[256];

static void init_storage_paths(void)
{
	if (data_dir[0] != '\0') return;
#if defined(__MACOS__) && !defined(BUILD_FOR_10_4)
	char vol_name[256];
	short vref = 0;
	long dirid = 0;
	vol_name[0] = '\0';
	if (HGetVol((StringPtr)vol_name, &vref, &dirid) == noErr && vol_name[0] != '\0')
	{
		char vol_cstr[256];
		pstr_to_cstr((const unsigned char*)vol_name, vol_cstr, sizeof(vol_cstr));
		snprintf(data_dir, sizeof(data_dir), "%s:memebooru_data", vol_cstr);
		build_storage_path(index_path, sizeof(index_path), data_dir, "memes.txt");
		storage_vrefnum = vref;
		storage_dirid = fsRtDirID;
		return;
	}
#endif
	const char* home = getenv("HOME");
	if (home && home[0] != '\0')
		snprintf(data_dir, sizeof(data_dir), "%s/%s", home, "memebooru_data");
	else
		snprintf(data_dir, sizeof(data_dir), "%s", "memebooru_data");
	build_storage_path(index_path, sizeof(index_path), data_dir, "memes.txt");
}

static const char* get_data_dir(void)
{
	init_storage_paths();
	return data_dir;
}

static const char* get_index_path(void)
{
	init_storage_paths();
	return index_path;
}

static int ensure_data_dir(void)
{
	const char* dir = get_data_dir();
	struct stat st;
	if (stat(dir, &st) == 0) return 1;
#if defined(__MACOS__) && !defined(BUILD_FOR_10_4)
	if (storage_ready) return 1;
	FSSpec dir_spec;
	unsigned char folder_name[32];
	long created_dirid = 0;
	folder_name[0] = 13;
	memcpy(folder_name + 1, "memebooru_data", 14);
	if (FSMakeFSSpec(storage_vrefnum, storage_dirid, (ConstStr255Param)folder_name, &dir_spec) == noErr)
	{
		CInfoPBRec cat;
		memset(&cat, 0, sizeof(cat));
		cat.dirInfo.ioNamePtr = (StringPtr)dir_spec.name;
		cat.dirInfo.ioVRefNum = dir_spec.vRefNum;
		cat.dirInfo.ioDrDirID = dir_spec.parID;
		cat.dirInfo.ioFDirIndex = 0;
		if (PBGetCatInfo(&cat, false) == noErr && (cat.dirInfo.ioFlAttrib & ioDirMask))
		{
			storage_dirid = cat.dirInfo.ioDrDirID;
			storage_ready = 1;
			return 1;
		}
	}
	if (FSpDirCreate(&dir_spec, smSystemScript, &created_dirid) == noErr)
	{
		storage_dirid = created_dirid;
		storage_ready = 1;
		return 1;
	}
	snprintf(last_storage_error, sizeof(last_storage_error), "Failed to create storage folder (vref=%d dirid=%ld)", (int)storage_vrefnum, storage_dirid);
	fprintf(stderr, "%s\\n", last_storage_error);
	return 0;
#else
	return (mkdir(dir, 0755) == 0);
#endif
}

static void safe_copy(char* dst, size_t cap, const char* src)
{
	if (!dst || cap == 0) return;
	if (!src) { dst[0] = '\0'; return; }
	strncpy(dst, src, cap - 1);
	dst[cap - 1] = '\0';
}

static void set_image_error(const char* msg)
{
	safe_copy(last_image_error, sizeof(last_image_error), msg);
}

static void set_image_error_code(const char* step, int err)
{
	char buf[256];
	snprintf(buf, sizeof(buf), "IMG: %s (err=%d)", step, err);
	set_image_error(buf);
}

static void clear_image_error(void)
{
	last_image_error[0] = '\0';
}

static void trim_right(char* s)
{
	int len = (int)strlen(s);
	while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ' || s[len - 1] == '\t'))
	{
		s[len - 1] = '\0';
		len--;
	}
}

static void escape_field(const char* in, char* out, size_t cap)
{
	size_t j = 0;
	for (size_t i = 0; in[i] != '\0' && j + 2 < cap; i++)
	{
		char c = in[i];
		if (c == '\\' || c == '|' || c == '\n')
		{
			out[j++] = '\\';
			if (c == '\n') out[j++] = 'n';
			else out[j++] = c;
		}
		else
		{
			out[j++] = c;
		}
	}
	out[j] = '\0';
}

static void unescape_field(const char* in, char* out, size_t cap)
{
	size_t j = 0;
	for (size_t i = 0; in[i] != '\0' && j + 1 < cap; i++)
	{
		if (in[i] == '\\')
		{
			char n = in[i + 1];
			if (n == 'n') { out[j++] = '\n'; i++; }
			else if (n == '|' || n == '\\') { out[j++] = n; i++; }
			else { out[j++] = in[i]; }
		}
		else
		{
			out[j++] = in[i];
		}
	}
	out[j] = '\0';
}

static int split_fields(const char* line, char fields[][TEXT_MAX_LEN], int max_fields)
{
	int field = 0;
	int pos = 0;
	for (size_t i = 0; line[i] != '\0' && field < max_fields; i++)
	{
		char c = line[i];
		if (c == '\\')
		{
			if (line[i + 1] != '\0' && pos + 1 < TEXT_MAX_LEN)
			{
				fields[field][pos++] = c;
				fields[field][pos++] = line[i + 1];
				i++;
			}
			continue;
		}
		if (c == '|')
		{
			fields[field][pos] = '\0';
			field++;
			pos = 0;
			continue;
		}
		if (pos + 1 < TEXT_MAX_LEN)
			fields[field][pos++] = c;
	}
	if (field < max_fields)
		fields[field][pos] = '\0';
	return field + 1;
}

static int storage_load(MemeList* list)
{
	list->count = 0;
#if defined(__MACOS__) && !defined(BUILD_FOR_10_4)
	FSSpec index_spec;
	if (!storage_leaf_spec(storage_vrefnum, storage_dirid, "memes.txt", &index_spec))
		return 1;
	short ref = 0;
	if (FSpOpenDF(&index_spec, fsRdPerm, &ref) != noErr)
		return 1;
	long eof = 0;
	if (GetEOF(ref, &eof) != noErr || eof <= 0)
	{
		FSClose(ref);
		return 1;
	}
	char* buffer = (char*)malloc((size_t)eof + 1);
	if (!buffer)
	{
		FSClose(ref);
		return 0;
	}
	long remaining = eof;
	char* out = buffer;
	while (remaining > 0)
	{
		long chunk = remaining;
		OSErr err = FSRead(ref, &chunk, out);
		if (err != noErr && err != eofErr)
		{
			free(buffer);
			FSClose(ref);
			return 1;
		}
		out += chunk;
		remaining -= chunk;
		if (err == eofErr) break;
	}
	*out = '\0';
	FSClose(ref);

	char line[2048];
	size_t pos = 0;
	for (char* p = buffer; *p != '\0'; p++)
	{
		if (*p == '\r' || *p == '\n')
		{
			if (pos > 0)
			{
				line[pos] = '\0';
				char fields[8][TEXT_MAX_LEN];
				int count = split_fields(line, fields, 8);
				if (count >= 8 && list->count < MEME_MAX)
				{
					Meme* m = &list->items[list->count++];
					unescape_field(fields[0], m->id, sizeof(m->id));
					unescape_field(fields[1], m->filename, sizeof(m->filename));
					unescape_field(fields[2], m->source_status, sizeof(m->source_status));
					unescape_field(fields[3], m->source_detail, sizeof(m->source_detail));
					unescape_field(fields[4], m->tags, sizeof(m->tags));
					unescape_field(fields[5], m->text, sizeof(m->text));
					unescape_field(fields[6], m->file_type, sizeof(m->file_type));
					unescape_field(fields[7], m->situation, sizeof(m->situation));
				}
				pos = 0;
			}
			continue;
		}
		if (pos + 1 < sizeof(line))
			line[pos++] = *p;
	}
	if (pos > 0 && list->count < MEME_MAX)
	{
		line[pos] = '\0';
		char fields[8][TEXT_MAX_LEN];
		int count = split_fields(line, fields, 8);
		if (count >= 8)
		{
			Meme* m = &list->items[list->count++];
			unescape_field(fields[0], m->id, sizeof(m->id));
			unescape_field(fields[1], m->filename, sizeof(m->filename));
			unescape_field(fields[2], m->source_status, sizeof(m->source_status));
			unescape_field(fields[3], m->source_detail, sizeof(m->source_detail));
			unescape_field(fields[4], m->tags, sizeof(m->tags));
			unescape_field(fields[5], m->text, sizeof(m->text));
			unescape_field(fields[6], m->file_type, sizeof(m->file_type));
			unescape_field(fields[7], m->situation, sizeof(m->situation));
		}
	}
	free(buffer);
	return 1;
#else
	FILE* f = fopen(get_index_path(), "r");
	if (!f) return 1;
	char line[2048];
	while (fgets(line, sizeof(line), f))
	{
		trim_right(line);
		if (line[0] == '\0') continue;
		char fields[8][TEXT_MAX_LEN];
		int count = split_fields(line, fields, 8);
		if (count < 8) continue;
		if (list->count >= MEME_MAX) break;

		Meme* m = &list->items[list->count++];
		unescape_field(fields[0], m->id, sizeof(m->id));
		unescape_field(fields[1], m->filename, sizeof(m->filename));
		unescape_field(fields[2], m->source_status, sizeof(m->source_status));
		unescape_field(fields[3], m->source_detail, sizeof(m->source_detail));
		unescape_field(fields[4], m->tags, sizeof(m->tags));
		unescape_field(fields[5], m->text, sizeof(m->text));
		unescape_field(fields[6], m->file_type, sizeof(m->file_type));
		unescape_field(fields[7], m->situation, sizeof(m->situation));
	}
	fclose(f);
	return 1;
#endif
}

static int storage_save(const MemeList* list)
{
#if defined(__MACOS__) && !defined(BUILD_FOR_10_4)
	FSSpec index_spec;
	unsigned char pstr[256];
	short ref = 0;
	if (!cstr_to_pstr("memes.txt", pstr, sizeof(pstr)))
		return 0;
	OSErr err = FSMakeFSSpec(storage_vrefnum, storage_dirid, (ConstStr255Param)pstr, &index_spec);
	if (err == fnfErr)
	{
		err = HCreate(storage_vrefnum, storage_dirid, (ConstStr255Param)pstr, 'MebO', 'TEXT');
		if (err != noErr)
			return 0;
		err = FSMakeFSSpec(storage_vrefnum, storage_dirid, (ConstStr255Param)pstr, &index_spec);
	}
	if (err != noErr)
		return 0;
	if (FSpOpenDF(&index_spec, fsWrPerm, &ref) != noErr)
		return 0;
	SetEOF(ref, 0);
	for (int i = 0; i < list->count; i++)
	{
		char f0[TEXT_MAX_LEN], f1[TEXT_MAX_LEN], f2[TEXT_MAX_LEN], f3[TEXT_MAX_LEN];
		char f4[TEXT_MAX_LEN], f5[TEXT_MAX_LEN], f6[TEXT_MAX_LEN], f7[TEXT_MAX_LEN];
		char line[4096];
		long written = 0;
		escape_field(list->items[i].id, f0, sizeof(f0));
		escape_field(list->items[i].filename, f1, sizeof(f1));
		escape_field(list->items[i].source_status, f2, sizeof(f2));
		escape_field(list->items[i].source_detail, f3, sizeof(f3));
		escape_field(list->items[i].tags, f4, sizeof(f4));
		escape_field(list->items[i].text, f5, sizeof(f5));
		escape_field(list->items[i].file_type, f6, sizeof(f6));
		escape_field(list->items[i].situation, f7, sizeof(f7));
		snprintf(line, sizeof(line), "%s|%s|%s|%s|%s|%s|%s|%s\r", f0, f1, f2, f3, f4, f5, f6, f7);
		written = (long)strlen(line);
		if (FSWrite(ref, &written, line) != noErr || written != (long)strlen(line))
		{
			FSClose(ref);
			return 0;
		}
	}
	FSClose(ref);
	return 1;
#else
	FILE* f = fopen(get_index_path(), "w");
	if (!f) return 0;
	for (int i = 0; i < list->count; i++)
	{
		char f0[TEXT_MAX_LEN], f1[TEXT_MAX_LEN], f2[TEXT_MAX_LEN], f3[TEXT_MAX_LEN];
		char f4[TEXT_MAX_LEN], f5[TEXT_MAX_LEN], f6[TEXT_MAX_LEN], f7[TEXT_MAX_LEN];
		escape_field(list->items[i].id, f0, sizeof(f0));
		escape_field(list->items[i].filename, f1, sizeof(f1));
		escape_field(list->items[i].source_status, f2, sizeof(f2));
		escape_field(list->items[i].source_detail, f3, sizeof(f3));
		escape_field(list->items[i].tags, f4, sizeof(f4));
		escape_field(list->items[i].text, f5, sizeof(f5));
		escape_field(list->items[i].file_type, f6, sizeof(f6));
		escape_field(list->items[i].situation, f7, sizeof(f7));
		fprintf(f, "%s|%s|%s|%s|%s|%s|%s|%s\n", f0, f1, f2, f3, f4, f5, f6, f7);
	}
	fclose(f);
	return 1;
#endif
}

static int copy_file(const char* src_path, const char* dst_path)
{
#if defined(__MACOS__) && !defined(BUILD_FOR_10_4)
	if (g_browse_spec_valid && src_path && strcmp(src_path, g_browse_path) == 0)
	{
		short src_ref = 0;
		short dst_ref = 0;
		char dst_name[64];
		const char* base = strrchr(dst_path, ':');
		if (!base) base = strrchr(dst_path, '/');
		base = base ? base + 1 : dst_path;
		snprintf(dst_name, sizeof(dst_name), "%s", base);
		if (!ensure_data_dir()) return 0;
		FSSpec dst_spec;
		unsigned char dst_pstr[256];
		size_t len = strlen(dst_name);
		if (len > 255) len = 255;
		dst_pstr[0] = (unsigned char)len;
		memcpy(dst_pstr + 1, dst_name, len);
		if (FSMakeFSSpec(storage_vrefnum, storage_dirid, (ConstStr255Param)dst_pstr, &dst_spec) != noErr)
		{
			if (FSpCreate(&dst_spec, 'MebO', 'JPEG', smSystemScript) != noErr)
			{
				fprintf(stderr, "Failed to create destination file: %s\\n", dst_path);
				return 0;
			}
		}
		if (FSpOpenDF(&dst_spec, fsWrPerm, &dst_ref) != noErr)
		{
			fprintf(stderr, "Failed to open destination for writing: %s\\n", dst_path);
			return 0;
		}
		if (FSpOpenDF(&g_browse_spec, fsRdPerm, &src_ref) != noErr)
		{
			FSClose(dst_ref);
			fprintf(stderr, "Failed to open source for reading: %s\\n", src_path);
			return 0;
		}
		char buf[4096];
		long n;
		OSErr err = noErr;
		do
		{
			n = sizeof(buf);
			err = FSRead(src_ref, &n, buf);
			if (err != noErr && err != eofErr)
			{
				FSClose(src_ref);
				FSClose(dst_ref);
				snprintf(last_storage_error, sizeof(last_storage_error), "Failed reading source while copying (%d)", (int)err);
				fprintf(stderr, "%s: %s\\n", last_storage_error, src_path);
				return 0;
			}
			if (n > 0)
			{
				long wrote = n;
				if (FSWrite(dst_ref, &wrote, buf) != noErr || wrote != n)
				{
					FSClose(src_ref);
					FSClose(dst_ref);
					snprintf(last_storage_error, sizeof(last_storage_error), "Failed writing destination while copying");
					fprintf(stderr, "%s: %s\\n", last_storage_error, dst_path);
					return 0;
				}
			}
		} while (err != eofErr);
		FSClose(src_ref);
		FSClose(dst_ref);
		return 1;
	}
#endif
	FILE* src = fopen(src_path, "rb");
	if (!src)
	{
#if defined(__MACOS__)
		snprintf(last_storage_error, sizeof(last_storage_error), "Failed to open source file");
		fprintf(stderr, "%s: %s\\n", last_storage_error, src_path);
#endif
		return 0;
	}
	FILE* dst = fopen(dst_path, "wb");
	if (!dst)
	{
		fclose(src);
#if defined(__MACOS__)
		snprintf(last_storage_error, sizeof(last_storage_error), "Failed to open destination file");
		fprintf(stderr, "%s: %s\\n", last_storage_error, dst_path);
#endif
		return 0;
	}
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
		fwrite(buf, 1, n, dst);
	fclose(src);
	fclose(dst);
	return 1;
}

static void generate_id(char* out, size_t cap)
{
	unsigned long t = (unsigned long)time(NULL);
	snprintf(out, cap, "%lu", t);
}

/* ====================================================================== */
/* UI and app state */

typedef enum {
	VIEW_LIST,
	VIEW_DETAIL,
	VIEW_ADD
} ViewMode;

typedef enum {
	FOCUS_SEARCH = 0,
	FOCUS_FORM = 1
} InputFocus;

typedef struct {
	MemeList list;
	int selected;
	int list_offset;
	char search[SMALL_MAX_LEN];
	char status_line[TEXT_MAX_LEN];
	ViewMode view;
	int pressed_button;
	InputFocus focus;
	int text_cursor;
	int text_anchor;

	// double-click tracking
	Uint32 last_click_time;
	int last_click_item;

	// add/edit form
	int edit_index;
	int field_index;
	char form_path[PATH_MAX_LEN];
	char form_source_status[SMALL_MAX_LEN];
	char form_source_detail[PATH_MAX_LEN];
	char form_tags[TAGS_MAX_LEN];
	char form_text[TEXT_MAX_LEN];
	char form_file_type[SMALL_MAX_LEN];
	char form_situation[SMALL_MAX_LEN];
	char edit_original_filename[PATH_MAX_LEN];
} AppState;

#define THUMB_CACHE_MAX 32
typedef struct {
	int index;
	char path[PATH_MAX_LEN];
	SDL_Surface* surf;
	unsigned long stamp;
} ThumbCacheEntry;

static ThumbCacheEntry g_thumb_cache[THUMB_CACHE_MAX];
static unsigned long g_thumb_stamp = 1;
static SDL_Surface* g_detail_cache;
static char g_detail_cache_name[PATH_MAX_LEN];
static int g_previews_enabled = 1;

/* Text-preview cache (for TXT/CSV/MD/TEX/BIB detail view) */
#define TEXT_PREVIEW_BUF  8192
#define TEXT_PREVIEW_LINES 256
static char g_text_preview_buf[TEXT_PREVIEW_BUF];
static char g_text_preview_path[PATH_MAX_LEN];
static int  g_text_preview_line_off[TEXT_PREVIEW_LINES]; /* byte offsets into buf */
static int  g_text_preview_line_count;

static const char* source_statuses[] = {
	"unknown",
	"known",
	"unavailable",
	"available"
};

typedef struct UiRect {
	int x;
	int y;
	int w;
	int h;
} UiRect;

static int get_btn_y(void) { return screen->h - STATUS_BAR_H - BTN_H - 10; }
static UiRect get_list_button(int i)   { return (UiRect){BTN_START_X + i*(BTN_W+BTN_GAP), get_btn_y(), BTN_W, BTN_H}; }
static UiRect get_detail_button(int i) { return (UiRect){BTN_START_X + i*(BTN_W+BTN_GAP), get_btn_y(), BTN_W, BTN_H}; }
static UiRect get_add_button(int i)    { return (UiRect){BTN_START_X + i*(BTN_W+BTN_GAP), get_btn_y(), BTN_W, BTN_H}; }
static UiRect get_search_box(void)     { int sx = screen->w / 2 + 30; return (UiRect){sx, 8, screen->w - sx - 16, 34}; }

enum {
	BUTTON_NONE = 0,
	BUTTON_LIST_ADD,
	BUTTON_LIST_EDIT,
	BUTTON_LIST_DELETE,
	BUTTON_LIST_OPEN,
	BUTTON_LIST_QUIT,
	BUTTON_DETAIL_BACK,
	BUTTON_DETAIL_PREV,
	BUTTON_DETAIL_NEXT,
	BUTTON_DETAIL_EDIT,
	BUTTON_DETAIL_DELETE,
	BUTTON_ADD_BROWSE,
	BUTTON_ADD_SAVE,
	BUTTON_ADD_CANCEL
};

static int point_in_uirect(int x, int y, UiRect r)
{
	return point_in_rect(x, y, r.x, r.y, r.w, r.h);
}

static void normalize_mouse(int* mx, int* my)
{
#if defined(BUILD_FOR_11_0) || defined(BUILD_FOR_10_7)
	int ww = g_win_surf ? g_win_surf->w : WINDOW_W;
	int wh = g_win_surf ? g_win_surf->h : WINDOW_H;
	/* During live resize screen and g_win_surf temporarily differ in size;
	 * scale mouse from g_win_surf coords into screen/layout coords. */
	if (screen && (screen->w != ww || screen->h != wh)) {
		*mx = *mx * screen->w / ww;
		*my = *my * screen->h / wh;
	}
	if (*mx < 0) *mx = 0;
	if (*my < 0) *my = 0;
	if (screen && *mx >= screen->w) *mx = screen->w - 1;
	if (screen && *my >= screen->h) *my = screen->h - 1;
#else
	(void)mx; (void)my;
#endif
}

static void draw_card(int x, int y, int w, int h, Uint32 bg, Uint32 border, Uint32 shadow)
{
	fill_rect(x + 2, y + 2, w, h, shadow);
	fill_rect(x, y, w, h, border);
	fill_rect(x + 1, y + 1, w - 2, h - 2, bg);
}

static void draw_text_fit(int x, int y, const char* value, Uint32 color, int scale, int max_px)
{
	if (!value) return;
	char line[TEXT_MAX_LEN];
	size_t len = strlen(value);
#if defined(USE_SDL_TTF)
	int char_px = (scale > 1) ? g_ttf_char_w2 : g_ttf_char_w;
	if (char_px < 1) char_px = 8;
#else
	int char_px = (scale > 1) ? (scale * 7) : 8;
#endif
	int max_chars = max_px / char_px;
	if (max_chars < 1) max_chars = 1;
	if ((int)len > max_chars)
	{
		int out_len = max_chars;
		if (out_len >= (int)sizeof(line)) out_len = (int)sizeof(line) - 1;
		if (out_len >= 4)
		{
			out_len -= 3;
			memcpy(line, value, (size_t)out_len);
			line[out_len] = '.';
			line[out_len + 1] = '.';
			line[out_len + 2] = '.';
			line[out_len + 3] = '\0';
		}
		else
		{
			memcpy(line, value, (size_t)out_len);
			line[out_len] = '\0';
		}
	}
	else
	{
		size_t out_len = len;
		if (out_len >= sizeof(line)) out_len = sizeof(line) - 1;
		memcpy(line, value, out_len);
		line[out_len] = '\0';
	}
	draw_text(x, y, line, color, scale);
}

static void draw_text_value(int x, int y, const char* value, Uint32 color, int max_px)
{
	draw_text_fit(x, y, value, color, 1, max_px);
}

static void draw_editable_text(int x, int y, int max_px, const char* value, int cursor, int anchor, Uint32 fg, Uint32 sel_bg, Uint32 sel_fg, int show_caret)
{
	int len = (int)strlen(value);
#if defined(USE_SDL_TTF)
	int char_px = g_ttf_char_w > 0 ? g_ttf_char_w : 8;
#else
	int char_px = 8;
#endif
	int max_chars = max_px / char_px;
	if (max_chars < 1) max_chars = 1;
	int vis_len = (len < max_chars) ? len : max_chars;
	int sel_a = (cursor < anchor) ? cursor : anchor;
	int sel_b = (cursor < anchor) ? anchor : cursor;
	if (sel_a < 0) sel_a = 0;
	if (sel_b < 0) sel_b = 0;
	if (sel_a > len) sel_a = len;
	if (sel_b > len) sel_b = len;

	char vis[TEXT_MAX_LEN];
	if (vis_len > (int)sizeof(vis) - 1) vis_len = (int)sizeof(vis) - 1;
	memcpy(vis, value, (size_t)vis_len);
	vis[vis_len] = '\0';

	draw_text(x, y, vis, fg, 1);

	if (sel_b > sel_a)
	{
		int draw_a = (sel_a < max_chars) ? sel_a : max_chars;
		int draw_b = (sel_b < max_chars) ? sel_b : max_chars;
		if (draw_b > draw_a)
		{
			fill_rect(x + draw_a * char_px, y - 1, (draw_b - draw_a) * char_px, 14, sel_bg);
			char sel[TEXT_MAX_LEN];
			int slen = draw_b - draw_a;
			if (slen > (int)sizeof(sel) - 1) slen = (int)sizeof(sel) - 1;
			memcpy(sel, value + draw_a, (size_t)slen);
			sel[slen] = '\0';
			draw_text(x + draw_a * char_px, y, sel, sel_fg, 1);
		}
	}

	if (show_caret && ((SDL_GetTicks() / 500) % 2) != 0)
	{
		int caret = cursor;
		if (caret < 0) caret = 0;
		if (caret > max_chars) caret = max_chars;
		fill_rect(x + caret * char_px, y, 2, 12, fg);
	}
}

static UiRect add_field_rect(int index)
{
	int btn_y    = get_btn_y();
	int top      = TOP_BAR_H + 48;
	int path_h   = 38;
	int path_y   = btn_y - path_h - 32;
	int tags_y   = top + 22;
	int tags_h   = path_y - tags_y - 14;
	if (tags_h < 60) tags_h = 60;
	if (index == 0) /* tags */
		return (UiRect){24, tags_y, screen->w - 48, tags_h};
	else /* path */
		return (UiRect){24, path_y, screen->w - 48 - BTN_W - 16, path_h};
}

static UiRect get_add_browse_rect(void)
{
	UiRect p = add_field_rect(1);
	return (UiRect){p.x + p.w + 10, p.y, BTN_W, p.h};
}

static int add_field_at_point(int x, int y)
{
	for (int i = 0; i < 2; i++)
		if (point_in_uirect(x, y, add_field_rect(i))) return i;
	return -1;
}

static int button_id_at_point(ViewMode view, int x, int y)
{
	if (view == VIEW_LIST)
	{
		if (point_in_uirect(x, y, get_list_button(0))) return BUTTON_LIST_ADD;
	}
	else if (view == VIEW_DETAIL)
	{
		for (int i = 0; i < 5; i++)
			if (point_in_uirect(x, y, get_detail_button(i))) return BUTTON_DETAIL_BACK + i;
	}
	else if (view == VIEW_ADD)
	{
		if (point_in_uirect(x, y, get_add_browse_rect())) return BUTTON_ADD_BROWSE;
		if (point_in_uirect(x, y, get_add_button(0)))     return BUTTON_ADD_SAVE;
		if (point_in_uirect(x, y, get_add_button(1)))     return BUTTON_ADD_CANCEL;
	}
	return BUTTON_NONE;
}

static void set_status(AppState* app, const char* msg)
{
	safe_copy(app->status_line, sizeof(app->status_line), msg);
}

static void maybe_report_image_error(AppState* app)
{
	if (!app) return;
	if (last_image_error[0] == '\0')
	{
		if (strncmp(app->status_line, "IMG:", 4) == 0)
			set_status(app, "");
		return;
	}
	set_status(app, last_image_error);
}

static void clear_image_caches(void)
{
	for (int i = 0; i < THUMB_CACHE_MAX; i++)
	{
		if (g_thumb_cache[i].surf)
		{
			SDL_FreeSurface(g_thumb_cache[i].surf);
			g_thumb_cache[i].surf = NULL;
		}
		g_thumb_cache[i].index = -1;
		g_thumb_cache[i].path[0] = '\0';
		g_thumb_cache[i].stamp = 0;
	}
	if (g_detail_cache)
	{
		SDL_FreeSurface(g_detail_cache);
		g_detail_cache = NULL;
	}
	g_detail_cache_name[0] = '\0';
	g_text_preview_path[0] = '\0';
}

static SDL_Surface* get_thumb_cached(int index, const char* path, int max_w, int max_h)
{
	if (!path) return NULL;
	if (index < 0) return NULL;
	for (int i = 0; i < THUMB_CACHE_MAX; i++)
	{
		if (g_thumb_cache[i].surf && g_thumb_cache[i].index == index &&
			strcmp(g_thumb_cache[i].path, path) == 0)
		{
			g_thumb_cache[i].stamp = g_thumb_stamp++;
			return g_thumb_cache[i].surf;
		}
	}

	SDL_Surface* img = load_image_surface(path, max_w, max_h);
	if (img && (img->w > max_w || img->h > max_h))
	{
		SDL_Surface* scaled = scale_surface(img, max_w, max_h);
		if (scaled != img)
		{
			SDL_FreeSurface(img);
			img = scaled;
		}
	}
	if (!img) return NULL;

	int slot = -1;
	unsigned long oldest = 0xffffffff;
	for (int i = 0; i < THUMB_CACHE_MAX; i++)
	{
		if (!g_thumb_cache[i].surf)
		{
			slot = i;
			break;
		}
		if (g_thumb_cache[i].stamp < oldest)
		{
			oldest = g_thumb_cache[i].stamp;
			slot = i;
		}
	}
	if (slot >= 0)
	{
		if (g_thumb_cache[slot].surf)
			SDL_FreeSurface(g_thumb_cache[slot].surf);
		g_thumb_cache[slot].surf = img;
		g_thumb_cache[slot].index = index;
		safe_copy(g_thumb_cache[slot].path, sizeof(g_thumb_cache[slot].path), path);
		g_thumb_cache[slot].stamp = g_thumb_stamp++;
	}
	return img;
}

static SDL_Surface* get_detail_cached(const char* path, int max_w, int max_h)
{
	if (!path) return NULL;
	if (g_detail_cache && strcmp(g_detail_cache_name, path) == 0)
		return g_detail_cache;

	if (g_detail_cache)
	{
		SDL_FreeSurface(g_detail_cache);
		g_detail_cache = NULL;
	}

	SDL_Surface* img = load_image_surface(path, max_w, max_h);
	if (img && (img->w > max_w || img->h > max_h))
	{
		SDL_Surface* scaled = scale_surface(img, max_w, max_h);
		if (scaled != img)
		{
			SDL_FreeSurface(img);
			img = scaled;
		}
	}
	g_detail_cache = img;
	safe_copy(g_detail_cache_name, sizeof(g_detail_cache_name), path);
	return img;
}

static int matches_search(const Meme* m, const char* q)
{
	if (!q || q[0] == '\0') return 1;
	char lowq[SMALL_MAX_LEN];
	safe_copy(lowq, sizeof(lowq), q);
	for (size_t i = 0; lowq[i]; i++) lowq[i] = (char)tolower(lowq[i]);

	char buffer[TEXT_MAX_LEN * 2];
	snprintf(buffer, sizeof(buffer), "%s %s %s", m->filename, m->tags, m->text);
	for (size_t i = 0; buffer[i]; i++) buffer[i] = (char)tolower(buffer[i]);
	return (strstr(buffer, lowq) != NULL);
}

static int next_visible_item(const AppState* app, int from, int dir)
{
	int i = from + dir;
	while (i >= 0 && i < app->list.count) {
		if (matches_search(&app->list.items[i], app->search)) return i;
		i += dir;
	}
	return -1;
}

static int gallery_item_at_point(const AppState* app, int x, int y)
{
	int shown = 0;
	int col = 0;
	int row_num = 0;
	int thumb_x = 40;
	int thumb_y = 100;
	int thumb_size = THUMB_SIZE;
	int spacing = THUMB_SPACING;
	int cols_per_row = (screen->w - thumb_x * 2) / spacing;
	if (cols_per_row < 1) cols_per_row = 1;
	int max_shown = cols_per_row * ((get_btn_y() - thumb_y) / spacing);
	if (max_shown < 1) max_shown = 1;

	for (int i = app->list_offset; i < app->list.count; i++)
	{
		if (!matches_search(&app->list.items[i], app->search))
			continue;
		if (shown >= max_shown) break;

		int item_x = thumb_x + col * spacing;
		int item_y = thumb_y + row_num * spacing;

		if (x >= item_x - 4 && x < item_x + thumb_size + 4 &&
		    y >= item_y - 4 && y < item_y + thumb_size + 4)
			return i;

		col++;
		if (col >= cols_per_row) { col = 0; row_num++; }
		shown++;
	}
	return -1;
}

static void reset_form(AppState* app)
{
	app->field_index = 0;
	safe_copy(app->form_path, sizeof(app->form_path), "");
	safe_copy(app->form_source_status, sizeof(app->form_source_status), source_statuses[0]);
	safe_copy(app->form_source_detail, sizeof(app->form_source_detail), "");
	safe_copy(app->form_tags, sizeof(app->form_tags), "");
	safe_copy(app->form_text, sizeof(app->form_text), "");
	safe_copy(app->form_file_type, sizeof(app->form_file_type), "");
	safe_copy(app->form_situation, sizeof(app->form_situation), "");
	safe_copy(app->edit_original_filename, sizeof(app->edit_original_filename), "");
}

static void load_form_from_meme(AppState* app, const Meme* m)
{
	app->field_index = 0;
	safe_copy(app->form_path, sizeof(app->form_path), m->filename);
	safe_copy(app->form_source_status, sizeof(app->form_source_status), m->source_status);
	safe_copy(app->form_source_detail, sizeof(app->form_source_detail), m->source_detail);
	safe_copy(app->form_tags, sizeof(app->form_tags), m->tags);
	safe_copy(app->form_text, sizeof(app->form_text), m->text);
	safe_copy(app->form_file_type, sizeof(app->form_file_type), m->file_type);
	safe_copy(app->form_situation, sizeof(app->form_situation), m->situation);
	safe_copy(app->edit_original_filename, sizeof(app->edit_original_filename), m->filename);
}

static void draw_top_bar(const AppState* app, Uint32 fg, Uint32 bg)
{
	Uint32 accent = SDL_MapRGB(screen->format, 215, 145, 30);
	Uint32 idle = SDL_MapRGB(screen->format, 72, 65, 52);
	Uint32 search_bg = SDL_MapRGB(screen->format, 30, 28, 24);
	Uint32 search_border = (app->focus == FOCUS_SEARCH) ? accent : idle;
	int bar_text_y = (TOP_BAR_H - 16) / 2;
	fill_rect(0, 0, screen->w, TOP_BAR_H, bg);
	fill_rect(0, TOP_BAR_H - 3, screen->w, 3, accent);
	draw_text(16, bar_text_y, "Memebooru", fg, 1);
	draw_text(16 + 100, bar_text_y, "local meme archive", SDL_MapRGB(screen->format, 175, 165, 148), 1);
	UiRect sb = get_search_box();
	draw_card(sb.x, sb.y, sb.w, sb.h, search_bg, search_border, SDL_MapRGB(screen->format, 12, 10, 8));
	int search_label_x = sb.x + 10;
	int search_text_x  = sb.x + 72;
	int search_text_w  = sb.w - 82;
	draw_text(search_label_x, bar_text_y, "Search:", SDL_MapRGB(screen->format, 148, 138, 118), 1);
	if (app->focus == FOCUS_SEARCH)
		draw_editable_text(search_text_x, bar_text_y, search_text_w, app->search, app->text_cursor, app->text_anchor, fg, SDL_MapRGB(screen->format, 215, 145, 30), SDL_MapRGB(screen->format, 255, 255, 255), 1);
	else
		draw_text_value(search_text_x, bar_text_y, app->search, fg, search_text_w);
}

static void draw_status_bar(const AppState* app, Uint32 fg, Uint32 bg)
{
	Uint32 border = SDL_MapRGB(screen->format, 215, 145, 30);
	fill_rect(0, screen->h - STATUS_BAR_H, screen->w, STATUS_BAR_H, bg);
	fill_rect(0, screen->h - STATUS_BAR_H, screen->w, 2, border);
	draw_text_value(14, screen->h - 20, app->status_line, fg, screen->w - 28);
}

static void load_text_preview(const char* path)
{
	if (g_text_preview_path[0] != '\0' && strcmp(g_text_preview_path, path) == 0)
		return; /* already cached */

	g_text_preview_buf[0]    = '\0';
	g_text_preview_line_count = 0;
	safe_copy(g_text_preview_path, sizeof(g_text_preview_path), path);

	FILE* f = fopen(path, "r");
	if (!f) return;
	size_t total = fread(g_text_preview_buf, 1, TEXT_PREVIEW_BUF - 1, f);
	fclose(f);
	g_text_preview_buf[total] = '\0';

	/* Build line-offset index: replace line endings with \0 in-place. */
	g_text_preview_line_off[0] = 0;
	g_text_preview_line_count  = 1;
	size_t i;
	for (i = 0; i < total && g_text_preview_line_count < TEXT_PREVIEW_LINES - 1; i++) {
		if (g_text_preview_buf[i] == '\r' || g_text_preview_buf[i] == '\n') {
			if (g_text_preview_buf[i] == '\r' && i + 1 < total && g_text_preview_buf[i + 1] == '\n') {
				g_text_preview_buf[i] = '\0';
				i++;
			}
			g_text_preview_buf[i] = '\0';
			if (i + 1 < total)
				g_text_preview_line_off[g_text_preview_line_count++] = (int)(i + 1);
		}
	}
}

static void draw_list_view(AppState* app, Uint32 fg, Uint32 bg, Uint32 hi)
{
	Uint32 card = SDL_MapRGB(screen->format, 30, 28, 24);
	Uint32 border = SDL_MapRGB(screen->format, 72, 65, 52);
	Uint32 shadow = SDL_MapRGB(screen->format, 12, 10, 8);
	Uint32 soft = SDL_MapRGB(screen->format, 175, 165, 148);

	fill_rect(0, TOP_BAR_H, screen->w, screen->h - TOP_BAR_H - STATUS_BAR_H, bg);
	int gallery_card_h = get_btn_y() - 60 + BTN_H + 10;
	draw_card(20, 60, screen->w - 40, gallery_card_h, card, border, shadow);
	draw_text(32, 74, "Gallery", fg, 1);

	int shown = 0;
	int col = 0;
	int row_num = 0;
	int thumb_x = 40;
	int thumb_y = 100;
	int thumb_size = THUMB_SIZE;
	int spacing = THUMB_SPACING;
	int cols_per_row = (screen->w - thumb_x * 2) / spacing;
	if (cols_per_row < 1) cols_per_row = 1;
	int max_shown = cols_per_row * ((get_btn_y() - thumb_y) / spacing);
	if (max_shown < 1) max_shown = 1;

	for (int i = app->list_offset; i < app->list.count; i++)
	{
		if (!matches_search(&app->list.items[i], app->search))
			continue;
		if (shown >= max_shown) break;

		int x = thumb_x + col * spacing;
		int y = thumb_y + row_num * spacing;

		Uint32 sel_color = (i == app->selected) ? hi : border;
		fill_rect(x - 4, y - 4, thumb_size + 8, thumb_size + 8, sel_color);
		fill_rect(x, y, thumb_size, thumb_size, SDL_MapRGB(screen->format, 45, 42, 38));

		char path[PATH_MAX_LEN * 2];
		build_storage_path(path, sizeof(path), get_data_dir(), app->list.items[i].filename);
		if (is_image_path(path))
		{
			if (!g_previews_enabled)
			{
				draw_text(x + 20, y + 35, "?..", fg, 1);
				goto next_item;
			}
#if defined(__MACOS__) && defined(__m68k__)
			if (i != app->selected)
			{
				draw_text(x + 20, y + 35, "?..", fg, 1);
				goto next_item;
			}
#endif
			SDL_Surface* img = get_thumb_cached(i, path, thumb_size, thumb_size);
			if (img)
			{
				SDL_Rect dst;
				dst.x = x + (thumb_size - img->w) / 2;
				dst.y = y + (thumb_size - img->h) / 2;
				dst.w = img->w;
				dst.h = img->h;
				SDL_BlitSurface(img, NULL, screen, &dst);
				if (i == app->selected)
					maybe_report_image_error(app);
			}
			else if (i == app->selected)
			{
				maybe_report_image_error(app);
			}
		}
		else
		{
			/* Document / video badge */
			Uint32 badge_bg, badge_fg;
			get_doc_badge_colors(path, &badge_bg, &badge_fg);
			int bx = x + 6, by = y + 6, bw = thumb_size - 12, bh = thumb_size - 12;
			fill_rect(bx, by, bw, bh, badge_bg);
			/* Centered type label at 2x scale */
			char lbl[8];
			get_ext_label(path, lbl, sizeof(lbl));
			int lw = (int)strlen(lbl) * 8 * 2;
			int lx = bx + (bw - lw) / 2;
			int ly = by + (bh - 14) / 2;
			draw_text(lx, ly, lbl, badge_fg, 2);
		}

next_item:
		col++;
		if (col >= cols_per_row) { col = 0; row_num++; }
		shown++;
	}

	{ UiRect b = get_list_button(0); draw_button(b.x, b.y, b.w, b.h, "Add", hi, fg, app->pressed_button == BUTTON_LIST_ADD); }
}

static void draw_tag_pills(int fx, int fy, int fw, int fh,
                            const char* text, int cursor,
                            Uint32 pill_bg, Uint32 pill_br, Uint32 pill_fg,
                            Uint32 word_fg, int focused)
{
#if defined(USE_SDL_TTF)
	int cw = g_ttf_char_w > 0 ? g_ttf_char_w : 8;
#else
	int cw = 8;
#endif
	const int pad   = 7;
	const int gap   = 6;
	int pill_h = fh > 36 ? 26 : fh - 8;
	if (pill_h < 14) pill_h = 14;
	int pill_y  = fy + (fh - pill_h) / 2;
	int text_y  = pill_y + (pill_h - 14) / 2 + 1;
	int vx      = fx + 8;
	int cursor_vx = -1;
	int len     = (int)strlen(text);
	const char* p = text;
	const char* end = text + len;

	while (p <= end) {
		int idx = (int)(p - text);
		if (p == end) {
			if (focused && cursor == idx) cursor_vx = vx;
			break;
		}
		const char* ws = p;
		while (p < end && *p != ' ') p++;
		int wlen = (int)(p - ws);
		int committed = (p < end);

		if (committed) {
			int pw = wlen * cw + pad * 2;
			fill_rect(vx,      pill_y,          pw, pill_h, pill_bg);
			fill_rect(vx,      pill_y,          pw, 1,      pill_br);
			fill_rect(vx,      pill_y+pill_h-1, pw, 1,      pill_br);
			fill_rect(vx,      pill_y,          1,  pill_h, pill_br);
			fill_rect(vx+pw-1, pill_y,          1,  pill_h, pill_br);
			char word[512]; int wl = wlen < 511 ? wlen : 511;
			memcpy(word, ws, wl); word[wl] = '\0';
			draw_text(vx + pad, text_y, word, pill_fg, 1);
			int wi = (int)(ws - text);
			if (focused && cursor >= wi && cursor < wi + wlen)
				cursor_vx = vx + pad + (cursor - wi) * cw;
			if (focused && cursor == wi + wlen)
				cursor_vx = vx + pw + gap / 2;
			vx += pw + gap;
			p++;
		} else {
			char word[512]; int wl = wlen < 511 ? wlen : 511;
			memcpy(word, ws, wl); word[wl] = '\0';
			draw_text(vx, text_y, word, word_fg, 1);
			int wi = (int)(ws - text);
			if (focused && cursor >= wi && cursor <= wi + wlen)
				cursor_vx = vx + (cursor - wi) * cw;
		}
	}
	if (focused && cursor_vx < 0) cursor_vx = vx;
	if (focused && ((SDL_GetTicks() / 500) % 2) != 0)
		fill_rect(cursor_vx, pill_y + 2, 2, pill_h - 4, word_fg);
}

static void draw_detail_view(AppState* app, Uint32 fg, Uint32 bg, Uint32 panel)
{
	fill_rect(0, TOP_BAR_H, screen->w, screen->h - TOP_BAR_H - STATUS_BAR_H, bg);
	if (app->selected < 0 || app->selected >= app->list.count) return;
	const Meme* m = &app->list.items[app->selected];

	Uint32 strip_bg   = SDL_MapRGB(screen->format, 24, 22, 19);
	Uint32 soft       = SDL_MapRGB(screen->format, 175, 165, 148);
	Uint32 tag_pill_bg = SDL_MapRGB(screen->format, 70, 50, 12);
	Uint32 tag_pill_br = SDL_MapRGB(screen->format, 240, 165, 20);
	Uint32 tag_pill_fg = SDL_MapRGB(screen->format, 255, 210, 80);

	int btn_y    = get_btn_y();
	int strip_h  = 30;
	int strip_y  = btn_y - strip_h - 4;
	int img_x    = 2;
	int img_y    = TOP_BAR_H + 2;
	int img_w    = screen->w - 4;
	int img_h    = strip_y - img_y - 2;

	/* Image area */
	fill_rect(img_x, img_y, img_w, img_h, panel);
	char path[PATH_MAX_LEN * 2];
	build_storage_path(path, sizeof(path), get_data_dir(), m->filename);
	if (is_image_path(path))
	{
		if (!g_previews_enabled)
		{
			draw_text(img_x + 12, img_y + 14, "(preview disabled)", fg, 2);
		}
		else
		{
			SDL_Surface* img = get_detail_cached(path, img_w, img_h);
			if (img)
			{
				SDL_Rect dst;
				dst.x = img_x + (img_w - img->w) / 2;
				dst.y = img_y + (img_h - img->h) / 2;
				dst.w = img->w;
				dst.h = img->h;
				SDL_BlitSurface(img, NULL, screen, &dst);
				maybe_report_image_error(app);
			}
			else
			{
				maybe_report_image_error(app);
				draw_text(img_x + 12, img_y + 14, "(failed to load image)", fg, 2);
			}
		}
	}
	else if (is_text_path(path))
	{
		load_text_preview(path);
		Uint32 line_fg = SDL_MapRGB(screen->format, 210, 205, 190);
		Uint32 hdr_fg  = SDL_MapRGB(screen->format, 240, 165, 20);
		int line_h   = 16;
		int tx       = img_x + 10;
		int ty       = img_y + 8;
		int max_chars = (img_w - 20) / 8;
		int max_shown = (img_h - 16) / line_h;
		char row_buf[256];
		int shown = 0;
		if (max_chars < 1) max_chars = 1;
		if (max_chars > 255) max_chars = 255;
		/* header: type label + filename */
		char hdr[PATH_MAX_LEN + 16];
		char lbl[8];
		get_ext_label(path, lbl, sizeof(lbl));
		snprintf(hdr, sizeof(hdr), "[%s] %s", lbl, m->filename);
		draw_text(tx, ty, hdr, hdr_fg, 1);
		shown++;
		/* file lines */
		int li;
		for (li = 0; li < g_text_preview_line_count && shown < max_shown; li++) {
			const char* line = g_text_preview_buf + g_text_preview_line_off[li];
			int len = (int)strlen(line);
			if (len > max_chars) {
				strncpy(row_buf, line, (size_t)(max_chars - 3));
				row_buf[max_chars - 3] = '.';
				row_buf[max_chars - 2] = '.';
				row_buf[max_chars - 1] = '.';
				row_buf[max_chars]     = '\0';
				draw_text(tx, ty + shown * line_h, row_buf, line_fg, 1);
			} else {
				draw_text(tx, ty + shown * line_h, line, line_fg, 1);
			}
			shown++;
		}
		if (g_text_preview_line_count > shown - 1)
			draw_text(tx, ty + shown * line_h, "...(truncated)", soft, 1);
	}
	else if (is_pdf_path(path))
	{
		Uint32 badge_bg, badge_fg2;
		get_doc_badge_colors(path, &badge_bg, &badge_fg2);
		int bw = 120, bh = 160;
		int bx = img_x + (img_w - bw) / 2;
		int by = img_y + (img_h - bh) / 2 - 20;
		fill_rect(bx, by, bw, bh, badge_bg);
		draw_text(bx + (bw - 24) / 2, by + (bh - 14) / 2, "PDF", badge_fg2, 3);
		draw_text(img_x + (img_w - (int)strlen(m->filename) * 8) / 2,
		          by + bh + 10, m->filename, soft, 1);
	}
	else if (ext_equals(path_ext(path), ".mp4"))
		draw_text(img_x + 12, img_y + 14, "(video - no preview)", fg, 2);
	else
		draw_text(img_x + 12, img_y + 14, "(unsupported format)", fg, 2);

	/* Tags + filename strip */
	fill_rect(0, strip_y, screen->w, strip_h, strip_bg);
	int strip_text_y = strip_y + (strip_h - 14) / 2;
	char file_info[128];
	snprintf(file_info, sizeof(file_info), "%s", m->filename);
	draw_text_value(screen->w - 400, strip_text_y, file_info, soft, 380);
	/* Draw tag pills in the strip */
	draw_tag_pills(8, strip_y, screen->w - 420, strip_h,
	               m->tags, -1,
	               tag_pill_bg, tag_pill_br, tag_pill_fg,
	               soft, 0);

	/* Buttons: Back | < Prev | Next > | Edit | Delete */
	{ UiRect b = get_detail_button(0); draw_button(b.x, b.y, b.w, b.h, "Back",   panel, fg, app->pressed_button == BUTTON_DETAIL_BACK); }
	{ UiRect b = get_detail_button(1); draw_button(b.x, b.y, b.w, b.h, "< Prev",  panel, fg, app->pressed_button == BUTTON_DETAIL_PREV); }
	{ UiRect b = get_detail_button(2); draw_button(b.x, b.y, b.w, b.h, "Next >", panel, fg, app->pressed_button == BUTTON_DETAIL_NEXT); }
	{ UiRect b = get_detail_button(3); draw_button(b.x, b.y, b.w, b.h, "Edit",   panel, fg, app->pressed_button == BUTTON_DETAIL_EDIT); }
	{ UiRect b = get_detail_button(4); draw_button(b.x, b.y, b.w, b.h, "Delete", panel, fg, app->pressed_button == BUTTON_DETAIL_DELETE); }
}

static void draw_add_view(const AppState* app, Uint32 fg, Uint32 bg, Uint32 panel)
{
	Uint32 card         = SDL_MapRGB(screen->format, 30, 28, 24);
	Uint32 border       = SDL_MapRGB(screen->format, 72, 65, 52);
	Uint32 shadow       = SDL_MapRGB(screen->format, 12, 10, 8);
	Uint32 field_bg     = SDL_MapRGB(screen->format, 24, 22, 19);
	Uint32 active_bdr   = SDL_MapRGB(screen->format, 215, 145, 30);
	Uint32 tag_pill_bg  = SDL_MapRGB(screen->format, 70, 50, 12);
	Uint32 tag_pill_br  = SDL_MapRGB(screen->format, 240, 165, 20);
	Uint32 tag_pill_fg  = SDL_MapRGB(screen->format, 255, 210, 80);
	Uint32 soft         = SDL_MapRGB(screen->format, 175, 165, 148);

	(void)card;

	fill_rect(0, TOP_BAR_H, screen->w, screen->h - TOP_BAR_H - STATUS_BAR_H, bg);

	int title_y = TOP_BAR_H + 14;
	draw_text(24, title_y, (app->edit_index >= 0) ? "Edit meme" : "Add meme", fg, 1);

	/* Tags field */
	UiRect tf = add_field_rect(0);
	int tags_label_y = tf.y - 18;
	draw_text(24, tags_label_y, "Tags  (space-separated):", soft, 1);
	Uint32 tags_bdr = (app->field_index == 0 && app->focus == FOCUS_FORM) ? active_bdr : border;
	draw_card(tf.x, tf.y, tf.w, tf.h, field_bg, tags_bdr, shadow);
	int tags_focused = (app->field_index == 0 && app->focus == FOCUS_FORM);
	draw_tag_pills(tf.x + 2, tf.y, tf.w - 4, tf.h,
	               app->form_tags, app->text_cursor,
	               tag_pill_bg, tag_pill_br, tag_pill_fg,
	               fg, tags_focused);

	/* Path field */
	UiRect pf = add_field_rect(1);
	int path_label_y = pf.y - 18;
	draw_text(24, path_label_y, "File path (PNG / JPG / PDF / TXT / CSV / MD / TEX / MP4):", soft, 1);
	Uint32 path_bdr = (app->field_index == 1 && app->focus == FOCUS_FORM) ? active_bdr : border;
	draw_card(pf.x, pf.y, pf.w, pf.h, field_bg, path_bdr, shadow);
	int path_text_y = pf.y + (pf.h - 14) / 2;
	if (app->field_index == 1 && app->focus == FOCUS_FORM)
		draw_editable_text(pf.x + 10, path_text_y, pf.w - 20,
		                   app->form_path, app->text_cursor, app->text_anchor,
		                   fg, active_bdr,
		                   SDL_MapRGB(screen->format, 255, 255, 255), 1);
	else
		draw_text_value(pf.x + 10, path_text_y, app->form_path, fg, pf.w - 20);

	/* Browse button inline with path field */
	{ UiRect b = get_add_browse_rect(); draw_button(b.x, b.y, b.w, b.h, "Browse", panel, fg, app->pressed_button == BUTTON_ADD_BROWSE); }

	/* Save / Cancel at bottom */
	{ UiRect b = get_add_button(0); draw_button(b.x, b.y, b.w, b.h, "Save",   panel, fg, app->pressed_button == BUTTON_ADD_SAVE); }
	{ UiRect b = get_add_button(1); draw_button(b.x, b.y, b.w, b.h, "Cancel", panel, fg, app->pressed_button == BUTTON_ADD_CANCEL); }
}

static int find_source_status_index(const char* status)
{
	for (int i = 0; i < 4; i++) if (strcmp(status, source_statuses[i]) == 0) return i;
	return 0;
}

static char* active_field(AppState* app)
{
	switch (app->field_index)
	{
		case 0: return app->form_tags;
		case 1: return app->form_path;
		default: return app->form_tags;
	}
}

static size_t active_field_cap(const AppState* app)
{
	switch (app->field_index)
	{
		case 0: return sizeof(app->form_tags);
		case 1: return sizeof(app->form_path);
		default: return sizeof(app->form_tags);
	}
}

static void clamp_selection(AppState* app, const char* buffer)
{
	int len = (int)strlen(buffer);
	if (app->text_cursor < 0) app->text_cursor = 0;
	if (app->text_cursor > len) app->text_cursor = len;
	if (app->text_anchor < 0) app->text_anchor = 0;
	if (app->text_anchor > len) app->text_anchor = len;
}

static void set_selection_all(AppState* app, const char* buffer)
{
	int len = (int)strlen(buffer);
	app->text_anchor = 0;
	app->text_cursor = len;
}

static void set_selection_to_end(AppState* app, const char* buffer)
{
	int len = (int)strlen(buffer);
	app->text_anchor = len;
	app->text_cursor = len;
}

static int is_primary_modifier(Uint16 mod)
{
#if defined(KMOD_GUI)
	return ((mod & KMOD_GUI) != 0);
#else
	return ((mod & KMOD_META) != 0);
#endif
}

#if defined(__APPLE__) && !defined(BUILD_FOR_10_4)
static int clipboard_set_text(const char* text)
{
	PasteboardRef pb = NULL;
	OSStatus status = PasteboardCreate(kPasteboardClipboard, &pb);
	if (status != noErr || !pb) return 0;
	PasteboardClear(pb);
	PasteboardSynchronize(pb);
	CFDataRef data = CFDataCreate(NULL, (const UInt8*)text, (CFIndex)strlen(text));
	if (!data) { CFRelease(pb); return 0; }
	status = PasteboardPutItemFlavor(pb, (PasteboardItemID)1, CFSTR("public.utf8-plain-text"), data, 0);
	CFRelease(data);
	CFRelease(pb);
	return (status == noErr);
}

static char* clipboard_get_text(void)
{
	PasteboardRef pb = NULL;
	if (PasteboardCreate(kPasteboardClipboard, &pb) != noErr || !pb) return NULL;
	PasteboardSynchronize(pb);

	ItemCount count = 0;
	if (PasteboardGetItemCount(pb, &count) != noErr || count == 0) { CFRelease(pb); return NULL; }

	for (ItemCount i = 1; i <= count; i++)
	{
		PasteboardItemID item = 0;
		if (PasteboardGetItemIdentifier(pb, i, &item) != noErr) continue;
		CFDataRef data = NULL;
		if (PasteboardCopyItemFlavorData(pb, item, CFSTR("public.utf8-plain-text"), &data) != noErr || !data) continue;

		CFIndex len = CFDataGetLength(data);
		const UInt8* bytes = CFDataGetBytePtr(data);
		char* out = (char*)malloc((size_t)len + 1);
		if (out)
		{
			memcpy(out, bytes, (size_t)len);
			out[len] = '\0';
		}
		CFRelease(data);
		CFRelease(pb);
		return out;
	}

	CFRelease(pb);
	return NULL;
}
#else
static int clipboard_set_text(const char* text)
{
	(void)text;
	return 0;
}

static char* clipboard_get_text(void)
{
	return NULL;
}
#endif

static void delete_selected_range(char* buffer, int* cursor, int* anchor)
{
	int a = (*cursor < *anchor) ? *cursor : *anchor;
	int b = (*cursor < *anchor) ? *anchor : *cursor;
	if (a == b) return;
	int len = (int)strlen(buffer);
	memmove(buffer + a, buffer + b, (size_t)(len - b + 1));
	*cursor = a;
	*anchor = a;
}

static void copy_selected_text_to_clipboard(AppState* app, const char* buffer)
{
	clamp_selection(app, buffer);
	if (app->text_cursor == app->text_anchor) return;
	int a = (app->text_cursor < app->text_anchor) ? app->text_cursor : app->text_anchor;
	int b = (app->text_cursor < app->text_anchor) ? app->text_anchor : app->text_cursor;
	int len = b - a;
	if (len <= 0) return;
	char* temp = (char*)malloc((size_t)len + 1);
	if (!temp) return;
	memcpy(temp, buffer + a, (size_t)len);
	temp[len] = '\0';
	clipboard_set_text(temp);
	free(temp);
}

static void insert_text_at_selection(AppState* app, char* buffer, size_t cap, const char* text)
{
	if (!text || !text[0]) return;
	clamp_selection(app, buffer);
	if (app->text_cursor != app->text_anchor)
		delete_selected_range(buffer, &app->text_cursor, &app->text_anchor);

	for (size_t i = 0; text[i] != '\0'; i++)
	{
		unsigned char c = (unsigned char)text[i];
		if (c < 32 || c >= 127) continue;
		int len = (int)strlen(buffer);
		if (len + 1 >= (int)cap) break;
		memmove(buffer + app->text_cursor + 1, buffer + app->text_cursor, (size_t)(len - app->text_cursor + 1));
		buffer[app->text_cursor] = (char)c;
		app->text_cursor++;
		app->text_anchor = app->text_cursor;
	}
}

static void handle_text_input(AppState* app, char* buffer, size_t cap, const SDL_Event* event)
{
	if (event->type != SDL_KEYDOWN) return;
	clamp_selection(app, buffer);

	SDLKey sym = event->key.keysym.sym;
	Uint16 mod = event->key.keysym.mod;
	int shift = ((mod & KMOD_SHIFT) != 0);

	if (sym == SDLK_LEFT || sym == SDLK_RIGHT || sym == SDLK_HOME || sym == SDLK_END)
	{
		int len = (int)strlen(buffer);
		int next = app->text_cursor;
		if (sym == SDLK_LEFT && next > 0) next--;
		else if (sym == SDLK_RIGHT && next < len) next++;
		else if (sym == SDLK_HOME) next = 0;
		else if (sym == SDLK_END) next = len;
		app->text_cursor = next;
		if (!shift) app->text_anchor = next;
		return;
	}

	if (sym == SDLK_BACKSPACE || sym == SDLK_DELETE)
	{
		int len = (int)strlen(buffer);
		if (app->text_cursor != app->text_anchor)
		{
			delete_selected_range(buffer, &app->text_cursor, &app->text_anchor);
			return;
		}
		if (sym == SDLK_BACKSPACE && app->text_cursor > 0)
		{
			memmove(buffer + app->text_cursor - 1, buffer + app->text_cursor, (size_t)(len - app->text_cursor + 1));
			app->text_cursor--;
			app->text_anchor = app->text_cursor;
		}
		else if (sym == SDLK_DELETE && app->text_cursor < len)
		{
			memmove(buffer + app->text_cursor, buffer + app->text_cursor + 1, (size_t)(len - app->text_cursor));
			app->text_anchor = app->text_cursor;
		}
		return;
	}

	Uint16 uni = event->key.keysym.unicode;
	if (uni >= 32 && uni < 127)
	{
		int len = (int)strlen(buffer);
		if (app->text_cursor != app->text_anchor)
		{
			delete_selected_range(buffer, &app->text_cursor, &app->text_anchor);
			len = (int)strlen(buffer);
		}
		if (len + 1 < (int)cap)
		{
			memmove(buffer + app->text_cursor + 1, buffer + app->text_cursor, (size_t)(len - app->text_cursor + 1));
			buffer[app->text_cursor] = (char)uni;
			app->text_cursor++;
			app->text_anchor = app->text_cursor;
		}
	}
}

static int add_or_update_meme(AppState* app)
{
	last_storage_error[0] = '\0';
	if (!ensure_data_dir())
	{
		set_status(app, last_storage_error[0] ? last_storage_error : "Failed to create data directory");
		return 0;
	}
	if (!is_supported_path(app->form_path))
	{
		set_status(app, "Supported: PNG/JPG/MP4");
		return 0;
	}

	if (app->edit_index >= 0)
	{
		Meme* m = &app->list.items[app->edit_index];
		if (strcmp(app->form_path, app->edit_original_filename) != 0)
		{
			char new_ext[SMALL_MAX_LEN];
			set_file_type_from_path(new_ext, sizeof(new_ext), app->form_path);
			char dst_name[SMALL_MAX_LEN + 16];
			snprintf(dst_name, sizeof(dst_name), "meme_%s.%s", m->id, new_ext);
			char dst_path[PATH_MAX_LEN * 2];
			build_storage_path(dst_path, sizeof(dst_path), get_data_dir(), dst_name);
			if (!copy_file(app->form_path, dst_path))
			{
				set_status(app, last_storage_error[0] ? last_storage_error : "Failed to copy file into storage");
				return 0;
			}
			char old_path[PATH_MAX_LEN * 2];
			build_storage_path(old_path, sizeof(old_path), get_data_dir(), m->filename);
			remove(old_path);
			safe_copy(m->filename, sizeof(m->filename), dst_name);
			set_file_type_from_path(app->form_file_type, sizeof(app->form_file_type), app->form_path);
		}
		safe_copy(m->source_status, sizeof(m->source_status), app->form_source_status);
		safe_copy(m->source_detail, sizeof(m->source_detail), app->form_source_detail);
		safe_copy(m->tags, sizeof(m->tags), app->form_tags);
		safe_copy(m->text, sizeof(m->text), app->form_text);
		safe_copy(m->file_type, sizeof(m->file_type), app->form_file_type);
		safe_copy(m->situation, sizeof(m->situation), app->form_situation);
		if (storage_save(&app->list))
			set_status(app, "Updated meme");
		else
			set_status(app, "Failed to save index");
		clear_image_caches();
		return 1;
	}

	char id[SMALL_MAX_LEN];
	generate_id(id, sizeof(id));
	char ext[SMALL_MAX_LEN];
	set_file_type_from_path(ext, sizeof(ext), app->form_path);
	char dst_name[SMALL_MAX_LEN + 16];
	snprintf(dst_name, sizeof(dst_name), "meme_%s.%s", id, ext);
	char dst_path[PATH_MAX_LEN * 2];
	build_storage_path(dst_path, sizeof(dst_path), get_data_dir(), dst_name);

	if (!copy_file(app->form_path, dst_path))
	{
		set_status(app, last_storage_error[0] ? last_storage_error : "Failed to copy file into storage");
		return 0;
	}

	if (app->list.count >= MEME_MAX)
	{
		set_status(app, "Meme limit reached");
		return 0;
	}

	Meme* m = &app->list.items[app->list.count++];
	safe_copy(m->id, sizeof(m->id), id);
	safe_copy(m->filename, sizeof(m->filename), dst_name);
	safe_copy(m->source_status, sizeof(m->source_status), app->form_source_status);
	safe_copy(m->source_detail, sizeof(m->source_detail), app->form_source_detail);
	safe_copy(m->tags, sizeof(m->tags), app->form_tags);
	safe_copy(m->text, sizeof(m->text), app->form_text);
	set_file_type_from_path(app->form_file_type, sizeof(app->form_file_type), app->form_path);
	safe_copy(m->file_type, sizeof(m->file_type), app->form_file_type);
	safe_copy(m->situation, sizeof(m->situation), app->form_situation);

	if (!storage_save(&app->list))
	{
		set_status(app, "Failed to save index");
		return 0;
	}
	set_status(app, "Meme added");
	clear_image_caches();
	return 1;
}

#if defined(__APPLE__) && !defined(__MACOS__)
static void open_file_externally(const char* path)
{
	char cmd[PATH_MAX_LEN * 2 + 8];
	snprintf(cmd, sizeof(cmd), "open \"%s\"", path);
	system(cmd);
}
#endif

static void delete_selected(AppState* app)
{
	if (app->selected < 0 || app->selected >= app->list.count) return;
	char path[PATH_MAX_LEN * 2];
	build_storage_path(path, sizeof(path), get_data_dir(), app->list.items[app->selected].filename);
	remove(path);
	for (int i = app->selected; i < app->list.count - 1; i++)
		app->list.items[i] = app->list.items[i + 1];
	app->list.count--;
	if (app->selected >= app->list.count) app->selected = app->list.count - 1;
	storage_save(&app->list);
	set_status(app, "Meme deleted");
	clear_image_caches();
}

#if defined(BUILD_FOR_11_0) || defined(BUILD_FOR_10_7)
static void blit_scaled(SDL_Surface* src, SDL_Surface* dst)
{
	if (src->w == dst->w && src->h == dst->h) {
		SDL_BlitSurface(src, NULL, dst, NULL);
		return;
	}
	if (SDL_MUSTLOCK(src)) SDL_LockSurface(src);
	if (SDL_MUSTLOCK(dst)) SDL_LockSurface(dst);

	const int sw = src->w, sh = src->h, dw = dst->w, dh = dst->h;

	/* Precompute per-column sample positions and blend weights (float → once per resize). */
	int* xs0 = (int*)malloc((size_t)dw * 3 * sizeof(int));
	if (!xs0) {
		/* OOM: fall back to nearest-neighbor */
		for (int dy = 0; dy < dh; dy++) {
			const Uint32* s = (const Uint32*)((const Uint8*)src->pixels + (dy * sh / dh) * src->pitch);
			Uint32* d = (Uint32*)((Uint8*)dst->pixels + dy * dst->pitch);
			for (int dx = 0; dx < dw; dx++) d[dx] = s[dx * sw / dw];
		}
		goto blit_done;
	}
	{
		int* xs1 = xs0 + dw;
		int* xw  = xs0 + dw * 2;
		for (int dx = 0; dx < dw; dx++) {
			float fx = (dx + 0.5f) * sw / (float)dw - 0.5f;
			int i0 = (int)fx; if (i0 < 0) i0 = 0; if (i0 >= sw) i0 = sw - 1;
			int i1 = i0 + 1;  if (i1 >= sw) i1 = sw - 1;
			int w = (int)((fx - i0) * 256.0f + 0.5f);
			xs0[dx] = i0; xs1[dx] = i1;
			xw[dx] = (w < 0) ? 0 : (w > 255) ? 255 : w;
		}
		for (int dy = 0; dy < dh; dy++) {
			float fy = (dy + 0.5f) * sh / (float)dh - 0.5f;
			int sy0 = (int)fy; if (sy0 < 0) sy0 = 0; if (sy0 >= sh) sy0 = sh - 1;
			int sy1 = sy0 + 1; if (sy1 >= sh) sy1 = sh - 1;
			int wy = (sy1 == sy0) ? 0 : (int)((fy - sy0) * 256.0f + 0.5f);
			if (wy < 0) wy = 0; if (wy > 255) wy = 255;
			const int wy0 = 256 - wy;
			const Uint32* s0 = (const Uint32*)((const Uint8*)src->pixels + sy0 * src->pitch);
			const Uint32* s1 = (const Uint32*)((const Uint8*)src->pixels + sy1 * src->pitch);
			Uint32* d = (Uint32*)((Uint8*)dst->pixels + dy * dst->pitch);
			for (int dx = 0; dx < dw; dx++) {
				const int wx = xw[dx], wx0 = 256 - wx;
				const Uint32 p00=s0[xs0[dx]], p10=s0[xs1[dx]], p01=s1[xs0[dx]], p11=s1[xs1[dx]];
				const int w00=wx0*wy0, w10=wx*wy0, w01=wx0*wy, w11=wx*wy;
				d[dx] = (Uint32)((( p00     &0xff)*w00+( p10     &0xff)*w10+( p01     &0xff)*w01+( p11     &0xff)*w11)>>16)
				      |(Uint32)(((( p00>> 8)&0xff)*w00+(( p10>> 8)&0xff)*w10+(( p01>> 8)&0xff)*w01+(( p11>> 8)&0xff)*w11)>>16)<<8
				      |(Uint32)(((( p00>>16)&0xff)*w00+(( p10>>16)&0xff)*w10+(( p01>>16)&0xff)*w01+(( p11>>16)&0xff)*w11)>>16)<<16
				      |(Uint32)(((( p00>>24)&0xff)*w00+(( p10>>24)&0xff)*w10+(( p01>>24)&0xff)*w01+(( p11>>24)&0xff)*w11)>>16)<<24;
			}
		}
		free(xs0);
	}
blit_done:
	if (SDL_MUSTLOCK(dst)) SDL_UnlockSurface(dst);
	if (SDL_MUSTLOCK(src)) SDL_UnlockSurface(src);
}
#endif

#if defined(USE_SDL_TTF) && defined(__APPLE__) && !defined(BUILD_FOR_10_4)
static void init_ttf(void)
{
	if (TTF_Init() < 0) return;

	char path[1024] = "";
	/* Ask CoreText for the real path of Helvetica Neue on this system. */
	CTFontDescriptorRef desc = CTFontDescriptorCreateWithNameAndSize(CFSTR("HelveticaNeue"), 14.0);
	CFURLRef url = (CFURLRef)CTFontDescriptorCopyAttribute(desc, kCTFontURLAttribute);
	CFRelease(desc);
	if (url) {
		CFURLGetFileSystemRepresentation(url, true, (UInt8*)path, (CFIndex)sizeof(path) - 1);
		CFRelease(url);
	}
	if (!path[0]) {
		/* Fallback to known system font locations. */
		static const char* fallbacks[] = {
			"/System/Library/Fonts/Helvetica.ttc",
			"/System/Library/Fonts/HelveticaNeue.ttc",
			"/Library/Fonts/Arial.ttf",
			NULL
		};
		for (int i = 0; fallbacks[i]; i++) {
			struct stat st;
			if (stat(fallbacks[i], &st) == 0) {
				strncpy(path, fallbacks[i], sizeof(path) - 1);
				break;
			}
		}
	}
	if (!path[0]) return;

	g_ttf_normal = TTF_OpenFontIndex(path, 14, 0);
	g_ttf_large  = TTF_OpenFontIndex(path, 28, 0);

	/* Measure average glyph advance for editable text cursor positioning. */
	if (g_ttf_normal) {
		int w = 0, h = 0;
		TTF_SizeUTF8(g_ttf_normal, "MMMMMMMMMM", &w, &h);
		if (w > 0) g_ttf_char_w = w / 10;
	}
	if (g_ttf_large) {
		int w = 0, h = 0;
		TTF_SizeUTF8(g_ttf_large, "MMMMMMMMMM", &w, &h);
		if (w > 0) g_ttf_char_w2 = w / 10;
	}
}
#endif

int sdl_app(int argc, char* argv[])
{
#ifdef BUILD_FOR_10_4
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
	[NSApplication sharedApplication];
	[NSApp finishLaunching];
#endif
	(void)argc; (void)argv;


	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "Couldn't initialize SDL: %s\n", SDL_GetError());
		exit(1);
	}
	atexit(SDL_Quit);
	SDL_EnableUNICODE(1);
#if defined(__APPLE__) && !defined(BUILD_FOR_10_4)
	/* Keep Metal rendering right-side-up; HiDPI path interacts badly with the
	 * FLIP_VERTICAL patch in sdl12-compat causing an upside-down display. */
	if (!getenv("SDL12COMPAT_HIGHDPI"))
		setenv("SDL12COMPAT_HIGHDPI", "0", 1);
#endif
#if defined(USE_SDL_TTF) && defined(__APPLE__) && !defined(BUILD_FOR_10_4)
	init_ttf();
#endif
#if defined(__MACOS__) && !defined(BUILD_FOR_10_4) && !defined(__m68k__)
	init_quicktime();
#endif

#if defined(BUILD_FOR_11_0) || defined(BUILD_FOR_10_7)
	g_win_surf = SDL_SetVideoMode(WINDOW_W, WINDOW_H, 0, SDL_SWSURFACE | SDL_RESIZABLE);
	if (!g_win_surf) {
		fprintf(stderr, "Failed to open window: %s\n", SDL_GetError());
		exit(1);
	}
	screen = SDL_CreateRGBSurface(SDL_SWSURFACE, WINDOW_W, WINDOW_H,
		g_win_surf->format->BitsPerPixel,
		g_win_surf->format->Rmask, g_win_surf->format->Gmask,
		g_win_surf->format->Bmask, 0);
	if (!screen) {
		fprintf(stderr, "Failed to create canvas: %s\n", SDL_GetError());
		exit(1);
	}
#else
	{
		Uint32 video_flags = SDL_SWSURFACE;
#ifdef BUILD_FOR_10_4
		video_flags = SDL_ANYFORMAT | SDL_HWSURFACE | SDL_DOUBLEBUF;
#endif
		screen = SDL_SetVideoMode(WINDOW_W, WINDOW_H, 0, video_flags);
		if (!screen) {
			fprintf(stderr, "Failed to open window: %s\n", SDL_GetError());
			exit(1);
		}
	}
#endif
	SDL_WM_SetCaption("Memebooru", NULL);

	Uint32 bg = SDL_MapRGB(screen->format, 18, 17, 15);
	Uint32 panel = SDL_MapRGB(screen->format, 28, 26, 23);
	Uint32 fg = SDL_MapRGB(screen->format, 232, 222, 205);
	Uint32 hi = SDL_MapRGB(screen->format, 90, 72, 40);

	static AppState app;
	memset(&app, 0, sizeof(app));
	app.selected = 0;
	app.list_offset = 0;
	app.view = VIEW_LIST;
	app.focus = FOCUS_SEARCH;
	app.text_cursor = 0;
	app.text_anchor = 0;
	app.edit_index = -1;
	app.last_click_item = -1;
	app.last_click_time = 0;
	reset_form(&app);
	set_status(&app, "Ready");
	init_storage_paths();
	ensure_data_dir();
	storage_load(&app.list);
	clear_image_caches();
	for (int i = 0; i < THUMB_CACHE_MAX; i++)
		g_thumb_cache[i].index = -1;
#if defined(__MACOS__) && defined(__m68k__)
	g_previews_enabled = 0;
	set_status(&app, "Preview disabled (Cmd+P to enable)");
#endif


	int running = 1;
	while (running)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
				case SDL_QUIT:
					running = 0;
					break;
				case SDL_MOUSEBUTTONDOWN:
					if (event.button.button == SDL_BUTTON_LEFT)
					{
						int mx = event.button.x;
						int my = event.button.y;
						normalize_mouse(&mx, &my);
						app.pressed_button = button_id_at_point(app.view, mx, my);
						if (app.view == VIEW_LIST)
						{
							if (point_in_uirect(mx, my, get_search_box()))
							{
								app.focus = FOCUS_SEARCH;
								set_selection_all(&app, app.search);
							}
						}
						if (app.view == VIEW_ADD)
						{
							int field = add_field_at_point(mx, my);
							if (field >= 0)
							{
								app.field_index = field;
								app.focus = FOCUS_FORM;
								set_selection_all(&app, active_field(&app));
							}
						}
					}
					break;
				case SDL_MOUSEBUTTONUP:
					if (event.button.button == SDL_BUTTON_LEFT)
					{
						int mx = event.button.x;
						int my = event.button.y;
						normalize_mouse(&mx, &my);
						int released_button = button_id_at_point(app.view, mx, my);
						int clicked_button = (released_button == app.pressed_button) ? released_button : BUTTON_NONE;
						app.pressed_button = BUTTON_NONE;

						switch (clicked_button)
						{
							case BUTTON_LIST_ADD:
								app.view = VIEW_ADD;
								app.focus = FOCUS_FORM;
								app.edit_index = -1;
								reset_form(&app);
								set_selection_all(&app, active_field(&app));
								set_status(&app, "Add new meme");
								break;
							case BUTTON_LIST_EDIT:
								if (app.list.count > 0) { app.view = VIEW_ADD; app.focus = FOCUS_FORM; app.edit_index = app.selected; load_form_from_meme(&app, &app.list.items[app.selected]); set_selection_all(&app, active_field(&app)); set_status(&app, "Edit meme"); }
								break;
							case BUTTON_LIST_DELETE:
								if (app.list.count > 0) delete_selected(&app);
								break;
							case BUTTON_LIST_OPEN:
								if (app.list.count > 0) app.view = VIEW_DETAIL;
								break;
							case BUTTON_LIST_QUIT:
								running = 0;
								break;
							case BUTTON_DETAIL_BACK:
								app.view = VIEW_LIST;
								app.focus = FOCUS_SEARCH;
								set_selection_to_end(&app, app.search);
								break;
							case BUTTON_DETAIL_PREV:
							{
								int p = next_visible_item(&app, app.selected, -1);
								if (p >= 0) { app.selected = p; if (g_detail_cache) { SDL_FreeSurface(g_detail_cache); g_detail_cache = NULL; g_detail_cache_name[0] = '\0'; g_text_preview_path[0] = '\0'; } }
								break;
							}
							case BUTTON_DETAIL_NEXT:
							{
								int n = next_visible_item(&app, app.selected, 1);
								if (n >= 0) { app.selected = n; if (g_detail_cache) { SDL_FreeSurface(g_detail_cache); g_detail_cache = NULL; g_detail_cache_name[0] = '\0'; g_text_preview_path[0] = '\0'; } }
								break;
							}
							case BUTTON_DETAIL_EDIT:
								app.view = VIEW_ADD;
								app.focus = FOCUS_FORM;
								app.edit_index = app.selected;
								load_form_from_meme(&app, &app.list.items[app.selected]);
								set_selection_all(&app, active_field(&app));
								set_status(&app, "Edit meme");
								break;
							case BUTTON_DETAIL_DELETE:
								delete_selected(&app);
								app.view = VIEW_LIST;
								app.focus = FOCUS_SEARCH;
								set_selection_to_end(&app, app.search);
								break;
							case BUTTON_ADD_BROWSE:
							{
								const char* path = mac_file_dialog("Select file", "png;jpg;jpeg;mp4;pdf;txt;csv;md;tex;bib", NULL);
								if (path) {
									safe_copy(app.form_path, sizeof(app.form_path), path);
									set_file_type_from_path(app.form_file_type, sizeof(app.form_file_type), path);
								}
								break;
							}
							case BUTTON_ADD_SAVE:
								if (add_or_update_meme(&app)) { app.view = VIEW_LIST; app.focus = FOCUS_SEARCH; set_selection_to_end(&app, app.search); }
								break;
							case BUTTON_ADD_CANCEL:
								app.view = VIEW_LIST;
								app.focus = FOCUS_SEARCH;
								set_selection_to_end(&app, app.search);
								set_status(&app, "Canceled");
								break;
							default:
								if (app.view == VIEW_LIST)
								{
									int idx = gallery_item_at_point(&app, mx, my);
									if (idx >= 0)
									{
										app.selected = idx;
										app.view = VIEW_DETAIL;
									}
								}
								else if (app.view == VIEW_DETAIL)
								{
#if defined(__APPLE__) && !defined(__MACOS__)
									int img_area_x2 = 24 + 4;
									int img_area_y2 = TOP_BAR_H + 22;
									int img_area_w2 = (screen->w - 60) * 44 / 100 - 4;
									int img_area_h2 = get_btn_y() - img_area_y2 - 10;
									if (point_in_rect(mx, my, img_area_x2, img_area_y2, img_area_w2, img_area_h2))
									{
										Uint32 now = SDL_GetTicks();
										if (now - app.last_click_time < 400 && app.last_click_item == -2)
										{
											if (app.selected >= 0 && app.selected < app.list.count)
											{
												char fpath[PATH_MAX_LEN * 2];
												build_storage_path(fpath, sizeof(fpath), get_data_dir(), app.list.items[app.selected].filename);
												open_file_externally(fpath);
											}
											app.last_click_item = -1;
										}
										else
										{
											app.last_click_time = now;
											app.last_click_item = -2;
										}
									}
#endif
								}
								if (app.view == VIEW_ADD)
								{
									int field = add_field_at_point(mx, my);
									if (field >= 0)
									{
										app.field_index = field;
										app.focus = FOCUS_FORM;
										set_selection_all(&app, active_field(&app));
									}
								}
								break;
						}
					}
					break;
				case SDL_KEYDOWN:
				{
					SDLKey sym = event.key.keysym.sym;
					Uint16 mod = event.key.keysym.mod;
					int cmd = is_primary_modifier(mod);

					if (cmd)
					{
#if defined(BUILD_FOR_11_0) || defined(BUILD_FOR_10_7)
						if (sym == SDLK_f)
						{
							/* Toggle fullscreen. SDL_WM_ToggleFullScreen maps to native macOS fullscreen
							   via sdl12-compat; fall back to explicit SetVideoMode if it returns 0. */
							if (g_win_surf && !SDL_WM_ToggleFullScreen(g_win_surf))
							{
								int is_fs = (g_win_surf->flags & SDL_FULLSCREEN) != 0;
								Uint32 flags = SDL_SWSURFACE | SDL_RESIZABLE;
								if (!is_fs) flags |= SDL_FULLSCREEN;
								SDL_Surface* ns = SDL_SetVideoMode(0, 0, 0, flags);
								if (!ns) ns = SDL_SetVideoMode(WINDOW_W, WINDOW_H, 0, flags);
								if (ns) g_win_surf = ns;
							}
							break;
						}
#endif
						if (sym == SDLK_p)
						{
							g_previews_enabled = !g_previews_enabled;
							if (!g_previews_enabled)
							{
								clear_image_caches();
								set_status(&app, "Preview disabled");
							}
							else
							{
								set_status(&app, "Preview enabled");
							}
							break;
						}
						char* edit_buf = NULL;
						size_t edit_cap = 0;
						if (app.focus == FOCUS_SEARCH)
						{
							edit_buf = app.search;
							edit_cap = sizeof(app.search);
						}
						else if (app.focus == FOCUS_FORM && app.view == VIEW_ADD)
						{
							edit_buf = active_field(&app);
							edit_cap = active_field_cap(&app);
						}

						if (edit_buf)
						{
							if (sym == SDLK_a)
							{
								set_selection_all(&app, edit_buf);
								break;
							}
							if (sym == SDLK_c)
							{
								copy_selected_text_to_clipboard(&app, edit_buf);
								break;
							}
							if (sym == SDLK_x)
							{
								copy_selected_text_to_clipboard(&app, edit_buf);
								delete_selected_range(edit_buf, &app.text_cursor, &app.text_anchor);
								break;
							}
							if (sym == SDLK_v)
							{
								char* clip = clipboard_get_text();
								if (clip)
								{
									insert_text_at_selection(&app, edit_buf, edit_cap, clip);
									free(clip);
								}
								break;
							}
						}
					}

					if (app.view == VIEW_LIST)
					{
						if (sym == SDLK_ESCAPE) running = 0;
						else if (sym == SDLK_DOWN) app.selected = (app.selected + 1 < app.list.count) ? app.selected + 1 : app.selected;
						else if (sym == SDLK_UP) app.selected = (app.selected > 0) ? app.selected - 1 : app.selected;
						else if (sym == SDLK_RETURN && app.list.count > 0) app.view = VIEW_DETAIL;
						else { app.focus = FOCUS_SEARCH; handle_text_input(&app, app.search, sizeof(app.search), &event); }
					}
					else if (app.view == VIEW_DETAIL)
					{
						if (sym == SDLK_BACKSPACE || sym == SDLK_ESCAPE) { app.view = VIEW_LIST; app.focus = FOCUS_SEARCH; set_selection_to_end(&app, app.search); }
						else if (sym == SDLK_LEFT)  { int p = next_visible_item(&app, app.selected, -1); if (p >= 0) { app.selected = p; if (g_detail_cache) { SDL_FreeSurface(g_detail_cache); g_detail_cache = NULL; g_detail_cache_name[0] = '\0'; g_text_preview_path[0] = '\0'; } } }
						else if (sym == SDLK_RIGHT) { int n = next_visible_item(&app, app.selected,  1); if (n >= 0) { app.selected = n; if (g_detail_cache) { SDL_FreeSurface(g_detail_cache); g_detail_cache = NULL; g_detail_cache_name[0] = '\0'; g_text_preview_path[0] = '\0'; } } }
					}
					else if (app.view == VIEW_ADD)
					{
						if (sym == SDLK_ESCAPE) { app.view = VIEW_LIST; app.focus = FOCUS_SEARCH; set_selection_to_end(&app, app.search); set_status(&app, "Canceled"); }
						else if (sym == SDLK_RETURN) { app.field_index = (app.field_index + 1) % 2; app.focus = FOCUS_FORM; set_selection_all(&app, active_field(&app)); }
						else {
							char* field = active_field(&app);
							app.focus = FOCUS_FORM;
							handle_text_input(&app, field, active_field_cap(&app), &event);
						}
					}
				}
					break;
#if defined(BUILD_FOR_11_0) || defined(BUILD_FOR_10_7)
				case SDL_VIDEORESIZE: {
					int nw = event.resize.w, nh = event.resize.h;
					if (!screen || screen->w != nw || screen->h != nh) {
						SDL_Surface* old_s = screen;
						Uint32 rm = g_win_surf ? g_win_surf->format->Rmask : 0x000000ff;
						Uint32 gm = g_win_surf ? g_win_surf->format->Gmask : 0x0000ff00;
						Uint32 bm = g_win_surf ? g_win_surf->format->Bmask : 0x00ff0000;
						int   bpp = g_win_surf ? g_win_surf->format->BitsPerPixel : 32;
						screen = SDL_CreateRGBSurface(SDL_SWSURFACE, nw, nh, bpp, rm, gm, bm, 0);
						if (old_s) SDL_FreeSurface(old_s);
						if (g_detail_cache) { SDL_FreeSurface(g_detail_cache); g_detail_cache = NULL; g_detail_cache_name[0] = '\0'; g_text_preview_path[0] = '\0'; }
					}
					/* Delay SDL_SetVideoMode until the render loop so we can draw
					 * immediately after, preventing a visible black frame. */
					g_resize_pending_w = nw;
					g_resize_pending_h = nh;
					g_resize_idle = 0;
					break;
				}
#endif
				default:
					break;
			}
		}

		SDL_FillRect(screen, NULL, bg);
		draw_top_bar(&app, fg, panel);
		if (app.view == VIEW_LIST)
			draw_list_view(&app, fg, bg, hi);
		else if (app.view == VIEW_DETAIL)
			draw_detail_view(&app, fg, bg, panel);
		else
			draw_add_view(&app, fg, bg, panel);
		draw_status_bar(&app, fg, panel);

#if defined(BUILD_FOR_11_0) || defined(BUILD_FOR_10_7)
		if (g_win_surf && screen) {
#if defined(BUILD_FOR_11_0)
			/* Metal: wait for drag to stabilise before calling SDL_SetVideoMode
			 * (Metal layer survives a few frames without it). */
			if (g_resize_pending_w > 0) {
				g_resize_idle++;
				if (g_resize_idle >= 4) {
					Uint32 keep = g_win_surf->flags & SDL_FULLSCREEN;
					SDL_Surface* nw = SDL_SetVideoMode(g_resize_pending_w, g_resize_pending_h,
						0, SDL_SWSURFACE | SDL_RESIZABLE | keep);
					if (nw) g_win_surf = nw;
					g_resize_pending_w = g_resize_pending_h = g_resize_idle = 0;
				}
			}
#elif defined(BUILD_FOR_10_7)
			/* OpenGL: call SDL_SetVideoMode every frame while resizing so the GL
			 * viewport stays current. Drawing follows immediately, so the clear
			 * is never visible. */
			if (g_resize_pending_w > 0) {
				Uint32 keep = g_win_surf->flags & SDL_FULLSCREEN;
				SDL_Surface* nw = SDL_SetVideoMode(g_resize_pending_w, g_resize_pending_h,
					0, SDL_SWSURFACE | SDL_RESIZABLE | keep);
				if (nw) g_win_surf = nw;
				g_resize_pending_w = g_resize_pending_h = g_resize_idle = 0;
			}
#endif
			blit_scaled(screen, g_win_surf);
			SDL_Flip(g_win_surf);
		}
#else
		SDL_Flip(screen);
#endif
		SDL_Delay(16);
	}


#if defined(USE_SDL_TTF)
	if (g_ttf_normal) { TTF_CloseFont(g_ttf_normal); g_ttf_normal = NULL; }
	if (g_ttf_large)  { TTF_CloseFont(g_ttf_large);  g_ttf_large  = NULL; }
	TTF_Quit();
#endif
#ifdef BUILD_FOR_10_4
	[pool release];
#endif
	return 0;
}

#if defined(BUILD_FOR_10_4) || defined(BUILD_FOR_10_7) || defined(BUILD_FOR_11_0)
#ifdef USE_SDL_MAIN
int SDL_main(int argc, char* argv[])
{
	return sdl_app(argc, argv);
}
#else
int main(int argc, char* argv[])
{
	return sdl_app(argc, argv);
}
#endif

#else

#ifdef __MACOS__
int SDL_main(int argc, char* argv[])
{
	return sdl_app(argc, argv);
}
#else
int main(int argc, char* argv[])
{
	return sdl_app(argc, argv);
}
#endif
#endif
