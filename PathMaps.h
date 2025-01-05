#ifndef __PATH_MAPS_H
#define __PATH_MAPS_H

#include "NamingServer.h"
#include <assert.h>

#define HT_SIZE 103 // reasonably sized prime for our use case I think

struct node {
  char* path;
  int storageServerIndex;
  struct node* next;
};

typedef struct node* ptrNode;

typedef struct {
  int maxTableSize;
  ptrNode* table;
} hashStruct;

typedef hashStruct* PathMap;

PathMap createPathMap(int maxSize);
ptrNode findInTable(char* path, PathMap ht);
ptrNode insertInTable(char* path, int serverStorageIndex, PathMap ht);
int getStorageServerIndex(char* path, PathMap ht);
bool deleteInTable(char* path, PathMap ht);

int getHash(char* path, PathMap ht);
void freePathMap(PathMap PM);
void printHT(PathMap PM);

#endif