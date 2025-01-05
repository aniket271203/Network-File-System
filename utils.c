#include "utils.h"

// Attempts to connect to given IP and port. Returns -1 if failure occurred, and prints message to stderr.
// Pass the sockaddr_in* struct pointer. This function will return the file descriptor of the socket that it opened (if valid).
// Else, will return -1.
int connectViaTCP(char IP[], int port, struct sockaddr_in* serverInfo){

    // Initialise variables:
    int serverSockFD;

    // Check the validity of the port entered.
    if(!(port >= 0 && port <= 65535)){
        fprintf(stderr, BRED "ERROR: Invalid port! Please define a valid integer for the port, between 0 and 65535.\n" CRESET);
        return -1;
    }

    // Set details of server.
    serverInfo->sin_family = AF_INET; // IPv4 address family.
    serverInfo->sin_port = htons(port); // Converts the unsigned short int hostshort from host byte order to network byte order (so that the network can interpret this number correctly).
    serverInfo->sin_addr.s_addr = inet_addr(IP); // Convert internet host address from numbers and dots notation to network byte order.

    // Create a TCP server socket.
    if((serverSockFD = socket(AF_INET, SOCK_STREAM, 0)) == -1){ // domain, type, protocol. Domain is IPv4 type, SOCK_STREAM is for reliable 2-way connection-oriented communication (TCP), and protocol 0 implies to use whatever protocol is available for this type of domain (IPv4).
        perror(BRED "ERROR: Failed to open a socket to the destination: " CRESET);
        return -1;
    }

    // Connect to the server.
    if(connect(serverSockFD, (struct sockaddr*) serverInfo, sizeof(*serverInfo)) == -1){
        perror(BRED "ERROR: Failed to connect to the destination: " CRESET);
        close(serverSockFD);
        return -1;
    }

    printf(BGRN "Successfully established a connection with the destination!\n" CRESET);
    return serverSockFD;
}


void print_ss_info(ss_info *info)
{
    printf("SS_INFO stucture:\n");
    printf("IP: %s\n", info->ipAddress);
    printf("NM Port: %d\n", info->nmPort);
    printf("Client Port: %d\n", info->clientPort);
    printf("isBackup: %d\n", (int)(info->isBackup));
    printf("number of accessible paths: %d\nAccessible paths:\n", info->numAccessiblePaths);
    for(int i = 0; i < info->numAccessiblePaths; i++) printf("%s\n", info->accessiblePaths[i]);
    printf("=================\n");
}

// converts non-string fields to string and delimits all in order, using '\n'
// using snprintf to convert integers to strings: https://stackoverflow.com/questions/8257714/how-can-i-convert-an-int-to-a-string-in-c
char *stringifySSInfo(ss_info *info)
{
    char *ans;

    char *nmPortStr, *clientPortStr, *numAccessiblePathsStr;
    int nmPortLen, clientPortLen, numAccessiblePathsLen;

    nmPortLen = snprintf(NULL, 0, "%d", info->nmPort);
    nmPortStr = (char*)malloc(sizeof(char) * (nmPortLen + 1));
    snprintf(nmPortStr, nmPortLen + 1, "%d", info->nmPort);

    clientPortLen = snprintf(NULL, 0, "%d", info->clientPort);
    clientPortStr = (char*)malloc(sizeof(char) * (clientPortLen + 1));
    snprintf(clientPortStr, clientPortLen + 1, "%d", info->clientPort);

    char back[2] = {0};
    back[0] = '0' + (int)info->isBackup;

    numAccessiblePathsLen = snprintf(NULL, 0, "%d", info->numAccessiblePaths);
    numAccessiblePathsStr = (char*)malloc(sizeof(char) * (numAccessiblePathsLen + 1));
    snprintf(numAccessiblePathsStr, numAccessiblePathsLen + 1, "%d", info->numAccessiblePaths);

    int ansLen = 0;
    ansLen += strlen(info->ipAddress) + 1 + nmPortLen + 1 + clientPortLen + 1 + 1 + 1 + numAccessiblePathsLen + 1; // +1 for '\n', isBackup after clientPort
    for(int i = 0; i < info->numAccessiblePaths; i++) ansLen += (strlen(info->accessiblePaths[i]) + 1); // +1 with last item for '\0'

    ans = (char*)malloc(sizeof(char) * ansLen);
    strcpy(ans, info->ipAddress);
    strcat(ans, "\n");
    strcat(ans, nmPortStr);
    strcat(ans, "\n");
    strcat(ans, clientPortStr);
    strcat(ans, "\n");
    strcat(ans, back);
    strcat(ans, "\n");
    strcat(ans, numAccessiblePathsStr);

    if(info->isBackup == 1) // don't add paths
        return ans;

    strcat(ans, "\n");
    for(int i = 0; i < info->numAccessiblePaths; i++)
    {
        strcat(ans, info->accessiblePaths[i]);
        if(i != info->numAccessiblePaths - 1) strcat(ans, "\n");
    }
    
    return ans;
}

