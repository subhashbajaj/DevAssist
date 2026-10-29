#ifndef DISK_ANALYZER_H
#define DISK_ANALYZER_H

/* Shows filesystem usage (space + inodes) for "/" with warnings,
 * then offers a sub-menu: find large files / find large folders / back. */
void show_disk_usage(void);

/* Asks for a directory path and a size threshold, then lists large files. */
void find_large_files(void);

/* Asks for a directory path and shows which sub-folders use the most space. */
void find_large_directories(void);

#endif
