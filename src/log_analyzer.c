/*
 * log_analyzer.c - Log Analyzer module for DevAssist
 * Author: Sneha Kanjwani
 * Read-only: the log file is never modified.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include "log_analyzer.h"

#define LOG_LINE_LEN      1024
#define MAX_REPEATS       512
#define RECENT            5
#define REPEAT_THRESHOLD  3

enum { SEV_NONE, SEV_WARNING, SEV_ERROR, SEV_CRITICAL };

typedef struct {
    const char *needle;
    const char *title;
    const char *cause;
    const char *fix;
    int count;
} Known;

static Known known[] = {
    { "permission denied", "Permission denied",
      "A program tried to use a file or resource it has no rights to.",
      "Check with ls -l, then fix with chmod/chown (see the Permission Detector).", 0 },
    { "no space left", "Disk full",
      "The disk or partition has run out of free space.",
      "Run df -h and du -sh * to find what is large; delete old logs/build files.", 0 },
    { "out of memory", "Out of memory",
      "The system or program ran out of RAM.",
      "Check free -h and top; close heavy programs, fix leaks, or add swap.", 0 },
    { "segmentation fault", "Segmentation fault",
      "A C program accessed memory it should not (bad pointer, overflow).",
      "Compile with gcc -g and debug with gdb or valgrind.", 0 },
    { "segfault", "Segmentation fault",
      "A C program accessed memory it should not (bad pointer, overflow).",
      "Compile with gcc -g and debug with gdb or valgrind.", 0 },
    { "connection refused", "Connection refused",
      "Nothing is listening on the target port, or a firewall blocked it.",
      "Make sure the service is running; check the port with: ss -tulnp | grep <port>", 0 },
    { "address already in use", "Port conflict",
      "Another process is already using the port your program wants.",
      "Find it with: lsof -i :<port> ; then stop it or change your port.", 0 },
    { "timed out", "Timeout",
      "An operation took too long (slow network, overloaded or unreachable server).",
      "Check network/server status and increase the timeout if appropriate.", 0 },
    { "failed password", "Failed login attempts",
      "Wrong passwords were entered; many repeats may mean a brute-force attempt.",
      "Review with: grep 'Failed password' /var/log/auth.log ; consider key-only SSH login.", 0 },
    { "no such file", "Missing file or directory",
      "A program referenced a path that does not exist.",
      "Verify the path with ls, check for typos and the working directory.", 0 },
};
#define NKNOWN ((int)(sizeof known / sizeof known[0]))

typedef struct {
    char key[256];
    char sample[LOG_LINE_LEN];
    int  count;
} RepeatEntry;

static RepeatEntry repeats[MAX_REPEATS];
static int nrepeats;

static int has(const char *line, const char *word)
{
    return strcasestr(line, word) != NULL;
}

static int classify(const char *l)
{
    if (has(l, "critical") || has(l, "fatal") || has(l, "panic") ||
        has(l, "segfault") || has(l, "segmentation fault") || has(l, "out of memory"))
        return SEV_CRITICAL;
    if (has(l, "error") || has(l, "fail") || has(l, "denied") ||
        has(l, "refused") || has(l, "exception") || has(l, "timed out"))
        return SEV_ERROR;
    if (has(l, "warn"))
        return SEV_WARNING;
    return SEV_NONE;
}

/* Replace every run of digits with '#', so lines differing only in
   timestamps/PIDs count as the same message. */
static void normalize(const char *in, char *out, size_t n)
{
    size_t j = 0;
    int prev_digit = 0;
    for (size_t i = 0; in[i] && j + 1 < n; i++) {
        if (isdigit((unsigned char)in[i])) {
            if (!prev_digit) out[j++] = '#';
            prev_digit = 1;
        } else {
            out[j++] = in[i];
            prev_digit = 0;
        }
    }
    out[j] = '\0';
}

static void track_repeat(const char *line)
{
    char key[256];
    normalize(line, key, sizeof key);

    for (int i = 0; i < nrepeats; i++) {
        if (strcmp(repeats[i].key, key) == 0) {
            repeats[i].count++;
            return;
        }
    }
    if (nrepeats < MAX_REPEATS) {
        strcpy(repeats[nrepeats].key, key);
        strncpy(repeats[nrepeats].sample, line, LOG_LINE_LEN - 1);
        repeats[nrepeats].sample[LOG_LINE_LEN - 1] = '\0';
        repeats[nrepeats].count = 1;
        nrepeats++;
    }
}

