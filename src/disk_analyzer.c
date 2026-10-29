/* disk_analyzer.c - Disk & File Analyzer module (DevAssist)
 * Author: Krishna
 * Read-only: this module never deletes or modifies any file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include "disk_analyzer.h"

#define DISK_WARNING_THRESHOLD 80   /* percent */
#define DEFAULT_MIN_MB         50   /* default "large file" size */
#define MAX_RESULTS            1000
#define MAX_DEPTH              20

typedef struct {
    char path[PATH_MAX];
    unsigned long long size;
} FileEntry;

typedef struct {
    FileEntry *entries;
    int count;
    int truncated;                 /* 1 if more than MAX_RESULTS were found */
    unsigned long long min_bytes;
    unsigned long files_scanned;
    unsigned long errors;          /* directories/files we could not read */
} ScanCtx;

/* ---------- helpers ---------- */

static void read_line(char *buf, size_t size)
{
    if (fgets(buf, size, stdin) == NULL) {
        buf[0] = '\0';
        return;
    }
    buf[strcspn(buf, "\n")] = '\0';
}

static void format_size(unsigned long long bytes, char *out, size_t n)
{
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        u++;
    }
    if (u == 0)
        snprintf(out, n, "%llu B", bytes);
    else
        snprintf(out, n, "%.2f %s", v, units[u]);
}

static int compare_desc(const void *a, const void *b)
{
    const FileEntry *x = a, *y = b;
    if (x->size < y->size) return 1;
    if (x->size > y->size) return -1;
    return 0;
}

/* ---------- directory scan ---------- */

static void scan_directory(const char *dir, int depth, ScanCtx *ctx)
{
    DIR *d = opendir(dir);
    if (d == NULL) {
        ctx->errors++;
        return;
    }

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;

        char full[PATH_MAX];
        int len = snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
        if (len < 0 || (size_t)len >= sizeof(full)) {
            ctx->errors++;
            continue;
        }

        struct stat st;
        if (lstat(full, &st) != 0) {       /* lstat: do not follow symlinks */
            ctx->errors++;
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            /* skip virtual filesystems: they are not real disk usage */
            if (strcmp(full, "/proc") == 0 || strcmp(full, "/sys") == 0 ||
                strcmp(full, "/dev") == 0)
                continue;
            if (depth < MAX_DEPTH)
                scan_directory(full, depth + 1, ctx);
        } else if (S_ISREG(st.st_mode)) {
            ctx->files_scanned++;
            if ((unsigned long long)st.st_size >= ctx->min_bytes) {
                if (ctx->count < MAX_RESULTS) {
                    strncpy(ctx->entries[ctx->count].path, full, PATH_MAX - 1);
                    ctx->entries[ctx->count].path[PATH_MAX - 1] = '\0';
                    ctx->entries[ctx->count].size = (unsigned long long)st.st_size;
                    ctx->count++;
                } else {
                    ctx->truncated = 1;
                }
            }
        }
    }
    closedir(d);
}

/* ---------- public functions ---------- */

