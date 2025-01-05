#include "NamingServer.h"

#include <time.h>
#include <unistd.h>

#include "LRU_Cache.h"
#include "PathMaps.h"
#include "SS_Info.h"

pthread_mutex_t ssLock;
pthread_mutex_t printLock;

StorageServers *SS;
PathMap PM;
LRU_Cache Q;

// sets addr with given port and IP values
void setSockAddrInfo(struct sockaddr_in *addr, int port, char *IP) {
  memset(addr, '\0', sizeof(struct sockaddr_in));
  addr->sin_family = AF_INET;
  addr->sin_port = htons(port);
  addr->sin_addr.s_addr = inet_addr(IP);
}

// sends the given request to the storage server with given index, and gets the response
NM_SS_response_packet sendRequestToStorageIndex(NM_SS_request_packet operReq,
                                                int ssIndex) {
  NM_SS_response_packet resp;
  if (ssIndex < 0) {
    resp.statusCode =
        -99;  // request could not be sent to SS, some status code, should not happen
    return resp;
  }

  ss_info *storageServer = SS->storageServers[ssIndex];
  int port = storageServer->nmPort;
  char IP[IP_LEN];
  printf("Attempting to send request to storage server %d on port %d\n", ssIndex, port);
  memset(IP, '\0', sizeof(char) * IP_LEN);
  strcpy(IP, storageServer->ipAddress);

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("[-]Socket Error in sending request to SS");
    resp.statusCode =
        -99;  // request could not be sent to SS, some status code, should not happen
    close(sock);
    return resp;
  }

  struct sockaddr_in addr;
  setSockAddrInfo(&addr, port, IP);

  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("[-] Connect error in sending request to SS");
    resp.statusCode = -99;
    close(sock);
    return resp;
  }

  printf("Sending request ");
  printf(MAG "%s %s" CRESET, operReq.operation, operReq.sourcePath);
  printf(" to storage servers\n");
  printf("Sending in port %d\n", port);

  if (send(sock, &operReq, sizeof(NM_SS_request_packet), 0) < 0) {
    perror("[-] Send error in sending request to SS");
    resp.statusCode =
        -99;  // request could not be sent to SS, some status code, should not happen
    close(sock);
    return resp;
  }

  if (recv(sock, &resp, sizeof(NM_SS_response_packet), 0) < 0) {
    perror("[-] recv error in receiving response from SS");
    resp.statusCode =
        -99;  // request could not be sent to SS, some status code, should not happen
    close(sock);
    return resp;
  }

  printf("Received response of ");
  printf(MAG "%s %s" CRESET, operReq.operation, operReq.sourcePath);
  printf(" from storage server\n");

  close(sock);
  return resp;
}

// finds the correct storage server to send the request to, and sends it
NM_SS_response_packet sendRequestToStorageServer(NM_SS_request_packet operReq) {
  int ssIndex = getStorageServerIndex(operReq.sourcePath, PM);
  return sendRequestToStorageIndex(operReq, ssIndex);
}

void createBackupRequest(int backupIndex, char *accessiblePath, int ssIndex,
                         cl_NM_request_packet *backupRequest) {
  memset(backupRequest->operation, '\0', sizeof(char) * MAX_OPER_LEN);
  strncpy(backupRequest->operation, "copy", MAX_OPER_LEN);
  memset(backupRequest->sourcePath, '\0', sizeof(char) * PATH_MAX);
  strncpy(backupRequest->sourcePath, accessiblePath, PATH_MAX);
  memset(backupRequest->destinationPath, '\0', sizeof(char) * PATH_MAX);
  snprintf(backupRequest->destinationPath, PATH_MAX, "SS%d_%d/", backupIndex, ssIndex);
}

void *copyBackupThread(void *args) {
  cl_NM_request_packet *reqPtr = ((cl_NM_request_packet *)args);
  cl_NM_request_packet req = *reqPtr;
  NM_SS_response_packet resp = copyData(req);
  if (resp.statusCode == 0) {
    printf("Copy of %s succesfully created to path %s\n", req.sourcePath,
           req.destinationPath);
  } else {
    printf("Copy of %s to path %s unsuccesful, status code: %d\n", req.sourcePath,
           req.destinationPath, resp.statusCode);
  }
  free(reqPtr);
  return NULL;
}

