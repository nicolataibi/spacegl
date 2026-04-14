/*
 * SPACE GL - SHARED MEMORY DIAGNOSTIC TOOL
 */

#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <ncurses.h>
#include <string.h>
#include <errno.h>
#include "../include/shared_state.h"

int main() {
    int shm_fd = shm_open(SHM_NAME, O_RDONLY, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed (is the server running?)");
        return 1;
    }

    size_t shm_size = sizeof(GameState);
    GameState* state = mmap(NULL, shm_size, PROT_READ, MAP_SHARED, shm_fd, 0);
    if (state == MAP_FAILED) {
        perror("mmap failed");
        close(shm_fd);
        return 1;
    }

    initscr(); start_color(); curs_set(0); noecho(); nodelay(stdscr, TRUE);
    init_pair(1, COLOR_GREEN, COLOR_BLACK);
    init_pair(2, COLOR_CYAN, COLOR_BLACK);

    while (getch() != 'q') {
        erase();
        attron(COLOR_PAIR(2) | A_BOLD);
        mvprintw(0, 0, "SPACE GL SHM DIAGNOSTIC | Objects: %d", state->object_count);
        attroff(A_BOLD);
        
        mvprintw(2, 0, "%-6s | %-20s | %-10s | %-10s", "ID", "NAME", "TYPE", "HEALTH");
        
        for (int i = 0; i < state->object_count && i < 20; i++) {
            SharedObject* obj = &state->objects[i];
            if (obj->active) {
                mvprintw(i + 3, 0, "%-6d | %-20.20s | %-10d | %-10d", 
                         obj->id, obj->shm_name, obj->type, obj->health_pct);
            }
        }
        
        refresh();
        usleep(100000);
    }

    munmap(state, shm_size);
    close(shm_fd);
    endwin();
    return 0;
}
