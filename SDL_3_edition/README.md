# Physicsbooru / Memebooru — SDL3 edition

A small, local archive app for images **and documents** (PDF, TXT, CSV, Markdown,
LaTeX, BibTeX). Tag your files, browse them in an adaptive gallery, preview them
(including rendered PDF pages), and search by **tags and file contents**.

This edition is a ground-up rewrite on **SDL 3** with:

- **Vector fonts** via SDL3_ttf (FreeType + HarfBuzz) — crisp at any size / HiDPI.
- **Native text editing** in every field: click-to-caret, shift-select,
  word-jump (⌥/Ctrl + ←/→), select-all, copy / cut / paste via the system
  clipboard, Home/End.
- **PDF preview** with page navigation, rendered by **MuPDF**.
- **Full-text search**: TXT/CSV/MD/TEX/BIB are read directly and PDFs are text-
  extracted (MuPDF), so the search box matches document contents, not just tags.
- A **fully adaptive** layout that reflows to any window size.

Everything is drawn in pixels, so it stays sharp on Retina / HiDPI displays.

## Layout of this folder

```
SDL_3_edition/
├── CMakeLists.txt        cross-platform build
├── src/                  application source (C11)
├── assets/fonts/         bundled Roboto-Regular.ttf (Apache-2.0)
└── sources/              vendored library sources
    ├── SDL/              SDL3            (built shared)
    ├── SDL_ttf/          SDL3_ttf + FreeType/HarfBuzz (built shared)
    └── mupdf/            MuPDF           (built static, via its Makefile)
```

SDL3 and SDL3_ttf are built as **shared** libraries on purpose: that keeps their
bundled FreeType/HarfBuzz private so they don't collide with MuPDF's own copies
(which are linked statically).

## Build

### Prerequisites
- CMake ≥ 3.22
- A C/C++ toolchain (Clang, GCC, or MSVC + MSYS2 on Windows)
- GNU `make` (used to build MuPDF — already present on macOS/Linux; on Windows
  use an MSYS2/MinGW shell)

### macOS
```sh
cd SDL_3_edition
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
open build/physicsbooru.app
```

### Linux
```sh
cd SDL_3_edition
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/physicsbooru
```
The native file picker uses your desktop portal; install `zenity` or
`kdialog` if no portal is available.

### Windows (MSYS2 / MinGW-w64)
MuPDF builds with its GNU Makefile, so build from an **MSYS2 MinGW64** shell:
```sh
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake make
cd SDL_3_edition
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/physicsbooru.exe
```
(The `Roboto-Regular.ttf` is copied next to the executable automatically.)

## Where files are stored
Imported files and the `index.txt` catalog live in the per-user app data dir
returned by `SDL_GetPrefPath`:
- macOS:  `~/Library/Application Support/Physicsbooru/archive/`
- Linux:  `~/.local/share/Physicsbooru/archive/`
- Windows: `%APPDATA%\Physicsbooru\archive\`

## Usage
- **Add**: click *Add*, type space-separated tags, *Browse* for a file, *Save*.
  The file is copied into the archive.
- **Browse**: single-click any card to open it.
- **Detail**: *< Prev / Next >* (or ←/→) move between items; PDFs get page
  controls; *Edit* / *Delete* manage the entry; *Back* (or Esc) returns.
- **Search**: type in the top-right box — matches tags, filenames and contents.

## Licenses
- SDL3, SDL3_ttf — Zlib
- FreeType — FTL/GPL; HarfBuzz — MIT
- MuPDF — **AGPL v3** (commercial license available from Artifex)
- Roboto — Apache-2.0