void makeCreateRequest(cl_NM_request_packet *createReq, int ssIndex, int backupIndex) {
  memset(createReq->operation, '\0', sizeof(char) * MAX_OPER_LEN);
  strncpy(createReq->operation, "create", MAX_OPER_LEN);
  memset(createReq->sourcePath, '\0', sizeof(char) * MAX_OPER_LEN);
  snprintf(createReq->sourcePath, PATH_MAX, "SS%d_%d/", backupIndex, ssIndex);
}

void *createRequestThread(void *args) {
  cl_NM_request_packet *reqPtr = ((cl_NM_request_packet *)args);
  cl_NM_request_packet req = *reqPtr;

  printf("Sending Request %s %s\n", req.operation, req.sourcePath);

  if (findInTable(req.sourcePath, PM)) {
    printf("Directory %s is already in storage servers, this is not allowed\n",
           req.sourcePath);
    exit(1);
  } else {
    NM_SS_request_packet operReq;
    strncpy(operReq.operation, req.operation, MAX_OPER_LEN);
    strncpy(operReq.sourcePath, req.sourcePath, PATH_MAX);
    NM_SS_response_packet ssResp =
        sendRequestToStorageIndex(operReq, req.sourcePath[2] - '0');

    if (ssResp.statusCode == 0) {
      printf("Succesfully created %s\n", req.sourcePath);
      insertInTable(operReq.sourcePath, req.sourcePath[2] - '0', PM);
    } else {
      printf("Could not create succesfully, status %d\n", ssResp.statusCode);
    }
  }

  return NULL;
}

bool attemptToConnectToSS(int ssIndex) {
  ss_info *srcSS = SS->storageServers[ssIndex];
  int port = srcSS->nmPort;
  char IP[IP_LEN];
  memset(IP, '\0', sizeof(char) * IP_LEN);
  strcpy(IP, srcSS->ipAddress);

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    return false;
  }

  struct sockaddr_in addr;
  setSockAddrInfo(&addr, port, IP);

  int flags = fcntl(sock, F_GETFL, 0);
  flags |= O_NONBLOCK;
  fcntl(sock, F_SETFL, flags);
  fd_set fdSet;
  struct timeval tv;
  tv.tv_sec = CONNECT_TIMEOUT;
  tv.tv_usec = 0;
  FD_ZERO(&fdSet);

  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    FD_SET(sock, &fdSet);
    if (select(sock + 1, NULL, &fdSet, NULL, &tv) == 1) {
      int so_error;
      socklen_t len = sizeof so_error;
      getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
      if (so_error != 0) {
        perror("[-] Connect error in sending request to SS, SS dead");
        printf("SS index %d dead\n", ssIndex);

        SS->isDead[ssIndex] = true;
        return false;
      }
    }
  }

  flags &= ~O_NONBLOCK;
  fcntl(sock, F_SETFL, flags);

  return true;
}

