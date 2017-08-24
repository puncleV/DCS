#include "pa1.h"
#include "ipc.h"
#include "common.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <time.h>

int eventsLog;
int pipesLog;



int processCountGlobal;
typedef struct {
    int write;
    int read;
} PipeDescriptor;
PipeDescriptor** pipes;


void pipeLogPut(int pipeStatus, local_id source, local_id destination) {
    char buffer[255];

    sprintf(buffer, ((pipeStatus == 0) ? "pipe [%i][%i] is opened\n" : "pipe [%i][%i] is closed\n"), source, destination);
    write(pipesLog, buffer, strlen(buffer));
}

void pipesInit() {
    pipes = (PipeDescriptor**) malloc(sizeof(PipeDescriptor*) * processCountGlobal);
    for(int i = 0 ; i < processCountGlobal; i++) {
        pipes[i] = (PipeDescriptor*) malloc(sizeof(PipeDescriptor) * (processCountGlobal - 1));
    }


    for (int i = 0; i < processCountGlobal; i++) {
        for (int j = 0; j < processCountGlobal; j++) {
            if ( j != i ) {
    			int descriptors[2];

                pipe(descriptors);

                pipes[i][j].write = descriptors[1];
                pipeLogPut(0, i, j);

                pipes[j][i].read = descriptors[0];
                pipeLogPut(0, j, i);
            }
        }
    }
}

void evetLogPut(char buffer[255]) {
    printf(buffer, 0);
    write(eventsLog, buffer, strlen(buffer));
}

void startChildLog(local_id currentLocalId) {
    char buffer[255];

    sprintf(buffer, log_started_fmt, currentLocalId, getpid(), getppid());
    evetLogPut(buffer);
}

void logReceiveStarted(local_id currentLocalId) {
    char buffer[255];

    sprintf(buffer, log_received_all_started_fmt, currentLocalId);
    evetLogPut(buffer);
}

void logWorksDone(local_id currentLocalId) {
    char buffer[255];

    sprintf(buffer, log_done_fmt, currentLocalId);
    evetLogPut(buffer);
}

void logReceiveDone(local_id currentLocalId) {
    char buffer[255];

    sprintf(buffer, log_received_all_done_fmt, currentLocalId);
    evetLogPut(buffer);
}

void closePipe(PipeDescriptor pipe) {
	close(pipe.write);
	close(pipe.read);
}
void closeUnusedPipes(local_id currentLocalId) {
    for (local_id localId = 0; localId < processCountGlobal; localId++) {
        if (localId != currentLocalId) {
	        for (local_id j = 0; j < processCountGlobal; j++) {
	            if (localId != j) {
	                closePipe(pipes[localId][j]);
	                pipeLogPut(1, localId, j);
	            }
        	}
        }
    }
}

void setMessageHeader(int len, Message msg) {
    msg.s_header.s_magic = MESSAGE_MAGIC;
    msg.s_header.s_payload_len = len;
    msg.s_header.s_type = STARTED;
    msg.s_header.s_local_time = time(NULL);
}

void startChild(local_id id, balance_t balance) {
    Message message;
    int len = sprintf(message.s_payload, log_started_fmt, id, getpid(), getppid());

    startChildLog(id);

    setMessageHeader(len, message);

    if ( send_multicast(&id, &message) != 0 ) {
        exit(EXIT_FAILURE);
    }

    receive_any(&id, NULL);
    logReceiveStarted(id);
}

void stopChild(local_id id) {
    Message message;
    int len = sprintf(message.s_payload, log_done_fmt, id);
    
    logWorksDone(id);

    setMessageHeader(len, message);

    if (send_multicast(&id, &message) != 0) {
        exit(EXIT_FAILURE);
    }

    receive_any(&id, NULL);
    logReceiveDone(id);
}


void someFunction(local_id id) {
	int a = id;
	a++;
}

void childWork(local_id id) {
    closeUnusedPipes(id);
    startChild(id);
    someFunction(id);
    stopChild(id);
}

int send(void * self, local_id dst, const Message * msg) {
    local_id currentLocalId = *(local_id *)self;
    PipeDescriptor pipe = pipes[currentLocalId][dst];
    int result = write(pipe.write, msg, sizeof(MessageHeader) + msg -> s_header.s_payload_len);
    return (result > 0) ? 0 : 1;
}

int send_multicast(void * self, const Message * msg) {
    local_id currentLocalId = *(local_id *)self;
    int res = 0;
    for (local_id destinationLocalId = 0; destinationLocalId < processCountGlobal; destinationLocalId++) {
        if (destinationLocalId != currentLocalId) {
            if (send(self, destinationLocalId, msg) != 0) {
                res = 1;
            }
        }

    }
    return res;
}

int receive (void * self, local_id from, Message * msg) {
    int res = 0, payloadLength = 0;
    local_id src = *(local_id *)self;
    MessageHeader msgHeader;

    if ( read(pipes[src][from].read, &msgHeader, sizeof(MessageHeader)) != sizeof(MessageHeader) ) {
        res = 1;
    }

    payloadLength = msgHeader.s_payload_len;
    char readContentBuf[payloadLength];

    if ( read(pipes[src][from].read, readContentBuf, payloadLength) != payloadLength ) {
        res = 1; 
    }

    msg -> s_header = msgHeader;
    strcpy(msg -> s_payload, readContentBuf);

    return res;
}

int receive_any(void * self, Message * msg) {
    local_id currentLocalId = *(local_id *)self;
    Message message;
    int res = 0;
    for (local_id sourceLocalId = 1; sourceLocalId < processCountGlobal; sourceLocalId++) {
        if ( sourceLocalId != currentLocalId ) {
            if ( receive(&currentLocalId, sourceLocalId, &message) != 0 ) {
                res = 1;
            }
        }
    }
    return res;
}

int main(int argc, char *const *argv)
{
    int opt, processCount = 0;
	while ( ( opt = getopt(argc, argv, "p:") ) != -1 ) {
        switch (opt) {
	        case 'p':
	            processCount = atoi(optarg);
	                if( processCount == 0 ) {  perror("You have to use valid value for process count"); exit(EXIT_FAILURE); }
	            processCountGlobal = processCount + 1;
	        break;
	        default:
	            fprintf(stderr, "Usage: %s [-p processCount]\n",
	               argv[0]);
	            exit(EXIT_FAILURE);
        }
    }
    if( processCount == 0 ) { 
    	fprintf(stderr, "Usage: %s [-p processCount]\n", argv[0]);
    	exit(EXIT_FAILURE);
    }


    if( (pipesLog = open(pipes_log, O_WRONLY|O_CREAT, 0777)) < 0 ) { perror("CANT OPEN PIPES LOG"); exit(EXIT_FAILURE); }
    if( (eventsLog = open(events_log, O_WRONLY|O_CREAT, 0777)) < 0 ) { perror("CANT OPEN EVENT LOG"); exit(EXIT_FAILURE); }

    pid_t processId[processCountGlobal];
    processId[0] = getpid();

 	pipesInit();

    for (local_id localId = 1; localId < processCountGlobal; localId++) {
        processId[localId] = fork();
        if (processId[localId] == 0) {
            childWork(localId);
            exit(EXIT_SUCCESS);
        }
    }

    closeUnusedPipes(0);
    while( wait(NULL) > 0 );

    free(pipes);

    close(pipesLog);
    close(eventsLog);

	return EXIT_SUCCESS;
}
