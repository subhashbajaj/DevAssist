#include <stdio.h>
#include <tdlib.h>
#include "disk_analyzer.h"
void show_disk_usage(void)
{
	printf("\n--- Disk Usage ---\n");
	system("df -h");
	printf("\n");
}
