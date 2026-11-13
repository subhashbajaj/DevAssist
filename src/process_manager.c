#include <stdio.h>
#include <stdlib.h>
#include "process_manager.h"
void show_processes(void)
{
	int choice;
	int port;
	do {
	printf("\n==== Process & Port Manager ====\n");
	printf("1. Sow Running Processes\n");
	printf("2. Show Listening Ports\n");
	printf("3. Check Specific Port Conflict\n");
	printf("0. Back to Main Menu\n");
	printf("Enter your choice: ");

	if (scanf("%d" , &choice) !=1) {
	printf("Invalid input.\n");
	while (getchar() != '\n');
	return;	
}

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

	case 3:
	  printf("\nEnter port number (1-65535): ");

	  if(scanf("%d", &port) != 1) {
	   printf("onvalid port number.\n");
	   while (getchar() != '\n');
	   break;
	  }
	  if (port < 1 || port > 65535) {
	  printf("Port must be between 1 and 65535.\n");
	  break;
	  }
	  printf("\nChecking port %d...\n", port);

	  {
	    char command[160];
	    snprintf(command, sizeof(command),
		"ss -H -lntup 'sport = :%d' 2>/dev/null | grep -q .",
	         port);

	   if (system(command) == 0) {
	      printf("Port %d is OCCUPIED.\n", port);
	   } else {
              printf("Port %d is FREE.\n", port);
           }
        }
	break;	
	case 0:
	  printf("Returning to main menu...\n");
	  break;

	default:
	  printf("Invalid choice. Try again.\n");
     }
 } while (choice != 0);
}
