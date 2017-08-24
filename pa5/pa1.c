#define _GNU_SOURCE

#include "pa2345.h"
#include "ipc.h"
#include "common.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <getopt.h>

void setPending();
void clearPending();

typedef struct
{
    int done;
    int pending;
    timestamp_t time;
    void (*setPending)(int);
    void (*clearPending)(int);
} Queue;

Queue queue[200];

void setQueuePending(int id) {
    queue[id].pending = 1;
}

void clearQueuePending(int id) {
    queue[id].pending = 0;
}

void setPending( int id ) {
    queue[id].pending = 1;
}
void clearPending(int id) {
    queue[id].pending = 0;
}

typedef struct {
    int descriptor[2];
} PipeDescriptor;

PipeDescriptor** pipes;
int processCountGlobal, eventsLog, pipesLog, currentReceivedId;

int useMutex = 0;

typedef struct {
    timestamp_t time;
    int (*getTime)();
    void (*setTime)(timestamp_t);
} LamportTime;

int getTime();
void setTime(timestamp_t newTime);
LamportTime lamportTime = {
        0,
        getTime,
        setTime
};

int getTime(){
    return lamportTime.time;
}
void setTime(timestamp_t timeToSet){
    timestamp_t newTime = ( lamportTime.time > timeToSet ) ? lamportTime.time : timeToSet;
    lamportTime.time = newTime + 1;
}

void eventLog(char buffer[100]) {
    printf(buffer, 0);
    write(eventsLog, buffer, strlen(buffer));
}

void startChildLog(local_id currentLocalId) {
    char outputString[100];

    sprintf(outputString, log_started_fmt, lamportTime.getTime(), currentLocalId, getpid(), getppid(), 0);
    eventLog(outputString);
}

void logReceiveStarted(local_id currentLocalId) {
    char outputString[100];

    sprintf(outputString, log_received_all_started_fmt, lamportTime.getTime(), currentLocalId);
    eventLog(outputString);
}

void logWorksDone(local_id currentLocalId) {
    char outputString[100];

    sprintf(outputString, log_done_fmt, lamportTime.getTime(), currentLocalId, 0);
    eventLog(outputString);
}

void logReceiveDone(local_id currentLocalId) {
    char outputString[100];

    sprintf(outputString, log_received_all_done_fmt, lamportTime.getTime(), currentLocalId);
    eventLog(outputString);
}

int getLastCs() {
    local_id lastCsId = 0;
    for (int idToCheck = 1; idToCheck <= processCountGlobal - 1; idToCheck++)
        if ( queue[idToCheck].pending ) {
            if ( lastCsId == 0 ) lastCsId = idToCheck;
            else if ( queue[idToCheck].time < queue[lastCsId].time ) lastCsId = idToCheck;
        }
    return lastCsId;
}

int receiveAny(void * self, Message * msg) {
    local_id currentProcessLocalId = *(local_id *)self;
    for(;;) {
        for (local_id i = 0; i < processCountGlobal; i++) {
            if (i != currentProcessLocalId) {
                int bytes_count;
                if ( (bytes_count = read(pipes[currentProcessLocalId][i].descriptor[1], &(msg->s_header), sizeof(MessageHeader))) > 0 ) {
                    read(pipes[currentProcessLocalId][i].descriptor[1], msg->s_payload, msg->s_header.s_payload_len);
                    lamportTime.setTime(msg->s_header.s_local_time);
                    currentReceivedId = i;
                    return 0;
                }
            }
        }
    }
}

Message fillMessageHeader(int16_t type, uint16_t len) {
    lamportTime.setTime(0);
    Message filledMessage;
    filledMessage.s_header.s_magic = MESSAGE_MAGIC;
    filledMessage.s_header.s_payload_len = len;
    filledMessage.s_header.s_type = type;
    filledMessage.s_header.s_local_time = lamportTime.getTime();
    return filledMessage;
}

