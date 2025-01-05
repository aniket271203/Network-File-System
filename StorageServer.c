#include "StorageServer.h"

/* setup/util fuctions */

// initialises all locks (binary semaphores)
void initLocks()
{
    sem_init(&printLock, 0, 1);
}

// returns type of path: 0 for file, 1 for direcotry (in which case we add all paths of the directory to our list)
// returns -1 if it's an invalid path
int pathType(char *path)
{
    int ans;
    struct stat info;
    if(lstat(path, &info) < 0) ans = -1; // some error occurred, so assuming path is invalid
    else
    {
        unsigned int checkType = info.st_mode & S_IFMT;
        if(checkType == S_IFDIR) ans = 1;
        else if(!(checkType == S_IFBLK) || (checkType == S_IFCHR) || (checkType == S_IFIFO) || (checkType == S_IFLNK) || (checkType == S_IFSOCK))
            ans = 0;
    }
    return ans;
}

// helper routine to add all paths inside a directory
// counter is there to help with the while loop it works inside
// pass hashFlag as 1 if the paths are to be added to the hashTable PM instead; pass counter as NULL then
void addAllSubPaths(char *buf, int *counter, int hashFlag)
{
    chdir(buf);
    char *cwd = getcwd(NULL, 0);
    // add everything in it to myInfo
    struct dirent **info;
    int numEntries;
    if((numEntries = scandir(".", &info, NULL, alphasort)) == -1)
    {
        sem_wait(&printLock);
        fprintf(stderr, BRED "Unable to scan this directory: %s\n" CRESET, getRelPath(getcwd(NULL, 0), rootDir));
        sem_post(&printLock);
        chdir(cwd);
        free(cwd);
        return;
    }

    for(int i = 0; i < numEntries; i++)
    {
        struct stat temp = {0};
        if(stat(info[i]->d_name, &temp) == -1)
        {
            chdir(cwd);
            free(cwd);
            return;
        }
        if(strcmp(info[i]->d_name, ".") == 0 || strcmp(info[i]->d_name, "..") == 0 ) continue;

        // to have the path relative from rootDir
        char *tempName = (char*)malloc(sizeof(char) * (strlen(info[i]->d_name) + 1 + 1));
        strcpy(tempName, "/");
        strcat(tempName, info[i]->d_name);
        char *relPath = getRelPath(getAbsPath(tempName, cwd), rootDir);
        free(tempName);
        if(S_ISDIR(temp.st_mode)) // add a '/' at the end
        {
            tempName = (char*)malloc(sizeof(char) * (strlen(relPath) + 1 + 1));
            strcpy(tempName, relPath);
            strcat(tempName, "/");
            free(relPath);
            relPath = tempName;
        }

        if(hashFlag == 1)
            insertInTable(relPath + 1, PM);
        else
        {
            // add this to myInfo
            myInfo.accessiblePaths = (char**)realloc(myInfo.accessiblePaths, sizeof(char*) * (myInfo.numAccessiblePaths + 1));
            myInfo.accessiblePaths[(*counter)++] = relPath + 1; // to get rid of the first '/'
            myInfo.numAccessiblePaths++;
        }

        // if it's a directory recursively add its contents
        if(S_ISDIR(temp.st_mode)) addAllSubPaths(info[i]->d_name, counter, hashFlag);
        chdir(cwd);
    }

    free(cwd);
    chdir(rootDir); // to be safe
}

// takes user input for getting the accessible paths, and fill other fields like ports for clients and for NM etc.
// doesn't take in input for paths if isBackup is 1
void fillMyInfo(int portForNM, int portForClients, int isBackup)
{
    myInfo.ipAddress = SS_IP;
    myInfo.nmPort = portForNM;
    myInfo.clientPort = portForClients;
    myInfo.isBackup = isBackup;

    if(isBackup == 1)
    {
        myInfo.numAccessiblePaths = 0;
        myInfo.accessiblePaths = NULL;
        printf(MAG "Setting up as a backup Storage Server.\n" CRESET);
        return;
    }

    printf(MAG "Enter the number of accessible paths: " BLU);
    scanf("%d", &myInfo.numAccessiblePaths);
    while(myInfo.numAccessiblePaths <= 0)
    {
        printf(BRED "Invalid entry. Re-enter a positive number: "CRESET);
        scanf("%d", &myInfo.numAccessiblePaths);
    }
    myInfo.accessiblePaths = (char**)malloc(sizeof(char*) * myInfo.numAccessiblePaths);

    printf(MAG "Enter the accessible paths, to be exposed to the Naming Server:\n" BLU);
    int i = 0;
    while(i < myInfo.numAccessiblePaths)
    {
        char buf[MAX_BUF_LEN];
        memset(buf, '\0', sizeof(char) * MAX_BUF_LEN);
        scanf("%s", buf);
        int type;
        if((type = pathType(buf)) == -1)
        {
            printf(WHT "%s" BRED " is an invalid path. Please enter a valid path\n" BLU, buf);
            continue;
        }
        else if(type == 1)
            addAllSubPaths(buf, &i, 0);
        myInfo.accessiblePaths[i] = (char*)malloc(sizeof(char) * (strlen(buf) + 1));
        strcpy(myInfo.accessiblePaths[i], buf);

        i++;
    }
    printf(CRESET);
}

