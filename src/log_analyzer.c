#include <stdio.h>
#include <stdlib.h>
#include "log_analyzer.h"

void analyze_logs(void)
{
	printf("\n--- Log Analyzer ---\n");
	printf("Searching system logs for errors...\n");

	system("journalctl -p err -n 20 --no-pager 2>/dev/null");
}
