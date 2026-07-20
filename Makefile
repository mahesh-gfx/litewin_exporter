CC64=x86_64-w64-mingw32-gcc
CC32=i686-w64-mingw32-gcc
CFLAGS=-O2 -s -static -Wall -Wextra -Werror -D_WIN32_WINNT=0x0601 -Isrc
LIBS=-lws2_32 -ladvapi32 -lpdh
SRC=$(wildcard src/*.c src/collectors/*.c)

all: dist/litewin_exporter_amd64.exe dist/litewin_exporter_386.exe

dist/litewin_exporter_amd64.exe: $(SRC)
	@mkdir -p dist && $(CC64) $(CFLAGS) -o $@ $(SRC) $(LIBS)

dist/litewin_exporter_386.exe: $(SRC)
	@mkdir -p dist && $(CC32) $(CFLAGS) -o $@ $(SRC) $(LIBS)

clean:
	rm -rf dist

# dev cycle: cross-compile both exes, then native unit tests.
dev:
	@$(MAKE) all
	@$(MAKE) -f Makefile.test unit

# Watch mode: re-runs `make dev` on save; spacebar forces a run, q quits.
watch:
	find src tests -name '*.c' -o -name '*.h' | entr -c $(MAKE) dev

.PHONY: all clean dev watch
