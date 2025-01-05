#include "headers.h"
#include <sys/wait.h>

/* functions to implement copying, defined in copy.c*/
int execCmd(char* cmd);
int zipAndSendFile_NMSS(int serverSockFD, char* path);
int recAndUnzip_NMSS(int clientSockFD, char* path);
