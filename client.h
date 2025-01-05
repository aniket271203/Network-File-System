#ifndef __CLIENT_H
#define __CLIENT_H

#include "headers.h"

// General functions:
int establishConnectionWithServer(char IP[], int port, struct sockaddr_in* clientInfo, struct sockaddr_in* newServerInfo);
int connectToServer(char IP[], int port, struct sockaddr_in* serverInfo);
int forwardRequestToNM(cl_NM_request_packet* reqToNM, cl_NM_response_packet* responseFromNM, char clientRequest[]);
int forwardRequestToSS(char clientRequest[], struct sockaddr_in* SSInfo, cl_NM_response_packet* responseFromNM, cl_SS_response_packet* responseFromSS, char message[]);

// Client request functions:
void sendHelp();
void getFileInfo(char clientRequest[], const char* arg);
void readFromFile(char clientRequest[], const char* arg);
void writeToFile(char clientRequest[], const char* arg);
void appendToFile(char clientRequest[], const char* arg);
void deleteFile(char clientRequest[], const char* arg);
void createFile(char clientRequest[], const char* arg);
void copyFile(char clientRequest[], char* arg);
void processRequest(char clientRequest[]);


#define REQUEST_TIMEOUT 1   // Timeout for any request.
#define REATTEMPT_WAIT 1    // Wait before reattempting connection.
#define NUM_RETRIES 10      // Number of times a connection is retried when it fails to connect.

#endif //__CLIENT_H
