/*
 * system_monitor.c - CPU & RAM Monitor module for DevAssist
 * Author: Sneha Kanjwani
 * Read-only: reads /proc and never changes anything.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <dirent.h>
#include "system_monitor.h"

#define RAM_WARN   80
#define RAM_CRIT   90
#define SWAP_WARN  50
#define CPU_WARN   80
#define TOP_N      5

typedef struct { unsigned long long total, idle; } CpuTimes;
typedef struct { int pid; char name[64]; long rss_kb; } Proc;

static int read_cpu(CpuTimes *t)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;
    unsigned long long u, n, s, i, w, irq, sirq, st;
    int r = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &u, &n, &s, &i, &w, &irq, &sirq, &st);
    fclose(f);
    if (r < 8) return -1;
    t->idle = i + w;
    t->total = u + n + s + i + w + irq + sirq + st;
    return 0;
}

static int read_mem(long *total, long *avail, long *swap_total, long *swap_free)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;
    char line[128];
    int found = 0;
    *total = *avail = *swap_total = *swap_free = 0;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "MemTotal: %ld kB", total) == 1) found++;
        else if (sscanf(line, "MemAvailable: %ld kB", avail) == 1) found++;
        else if (sscanf(line, "SwapTotal: %ld kB", swap_total) == 1) found++;
        else if (sscanf(line, "SwapFree: %ld kB", swap_free) == 1) found++;
    }
    fclose(f);
    return found >= 2 ? 0 : -1;
}

static void top_memory_processes(void)
{
    Proc top[TOP_N];
    memset(top, 0, sizeof top);

    DIR *d = opendir("/proc");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;

        char path[300], line[256], name[64] = "?";
        long rss = 0;
        snprintf(path, sizeof path, "/proc/%s/status", e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        while (fgets(line, sizeof line, f)) {
            if (strncmp(line, "Name:", 5) == 0)
                sscanf(line, "Name: %63s", name);
            else if (strncmp(line, "VmRSS:", 6) == 0)
                sscanf(line, "VmRSS: %ld kB", &rss);
        }
        fclose(f);
        if (rss <= 0) continue;

        int min = 0;
        for (int i = 1; i < TOP_N; i++)
            if (top[i].rss_kb < top[min].rss_kb) min = i;
        if (rss > top[min].rss_kb) {
            top[min].pid = atoi(e->d_name);
            top[min].rss_kb = rss;
            strcpy(top[min].name, name);
        }
    }
    closedir(d);

    for (int i = 0; i < TOP_N; i++)
        for (int j = i + 1; j < TOP_N; j++)
            if (top[j].rss_kb > top[i].rss_kb) {
                Proc t = top[i]; top[i] = top[j]; top[j] = t;
            }

    printf("\n--- Top %d memory-using processes ---\n", TOP_N);
    printf("%-8s %-20s %s\n", "PID", "NAME", "MEMORY");
    for (int i = 0; i < TOP_N; i++)
        if (top[i].rss_kb > 0)
            printf("%-8d %-20s %.1f MB\n", top[i].pid, top[i].name, top[i].rss_kb / 1024.0);
}

void check_system_resources(void)
{
    int problems = 0;

    printf("\n===== CPU & RAM Monitor =====\n");

    /* ---------- RAM ---------- */
    long total, avail, swt, swf;
    if (read_mem(&total, &avail, &swt, &swf) != 0) {
        printf("Error: could not read /proc/meminfo.\n");
        return;
    }
    long used = total - avail;
    double ram_pct = total > 0 ? used * 100.0 / total : 0.0;

    printf("\n--- Memory (RAM) ---\n");
    printf("Total     : %.0f MB\n", total / 1024.0);
    printf("Used      : %.0f MB\n", used / 1024.0);
    printf("Available : %.0f MB\n", avail / 1024.0);
    printf("Usage     : %.0f%%  (warning at %d%%)\n", ram_pct, RAM_WARN);

    if (ram_pct >= RAM_CRIT) {
        printf("[CRITICAL] RAM is almost full. The system may become very slow or kill programs (OOM killer).\n");
        printf("           Fix: close heavy programs; see the list below; check with: free -h\n");
        problems++;
    } else if (ram_pct >= RAM_WARN) {
        printf("[WARNING] RAM usage is high.\n");
        printf("          Fix: close programs you do not need, or look for a memory leak in the top process below.\n");
        problems++;
    } else {
        printf("[OK] RAM usage is normal.\n");
    }

    if (swt > 0) {
        double swap_pct = (swt - swf) * 100.0 / swt;
        printf("Swap      : %.0f MB used of %.0f MB (%.0f%%)\n",
               (swt - swf) / 1024.0, swt / 1024.0, swap_pct);
        if (swap_pct >= SWAP_WARN) {
            printf("[WARNING] Heavy swap use means RAM is too small for the current workload; the system will feel slow.\n");
            printf("          Fix: free memory by closing programs, or add more RAM to the VM.\n");
            problems++;
        }
    } else {
        printf("Swap      : not configured\n");
    }

    /* ---------- CPU ---------- */
    printf("\n--- CPU ---\n");
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores < 1) cores = 1;
    printf("Cores     : %ld\n", cores);

    double l1 = 0, l5 = 0, l15 = 0;
    FILE *lf = fopen("/proc/loadavg", "r");
    if (lf) {
        if (fscanf(lf, "%lf %lf %lf", &l1, &l5, &l15) == 3)
            printf("Load avg  : %.2f (1 min), %.2f (5 min), %.2f (15 min)\n", l1, l5, l15);
        fclose(lf);
    }

    CpuTimes a, b;
    double cpu_pct = -1.0;
    printf("Measuring CPU usage for 1 second...\n");
    if (read_cpu(&a) == 0) {
        sleep(1);
        if (read_cpu(&b) == 0 && b.total > a.total)
            cpu_pct = 100.0 * (double)((b.total - a.total) - (b.idle - a.idle)) / (double)(b.total - a.total);
    }
    if (cpu_pct >= 0) {
        printf("Usage     : %.0f%%  (warning at %d%%)\n", cpu_pct, CPU_WARN);
        if (cpu_pct >= CPU_WARN) {
            printf("[WARNING] CPU usage is high right now.\n");
            printf("          Fix: find the busy process with: top  (press P to sort by CPU), then stop or fix it.\n");
            problems++;
        } else {
            printf("[OK] CPU usage is normal.\n");
        }
    } else {
        printf("Could not measure CPU usage.\n");
    }

    if (l5 / cores > 1.0) {
        printf("[WARNING] Load is higher than the number of cores (%.2f per core): processes are waiting for the CPU.\n", l5 / cores);
        printf("          Fix: reduce parallel jobs or find CPU-heavy processes with: top\n");
        problems++;
    }

    top_memory_processes();

    printf("\n--- Summary ---\n");
    if (problems == 0)
        printf("System resources look healthy.\n");
    else
        printf("%d problem(s) found. Review the suggestions above.\n", problems);
}
