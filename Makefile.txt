# DaZipHop (dazip) — Linux / macOS / Windows(MSYS2) 빌드
CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
PREFIX  ?= /usr/local

LA_CFLAGS := $(shell pkg-config --cflags libarchive 2>/dev/null)
LA_LIBS   := $(shell pkg-config --libs libarchive 2>/dev/null || echo -larchive)

dazip: dazip.c
	$(CC) $(CFLAGS) $(LA_CFLAGS) -o $@ $< $(LA_LIBS)

# 외부 라이브러리 없이 실행되는 단일 파일 (정적 libarchive가 설치된 경우)
static: dazip.c
	$(CC) $(CFLAGS) -static -DLIBARCHIVE_STATIC $(shell pkg-config --static --cflags libarchive) \
	  -o dazip $< $(shell pkg-config --static --libs libarchive)

# Linux에서 Windows용 dazip.exe 만들기
windows:
	./build-windows.sh

test: dazip
	./test.sh

install: dazip
	install -Dm755 dazip $(DESTDIR)$(PREFIX)/bin/dazip

clean:
	rm -f dazip dazip.exe

.PHONY: static windows test install clean
