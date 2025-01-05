#include "client.h"
#include "utils.h"

char NM_IP[] = "127.0.0.1";
char delimiters[] = " ";

int establishConnectionWithServer(char IP[], int port, struct sockaddr_in* clientInfo, struct sockaddr_in* newServerInfo){
    // Open connection with NM on pre-defined IP and port (from headers.h)
    struct sockaddr_in* serverInfo = (struct sockaddr_in*) malloc(sizeof(struct sockaddr_in));
    int NMSockFD = -1;
    int count = NUM_RETRIES;
    while(count >= 0 && NMSockFD < 0){
//        printf("Attempting to connect to the server at IP: %s and port: %d\n", IP, port);
        NMSockFD = connectViaTCP(IP, port, serverInfo);
        if(NMSockFD < 0) fprintf(stderr, "Failed to connect to the server. Reattempting connection in %d second(s).\n", REATTEMPT_WAIT);
        sleep(REATTEMPT_WAIT);
        count--;
    }
    if(count < 0) {
        fprintf(stderr, BRED "Failed to connect to server.\n" CRESET);
        return -2;
    }
//    printf("Connection established with server. Sending port details.\n");

    // Open a socket on a free port: Reference -> https://stackoverflow.com/questions/7531591/getting-unused-port-number-in-c-dynamically-when-running-server-process
    int clientSockFD, serverSockFD;
    socklen_t clientSize = sizeof(*clientInfo);

    // Set details of server.
    newServerInfo->sin_family = AF_INET; // IPv4 address family.
    newServerInfo->sin_port = htons(0);
    newServerInfo->sin_addr.s_addr = inet_addr("127.0.0.1"); // Convert internet host address from numbers and dots notation to network byte order.

    // Create a TCP server socket.
    if((clientSockFD = socket(AF_INET, SOCK_STREAM, 0)) == -1){ // domain, type, protocol. Domain is IPv4 type, SOCK_STREAM is for reliable 2-way connection-oriented communication (TCP), and protocol 0 implies to use whatever protocol is available for this type of domain (IPv4).
        perror(BRED "ERROR: Failed to open a server socket: " CRESET);
        close(NMSockFD);
        return -1;
    }

    // Bind to the socket.
    if(bind(clientSockFD, (struct sockaddr*) newServerInfo, sizeof(*newServerInfo)) < 0){
        perror(BRED "ERROR: Failed to bind to server socket: " CRESET);
        close(NMSockFD), close(clientSockFD);
        return -1;
    }

    // Get port and other details.
    socklen_t serverSize = sizeof(*newServerInfo);
    if(getsockname(clientSockFD, (struct sockaddr *) newServerInfo, &serverSize) < 0){
        perror(BRED "ERROR: Error occurred while retrieving port details of client socket: " CRESET);
        close(NMSockFD), close(clientSockFD);
        return -1;
    }

    // Listen to the socket for incoming connections.
    if(listen(clientSockFD, 1) == -1) {
        perror(BRED "ERROR: Error occurred while attempting to listen to server socket: " CRESET);
        close(NMSockFD), close(clientSockFD);
        return -1;
    }

    cl_info sendPortInfo = {0};
    strcpy(sendPortInfo.ipAddress, "127.0.0.1");
    sendPortInfo.clientPort = ntohs(newServerInfo->sin_port);
    // Send the details of this port to the socket connected to earlier.
    if(send(NMSockFD, &sendPortInfo, sizeof(sendPortInfo), 0) == -1){ // Send it to the server as well to stop the server.
        perror(BRED "ERROR: Failed to send this message to the server: " CRESET);
        close(NMSockFD), close(clientSockFD);
        return -1;
    }

    // Now, accept the connection request from client, and establish a connection.
    printf(BYEL "Making the server connect back to us at IP: %s and port: %d\n" CRESET, IP, ntohs(newServerInfo->sin_port));
    if((serverSockFD = accept(clientSockFD, (struct sockaddr*) clientInfo, &clientSize)) == -1){
        perror(BRED "ERROR: Error occurred while attempting to accept connection to client: " CRESET);
        close(NMSockFD), close(clientSockFD), close(serverSockFD);
        return -1;
    }

    free(serverInfo);
    return serverSockFD;
}

