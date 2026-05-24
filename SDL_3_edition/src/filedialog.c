#include "filedialog.h"
#include <string.h>

static char g_result[2048];
static bool g_have_result = false;

static void SDLCALL on_files(void* userdata, const char* const* filelist, int filter)
{
    (void)userdata; (void)filter;
    if (!filelist || !filelist[0]) return;   /* cancelled or error */
    SDL_strlcpy(g_result, filelist[0], sizeof(g_result));
    g_have_result = true;
}

void filedialog_open(SDL_Window* win)
{
    static const SDL_DialogFileFilter filters[] = {
        { "Supported files", "png;jpg;jpeg;pdf;txt;csv;md;tex;bib;mp4" },
        { "Images",          "png;jpg;jpeg" },
        { "Documents",       "pdf;txt;csv;md;tex;bib" },
        { "All files",       "*" },
    };
    SDL_ShowOpenFileDialog(on_files, NULL, win, filters,
                           (int)SDL_arraysize(filters), NULL, false);
}

const char* filedialog_take_result(void)
{
    if (!g_have_result) return NULL;
    g_have_result = false;
    return g_result;
}
