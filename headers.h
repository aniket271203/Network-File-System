#ifndef _HEADERS_H
#define _HEADERS_H

/*
Headers common to all files, import into the header
files for each of NM, SS etc
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>

// ----------------------------------------------------------------------------

// Global macros
#define MSG_LEN 8192
#define NM_CLIENT_PORT 1234
#define PATH_MAX 4096
#define IP_LEN 16
#define NM_SS_PORT 1235
#define MAX_BUF_LEN 4096
#define MAX_OPER_LEN 9 // Max length of the operation (client-sided, such as getinfo, read, write etc.)

// Global variables
extern char NM_IP[];

// ----------------------------------------------------------------------------

// data sent from SS to NM on initialisation
typedef struct {
  char* ipAddress;
  int nmPort;
  int clientPort;
  int numAccessiblePaths;
  char** accessiblePaths;
  bool isBackup;
} ss_info;

// usage:
// SS wannabe backup, NM rejects :                                      addedIndices = NULL, backupSuccess = 0
// SS wannabe backup, NM accepts :                                      addedIndices = NULL, backupSuccess = 1
// SS doesn't wannabe backup, NM rejects (if  < 2 BSS are there) :      addedIndices = bool* (all false), backupSuccess = 0
// SS doesn't wannabe backup, NM accepts (regular SS) :                 addedIndices = bool* (accepted paths true), backupSuccess = 1
typedef struct {
    bool* addedIndices;
    bool backupSuccess;
} added_paths;

typedef struct {
  int clientPort;
  char ipAddress[IP_LEN];
} cl_info;

typedef struct {
    char operation[MAX_OPER_LEN];
    char sourcePath[PATH_MAX];
    char destinationPath[PATH_MAX];
} cl_NM_request_packet;

typedef struct {
    char SS_IP[IP_LEN];
    int SS_Port;
    char message[MSG_LEN];
    int statusCode;
} cl_NM_response_packet;

typedef struct {
    char operation[MAX_OPER_LEN];
    char path[PATH_MAX];
    char message[MSG_LEN];
} cl_SS_request_packet;

typedef struct {
    char response[MSG_LEN];
    int statusCode;
} cl_SS_response_packet;

// request for operations -> create and destroy
typedef struct {
    char operation[MAX_OPER_LEN];
    char sourcePath[PATH_MAX];
} NM_SS_request_packet;

// response for operations -> create and destroy
typedef struct {
    int statusCode;
} NM_SS_response_packet;

// ----------------------------------------------------------------------------

// Color coding macros:
// ANSI Color codes: https://gist.github.com/RabaDabaDoba/145049536f815903c79944599c6f952a

// Regular text
#define BLK "\e[0;30m"
#define RED "\e[0;31m"
#define GRN "\e[0;32m"
#define YEL "\e[0;33m"
#define BLU "\e[0;34m"
#define MAG "\e[0;35m"
#define CYN "\e[0;36m"
#define WHT "\e[0;37m"

// Bold text
#define BBLK "\e[1;30m"
#define BRED "\e[1;31m"
#define BGRN "\e[1;32m"
#define BYEL "\e[1;33m"
#define BBLU "\e[1;34m"
#define BMAG "\e[1;35m"
#define BCYN "\e[1;36m"
#define BWHT "\e[1;37m"

// To reset color
#define CRESET "\e[0m"

#endif