#include "LRU_Cache.h"

cacheNode* createCacheNode(cl_NM_request_packet req, cl_SS_response_packet resp) {
  cacheNode* cNode = (cacheNode*)malloc(sizeof(cacheNode));

  memset(cNode->request.operation, '\0', sizeof(char) * MAX_OPER_LEN);
  strncpy(cNode->request.operation, req.operation, MAX_OPER_LEN);
  memset(cNode->request.sourcePath, '\0', sizeof(char) * PATH_MAX);
  strncpy(cNode->request.sourcePath, req.sourcePath, PATH_MAX);
  memset(cNode->request.destinationPath, '\0', sizeof(char) * PATH_MAX);
  strncpy(cNode->request.destinationPath, req.destinationPath, PATH_MAX);

  memset(cNode->response.response, '\0', sizeof(char) * MSG_LEN);
  strncpy(cNode->response.response, resp.response, MSG_LEN);
  cNode->response.statusCode = resp.statusCode;

  cNode->prev = cNode->next = NULL;
  return cNode;
}

LRU_Cache createLRUCache() {
  LRU_Cache q = (LRU_Cache)malloc(sizeof(QueueStruct));
  q->count = 0;
  q->front = q->rear = NULL;
  q->maxNum = CACHE_SIZE;

  return q;
}

bool isCacheFull(LRU_Cache q) { return q->count == q->maxNum; }

bool isCacheEmpty(LRU_Cache q) { return q->rear == NULL; }

void deQueue(LRU_Cache Q) {
  if (isCacheEmpty(Q)) {
    return;
  }

  if (Q->front == Q->rear) {
    Q->front = NULL;
  }

  cacheNode* prevRear = Q->rear;
  Q->rear = Q->rear->prev;

  if (Q->rear) {
    Q->rear->next = NULL;
  }

  free(prevRear);

  Q->count -= 1;
}

void addCacheNode(LRU_Cache Q, cl_NM_request_packet req, cl_SS_response_packet resp) {
  if (isCacheFull(Q)) {
    deQueue(Q);
  }

  cacheNode* newNode = createCacheNode(req, resp);
  newNode->next = Q->front;

  if (isCacheEmpty(Q)) {
    Q->rear = Q->front = newNode;
  } else {
    Q->front->prev = newNode;
    Q->front = newNode;
  }

  Q->count += 1;
}

bool requestIsSame(cl_NM_request_packet req1, cl_NM_request_packet req2) {
  return strcmp(req1.operation, req2.operation) == 0 &&
         strcmp(req1.sourcePath, req2.sourcePath) == 0 &&
         strcmp(req1.destinationPath, req2.destinationPath) == 0;
}

cacheNode* findNode(LRU_Cache Q, cl_NM_request_packet req) {
  cacheNode* nodePtr = Q->front;
  while (nodePtr) {
    if (requestIsSame(req, nodePtr->request)) {
      return nodePtr;
    }
    nodePtr = nodePtr->next;
  }
  return NULL;
}

void removePath(LRU_Cache Q, char* path) {
  cacheNode* nodePtr = Q->front;
  while (nodePtr) {
    if (strcmp(path, nodePtr->request.sourcePath) == 0) {
      if (nodePtr->prev != NULL) {
        nodePtr->prev->next = nodePtr->next;
      } else {
        Q->front = nodePtr->next;
      }

      if (nodePtr->next != NULL) {
        nodePtr->next->prev = nodePtr->prev;
      } else {
        Q->rear = nodePtr->prev;
      }

      cacheNode* nextNode = nodePtr->next;
      free(nodePtr);
      nodePtr = nextNode;

      Q->count -= 1;
    } else {
      nodePtr = nodePtr->next;
    }
  }
}

void referencePage(LRU_Cache Q, cl_NM_request_packet req, cl_SS_response_packet resp) {
  cacheNode* nodePtr = findNode(Q, req);

  if (nodePtr == NULL) {
    addCacheNode(Q, req, resp);
  } else if (nodePtr != Q->front) {
    nodePtr->prev->next = nodePtr->next;
    if (nodePtr->next) {
      nodePtr->next->prev = nodePtr->prev;
    }

    if (nodePtr == Q->rear) {
      Q->rear = nodePtr->prev;
      Q->rear->next = NULL;
    }

    nodePtr->next = Q->front;
    nodePtr->prev = NULL;

    nodePtr->next->prev = nodePtr;
    Q->front = nodePtr;
  }
}

void printCacheState(LRU_Cache Q) {
  cacheNode* nodePtr = Q->front;
  while (nodePtr) {
    printf("%s ", nodePtr->request.sourcePath);
    nodePtr = nodePtr->next;
  }
  printf("\n");
}
