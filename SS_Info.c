#include "SS_Info.h"

void addStorageServer(StorageServers *SS, ss_info *newServer) {
  if (SS->storageServerCount == SS->maxStorageCount) {
    SS->maxStorageCount *= 2;
    SS->storageServers = (ss_info **)realloc(SS->storageServers, (sizeof(ss_info *) * SS->maxStorageCount));
    SS->isDead = (bool *) realloc(SS->isDead, (sizeof(bool *) * SS->maxStorageCount));
    for (int s = SS->maxStorageCount / 2 + 1; s < SS->maxStorageCount; s++) {
      SS->storageServers[s] = (ss_info *)malloc(sizeof(ss_info));
      assert(SS->storageServers[s]);
    }
  }
  SS->storageServers[SS->storageServerCount++] = newServer;
}

void freeSSInfo(StorageServers *SS, int index) {
  free(SS->storageServers[index]->accessiblePaths);
  free(SS->storageServers[index]->ipAddress);
  free(SS->storageServers[index]);
}

StorageServers *initStorageServer(int maxSize) {
  StorageServers *S = (StorageServers *)malloc(sizeof(StorageServers));
  assert(S);
    S->maxStorageCount = maxSize;
    S->storageServerCount = 0;
    S->isDead = (bool*)calloc(S->maxStorageCount, sizeof(bool));
    assert(S->isDead);

  S->storageServers = (ss_info **)malloc(sizeof(ss_info *) * (S->maxStorageCount));
  assert(S->storageServers);
  for (int i = 0; i < maxSize; i++) {
    S->storageServers[i] = (ss_info *)malloc(sizeof(ss_info));
    assert(S->storageServers[i]);
  }

  return S;
}

void freeStorageServer(StorageServers *SS) {
  for (int i = 0; i < SS->storageServerCount; i++) {
    freeSSInfo(SS, i);
  }
  free(SS->isDead);
  free(SS->storageServers);
  free(SS);
}