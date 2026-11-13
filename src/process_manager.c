#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "process_manager.h"

void show_processes(void)
{
    int choice;
    int port;
    int c;

    do {
        printf("\n==== Process & Port Manager ====\n");
        printf("1. Show Running Processes\n");
        printf("2. Show Listening Ports\n");
        printf("3. Check Specific Port Conflict\n");
        printf("0. Back to Main Menu\n");
        printf("Enter your choice: ");

        if (scanf("%d", &choice) != 1) {
            printf("Invalid input.\n");
            while ((c = getchar()) != '\n' && c != EOF);
            return;
        }
        /* discard the rest of the line (the Enter key) so fgets() works later */
        while ((c = getchar()) != '\n' && c != EOF);

        switch (choice) {
        case 1:
            printf("\n--- Running Processes ---\n");
            system("ps -eo pid,comm --sort=pid");
            printf("\nTotal Processes: ");
            fflush(stdout);
            system("ps -e --no-headers | wc -l");
            break;

        case 2:
            printf("\n--- Listening TCP and UDP Ports ---\n");
            system("ss -lntup");
            printf("\nNote: Some process details may require sudo.\n");
            break;

        case 3: {
            char line[64];
            char command[160];
            char *end;
            long value;
            size_t len;

            printf("\nEnter port number (1-65535): ");
            if (fgets(line, sizeof(line), stdin) == NULL) {
                printf("\nInvalid input.\n");
                return;
            }
            len = strlen(line);
            if (len > 0 && line[len - 1] == '\n')
                line[len - 1] = '\0';
            else
                while ((c = getchar()) != '\n' && c != EOF);

            if (line[0] == '\0') {
                printf("Invalid input: no port number entered.\n");
                break;
            }
            value = strtol(line, &end, 10);
            if (end == line || *end != '\0') {
                printf("Invalid port number.\n");
                break;
            }
            if (value < 1 || value > 65535) {
                printf("Port must be between 1 and 65535.\n");
                break;
            }
            port = (int)value;

            printf("\nChecking port %d...\n", port);
            snprintf(command, sizeof(command),
                     "ss -H -lntup 'sport = :%d' 2>/dev/null | grep -q .", port);

            if (system(command) == 0) {
                printf("Port %d is OCCUPIED.\n", port);
                printf("Warning: another process is already using this port.\n");
                printf("Details:\n");
                fflush(stdout);
                snprintf(command, sizeof(command),
                         "ss -H -lntup 'sport = :%d' 2>/dev/null", port);
                system(command);
                printf("Suggestion: identify the process using this port before "
                       "starting another one.\n");
                printf("            If it is safe, stop that process, or choose "
                       "a different port.\n");
            } else {
                printf("Port %d is FREE.\n", port);
            }
            break;
        }

        case 0:
            printf("Returning to main menu...\n");
            break;

        default:
            printf("Invalid choice. Try again.\n");
        }
    } while (choice != 0);
}
