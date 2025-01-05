#ifndef __CONCURRENCYSS_H_
#define __CONCURRENCYSS_H_

#include "headers.h"
#include <semaphore.h>

typedef struct {
    sem_t serviceQueue;
    sem_t lock;
    sem_t writelock;
    int readers;
} rwlock_t;

void rwlock_init(rwlock_t *rw);
void rwlock_acquire_readlock(rwlock_t * rw);
void rwlock_release_readlock(rwlock_t * rw);
void rwlock_acquire_writelock(rwlock_t * rw);
void rwlock_release_writelock(rwlock_t * rw);

#endif