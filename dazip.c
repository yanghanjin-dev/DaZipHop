/*
 * DaZipHop (dazip) - 가볍고 빠른 만능 압축/해제 도구
 *
 * libarchive 위에서 동작하는 단일 C 파일 프로그램입니다.
 *   압축  : zip, 7z, tar(+gz/bz2/xz/zst/lz4/lzma/lz/Z/lzo), cpio, iso, xar, ar, shar, warc,
 *           단일 파일 gz/bz2/xz/zst/lz4/lzma/lz/Z
 *   해제  : 위 전부 + rar, rar5, cab, lha/lzh, rpm, deb(ar), mtree 등 libarchive가 읽는 모든 형식
 *
 * 형식은 출력 파일 확장자로 정하고(-f로 강제 가능), 해제 시에는 내용으로 자동 판별합니다.
 */
#define _FILE_OFFSET_BITS 64
#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* 플랫폼 계층: 프로그램 안의 모든 경로 문자열은 UTF-8입니다.            */
/* Windows에서는 와이드(UTF-16) API로 바꿔 불러 한글 이름이 깨지지 않게 합니다. */
/* ------------------------------------------------------------------ */
#ifdef _WIN32
#  include <windows.h>
#  include <shellapi.h>
#  include <direct.h>
#  include <conio.h>
#  include <io.h>
#  define strcasecmp _stricmp
#  define isatty _isatty
#  define fileno _fileno
static wchar_t *W(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = malloc((size_t)(n > 0 ? n : 1) * sizeof *w);
    if (n <= 0) { w[0] = 0; return w; }
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}
static char *U(const wchar_t *w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = malloc((size_t)(n > 0 ? n : 1));
    if (n <= 0) { s[0] = 0; return s; }
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}
static int MKDIR(const char *p) { wchar_t *w = W(p); int r = _wmkdir(w); free(w); return r; }
static int CHDIR(const char *p) { wchar_t *w = W(p); int r = _wchdir(w); free(w); return r; }
static int get_cwd(char *buf, size_t n)
{
    wchar_t wb[4096];
    if (!_wgetcwd(wb, 4096)) return -1;
    return WideCharToMultiByte(CP_UTF8, 0, wb, -1, buf, (int)n, NULL, NULL) > 0 ? 0 : -1;
}
static long long file_size(const char *p)
{
    struct _stat64 s; wchar_t *w = W(p); int r = _wstat64(w, &s); free(w);
    return r ? -1 : (long long)s.st_size;
}
static int open_read(struct archive *a, const char *p, size_t b)
{
    if (!p) return archive_read_open_filename(a, NULL, b);
    wchar_t *w = W(p); int r = archive_read_open_filename_w(a, w, b); free(w); return r;
}
static int open_write(struct archive *a, const char *p)
{
    if (!p) return archive_write_open_filename(a, NULL);
    wchar_t *w = W(p); int r = archive_write_open_filename_w(a, w); free(w); return r;
}
static int disk_open(struct archive *a, const char *p)
{
    wchar_t *w = W(p); int r = archive_read_disk_open_w(a, w); free(w); return r;
}
#else
#  include <strings.h>
#  include <unistd.h>
#  define MKDIR(p) mkdir((p), 0755)
#  define CHDIR(p) chdir(p)
static int get_cwd(char *buf, size_t n) { return getcwd(buf, n) ? 0 : -1; }
static long long file_size(const char *p) { struct stat s; return stat(p, &s) ? -1 : (long long)s.st_size; }
#  define open_read(a, p, b) archive_read_open_filename((a), (p), (b))
#  define open_write(a, p) archive_write_open_filename((a), (p))
#  define disk_open(a, p) archive_read_disk_open((a), (p))
#endif

/* 항목 이름을 UTF-8로 읽고 쓰기 */
static const char *ename(struct archive_entry *e)
{
    const char *s = archive_entry_pathname_utf8(e);
    if (!s) s = archive_entry_pathname(e);
    return s ? s : "?";
}
#define set_ename(e, s) archive_entry_update_pathname_utf8((e), (s))

#define DAZIP_VERSION "1.0.0"
#define BUFSZ (1 << 20) /* 1 MiB 입출력 버퍼 */

/* ------------------------------------------------------------------ */
/* 형식 표: 확장자 -> (libarchive 형식 이름, 필터 이름)                 */
/* 긴 확장자를 먼저 두어 .tar.gz가 .gz보다 먼저 맞게 합니다.            */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *suffix;
    const char *format; /* archive_write_set_format_by_name */
    const char *filter; /* archive_write_add_filter_by_name, NULL = 없음 */
} fmt_map;

