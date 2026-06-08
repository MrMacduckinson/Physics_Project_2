# Memebooru / Physicsbooru

Это локальный менеджер медиа-архива с тегами — что-то вроде booru, только у тебя на машине.
Позволяет хранить изображения, PDF, TXT, CSV, Markdown, LaTeX и BibTeX-файлы, тегировать их,
просматривать в галерее и искать как по тегам, так и по содержимому документов.

Проект существует в двух изданиях:

- **SDL 1.2** — собирается под широкий диапазон версий macOS, начиная от Mac OS 8.1 m68k,
  заканчивая macOS 26 arm64.
- **SDL 3** — современная полная перезапись с PDF-рендерингом (MuPDF), векторными шрифтами,
  полнотекстовым поиском и адаптивным интерфейсом; собирается под macOS, Windows и Linux.

---

## Кто и зачем?

Этот репозиторий сделан мной и моим другом в рамках обучения в ИТМО.

---

## Издание SDL 1.2

### Таргеты

| Флаг | Архитектура | Версии macOS |
|---|---|---|
| `BUILD_FOR_11_0` | arm64 | 11.0+ |
| `BUILD_FOR_10_7` | x86_64 | 10.7+ |
| `BUILD_FOR_10_4` | i386 | 10.4–10.15 |
| `BUILD_FOR_PPC` | ppc | Classic Mac OS 8.1 – Mac OS X 10.4 |
| `BUILD_FOR_M68K` | m68k | Classic Mac OS 8.1 – ??? |

### Требования

- Установленный [Retro68](https://github.com/autc04/Retro68) (только для PPC и M68K)
- macOS SDK 10.4u и 10.15, помещённые в `/Library/Developer/CommandLineTools/SDKs/`
- Apple Clang 17 (или другой совместимый компилятор)

В `Makefile.mac` и `Makefile.macm68k` нужно прописать **свои** пути к Retro68.

### Сборка

По умолчанию все флаги выставлены в `ON`. Собрать все таргеты сразу:

```sh
cmake -S . -B build
cmake --build build
```

Или только нужный:

```sh
cmake -S . -B build -DBUILD_FOR_11_0=ON -DBUILD_FOR_10_7=OFF -DBUILD_FOR_10_4=OFF -DBUILD_FOR_PPC=OFF -DBUILD_FOR_M68K=OFF
cmake --build build
```

Classic Mac OS (PPC и M68K) собираются отдельно через Makefile:

```sh
make -f Makefile.mac        # PPC
make -f Makefile.macm68k    # M68K
```

Результаты — в директории `build/`.

### Где хранятся данные

- **macOS 10.7+** — `~/Library/Application Support/Physicsbooru/archive/`
- **Classic Mac OS (PPC/M68K)** — папка `archive/` рядом с приложением

---

## Издание SDL 3

Исходники и инструкции по сборке — в [`SDL_3_edition/README.md`](SDL_3_edition/README.md).

Коротко: macOS собирается через CMake, Windows — кросс-компиляцией через MinGW-toolchain,
Linux ARM64 — через Docker-скрипт `build-linux.sh`.

### Где хранятся данные

- **macOS** — `~/Library/Application Support/Physicsbooru/archive/`
- **Windows** — `C:\Users\<username>\AppData\Roaming\Physicsbooru\archive\`
- **Linux** — `~/.local/share/Physicsbooru/archive/`

Для переноса коллекции между платформами достаточно скопировать всю папку `archive/` —
формат `index.txt`, `tags.txt` и `metatags.txt` одинаковый на всех платформах.

---

## TODO

- [ ] Добавить билд SDL 1.2 с Carbon API (закрывает дыру в 10.5 Leopard PPC)
- [ ] Добавить поддержку тегов-меток (цветовая маркировка)
- [ ] Сделать сборку более self-contained (положить SDK в репо, если лицензия позволяет)
- [ ] Перевести интерфейс (сейчас всё на английском)
- [ ] Добавить экспорт/импорт коллекции

---

## Credits

**Retro68** — кросс-компилятор для Classic Mac OS (autc04):
https://github.com/autc04/Retro68

**sdl12-compat** — SDL 1.2 API поверх SDL 2:
https://github.com/libsdl-org/sdl12-compat

**SDL 1.2 (модифицированная версия для Classic Mac OS)** — gameblabla:
https://github.com/gameblabla/SDL12_mac

**Makefile.mac, Makefile.macm68k, Retro68APPL.r** взяты (и изменены) из репозитория gameblabla:
https://github.com/gameblabla/worship-vector/tree/macosclassic

**SDL 3** — libsdl-org:
https://github.com/libsdl-org/SDL

**SDL3_ttf** — libsdl-org:
https://github.com/libsdl-org/SDL_ttf

**MuPDF** — Artifex Software (AGPL 3.0):
https://mupdf.com

**stb_image** — nothings:
https://github.com/nothings/stb