// thread for connecting storage servers to the naming server
void *connectStorageServers(void *) {
  // https://github.com/nikhilroxtomar/TCP-Client-Server-Implementation-in-C/blob/main/server.c
  char *ip = "127.0.0.1";  // for now assuming same system comms, localhost
  int listeningPort = NM_SS_PORT;

  int serverSock, sendingSock;
  struct sockaddr_in serverAddr, sendingAddr;
  socklen_t addr_size;

  if ((serverSock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("Error creating socket for Naming Server");
    exit(1);  // change to an error packet
  }

  printf(SS_CON_INIT_COL
         "Naming Server Socket created for Storage Server connection\n" CRESET);
  setSockAddrInfo(&serverAddr, listeningPort, ip);

  if ((bind(serverSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr))) < 0) {
    printf(RED "Cound not connect to port %d\n" CRESET, listeningPort);
    perror("Error binding to port for Storage Server connection");
    exit(1);  // change to an error packet, remember that this is in a lock
  }
  printf(SS_CON_INIT_COL "Naming Server bound to port %d \n" CRESET, listeningPort);

  if (listen(serverSock, QUEUED_CONNECTIONS_SS) < 0) {
    perror("listen");
    exit(1);
  }
  printf(SS_CON_INIT_COL
         "Naming Server listening for Storage Server Connections...\n" CRESET);
  printf("----------\n");

  while (true) {
    addr_size = sizeof(sendingAddr);
    sendingSock = accept(serverSock, (struct sockaddr *)&sendingAddr, &addr_size);
    if (sendingSock < 0) {
      perror("[-] accept");
      continue;
    }

    printf(SS_CON_COMP_COL "New Storage Server connected\n" CRESET);

    char buf[MAX_BUF_LEN];
    memset(buf, '\0', sizeof(char) * MAX_BUF_LEN);
    if (recv(sendingSock, buf, sizeof(buf), 0) < 0) {
      perror("[-] recv");
      continue;
    }

    ss_info *new_ss = destringifySSInfo(buf);

    pthread_mutex_lock(&ssLock);
    pthread_mutex_lock(&printLock);
    int numAccessiblePaths = new_ss->numAccessiblePaths;
    int count = 0;

    added_paths ret;
    bool nonBackupReject = !new_ss->isBackup && SS->storageServerCount < 2;
    bool backupReject = new_ss->isBackup && SS->storageServerCount >= 2;
    if (backupReject || nonBackupReject) {
      // case 1 -> we already have first two servers as backup
      if (backupReject) {
        printf("SS Rejected, enough backup servers already\n");
        ret.backupSuccess = false;
        ret.addedIndices = NULL;
      }
      // case 2 -> we want to have first two servers as backup
      else {
        printf("SS rejected, not enough backup servers\n");
        ret.backupSuccess = false;
        ret.addedIndices = (bool *)malloc(sizeof(bool) * numAccessiblePaths);
        memset(ret.addedIndices, (bool)false, sizeof(bool) * numAccessiblePaths);
      }

      goto respondToSS;
    }
    addStorageServer(SS, new_ss);
    int ssIndex = SS->storageServerCount - 1;
    printf("Storage Server Number: %d\n", ssIndex + 1);
    printf("%d accessible paths provided: \n", numAccessiblePaths);

    // case 3 -> it wants to be a backup server, and is obviously allowed to
    printf("Ok you are backup\n");
    printf("Port: %d\n", new_ss->nmPort);
    if (new_ss->isBackup) {
      // ok, good
      ret.backupSuccess = true;
      ret.addedIndices = NULL;
      goto respondToSS;
    }

    // case 4 -> wants to be a regular storage server, and is allowed to
    printf("Ok you are SS\n");
    ret.backupSuccess = true;
    ret.addedIndices = NULL;

    cl_NM_request_packet **copyRequests1 = (cl_NM_request_packet **)malloc(
        sizeof(cl_NM_request_packet *) * numAccessiblePaths);
    cl_NM_request_packet **copyRequests2 = (cl_NM_request_packet **)malloc(
        sizeof(cl_NM_request_packet *) * numAccessiblePaths);

    if (numAccessiblePaths > 0) {  // this should be true, NM should ensure
      cl_NM_request_packet createReq1, createReq2;
      makeCreateRequest(&createReq1, ssIndex, 0);
      makeCreateRequest(&createReq2, ssIndex, 1);

      pthread_t createT1, createT2;
      pthread_create(&createT1, NULL, createRequestThread, &createReq1);
      pthread_create(&createT2, NULL, createRequestThread, &createReq2);

      pthread_join(createT1, NULL);
      pthread_join(createT2, NULL);

      char **accessiblePaths = SS->storageServers[ssIndex]->accessiblePaths;
      ret.addedIndices = (bool *)malloc(sizeof(bool) * numAccessiblePaths);
      bool pathAdded = false;
      for (int i = 0; i < numAccessiblePaths; i++) {
        if (!insertInTable(accessiblePaths[i], ssIndex, PM)) {
          printf(RED "Path %s cannot be added, it is already present\n" CRESET,
                 accessiblePaths[i]);
          ret.addedIndices[i] = false;
        } else {
          printf(GRN "Path %s added to the set of accessible paths\n" CRESET,
                 accessiblePaths[i]);
          pathAdded = true;
          ret.addedIndices[i] = true;

          cl_NM_request_packet *req1 =
              (cl_NM_request_packet *)malloc(sizeof(cl_NM_request_packet));
          createBackupRequest(0, accessiblePaths[i], ssIndex, req1);
          cl_NM_request_packet *req2 =
              (cl_NM_request_packet *)malloc(sizeof(cl_NM_request_packet));
          createBackupRequest(1, accessiblePaths[i], ssIndex, req2);

          copyRequests1[count] = req1;
          copyRequests2[count++] = req2;
        }
        // printHT(PM);
      }
      if (!pathAdded && SS->storageServerCount > 0) {
        SS->storageServerCount -= 1;
        freeSSInfo(SS, SS->storageServerCount);
        printf(RED
               "Since no accessible paths were added, this storage server entry is "
               "removed\n" CRESET);
      }
    }
    printf("----------\n");
  respondToSS:
    pthread_mutex_unlock(&printLock);
    pthread_mutex_unlock(&ssLock);
    char *retBuf = stringifyAddedPaths(&ret, numAccessiblePaths);
    // printf("retBuf: %s", retBuf);
    if (send(sendingSock, retBuf, strlen(retBuf), 0) < 0) {
      perror("Error sending response to storage server\n");
      continue;
    }
    close(sendingSock);

    for (int i = 0; i < count; i++) {
      pthread_t c1, c2;
      sleep(1);
      pthread_create(&c1, NULL, copyBackupThread, copyRequests1[i]);
      pthread_join(c1, NULL);
      sleep(1);
      pthread_create(&c2, NULL, copyBackupThread, copyRequests2[i]);
      pthread_join(c2, NULL);
    }
  }
  return NULL;
}

