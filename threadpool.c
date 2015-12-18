#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include "threadpool.h"

pthread_t* initThreads(threadpool*, int);
void enqueue_job(threadpool*, work_t*);

//TODO add error handling for pthread methods (such as pthread_cond etc)


/******************************************************************************/
/******************************************************************************/
/******************************************************************************/

threadpool* create_threadpool(int num_threads_in_pool) {

        threadpool* pool = (threadpool*)calloc(1, sizeof(threadpool));
        if(pool == NULL) {
                perror("calloc");
                exit(-1);
        }

        pool->num_threads = num_threads_in_pool;
        pool->qsize = 0;

        pool->threads = initThreads(pool, num_threads_in_pool);
        pool->qhead = NULL;
        pool->qtail = NULL;
        if(pthread_mutex_init(&pool->qlock, NULL)) {
                perror("pthread_mutex_init");
                exit(-1);
        }
        if(pthread_cond_init(&pool->q_not_empty, NULL)) {
                perror("pthread_cond_init");
                exit(-1);
        }
        if(pthread_cond_init(&pool->q_empty, NULL)) {
                perror("pthread_cond_init");
                exit(-1);
        }
        pool->shutdown = 0;
        pool->dont_accept = 0;

        return pool;
}

/*********************************/
/*********************************/
/*********************************/

pthread_t* initThreads(threadpool* pool, int num_of_threads) {

        pthread_t* threads = (pthread_t*)calloc(num_of_threads, sizeof(pthread_t));
        if(threads == NULL) {
                perror("calloc");
                exit(-1);
        }

        int i;
        for(i = 0; i < num_of_threads; i++) {
                //TODO check do_work() has the correct param
                //pool in do_work()? or in arg?
                //pthread_create(*thread, *attr, void *(*start_routine) (void *), void *arg);

                if(pthread_create(&threads[i], NULL, do_work(pool), NULL)) {
                        perror("pthread_create");
                        exit(-1);
                }
        }

        return threads;
}

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/


void dispatch(threadpool* from_me, dispatch_fn dispath_to_here, void* arg) {

        if(from_me->dont_accept)
                return;

        work_t* new_job = (work_t*)calloc(1, sizeof(work_t));
        if(new_job == NULL) {
                perror("calloc");
                exit(-1);
        }

        new_job->routine = dispath_to_here;
        new_job->arg = arg;
        new_job->next = NULL;

        pthread_mutex_lock(&from_me->qlock);

        enqueue_job(from_me, new_job);

        pthread_mutex_unlock(&from_me->qlock);
}

/*********************************/
/*********************************/
/*********************************/

void enqueue_job(threadpool* pool, work_t* job) {

        if(pool->qsize == 0) {

                pool->qhead = job;
                pool->qtail = job;
                pool->qsize++;
                pthread_cond_signal(&pool->q_empty);
                return;
        }

        pool->qtail->next = job;
        pool->qtail = job;
        pool->qsize++;
        return;

}

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/

void* do_work(void* p) {

        // if(p == NULL)
        //         pthread_exit(0);

        threadpool* pool = (threadpool*) p;
        pthread_mutex_t* qlock = &pool->qlock;

        while(1) {

                pthread_mutex_lock(qlock);

                if(pool->shutdown) {
                        pthread_mutex_unlock(qlock);
                        pthread_exit(0);
                }

                if(!pool->qsize)
                        pthread_cond_wait(&pool->q_empty, qlock);

                if(pool->shutdown) {
                        pthread_mutex_unlock(qlock);
                        pthread_exit(0);
                }

                //dequeue job
                work_t* job = pool->qhead;
                pool->qhead = job->next;
                pool->qsize--;

                // if(was full)
                //         cond_signal //dispatch unlock

                //notify destroy function
                if(!pool->qsize && pool->dont_accept)
                        pthread_cond_signal(&pool->q_not_empty);

                pthread_mutex_unlock(qlock);

                //run job
                job->routine(job->arg);
                free(job);
        }
}

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/


void destroy_threadpool(threadpool* destroyme) {

        pthread_mutex_lock(&destroyme->qlock);

        destroyme->dont_accept = 1;

        //wait until queue is empty
        if(destroyme->qsize)
                pthread_cond_wait(&destroyme->q_not_empty, &destroyme->qlock);

        //wake sleeping threads
        pthread_cond_signal(&destroyme->q_empty);

        destroyme->shutdown = 1;

        int i;
        for(i = 0; i < destroyme->num_threads; i++) {
                pthread_join(destroyme->threads[i], NULL);
        }

        free(destroyme->threads);
        pthread_cond_destroy(&destroyme->q_empty);
        pthread_cond_destroy(&destroyme->q_not_empty);
        pthread_mutex_destroy(&destroyme->qlock);
        free(destroyme);

}
