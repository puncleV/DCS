#include "ipc.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char *const argv[])
{
	int opt, processCount;
	while ((opt = getopt(argc, argv, "p:")) != -1) {
		switch (opt) {
		case 'p':
			processCount = atoi(optarg);
		break;
		default:
			fprintf(stderr, "Usage: %s [-p processCount]\n",
			   argv[0]);
			exit(-1);
		}
	}

	printf("Count: %d\n", processCount);
	return 0;
}