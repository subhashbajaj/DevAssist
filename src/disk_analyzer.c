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
#define TOP_FOLDERS            10

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

typedef struct {
    char name[NAME_MAX + 1];
    unsigned long long size;
} FolderEntry;

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

static int compare_files_desc(const void *a, const void *b)
{
    const FileEntry *x = a, *y = b;
    if (x->size < y->size) return 1;
    if (x->size > y->size) return -1;
    return 0;
}

static int compare_folders_desc(const void *a, const void *b)
{
    const FolderEntry *x = a, *y = b;
    if (x->size < y->size) return 1;
    if (x->size > y->size) return -1;
    return 0;
}

static int is_virtual_dir(const char *path)
{
    return strcmp(path, "/proc") == 0 || strcmp(path, "/sys") == 0 ||
           strcmp(path, "/dev") == 0;
}

/* Asks the user for a directory path and checks it.
 * Returns 1 if the path is a valid directory, 0 otherwise (error printed). */
static int ask_directory(char *path, size_t size)
{
    printf("Enter directory path: ");
    read_line(path, size);

    if (path[0] == '\0') {
        printf("Error: no path entered.\n");
        return 0;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        printf("Error: cannot access '%s': %s\n", path, strerror(errno));
        printf("Suggestion: check the path spelling and your permissions.\n");
        return 0;
    }
    if (!S_ISDIR(st.st_mode)) {
        printf("Error: '%s' is not a directory.\n", path);
        return 0;
    }

    /* strip a trailing '/' (except for root) to keep paths tidy */
    size_t plen = strlen(path);
    if (plen > 1 && path[plen - 1] == '/')
        path[plen - 1] = '\0';

    return 1;
}

/* ---------- large file scan ---------- */

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
            if (is_virtual_dir(full))
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

/* ---------- folder size calculation ---------- */

/* Adds up the size of every regular file inside 'dir' (recursively). */
static unsigned long long dir_size(const char *dir, int depth,
                                   unsigned long *errors)
{
    DIR *d = opendir(dir);
    if (d == NULL) {
        (*errors)++;
        return 0;
    }

    unsigned long long total = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;

        char full[PATH_MAX];
        int len = snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
        if (len < 0 || (size_t)len >= sizeof(full)) {
            (*errors)++;
            continue;
        }

        struct stat st;
        if (lstat(full, &st) != 0) {
            (*errors)++;
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (!is_virtual_dir(full) && depth < MAX_DEPTH)
                total += dir_size(full, depth + 1, errors);
        } else if (S_ISREG(st.st_mode)) {
            total += (unsigned long long)st.st_size;
        }
    }
    closedir(d);
    return total;
}

/* ---------- public functions ---------- */