void find_large_files(void)
{
    char path[PATH_MAX];
    char input[64];

    printf("\n===== Large File Finder =====\n\n");
    printf("Enter directory path: ");
    read_line(path, sizeof(path));

    if (path[0] == '\0') {
        printf("Error: no path entered.\n");
        return;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        printf("Error: cannot access '%s': %s\n", path, strerror(errno));
        printf("Suggestion: check the path spelling and your permissions.\n");
        return;
    }
    if (!S_ISDIR(st.st_mode)) {
        printf("Error: '%s' is not a directory.\n", path);
        return;
    }

    printf("Minimum file size in MB (press Enter for %d): ", DEFAULT_MIN_MB);
    read_line(input, sizeof(input));
    unsigned long long min_mb = DEFAULT_MIN_MB;
    if (input[0] != '\0') {
        char *end;
        long v = strtol(input, &end, 10);
        if (*end != '\0' || v <= 0) {
            printf("Invalid number, using default of %d MB.\n", DEFAULT_MIN_MB);
        } else {
            min_mb = (unsigned long long)v;
        }
    }

    ScanCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.min_bytes = min_mb * 1024ULL * 1024ULL;
    ctx.entries = malloc(sizeof(FileEntry) * MAX_RESULTS);
    if (ctx.entries == NULL) {
        printf("Error: out of memory.\n");
        return;
    }

    /* strip a trailing '/' (except for root) to keep paths tidy */
    size_t plen = strlen(path);
    if (plen > 1 && path[plen - 1] == '/')
        path[plen - 1] = '\0';

    printf("\nScanning '%s' for files >= %llu MB ...\n\n", path, min_mb);
    scan_directory(path, 0, &ctx);

    qsort(ctx.entries, ctx.count, sizeof(FileEntry), compare_desc);

    if (ctx.count == 0) {
        printf("No files of %llu MB or larger were found.\n", min_mb);
    } else {
        unsigned long long total = 0;
        printf("%-12s %s\n", "SIZE", "PATH");
        printf("------------------------------------------------------\n");
        for (int i = 0; i < ctx.count; i++) {
            char sz[32];
            format_size(ctx.entries[i].size, sz, sizeof(sz));
            printf("%-12s %s\n", sz, ctx.entries[i].path);
            total += ctx.entries[i].size;
        }
        char tsz[32];
        format_size(total, tsz, sizeof(tsz));
        printf("\nFound %d large file(s), total %s.\n", ctx.count, tsz);
        if (ctx.truncated)
            printf("Note: more than %d matches; only the first %d are shown.\n",
                   MAX_RESULTS, MAX_RESULTS);
        printf("Suggestion: inspect these files and decide yourself whether "
               "they are still needed.\n"
               "            DevAssist never deletes files automatically.\n");
    }

    printf("\nFiles scanned: %lu\n", ctx.files_scanned);
    if (ctx.errors > 0)
        printf("Note: %lu item(s) could not be read (permission denied or "
               "similar) and were skipped.\n", ctx.errors);

    free(ctx.entries);
}

void show_disk_usage(void)
{
    const char *fs_path = "/";
    struct statvfs vfs;

    printf("\n===== Disk & File Analyzer =====\n\n");

    if (statvfs(fs_path, &vfs) != 0) {
        printf("Error: could not read filesystem info for '%s': %s\n",
               fs_path, strerror(errno));
        return;
    }

    unsigned long long total = (unsigned long long)vfs.f_blocks * vfs.f_frsize;
    unsigned long long freeb = (unsigned long long)vfs.f_bfree  * vfs.f_frsize;
    unsigned long long avail = (unsigned long long)vfs.f_bavail * vfs.f_frsize;
    unsigned long long used  = total - freeb;

    /* same formula as 'df': used / (used + available) */
    double percent = 0.0;
    if (used + avail > 0)
        percent = (double)used * 100.0 / (double)(used + avail);

    char t[32], u[32], a[32];
    format_size(total, t, sizeof(t));
    format_size(used,  u, sizeof(u));
    format_size(avail, a, sizeof(a));

    printf("Filesystem: %s\n", fs_path);
    printf("Total     : %s\n", t);
    printf("Used      : %s\n", u);
    printf("Available : %s\n\n", a);
    printf("Filesystem usage: %.0f%%\n", percent);
    printf("Warning threshold: %d%%\n\n", DISK_WARNING_THRESHOLD);

    if (percent >= DISK_WARNING_THRESHOLD) {
        printf("Problem: Disk usage is high.\n");
        printf("Suggestion: Inspect large files and unnecessary data.\n");
    } else {
        printf("Status: Disk usage is within the normal range.\n");
    }

    /* sub-menu */
    char choice[16];
    for (;;) {
        printf("\n1. Find large files\n0. Back to main menu\n\nEnter choice: ");
        read_line(choice, sizeof(choice));
        if (strcmp(choice, "1") == 0)
            find_large_files();
        else if (strcmp(choice, "0") == 0)
            return;
        else
            printf("Invalid choice. Please enter 1 or 0.\n");
    }
}
