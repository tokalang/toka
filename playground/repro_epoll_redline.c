// Comprehensive Cross-Platform OS Reactor & Side-Table Redline Concurrency Probe
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef __linux__
#include <sys/epoll.h>
#endif
#ifdef __APPLE__
#include <sys/event.h>
#include <sys/time.h>
#endif

extern void toka_reactor_del_fd(int rfd, int fd);
extern void toka_reactor_del_read(int rfd, int fd, uint64_t expected_key);
extern void toka_reactor_del_write(int rfd, int fd, uint64_t expected_key);
extern int toka_reactor_add_read(int rfd, int fd, uint64_t key);
extern int toka_reactor_add_write(int rfd, int fd, uint64_t key);
extern int toka_reactor_wait(int rfd, int timeout_ms, uint64_t *out_keys, int max_events);

void test_reactor_readiness_and_key_isolation(void) {
    printf("[Reactor Probe] Creating real OS reactor instance...\n");
    int rfd = -1;
#ifdef __linux__
    rfd = epoll_create1(0);
#endif
#ifdef __APPLE__
    rfd = kqueue();
#endif
    assert(rfd >= 0);

    int fds[2];
    int res = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    assert(res == 0);
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    fcntl(fds[1], F_SETFL, O_NONBLOCK);
    int test_fd = fds[0];

    uint64_t key_read_1 = 0x1111222233334444ULL;
    uint64_t key_read_2 = 0x5555666677778888ULL;
    uint64_t key_write_1 = 0xAAAABBBBCCCCDDDDULL;

    // 1. Add read key 1
    assert(toka_reactor_add_read(rfd, test_fd, key_read_1) == 0);

    // EBUSY check: Attempt to register key_read_2 while key_read_1 is active -> must fail!
    assert(toka_reactor_add_read(rfd, test_fd, key_read_2) == -1);

    // 2. Try del_read with WRONG expected_key (key_read_2) -> must be ignored!
    toka_reactor_del_read(rfd, test_fd, key_read_2);

    // Write data to fds[1] to trigger read readiness on fds[0]
    write(fds[1], "X", 1);

    uint64_t out_keys[10];
    int n = toka_reactor_wait(rfd, 100, out_keys, 10);
    assert(n == 1);
    assert(out_keys[0] == key_read_1); // Correct key emitted!

    // Read byte to clear readiness
    char buf[10];
    read(fds[0], buf, 10);

    // 3. Add dual token (read + write)
    assert(toka_reactor_add_read(rfd, test_fd, key_read_1) == 0);
    assert(toka_reactor_add_write(rfd, test_fd, key_write_1) == 0);

    // 4. del_read with CORRECT key_read_1 -> disarms read, leaves write!
    toka_reactor_del_read(rfd, test_fd, key_read_1);

    // Wait should emit key_write_1 because socket is writable
    n = toka_reactor_wait(rfd, 100, out_keys, 10);
    assert(n == 1);
    assert(out_keys[0] == key_write_1);

    // 5. del_fd on socket close
    toka_reactor_del_fd(rfd, test_fd);

    close(fds[0]);
    close(fds[1]);

    // 6. Test max_events=1 dual-readiness overflow and rearm
    res = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    assert(res == 0);
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    fcntl(fds[1], F_SETFL, O_NONBLOCK);
    test_fd = fds[0];

    assert(toka_reactor_add_read(rfd, test_fd, key_read_1) == 0);
    assert(toka_reactor_add_write(rfd, test_fd, key_write_1) == 0);
    write(fds[1], "Y", 1); // Trigger both READ and WRITE ready!

    // max_events = 1 -> can only deliver 1 token per call!
    n = toka_reactor_wait(rfd, 100, out_keys, 1);
    assert(n == 1);
    uint64_t k1 = out_keys[0];
    assert(k1 == key_read_1 || k1 == key_write_1);

    // Second wait with max_events = 1 -> must deliver the remaining token!
    n = toka_reactor_wait(rfd, 100, out_keys, 1);
    assert(n == 1);
    uint64_t k2 = out_keys[0];
    assert(k2 == key_read_1 || k2 == key_write_1);
    assert(k1 != k2); // Both tokens delivered with 0 lost wakes!

    read(fds[0], buf, 10);
    toka_reactor_del_fd(rfd, test_fd);
    close(fds[0]);
    close(fds[1]);

    // 7. Test write-only del_fd and stale event filtering
    res = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    assert(res == 0);
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    fcntl(fds[1], F_SETFL, O_NONBLOCK);
    test_fd = fds[0];

    assert(toka_reactor_add_write(rfd, test_fd, key_write_1) == 0);
    toka_reactor_del_fd(rfd, test_fd); // Write-only del_fd must succeed without ENOENT abort!

    n = toka_reactor_wait(rfd, 10, out_keys, 10);
    assert(n == 0); // Stale key must NOT be delivered!

    close(fds[0]);
    close(fds[1]);
    close(rfd);
    printf("[Reactor Probe] OS reactor key-conditional deletion, max_events=1 rearm & stale key filtering PASSED!\n");
}

int main(void) {
    printf("Starting Cross-Platform Reactor Redline Concurrency Probe...\n");
    test_reactor_readiness_and_key_isolation();
    printf("PASSED! Cross-Platform Reactor Redline Probe verified!\n");
    return 0;
}