// SS creates two sockets and binds them to two different ports, to act as "server" for both NM and clients (respectively)
// SS gets user input on accessible paths
// SS connects (as "client") to NM on NM_SS_PORT, sends the myInfo struct.
// NM registers new SS, closes the fd to close this connection
// NM connects to the new SS_NM_PORT (as "client") whenever it needs to 
// returns the new FD for comms with the NM ----> we'd need multiple fds for multiple clients
int initialiseSS(int isBackup)
{
    struct sockaddr_in *clientInfo = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
    struct sockaddr_in *newNMInfo = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
    int newNMSockFD;
    int clientSockFD;

    // create a socket for NM comms
    if((newNMSockFD = socket(AF_INET, SOCK_STREAM, 0)) == -1){ // domain, type, protocol. Domain is IPv4 type, SOCK_STREAM is for reliable 2-way connection-oriented communication (TCP), and protocol 0 implies to use whatever protocol is available for this type of domain (IPv4).
        perror(BRED "ERROR: Failed to open a socket for NM" CRESET);
        close(newNMSockFD);
        return -1;
    }

    newNMInfo->sin_family = AF_INET; // IPv4
    newNMInfo->sin_port = htons(0);  // any free port
    newNMInfo->sin_addr.s_addr = inet_addr(CLIENT_IP);

    // thread off for NM comms --------------------------------------> later

    // bind to the socket
    if(bind(newNMSockFD, (struct sockaddr*)newNMInfo, sizeof(*newNMInfo)) < 0){
        perror(BRED "ERROR: Failed to bind to socket for NM: " CRESET);
        close(newNMSockFD);
        return -1;
    }

    // get its port etc
    socklen_t newNMSize = sizeof(*newNMInfo);
    if(getsockname(newNMSockFD, (struct sockaddr*)newNMInfo, &newNMSize) < 0){
        perror(BRED "ERROR: Error occurred while retrieving port details on socket for NM: " CRESET);
        close(newNMSockFD);
        return -1;
    }
    printf(CYN "Port for communicating with the Naming Server is %d\n" CRESET, ntohs(newNMInfo->sin_port));

    // now, create a TCP socket and bind to a new port to comm with clients
    if((clientSockFD = socket(AF_INET, SOCK_STREAM, 0)) == -1){ // domain, type, protocol. Domain is IPv4 type, SOCK_STREAM is for reliable 2-way connection-oriented communication (TCP), and protocol 0 implies to use whatever protocol is available for this type of domain (IPv4).
        perror(BRED "ERROR: Failed to open a socket for clients: " CRESET);
        close(newNMSockFD), close(clientSockFD);
        return -1;
    }

    clientInfo->sin_family = AF_INET; // IPv4
    clientInfo->sin_port = htons(0);
    clientInfo->sin_addr.s_addr = inet_addr(CLIENT_IP);
    
    // Bind to the socket.
    if(bind(clientSockFD, (struct sockaddr*) clientInfo, sizeof(*clientInfo)) < 0){
        perror(BRED "ERROR: Failed to bind to socket for clients: " CRESET);
        close(newNMSockFD), close(clientSockFD);
        return -1;
    }

    // Get port and other details
    socklen_t clientSize = sizeof(*clientInfo);
    if(getsockname(clientSockFD, (struct sockaddr *) clientInfo, &clientSize) < 0){
        perror(BRED "ERROR: Error occurred while retrieving port details socket for clients: " CRESET);
        close(newNMSockFD), close(clientSockFD);
        return -1;
    }
    printf(CYN "Port for communicating with clients is %d\n" CRESET, ntohs(clientInfo->sin_port));

    // get accessible paths from user, and fill in new ports in the myInfo struct
    fillMyInfo(ntohs(newNMInfo->sin_port), ntohs(clientInfo->sin_port), isBackup);

    // connect to NM as "client" and send the myInfo struct
    struct sockaddr_in *NMInfo = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
    int NMSockFD = -1;

    // add NM_IP once initialised
    while(NMSockFD < 0)
    {
        printf("Attempting to connect to the Naming Server at IP: %s and port: %d\n", "127.0.0.1", NM_SS_PORT);
        NMSockFD = connectViaTCP("127.0.0.1", NM_SS_PORT, NMInfo);
        if(NMSockFD < 0) 
        {
            fprintf(stderr, "Failed to connect to the naming server. Reattempting connection in %d second(s).\n", REATTEMPT_WAIT);
        }
        sleep(REATTEMPT_WAIT);
    }

    // Bookkeeping
    struct sockaddr_in tempInfo = {0};
    socklen_t tempInfoSize = sizeof(tempInfo);
    if(getsockname(NMSockFD, (struct sockaddr *) &tempInfo, &tempInfoSize) < 0){
        perror(BRED "ERROR: Error occurred while retrieving port details on socket for NM: " CRESET);
        close(NMSockFD), close(newNMSockFD), close(clientSockFD);
        return -1;
    }
    sem_wait(&printLock);
    printf(CYN "Connection established with NM\n");
    printf("from my IP: " WHT "%s" CYN " and port: " WHT "%d\n", SS_IP, ntohs(tempInfo.sin_port));
    printf("to its IP: " WHT "%s" CYN " and port: " WHT "%d\n", "127.0.0.1", NM_SS_PORT);
    printf(CYN "Sending port details.\n" CRESET);
    sem_post(&printLock);

    // stringify struct
    char *myInfoMsg = stringifySSInfo(&myInfo);
    // send to NM
    if (send(NMSockFD, myInfoMsg, strlen(myInfoMsg), 0) < 0)
    {
        fprintf(stderr, BRED "Failed to send details to Naming Server.\n" CRESET); // should I retry?
        close(newNMSockFD), close(clientSockFD), close(NMSockFD);
        return -1;
    }

    // receive confirmation from NM
    char *regdPaths = (char*)calloc(myInfo.numAccessiblePaths + 2, sizeof(char));
    if (recv(NMSockFD, regdPaths, myInfo.numAccessiblePaths + 2, 0) < 0)
    {
        fprintf(stderr, BRED "Failed to receive confirmation message from Naming Server.\n" CRESET); // should I retry?
        close(newNMSockFD), close(clientSockFD), close(NMSockFD);
        return -1;
    }

    if(myInfo.isBackup == 1) // wants to be backup
    {
        if(regdPaths[0] == 'N') // rejected
        {
            printf(BRED "Naming Server has rejected request to register as a Backup Storage Server.\n\n"CRESET);
            close(newNMSockFD), close(clientSockFD), close(NMSockFD);
            return -1;
        }
        printf(CYN "Succesfully registered as a Backup Storage Server with the Naming Server.\n\n"CRESET);
    }
    else
    {
        if(regdPaths[0] == 'N') // rejected, NM needs BSSs
        {
            printf(BRED "Naming Server has rejected request to register as a Storage Server.\n\n"CRESET);
            close(newNMSockFD), close(clientSockFD), close(NMSockFD);
            return -1;
        }
        else // check for accepted paths and add them to the hashmap
        {
            int numRegd = 0;
            for(int i = 0; i < myInfo.numAccessiblePaths; i++)
            {
                if(regdPaths[i + 1] == '0')
                    printf(WHT "%s" RED " not registered with the Naming Server.\n" CRESET, myInfo.accessiblePaths[i]);
                else
                {
                    numRegd++;
                    printf(WHT "%s" GRN " successfully registered with the Naming Server.\n" CRESET, myInfo.accessiblePaths[i]);
                    insertInTable(myInfo.accessiblePaths[i], PM);
                }
            }
            if(numRegd == 0) // no path accepted
            {
                printf(BRED "Naming Server has rejected all paths.\n" CRESET);
                close(newNMSockFD), close(clientSockFD), close(NMSockFD);
                return -1;
            }
            else
                printf(CYN "Succesfully registered as a Storage Server with the Naming Server with %d paths accepted out of %d.\n\n" CRESET, numRegd, myInfo.numAccessiblePaths);
        }
    }

    close(NMSockFD);
    NM_SockFD = newNMSockFD;
    Client_SockFD = clientSockFD;
    return 0;
}

