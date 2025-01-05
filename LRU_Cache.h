#ifndef __LRU_CACHE_H
#define __LRU_CACHE_H

#include "headers.h"

#define CACHE_SIZE 10

struct Node {
  struct Node* prev;
  struct Node* next;
  cl_NM_request_packet request;
  cl_SS_response_packet response;
};

typedef struct Node cacheNode;

typedef struct {
  int count;
  int maxNum;
  cacheNode* front;
  cacheNode* rear;
} QueueStruct;

typedef QueueStruct* LRU_Cache;

cacheNode* createCacheNode(cl_NM_request_packet req, cl_SS_response_packet resp);
cacheNode* findNode(LRU_Cache Q, cl_NM_request_packet req);
LRU_Cache createLRUCache();
bool isCacheFull(LRU_Cache q);
bool isCacheEmpty(LRU_Cache q);
void deQueue(LRU_Cache Q);
void addCacheNode(LRU_Cache Q, cl_NM_request_packet req, cl_SS_response_packet resp);
bool requestIsSame(cl_NM_request_packet req1, cl_NM_request_packet req2);
cacheNode* findNode(LRU_Cache Q, cl_NM_request_packet req);
void removePath(LRU_Cache Q, char* path);
void referencePage(LRU_Cache Q, cl_NM_request_packet req, cl_SS_response_packet resp);

#endif