void find_large_files(void)
{
    char path[PATH_MAX];
    char input[64];

    printf("\n===== Large File Finder =====\n\n");
    if (!ask_directory(path, sizeof(path)))
        return;

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

    printf("\nScanning '%s' for files >= %llu MB ...\n\n", path, min_mb);
    scan_directory(path, 0, &ctx);

    qsort(ctx.entries, ctx.count, sizeof(FileEntry), compare_files_desc);

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

void find_large_directories(void)
{
    char path[PATH_MAX];

    printf("\n===== Large Folder Finder =====\n\n");
    if (!ask_directory(path, sizeof(path)))
        return;

    DIR *d = opendir(path);
    if (d == NULL) {
        printf("Error: cannot open '%s': %s\n", path, strerror(errno));
        return;
    }

    FolderEntry *folders = malloc(sizeof(FolderEntry) * MAX_RESULTS);
    if (folders == NULL) {
        printf("Error: out of memory.\n");
        closedir(d);
        return;
    }

    int count = 0;
    int truncated = 0;
    unsigned long errors = 0;
    unsigned long long loose_files = 0;   /* files directly inside 'path' */
    unsigned long long grand_total = 0;

    printf("Calculating folder sizes in '%s' ...\n\n", path);

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;

        char full[PATH_MAX];
        int len = snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
        if (len < 0 || (size_t)len >= sizeof(full)) {
            errors++;
            continue;
        }

        struct stat st;
        if (lstat(full, &st) != 0) {
            errors++;
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (is_virtual_dir(full))
                continue;
            unsigned long long sz = dir_size(full, 1, &errors);
            grand_total += sz;
            if (count < MAX_RESULTS) {
                strncpy(folders[count].name, e->d_name, NAME_MAX);
                folders[count].name[NAME_MAX] = '\0';
                folders[count].size = sz;
                count++;
            } else {
                truncated = 1;
            }
        } else if (S_ISREG(st.st_mode)) {
            loose_files += (unsigned long long)st.st_size;
            grand_total += (unsigned long long)st.st_size;
        }
    }
    closedir(d);

    qsort(folders, count, sizeof(FolderEntry), compare_folders_desc);

    if (count == 0) {
        printf("No sub-folders found in '%s'.\n", path);
    } else {
        int shown = count < TOP_FOLDERS ? count : TOP_FOLDERS;
        printf("%-12s %-8s %s\n", "SIZE", "SHARE", "FOLDER");
        printf("------------------------------------------------------\n");
        for (int i = 0; i < shown; i++) {
            char sz[32];
            format_size(folders[i].size, sz, sizeof(sz));
            double share = grand_total > 0
                         ? (double)folders[i].size * 100.0 / (double)grand_total
                         : 0.0;
            printf("%-12s %5.1f%%   %s/\n", sz, share, folders[i].name);
        }
        if (count > shown)
            printf("... and %d more folder(s) not shown.\n", count - shown);
        if (truncated)
            printf("Note: more than %d folders; only the first %d were measured.\n",
                   MAX_RESULTS, MAX_RESULTS);
    }

    char lf[32], gt[32];
    format_size(loose_files, lf, sizeof(lf));
    format_size(grand_total, gt, sizeof(gt));
    printf("\nFiles directly in this folder: %s\n", lf);
    printf("Total size of '%s': %s\n", path, gt);

    if (count > 0 && grand_total > 0) {
        printf("\nSuggestion: start by inspecting the biggest folder above "
               "('%s').\n"
               "            DevAssist never deletes anything automatically.\n",
               folders[0].name);
    }
    if (errors > 0)
        printf("Note: %lu item(s) could not be read (permission denied or "
               "similar) and were skipped.\n", errors);

    free(folders);
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

    /* inode check: a disk can be "full" because it ran out of inodes
     * (too many small files) even when free space remains */
    if (vfs.f_files > 0) {
        unsigned long long inodes_used =
            (unsigned long long)vfs.f_files - (unsigned long long)vfs.f_ffree;
        double inode_percent = (double)inodes_used * 100.0 / (double)vfs.f_files;

        printf("\nInode usage: %.0f%% (%llu of %llu used)\n",
               inode_percent, inodes_used, (unsigned long long)vfs.f_files);
        if (inode_percent >= DISK_WARNING_THRESHOLD) {
            printf("Problem: Inode usage is high.\n");
            printf("Suggestion: Look for folders with a very large number of "
                   "small files (cache, logs, temp files).\n");
        } else {
            printf("Status: Inode usage is within the normal range.\n");
        }
    }

    /* sub-menu */
    char choice[16];
    for (;;) {
        printf("\n1. Find large files\n"
               "2. Find large folders\n"
               "0. Back to main menu\n\nEnter choice: ");
        read_line(choice, sizeof(choice));
        if (strcmp(choice, "1") == 0)
            find_large_files();
        else if (strcmp(choice, "2") == 0)
            find_large_directories();
        else if (strcmp(choice, "0") == 0)
            return;
        else
            printf("Invalid choice. Please enter 0, 1 or 2.\n");
    }
}
