#!/bin/sh
# Linux에서 Windows용 dazip.exe(외부 DLL 없는 단일 파일)를 교차 컴파일합니다.
# 필요: x86_64-w64-mingw32-gcc, cmake, git
# 사용: ./build-windows.sh [작업폴더]
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
W=${1:-$HERE/build-win}
P=$W/prefix
SRC=$W/src
CC=x86_64-w64-mingw32-gcc
mkdir -p "$SRC" "$P"
TC=$W/mingw.cmake
cat > "$TC" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER $CC)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH $P)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF
CM="-DCMAKE_TOOLCHAIN_FILE=$TC -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$P -DBUILD_SHARED_LIBS=OFF"

get() { [ -d "$SRC/$(basename "$1")" ] || git clone -q --depth 1 -b "$2" "https://github.com/$1.git" "$SRC/$(basename "$1")"; }
get madler/zlib v1.3.1
get libarchive/bzip2 bzip2-1.0.8
get tukaani-project/xz v5.6.3
get facebook/zstd v1.5.6
get lz4/lz4 v1.10.0
get libarchive/libarchive v3.8.1

cd "$SRC/zlib" && make -s -f win32/Makefile.gcc PREFIX=x86_64-w64-mingw32- libz.a \
  && cp libz.a "$P/lib/" 2>/dev/null || { mkdir -p "$P/lib" "$P/include"; cp libz.a "$P/lib/"; }
mkdir -p "$P/include" && cp zlib.h zconf.h "$P/include/"

cd "$SRC/bzip2" && $CC -O2 -c blocksort.c huffman.c crctable.c randtable.c compress.c decompress.c bzlib.c \
  && x86_64-w64-mingw32-ar rcs "$P/lib/libbz2.a" ./*.o && cp bzlib.h "$P/include/"

cmake -S "$SRC/xz" -B "$W/b-xz" $CM -DXZ_NLS=OFF -DXZ_TOOL_XZ=OFF -DXZ_TOOL_XZDEC=OFF -DXZ_TOOL_LZMADEC=OFF \
  -DXZ_TOOL_LZMAINFO=OFF -DXZ_TOOL_SCRIPTS=OFF -DXZ_DOC=OFF -DBUILD_TESTING=OFF >/dev/null
cmake --build "$W/b-xz" -j"$(nproc)" --target install >/dev/null

cmake -S "$SRC/zstd/build/cmake" -B "$W/b-zstd" $CM -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_SHARED=OFF \
  -DZSTD_BUILD_TESTS=OFF -DZSTD_MULTITHREAD_SUPPORT=ON >/dev/null
cmake --build "$W/b-zstd" -j"$(nproc)" --target install >/dev/null

cmake -S "$SRC/lz4/build/cmake" -B "$W/b-lz4" $CM -DLZ4_BUILD_CLI=OFF -DLZ4_BUILD_LEGACY_LZ4C=OFF >/dev/null
cmake --build "$W/b-lz4" -j"$(nproc)" --target install >/dev/null

cmake -S "$SRC/libarchive" -B "$W/b-la" $CM -DENABLE_TEST=OFF -DENABLE_TAR=OFF -DENABLE_CPIO=OFF -DENABLE_CAT=OFF \
  -DENABLE_UNZIP=OFF -DENABLE_OPENSSL=OFF -DENABLE_LIBXML2=OFF -DENABLE_EXPAT=OFF -DENABLE_ICONV=OFF \
  -DENABLE_LIBB2=OFF -DENABLE_LZO=OFF -DENABLE_PCREPOSIX=OFF -DENABLE_PCRE2POSIX=OFF -DENABLE_ACL=OFF \
  -DENABLE_ZLIB=ON -DENABLE_BZip2=ON -DENABLE_LZMA=ON -DENABLE_ZSTD=ON -DENABLE_LZ4=ON \
  -DZLIB_LIBRARY="$P/lib/libz.a" -DZLIB_INCLUDE_DIR="$P/include" \
  -DBZIP2_LIBRARIES="$P/lib/libbz2.a" -DBZIP2_INCLUDE_DIR="$P/include" >/dev/null
cmake --build "$W/b-la" -j"$(nproc)" --target archive_static >/dev/null

# 긴 경로(260자 초과) 허용 매니페스트
cat > "$W/dazip.manifest" <<'EOF'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly manifestVersion="1.0" xmlns="urn:schemas-microsoft-com:asm.v1">
  <application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings>
      <longPathAware xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">true</longPathAware>
    </windowsSettings>
  </application>
</assembly>
EOF
printf '1 24 "%s"\n' "$W/dazip.manifest" > "$W/dazip.rc"
x86_64-w64-mingw32-windres "$W/dazip.rc" -O coff -o "$W/dazip.res"

LA=$(find "$W/b-la" -name 'libarchive*.a' | head -1)
$CC -O2 -s -static -DLIBARCHIVE_STATIC -I"$SRC/libarchive/libarchive" -o "$HERE/dazip.exe" \
  "$HERE/dazip.c" "$W/dazip.res" "$LA" "$P/lib/libzstd.a" "$P/lib/liblzma.a" "$P/lib/liblz4.a" \
  "$P/lib/libbz2.a" "$P/lib/libz.a" -lbcrypt -lxmllite -lole32 -luuid -lshell32 -ladvapi32
ls -la "$HERE/dazip.exe"
