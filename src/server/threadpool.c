/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * ThreadPool Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "server_internal.h"

static void *threadpool_worker(void *arg) {
    threadpool_t *pool = (threadpool_t *)arg;

    while (1) {
        pthread_mutex_lock(&(pool->lock));

        while (pool->queue_size == 0 && !pool->shutdown) {
            pthread_cond_wait(&(pool->notify), &(pool->lock));
        }

        if (pool->shutdown && pool->queue_size == 0) {
            pthread_mutex_unlock(&(pool->lock));
            pthread_exit(NULL);
        }

        /* Pop task from queue */
        thread_task_t *task = pool->queue_head;
        if (task) {
            pool->queue_head = task->next;
            pool->queue_size--;
        }

        pthread_mutex_unlock(&(pool->lock));

        if (task) {
            (*(task->function))(task->arg);
            free(task);
        }
    }
    return NULL;
}

threadpool_t *threadpool_create(int thread_count) {
    threadpool_t *pool = (threadpool_t *)malloc(sizeof(threadpool_t));
    if (!pool) return NULL;

    pool->thread_count = thread_count;
    pool->queue_size = 0;
    pool->queue_head = NULL;
    pool->shutdown = false;

    pthread_mutex_init(&(pool->lock), NULL);
    pthread_cond_init(&(pool->notify), NULL);

    pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * thread_count);
    for (int i = 0; i < thread_count; i++) {
        pthread_create(&(pool->threads[i]), NULL, threadpool_worker, (void *)pool);
    }

    return pool;
}

int threadpool_add_task(threadpool_t *pool, thread_task_fn function, void *arg) {
    thread_task_t *task = (thread_task_t *)malloc(sizeof(thread_task_t));
    if (!task) return -1;

    task->function = function;
    task->arg = arg;
    task->next = NULL;

    pthread_mutex_lock(&(pool->lock));

    if (pool->shutdown) {
        pthread_mutex_unlock(&(pool->lock));
        free(task);
        return -1;
    }

    /* Add to end of queue */
    if (pool->queue_head == NULL) {
        pool->queue_head = task;
    } else {
        thread_task_t *cur = pool->queue_head;
        while (cur->next) cur = cur->next;
        cur->next = task;
    }

    pool->queue_size++;
    pthread_cond_signal(&(pool->notify));
    pthread_mutex_unlock(&(pool->lock));

    return 0;
}

void threadpool_destroy(threadpool_t *pool) {
    if (!pool) return;

    pthread_mutex_lock(&(pool->lock));
    pool->shutdown = true;
    pthread_cond_broadcast(&(pool->notify));
    pthread_mutex_unlock(&(pool->lock));

    for (int i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    free(pool->threads);
    
    /* Clean remaining tasks */
    thread_task_t *task = pool->queue_head;
    while (task) {
        thread_task_t *next = task->next;
        free(task);
        task = next;
    }

    pthread_mutex_destroy(&(pool->lock));
    pthread_cond_destroy(&(pool->notify));
    free(pool);
}