// ===============================================================================================================================
// ===============================================================================================================================

/* Functions for commands issued by NM */

// creates a directory with given path in the root directory.
// if one or more directory components in 'path' are not present, creates them as well
// returns 0 for success, -1 for failure
// -1 if a directory component in the path does not exist
// -2 if a directory component in the path exists but is not a directory
// -3 if a directory component does not provide search permissions
// -4 if the target directory already exists
// -5 if the target already exists but not as a directory
// -6 if the parent directory of the target does not provide write permisssions
// -7 for some other error
int createNewDirectory_NMSS(char *path)
{
    // remove last '/'
    path[strlen(path) - 1] = '\0';

    struct stat st = {0};
    int ret = stat(path, &st);
    if(ret == 0)
    {
        if(S_ISDIR(st.st_mode)) ret = -4;
        else ret = -5;
        goto exit_create_dir;
    }
    else if(ret == -1 && errno != ENOENT)
    {
        if(errno == EACCES) // a directory component denies search permissions
            ret = -3;
        else if(errno == ENOTDIR) // a component in the path is not a directory
            ret = -2;
        else ret = -7; // some other error
        goto exit_create_dir;
    }

    ret = mkdir(path, 0700);
    if(ret == -1)
    {
        if(errno == ENOENT) // a component in the path doesn't exist
            ret = -1;
        else if(errno == EACCES) // parent directory does not give write permissions
            ret = -6;
        else ret = -7;
        goto exit_create_dir;
    }
    
    insertInTable(path, PM);
    ret = 0;
exit_create_dir:
    if(ret < 0)
    {
        sem_wait(&printLock);
        printf(BRED "Could not create directory at " WHT "%s/" BRED " because ", path);
        switch(ret){
            case -1: printf("a directory component in the path does not exist.\n" CRESET); break;
            case -2: printf("a directory component in the path exists but is not a directory.\n" CRESET); break;
            case -3: printf("a directory component in the path does not provide search permissions.\n" CRESET); break;
            case -4: printf("the target directory already exists\n" CRESET); break;
            case -5: printf("the target already exists but not as a directory.\n" CRESET); break;
            case -6: printf("if the parent directory of the target does not provide write permisssions.\n" CRESET); break;
            default: printf("some error occurred.\n" CRESET); break;
        }
        sem_post(&printLock);
    }
    else printf(BGRN "Successfully created directory at " WHT "%s/ \n"CRESET, path);
    return ret;
}

