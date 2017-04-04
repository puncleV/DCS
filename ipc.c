#include "ipc.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char *const argv[])
{
    int opt, processCount;
//	pid_t *pids;

    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
        case 'p':
            processCount = atoi(optarg);
                if( processCount == 0 ) { exit(EXIT_FAILURE); }
        break;
        default:
            fprintf(stderr, "Usage: %s [-p processCount]\n",
               argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    printf("Count: %d\n", processCount);
    exit(EXIT_SUCCESS);
}