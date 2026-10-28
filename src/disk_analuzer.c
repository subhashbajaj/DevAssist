#include <stdio.h>
#include <stdlib.h>
#include "disk_analyzer.h"

void show_disk_usage(void)
{
	printf("\n--- Disk Usuage ---\n");
	system("df -h");
	printf("\n");
}
