#ifndef __PATHHASHSS_H_
#define __PATHHASHSS_H_

/* modified PathMaps.h*/

#include <assert.h>
#include "ConcurrencySS.h"

#define HT_SIZE 103 // reasonably sized prime for our use case I think

struct node {
  char* path;
  rwlock_t fileLock;
  struct node* next;
};

typedef struct node* ptrNode;

typedef struct {
  int maxTableSize;
  ptrNode* table;
} hashStruct;

typedef hashStruct* PathMapSS;

PathMapSS createPathMap(int maxSize);
ptrNode findInTable(char* path, PathMapSS ht);
ptrNode insertInTable(char* path, PathMapSS ht);
rwlock_t *getFileLock(char* path, PathMapSS ht);
bool deleteInTable(char* path, PathMapSS ht);

int getHash(char* path, PathMapSS ht);
void freePathMap(PathMapSS PM);
void printHT(PathMapSS PM);

#endif