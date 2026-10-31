#ifndef PERMISSION_CHECKER_H
#define PERMISSION_CHECKER_H

/* Menu for the Permission Detector (called from main.c). */
void check_permissions(void);

/* Analyze one file or directory. Returns number of issues, -1 on error. */
int check_path_permissions(const char *path);

/* Scan every entry directly inside a directory (non-recursive). */
int check_directory_permissions(const char *dir);

#endif