int connectToServer(char IP[], int port, struct sockaddr_in* serverInfo){
    struct sockaddr_in clientInfo;
    int serverSocketFD = -1;
    while (serverSocketFD < 0) {
//        printf("Establishing connection with server.\n");
        serverSocketFD = establishConnectionWithServer(IP, port, &clientInfo, serverInfo);
        if (serverSocketFD < 0) {
            if(serverSocketFD != -2) fprintf(stderr, BRED "Failed to connect to the server.\n" CRESET);
            return -1;
        }
    }
    printf(BGRN "Connection established with server.\n" CRESET);
    return serverSocketFD;
}

// Returns NMSocketFD if successfully sent request and received a response.
// Returns -1 for other errors.
int forwardRequestToNM(cl_NM_request_packet* reqToNM, cl_NM_response_packet* responseFromNM, char clientRequest[]){
    // Connect to NM first. This will happen only for a valid request.
    struct sockaddr_in NMserverInfo;
    printf(BYEL "Connecting to the Naming Server at IP: %s and port: %d\n" CRESET, NM_IP, NM_CLIENT_PORT);
    int NMSocketFD = connectToServer(NM_IP, NM_CLIENT_PORT, &NMserverInfo);
    if(NMSocketFD < 0) return -1;

    const char *args = strtok(clientRequest, delimiters);
    strcpy(reqToNM->operation, args);
    args = strtok(NULL, delimiters);
    strcpy(reqToNM->sourcePath, args);
    args = strtok(NULL, delimiters);
    if (args) strcpy(reqToNM->destinationPath, args);

    // Now, send the request to the NM.
    if(send(NMSocketFD, reqToNM, sizeof(cl_NM_request_packet), 0) == -1){
        perror(BRED "ERROR: Failed to send this request to the Naming Server: " CRESET);
        close(NMSocketFD);
        return -1;
    }

    // Now, wait for a response from the NM.
    if(recv(NMSocketFD, responseFromNM, sizeof(cl_NM_response_packet), 0) == -1){
        perror(BRED "ERROR: Failed to receive a response from the Naming Server: " CRESET);
        close(NMSocketFD);
        return -1;
    }

    // Handle statusCodes here.
    switch(responseFromNM->statusCode){
        case -1:
            printf(BRED "Invalid source path specified!\n"
                        "This source path doesn't exist on any Storage Server that is currently connected to the Naming Server.\n" CRESET);
            break;
        case -2:
            printf(BRED "Invalid destination path specified!\n"
                   "This destination path doesn't exist on any Storage Server that is currently connected to the Naming Server.\n" CRESET);
            break;
        default:
//           printf(BGRN "BOTH PATHS ARE VALID.\n" CRESET);
            break;
    }

    // Return status code for any sort of failure. If successful, return NMSocketFD.
    return responseFromNM->statusCode < 0 ? responseFromNM->statusCode : NMSocketFD;
}

int forwardRequestToSS(char clientRequest[], struct sockaddr_in* SSInfo, cl_NM_response_packet* responseFromNM, cl_SS_response_packet* responseFromSS, char message[]){
    // Connect to SS and send the initial packet.
    printf(BYEL "Connecting to the Storage Server at IP: %s and port: %d\n" CRESET, responseFromNM->SS_IP, responseFromNM->SS_Port);
    int SSSockFD = connectToServer(responseFromNM->SS_IP, responseFromNM->SS_Port, SSInfo);
    if(SSSockFD < 0) return -1;

    cl_SS_request_packet reqToSS = {0};
    char *args = strtok(clientRequest, delimiters);
    strcpy(reqToSS.operation, args);
    args = strtok(NULL, delimiters);
    strcpy(reqToSS.path, args);
    strcpy(reqToSS.message, message);

    // Now, send the request to the SS.
    if(send(SSSockFD, &reqToSS, sizeof(cl_SS_request_packet), 0) == -1){
        perror(BRED "ERROR: Failed to send this request to the Storage Server: " CRESET);
        close(SSSockFD);
        return -1;
    }

    // Now, wait for a response from the SS.
    if(recv(SSSockFD, responseFromSS, sizeof(cl_SS_response_packet), 0) == -1){
        perror(BRED "ERROR: Failed to receive a response from the Storage Server: " CRESET);
        close(SSSockFD);
        return -1;
    }

    // Handle statusCodes here.
    switch(responseFromSS->statusCode){
        default:
            break;
    }

    // Close connection with the Naming Server once a response has been received.
    close(SSSockFD);
    return responseFromSS->statusCode;
}

