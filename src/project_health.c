/* project_health.c - Project Health Checker module (DevAssist)
 * Author: Krishna
 * Read-only: this module never deletes or modifies any file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include "project_health.h"

#define MAX_SCAN_DEPTH 6
#define MAX_LISTED     5

typedef struct {
    /* found in the top level of the project */
    int has_git;
    int has_readme;
    int has_build;
    int has_gitignore;
    int has_license;
    int has_src_dir;
    /* found anywhere in the project */
    int has_tests;
    unsigned long total_files;
    unsigned long source_files;
    unsigned long binary_count;
    unsigned long errors;
    char binaries[MAX_LISTED][PATH_MAX];
} Project;

typedef struct {
    const char *name;
    int passed;
    int points;
    const char *problem;
    const char *suggestion;
} Check;

/* ---------- helpers ---------- */

static void read_line(char *buf, size_t size)
{
    if (fgets(buf, size, stdin) == NULL) {
        buf[0] = '\0';
        return;
    }
    buf[strcspn(buf, "\n")] = '\0';
}

static int ends_with(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lx = strlen(suffix);
    return ls >= lx && strcmp(s + ls - lx, suffix) == 0;
}

static int starts_with_nocase(const char *s, const char *prefix)
{
    return strncasecmp(s, prefix, strlen(prefix)) == 0;
}

static int is_source_file(const char *name)
{
    const char *ext[] = {".c", ".h", ".cpp", ".hpp", ".cc", ".py",
                         ".js", ".java", ".go", ".rs"};
    for (size_t i = 0; i < sizeof(ext) / sizeof(ext[0]); i++)
        if (ends_with(name, ext[i]))
            return 1;
    return 0;
}

/* A compiled Linux program or object file starts with the bytes 7F 'E' 'L' 'F'. */
static int is_elf_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return 0;
    unsigned char magic[4];
    size_t n = fread(magic, 1, 4, f);
    fclose(f);
    return n == 4 && magic[0] == 0x7f && magic[1] == 'E' &&
           magic[2] == 'L' && magic[3] == 'F';
}

static int is_skipped_dir(const char *name)
{
    return strcmp(name, ".git") == 0 || strcmp(name, "node_modules") == 0 ||
           strcmp(name, ".cache") == 0;
}

/* ---------- project scan ---------- */

static void scan_project(const char *root, const char *dir, int depth,
                         Project *p)
{
    DIR *d = opendir(dir);
    if (d == NULL) {
        p->errors++;
        return;
    }

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *name = e->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        char full[PATH_MAX];
        int len = snprintf(full, sizeof(full), "%s/%s", dir, name);
        if (len < 0 || (size_t)len >= sizeof(full)) {
            p->errors++;
            continue;
        }

        struct stat st;
        if (lstat(full, &st) != 0) {       /* lstat: never follow symlinks */
            p->errors++;
            continue;
        }

        if (depth == 0 && strcmp(name, ".git") == 0)
            p->has_git = 1;

        if (S_ISDIR(st.st_mode)) {
            if (depth == 0 && strcmp(name, "src") == 0)
                p->has_src_dir = 1;
            if (strcasecmp(name, "test") == 0 || strcasecmp(name, "tests") == 0)
                p->has_tests = 1;
            if (!is_skipped_dir(name) && depth < MAX_SCAN_DEPTH)
                scan_project(root, full, depth + 1, p);
        } else if (S_ISREG(st.st_mode)) {
            p->total_files++;

            if (depth == 0) {
                if (starts_with_nocase(name, "readme"))
                    p->has_readme = 1;
                if (strcmp(name, "Makefile") == 0 ||
                    strcmp(name, "makefile") == 0 ||
                    strcmp(name, "GNUmakefile") == 0 ||
                    strcmp(name, "CMakeLists.txt") == 0)
                    p->has_build = 1;
                if (strcmp(name, ".gitignore") == 0)
                    p->has_gitignore = 1;
                if (starts_with_nocase(name, "license") ||
                    starts_with_nocase(name, "copying"))
                    p->has_license = 1;
            }

            if (starts_with_nocase(name, "test"))
                p->has_tests = 1;

            if (is_source_file(name))
                p->source_files++;

            /* compiled files: executable bit or object-file style names,
             * confirmed by the ELF signature */
            if ((st.st_mode & S_IXUSR) || ends_with(name, ".o") ||
                ends_with(name, ".out") || ends_with(name, ".so")) {
                if (is_elf_file(full)) {
                    if (p->binary_count < MAX_LISTED) {
                        const char *rel = full + strlen(root);
                        if (*rel == '/')
                            rel++;
                        strncpy(p->binaries[p->binary_count], rel, PATH_MAX - 1);
                        p->binaries[p->binary_count][PATH_MAX - 1] = '\0';
                    }
                    p->binary_count++;
                }
            }
        }
    }
    closedir(d);
}

