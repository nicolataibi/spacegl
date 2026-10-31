/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * shm_layout_test — the SharedIPC segment (include/shared_state.h):
 *  1. on a real mapping, the process-shared mutex and semaphore and every
 *     atomic index sit at their natural alignment (they were 4 mod 8 when
 *     the struct was packed), and the ring indices written by different
 *     processes are on different 64-byte lines;
 *  2. the layout checks refuse a segment that is too short, has no header,
 *     or has another version or size; they accept a stamped one;
 *  3. the process-shared mutex and semaphore work across fork().
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "shared_state.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s at %s:%d\n", #c, __FILE__, __LINE__); exit(1); } } while (0)
#define ALIGNED(p, a) ((((uintptr_t)(p)) % (a)) == 0)
#define LINE(p) (((uintptr_t)(p)) / 64)

int main(void) {
    char name[64];
    snprintf(name, sizeof(name), "/sg_layout_test_%d", (int)getpid());
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    CHECK(fd >= 0);
    shm_unlink(name);
    CHECK(ftruncate(fd, sizeof(SharedIPC)) == 0);
    SharedIPC *s = mmap(NULL, sizeof(SharedIPC), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    CHECK(s != MAP_FAILED);

    /* 1. alignment on the real mapping */
    CHECK(ALIGNED(&s->mutex, _Alignof(pthread_mutex_t)) && ALIGNED(&s->mutex, 8));
    CHECK(ALIGNED(&s->data_ready, _Alignof(sem_t)) && ALIGNED(&s->data_ready, 8));
    CHECK(ALIGNED(&s->read_index, 4) && ALIGNED(&s->write_index, 4));
    CHECK(ALIGNED(&s->event_head, 64) && ALIGNED(&s->event_tail, 64));
    CHECK(ALIGNED(&s->cmd_head, 64) && ALIGNED(&s->cmd_tail, 64));
    CHECK(ALIGNED(&s->force_shutdown, 64));
    CHECK(LINE(&s->event_head) != LINE(&s->event_tail));
    CHECK(LINE(&s->cmd_head) != LINE(&s->cmd_tail));
    printf("[1] mutex, semaphore and ring indices naturally aligned, producer/consumer on separate lines: OK\n");

    /* 2. layout checks */
    CHECK(shm_layout_check_size((long long)sizeof(SharedIPC) - 1) != NULL);
    CHECK(shm_layout_check_size((long long)sizeof(SharedIPC)) == NULL);
    CHECK(shm_layout_check(s) != NULL);              /* zeroed: no header */
    shm_layout_stamp(s);
    CHECK(shm_layout_check(s) == NULL);
    s->layout_version = SHM_LAYOUT_VERSION + 1;
    CHECK(shm_layout_check(s) != NULL);
    s->layout_version = SHM_LAYOUT_VERSION;
    s->layout_size = sizeof(SharedIPC) + 8;
    CHECK(shm_layout_check(s) != NULL);
    shm_layout_stamp(s);
    struct stat st;
    CHECK(fstat(fd, &st) == 0 && shm_layout_check_size((long long)st.st_size) == NULL);
    printf("[2] short segment, missing header, other version/size refused; stamped accepted: OK\n");

    /* 3. process-shared mutex + semaphore across fork */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    CHECK(pthread_mutex_init(&s->mutex, &attr) == 0);
    CHECK(sem_init(&s->data_ready, 1, 0) == 0);
    atomic_store(&s->event_head, 0);
    enum { N = 20000 };
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        for (int i = 0; i < N; i++) {
            pthread_mutex_lock(&s->mutex);
            s->dismantle_telemetry.frame_id++;
            pthread_mutex_unlock(&s->mutex);
            atomic_fetch_add(&s->event_head, 1);
        }
        sem_post(&s->data_ready);
        _exit(0);
    }
    for (int i = 0; i < N; i++) {
        pthread_mutex_lock(&s->mutex);
        s->dismantle_telemetry.frame_id++;
        pthread_mutex_unlock(&s->mutex);
        atomic_fetch_add(&s->event_head, 1);
    }
    CHECK(sem_wait(&s->data_ready) == 0);
    int status = 0;
    CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(s->dismantle_telemetry.frame_id == 2 * N);
    CHECK(atomic_load(&s->event_head) == 2 * N);
    printf("[3] process-shared mutex and semaphore across fork (%d increments): OK\n", 2 * N);

    munmap(s, sizeof(SharedIPC));
    close(fd);
    printf("shm_layout_test: all checks passed\n");
    return 0;
}