void sendHelp(){
    printf("The following is an exhaustive, case-sensitive list of the requests you can send:\n"
           "getinfo <path to folder/file>\n"
           "read <path to folder/file>\n"
           "write <path to folder/file>\n"
           "append <path to folder/file>\n"
           "delete <path to folder/file>\n"
           "create <path to folder/file>\n"
           "copy <source path to folder/file> <destination path to folder/file>\n"
           "help\n");
}

void getFileInfo(char clientRequest[], const char* arg){
    if(!arg) {
        printf(BRED "Incomplete arguments. Path hasn't been specified.\n" CRESET);
        sendHelp();
        return;
    }

    char* SSClientRequestCopy = (char*)calloc(strlen(clientRequest) + 20, sizeof(char));
    strcpy(SSClientRequestCopy, clientRequest);

    cl_NM_request_packet reqToNM = {0}; cl_NM_response_packet responseFromNM = {0};
    int NMSocketFD = forwardRequestToNM(&reqToNM, &responseFromNM, clientRequest);
    if(NMSocketFD < 0 && NMSocketFD != -999 && NMSocketFD != -995 && NMSocketFD != -994 && NMSocketFD != -993) return;
    else if(NMSocketFD == -999) {
        printf(BBLU "%s\n" CRESET, responseFromNM.message);
        free(SSClientRequestCopy);
        return;
    }
    else if(NMSocketFD == -995) {
        printf(BRED "The main storage server containing this file, and all its backup storage servers have gone offline. Please try again later.\n" CRESET);
        return;
    }
    else if(NMSocketFD == -994 || NMSocketFD == -993) {
        char tempBuf[MSG_LEN] = {0};
        sprintf(tempBuf, "%s%s", responseFromNM.message, SSClientRequestCopy);
        strcpy(SSClientRequestCopy, tempBuf);
    }

    // Connect to SS:
    printf(BBLU "Connecting to the storage server, at IP: %s and port %d.\n" CRESET, responseFromNM.SS_IP, responseFromNM.SS_Port);
    struct sockaddr_in SSInfo = {0}; cl_SS_response_packet responseFromSS = {0};
    int retVal = forwardRequestToSS(SSClientRequestCopy, &SSInfo, &responseFromNM, &responseFromSS, "");
    if(retVal < 0) return;

    // Reference: getInfoOfFile_CLSS in StorageServer.c
    // Returns 0 on success for a file.
    // 1 on success for a directory -> will print total number of blocks instead of size of directory in this case.
    // -1 if unable to scan directory.
    switch(responseFromSS.statusCode) {
        case 0:
            printf(BGRN "Received info from the Storage Server for the requested file:\n"
                   BCYN "%s\n" CRESET, responseFromSS.response);
            break;
        case 1:
            printf(BGRN "Received the info from the Storage Server for the requested directory:\n"
                   BCYN "%s\n" CRESET, responseFromSS.response);
            break;
        default:
            printf(BRED "Unable to scan one of the directories mentioned in the path: %s\n" CRESET, responseFromSS.response);
            break;
    }

    // Forward the response received from SS, to the NM.
    if(send(NMSocketFD, &responseFromSS, sizeof(cl_SS_response_packet), 0) == -1) perror(BRED "ERROR: Failed to forward this response to the Naming Server: " CRESET);
    close(NMSocketFD);

    free(SSClientRequestCopy);
}