// uses strtok to tokenise, assumes only a valid string is passed
// uses strtol to convert string to long: https://man7.org/linux/man-pages/man3/strtol.3.html
ss_info *destringifySSInfo(char *info) 
{
    ss_info *ans = (ss_info *)malloc(sizeof(ss_info));

    char *token;
    token = strtok(info, "\n"); // first is IP

    ans->ipAddress = strdup(token);

    token = strtok(NULL, "\n");
    ans->nmPort = strtol(token, NULL, 10);

    token = strtok(NULL, "\n");
    ans->clientPort = strtol(token, NULL, 10);

    token = strtok(NULL, "\n");
    ans->isBackup = (strcmp(token, "1") == 0) ? true : false;

    token = strtok(NULL, "\n");
    ans->numAccessiblePaths = strtol(token, NULL, 10);

    if(ans->isBackup == 1) // don't add paths
        ans->accessiblePaths = NULL;
    else
        ans->accessiblePaths = (char**)malloc(sizeof(char*) * ans->numAccessiblePaths);

    // rest are accessible paths
    for(int i = 0; i < ans->numAccessiblePaths; i++)
    {
        token = strtok(NULL, "\n");
        ans->accessiblePaths[i] = strdup(token);
    }

    return ans;
}

// converts the boolean array to a binary string on {'0', '1'}, and places the backupSuccess field as the first byte
char *stringifyAddedPaths(added_paths *ap, int numTotalPaths)
{
    char *ans = (char*)calloc(1 + ((ap == NULL) ? 0 : numTotalPaths) + 1, sizeof(char)); // don't add the array if it is NULL
    ans[0] = (ap->backupSuccess == 1) ? 'B' : 'N'; // different alphabet for clarity
    if(ap != NULL) for(int i = 0; i < numTotalPaths; i++) ans[i + 1] = (int)(ap->addedIndices[i]) + '0';
    return ans;
}

/* functions for path resolution */

// gives path relative to 'ref'
// returns NULL if 'absPath' is not in the subtree of 'ref', or if 'ref' contains 'absPath'
// if valid, returns relative path prefixed by '/'.
// simply returns "" (empty string) if both arguments are the same
char *getRelPath(const char *absPath, const char *ref)
{
    char *ans;
    int i = 0;
    int absLen = strlen(absPath), refLen = strlen(ref);
    
    while(i < refLen && i < absLen)
    {
        if(absPath[i] != ref[i]) // not in subtree
            return NULL;
        i++;
    }
    if(i == absLen && i != refLen) // ref contains absPath
        return NULL;

    ans = (char*)malloc(sizeof(char) *  (absLen - refLen + 1)); // +1 for '\0'
    strcpy(ans, "");
    if(absLen != refLen) strcat(ans, absPath + refLen); // '/' is included in absPath

    return ans;
}

// 'relPath' is a '/' prefixed path relative to 'ref', where 'ref' is a fully qualified path.
// returns the absolute path of 'relPath' wrt 'ref'
char *getAbsPath(const char *relPath, const char *ref)
{
    char *ans = (char*)malloc(sizeof(char) * (strlen(ref) + strlen(relPath) + 1)); // '/' included in 'relPath', +1 for '\0'
    strcpy(ans, ref);
    strcat(ans, relPath);
    return ans;
}