// master function for copying of data. Implemented separate because of complexity.
/*
1. Send copysrc to source path
2. Receive number of bits
3. Receive the buffer itself
4. Receive status code
5. Send copydest to destination path
6. Send number of bits
7. Send the buffer itself
8. Receive response from the SS
Finally, (not in this function), send the return of this function back to the client
*/
NM_SS_response_packet copyData(cl_NM_request_packet req) {
  int srcIndex = getStorageServerIndex(req.sourcePath, PM);
  printf("%s %d\n", req.sourcePath, srcIndex);

  NM_SS_response_packet resp;

  ss_info *srcSS = SS->storageServers[srcIndex];
  int port = srcSS->nmPort;
  char IP[IP_LEN];
  memset(IP, '\0', sizeof(char) * IP_LEN);
  strcpy(IP, srcSS->ipAddress);

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("[-]Socket Error in sending request to SS");
    resp.statusCode =
        -99;  // request could not be sent to SS, some status code, should not happen
    return resp;
  }

  struct sockaddr_in addr;
  setSockAddrInfo(&addr, port, IP);

  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("[-] Connect error in sending request to SS");
    printf("Cannot connect to port %d\n", port);
    resp.statusCode =
        -99;  // request could not be sent to SS, some status code, should not happen
    return resp;
  }

  strncpy(req.operation, "copysrc", MAX_OPER_LEN);
  printf("Sending request ");
  printf(MAG "%s %s" CRESET, req.operation, req.sourcePath);
  printf(" to storage servers\n");

  // sending copysrc to source
  if (send(sock, &req, sizeof(NM_SS_request_packet), 0) < 0) {
    perror("[-] Send error in sending request to SS");
    resp.statusCode =
        -99;  // request could not be sent to SS, some status code, should not happen
    return resp;
  }

  char *buf = (char *)malloc(sizeof(char) * MAX_BUF_LEN);
  memset(buf, '\0', sizeof(char) * MAX_BUF_LEN);
  // receiving number of bytes
  if (recv(sock, buf, sizeof(char) * MAX_BUF_LEN, 0) < 0) {
    perror("[-] recv error in receiving response from SS");
    resp.statusCode =
        -99;  // request could not be sent to SS, some status code, should not happen
    return resp;
  }

  int numBytes = (int)strtol(buf, NULL, 10);
  printf("Number of bytes to copy: %d\n", numBytes);

  char *copyData = (char *)malloc(sizeof(char) * (numBytes + 1));
  memset(copyData, '\0', sizeof(char) * (numBytes + 1));
  // receiving data to copy
  if (recv(sock, copyData, sizeof(char) * numBytes, 0) < 0) {
    perror("[-] recv error in receiving response from SS");
    resp.statusCode =
        -99;  // request could not be sent to SS, some status code, should not happen
    return resp;
  }
  // printf("Received data: ");
  // for (int i = 0; i < numBytes; i++) printf("%c", copyData[i]);
  // printf("\n");
  printf("Copied data received\n");

  NM_SS_response_packet status;
  if (recv(sock, &status, sizeof(NM_SS_response_packet), 0) < 0) {
    perror("[-] recv error in receiving response from SS");
    resp.statusCode =
        -99;  // request could not be sent to SS, some status code, should not happen
    return resp;
  }

  printf("Status received from source: %d\n", status.statusCode);
  if (status.statusCode != 0) {
    printf("[-] Failed status, exiting\n");
    resp.statusCode = status.statusCode;  // request could not be sent to SS, some
                                          // status code, should not happen
    return resp;
  }

  NM_SS_request_packet destReq;
  memset(destReq.operation, '\0', sizeof(char) * MAX_OPER_LEN);
  memset(destReq.sourcePath, '\0', sizeof(char) * PATH_MAX);
  strncpy(destReq.operation, "copydest", MAX_OPER_LEN);
  strncpy(destReq.sourcePath, req.destinationPath, PATH_MAX);

  printf("Sending request ");
  printf(MAG "%s %s" CRESET, destReq.operation, destReq.sourcePath);
  printf(" to storage servers\n");

  int destIndex = getStorageServerIndex(req.destinationPath, PM);
  if (destIndex < 0) {
    resp.statusCode = -2;
    return resp;
  }

  ss_info *destSS = SS->storageServers[destIndex];
  port = destSS->nmPort;
  char IP_dest[IP_LEN];
  memset(IP_dest, '\0', sizeof(char) * IP_LEN);
  strcpy(IP_dest, destSS->ipAddress);

  int destSock = socket(AF_INET, SOCK_STREAM, 0);
  if (destSock < 0) {
    perror("[-]Socket Error in sending request to SS");
    resp.statusCode =
        -99;  // request could not be sent to SS, some status code, should not happen
    return resp;
  }

  setSockAddrInfo(&addr, port, IP_dest);

  if (connect(destSock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("[-] Connect error in sending request to SS");
    resp.statusCode =
        -99;  // request could not be sent to SS, some status code, should not happen
    return resp;
  }

  // send the request
  if (send(destSock, &destReq, sizeof(NM_SS_request_packet), 0) < 0) {
    perror("[-] Send error in sending request to SS");
    resp.statusCode =
        -99;  // request could not be sent to SS, some status code, should not happen
    return resp;
  }

  // send he number of bytes
  printf("Sending number of bytes %d to destination %s\n", numBytes, destReq.sourcePath);
  if (send(destSock, buf, sizeof(char) * MAX_BUF_LEN, 0) < 0) {
    perror("[-] Send error in sending request to SS");
    resp.statusCode =
        -99;  // request could not be sent to SS, some status code, should not happen
    return resp;
  }

  // send the data itself
  // printf("Received data: ");
  // for (int i = 0; i < numBytes; i++) printf("%c", copyData[i]);
  // printf("\n");
  printf("Sending copied data to destination %s\n", destReq.sourcePath);
  if (send(destSock, copyData, sizeof(char) * numBytes, 0) < 0) {
    perror("[-] Send error in sending request to SS");
    resp.statusCode =
        -99;  // request could not be sent to SS, some status code, should not happen
    return resp;
  }

  // receive status code
  if (recv(sock, &resp, sizeof(NM_SS_response_packet), 0) < 0) {
    perror("[-] recv error in receiving response from SS");
    resp.statusCode =
        -99;  // request could not be sent to SS, some status code, should not happen
    return resp;
  }
  printf("Received status %d from destination %s\n", resp.statusCode, destReq.sourcePath);

  free(buf);

  close(destSock);

  return resp;
}

