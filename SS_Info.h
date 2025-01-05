#ifndef __SS_INFO_H
#define __SS_INFO_H

#include "NamingServer.h"

void addStorageServer(StorageServers *SS, ss_info *newServer);
void freeSSInfo(StorageServers *SS, int index);
StorageServers *initStorageServer(int maxSize);
void freeStorageServer(StorageServers *SS);

#endif