// creates a file with given path in the root directory.
// if one or more directory components in 'path' are not present, creates them as well
// returns 0 for success
// -1 if a directory component in the path does not exist
// -2 if a directory component in the path exists but is not a directory
// -3 if a directory does not provide search permissions
// -4 if the file already exists
// -5 if the file exists but as a directory
// -6 if the file exists but neither as a regular file nor as a directory
// -7 if the target's parent directory does not provide write permissions
// -8 for some other error
int createNewFile_NMSS(char *path)
{
    struct stat st = {0};
    int ret = stat(path, &st);
    if(ret == 0)
    {
        if(S_ISDIR(st.st_mode)) ret = -5;
        if(S_ISREG(st.st_mode)) ret = -4;
        else ret = -6;
        goto exit_create_file;
    }
    else if(ret == -1 && errno != ENOENT)
    {
        if(errno == EACCES) // a directory component denies search permissions
            ret = -3;
        else if(errno == ENOTDIR) // a component in the path is not a directory
            ret = -2;
        else ret = -8; // some other error
        goto exit_create_file;
    }

    int fd = open(path, O_CREAT, 0644);
    if(fd == -1)
    {
        if(errno == EACCES) // write perms denied
            ret = -7;
        else if(errno == ENOENT) // a component in the path doesn't exist
            ret = -1;
        else ret = -7;
        goto exit_create_file;
    }
    close(fd);
    insertInTable(path, PM);
    ret = 0;
exit_create_file:
    if(ret < 0)
    {
        sem_wait(&printLock);
        printf(BRED "Could not create file at " WHT "%s" BRED " because ", path);
        switch(ret){
            case -1: printf("a directory component in the path does not exist.\n" CRESET); break;
            case -2: printf("a directory component in the path exists but is not a directory.\n" CRESET); break;
            case -3: printf("a directory component in the path does not provide search permissions.\n" CRESET); break;
            case -4: printf("the file already exists.\n" CRESET); break;
            case -5: printf("the file exists but as a directory.\n" CRESET); break;
            case -6: printf("the file exists but neither as a regular file nor as a directory.\n" CRESET); break;
            case -7: printf("the target file's parent directory does not provide write permissions.\n" CRESET); break;
            default: printf("some error occurred.\n" CRESET); break;
        }
        sem_post(&printLock);
    }
    else printf(BGRN "Successfully created file at " WHT "%s \n"CRESET, path);
    return ret;
}

// helper function for deleteDirectory_NMSS
int recursiveDelete(char *path)
{
    int ret;
    chdir(path);
    char *cwd = getcwd(NULL, 0);
    struct dirent **info;
    int numEntries;
    if((numEntries = scandir(".", &info, NULL, alphasort)) == -1)
    {
        sem_wait(&printLock);
        fprintf(stderr, BRED "Unable to scan this directory: %s\n" CRESET, getRelPath(getcwd(NULL, 0), rootDir));
        sem_post(&printLock);
        ret = -5;
        chdir(cwd);
        chdir("..");
        free(cwd);
        return ret;
    }

    for(int i = 0; i < numEntries; i++)
    {
        struct stat temp = {0};
        if((ret = stat(info[i]->d_name, &temp)) == -1)
        {
            ret = -5;
            chdir(cwd);
            chdir("..");
            free(cwd);
            return ret;
        }
        if(strcmp(info[i]->d_name, ".") == 0 || strcmp(info[i]->d_name, "..") == 0 ) continue;

        // to have paths relative form rootDir
        char *tempName = (char*)malloc(sizeof(char) * (strlen(info[i]->d_name) + 1 + 1));
        strcpy(tempName, "/");
        strcat(tempName, info[i]->d_name);

        if(S_ISDIR(temp.st_mode)) ret = recursiveDelete(getAbsPath(tempName, getcwd(NULL, 0)));
        if(S_ISREG(temp.st_mode)) ret = deleteFile_NMSS(getAbsPath(tempName, getcwd(NULL, 0)));
        free(tempName);
        if(ret < 0)
        {
            chdir(cwd);
            chdir("..");
            free(cwd);
            return -5;
        }
    }
    chdir(cwd);
    chdir(".."); // to get to delete the directory itself
    free(cwd);

    if(rmdir(path) == -1)
    {
        if(errno == EACCES) ret = -4;
        else ret = -5;
        // printf(BRED "Could not delete directory at " WHT "%s\n" CRESET, getRelPath(path, rootDir) + 1);
        return ret;
    }
    // printf(BGRN "Successfully deleted directory at " WHT "%s\n"CRESET, getRelPath(path, rootDir) + 1);
    deleteInTable(path, PM);

    return 0;
}

// deletes a directory with the given path
// returns 0 on success
// -1 if a component in the path doesn't exist
// -2 if a directory component denies search permissions
// -3 if a component in the path is not a directory
// -4 if the target directory's parent does not provide write permissions
// -5 if an error occurred during recursively deleting contents. too much errors handled now lol
// -6 for some other error
int deleteDirectory_NMSS(char *path)
{
    // remove '/' at the end of path
    path[strlen(path) - 1] = '\0';
    
    struct stat pathStat = {0};
    int ret = stat(path, &pathStat);
    if(ret == -1)
    {
        if(errno == ENOENT) // a component in the path doesn't exist
            ret = -1;
        else if(errno == EACCES) // a directory component denies search permissions
            ret = -2;
        else if(errno == ENOTDIR) // a component in the path is not a directory
            ret = -3;
        else ret = -6; // some other error
        goto exit_delete;
    }

    char *tempName = (char*)malloc(sizeof(char) * (strlen(path) + 1 + 1));
    if(path[0] != '/')
    {
        strcpy(tempName, "/");
        strcat(tempName, path);
    }
    else strcpy(tempName, path);
    ret = recursiveDelete(getAbsPath(tempName, rootDir));
    free(tempName);
    
exit_delete:
    chdir(rootDir);
    if(ret < 0)
    {
        sem_wait(&printLock);
        printf(BRED "Could not delete directory at " WHT "%s" BRED " because ", path);
        switch(ret){
            case -1: printf("a directory component in the path does not exist.\n" CRESET); break;
            case -2: printf("a directory component in the path does not provide search permissions.\n" CRESET); break;
            case -3: printf("a directory component in the path exists but is not a directory.\n" CRESET); break;
            case -4: printf("the target directory's parent directory does not provide write permissions.\n" CRESET); break;
            case -5: printf("an error occurred during recursively deleting the directory's contents.\n" CRESET); break;
            default: printf("some error occurred.\n" CRESET); break;
        }
        sem_post(&printLock);
    }
    else printf(BGRN "Successfully deleted directory at " WHT "%s \n"CRESET, path);
    return ret;
}