int request_cs(const void * self) {
    Message messageToSend = fillMessageHeader(CS_REQUEST, 0);
    local_id currentLocalId = *(local_id *)self;
    int replyCount = 0;

    queue[currentLocalId].time = messageToSend.s_header.s_local_time;
    setQueuePending(currentLocalId);
    send_multicast(&currentLocalId, &messageToSend);

    Message receivedMessage;
    for(;;) {
        receiveAny(&currentLocalId, &receivedMessage);
        int receivedMessageType = receivedMessage.s_header.s_type;

        if( receivedMessageType  == CS_REQUEST ) {
            queue[currentReceivedId].time = receivedMessage.s_header.s_local_time;
            setQueuePending(currentReceivedId);
            messageToSend = fillMessageHeader(CS_REPLY, 0);
            send(&currentLocalId, currentReceivedId, &messageToSend);
        }

        if( receivedMessageType == CS_REPLY ) {
            replyCount++;
            if ( (replyCount == processCountGlobal - 2) && ( getLastCs() == currentLocalId ) ) break;
        }

        if( receivedMessageType == CS_RELEASE ) {
            clearQueuePending(currentReceivedId);
            if ( (replyCount == processCountGlobal - 2) && ( getLastCs() == currentLocalId ) ) break;
        }

        if( receivedMessageType == DONE ) {
            queue[currentReceivedId].done = 1;
        }
    }
    return 0;
}


int release_cs(const void * self) {
    Message messageToSend = fillMessageHeader(CS_RELEASE, 0);
    local_id currentLocalId = *(local_id *)self;
    clearQueuePending(currentLocalId);
    send_multicast(&currentLocalId, &messageToSend);
    return 0;
}

int isQueueDone() {
    int res = 1;
    for (int i = 1; ( i <= processCountGlobal - 1 ) && res; i++) {
        res = res && queue[i].done;
    }
    return res;
}

void pipeLog(int pipeStatus, local_id source, local_id destination) {
    char outputString[100];

    sprintf(outputString, ((pipeStatus == 0) ? "pipe [%i][%i] is opened\n" : "pipe [%i][%i] is closed\n"), source, destination);
    write(pipesLog, outputString, strlen(outputString));
}

int send(void * self, local_id dst, const Message * msg) {
    local_id currentProcessLocalId = *(local_id *)self;
    PipeDescriptor pipe = pipes[currentProcessLocalId][dst];
    return !(write(pipe.descriptor[0], msg, sizeof(MessageHeader) + msg->s_header.s_payload_len) > 0);
}

int send_multicast(void * self, const Message * msg) {
    int res = 0;
    local_id currentProcessLocalId = *(local_id *)self;
    for (local_id i = 0; i < processCountGlobal; i++) {
        if ( i != currentProcessLocalId ) {
            if ( send(self, i, msg) != 0 ){
                perror("send_multicast()\n");
                res = 1;
            }
        }
    }
    return res;
}

int receive (void * self, local_id source, Message * receivedMessage) {
    local_id destination = *(local_id *)self;
    for(;;) {
        int n;
        if ( (n = read(pipes[destination][source].descriptor[1],
                       &(receivedMessage->s_header), sizeof(MessageHeader)) ) != -1) {
            read(pipes[destination][source].descriptor[1],
                 receivedMessage->s_payload, receivedMessage->s_header.s_payload_len);
            lamportTime.setTime(receivedMessage->s_header.s_local_time);
            break;
        }
    }
    return 0;
}

void closePipe(PipeDescriptor pipe) {
    close(pipe.descriptor[0]);
    close(pipe.descriptor[1]);
}

void closeUnusedPipes(local_id currentProcessLocalId) {
    for (local_id i = 0; i < processCountGlobal; i++) {
        if (i != currentProcessLocalId) {
            for (local_id j = 0; j < processCountGlobal; j++) {
                if (i != j) {
                    closePipe(pipes[i][j]);
                    pipeLog(1, i, j);
                }
            }
        }
    }
}

int receiveAll(local_id currentProcessLocalId, int16_t type) {
    int res = 0;
    Message receivedMessage;
    for (local_id i = 1; i < processCountGlobal; i++) {
        if (i != currentProcessLocalId) {
            if (receive(&currentProcessLocalId, i, &receivedMessage) != 0) {
                printf("receive()\n");
                res = 1;
            } else {
                for (;;) {
                    if (receivedMessage.s_header.s_type == type) break;
                    receive(&currentProcessLocalId, i, &receivedMessage);
                }
            }
        }
    }
    return res;
}

void clientStart(local_id currentLocalProcessId) {
    startChildLog(currentLocalProcessId);
    Message startMessage = fillMessageHeader(STARTED,
                                             sprintf(startMessage.s_payload,
                                                     log_started_fmt,
                                                     lamportTime.getTime(),
                                                     currentLocalProcessId, getpid(), getppid(), 0));

    send_multicast(&currentLocalProcessId, &startMessage);

    receiveAll(currentLocalProcessId, STARTED);
    logReceiveStarted(currentLocalProcessId);
}

