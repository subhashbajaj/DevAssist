#include <stdio.h>
#include "process_manager.h"
#include "disk_analyzer.h"
#include "log_analyzer.h"
#include "permission_checker.h"
#include "project_health.h"
#include "system_monitor.h"
int main(void)
{
	int choice = 0;

	do{
	printf("\n======DevAssist =====\n");
	printf("1. Process Manager\n");
	printf("2. Disk Analyzer\n");
	printf("3. log analyzer\n");
	printf("4. Permission Checker\n");
	printf("5. Project Health Checker\n");
	printf("6. CPU & RAM Monitor\n");
	printf("7. Exit\n");
	printf("Enter your choice: ");

	if(scanf("%d" , &choice) !=1){
		printf("Invalid input\n");
		return 1;
        }
	int ch;
	while((ch = getchar()) != '\n' && ch != EOF) {
}

	switch (choice) {
	case 1:
		show_processes();
                break;
        case 2:
                show_disk_usage();
                break;
        case 3:
                analyze_logs();
                break;
        case 4:
                check_permissions();
                break;
	case 5:
		check_project_health();
		break;
	case 6:
		check_system_resources();
		break;
        case 7:
                printf("Exiting DevAssist. Goodbye!\n");
                break;
        default:
                printf("Invalied choice. enter 1 to 7.\n");
    }
 } while (choice != 7);

   return 0;
}
	
