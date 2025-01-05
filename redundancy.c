// https://cboard.cprogramming.com/networking-device-communication/50262-connect-timeout.html

#include "NamingServer.h"
#include "LRU_Cache.h"
#include "PathMaps.h"
#include "SS_Info.h"

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

    pthread_mutex_lock(&printLock);

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

    pthread_mutex_unlock(&printLock);

    while (true) {
        addr_size = sizeof(sendingAddr);
        sendingSock = accept(serverSock, (struct sockaddr *)&sendingAddr, &addr_size);
        if (sendingSock < 0) {
            perror("[-] accept");
            continue;
        }

        pthread_mutex_lock(&printLock);
        printf(SS_CON_COMP_COL "New Storage Server connected\n" CRESET);
        pthread_mutex_unlock(&printLock);

        char buf[MAX_BUF_LEN];
        memset(buf, '\0', sizeof(char) * MAX_BUF_LEN);
        // decide how much to wait. After that put this also within printlock.
        if (recv(sendingSock, buf, sizeof(buf), 0) < 0) {
            perror("[-] recv");
            continue;
        }
        ss_info *new_ss = destringifySSInfo(buf);

        pthread_mutex_lock(&ssLock);
        addStorageServer(SS, new_ss);
        int ssIndex = SS->storageServerCount - 1;
        // handle the copies shite here also
        pthread_mutex_lock(&printLock);
        printf("Storage Server Number: %d\n", ssIndex + 1);
        int numAccessiblePaths = SS->storageServers[ssIndex]->numAccessiblePaths;
        printf("%d accessible paths provided: \n", numAccessiblePaths);
        if (numAccessiblePaths > 0) {
            char **accessiblePaths = SS->storageServers[ssIndex]->accessiblePaths;
            bool pathAdded = false;
            for (int i = 0; i < numAccessiblePaths; i++) {
                int *copies = (int *)malloc(sizeof(int) * 2);
                assert(copies);
                copies[0] = -1;
                copies[1] = -1;

                if (!insertInTable(accessiblePaths[i], ssIndex, copies, PM)) {
                    printf(RED "Path %s cannot be added, it is already present\n" CRESET,
                            accessiblePaths[i]);
                } else {
                    printf(GRN "Path %s added to the set of accessible paths\n" CRESET,
                            accessiblePaths[i]);
                    if(SS->num) {
                        printf(BLU "Copying data over from this Storage Server to the backup Storage Servers.\n" CRESET);
                        cl_NM_request_packet req = (cl_NM_request_packet) {0};
                        // Create ".backup/" folder in SS0 and SS1, if it doesn't exist.


                        strcpy(req.sourcePath, accessiblePaths[i]), strcpy(req.destinationPath,)
                        copyData(req);
                    }
                    pathAdded = true;
                }
                free(copies);
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
        pthread_mutex_unlock(&printLock);
        pthread_mutex_unlock(&ssLock);

        char response[BUF_SIZE];
        memset(response, '\0', sizeof(char) * BUF_SIZE);
        strcpy(response, "Storage Server Connected");  // send an error message if no storage
        // servers are added
        // send(sendingSock, response, BUF_SIZE, 0);
    }

    return NULL;
}