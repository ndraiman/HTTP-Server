#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include "threadpool.h"

#define DEBUG 1
#define debug_print(fmt, ...) \
           do { if (DEBUG) fprintf(stderr, fmt, __VA_ARGS__); } while (0)


int avoda(void*);

int main() {

        srand(time(NULL));
        int num_of_threads = 5;
        int num_of_jobs = 20;

        debug_print("Main - %s\n", "Creating pool");
        threadpool* pool = create_threadpool(num_of_threads);
        int i;
        for(i = 0; i < num_of_threads; i++) {
                debug_print("thread[%d] tid = %d\n", i, (int)pool->threads[i]);
        }

        int y[num_of_jobs];

        debug_print("\n%s\n", "****************************************");
        for(i = 0; i < num_of_jobs; i ++) {

                y[i] = rand();
                debug_print("Main - dispatching job with y = %d\n", y[i]);
                dispatch(pool, avoda, (void*)(&(y[i])));

        }
        debug_print("\n%s\n", "****************************************");

        debug_print("Main - %s\n", "going to sleep");
        sleep(5);
        debug_print("Main - %s\n", "woke up");

        debug_print("Main - %s\n", "destroying pool");
        destroy_threadpool(pool);

}


int avoda(void* pong) {
        debug_print("*****Sleeping on the job - tid: %d\n", (int)pthread_self());
        sleep(5);
        debug_print("*****!!!! JOB ROUTINE BEING DONE - tid: %d\n", (int)pthread_self());
        debug_print("*****pong = %d\n", *((int*)pong));
        return 0;
}