/* ---------- public function ---------- */

void check_project_health(void)
{
    char input[PATH_MAX];
    char root[PATH_MAX];

    printf("\n===== Project Health Checker =====\n\n");
    printf("Enter project folder path (press Enter for current folder): ");
    read_line(input, sizeof(input));
    if (input[0] == '\0')
        strcpy(input, ".");

    struct stat st;
    if (stat(input, &st) != 0) {
        printf("Error: cannot access '%s': %s\n", input, strerror(errno));
        printf("Suggestion: check the path spelling and your permissions.\n");
        return;
    }
    if (!S_ISDIR(st.st_mode)) {
        printf("Error: '%s' is not a directory.\n", input);
        return;
    }
    if (realpath(input, root) == NULL) {
        printf("Error: cannot resolve '%s': %s\n", input, strerror(errno));
        return;
    }

    Project p;
    memset(&p, 0, sizeof(p));

    printf("\nChecking '%s' ...\n\n", root);
    scan_project(root, root, 0, &p);

    if (p.total_files == 0 && !p.has_git) {
        if (p.errors > 0)
            printf("Error: this folder could not be read (permission denied "
                   "or similar).\n");
        else
            printf("This folder is empty (no files found), so there is "
                   "nothing to check.\n");
        return;
    }

    Check checks[] = {
        {"Git repository (.git)", p.has_git, 20,
         "This folder is not under version control.",
         "Run 'git init' (or clone the repository) so changes can be tracked."},
        {"README file", p.has_readme, 15,
         "No README was found.",
         "Add a README that explains what the project does and how to build it."},
        {"Build file (Makefile / CMakeLists.txt)", p.has_build, 15,
         "No build file was found.",
         "Add a Makefile so anyone can compile the project with one command."},
        {".gitignore file", p.has_gitignore, 10,
         "No .gitignore was found.",
         "Add a .gitignore so compiled files and temporary files are not committed."},
        {"Source files", p.source_files > 0, 15,
         "No source code files were found.",
         "Check that you selected the correct project folder."},
        {"Tests (test/tests folder or test files)", p.has_tests, 10,
         "No tests were found.",
         "Add a tests folder with at least a few simple test cases."},
        {"Organized source folder (src/)", p.has_src_dir, 5,
         "There is no src/ folder.",
         "Keep source files in src/ (and headers in include/) to stay organized."},
        {"LICENSE file", p.has_license, 5,
         "No LICENSE file was found.",
         "Add a LICENSE file if the project will be shared with others."},
        {"No compiled files in the project", p.binary_count == 0, 5,
         "Compiled program/object files were found inside the project.",
         "Compiled files usually should not be committed; add them to .gitignore."},
    };
    int n = (int)(sizeof(checks) / sizeof(checks[0]));

    int score = 0, problems = 0;
    for (int i = 0; i < n; i++) {
        printf(" [%s] %s\n", checks[i].passed ? " OK    " : "MISSING",
               checks[i].name);
        if (checks[i].passed)
            score += checks[i].points;
        else
            problems++;
    }

    printf("\nFiles scanned: %lu (source files: %lu)\n",
           p.total_files, p.source_files);

    if (p.binary_count > 0) {
        printf("\nCompiled files found (%lu):\n", p.binary_count);
        unsigned long shown = p.binary_count < MAX_LISTED ? p.binary_count
                                                          : MAX_LISTED;
        for (unsigned long i = 0; i < shown; i++)
            printf("   - %s\n", p.binaries[i]);
        if (p.binary_count > shown)
            printf("   ... and %lu more\n", p.binary_count - shown);
    }

    if (problems > 0) {
        printf("\nProblems and suggestions:\n");
        for (int i = 0; i < n; i++) {
            if (checks[i].passed)
                continue;
            printf(" * Problem: %s\n", checks[i].problem);
            printf("   Suggestion: %s\n", checks[i].suggestion);
        }
    } else {
        printf("\nNo problems found. This project is well organized.\n");
    }

    const char *rating = score >= 80 ? "Good"
                       : score >= 50 ? "Fair" : "Needs attention";
    printf("\nHealth score: %d/100 (%s)\n", score, rating);
    printf("DevAssist only reports; it never changes your project files.\n");

    if (p.errors > 0)
        printf("Note: %lu item(s) could not be read (permission denied or "
               "similar) and were skipped.\n", p.errors);
}
