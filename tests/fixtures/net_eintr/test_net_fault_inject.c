#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

static int g_fault_count = 0;
static int g_fault_errno = 0;
static int g_fault_target_fd = -1;
static int g_eintr_attempts = 0;
static int g_cancel_after_attempts = -1;
static volatile int g_cancel_triggered = 0;

void toka_test_set_accept_fault(int count, int err, int target_fd) {
    g_fault_count = count;
    g_fault_errno = err;
    g_fault_target_fd = target_fd;
}

void toka_test_reset_stats(void) {
    g_fault_count = 0;
    g_fault_errno = 0;
    g_fault_target_fd = -1;
    g_eintr_attempts = 0;
    g_cancel_after_attempts = -1;
    g_cancel_triggered = 0;
}

int toka_test_get_eintr_attempts(void) {
    return g_eintr_attempts;
}

int toka_test_get_remaining_faults(void) {
    return g_fault_count;
}

void toka_test_arm_cancel_after(int attempts) {
    g_cancel_after_attempts = attempts;
    g_cancel_triggered = 0;
}

int toka_test_is_cancel_triggered(void) {
    return g_cancel_triggered;
}

int toka_test_inject_accept(int fd, void *addr, socklen_t *len) {
    if (g_fault_count > 0 && (g_fault_target_fd == -1 || g_fault_target_fd == fd)) {
        g_fault_count--;
        if (g_fault_errno == 4) {
            g_eintr_attempts++;
            if (g_cancel_after_attempts > 0 && g_eintr_attempts >= g_cancel_after_attempts) {
                g_cancel_triggered = 1;
            }
        }
        errno = g_fault_errno;
        return -1;
    }
    return accept(fd, (struct sockaddr *)addr, len);
}
