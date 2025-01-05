#include "PathHashSS.h"

PathMapSS createPathMap(int maxSize) {
  PathMapSS ht = (PathMapSS)malloc(sizeof(hashStruct));
  if (!ht) return NULL;

  ht->maxTableSize = maxSize;
  ht->table = (ptrNode*)malloc(sizeof(ptrNode) * maxSize);
  assert(ht->table);
  if (!(ht->table)) return NULL;  // free data allocated so far

  for (int i = 0; i < maxSize; i++) {
    ht->table[i] = (ptrNode)malloc(sizeof(struct node));
    assert(ht->table[i]);

    (ht->table[i])->path = NULL;
    (ht->table[i])->next = NULL;
  }

  return ht;
}

ptrNode findInTable(char* path, PathMapSS ht) {
  int hashVal = getHash(path, ht);
  ptrNode node = (ht->table[hashVal])->next;

  while (node && strcmp(path, node->path)) {
    node = node->next;
  }

  return node;
}

ptrNode insertInTable(char* path, PathMapSS ht) {
  if (findInTable(path, ht)) {
    return NULL;
  } else {
    int hashVal = getHash(path, ht);
    ptrNode bucket = ht->table[hashVal];

    ptrNode newNode = (ptrNode)malloc(sizeof(struct node));
    assert(newNode);
    newNode->path = strdup(path);
    rwlock_init(&newNode->fileLock);

    newNode->next = bucket->next;
    bucket->next = newNode;

    return newNode;
  }
}

bool deleteInTable(char* path, PathMapSS ht) {
  ptrNode bucketPtr = ht->table[getHash(path, ht)];

  ptrNode slow = bucketPtr;
  ptrNode fast = bucketPtr->next;

  while (fast && strcmp(fast->path, path)) {
    slow = slow->next;
    fast = fast->next;
  }
  if (!fast) return false;

  slow->next = fast->next;
  free(fast);

  return true;
}

int getHash(char* path, PathMapSS PM) {
  int key = 0;

  int len = strlen(path);
  for (int i = 0; i < len; i++) key = (key + path[i]) % PM->maxTableSize;

  return key;
}

void freePathMap(PathMapSS PM) {
  for (int i = 0; i < PM->maxTableSize; i++) {
    ptrNode node = (PM->table[i]);

    while (node) {
      ptrNode next = node->next;
      free(node->path);
      free(node);

      node = next;
    }
  }
  free(PM->table);
}

void printHT(PathMapSS PM) {
  printf("Current State of PM\n");
  for (int i = 0; i < PM->maxTableSize; i++) {
    ptrNode node = (PM->table[i])->next;
    if (!node) continue;

    printf("%d ", i);
    while (node) {
      printf("%s ", node->path);
      node = node->next;
    }
    printf("\n");
  }
}

rwlock_t *getFileLock(char* path, PathMapSS ht) {
  ptrNode node;
  if ((node = findInTable(path, ht))) {
    return &node->fileLock;
  } else {
    return NULL;
  }
}
