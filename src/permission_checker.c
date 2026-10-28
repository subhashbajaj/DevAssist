#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "permission_checker.h"

void check_permissions(void)
{
	char path[256];
	printf("\n--- Permission Checker ---\n");
	printf("enter file or directory path: ");
	if (fgets(path, sizeof(path), stdin) == NULL) {
	printf("Input error.\n");
	return;
}

	path[strcspn(path, "\n")] = '\0';

	if(path[0] == '0') {
	printf("No path entered.\n");
	return;
}

	char command[512];
	snprintf(command, sizeof(command), "ls -ld -- '%s'", path);
	system(command);
}
