#ifndef __STORAGE_SERVER_H
#define __STORAGE_SERVER_H

#include <pthread.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <dirent.h>

#include "headers.h"
#include "utils.h"
#include "PathHashSS.h"
#include "copy.h"

#define SS_IP "127.0.0.1"
#define CLIENT_IP "127.0.0.1"

ss_info myInfo; // sent to NM on initialisation
int NM_SockFD; // socket bound to port for NM
int Client_SockFD; // socket bound to port for clients
const char *rootDir;
PathMapSS PM;

/* locks and semaphores */
sem_t printLock;

/* setup functions */

void fillMyInfo(int portForNM, int portForClients, int isBackup);
int initialiseSS(int isBackup);

/* functions for commands issued by NM */

int createNewDirectory_NMSS(char *path);
int createNewFile_NMSS(char *path);
int deleteDirectory_NMSS(char *path); // like rm -rf
int deleteFile_NMSS(char *path);

/* functions for commands issued by clients */
int getInfoOfFile_CLSS(char *path, cl_SS_response_packet *CLResponse);
int readFromFile_CLSS(char *path, cl_SS_response_packet *CLResponse);
int writeToFile_CLSS(char *path, char *message, cl_SS_response_packet *CLResponse);
int appendToFile_CLSS(char *path, char *message,  cl_SS_response_packet *CLResponse);


#define REATTEMPT_WAIT 1

#endif

// 1. read delete, path handling (getrelpath(), getabspath())