void readFromFile(char clientRequest[], const char* arg){
    if(!arg) {
        printf(BRED "Incomplete arguments. Path hasn't been specified.\n" CRESET);
        sendHelp();
        return;
    }

    char* SSClientRequestCopy = (char*)calloc(strlen(clientRequest) + 20, sizeof(char));
    strcpy(SSClientRequestCopy, clientRequest);

    cl_NM_request_packet reqToNM = {0}; cl_NM_response_packet responseFromNM = {0};
    int NMSocketFD = forwardRequestToNM(&reqToNM, &responseFromNM, clientRequest);
    if(NMSocketFD < 0 && NMSocketFD != -999 && NMSocketFD != -995 && NMSocketFD != -994 && NMSocketFD != -993) return;
    else if(NMSocketFD == -999) {
        printf(BBLU "%s\n" CRESET, responseFromNM.message);
        free(SSClientRequestCopy);
        return;
    }
    else if(NMSocketFD == -995) {
        printf(BRED "The main storage server containing this file, and all its backup storage servers have gone offline. Please try again later.\n" CRESET);
        return;
    }
    else if(NMSocketFD == -994 || NMSocketFD == -993) {
        char tempBuf[MSG_LEN] = {0};
        sprintf(tempBuf, "%s%s", responseFromNM.message, SSClientRequestCopy);
        strcpy(SSClientRequestCopy, tempBuf);
    }

    // Connect to SS:
    printf(BBLU "Connecting to the storage server, at IP: %s and port %d.\n" CRESET, responseFromNM.SS_IP, responseFromNM.SS_Port);
    struct sockaddr_in SSInfo = {0}; cl_SS_response_packet responseFromSS = {0};
    int retVal = forwardRequestToSS(SSClientRequestCopy, &SSInfo, &responseFromNM, &responseFromSS, "");
    if(retVal < 0) return;

    // Reference: readFromFile_CLSS in StorageServer.c
    // Returns 0 if successful.
    // Returns 1 if it is a directory (no message returned in this case).
    // Returns -1 if file doesn't have read permissions for user.
    // Returns -2 for other errors while fopen-ing file.
    switch(responseFromSS.statusCode) {
        case 0:
            printf(BGRN "Successfully read the requested file from the Storage Server:\n"
                   BCYN "%s\n" CRESET, responseFromSS.response);
            break;
        case 1:
            printf(BRED "Failed to read data from the requested object, since it is a directory.\n" CRESET);
            break;
        case -1:
            printf(BRED "This file doesn't have read permissions for the user.\n" CRESET);
            break;
        default:
            printf(BRED "An error has occurred. Failed to read from the requested file: %s\n" CRESET, responseFromSS.response);
            break;
    }

    // Forward the response received from SS, to the NM.
    if(send(NMSocketFD, &responseFromSS, sizeof(cl_SS_response_packet), 0) == -1) perror(BRED "ERROR: Failed to forward this response to the Naming Server: " CRESET);
    close(NMSocketFD);

    free(SSClientRequestCopy);
}

