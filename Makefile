CC64=x86_64-w64-mingw32-gcc
CC32=i686-w64-mingw32-gcc
WINDRES64=x86_64-w64-mingw32-windres
WINDRES32=i686-w64-mingw32-windres
CFLAGS=-O2 -s -static -Wall -Wextra -Werror -D_WIN32_WINNT=0x0601 -Isrc
LIBS=-lws2_32 -ladvapi32 -lpdh
SRC=$(wildcard src/*.c src/collectors/*.c)
RES=src/resource.rc src/litewin.manifest src/version.h

all: dist/litewin_exporter_amd64.exe dist/litewin_exporter_386.exe

# Compile the version/manifest resource (windres) and link it into the exe.
dist/litewin_exporter_amd64.exe: $(SRC) $(RES)
	@mkdir -p dist build
	$(WINDRES64) --include-dir src src/resource.rc -O coff -o build/res_amd64.o
	$(CC64) $(CFLAGS) -o $@ $(SRC) build/res_amd64.o $(LIBS)

dist/litewin_exporter_386.exe: $(SRC) $(RES)
	@mkdir -p dist build
	$(WINDRES32) --include-dir src src/resource.rc -O coff -o build/res_386.o
	$(CC32) $(CFLAGS) -o $@ $(SRC) build/res_386.o $(LIBS)

clean:
	rm -rf dist build

# dev cycle: cross-compile both exes, then native unit tests.
dev:
	@$(MAKE) all
	@$(MAKE) -f Makefile.test unit

# Watch mode: re-runs `make dev` on save; spacebar forces a run, q quits.
watch:
	find src tests -name '*.c' -o -name '*.h' | entr -c $(MAKE) dev

.PHONY: all clean dev watch