// deletes a file with the given path
// returns 0 on success
// -1 if a component in the path doesn't exist
// -2 if a directory component denies search permissions
// -3 if a component in the path is not a directory
// -4 if the target file is a directory
// -5 if the target file's parent directory does not provide write permissions
// -6 for some other error
int deleteFile_NMSS(char *path)
{
    struct stat pathStat = {0};
    int ret = stat(path, &pathStat);
    if(ret == -1)
    {
        if(errno == ENOENT) // a component in the path doesn't exist
            ret = -1;
        else if(errno == EACCES) // a directory component denies search permissions
            ret = -2;
        else if(errno == ENOTDIR) // a component in the path is not a directory
            ret = -3;
        else ret = -6; // some other error
        goto exit_delete_file;
    }

    if((ret = unlink(path)) < 0)
    {
        if(errno == EISDIR) // target file is a directory
            ret = -4;
        else if(errno == EACCES) // target's parent dir does not allow write perms
            ret = -5;
        else ret = -6;
    }

    deleteInTable(path, PM);

exit_delete_file:
    char *relPath;
    if(path[0] == '/') // path is absolute
        relPath = getRelPath(path, rootDir);
    else relPath = strdup(path);
    if(ret < 0)
    {
        sem_wait(&printLock);
        printf(BRED "Could not delete file at " WHT "%s" BRED " because ", relPath + 1);
        switch(ret){
            case -1: printf("a directory component in the path does not exist.\n" CRESET); break;
            case -2: printf("a directory component in the path does not provide search permissions.\n" CRESET); break;
            case -3: printf("a directory component in the path exists but is not a directory.\n" CRESET); break;
            case -4: printf("the target file is in fact a directory.\n" CRESET); break;
            case -5: printf("the target directory's parent directory does not provide write permissions.\n" CRESET); break;
            default: printf("some error occurred.\n" CRESET); break;
        }
        sem_post(&printLock);
    }
    else printf(BGRN "Successfully deleted file at " WHT "%s \n"CRESET, relPath + 1);

    free(relPath);
    return ret;
}

// ===============================================================================================================================
// ===============================================================================================================================

/* Functions for commands issued by clients */

// Gets permissions of a file at a specified path.
// Returns 0 on success for a file.
// 1 on success for a directory -> will print total number of blocks instead of size of directory in this case.
// -1 if unable to scan directory.
int getInfoOfFile_CLSS(char *path, cl_SS_response_packet *CLResponse){

    // remove last '/' if present
    if(path[strlen(path) - 1] == '/') path[strlen(path) - 1] = '\0';

    struct dirent** info; // To store name of the file in directory.
    struct stat stats; // To retrieve information about a file.
    long blockSize = 0; // To calculate total block size of the directory:

    lstat(path, &stats); // Store info about file here.
    unsigned int checkType = stats.st_mode & S_IFMT;

    char permissions[11] = {0};
    // For the permissions:
    if(checkType == S_IFBLK) permissions[0] = 'b'; // Block Device
    else if(checkType == S_IFCHR) permissions[0] = 'c'; // Character Device
    else if(checkType == S_IFDIR) permissions[0] = 'd'; // Directory
    else if(checkType == S_IFIFO) permissions[0] = 'p'; // FIFO/pipe
    else if(checkType == S_IFLNK) permissions[0] = 'l'; // Symlink
    else if(checkType == S_IFSOCK) permissions[0] = 's'; // Socket
    else permissions[0] = '-'; // Special file type hasn't been determined, so, must be a regular file.
    permissions[1] = (stats.st_mode & S_IRUSR) ? 'r' : '-';
    permissions[2] = (stats.st_mode & S_IWUSR) ? 'w' : '-';
    permissions[3] = (stats.st_mode & S_IXUSR) ? 'x' : '-';
    permissions[4] = (stats.st_mode & S_IRGRP) ? 'r' : '-';
    permissions[5] = (stats.st_mode & S_IWGRP) ? 'w' : '-';
    permissions[6] = (stats.st_mode & S_IXGRP) ? 'x' : '-';
    permissions[7] = (stats.st_mode & S_IROTH) ? 'r' : '-';
    permissions[8] = (stats.st_mode & S_IWOTH) ? 'w' : '-';
    permissions[9] = (stats.st_mode & S_IXOTH) ? 'x' : '-';

    // For the file type:
    if(checkType == S_IFDIR) {
        int returnVal = scandir(path, &info, NULL, alphasort); // Store number of folders, if it is a directory. Returns -1 if failed, number of files if successful.
        if (returnVal == -1) {
            sem_wait(&printLock);
            fprintf(stderr, BRED "Unable to scan the directory at " WHT "%s\n" CRESET, path);
            perror(NULL);
            sem_post(&printLock);
            strcpy(CLResponse->response, strerror(errno));
            CLResponse->statusCode = -1;
            return -1;
        }

        for (int i = 0; i < returnVal; i++) {
            char filePath[PATH_MAX] = {0};
            strcat(filePath, path);
            strcat(filePath, "/");
            strcat(filePath, info[i]->d_name);
            lstat(filePath, &stats); // Store info about file here.
            blockSize += (stats.st_blocks); // Get it in terms of 512 byte chunks.
        }
        sprintf(CLResponse->response, "Permissions: %s, Total number of blocks: %ld\n", permissions, blockSize);
        CLResponse->statusCode = 1;
        printf(BGRN "Successfully obtained information on directory at " WHT "%s\n" CRESET, path);
        return 1;
    }
    else
    {
        sprintf(CLResponse->response, "Permissions: %s, Total number of bytes: %ld\n",  permissions, stats.st_size), CLResponse->statusCode = 0;
        printf(BGRN "Successfully obtained information on directory at " WHT "%s\n" CRESET, path);
    }
    return 0;
}

