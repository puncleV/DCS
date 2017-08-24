#define _GNU_SOURCE

#include "pa2345.h"

#include "ipc.h"
#include "common.h"
#include "banking.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <getopt.h>

typedef struct {
    int descriptor[2];
} PipeDescriptor;


int processCountGlobal;
int eventsLog;
int pipesLog;

PipeDescriptor** pipes;

BalanceHistory balanceHistory;
balance_t balance = 0;

void pipeLog(int pipeStatus, local_id source, local_id destination) {
    char outputString[100];

    sprintf(outputString, ((pipeStatus == 0) ? "pipe [%i][%i] is opened\n" : "pipe [%i][%i] is closed\n"), source, destination);
    write(pipesLog, outputString, strlen(outputString));
}

void eventLog(char buffer[100]) {
    printf(buffer, 0);
    write(eventsLog, buffer, strlen(buffer));
}

void startChildLog(local_id currentLocalId) {
    char outputString[100];

    sprintf(outputString, log_started_fmt, lamportTime.getTime(), currentLocalId, getpid(), getppid(), balance);
    eventLog(outputString);
}

void logReceiveStarted(local_id currentLocalId) {
    char outputString[100];

    sprintf(outputString, log_received_all_started_fmt, lamportTime.getTime(), currentLocalId);
    eventLog(outputString);
}

void logWorksDone(local_id currentLocalId) {
    char outputString[100];

    sprintf(outputString, log_done_fmt, lamportTime.getTime(), currentLocalId, balance);
    eventLog(outputString);
}

void logReceiveDone(local_id currentLocalId) {
    char outputString[100];

    sprintf(outputString, log_received_all_done_fmt, lamportTime.getTime(), currentLocalId);
    eventLog(outputString);
}

void logTransferIn(local_id currentLocalId, balance_t amount, local_id source) {
    char outputString[100];

    sprintf(outputString, log_transfer_in_fmt, lamportTime.getTime(), currentLocalId, amount, source);
    eventLog(outputString);
}

void logTransferOut(local_id currentLocalId, balance_t amount, local_id destination) {
    char outputString[100];

    sprintf(outputString, log_transfer_out_fmt, lamportTime.getTime(), currentLocalId, amount, destination);
    eventLog(outputString);
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

int receiveAll(local_id currentProcessLocalId, int16_t type) {
    int res = 0;
    Message receivedMessage;
    for (local_id i = 1; i < processCountGlobal; i++)
        if (i != currentProcessLocalId) {
            if (receive(&currentProcessLocalId, i, &receivedMessage) != 0) {
                printf("receive()\n");
                res = 1;
            }
        }
    return res;
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
                    return 0;
                }
            }
        }
    }
}

int receive (void * self, local_id source, Message * receivedMessage) {
    local_id destination = *(local_id *)self;
    for(;;) {
        int n;
        if ( (n = read(pipes[destination][source].descriptor[1],
                       &(receivedMessage->s_header), sizeof(MessageHeader)) ) != -1) {
            read(pipes[destination][source].descriptor[1],
                 receivedMessage->s_payload, receivedMessage->s_header.s_payload_len);
            lamportTime.setTime(msg->s_header.s_local_time);
            break;
        }
    }
    return 0;
}


