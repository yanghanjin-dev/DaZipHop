#!/bin/sh
# DaZipHop 왕복 테스트: 모든 쓰기 형식으로 압축 -> 해제 -> 원본과 비교
set -u
BIN=${BIN:-$(cd "$(dirname "$0")" && pwd)/dazip}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
cd "$T"

mkdir -p src/sub/깊은폴더 src/empty
printf 'hello\n' > src/a.txt
head -c 300000 /dev/urandom > src/sub/rand.bin
seq 1 100000 > src/sub/깊은폴더/숫자.txt
ln -s a.txt src/link 2>/dev/null || true

pass=0; fail=0
ok()  { pass=$((pass+1)); printf '  OK   %s\n' "$1"; }
bad() { fail=$((fail+1)); printf '  FAIL %s\n' "$1"; }

echo "[다중 파일 형식]"
for ext in tar tar.gz tgz tar.bz2 tar.xz tar.zst tar.lz4 tar.lzma tar.lz tar.Z zip 7z cpio iso xar ar; do
    rm -rf out; mkdir out
    if [ "$ext" = ar ]; then  # ar은 폴더 없이 평평한 파일만 담는 형식
        (cd src && "$BIN" c -q ../t.$ext a.txt) && "$BIN" x -q -C out t.$ext \
            && cmp -s src/a.txt out/a.txt && ok "$ext" || bad "$ext"
    else
        (cd src && "$BIN" c -q ../t.$ext .) && "$BIN" t t.$ext >/dev/null && "$BIN" x -q -C out t.$ext \
            && diff -r src out >/dev/null 2>&1 && ok "$ext" || bad "$ext"
    fi
    rm -f t.$ext
done

echo "[단일 파일 형식]"
for ext in gz bz2 xz zst lz4 lzma lz Z; do
    rm -rf out; mkdir out
    "$BIN" c -q f.txt.$ext src/sub/깊은폴더/숫자.txt && "$BIN" x -q -C out f.txt.$ext \
        && cmp -s src/sub/깊은폴더/숫자.txt out/f.txt && ok "$ext" || bad "$ext"
    rm -f f.txt.$ext
done

echo "[기능]"
"$BIN" c -q -p secret enc.zip src && rm -rf out && mkdir out \
  && "$BIN" x -q -p secret -C out enc.zip && diff -r src out/src >/dev/null && ok "zip AES-256 암호" || bad "zip AES-256 암호"
"$BIN" x -q -p wrong -C out2 enc.zip 2>/dev/null && bad "틀린 암호 거부" || ok "틀린 암호 거부"
"$BIN" c -q -9 l9.tar.zst src && "$BIN" c -q -1 l1.tar.zst src && ok "레벨 옵션" || bad "레벨 옵션"
"$BIN" c -q -f tar.gz - src | "$BIN" x -q -C pipe - && diff -r src pipe/src >/dev/null && ok "표준 입출력 파이프" || bad "표준 입출력 파이프"
cp l9.tar.zst drop.tar.zst && "$BIN" -q drop.tar.zst && diff -r src drop/src >/dev/null && ok "끌어다 놓기 모드" || bad "끌어다 놓기 모드"
printf 'plain' > plain.txt; "$BIN" x -q plain.txt 2>/dev/null && bad "비압축 파일 거부" || ok "비압축 파일 거부"
head -c 2000 l9.tar.zst > broken.tar.zst; "$BIN" t broken.tar.zst >/dev/null 2>&1 && bad "손상 감지" || ok "손상 감지"

echo "[보안: 경로 탈출 차단]"
python3 - <<'PY'
import tarfile, io
with tarfile.open("evil.tar", "w") as t:
    for n in ("../escape.txt", "/tmp/dazip_abs_escape.txt"):
        d = b"pwned"; i = tarfile.TarInfo(n); i.size = len(d); t.addfile(i, io.BytesIO(d))
PY
mkdir -p jail && "$BIN" x -q -C jail evil.tar 2>/dev/null
[ ! -e escape.txt ] && [ ! -e /tmp/dazip_abs_escape.txt ] && ok "../ 와 절대경로 차단" || bad "../ 와 절대경로 차단"

echo
echo "결과: 통과 $pass, 실패 $fail"
[ "$fail" -eq 0 ]
