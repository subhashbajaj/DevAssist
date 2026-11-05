#ifndef LOG_ANALYZER_H
#define LOG_ANALYZER_H

/* Menu for the Log Analyzer (called from main.c). */
void analyze_logs(void);

/* Analyze one log file. Returns number of problem lines, -1 on error. */
int analyze_log_file(const char *path);

#endif