void waitClientsDone(local_id currentLocalId) {
    Message receivedMessage;
    queue[currentLocalId].done = 1;
    if ( !isQueueDone() ) {
        Message messageToSend;
        for(int donesLeft = processCountGlobal - 2; ( donesLeft > 0 ) && !isQueueDone();) {
            receiveAny(&currentLocalId, &receivedMessage);
            int receivedMessageType = receivedMessage.s_header.s_type;
            if( receivedMessageType == CS_REQUEST ) {
                queue[currentReceivedId].time = receivedMessage.s_header.s_local_time;
                queue[currentReceivedId].pending = 1;
                messageToSend = fillMessageHeader(CS_REPLY, 0);
                send(&currentLocalId, currentReceivedId, &messageToSend);
            }
            if( receivedMessageType == DONE ) {
                donesLeft--;
                queue[currentReceivedId].done = 1;
            }
        }
    }
}

void clientKill(local_id currentProcessLocalId) {
    logWorksDone(currentProcessLocalId);
    Message doneMessage = fillMessageHeader(DONE, sprintf(doneMessage.s_payload, log_done_fmt, lamportTime.getTime(), currentProcessLocalId, 0));

    send_multicast(&currentProcessLocalId, &doneMessage);
    waitClientsDone( currentProcessLocalId );

    logReceiveDone(currentProcessLocalId);
}

void client(local_id currentProcessLocalId) {
    closeUnusedPipes(currentProcessLocalId);
    clientStart(currentProcessLocalId);
    char * str = (char *) calloc(100, sizeof(char));
    for (int i = 1; i <= currentProcessLocalId * 5; i++) {
        int iteration = currentProcessLocalId * 5;
        sprintf(str, log_loop_operation_fmt, currentProcessLocalId, i, iteration);
        if (useMutex) {
            request_cs(&currentProcessLocalId);
            print(str);
            release_cs(&currentProcessLocalId);
        }
    }
    clientKill(currentProcessLocalId);
}

void stopAll(int parent) {
    Message stopMessage = fillMessageHeader(STOP, 0);
    send_multicast(&parent, &stopMessage);
}

int main(int argc, char** argv) {
    int processCount, opt;
    while ( ( opt = getopt(argc, argv, "p:-:") ) != -1 ) {
        switch (opt) {
            case 'p':
                processCount = atoi(optarg);
                if( processCount == 0 ) {  perror("You have to use valid value for process count"); exit(EXIT_FAILURE); }
                processCountGlobal = processCount + 1;
                break;
            case '-':
                    useMutex = 1;
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

    pipes = (PipeDescriptor**) malloc(sizeof(PipeDescriptor*) * processCountGlobal);
    for(int i = 0 ; i < processCountGlobal; i++) {
        pipes[i] = (PipeDescriptor*) malloc(sizeof(PipeDescriptor) * (processCountGlobal - 1));
    }
    for (int i = 0; i < processCountGlobal; i++) {
        for (int j = 0; j < processCountGlobal; j++) {
            if (i != j) {
                int fd[2];

                pipe2(fd, O_NONBLOCK);

                pipes[i][j].descriptor[0] = fd[1];
                pipeLog(0, i, j);

                pipes[j][i].descriptor[1] = fd[0];
                pipeLog(0, j, i);
            }
        }
    }
    pid_t processId[processCountGlobal];
    processId[0] = getpid();
    local_id processLocalId;
    for (processLocalId = 1; processLocalId < processCountGlobal; processLocalId++) {
        processId[processLocalId] = fork();
        if (processId[processLocalId] < 0) {
            perror("fork()");
            exit(EXIT_FAILURE);
        } else if(processId[processLocalId] == 0){
            client(processLocalId);
            exit(EXIT_SUCCESS);
        }
    }
    closeUnusedPipes(0);
    local_id parentId = PARENT_ID;
    receiveAll(parentId, STARTED);
    stopAll(parentId);
    receiveAll(parentId, DONE);

    while(wait(NULL) > 0);
    free(pipes);
    close(eventsLog);
    close(pipesLog);
    return EXIT_SUCCESS;
}