// thread for processing client requests
void *receiveClientRequest(void *arg) {
  cl_info *client = (cl_info *)arg;
  int clientPort = client->clientPort;
  char *ipAddr = client->ipAddress;

  int sock;
  struct sockaddr_in addr;
  cl_NM_request_packet req = (cl_NM_request_packet){0};

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("[-]Socket Error in connection to client");
    return NULL;
  }

  setSockAddrInfo(&addr, clientPort, ipAddr);

  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("Error connecting to client port");
    close(sock);
    return NULL;
  }

  printf(CL_CON_COMP_COL "Connected to client at port %d\n" CRESET, clientPort);
  printf("Ready to receive requests\n");

  if (recv(sock, &req, sizeof(cl_NM_request_packet), 0) < 0) {
    perror("Error receiving client request");
    close(sock);
    return NULL;
  }

  printf("Received request\n");
  printf("operation: ");
  printf(MAG "%s" CRESET, req.operation);
  printf("\nsource: ");
  printf(MAG "%s" CRESET, req.sourcePath);
  printf("\ndest: ");
  printf(MAG "%s" CRESET, req.destinationPath);
  printf("\n");

  cl_NM_response_packet resp;
  if (strcmp(req.operation, "create") != 0 && !findInTable(req.sourcePath, PM)) {
    resp.statusCode = -1;
    printf("Invalid source path %s, sending response to client\n", req.sourcePath);
    if (send(sock, &resp, sizeof(cl_NM_response_packet), 0) < 0) {
      perror("Error sending response to client");
      close(sock);
      return NULL;
    }
  } else if (strcmp(req.operation, "copy") == 0 &&
             !findInTable(req.destinationPath, PM)) {
    printf("Invalid destination path %s, sending response to client\n",
           req.destinationPath);
    resp.statusCode = -2;
    if (send(sock, &resp, sizeof(cl_NM_response_packet), 0) < 0) {
      perror("Error sending response to client");
      close(sock);
      return NULL;
    }
  } else {
    resp.statusCode = 0;
    // if (strcmp(req.operation, "read") != 0 && strcmp(req.operation, "getinfo") != 0) {
    //   int ssIndex = getStorageServerIndex(req.sourcePath, PM);
    //   if (ssIndex < 2) {
    //     resp.statusCode = -995;
    //     if (send(sock, &resp, sizeof(cl_NM_response_packet), 0) < 0) {
    //       perror("Error sending response to client");
    //       close(sock);
    //       return NULL;
    //     }

    //     return NULL;
    //   }
    // }

    if ((strcmp(req.operation, "read") == 0) || (strcmp(req.operation, "write") == 0) ||
        (strcmp(req.operation, "append") == 0) ||
        (strcmp(req.operation, "getinfo") == 0)) {
      int storageServerIndex = getStorageServerIndex(req.sourcePath, PM);

      if (strcmp(req.operation, "read") == 0 || strcmp(req.operation, "getinfo") == 0) {
        cacheNode *cache = findNode(Q, req);
        if (cache) {
          printf(GRN "Cache Hit! Sending response\n" CRESET);
          cl_SS_response_packet resp = cache->response;
          cl_NM_response_packet respToClient;
          respToClient.statusCode = -999;
          memset(respToClient.message, '\0', sizeof(char) * MSG_LEN);
          strncpy(respToClient.message, resp.response, sizeof(char) * MSG_LEN);

          if (send(sock, &respToClient, sizeof(cl_NM_response_packet), 0) < 0) {
            perror("Error sending response to client");
            close(sock);
            return NULL;
          }
          goto endConnection;
        }
      }

      pthread_mutex_lock(&ssLock);
      if (storageServerIndex < 0 ||
          storageServerIndex > SS->storageServerCount) {  // should never happen ideally
        resp.statusCode = -1;
        if (send(sock, &resp, sizeof(cl_NM_response_packet), 0) < 0) {
          perror("Error sending response to client");
          close(sock);
          return NULL;
        }
      } else {
        ss_info *storageServer = SS->storageServers[storageServerIndex];
        if (!attemptToConnectToSS(storageServerIndex)) {
          if (strcmp("read", req.operation) == 0 ||
              strcmp("getinfo", req.operation) == 0) {
            resp.statusCode = -99;
            printf("Server is dead\n");
            return NULL;
          }
          if (attemptToConnectToSS(0)) {
            printf("Going to backup 0");
            resp.statusCode = -994;
            memset(resp.message, '\0', sizeof(char) * MSG_LEN);
            snprintf(resp.message, MSG_LEN, "SS%d_%d/", 0, storageServerIndex);
            storageServer = SS->storageServers[0];
          } else if (attemptToConnectToSS(1)) {
            printf("Going to backup 1");
            resp.statusCode = -993;
            snprintf(resp.message, MSG_LEN, "SS%d_%d/", 1, storageServerIndex);
            storageServer = SS->storageServers[1];
          } else {
            printf("All servers and backups are dead\n");
            resp.statusCode = -995;
          }
        } else {
          // restore data of backups here

          // if (SS->isDead[storageServerIndex]) {
          //   SS->isDead[storageServerIndex] = false;
          //   printf(
          //       "Server %d has come back alive, attempting to replenish data in
          //       copies\n", storageServerIndex);
          //   int numAccess = SS->storageServers[storageServerIndex]->numAccessiblePaths;
          //   for (int i = 0; i < numAccess; i++) {
          //     cl_NM_request_packet reqCopy;
          //     char *path = SS->storageServers[storageServerIndex]->accessiblePaths[i];
          //     memset(reqCopy.operation, '\0', MAX_OPER_LEN);
          //     memset(reqCopy.sourcePath, '\0', PATH_MAX);
          //     memset(reqCopy.destinationPath, '\0', PATH_MAX);
          //     strncpy(reqCopy.operation, "copy", MAX_OPER_LEN);
          //     strncpy(reqCopy.sourcePath, path, PATH_MAX);
          //     snprintf(reqCopy.destinationPath, "SS0_%d/%s", storageServerIndex, path);

          //     NM_SS_response_packet resp = copyData(reqCopy);
          //     printf("Status of copy from %d to copy %d is %d\n", storageServerIndex,
          //     0,
          //            resp.statusCode);

          //     memset(reqCopy.operation, '\0', MAX_OPER_LEN);
          //     memset(reqCopy.sourcePath, '\0', PATH_MAX);
          //     memset(reqCopy.destinationPath, '\0', PATH_MAX);
          //     strncpy(reqCopy.operation, "copy", MAX_OPER_LEN);
          //     strncpy(reqCopy.sourcePath, path, PATH_MAX);
          //     snprintf(reqCopy.destinationPath, "SS1_%d/%s", storageServerIndex, path);
          //     resp = copyData(reqCopy);
          //     printf("Status of copy of from %d to copy %d is %d\n",
          //     storageServerIndex,
          //            1, resp.statusCode);
          //   }
          // }
        }

        strcpy(resp.SS_IP, storageServer->ipAddress);  // make sure ss.ip is within 16
        resp.SS_Port = storageServer->clientPort;

        printf("Sending port %d and IP %s to client, to communicate with SS\n",
               resp.SS_Port, resp.SS_IP);
        if (send(sock, &resp, sizeof(cl_NM_response_packet), 0) < 0) {
          perror("Error sending response to client");
          close(sock);
          return NULL;
        }

        printf("Awaiting response from client\n");
        cl_SS_response_packet resp = {0};
        if (recv(sock, &resp, sizeof(cl_SS_response_packet), 0) < 0) {
          perror("Error receiving client info");
          close(sock);
          return NULL;
        }

        if (strcmp(req.operation, "read") == 0 || strcmp(req.operation, "getinfo") == 0) {
          printf("Caching %s on %s\n", req.operation, req.sourcePath);
          referencePage(Q, req, resp);
        } else if (strcmp(req.operation, "write") == 0 ||
                   strcmp(req.operation, "append") == 0) {
          if (resp.statusCode == 0) {
            printf("Removing all instances of %s from cache, owing to a succesful %s\n",
                   req.sourcePath, req.operation);
            removePath(Q, req.sourcePath);
          }
        }
      }
      pthread_mutex_unlock(&ssLock);
    } else if (strcmp(req.operation, "delete") == 0) {
      NM_SS_request_packet operReq;
      strncpy(operReq.operation, req.operation, MAX_OPER_LEN);
      strncpy(operReq.sourcePath, req.sourcePath, PATH_MAX);
      NM_SS_response_packet ssResp = sendRequestToStorageServer(operReq);

      resp.statusCode = ssResp.statusCode;  // same status code as from server response
      strcpy(resp.SS_IP, "");
      resp.SS_Port = -1;

      // assuming 0 -> succesful delete / operation
      if (strcmp(req.operation, "delete") == 0 && resp.statusCode == 0) {
        deleteInTable(req.sourcePath, PM);
        printf(GRN "Deleted %s from accessible paths, and from cache instances\n" CRESET,
               req.sourcePath);
        removePath(Q, req.sourcePath);
      }

      printf("Sending %s operation result: status code %d, to client\n", req.operation,
             resp.statusCode);

      if (send(sock, &resp, sizeof(cl_NM_response_packet), 0) < 0) {
        perror("Error sending response to client");
        close(sock);
        return NULL;
      }
    } else if (strcmp(req.operation, "create") == 0) {
      if (SS->storageServerCount == 0) {
        resp.statusCode = -998;  // no storage servers created
      } else if (findInTable(req.sourcePath, PM)) {
        resp.statusCode = -997;  // path is already in NM
      } else {
        NM_SS_request_packet operReq;
        strncpy(operReq.operation, req.operation, MAX_OPER_LEN);
        strncpy(operReq.sourcePath, req.sourcePath, PATH_MAX);
        NM_SS_response_packet ssResp = sendRequestToStorageIndex(operReq, 0);

        resp.statusCode = ssResp.statusCode;
        if (resp.statusCode == 0) {
          insertInTable(operReq.sourcePath, 0, PM);
        }

        printf("Sending %s operation result: status code %d, to client\n", req.operation,
               resp.statusCode);

        if (send(sock, &resp, sizeof(cl_NM_response_packet), 0) < 0) {
          perror("Error sending response to client");
          close(sock);
          return NULL;
        }
      }

    } else {
      // assumed to be "copy"
      NM_SS_response_packet ssResp = copyData(req);
      printf("Sending status code %d after attempted copy, to client\n",
             ssResp.statusCode);
      resp.statusCode = ssResp.statusCode;
      if (send(sock, &resp, sizeof(cl_NM_response_packet), 0) < 0) {
        perror("Error sending response to client");
        close(sock);
        return NULL;
      }
    }
  }