void writeToFile(char clientRequest[], const char* arg){
    if(!arg) {
        printf(BRED "Incomplete arguments. Path hasn't been specified.\n" CRESET);
        sendHelp();
        return;
    }

    char* SSClientRequestCopy = (char*)calloc(strlen(clientRequest) + 1, sizeof(char));
    strcpy(SSClientRequestCopy, clientRequest);

    cl_NM_request_packet reqToNM = {0}; cl_NM_response_packet responseFromNM = {0};
    int NMSocketFD = forwardRequestToNM(&reqToNM, &responseFromNM, clientRequest);
    if(NMSocketFD < 0 && NMSocketFD != -995) return;
    else if(NMSocketFD == -995 || NMSocketFD == -994 || NMSocketFD == -993) {
        printf(BRED "The main storage server containing this file has gone offline. Please try again later.\n" CRESET);
        cl_SS_response_packet responseFromSS = {0};
        strcpy(responseFromSS.response, "FAILED");
        responseFromSS.statusCode = -1;
        if(send(NMSocketFD, &responseFromSS, sizeof(cl_SS_response_packet), 0) == -1) perror(BRED "ERROR: Failed to forward this response to the Naming Server: " CRESET);
        close(NMSocketFD);
        return;
    }

    // Get data from the user:
    printf(BCYN "Please type your message here.\n" CRESET);
    char clientMsg[MSG_LEN] = {0};
    fgets(clientMsg, MSG_LEN, stdin);

    // Connect to SS:
    printf(BBLU "Connecting to the storage server, at IP: %s and port %d.\n" CRESET, responseFromNM.SS_IP, responseFromNM.SS_Port);
    struct sockaddr_in SSInfo = {0}; cl_SS_response_packet responseFromSS = {0};
    int retVal = forwardRequestToSS(SSClientRequestCopy, &SSInfo, &responseFromNM, &responseFromSS, clientMsg);
    if(retVal < 0) return;

    // Reference: writeToFile_CLSS in StorageServer.c
    // Returns 0 if successful.
    // Returns 1 if it is a directory (no message returned in this case).
    // Returns -1 if file doesn't have write permissions for user.
    // Returns -2 for other errors while fopen-ing file.
    switch(responseFromSS.statusCode) {
        case 0:
            printf(BGRN "Successfully wrote the data to the requested file in the Storage Server:\n" CRESET);
            break;
        case 1:
            printf(BRED "Failed to write data to the requested object, since it is a directory.\n" CRESET);
            break;
        case -1:
            printf(BRED "This file doesn't have write permissions for the user.\n" CRESET);
            break;
        default:
            printf(BRED "An error has occurred. Failed to write to the requested file: %s\n" CRESET, responseFromSS.response);
            break;
    }

    // Forward the response received from SS, to the NM.
    if(send(NMSocketFD, &responseFromSS, sizeof(cl_SS_response_packet), 0) == -1) perror(BRED "ERROR: Failed to forward this response to the Naming Server: " CRESET);
    close(NMSocketFD);

    free(SSClientRequestCopy);
}

void appendToFile(char clientRequest[], const char* arg){
    if(!arg) {
        printf(BRED "Incomplete arguments. Path hasn't been specified.\n" CRESET);
        sendHelp();
        return;
    }

    char* SSClientRequestCopy = (char*)calloc(strlen(clientRequest) + 1, sizeof(char));
    strcpy(SSClientRequestCopy, clientRequest);

    cl_NM_request_packet reqToNM = {0}; cl_NM_response_packet responseFromNM = {0};
    int NMSocketFD = forwardRequestToNM(&reqToNM, &responseFromNM, clientRequest);
    if(NMSocketFD < 0 && NMSocketFD != -995) return;
    else if(NMSocketFD == -995 || NMSocketFD == -994 || NMSocketFD == -993) {
        printf(BRED "The main storage server containing this file has gone offline. Please try again later.\n" CRESET);
        cl_SS_response_packet responseFromSS = {0};
        strcpy(responseFromSS.response, "FAILED");
        responseFromSS.statusCode = -1;
        if(send(NMSocketFD, &responseFromSS, sizeof(cl_SS_response_packet), 0) == -1) perror(BRED "ERROR: Failed to forward this response to the Naming Server: " CRESET);
        close(NMSocketFD);
        return;
    }

    // Get data from the user:
    printf(BCYN "Please type your message here.\n" CRESET);
    char clientMsg[MSG_LEN] = {0};
    fgets(clientMsg, MSG_LEN, stdin);

    // Connect to SS:
    printf(BBLU "Connecting to the storage server, at IP: %s and port %d.\n" CRESET, responseFromNM.SS_IP, responseFromNM.SS_Port);
    struct sockaddr_in SSInfo = {0}; cl_SS_response_packet responseFromSS = {0};
    int retVal = forwardRequestToSS(SSClientRequestCopy, &SSInfo, &responseFromNM, &responseFromSS, clientMsg);
    if(retVal < 0) return;

    // Reference: writeToFile_CLSS in StorageServer.c
    // Returns 0 if successful.
    // Returns 1 if it is a directory (no message returned in this case).
    // Returns -1 if file doesn't have write permissions for user.
    // Returns -2 for other errors while fopen-ing file.
    switch(responseFromSS.statusCode) {
        case 0:
            printf(BGRN "Successfully appended the data to the requested file in the Storage Server:\n" CRESET);
            break;
        case 1:
            printf(BRED "Failed to append data to the requested object, since it is a directory.\n" CRESET);
            break;
        case -1:
            printf(BRED "This file doesn't have write permissions for the user.\n" CRESET);
            break;
        default:
            printf(BRED "An error has occurred. Failed to append to the requested file: %s\n" CRESET, responseFromSS.response);
            break;
    }
    if(responseFromSS.statusCode == 0) printf(BGRN "Data has been successfully appended to the end of the file specified!\n" CRESET);
    else printf(BRED "Failed to append the data to the end of the file specified.\n" CRESET);

    // Forward the response received from SS, to the NM.
    if(send(NMSocketFD, &responseFromSS, sizeof(cl_SS_response_packet), 0) == -1) perror(BRED "ERROR: Failed to forward this response to the Naming Server: " CRESET);
    close(NMSocketFD);

    free(SSClientRequestCopy);
}

