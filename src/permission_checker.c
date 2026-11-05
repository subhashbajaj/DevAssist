/*
 * permission_checker.c - Permission Detector module for DevAssist
 * Author: Sneha Kanjwani
 * Read-only: this module never changes any file or permission.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "permission_checker.h"

typedef struct {
    const char *path;
    int quiet;
    int header;
    int issues;
} Ctx;

static void print_header(Ctx *c)
{
    if (!c->header) {
        printf("\n--- %s ---\n", c->path);
        c->header = 1;
    }
}

static void add_issue(Ctx *c, const char *level, const char *problem, const char *fix)
{
    print_header(c);
    printf("  [%s] %s\n", level, problem);
    if (fix)
        printf("         Suggested fix: %s\n", fix);
    c->issues++;
}

static void mode_to_string(mode_t m, char *s)
{
    s[0] = S_ISDIR(m) ? 'd' : S_ISLNK(m) ? 'l' : S_ISREG(m) ? '-' :
           S_ISCHR(m) ? 'c' : S_ISBLK(m) ? 'b' : S_ISFIFO(m) ? 'p' :
           S_ISSOCK(m) ? 's' : '?';
    s[1] = (m & S_IRUSR) ? 'r' : '-';
    s[2] = (m & S_IWUSR) ? 'w' : '-';
    s[3] = (m & S_ISUID) ? ((m & S_IXUSR) ? 's' : 'S') : ((m & S_IXUSR) ? 'x' : '-');
    s[4] = (m & S_IRGRP) ? 'r' : '-';
    s[5] = (m & S_IWGRP) ? 'w' : '-';
    s[6] = (m & S_ISGID) ? ((m & S_IXGRP) ? 's' : 'S') : ((m & S_IXGRP) ? 'x' : '-');
    s[7] = (m & S_IROTH) ? 'r' : '-';
    s[8] = (m & S_IWOTH) ? 'w' : '-';
    s[9] = (m & S_ISVTX) ? ((m & S_IXOTH) ? 't' : 'T') : ((m & S_IXOTH) ? 'x' : '-');
    s[10] = '\0';
}

static int is_sensitive_name(const char *name)
{
    static const char *patterns[] = {
        "id_rsa", "id_ed25519", ".pem", ".key", ".env", "secret", "credentials", NULL
    };
    for (int i = 0; patterns[i]; i++)
        if (strcasestr(name, patterns[i]))
            return 1;
    return 0;
}

static int has_suffix(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

static int analyze(const char *path, int quiet)
{
    struct stat st, lst;
    Ctx c = { path, quiet, 0, 0 };
    char fix[PATH_MAX + 128];
    char perm[16];

    if (lstat(path, &lst) != 0) {
        printf("\nCannot access '%s': %s\n", path, strerror(errno));
        return -1;
    }
    int is_link = S_ISLNK(lst.st_mode);
    if (stat(path, &st) != 0) {
        add_issue(&c, "WARNING", "Broken symbolic link or inaccessible target.",
                  "Check where the link points with: ls -l <path>");
        return c.issues;
    }

    mode_t m = st.st_mode;
    mode_to_string(m, perm);
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;

    struct passwd *pw = getpwuid(st.st_uid);
    struct group  *gr = getgrgid(st.st_gid);
    struct passwd *me = getpwuid(getuid());

    if (!quiet) {
        char timebuf[32];
        strftime(timebuf, sizeof timebuf, "%Y-%m-%d %H:%M", localtime(&st.st_mtime));
        print_header(&c);
        printf("  Type        : %s%s\n", S_ISDIR(m) ? "Directory" : S_ISREG(m) ? "Regular file" : "Special file",
               is_link ? " (via symbolic link)" : "");
        printf("  Permissions : %s (%04o)\n", perm, (unsigned)(m & 07777));
        if (pw) printf("  Owner       : %s (uid %u)\n", pw->pw_name, (unsigned)st.st_uid);
        else    printf("  Owner       : uid %u\n", (unsigned)st.st_uid);
        if (gr) printf("  Group       : %s (gid %u)\n", gr->gr_name, (unsigned)st.st_gid);
        else    printf("  Group       : gid %u\n", (unsigned)st.st_gid);
        printf("  Size        : %lld bytes\n", (long long)st.st_size);
        printf("  Modified    : %s\n", timebuf);
        printf("  You (%s) can: read=%s write=%s execute=%s\n",
               me ? me->pw_name : "?",
               access(path, R_OK) == 0 ? "yes" : "no",
               access(path, W_OK) == 0 ? "yes" : "no",
               access(path, X_OK) == 0 ? "yes" : "no");
        printf("  Analysis:\n");
    }

    if (m & S_IWOTH) {
        if (S_ISDIR(m) && (m & S_ISVTX)) {
            if (!quiet)
                printf("  [INFO] World-writable directory with sticky bit (like /tmp). This is normal.\n");
        } else {
            snprintf(fix, sizeof fix, "chmod o-w \"%s\"   (or chmod %s \"%s\")",
                     path, S_ISDIR(m) ? "755" : "644", path);
            add_issue(&c, "CRITICAL",
                      "Anyone on the system can modify this. World-writable files are a security risk.", fix);
        }
    }

    if (S_ISREG(m) && (m & (S_ISUID | S_ISGID))) {
        snprintf(fix, sizeof fix, "If not intentional: chmod u-s,g-s \"%s\"", path);
        add_issue(&c, "WARNING",
                  "setuid/setgid bit is set: this program runs with the owner's/group's privileges.", fix);
    }

    if (S_ISREG(m) && is_sensitive_name(name) && (m & (S_IRWXG | S_IRWXO))) {
        snprintf(fix, sizeof fix, "chmod 600 \"%s\"", path);
        add_issue(&c, "CRITICAL",
                  "This looks like a key/secret file but group/others have access to it. SSH and other tools may refuse it.", fix);
    }

    if (S_ISREG(m) && (has_suffix(name, ".sh") || has_suffix(name, ".py")) && !(m & S_IXUSR)) {
        snprintf(fix, sizeof fix, "chmod +x \"%s\"   (then run it with ./%s)", path, name);
        add_issue(&c, "INFO",
                  "Script is not executable, so running it directly gives 'Permission denied'.", fix);
    }

    if (S_ISDIR(m) && !(m & S_IXUSR)) {
        snprintf(fix, sizeof fix, "chmod u+x \"%s\"", path);
        add_issue(&c, "WARNING",
                  "Directory has no execute (x) bit for the owner, so it cannot be entered (cd fails).", fix);
    }

    if (access(path, R_OK) != 0) {
        snprintf(fix, sizeof fix, "Check access: ls -ld \"%s\" ; ask the owner, or use sudo if you are allowed", path);
        add_issue(&c, "WARNING", "You do not have read permission on this path.", fix);
    }

    if (st.st_uid == 0 && getuid() != 0 && access(path, W_OK) != 0) {
        snprintf(fix, sizeof fix, "If this is your project file: sudo chown %s:%s \"%s\"",
                 me ? me->pw_name : "$USER", me ? me->pw_name : "$USER", path);
        add_issue(&c, "INFO",
                  "Owned by root and you cannot modify it. This often happens after running commands with sudo.", fix);
    }

    if (!quiet && (m & S_IWGRP) && !(m & S_IWOTH))
        printf("  [INFO] Group-writable: members of group '%s' can modify this.\n", gr ? gr->gr_name : "?");

    if (!quiet && c.issues == 0)
        printf("  [OK] No permission problems found.\n");

    return c.issues;
}

int check_path_permissions(const char *path)
{
    return analyze(path, 0);
}

int check_directory_permissions(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) {
        printf("Cannot open directory '%s': %s\n", dir, strerror(errno));
        return -1;
    }

    struct dirent *e;
    char full[PATH_MAX];
    int scanned = 0, total_issues = 0, bad_entries = 0;

    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
        int n = analyze(full, 1);
        scanned++;
        if (n > 0) { total_issues += n; bad_entries++; }
    }
    closedir(d);

    printf("\n=== Scan summary for %s ===\n", dir);
    printf("Entries scanned     : %d\n", scanned);
    printf("Entries with issues : %d\n", bad_entries);
    printf("Total issues        : %d\n", total_issues);
    if (total_issues == 0)
        printf("Result: everything looks fine.\n");
    return total_issues;
}

static int read_line(const char *prompt, char *buf, size_t n)
{
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, (int)n, stdin))
        return 0;
    buf[strcspn(buf, "\n")] = '\0';
    return 1;
}

void check_permissions(void)
{
    char choice[16], path[PATH_MAX];

    for (;;) {
        printf("\n===== Permission Detector =====\n");
        printf("1. Check a single file or directory\n");
        printf("2. Scan all entries in a directory\n");
        printf("0. Back\n");
        if (!read_line("Choose: ", choice, sizeof choice))
            return;

        if (choice[0] == '1') {
            if (!read_line("Enter path: ", path, sizeof path) || path[0] == '\0')
                continue;
            check_path_permissions(path);
        } else if (choice[0] == '2') {
            if (!read_line("Enter directory (Enter = current): ", path, sizeof path))
                return;
            if (path[0] == '\0')
                strcpy(path, ".");
            check_directory_permissions(path);
        } else if (choice[0] == '0') {
            return;
        } else {
            printf("Invalid choice.\n");
        }
    }
}
