#include "ConcurrencySS.h"

void rwlock_init(rwlock_t *rw)
{
    rw->readers = 0;
    sem_init(&rw->serviceQueue, 0, 1);
    sem_init(&rw->lock, 0, 1);
    sem_init(&rw->writelock, 0, 1);
}

void rwlock_acquire_readlock(rwlock_t * rw)
{
    sem_wait(&rw->serviceQueue);
    sem_wait(&rw->lock);
    if(++(rw->readers) == 1) sem_wait(&rw->writelock); // first reader gets writelock
    sem_post(&rw->lock);
}

void rwlock_release_readlock(rwlock_t * rw)
{
    sem_wait(&rw->lock);
    if(--(rw->readers) == 0) sem_post(&rw->writelock); // last reader lets it go
    sem_post(&rw->lock);
    sem_post(&rw->serviceQueue);
}

void rwlock_acquire_writelock(rwlock_t * rw)
{
    sem_wait(&rw->serviceQueue);
    sem_wait(&rw->writelock);
}

void rwlock_release_writelock(rwlock_t * rw)
{
    sem_post(&rw->writelock);
    sem_post(&rw->serviceQueue);
}