void deleteFile(char clientRequest[], const char* arg){
    if(!arg) {
        printf(BRED "Incomplete arguments. Path hasn't been specified.\n" CRESET);
        sendHelp();
        return;
    }

    cl_NM_request_packet reqToNM = {0}; cl_NM_response_packet responseFromNM = {0};
    int NMSocketFD = forwardRequestToNM(&reqToNM, &responseFromNM, clientRequest);
    if(NMSocketFD < 0 && NMSocketFD != -995) return;
    else if(NMSocketFD == -995 || NMSocketFD == -994 || NMSocketFD == -993) {
        printf(BRED "The main storage server containing this file has gone offline. Please try again later.\n" CRESET);
        return;
    }

    // Determine whether this file is a directory or not (check for a / at the end), so that you can display error codes accordingly.
    bool isDirectory = false;
    int i = 0;
    while(arg[i] != '\0') i++;
    if(arg[i-1] == '\\') isDirectory = true;

    // Reference: deleteDirectory_NMSS in StorageServer.c
    // -1 if a component in the path doesn't exist
    // -2 if a directory component denies search permissions
    // -3 if a component in the path is not a directory
    // -4 if the target directory's parent does not provide write permissions
    // -5 if an error occurred during recursively deleting contents.
    // -6 for some other error
    if(isDirectory) {
        switch (responseFromNM.statusCode) {
            case -1:
                printf(BRED "One or more of the directory components mentioned in the path don't exist.\n" CRESET);
                break;
            case -2:
                printf(BRED "One or more of the directory components mentioned in the path doesn't provide \"read\" permissions, so, cannot search in that directory.\n" CRESET);
                break;
            case -3:
                printf(BRED "One or more of the directory components mentioned in the path isn't actually a directory.\n" CRESET);
                break;
            case -4:
                printf(BRED "The parent directory of the target does not provide write permissions. Cannot create this directory.\n" CRESET);
                break;
            case -5:
                printf(BRED "An error has occurred while recursively deleting the contents of this directory.\n" CRESET);
                break;
            case -6:
                printf(BRED "An unknown error has occurred.\n" CRESET);
                break;
            default:
                printf(BGRN "Directory successfully deleted!\n" CRESET);
                break;
        }
    }
    // Reference: deleteFile_NMSS in StorageServer.c
    // returns 0 on success
    // -1 if a component in the path doesn't exist
    // -2 if a directory component denies search permissions
    // -3 if a component in the path is not a directory
    // -4 if the target file is a directory
    // -5 if the target file's parent directory does not provide write permissions
    // -6 for some other error
    else{
        switch(responseFromNM.statusCode) {
            case -1:
                printf(BRED "One or more of the directory components mentioned in the path don't exist.\n" CRESET);
                break;
            case -2:
                printf(BRED "One or more of the directory components mentioned in the path doesn't provide \"read\" permissions, so, cannot search in that directory.\n" CRESET);
                break;
            case -3:
                printf(BRED "One or more of the directory components mentioned in the path isn't actually a directory.\n" CRESET);
                break;
            case -4:
                printf(BRED "The target file is a directory. Please insert a '\\' at the end of the path, and try again to delete a directory instead.\n" CRESET);
                break;
            case -5:
                printf(BRED "The parent directory of the target does not provide write permissions. Cannot create this file in this directory.\n" CRESET);
                break;
            case -6:
                printf(BRED "An unknown error has occurred.\n" CRESET);
                break;
            default:
                printf(BGRN "File successfully deleted!\n" CRESET);
                break;
        }
    }
}

