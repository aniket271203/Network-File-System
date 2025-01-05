#ifndef _NAMING_SERVER_H
#define _NAMING_SERVER_H

#include <arpa/inet.h>
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "headers.h"
#include "utils.h"

typedef struct {
  ss_info** storageServers;
  int storageServerCount;
  int maxStorageCount;
  bool* isDead;
} StorageServers;

void setSockAddrInfo(struct sockaddr_in *addr, int port, char *IP); // sets addr with given port and IP values
NM_SS_response_packet sendRequestToStorageIndex(NM_SS_request_packet operReq, int ssIndex); // sends the given request to the storage server with given index, and gets the response
NM_SS_response_packet sendRequestToStorageServer(NM_SS_request_packet operReq); // finds the correct storage server to send the request to, and sends it
void *connectStorageServers(void *); // thread for connecting storage servers to the naming server
NM_SS_response_packet copyData(cl_NM_request_packet req); // master function for copying of data. Implemented separate because of complexity.
void *receiveClientRequest(void *arg); // thread for processing client requests
void *connectClients(void *); // thread for accepting client connections

#define BUF_SIZE 4096
#define INIT_MAX_SS 15
#define QUEUED_CONNECTIONS_SS 100

#define SS_CON_INIT_COL BLU  // the initialisation colours of the connection
#define CL_CON_INIT_COL MAG
#define SS_CON_COMP_COL GRN  // the colour used on an established connection
#define CL_CON_COMP_COL YEL

#define CONNECT_TIMEOUT 3

#endif