// Open a file to read its contents.
// Returns 0 if successful.
// Returns 1 if it is a directory (no message returned in this case).
// Returns -1 if file doesn't have read permissions for user.
// Returns -2 for other errors while fopen-ing file.
int readFromFile_CLSS(char *path, cl_SS_response_packet *CLResponse){
    struct stat stats; // To retrieve information about a file.
    lstat(path, &stats); // Store info about file here.

    if((stats.st_mode & S_IFMT) == S_IFDIR) { // The file is a directory.
        CLResponse->statusCode = 1;
        fprintf(stderr, BRED "Could not read file at " WHT "%s" BRED " as it is a directory.\n" CRESET, path);
        return 1;
    }

    if(!(stats.st_mode & S_IRUSR)) { // User doesn't have read permissions for this file.
        CLResponse->statusCode = -1;
        fprintf(stderr, BRED "Could not read file at " WHT "%s" BRED " as the user lacks read permissions.\n" CRESET, path);
        return -1;
    }

    rwlock_t *fileLock = getFileLock(path, PM);
    rwlock_acquire_readlock(fileLock);
    FILE* fd = fopen(path, "r");
    if(!fd){
        strcpy(CLResponse->response, strerror(errno));
        CLResponse->statusCode = -2;
        fclose(fd);
        rwlock_release_readlock(fileLock);
        fprintf(stderr, BRED "Could not read file at " WHT "%s" BRED " as an error occurred.\n" CRESET, path);
        return -2;
    }

    long size = stats.st_size;
    fread(CLResponse->response, sizeof(char), size, fd);
    fclose(fd);
    rwlock_release_readlock(fileLock);
    printf(BGRN "Successfully read from file at " WHT "%s\n" CRESET, path);
    return 0;
}

// Open a file to write to it.
// Returns 0 if successful.
// Returns 1 if it is a directory (no message returned in this case).
// Returns -1 if file doesn't have write permissions for user.
// Returns -2 for other errors while fopen-ing file.
int writeToFile_CLSS(char *path, char *message, cl_SS_response_packet *CLResponse){
    struct stat stats; // To retrieve information about a file.
    lstat(path, &stats); // Store info about file here.

    if((stats.st_mode & S_IFMT) == S_IFDIR) { // The file is a directory.
        CLResponse->statusCode = 1;
        fprintf(stderr, BRED "Could not write to file at " WHT "%s" BRED " as it is a directory.\n" CRESET, path);
        return 1;
    }

    if(!(stats.st_mode & S_IWUSR)) { // User doesn't have write permissions for this file.
        CLResponse->statusCode = -1;
        fprintf(stderr, BRED "Could not write to file at " WHT "%s" BRED " as the user lacks write permissions.\n" CRESET, path);
        return -1;
    }

    rwlock_t *fileLock = getFileLock(path, PM);
    rwlock_acquire_writelock(fileLock);
    FILE* fd = fopen(path, "w");
    if(!fd){
        strcpy(CLResponse->response, strerror(errno));
        CLResponse->statusCode = -2;
        fclose(fd);
        rwlock_release_writelock(fileLock);
        fprintf(stderr, BRED "Could not write to file at " WHT "%s" BRED " as an error occurred.\n" CRESET, path);
        return -2;
    }

    fwrite(message, sizeof(char), strlen(message), fd);
    fclose(fd);
    rwlock_release_writelock(fileLock);
    printf(BGRN "Successfully wrote to file at " WHT "%s\n" CRESET, path);
    return 0;
}

// Open a file to append to it.
// Returns 0 if successful.
// Returns 1 if it is a directory (no message returned in this case).
// Returns -1 if file doesn't have write permissions for user.
// Returns -2 for other errors while fopen-ing file.
int appendToFile_CLSS(char *path, char *message,  cl_SS_response_packet *CLResponse){
    struct stat stats; // To retrieve information about a file.
    lstat(path, &stats); // Store info about file here.

    if((stats.st_mode & S_IFMT) == S_IFDIR) { // The file is a directory.
        CLResponse->statusCode = 1;
        fprintf(stderr, BRED "Could not append to file at " WHT "%s" BRED " as it is a directory.\n" CRESET, path);
        return 1;
    }

    if(!(stats.st_mode & S_IWUSR)) { // User doesn't have write permissions for this file.
        CLResponse->statusCode = -1;
        fprintf(stderr, BRED "Could not append to file at " WHT "%s" BRED " as the user lacks write permissions.\n" CRESET, path);
        return -1;
    }

    rwlock_t *fileLock = getFileLock(path, PM);
    rwlock_acquire_writelock(fileLock);
    FILE* fd = fopen(path, "a");
    if(!fd){
        strcpy(CLResponse->response, strerror(errno));
        CLResponse->statusCode = -2;
        fclose(fd);
        rwlock_release_writelock(fileLock);
        fprintf(stderr, BRED "Could not append to file at " WHT "%s" BRED " as an error occurred.\n" CRESET, path);
        return -2;
    }

    fwrite(message, sizeof(char), strlen(message), fd);
    fclose(fd);
    rwlock_release_writelock(fileLock);
    printf(BGRN "Successfully append to file at " WHT "%s\n" CRESET, path);
    return 0;
}

// ===============================================================================================================================
// ===============================================================================================================================

/* Networking functions for commands issued by NM */