void createFile(char clientRequest[], const char* arg){
    if(!arg) {
        printf(BRED "Incomplete arguments. Path hasn't been specified.\n" CRESET);
        sendHelp();
        return;
    }

    cl_NM_request_packet reqToNM = {0}; cl_NM_response_packet responseFromNM = {0};
    int NMSocketFD = forwardRequestToNM(&reqToNM, &responseFromNM, clientRequest);
    if(NMSocketFD < 0 && NMSocketFD != -995) return;
    else if(NMSocketFD == -995 || NMSocketFD == -994 || NMSocketFD == -993) {
        printf(BRED "The main storage server containing this file has gone offline. Please try again later.\n" CRESET);
        return;
    }

    // Determine whether this file is a directory or not (check for a / at the end), so that you can display error codes accordingly.
    bool isDirectory = false;
    int i = 0;
    while(arg[i] != '\0') i++;
    if(arg[i-1] == '\\') isDirectory = true;

    // Reference: createNewDirectory_NMSS in StorageServer.c
    // -1 if a directory component in the path does not exist
    // -2 if a directory component in the path exists but is not a directory
    // -3 if a directory component does not provide search permissions
    // -4 if the target directory already exists
    // -5 if the target already exists but not as a directory
    // -6 if the parent directory of the target does not provide write permisssions
    // -7 for some other error
    if(isDirectory) {
        switch (responseFromNM.statusCode) {
            case -1:
                printf(BRED "One or more of the directory components mentioned in the path don't exist.\n" CRESET);
                break;
            case -2:
                printf(BRED "One or more of the directory components mentioned in the path isn't actually a directory.\n" CRESET);
                break;
            case -3:
                printf(BRED "One or more of the directory components mentioned in the path doesn't provide \"read\" permissions, so, cannot search in that directory.\n" CRESET);
                break;
            case -4:
                printf(BRED "The target directory already exists.\n" CRESET);
                break;
            case -5:
                printf(BRED "The target already exists, but isn't a directory. Please remove the '\\' at the end of the path, if this was not intentional.\n" CRESET);
                break;
            case -6:
                printf(BRED "The parent directory of the target does not provide write permissions. Cannot create this directory.\n" CRESET);
                break;
            case -7:
                printf(BRED "An unknown error has occurred.\n" CRESET);
                break;
            default:
                printf(BGRN "Directory successfully created!\n" CRESET);
                break;
        }
    }
    // Reference: createNewFile_NMSS in StorageServer.c
    // -1 if a directory component in the path does not exist
    // -2 if a directory component in the path exists but is not a directory
    // -3 if a directory does not provide search permissions
    // -4 if the file already exists
    // -5 if the file exists but as a directory
    // -6 if the file exists but neither as a regular file nor as a directory
    // -7 if the target's parent directory does not provide write permissions
    // -8 for some other error
    else{
        switch(responseFromNM.statusCode) {
            case -1:
                printf(BRED "One or more of the directory components mentioned in the path don't exist.\n" CRESET);
                break;
            case -2:
                printf(BRED "One or more of the directory components mentioned in the path isn't actually a directory.\n" CRESET);
                break;
            case -3:
                printf(BRED "One or more of the directory components mentioned in the path doesn't provide \"read\" permissions, so, cannot search in that directory.\n" CRESET);
                break;
            case -4:
                printf(BRED "The target file already exists.\n" CRESET);
                break;
            case -5:
                printf(BRED "The target file already exists, but is a directory. Please insert a '\\' at the end of the path, and try again to create a directory instead.\n" CRESET);
                break;
            case -6:
                printf(BRED "The target file already exists, but is neither a regular file nor a directory.\n" CRESET);
                break;
            case -7:
                printf(BRED "The parent directory of the target does not provide write permissions. Cannot create this file in this directory.\n" CRESET);
                break;
            case -8:
                printf(BRED "An unknown error has occurred.\n" CRESET);
                break;
            default:
                printf(BGRN "File successfully created!\n" CRESET);
                break;
        }
    }
}