void balanceHistoryUpdate(timestamp_t localTime) {
    uint8_t historyLen = balanceHistory.s_history_len;
    balance_t pending = 0;
    for (int i = historyLen; balanceHistory.s_history[i - 1].s_time < localTime - 1; i++ ) {
        balanceHistory.s_history_len++;
        balanceHistory.s_history[i] = balanceHistory.s_history[i - 1];
        balanceHistory.s_history[i].s_time++;
        historyLen++;
        balanceHistory.s_history[i].s_balance_pending_in = pending ?
                                                           0 : balanceHistory.s_history[i].s_balance_pending_in;
        pending = 1;
    }
    balanceHistory.s_history[historyLen].s_balance = balance;
    balanceHistory.s_history[historyLen].s_time = localTime;
    balance_t balanceDiff =
            balance_historybalanceHistory->s_history[balanceHistory->s_history_len-1].s_balance - balance;
    balanceHistory.s_history[historyLen].s_balance_pending_in = balanceDiff < 0 ? 0 : balanceDiff;
    balanceHistory.s_history_len++;

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

Message fillMessageHeader(int16_t type, uint16_t len) {
    Message filledMessage;
    filledMessage.s_header.s_magic = MESSAGE_MAGIC;
    filledMessage.s_header.s_payload_len = len;
    filledMessage.s_header.s_type = type;
    filledMessage.s_header.s_local_time = lamportTime.getTime();
    lamportTime.setTime(0);
    return filledMessage;
}

void clientStart(local_id currentLocalProcessId) {
    startChildLog(currentLocalProcessId);
    Message startMessage = fillMessageHeader(STARTED,
                                             sprintf(startMessage.s_payload,
                                                     log_started_fmt,
                                                     lamportTime.getTime(),
                                                     currentLocalProcessId, getpid(), getppid(), balance));

    send_multicast(&currentLocalProcessId, &startMessage);

    receiveAll(currentLocalProcessId, STARTED);
    logReceiveStarted(currentLocalProcessId);
}

void clientKill(local_id currentProcessLocalId) {
    logWorksDone(currentProcessLocalId);
    Message doneMessage = fillMessageHeader(DONE, sprintf(doneMessage.s_payload, log_done_fmt, lamportTime.getTime(), currentProcessLocalId, balance));

    send_multicast(&currentProcessLocalId, &doneMessage);
    receiveAll(currentProcessLocalId, DONE);

    logReceiveDone(currentProcessLocalId);

    balanceHistoryUpdate(doneMessage.s_header.s_local_time);

    Message historyMessage = fillMessageHeader(BALANCE_HISTORY, (uint16_t)sizeof(balanceHistory));
    memcpy(historyMessage.s_payload, &balanceHistory, sizeof(balanceHistory));
    send(&currentProcessLocalId, PARENT_ID, &historyMessage);
}

void client(local_id currentProcessLocalId) {
    closeUnusedPipes(currentProcessLocalId);
    clientStart(currentProcessLocalId);
    balanceHistory.s_id = currentProcessLocalId;
    balanceHistory.s_history_len = 1;
    balanceHistory.s_history[0].s_balance = balance;
    balanceHistory.s_history[0].s_balance_pending_in = 0;
    balanceHistory.s_history[0].s_time = 0;
    for( ;; ) {
        Message receivedMessage, messageToSend;
        TransferOrder *receivedOrder = (TransferOrder *)receivedMessage.s_payload;
        receiveAny(&currentProcessLocalId, &receivedMessage);
        int16_t receivedMessageType = receivedMessage.s_header.s_type;
        if( receivedMessageType == TRANSFER ) {
            if ( currentProcessLocalId == receivedOrder->s_src ) {
                balance -= receivedOrder->s_amount;
                messageToSend = fillMessageHeader(receivedMessage.s_header.s_type,
                                                  receivedMessage.s_header.s_payload_len);
                memcpy(messageToSend.s_payload, &receivedMessage.s_payload, sizeof(receivedMessage.s_payload));
                send(&currentProcessLocalId, receivedOrder->s_dst, &messageToSend);

                logTransferOut(currentProcessLocalId, receivedOrder->s_amount, receivedOrder->s_dst);
            } else {
                balance += receivedOrder->s_amount;
                messageToSend = fillMessageHeader(ACK, 0);

                send(&currentProcessLocalId, PARENT_ID, &messageToSend);

                logTransferIn(currentProcessLocalId, receivedOrder->s_amount, receivedOrder->s_src);
            }
            balanceHistoryUpdate(messageToSend.s_header.s_local_time);
        } else if( receivedMessageType == STOP ) {
            break;
        }
    }
    clientKill(currentProcessLocalId);
}

void transfer(void * parent_data, local_id src, local_id dst, balance_t amount) {
    Message messageToSend;
    Message receivedMessage;
    local_id parentId = *(local_id *)parent_data;
    TransferOrder *transferOrder = (TransferOrder *) messageToSend.s_payload;

    messageToSend = fillMessageHeader(TRANSFER, sizeof(TransferOrder));

    transferOrder->s_src = src;
    transferOrder->s_dst = dst;
    transferOrder->s_amount = amount;

    send(&parentId, src, &messageToSend);
    receive(&parentId, dst, &receivedMessage);

    if ( receivedMessage.s_header.s_type != ACK ) {
        perror("transfer()\n");
        exit(EXIT_FAILURE);
    }
}

void stopAll(int parent) {
    Message stopMessage = fillMessageHeader(STOP, 0);
    send_multicast(&parent, &stopMessage);
}
int main(int argc, char** argv) {
    int processCount, opt;
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
    } else if( processCount > ( argc - optind ) ) {
        fprintf(stderr, "U have to specify balances for all processes\n");
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
            balance = atoi(argv[optind + processLocalId - 1]);
            client(processLocalId);
            exit(EXIT_SUCCESS);
        }
    }
    closeUnusedPipes(0);
    local_id parentId = PARENT_ID;
    receiveAll(parentId, STARTED);
    bank_robbery(&parentId, processCount);
    stopAll(parentId);
    receiveAll(parentId, DONE);
    AllHistory history;

    for (processLocalId = 1; processLocalId < processCountGlobal; processLocalId++){
        Message receivedMessage;
        receive(&parentId, processLocalId, &receivedMessage);
        if (receivedMessage.s_header.s_type == BALANCE_HISTORY){
            BalanceHistory *bh = (BalanceHistory *)receivedMessage.s_payload;
            history.s_history_len++;
            history.s_history[processLocalId - 1] = *bh;
        }
    }
    print_history(&history);
    while(wait(NULL) > 0);
    free(pipes);
    close(eventsLog);
    close(pipesLog);
    return EXIT_SUCCESS;
}