endConnection:
  close(sock);
  return NULL;
}

// thread for accepting client connections
void *connectClients(void *) {
  // https://github.com/nikhilroxtomar/TCP-Client-Server-Implementation-in-C/blob/main/server.c
  char *ip = "127.0.0.1";  // for now assuming same system comms, localhost
  int listeningPort = NM_CLIENT_PORT;

  int serverSock, sendingSock;
  struct sockaddr_in serverAddr, sendingAddr;
  socklen_t addr_size;
  cl_info new_cl = (cl_info){0};

  if ((serverSock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("Error creating socket for Naming Server");
    exit(1);  // change to an error packet
  }

  printf(CL_CON_INIT_COL "Naming Server Socket created for Client Connection\n" CRESET);
  setSockAddrInfo(&serverAddr, listeningPort, ip);

  if ((bind(serverSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr))) < 0) {
    printf(RED "Could not connect to port %d\n" CRESET, listeningPort);
    perror("Error binding to port for Naming Server\n");
    exit(1);  // change to an error packet, remember that this is in a lock
  }
  printf(CL_CON_INIT_COL "Naming Server bound to port %d\n" CRESET, listeningPort);

  if (listen(serverSock, QUEUED_CONNECTIONS_SS) < 0) {
    perror("Error listening for client connections");
    exit(1);
  }
  printf(CL_CON_INIT_COL "Naming Server listening for Client Connections...\n" CRESET);
  printf("----------\n");

  while (true) {
    addr_size = sizeof(sendingAddr);
    if ((sendingSock = accept(serverSock, (struct sockaddr *)&sendingAddr, &addr_size)) <
        0) {
      perror("Error accepting client connection");
      continue;
    }

    printf(CL_CON_COMP_COL "New Client connected\n" CRESET);

    new_cl = (cl_info){0};  // replace with packet from child
    if (recv(sendingSock, &new_cl, sizeof(cl_info), 0) < 0) {
      perror("Error receiving client info");
      close(sendingSock);
      continue;
    }

    pthread_t requestThread;
    if (pthread_create(&requestThread, NULL, receiveClientRequest, &new_cl) != 0) {
      perror("pthread_create");
      exit(1);
    }

    close(sendingSock);
  }

  return NULL;
}

int main() {
  SS = initStorageServer(INIT_MAX_SS);
  PM = createPathMap(HT_SIZE);
  Q = createLRUCache();

  if (pthread_mutex_init(&ssLock, NULL) != 0) {
    perror("pthread_mutex_lock");
    exit(1);
  }
  if (pthread_mutex_init(&printLock, NULL) != 0) {
    perror("pthread_mutex_lock");
    exit(1);
  }

  pthread_t storageConnectionThread, clientConnectionThread;
  if (pthread_create(&storageConnectionThread, NULL, connectStorageServers, NULL) != 0) {
    perror("pthread_create");
    exit(1);
  }
  if (pthread_create(&clientConnectionThread, NULL, connectClients, NULL) != 0) {
    perror("pthread_create");
    exit(1);
  }

  pthread_join(storageConnectionThread, NULL);
  pthread_join(clientConnectionThread, NULL);

  freeStorageServer(SS);
  return 0;
}