void copyFile(char clientRequest[], char* arg){
    if(!arg) {
        printf(BRED "Incomplete arguments. Source path hasn't been specified.\n" CRESET);
        sendHelp();
        return;
    }
    arg = strtok(NULL, delimiters);
    if(!arg){
        printf(BRED "Incomplete arguments. Destination path hasn't been specified.\n" CRESET);
        sendHelp();
        return;
    }

    cl_NM_request_packet reqToNM = {0}; cl_NM_response_packet responseFromNM = {0};
    int NMSocketFD = forwardRequestToNM(&reqToNM, &responseFromNM, clientRequest);
    if(NMSocketFD < 0 && NMSocketFD != -995) return;
    else if(NMSocketFD == -995) {
        printf(BRED "The main storage server containing this file has gone offline. Please try again later.\n" CRESET);
        return;
    }

    // NM does this. Check statusCode, when a response acknowledgement is sent.
    // Reference: zipAndSendFile_NMSS and recAndUnzip_NMSS in copy.c
    // Returns 0 if successful.
    // Returns -1 if execvp failed.
    // Returns -2 if failed to fork.
    // Returns -3 if send/recv failed.
    // Returns -4 if failed to open zip file.
    switch(responseFromNM.statusCode) {
        case -1:
            printf(BRED "execvp() failed in the Storage Server.\n" CRESET);
            break;
        case -2:
            printf(BRED "Failed to fork() in the Storage Server.\n" CRESET);
            break;
        case -3:
            printf(BRED "send/recv failed while the Storage Server was attempting to connect to the Naming Server.\n" CRESET);
            break;
        case -4:
            printf(BRED "Failed to open the .zip file.\n" CRESET);
            break;
        default:
            printf(BGRN "Successfully copied the file from the source to the destination.\n" CRESET);
            break;
    }
}

void processRequest(char clientRequest[]){
    char* processMsg = (char*)calloc(strlen(clientRequest) + 1, sizeof(char));
    strcpy(processMsg, clientRequest);
    
    char* oper = strtok(processMsg, delimiters);
    if(strcmp(oper, "getinfo") == 0) oper = strtok(NULL, delimiters), getFileInfo(clientRequest, oper);
    else if(strcmp(oper, "read") == 0) oper = strtok(NULL, delimiters), readFromFile(clientRequest, oper);
    else if(strcmp(oper, "write") == 0) oper = strtok(NULL, delimiters), writeToFile(clientRequest, oper);
    else if(strcmp(oper, "append") == 0) oper = strtok(NULL, delimiters), appendToFile(clientRequest, oper);
    else if(strcmp(oper, "delete") == 0) oper = strtok(NULL, delimiters), deleteFile(clientRequest, oper);
    else if(strcmp(oper, "create") == 0) oper = strtok(NULL, delimiters), createFile(clientRequest, oper);
    else if(strcmp(oper, "copy") == 0) oper = strtok(NULL, delimiters), copyFile(clientRequest, oper);
    else {
        if(strcmp(oper, "help") != 0) printf("Invalid request!\n");
        sendHelp();
    }
    
    free(processMsg);
}

int main(){
    while(1) {
        printf(BMAG "Starting a new request instance. Please type your request here.\n" CRESET);

        // Take a request from the user, and pass it over to the naming server, if it is valid.
        char clientRequest[MSG_LEN] = {0};
        fgets(clientRequest, MSG_LEN, stdin);
        char* newLineEsc = strchr(clientRequest, '\n'); // Searches for first occurrence of newline char and if found, returns pointer to it. Else, returns NULL.
        if (newLineEsc) *newLineEsc = '\0';
        if(clientRequest[0] == '\0') continue;
        processRequest(clientRequest);

        printf(BMAG "Request has been processed. Terminated connection with the Naming Server and the Storage Server(s).\n" CRESET);
    }
}