static int cmp_repeat_desc(const void *a, const void *b)
{
    return ((const RepeatEntry *)b)->count - ((const RepeatEntry *)a)->count;
}

int analyze_log_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        printf("\nCannot open '%s': %s\n", path, strerror(errno));
        if (errno == EACCES)
            printf("Tip: system logs are often readable only by root/adm. Either run with sudo,\n"
                   "     or copy it: sudo cp %s ~/copy.log && sudo chown $USER ~/copy.log\n", path);
        return -1;
    }

    char line[LOG_LINE_LEN];
    char recent[RECENT][LOG_LINE_LEN];
    long total = 0, nwarn = 0, nerr = 0, ncrit = 0, nproblem = 0;

    nrepeats = 0;
    for (int i = 0; i < NKNOWN; i++) known[i].count = 0;

    while (fgets(line, sizeof line, fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        total++;

        int sev = classify(line);
        if (sev == SEV_NONE)
            continue;

        nproblem++;
        if (sev == SEV_WARNING)  nwarn++;
        if (sev == SEV_ERROR)    nerr++;
        if (sev == SEV_CRITICAL) ncrit++;

        if (sev >= SEV_ERROR)
            strcpy(recent[(nerr + ncrit - 1) % RECENT], line);
        track_repeat(line);

        for (int i = 0; i < NKNOWN; i++)
            if (has(line, known[i].needle))
                known[i].count++;
    }
    fclose(fp);

    printf("\n===== Log Analysis: %s =====\n", path);
    printf("Lines read : %ld\n", total);
    printf("Critical   : %ld\n", ncrit);
    printf("Errors     : %ld\n", nerr);
    printf("Warnings   : %ld\n", nwarn);

    if (nproblem == 0) {
        printf("\nResult: no errors or warnings found. The log looks healthy.\n");
        return 0;
    }

    int printed_header = 0;
    for (int i = 0; i < NKNOWN; i++) {
        if (known[i].count == 0) continue;
        if (!printed_header) {
            printf("\n--- Recognised problems ---\n");
            printed_header = 1;
        }
        printf("* %s (%d time%s)\n", known[i].title, known[i].count, known[i].count == 1 ? "" : "s");
        printf("    Cause : %s\n", known[i].cause);
        printf("    Fix   : %s\n", known[i].fix);
    }

    qsort(repeats, (size_t)nrepeats, sizeof repeats[0], cmp_repeat_desc);
    int shown = 0;
    for (int i = 0; i < nrepeats && shown < 5; i++) {
        if (repeats[i].count < REPEAT_THRESHOLD) break;
        if (shown == 0)
            printf("\n--- Repeated problems (seen %d+ times) ---\n", REPEAT_THRESHOLD);
        printf("* x%d: %.140s\n", repeats[i].count, repeats[i].sample);
        shown++;
    }
    if (shown > 0)
        printf("  A message that keeps repeating usually means the root cause is not fixed yet,\n"
               "  or a loop/retry is hammering a failing resource.\n");

    long nserious = nerr + ncrit;
    if (nserious > 0) {
        long cnt = nserious < RECENT ? nserious : RECENT;
        long start = nserious < RECENT ? 0 : nserious % RECENT;
        printf("\n--- Most recent errors ---\n");
        for (long i = 0; i < cnt; i++)
            printf("  %.150s\n", recent[(start + i) % RECENT]);
    }

    printf("\n--- Overall verdict ---\n");
    if (ncrit > 0)
        printf("CRITICAL: %ld critical event(s). Fix these first.\n", ncrit);
    else if (nerr > 0)
        printf("PROBLEMS FOUND: %ld error(s). Review the suggestions above.\n", nerr);
    else
        printf("MINOR: only warnings. Nothing urgent, but worth reviewing.\n");

    return (int)nproblem;
}

void analyze_logs(void)
{
    char path[PATH_MAX];

    for (;;) {
        printf("\n===== Log Analyzer =====\n");
        printf("Enter log file path (e.g. /var/log/syslog, ./app.log), or press Enter to go back: ");
        fflush(stdout);
        if (!fgets(path, sizeof path, stdin))
            return;
        path[strcspn(path, "\n")] = '\0';
        if (path[0] == '\0')
            return;
        analyze_log_file(path);
    }
}
