# DaZipHop (dazip)

배포 버전: **v0.0.1** (압축 엔진 `dazip` 자체 버전: 1.0.0).

Windows용 다운로드: [DaZipHop v0.0.1](https://github.com/yanghanjin-dev/DaZipHop/releases/tag/v0.0.1)의 `DaZipHop-v0.0.1-windows-x64.zip`을 풀고 `DaZipHop.exe`를 실행하세요.

가볍고 빠른 만능 압축/해제 도구. C 파일 하나(약 800줄)로 되어 있고, 압축 엔진은 검증된 **libarchive**와 각 형식의 공식 라이브러리(zlib, liblzma, libzstd, liblz4, bzip2)를 그대로 씁니다.

- Linux 실행 파일 약 45 KB, Windows용 `dazip.exe`는 DLL 없이 단독 실행되는 약 2.2 MB
- 형식은 **출력 파일 확장자**로 정하고, 해제할 때는 **내용을 보고 자동 판별**
- zstd·xz는 기본으로 모든 CPU 코어 사용, 1 MiB 버퍼, 희소 파일 그대로 해제

## 지원 형식

| 구분 | 형식 |
|---|---|
| 압축 + 해제 | zip, 7z, tar, tar.gz/tgz, tar.bz2, tar.xz, tar.zst, tar.lz4, tar.lzma, tar.lz, tar.Z, cpio, iso, xar, ar, shar(압축만), warc |
| 단일 파일 | .gz .bz2 .xz .zst .lz4 .lzma .lz .Z |
| 해제 전용 | rar, rar5, cab, lha/lzh, rpm, deb, 자기 해제(SFX) zip/rar |

RAR 만들기는 RARLAB의 독점 규격이라 지원하지 않습니다(오픈소스 도구 모두 마찬가지). `dazip formats`로 현재 빌드에서 쓸 수 있는 형식을 확인할 수 있습니다.

## 사용법

### Windows 창으로 사용

`DaZipHop.exe`를 더블클릭합니다. 같은 폴더에 압축 엔진인 `dazip.exe`가 있어야 합니다.

1. **파일 압축** 탭에서 파일 또는 폴더를 추가합니다. 목록에 끌어다 놓기도 가능합니다.
2. ZIP, 7z, TAR, tar.gz, tar.bz2, tar.xz, tar.zst 등 압축 형식을 선택하고 **찾아보기**로 저장 위치를 정합니다.
3. **압축하기**를 누릅니다. 암호를 지정하려면 ZIP을 선택합니다.
4. 해제할 때는 **압축 해제** 탭에서 압축파일과 저장 폴더를 선택한 뒤 **압축 해제하기**를 누릅니다. 입력 형식은 자동으로 판별합니다.

여러 압축파일을 한 번에 해제하면 각각 이름별 하위 폴더에 저장합니다. 기본값은 기존 파일을 덮어쓰지 않는 설정입니다.
단일 스트림 형식(gz, bz2, xz, zst, lz4)은 파일 하나만 압축할 수 있습니다. 진행 중에는 창을 닫을 수 없으며, 결과와 오류는 작업 기록에 표시합니다.

GUI 소스는 `DaZipHop.Gui.cs`이며 Windows PowerShell에서 `powershell -ExecutionPolicy Bypass -File .\build-gui.ps1`로 빌드합니다(.NET Framework 4.x 필요).

### 명령줄로 사용

```sh
dazip c backup.tar.zst ~/문서            # 압축 (확장자로 형식 결정)
dazip c -9 -p 비밀번호 secret.zip a.txt 폴더/   # 최고 압축 + AES-256 암호
dazip c log.txt.xz log.txt              # 파일 하나만 압축
dazip x movie.rar -C ~/받은파일           # 해제 (형식 자동 판별)
dazip l archive.7z                      # 목록
dazip t archive.zip                     # 무결성 검사
dazip archive.zip                       # 옆에 'archive' 폴더를 만들어 바로 해제
```

Windows에서는 압축 파일을 `dazip.exe` 아이콘 위로 끌어다 놓으면 같은 이름의 폴더로 풀립니다. 오류가 나면 창이 닫히지 않고 기다립니다.

| 옵션 | 의미 |
|---|---|
| `-0` … `-9`, `-l N` | 압축 레벨 (zstd는 1~22) |
| `-t N` | xz/zstd 스레드 수 (기본: 전체 코어) |
| `-f 형식` | 형식 강제 (예: `-f tar.zst`), 표준 출력으로 낼 때 필요 |
| `-C 폴더` | 해제 위치 |
| `-p 암호` | zip AES-256 암호화 / 해제 암호 (생략하면 필요할 때 물어봄) |
| `-c 문자집합` | 옛 zip의 파일 이름 인코딩. Linux·macOS에서 한국어 Windows 탐색기로 만든 zip은 `-c CP949` |
| `-k` | 기존 파일 덮어쓰지 않기 |
| `-X` | xattr/ACL/파일 플래그 보존 |
| `-v` / `-q` | 자세히 / 조용히 |

입출력에 `-`를 쓰면 파이프로 쓸 수 있습니다: `dazip c -f tar.zst - 폴더 | ssh 서버 'dazip x -'`

## 안전장치

해제 시 `../` 경로, 절대 경로, 심볼릭 링크를 통한 폴더 밖 쓰기를 모두 막습니다. 디스크의 다른 파일을 참조할 수 있는 mtree 형식은 일부러 읽지 않습니다. 압축 중인 결과 파일 자신은 압축에 넣지 않습니다.

## 빌드

**Linux**
```sh
sudo apt install libarchive-dev     # Fedora: libarchive-devel
make && make test
```

**macOS**
```sh
brew install libarchive
PKG_CONFIG_PATH="$(brew --prefix libarchive)/lib/pkgconfig" make
```

**Windows**: 동봉된 `dazip.exe`를 그대로 쓰면 됩니다. 직접 빌드하려면
- Linux/WSL에서 교차 컴파일: `sudo apt install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 cmake git` 후 `./build-windows.sh`
- MSYS2: `pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libarchive make` 후 `make`

## 참고 및 한계

- Windows 판은 경로를 UTF-16 API로 다뤄 한글 경로가 그대로 유지되며, 압축 파일 안의 이름은 UTF-8로 저장합니다(zip은 UTF-8 플래그 포함 — Windows 10 이상 탐색기, 7-Zip, macOS에서 정상 표시).
- 한국어 Windows에서 cpio 안의 한글 이름은 `-c UTF-8`을 붙여 풀어야 합니다(cpio에는 인코딩 표시가 없음). Windows 판에서 만든 iso는 한글 이름이 `?`로 바뀝니다.
- Windows 판의 xar 해제는 Wine 환경에서 XmlLite 문제로 실패해 실제 Windows에서 검증되지 않았습니다.
- 암호는 zip(AES-256, 옛 ZipCrypto 해제 포함)만 지원합니다. 암호 걸린 7z·rar는 libarchive가 풀지 못합니다.
