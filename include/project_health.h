#ifndef PROJECT_HEALTH_H
#define PROJECT_HEALTH_H

/* Asks for a project folder, runs rule-based health checks on it
 * (git, README, build file, .gitignore, sources, tests, binaries...)
 * and prints problems, suggestions and a health score. Read-only. */
void check_project_health(void);

#endif
