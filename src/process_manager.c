#include <stdio.h>
#include <stdlib.h>
#include "process_manager.h"
void show_processes(void)
{
	printf("\n--- Running Process ---\n");
	system("ps -eo pid,comm --sort=pid");
	printf("\n");
}