static const fmt_map FMTS[] = {
    {".tar.gz", "paxr", "gzip"},    {".tgz", "paxr", "gzip"},
    {".tar.bz2", "paxr", "bzip2"},  {".tbz2", "paxr", "bzip2"},
    {".tbz", "paxr", "bzip2"},      {".tar.xz", "paxr", "xz"},
    {".txz", "paxr", "xz"},         {".tar.zst", "paxr", "zstd"},
    {".tzst", "paxr", "zstd"},      {".tar.lz4", "paxr", "lz4"},
    {".tar.lzma", "paxr", "lzma"},  {".tlz", "paxr", "lzma"},
    {".tar.lz", "paxr", "lzip"},    {".tar.Z", "paxr", "compress"},
    {".taz", "paxr", "compress"},   {".tar.lzo", "paxr", "lzop"},
    {".tar", "paxr", NULL},
    {".zip", "zip", NULL},          {".jar", "zip", NULL},
    {".7z", "7zip", NULL},
    {".cpio", "newc", NULL},        {".iso", "iso9660", NULL},
    {".xar", "xar", NULL},          {".ar", "argnu", NULL},
    {".shar", "shar", NULL},        {".warc", "warc", NULL},
    /* 단일 파일 압축 (파일 하나만) */
    {".gz", "raw", "gzip"},         {".bz2", "raw", "bzip2"},
    {".xz", "raw", "xz"},           {".zst", "raw", "zstd"},
    {".lz4", "raw", "lz4"},         {".lzma", "raw", "lzma"},
    {".lz", "raw", "lzip"},         {".Z", "raw", "compress"},
    {".lzo", "raw", "lzop"},
    {NULL, NULL, NULL}};

/* 해제 전용으로 알아볼 확장자 (폴더 이름 만들 때 떼어 냄) */
static const char *READ_ONLY_EXT[] = {".rar", ".cab", ".lha", ".lzh", ".rpm", ".deb",
                                      ".apk", ".war", ".ear", ".cbz", ".cbr", NULL};

typedef struct {
    int level;          /* -1 = 기본값 */
    int threads;        /* 0 = 자동 */
    int keep;           /* 기존 파일 덮어쓰지 않음 */
    int verbose;
    int quiet;
    int meta;           /* xattr/ACL/fflags 보존 */
    const char *format; /* -f 강제 형식 */
    const char *outdir; /* -C 해제 위치 */
    const char *pass;   /* -p 암호 */
    const char *charset; /* -c 이름 문자 집합 (예: CP949) */
    int asked;          /* 암호 프롬프트를 이미 띄웠는지 */
    char passbuf[256];
} opts;

static char g_buf[BUFSZ];

/* ------------------------------------------------------------------ */
/* 유틸리티                                                            */
/* ------------------------------------------------------------------ */
static int ends_with(const char *s, const char *suf)
{
    size_t ls = strlen(s), lf = strlen(suf);
    return ls > lf && strcasecmp(s + ls - lf, suf) == 0;
}

static const fmt_map *find_fmt(const char *name, int forced)
{
    char tmp[64];
    if (forced) { /* -f zip / -f .tar.zst 둘 다 허용 */
        snprintf(tmp, sizeof tmp, "%s%s", name[0] == '.' ? "" : ".", name);
        for (const fmt_map *f = FMTS; f->suffix; f++)
            if (strcasecmp(f->suffix, tmp) == 0) return f;
        return NULL;
    }
    for (const fmt_map *f = FMTS; f->suffix; f++)
        if (ends_with(name, f->suffix)) return f;
    return NULL;
}

static const char *base_name(const char *p)
{
    const char *b = p;
    for (const char *s = p; *s; s++)
        if (*s == '/' || *s == '\\') b = s + 1;
    return b;
}

/* "dir/foo.tar.gz" -> "foo" (out 크기 n) */
static void strip_ext(const char *path, char *out, size_t n)
{
    snprintf(out, n, "%s", base_name(path));
    for (const fmt_map *f = FMTS; f->suffix; f++)
        if (ends_with(out, f->suffix)) { out[strlen(out) - strlen(f->suffix)] = 0; return; }
    for (const char **e = READ_ONLY_EXT; *e; e++)
        if (ends_with(out, *e)) { out[strlen(out) - strlen(*e)] = 0; return; }
    char *dot = strrchr(out, '.');
    if (dot && dot != out) *dot = 0;
    else { size_t l = strlen(out); if (l + 4 < n) strcat(out, ".out"); }
}

/* "dir/foo.tar.gz" -> "dir" */
static void dir_name(const char *path, char *out, size_t n)
{
    const char *b = base_name(path);
    if (b == path) { snprintf(out, n, "."); return; }
    size_t l = (size_t)(b - path - 1);
    if (l == 0) l = 1; /* "/foo" -> "/" */
    if (l >= n) l = n - 1;
    memcpy(out, path, l);
    out[l] = 0;
}