// executes command issued by NM
// forwards the return value of the operation function
int processNMRequest(NM_SS_request_packet *reqPacket, NM_SS_response_packet *respPacket)
{
    if(strcmp(reqPacket->operation, "create") == 0)
    {
        if(reqPacket->sourcePath[strlen(reqPacket->sourcePath) - 1] == '/')
        {
            printf(BYEL "Received request from NM to create a directory at " WHT "%s\b\n" CRESET, reqPacket->sourcePath);
            respPacket->statusCode = createNewDirectory_NMSS(reqPacket->sourcePath);
        }
        else
        {
            printf(BYEL "Received request from NM to create a file at " WHT "%s\n" CRESET, reqPacket->sourcePath);
            respPacket->statusCode = createNewFile_NMSS(reqPacket->sourcePath);
        }
    }
    else if(strcmp(reqPacket->operation, "delete") == 0)
    {
        if(reqPacket->sourcePath[strlen(reqPacket->sourcePath) - 1] == '/')
        {
            printf(BYEL "Received request from NM to delete directory at " WHT "%s\b\n" CRESET, reqPacket->sourcePath);
            respPacket->statusCode = deleteDirectory_NMSS(reqPacket->sourcePath);
        }
        else
        {
            printf(BYEL "Received request from NM to delete a file at " WHT "%s\n" CRESET, reqPacket->sourcePath);
            respPacket->statusCode = deleteFile_NMSS(reqPacket->sourcePath);
        }
    }

    return respPacket->statusCode;
}

// main function for a thread that keeps running forever, closing only when the SS closes (or crashes, L)
// keeps listening for a connection from the NM (since we don't wanna keep it alive all the time)
// receives a request packet, proceses that request and returns the response packet
// returns only when an error occurs, or when main() exists.
void *commWithNM(void *)
{
    struct sockaddr_in NM_Addr;
    socklen_t NM_AddrSize;
    int peerSock;

    NM_SS_request_packet reqPacket;
    NM_SS_response_packet respPacket;

    if(listen(NM_SockFD, 5) < 0)
    {
        fprintf(stderr, BRED "ERROR: listen (%d) : %s\n" CRESET, errno, strerror(errno));
        return NULL;
    }

    while(true)
    {
        NM_AddrSize = sizeof(NM_Addr);
        if((peerSock = accept(NM_SockFD, (struct sockaddr*)&NM_Addr, &NM_AddrSize)) < 0)
        {
            fprintf(stderr, BRED "Failed to accept connection request from NM\n" CRESET);
            close(peerSock);
            break;
        }

        // Bookkeeping
        sem_wait(&printLock);
        printf(BMAG "NM has successfully connected for a command\n");
        printf("to my IP: " WHT "%s" BMAG " and port: " WHT "%d\n" BMAG, SS_IP, myInfo.nmPort);
        printf("from its IP: " WHT "%s" BMAG " and port: " WHT "%d\n" CRESET, "127.0.0.1", ntohs(NM_Addr.sin_port));
        sem_post(&printLock);

        reqPacket = (NM_SS_request_packet){0};
        if(recv(peerSock, &reqPacket, sizeof(reqPacket), 0) < 0)
        {
            fprintf(stderr, BRED "Failed to receive request packet from NM\n" CRESET);
            close(peerSock);
            break;
        }
        printf(BBLU "Received command request from NM\n"CRESET);

        respPacket = (NM_SS_response_packet){0};
        if(strcmp(reqPacket.operation, "copysrc") == 0) respPacket.statusCode = zipAndSendFile_NMSS(peerSock, reqPacket.sourcePath);
        else if(strcmp(reqPacket.operation, "copydest") == 0)
        {
            respPacket.statusCode = recAndUnzip_NMSS(peerSock, reqPacket.sourcePath);
            // add files to hashtable
            addAllSubPaths(reqPacket.sourcePath, NULL, 1);
            insertInTable(reqPacket.sourcePath, PM);
        }
        else if (processNMRequest(&reqPacket, &respPacket) < 0)
            fprintf(stderr, BRED "Failure during processing of the request\n" CRESET); // just notify, don't die
        else
            printf(BBLU "Successfully processed command request from NM\n"CRESET);

        if (send(peerSock, &respPacket, sizeof(respPacket), 0) < 0) {
            fprintf(stderr, BRED "Failed to send response packet to NM\n\n" CRESET);
            close(peerSock);
            break;
        }
        printf(BBLU "Successfully sent response to NM\n\n"CRESET);

        close(peerSock);
    }

    return NULL;
}

// ===============================================================================================================================
// ===============================================================================================================================

/* Networking functions for commands issued by clients */

// processes client requests and returns the status code
int processClientRequest(cl_SS_request_packet *reqPacket, cl_SS_response_packet *respPacket)
{
    if(strcmp(reqPacket->operation, "read") == 0)
    {
        printf(BYEL "Received request from a client to read from file at " WHT "%s\n", reqPacket->path);
        return readFromFile_CLSS(reqPacket->path, respPacket);
    }
    else if(strcmp(reqPacket->operation, "write") == 0)
    {
        sem_wait(&printLock);
        printf(BYEL "Received request from a client to write to file at " WHT "%s" BYEL" with the message:\n", reqPacket->path);
        printf(BLU "%s" CRESET, reqPacket->message);
        sem_post(&printLock);
        return writeToFile_CLSS(reqPacket->path, reqPacket->message, respPacket);
    }
    else if(strcmp(reqPacket->operation, "getinfo") == 0)
    {
        if(reqPacket->path[strlen(reqPacket->path) - 1] == '/')
        {
            printf(BYEL "Received request from a client to get information on directory at " WHT "%s\b\n" CRESET, reqPacket->path);
            reqPacket->path[strlen(reqPacket->path) - 1] = '\0';
        }
        else
        {
            printf(BYEL "Received request from a client to get information on file at " WHT "%s\n" CRESET, reqPacket->path);
            return getInfoOfFile_CLSS(reqPacket->path, respPacket);
        }
    }
    else if(strcmp(reqPacket->operation, "append") == 0)
    {
        sem_wait(&printLock);
        printf(BYEL "Received request from a client to append to file at " WHT "%s" BYEL" with the message:\n", reqPacket->path);
        printf(BLU "%s\n" CRESET, reqPacket->message);
        sem_post(&printLock);
        return appendToFile_CLSS(reqPacket->path, reqPacket->message, respPacket);
    }

    return respPacket->statusCode;
}

