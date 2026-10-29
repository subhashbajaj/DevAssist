#ifndef DISK_ANALYZER_H
#define DISK_ANALYZER_H

/* Shows filesystem usage for "/" with a warning if usage is high,
 * then offers a small sub-menu (1. Find large files, 0. Back). */
void show_disk_usage(void);

/* Asks for a directory path and a size threshold, then lists large files. */
void find_large_files(void);

#endif