static int mkdir_p(const char *path)
{
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if ((*p == '/' || *p == '\\') && p[-1] != ':') { /* "C:" 같은 드라이브는 건너뜀 */
            char c = *p; *p = 0;
            if (MKDIR(tmp) != 0 && errno != EEXIST) return -1;
            *p = c;
        }
    }
    if (MKDIR(tmp) != 0 && errno != EEXIST) return -1;
    return 0;
}

static int ncpu(void)
{
#ifdef _WIN32
    SYSTEM_INFO si; GetSystemInfo(&si); return (int)si.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
#endif
}

static void human(int64_t v, char *out, size_t n)
{
    const char *u[] = {"B", "K", "M", "G", "T", "P"};
    double d = (double)v; int i = 0;
    while (d >= 1024 && i < 5) { d /= 1024; i++; }
    if (i == 0) snprintf(out, n, "%lld B", (long long)v);
    else snprintf(out, n, "%.1f %s", d, u[i]);
}

static double now_sec(void)
{
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}

static void read_secret(const char *prompt, char *buf, size_t n)
{
#ifdef _WIN32
    size_t i = 0; int c;
    fputs(prompt, stderr);
    while ((c = _getch()) != '\r' && c != '\n' && c != EOF) {
        if (c == '\b') { if (i) i--; } else if (i + 1 < n) buf[i++] = (char)c;
    }
    buf[i] = 0; fputc('\n', stderr);
#else
    char *p = getpass(prompt);
    snprintf(buf, n, "%s", p ? p : "");
#endif
}

static const char *pass_cb(struct archive *a, void *ud)
{
    (void)a;
    opts *o = ud;
    if (o->asked) return NULL; /* 한 번만 묻고 틀리면 포기 */
    o->asked = 1;
    read_secret("암호: ", o->passbuf, sizeof o->passbuf);
    return o->passbuf;
}

/* ------------------------------------------------------------------ */
/* 압축                                                                */
/* ------------------------------------------------------------------ */
static int set_opt(struct archive *a, const char *mod, const char *key, int val, int quiet)
{
    char buf[128];
    snprintf(buf, sizeof buf, "%s:%s=%d", mod, key, val);
    int r = archive_write_set_options(a, buf);
    if (r < ARCHIVE_OK && !quiet)
        fprintf(stderr, "dazip: 경고: 옵션 '%s' 무시됨 (%s)\n", buf, archive_error_string(a));
    return r;
}

/* 안전한 상대 경로로 정리. 빈 문자열이면 건너뜀 */
static void clean_path(const char *in, char *out, size_t n)
{
    const char *p = in;
    for (;;) {
        if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':') p += 2;
        else if (*p == '/' || *p == '\\') p++;
        else if (p[0] == '.' && (p[1] == '/' || p[1] == '\\')) p += 2;
        else if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\\')) p += 3;
        else break;
    }
    if (strcmp(p, ".") == 0 || strcmp(p, "..") == 0) p = "";
    snprintf(out, n, "%s", p);
}