// thread that is created to process client request.
// it receives the client's request packet, processes the command issued and returns response packet
void *commWithClient(void *args)
{
    int newClientSockFD;
    struct sockaddr_in clientAddr;

    // get a new socket by connecting to client on their new port (client is acting as TCP "server" now)
    newClientSockFD = connectViaTCP(((cl_info*)args)->ipAddress, ((cl_info*)args)->clientPort, &clientAddr);

    // Bookkeeping
    struct sockaddr_in tempInfo = {0};
    socklen_t tempInfoSize = sizeof(tempInfo);
    if(getsockname(newClientSockFD, (struct sockaddr *) &tempInfo, &tempInfoSize) < 0){
        perror(BRED "ERROR: Error occurred while retrieving port details on socket for NM: " CRESET);
        goto exit_comm_cl;
    }
    sem_wait(&printLock);
    printf(BYEL "Successfully connected to a client for a command\n");
    printf("from my IP: " WHT "%s" BYEL " and port: " WHT "%d\n", SS_IP, ntohs(tempInfo.sin_port)); // SS is TCP client here
    printf("at its IP: " WHT "%s" BYEL " and port: " WHT "%d\n", ((cl_info*)args)->ipAddress, ((cl_info*)args)->clientPort);
    sem_post(&printLock);

    cl_SS_request_packet reqPacket = {0};
    cl_SS_response_packet respPacket = {0};

    if(recv(newClientSockFD, &reqPacket, sizeof(cl_SS_request_packet), 0) < 0)
    {
        fprintf(stderr, BRED "Failed to receive request packet from client\n" CRESET);
        goto exit_comm_cl;
    }
    printf(BGRN "Received command request from client\n"CRESET);

    if(processClientRequest(&reqPacket, &respPacket) < 0)
        fprintf(stderr, BRED "Failure during processing of the request\n" CRESET);
    else
        printf(BGRN "Successfuly processed command request from client\n"CRESET);

    if(send(newClientSockFD, &respPacket, sizeof(respPacket), 0) < 0)
    {
        fprintf(stderr, BRED "Failed to send response packet to client\n" CRESET);
        goto exit_comm_cl;
    }
    printf(BGRN "Successfully sent response to client\n\n"CRESET);
    
exit_comm_cl:
    close(newClientSockFD);
    return NULL;
}

// accept incoming connections from clients
// once we get one, create a thread for that client to process their request
void checkForClients()
{
    struct sockaddr_in CL_Addr;
    socklen_t CL_AddrSize;
    int peerSock;

    if(listen(Client_SockFD, 1) < 0)
    {
        fprintf(stderr, BRED "ERROR: listen (%d) : %s\n" CRESET, errno, strerror(errno));
        return;
    }

    while(true)
    {
        // accept connections from clients
        CL_AddrSize = sizeof(CL_Addr);
        if((peerSock = accept(Client_SockFD, (struct sockaddr*)&CL_Addr, &CL_AddrSize)) < 0)
        {
            fprintf(stderr, BRED "Failed to accept connection request from client\n" CRESET);
            close(peerSock);
            break;
        }

        // Bookkeeping
        char clIPstr[INET_ADDRSTRLEN]; // get client IP
        if (inet_ntop(AF_INET, &CL_Addr.sin_addr, clIPstr, sizeof(clIPstr)) == NULL) {
            perror(BRED "inet_ntop" CRESET);
            close(peerSock);
            break;
        }
        sem_wait(&printLock);
        printf(BYEL "A client has successfully connected for a command\n");
        printf("to my IP: " WHT "%s" BYEL " and port: " WHT "%d\n", SS_IP, myInfo.clientPort); // SS is TCP server here
        printf("from its IP: " WHT "%s" BYEL " and port: " WHT "%d\n", clIPstr, ntohs(CL_Addr.sin_port));
        sem_post(&printLock);

        // get client info
        cl_info *clientInfo = (cl_info*)calloc(1, sizeof(cl_info));
        if(recv(peerSock, &clientInfo[0], sizeof(cl_info), 0) < 0)
        {
            fprintf(stderr, BRED "Failed to receive info packet from client\n" CRESET);
            close(peerSock);
            break;
        }

        close(peerSock); // close comm on this socket, SS will comm on a new socket in the thread as TCP server-client roles get flipped

        pthread_t client_t;
        pthread_create(&client_t, NULL, commWithClient, clientInfo);
    }
}

// ===============================================================================================================================
// ===============================================================================================================================


int main(int argc, char *argv[])
{
    pthread_t NM_comm_t;

    // setup 
    PM = createPathMap(HT_SIZE);
    rootDir = getcwd(NULL, 0);
    initLocks();

    printf(BWHT "Welcome to the Storage Server!\n\n" CRESET);

    // mark as backup only if second CL arg is "b"
    if(initialiseSS((argc > 1) ? (strcmp(argv[1], "b") == 0) : 0) < 0)
    {
        fprintf(stderr, BRED "Failed to initialise Storage Server. Committing suicide...\n\n" CRESET);
        goto exit;
    }

    pthread_create(&NM_comm_t, NULL, commWithNM, NULL);
    
    checkForClients();

    // remember to cleanup

    pthread_join(NM_comm_t, NULL);
exit:
    sem_wait(&printLock);
    printf(BWHT "Closing...\n" CRESET);
    sem_post(&printLock);
}