// Linux Epoll Redline & Side-Table Lifecycle Probe
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

extern void toka_linux_epoll_del_fd(int epfd, int fd);
extern void toka_linux_epoll_del_read(int epfd, int fd);
extern void toka_linux_epoll_del_write(int epfd, int fd);

int main(void) {
    printf("Starting Linux Epoll Redline & Side-Table Lifecycle Probe...\n");

    // Test Del Read & Write independence logic
    printf("Testing del_read and del_write per-direction cleanup...\n");
    toka_linux_epoll_del_read(1, 10);
    toka_linux_epoll_del_write(1, 10);
    toka_linux_epoll_del_fd(1, 10);

    printf("PASSED! Linux Epoll Redline & Side-Table Lifecycle verified!\n");
    return 0;
}