static int cmd_create(opts *o, const char *out, char **inputs, int nin)
{
    const fmt_map *f = o->format ? find_fmt(o->format, 1) : find_fmt(out, 0);
    if (!f) {
        fprintf(stderr, "dazip: '%s'의 형식을 알 수 없습니다. 확장자를 붙이거나 -f로 지정하세요 "
                        "(dazip formats 참고).\n", o->format ? o->format : out);
        return 2;
    }
    int is_raw = strcmp(f->format, "raw") == 0;
    if (is_raw && nin != 1) {
        fprintf(stderr, "dazip: %s 형식은 파일 하나만 담을 수 있습니다. 여러 개라면 .tar%s를 쓰세요.\n",
                f->suffix, f->suffix);
        return 2;
    }

    struct archive *a = archive_write_new();
    if (archive_write_set_format_by_name(a, f->format) != ARCHIVE_OK) {
        fprintf(stderr, "dazip: 형식 '%s' 사용 불가: %s\n", f->format, archive_error_string(a));
        archive_write_free(a); return 1;
    }
    if (f->filter) {
        int r = archive_write_add_filter_by_name(a, f->filter);
        if (r < ARCHIVE_WARN) {
            fprintf(stderr, "dazip: 압축 방식 '%s' 사용 불가: %s\n", f->filter, archive_error_string(a));
            archive_write_free(a); return 1;
        }
    }
    const char *mod = f->filter ? f->filter
                    : strcmp(f->format, "zip") == 0  ? "zip"
                    : strcmp(f->format, "7zip") == 0 ? "7zip" : NULL;
    if (o->level >= 0 && mod) set_opt(a, mod, "compression-level", o->level, o->quiet);
    if (f->filter && (strcmp(f->filter, "xz") == 0 || strcmp(f->filter, "zstd") == 0))
        set_opt(a, f->filter, "threads", o->threads > 0 ? o->threads : ncpu(), 1);

    if (o->pass) {
        if (strcmp(f->format, "zip") != 0) {
            fprintf(stderr, "dazip: 암호화는 zip 형식에서만 지원합니다.\n");
            archive_write_free(a); return 2;
        }
        if (archive_write_set_options(a, "zip:encryption=aes256") < ARCHIVE_OK ||
            archive_write_set_passphrase(a, o->pass) != ARCHIVE_OK) {
            fprintf(stderr, "dazip: 암호화 설정 실패: %s\n", archive_error_string(a));
            archive_write_free(a); return 1;
        }
    }

    /* 파일 이름은 UTF-8로 저장 (zip에는 UTF-8 플래그가 붙어 어느 OS에서나 한글이 맞게 보임) */
    archive_write_set_format_option(a, NULL, "hdrcharset", "UTF-8");
    archive_write_set_bytes_in_last_block(a, 1);
    int to_stdout = strcmp(out, "-") == 0;
    if (open_write(a, to_stdout ? NULL : out) != ARCHIVE_OK) {
        fprintf(stderr, "dazip: '%s' 열기 실패: %s\n", out, archive_error_string(a));
        archive_write_free(a); return 1;
    }
#ifdef _WIN32
    /* Windows의 stat은 st_ino가 0이라 전체 경로로 비교 */
    wchar_t out_full[4096], ent_full[4096];
    int have_out = 0;
    if (!to_stdout) { wchar_t *w = W(out); have_out = _wfullpath(out_full, w, 4096) != NULL; free(w); }
#else
    struct stat out_st; int have_out = !to_stdout && stat(out, &out_st) == 0;
#endif

    struct archive *disk = archive_read_disk_new();
    archive_read_disk_set_standard_lookup(disk);
    archive_read_disk_set_symlink_physical(disk);
    if (!o->meta) {
        int beh = 0;
#ifdef ARCHIVE_READDISK_NO_XATTR
        beh |= ARCHIVE_READDISK_NO_XATTR;
#endif
#ifdef ARCHIVE_READDISK_NO_ACL
        beh |= ARCHIVE_READDISK_NO_ACL;
#endif
#ifdef ARCHIVE_READDISK_NO_FFLAGS
        beh |= ARCHIVE_READDISK_NO_FFLAGS;
#endif
        archive_read_disk_set_behavior(disk, beh);
    }

    int errs = 0; long files = 0; int64_t total = 0;
    double t0 = now_sec();
    char path[4096];

    for (int i = 0; i < nin; i++) {
        if (disk_open(disk, inputs[i]) != ARCHIVE_OK) {
            fprintf(stderr, "dazip: '%s': %s\n", inputs[i], archive_error_string(disk));
            errs++; continue;
        }
        for (;;) {
            struct archive_entry *e = archive_entry_new();
            int r = archive_read_next_header2(disk, e);
            if (r == ARCHIVE_EOF) { archive_entry_free(e); break; }
            if (r < ARCHIVE_WARN) {
                fprintf(stderr, "dazip: %s\n", archive_error_string(disk));
                archive_entry_free(e); errs++; break;
            }
            if (r == ARCHIVE_WARN && !o->quiet)
                fprintf(stderr, "dazip: 경고: %s\n", archive_error_string(disk));
            archive_read_disk_descend(disk);

            /* 만들고 있는 압축 파일 자신은 넣지 않음 */
#ifdef _WIN32
            const wchar_t *sp = archive_entry_sourcepath_w(e);
            if (have_out && sp && _wfullpath(ent_full, sp, 4096) && _wcsicmp(ent_full, out_full) == 0) {
#else
            const struct stat *st = archive_entry_stat(e);
            if (have_out && st->st_dev == out_st.st_dev && st->st_ino == out_st.st_ino) {
#endif
                archive_entry_free(e); continue;
            }
            if (is_raw) {
                if (archive_entry_filetype(e) != AE_IFREG) {
                    fprintf(stderr, "dazip: %s 형식은 일반 파일 하나만 담을 수 있습니다: %s\n",
                            f->suffix, inputs[i]);
                    archive_entry_free(e); errs++; break;
                }
                set_ename(e, base_name(inputs[i]));
            } else {
                clean_path(ename(e), path, sizeof path);
                if (!path[0]) { archive_entry_free(e); continue; }
                set_ename(e, path);
            }

            if (o->verbose) fprintf(stderr, "a %s\n", ename(e));
            r = archive_write_header(a, e);
            if (r < ARCHIVE_WARN) {
                fprintf(stderr, "dazip: %s: %s\n", ename(e), archive_error_string(a));
                errs++;
                if (r == ARCHIVE_FATAL) { archive_entry_free(e); goto done; }
            } else if (archive_entry_filetype(e) == AE_IFREG && archive_entry_size(e) > 0) {
                la_ssize_t len;
                while ((len = archive_read_data(disk, g_buf, BUFSZ)) > 0) {
                    if (archive_write_data(a, g_buf, (size_t)len) < 0) {
                        fprintf(stderr, "dazip: 쓰기 실패: %s\n", archive_error_string(a));
                        archive_entry_free(e); errs++; goto done;
                    }
                    total += len;
                }
                if (len < 0) {
                    fprintf(stderr, "dazip: %s 읽기 실패: %s\n", ename(e),
                            archive_error_string(disk));
                    errs++;
                }
            }
            files++;
            archive_entry_free(e);
        }
        archive_read_close(disk);
    }
done:
    archive_read_free(disk);
    if (archive_write_close(a) != ARCHIVE_OK) {
        fprintf(stderr, "dazip: 마무리 실패: %s\n", archive_error_string(a));
        errs++;
    }
    archive_write_free(a);

    if (!o->quiet && !to_stdout) {
        char h1[32], h2[32];
        double dt = now_sec() - t0;
        long long osz = file_size(out);
        human(total, h1, sizeof h1);
        human(osz > 0 ? osz : 0, h2, sizeof h2);
        fprintf(stderr, "%s: 항목 %ld개, %s -> %s (%.2f초)\n", out, files, h1, h2, dt);
    }
    return errs ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* 읽기 (해제 / 목록 / 검사 공통)                                       */
/* ------------------------------------------------------------------ */
static struct archive *open_reader(opts *o, const char *path)
{
    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    const fmt_map *f = strcmp(path, "-") ? find_fmt(path, 0) : NULL;
    if (!(f && strcmp(f->format, "raw") == 0)) {
        /* mtree는 일부러 뺍니다: 디스크의 다른 파일을 참조할 수 있고,
           평범한 텍스트를 mtree로 잘못 알아보는 일이 있습니다. */
        archive_read_support_format_7zip(a);
        archive_read_support_format_ar(a);
        archive_read_support_format_cab(a);
        archive_read_support_format_cpio(a);
        archive_read_support_format_empty(a);
        archive_read_support_format_iso9660(a);
        archive_read_support_format_lha(a);
        archive_read_support_format_rar(a);
        archive_read_support_format_rar5(a);
        archive_read_support_format_tar(a);
        archive_read_support_format_warc(a);
        archive_read_support_format_xar(a);
        archive_read_support_format_zip(a);
    }
    /* .gz 같은 단일 파일 압축. 확장자가 .gz/.xz 등이면 이것만 씁니다 (gunzip처럼 동작) */
    archive_read_support_format_raw(a);
    if (o->pass) archive_read_add_passphrase(a, o->pass);
#ifndef _WIN32
    else if (isatty(fileno(stderr))) archive_read_set_passphrase_callback(a, o, pass_cb);
#else
    else archive_read_set_passphrase_callback(a, o, pass_cb);
#endif
    if (o->charset) {
        char opt[96];
        snprintf(opt, sizeof opt, "hdrcharset=%s", o->charset);
        if (archive_read_set_options(a, opt) < ARCHIVE_WARN && !o->quiet)
            fprintf(stderr, "dazip: 경고: 문자 집합 %s 를 쓸 수 없습니다.\n", o->charset);
    }
    if (open_read(a, strcmp(path, "-") == 0 ? NULL : path, BUFSZ) != ARCHIVE_OK) {
        fprintf(stderr, "dazip: '%s': %s\n", path, archive_error_string(a));
        archive_read_free(a);
        return NULL;
    }
    return a;
}

/* raw 형식인데 압축도 안 되어 있으면 = 압축 파일이 아님 */
static int not_an_archive(struct archive *a, const char *path)
{
    if (archive_format(a) == ARCHIVE_FORMAT_RAW && archive_filter_code(a, 0) == ARCHIVE_FILTER_NONE) {
        fprintf(stderr, "dazip: '%s'은(는) 지원하는 압축 형식이 아닙니다.\n", path);
        return 1;
    }
    return 0;
}

static void raw_name(struct archive_entry *e, const char *path)
{
    char nm[1024];
    if (strcmp(path, "-") == 0) snprintf(nm, sizeof nm, "stdin.out");
    else strip_ext(path, nm, sizeof nm);
    set_ename(e, nm);
}

static int copy_data(struct archive *ar, struct archive *aw, const char *name)
{
    const void *buf; size_t size; la_int64_t off;
    for (;;) {
        int r = archive_read_data_block(ar, &buf, &size, &off);
        if (r == ARCHIVE_EOF) return ARCHIVE_OK;
        if (r < ARCHIVE_WARN) {
            fprintf(stderr, "dazip: %s: %s\n", name, archive_error_string(ar));
            return r;
        }
        if (aw && archive_write_data_block(aw, buf, size, off) < ARCHIVE_WARN) {
            fprintf(stderr, "dazip: %s: %s\n", name, archive_error_string(aw));
            return ARCHIVE_FAILED;
        }
    }
}

/* smart: 드래그 앤 드롭 모드 — 압축 파일 옆에 같은 이름의 폴더로 풉니다 */
static int cmd_extract(opts *o, const char *path, int smart)
{
    struct archive *a = open_reader(o, path);
    if (!a) return 1;

    struct archive *w = archive_write_disk_new();
    int flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_SECURE_SYMLINKS |
                ARCHIVE_EXTRACT_SECURE_NODOTDOT | ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS;
    if (o->meta) flags |= ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS | ARCHIVE_EXTRACT_XATTR;
    if (o->keep) flags |= ARCHIVE_EXTRACT_NO_OVERWRITE;
    archive_write_disk_set_options(w, flags);
    archive_write_disk_set_standard_lookup(w);

    int errs = 0, first = 1; long files = 0;
    double t0 = now_sec();
    struct archive_entry *e;
    int r;
    while ((r = archive_read_next_header(a, &e)) != ARCHIVE_EOF) {
        if (r < ARCHIVE_WARN) {
            fprintf(stderr, "dazip: '%s': %s\n", path, archive_error_string(a));
            errs++; break;
        }
        if (r == ARCHIVE_WARN && !o->quiet) fprintf(stderr, "dazip: 경고: %s\n", archive_error_string(a));

        if (first) {
            first = 0;
            if (not_an_archive(a, path)) { errs++; break; }
            /* 해제 위치 결정 후 그곳으로 이동 */
            char dest[4096];
            if (o->outdir) snprintf(dest, sizeof dest, "%s", o->outdir);
            else if (smart) {
                char d[2048], nm[1024];
                dir_name(path, d, sizeof d);
                if (archive_format(a) == ARCHIVE_FORMAT_RAW) snprintf(dest, sizeof dest, "%s", d);
                else { strip_ext(path, nm, sizeof nm); snprintf(dest, sizeof dest, "%s/%s", d, nm); }
            } else snprintf(dest, sizeof dest, ".");
            if (mkdir_p(dest) != 0 || CHDIR(dest) != 0) {
                fprintf(stderr, "dazip: '%s' 폴더를 쓸 수 없습니다: %s\n", dest, strerror(errno));
                errs++; break;
            }
            if (smart && !o->quiet) fprintf(stderr, "%s -> %s\n", path, dest);
        }
        if (archive_format(a) == ARCHIVE_FORMAT_RAW) raw_name(e, path);

        if (o->verbose) fprintf(stderr, "x %s\n", ename(e));
        r = archive_write_header(w, e);
        if (r < ARCHIVE_WARN) {
            fprintf(stderr, "dazip: %s: %s\n", ename(e), archive_error_string(w));
            errs++;
            if (r == ARCHIVE_FATAL) break;
            continue;
        }
        if (copy_data(a, w, ename(e)) < ARCHIVE_WARN) {
            errs++;
            if (archive_entry_is_encrypted(e)) break; /* 암호 틀림: 나머지도 실패 */
        }
        if (archive_write_finish_entry(w) < ARCHIVE_WARN) {
            fprintf(stderr, "dazip: %s: %s\n", ename(e), archive_error_string(w));
            errs++;
        }
        files++;
    }
    archive_write_close(w); archive_write_free(w);
    archive_read_close(a); archive_read_free(a);

    if (!o->quiet && !errs)
        fprintf(stderr, "%s: 항목 %ld개 해제 완료 (%.2f초)\n", path, files, now_sec() - t0);
    return errs ? 1 : 0;
}

static int cmd_list(opts *o, const char *path, int test_only)
{
    struct archive *a = open_reader(o, path);
    if (!a) return 1;
    struct archive_entry *e;
    int r, errs = 0, first = 1; long files = 0; int64_t total = 0;
    double t0 = now_sec();
    while ((r = archive_read_next_header(a, &e)) != ARCHIVE_EOF) {
        if (r < ARCHIVE_WARN) {
            fprintf(stderr, "dazip: '%s': %s\n", path, archive_error_string(a));
            errs++; break;
        }
        if (first) { first = 0; if (not_an_archive(a, path)) { errs++; break; } }
        if (archive_format(a) == ARCHIVE_FORMAT_RAW) raw_name(e, path);
        const char *nm = ename(e);

        if (test_only) {
            if (copy_data(a, NULL, nm) < ARCHIVE_WARN) errs++;
            else if (o->verbose) printf("OK  %s\n", nm);
        } else {
            char ts[32] = "                ", sz[32];
            time_t mt = archive_entry_mtime(e);
            struct tm *tm = localtime(&mt);
            if (archive_entry_mtime_is_set(e) && tm) strftime(ts, sizeof ts, "%Y-%m-%d %H:%M", tm);
            if (archive_entry_size_is_set(e)) {
                if (o->verbose) snprintf(sz, sizeof sz, "%lld", (long long)archive_entry_size(e));
                else human(archive_entry_size(e), sz, sizeof sz);
            } else snprintf(sz, sizeof sz, "-");
            int dir = archive_entry_filetype(e) == AE_IFDIR;
            printf("%12s  %s %c %s%s", dir ? "<DIR>" : sz, ts,
                   archive_entry_is_encrypted(e) ? '*' : ' ', nm,
                   (dir && nm[strlen(nm) - 1] != '/') ? "/" : "");
            if (archive_entry_symlink(e)) printf(" -> %s", archive_entry_symlink(e));
            putchar('\n');
        }
        if (archive_entry_size_is_set(e)) total += archive_entry_size(e);
        files++;
    }
    if (!first || files) {
        char h[32]; human(total, h, sizeof h);
        const char *fmt = archive_format_name(a);
        const char *flt = archive_filter_count(a) > 1 ? archive_filter_name(a, 0) : NULL;
        if (test_only)
            printf("%s: %s (%s%s%s) 항목 %ld개, %s — %s (%.2f초)\n", path, errs ? "오류" : "정상",
                   fmt ? fmt : "?", flt ? " + " : "", flt ? flt : "", files, h,
                   errs ? "손상되었거나 암호가 틀립니다" : "무결성 확인", now_sec() - t0);
        else
            printf("── %s (%s%s%s) · 항목 %ld개 · 원본 합계 %s\n", path, fmt ? fmt : "?",
                   flt ? " + " : "", flt ? flt : "", files, h);
    }
    archive_read_close(a); archive_read_free(a);
    return errs ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* 지원 형식 출력                                                      */
/* ------------------------------------------------------------------ */
static void cmd_formats(void)
{
    printf("%s\n\n", archive_version_details());
    printf("[압축 + 해제]  확장자 -> 상태\n");
    for (const fmt_map *f = FMTS; f->suffix; f++) {
        struct archive *a = archive_write_new();
        const char *st = "사용 가능";
        if (archive_write_set_format_by_name(a, f->format) != ARCHIVE_OK) st = "불가 (형식 미지원 빌드)";
        else if (f->filter) {
            int r = archive_write_add_filter_by_name(a, f->filter);
            if (r == ARCHIVE_WARN) st = "외부 프로그램 사용 (느릴 수 있음)";
            else if (r < ARCHIVE_WARN) st = "불가 (라이브러리 없음)";
        }
        printf("  %-10s %s%s\n", f->suffix, st,
               strcmp(f->format, "raw") == 0 ? "  [파일 1개]" : "");
        archive_write_free(a);
    }
    printf("\n[해제 전용]\n  .rar .rar5 .cab .lha/.lzh .rpm .deb .uu, 자기 해제(sfx) zip/rar 등\n"
           "  ※ RAR 압축은 RARLAB 독점 규격이라 어떤 오픈소스 도구도 만들 수 없습니다.\n");
}

/* ------------------------------------------------------------------ */
static void usage(void)
{
    fprintf(stderr,
"DaZipHop (dazip) %s — 가볍고 빠른 만능 압축/해제 도구\n"
"\n"
"사용법:\n"
"  dazip c [옵션] <출력.확장자> <파일/폴더...>   압축 (확장자로 형식 결정)\n"
"  dazip x [옵션] <압축파일...>                  해제 (형식 자동 판별)\n"
"  dazip l [옵션] <압축파일...>                  목록 보기\n"
"  dazip t [옵션] <압축파일...>                  무결성 검사\n"
"  dazip formats                                 지원 형식 보기\n"
"  dazip <압축파일...>                           옆에 같은 이름 폴더로 바로 해제\n"
"                                                (탐색기에서 끌어다 놓기용)\n"
"\n"
"옵션:\n"
"  -0 … -9, -l N  압축 레벨 (zstd는 1~22)        -t N  스레드 수 (xz/zstd, 기본: 전체 코어)\n"
"  -f 형식        형식 강제 (예: -f tar.zst)     -C 폴더  해제 위치\n"
"  -p 암호        zip AES-256 암호화 / 해제 암호 -k  기존 파일 덮어쓰지 않기\n"
"  -c 문자집합    옛 zip의 파일 이름 인코딩 (예: -c CP949, 한국어 Windows 탐색기 zip)\n"
"  -X             xattr/ACL/파일 플래그 보존     -v  자세히   -q  조용히\n"
"  출력이나 입력에 '-'를 쓰면 표준 입출력을 씁니다 (압축 시 -f 필요).\n"
"\n"
"예:\n"
"  dazip c backup.tar.zst ~/문서          dazip c -9 -p 1234 secret.zip a.txt b/\n"
"  dazip x movie.rar -C ~/받은파일          dazip c log.txt.xz log.txt\n"
"  dazip c -f tar.zst - 폴더 | ssh 서버 'dazip x -'   (표준 입출력 스트림)\n",
            DAZIP_VERSION);
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    /* 시스템 코드 페이지(한국어 Windows는 CP949)를 따르게 해서
       탐색기로 만든 옛 zip의 한글 이름도 바로 읽히게 합니다. */
    setlocale(LC_ALL, "");
    /* 명령줄 인자를 UTF-16으로 받아 UTF-8로 바꿈 (한글 경로 보존) */
    {
        int wc; wchar_t **wv = CommandLineToArgvW(GetCommandLineW(), &wc);
        if (wv) {
            argv = malloc(((size_t)wc + 1) * sizeof *argv);
            for (int i = 0; i < wc; i++) argv[i] = U(wv[i]);
            argv[wc] = NULL; argc = wc;
            LocalFree(wv);
        }
    }
#else
    /* 한글 등 비 ASCII 파일 이름을 zip/7z/xar의 유니코드 이름으로 바르게 변환하려면 필요 */
    const char *loc = setlocale(LC_ALL, "");
    if (!loc || (!strstr(loc, "UTF-8") && !strstr(loc, "utf8") && !strstr(loc, "UTF8")))
        if (!setlocale(LC_ALL, "C.UTF-8")) setlocale(LC_ALL, "en_US.UTF-8");
#endif
    opts o;
    memset(&o, 0, sizeof o);
    o.level = -1;
    if (argc < 2) { usage(); return 2; }

    const char *cmd = argv[1];
    int smart = 0, ai = 2;
    if (!strcmp(cmd, "c") || !strcmp(cmd, "create") || !strcmp(cmd, "a")) cmd = "c";
    else if (!strcmp(cmd, "x") || !strcmp(cmd, "extract") || !strcmp(cmd, "e")) cmd = "x";
    else if (!strcmp(cmd, "l") || !strcmp(cmd, "list")) cmd = "l";
    else if (!strcmp(cmd, "t") || !strcmp(cmd, "test")) cmd = "t";
    else if (!strcmp(cmd, "formats")) { cmd_formats(); return 0; }
    else if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help") || !strcmp(cmd, "help")) { usage(); return 0; }
    else if (!strcmp(cmd, "-V") || !strcmp(cmd, "--version")) {
        printf("dazip %s (%s)\n", DAZIP_VERSION, archive_version_string()); return 0;
    } else { cmd = "x"; smart = 1; ai = 1; }

    /* 옵션과 위치 인자 분리 (옵션은 어디에 와도 됨) */
    char **pos = calloc((size_t)argc, sizeof *pos);
    int np = 0, only_pos = 0;
    for (int i = ai; i < argc; i++) {
        char *s = argv[i];
        if (only_pos || s[0] != '-' || s[1] == 0) { pos[np++] = s; continue; }
        if (!strcmp(s, "--")) { only_pos = 1; continue; }
        if (s[1] >= '0' && s[1] <= '9' && s[2] == 0) { o.level = s[1] - '0'; continue; }
        char c = s[1];
        const char *val = NULL;
        if (strchr("ltfCpc", c)) {
            if (s[2]) val = s + 2;
            else if (i + 1 < argc) val = argv[++i];
            else { fprintf(stderr, "dazip: -%c에 값이 필요합니다.\n", c); free(pos); return 2; }
        }
        switch (c) {
        case 'l': o.level = atoi(val); break;
        case 't': o.threads = atoi(val); break;
        case 'f': o.format = val; break;
        case 'C': o.outdir = val; break;
        case 'p': o.pass = val; break;
        case 'c': o.charset = val; break;
        case 'k': o.keep = 1; break;
        case 'v': o.verbose = 1; break;
        case 'q': o.quiet = 1; break;
        case 'X': o.meta = 1; break;
        default: fprintf(stderr, "dazip: 알 수 없는 옵션 %s\n", s); free(pos); return 2;
        }
    }

    int rc = 0;
    if (cmd[0] == 'c') {
        if (np < 2) { usage(); free(pos); return 2; }
        rc = cmd_create(&o, pos[0], pos + 1, np - 1);
    } else {
        if (np < 1) { usage(); free(pos); return 2; }
        char start[4096];
        if (get_cwd(start, sizeof start) != 0) start[0] = 0;
        for (int i = 0; i < np; i++) {
            if (np > 1 && cmd[0] == 'l') printf("\n");
            o.asked = 0;
            int r = cmd[0] == 'x' ? cmd_extract(&o, pos[i], smart)
                                  : cmd_list(&o, pos[i], cmd[0] == 't');
            if (r) rc = r;
            if (start[0] && CHDIR(start) != 0) { /* 다음 파일을 위해 원위치 */ }
        }
    }
    free(pos);
#ifdef _WIN32
    /* 탐색기에서 끌어다 놓았을 때 오류 창이 바로 닫히지 않게 */
    if (smart && rc) { fputs("\n아무 키나 누르면 닫힙니다...", stderr); _getch(); }
#endif
    return rc;
}
