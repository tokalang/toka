#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

void* toka_localtime_r(const time_t *timep, struct tm *result) {
#ifdef _WIN32
    if (localtime_s(result, timep) == 0) { return result; }
    return NULL;
#else
    return localtime_r(timep, result);
#endif
}

void* toka_gmtime_r(const time_t *timep, struct tm *result) {
#ifdef _WIN32
    if (gmtime_s(result, timep) == 0) { return result; }
    return NULL;
#else
    return gmtime_r(timep, result);
#endif
}


#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
int toka_setmode(int fd, int mode) { return _setmode(fd, mode); }
const char* toka_readdir_name(void* entry) { return NULL; }
void* toka_opendir_impl(const char* path) { return NULL; }
void* toka_readdir_impl(void* dir) { return NULL; }
void toka_closedir_impl(void* dir) {}
void* toka_stat_impl(const char* path) { return NULL; }
unsigned int toka_stat_mode(void* handle) { return 0; }
unsigned long long toka_stat_size(void* handle) { return 0; }
long long toka_stat_mtime(void* handle) { return 0; }
void toka_stat_free(void* handle) {}
int toka_fileno(FILE *f) { return _fileno(f); }

#else
#include <unistd.h>
int toka_setmode(int fd, int mode) { return 0; }
int toka_fileno(FILE *f) { return fileno(f); }

#if 0
#ifdef __linux__
extern int main(int argc, char **argv);
__attribute__((naked, weak)) void _start() {
    asm volatile (
        "pop %rdi\n"
        "mov %rsp, %rsi\n"
        "call main\n"
        "mov %rax, %rdi\n"
        "mov $60, %rax\n"
        "syscall\n"
    );
}
#endif
#endif
#endif

#ifdef _WIN32
#include <windows.h>
int toka_get_last_error() { return GetLastError(); }
#include <stdint.h>
int toka_clock_realtime(int64_t *ts) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    t -= 116444736000000000ULL;
    ts[0] = t / 10000000;
    ts[1] = (t % 10000000) * 100;
    return 1;
}
int toka_clock_monotonic(int64_t *ts) {
    LARGE_INTEGER freq, count;
    if (QueryPerformanceFrequency(&freq) && QueryPerformanceCounter(&count)) {
        ts[0] = count.QuadPart / freq.QuadPart;
        ts[1] = ((count.QuadPart % freq.QuadPart) * 1000000000) / freq.QuadPart;
        return 1;
    }
    return 0;
}
#endif

#include <stdio.h>
#include <stdlib.h>

#ifdef __linux__
#include <stdint.h>
#include <sys/epoll.h>

int toka_epoll_ctl_handle(int epfd, int op, int fd, uint32_t events,
                          uintptr_t handle) {
    struct epoll_event event = {0};
    event.events = events;
    event.data.u64 = (uint64_t)handle;
    return epoll_ctl(epfd, op, fd, &event);
}

int toka_epoll_wait_handles(int epfd, int timeout_ms, uintptr_t *out_events,
                            int max_events) {
    if (max_events <= 0) {
        return 0;
    }
    struct epoll_event *events = calloc((size_t)max_events, sizeof(*events));
    if (!events) {
        return -1;
    }
    int count = epoll_wait(epfd, events, max_events, timeout_ms);
    for (int i = 0; i < count; ++i) {
        out_events[i] = (uintptr_t)events[i].data.u64;
    }
    free(events);
    return count;
}
#endif

void toka_panic(const char* msg, int len) {
    const char *prefix = "thread 'main' panicked at '";
    const char *suffix = "'\n";
#ifdef _WIN32
    _write(2, prefix, 27);
    _write(2, msg, len);
    _write(2, suffix, 2);
    ExitProcess(3);
#else
    write(2, prefix, 27);
    write(2, msg, len);
    write(2, suffix, 2);
    abort();
#endif
}

#include <sys/stat.h>
#ifndef _WIN32
#include <dirent.h>
#include <errno.h>
int toka_get_last_error() { return errno; }
const char* toka_readdir_name(void* entry) { return ((struct dirent*)entry)->d_name; }
void* toka_opendir_impl(const char* path) { return opendir(path); }
void* toka_readdir_impl(void* dir) { return readdir(dir); }
void toka_closedir_impl(void* dir) { closedir(dir); }
void* toka_stat_impl(const char* path) {
    struct stat* st = malloc(sizeof(struct stat));
    if (!st) return NULL;
    if (stat(path, st) != 0) {
        free(st);
        return NULL;
    }
    return st;
}
unsigned int toka_stat_mode(void* handle) { return ((struct stat*)handle)->st_mode; }
unsigned long long toka_stat_size(void* handle) { return ((struct stat*)handle)->st_size; }
long long toka_stat_mtime(void* handle) { return ((struct stat*)handle)->st_mtime; }
void toka_stat_free(void* handle) { free(handle); }
#endif

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
static int wsa_initialized = 0;
void toka_ensure_wsa_initialized() {
    if (!wsa_initialized) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
            wsa_initialized = 1;
        }
    }
}
#else
#ifndef __wasi__
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#endif
void toka_ensure_wsa_initialized() {}
#endif

unsigned int toka_resolve_ipv4(const char* host) {
#ifdef __wasi__
    return 0;
#else
    toka_ensure_wsa_initialized();
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res;
    if (getaddrinfo(host, NULL, &hints, &res) != 0) return 0;
    struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
    unsigned int ip = addr->sin_addr.s_addr;
    freeaddrinfo(res);
    return ip;
#endif
}

void toka_print_str(const char* s) {
    printf("%s", s);
}

void toka_print_i32(int val) {
    printf("%d", val);
}

void toka_print_f64(double val) {
    printf("%g", val);
}

// =========================================================================
// Toka 1.0 Core Compiler Magic Hooks Real Implementations (L3 Execution)
// =========================================================================
#include <string.h>

// =========================================================================
// Process execution boundary
// =========================================================================

#include <errno.h>

static int toka_unpack_process_argv(const char *packed, size_t packed_len,
                                    size_t argc, char ***out_argv) {
    if (!packed || !out_argv || argc == 0) return EINVAL;
    char **argv = (char **)calloc(argc + 1, sizeof(char *));
    if (!argv) return ENOMEM;

    size_t offset = 0;
    for (size_t i = 0; i < argc; ++i) {
        if (offset >= packed_len) {
            free(argv);
            return EINVAL;
        }
        const void *end = memchr(packed + offset, '\0', packed_len - offset);
        if (!end) {
            free(argv);
            return EINVAL;
        }
        argv[i] = (char *)(packed + offset);
        offset = (size_t)((const char *)end - packed) + 1;
    }
    if (offset != packed_len || argv[0][0] == '\0') {
        free(argv);
        return EINVAL;
    }
    argv[argc] = NULL;
    *out_argv = argv;
    return 0;
}

#if !defined(_WIN32) && !defined(__wasi__)

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>

struct toka_process_buffer {
    char *data;
    size_t len;
    size_t cap;
};

static int toka_process_buffer_append(struct toka_process_buffer *buffer,
                                      const char *data, size_t len) {
    if (len == 0) return 0;
    if (buffer->len + len + 1 > buffer->cap) {
        size_t cap = buffer->cap ? buffer->cap : 4096;
        while (cap < buffer->len + len + 1) {
            if (cap > SIZE_MAX / 2) return ENOMEM;
            cap *= 2;
        }
        char *next = (char *)realloc(buffer->data, cap);
        if (!next) return ENOMEM;
        buffer->data = next;
        buffer->cap = cap;
    }
    memcpy(buffer->data + buffer->len, data, len);
    buffer->len += len;
    buffer->data[buffer->len] = '\0';
    return 0;
}

static int toka_process_buffer_finish(struct toka_process_buffer *buffer) {
    if (buffer->data) return 0;
    buffer->data = (char *)malloc(1);
    if (!buffer->data) return ENOMEM;
    buffer->data[0] = '\0';
    buffer->cap = 1;
    return 0;
}

static int toka_process_make_exec_pipe(int pipefd[2]) {
    if (pipe(pipefd) != 0) return errno;
    int flags = fcntl(pipefd[1], F_GETFD);
    if (flags < 0 || fcntl(pipefd[1], F_SETFD, flags | FD_CLOEXEC) != 0) {
        int error = errno;
        close(pipefd[0]);
        close(pipefd[1]);
        return error;
    }
    return 0;
}

static void toka_process_child_exec(char **argv, int exec_error_fd) {
    execvp(argv[0], argv);
    int error = errno;
    while (write(exec_error_fd, &error, sizeof(error)) < 0 && errno == EINTR) {}
    _exit(127);
}

int toka_process_spawn_packed(const char *packed, size_t packed_len,
                              size_t argc) {
    char **argv = NULL;
    int error = toka_unpack_process_argv(packed, packed_len, argc, &argv);
    if (error != 0) return -error;

    int exec_pipe[2];
    error = toka_process_make_exec_pipe(exec_pipe);
    if (error != 0) {
        free(argv);
        return -error;
    }

    pid_t pid = fork();
    if (pid < 0) {
        error = errno;
        close(exec_pipe[0]);
        close(exec_pipe[1]);
        free(argv);
        return -error;
    }
    if (pid == 0) {
        close(exec_pipe[0]);
        toka_process_child_exec(argv, exec_pipe[1]);
    }

    close(exec_pipe[1]);
    int exec_error = 0;
    ssize_t read_count;
    do {
        read_count = read(exec_pipe[0], &exec_error, sizeof(exec_error));
    } while (read_count < 0 && errno == EINTR);
    close(exec_pipe[0]);
    free(argv);

    if (read_count > 0) {
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}
        return -exec_error;
    }
    if (read_count < 0) {
        error = errno;
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}
        return -error;
    }
    return (int)pid;
}

int toka_process_wait(int pid, int *out_exit_code, int *out_signal) {
    if (pid <= 0 || !out_exit_code || !out_signal) return EINVAL;
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid((pid_t)pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) return errno;

    *out_exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    *out_signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    return 0;
}

int toka_process_status_packed(const char *packed, size_t packed_len,
                               size_t argc, int *out_exit_code,
                               int *out_signal) {
    int pid = toka_process_spawn_packed(packed, packed_len, argc);
    if (pid < 0) return -pid;
    return toka_process_wait(pid, out_exit_code, out_signal);
}

int toka_process_output_packed(const char *packed, size_t packed_len,
                               size_t argc, char **out_stdout,
                               size_t *out_stdout_len, char **out_stderr,
                               size_t *out_stderr_len, int *out_exit_code,
                               int *out_signal) {
    if (!out_stdout || !out_stdout_len || !out_stderr || !out_stderr_len ||
        !out_exit_code || !out_signal) return EINVAL;
    *out_stdout = NULL;
    *out_stdout_len = 0;
    *out_stderr = NULL;
    *out_stderr_len = 0;

    char **argv = NULL;
    int error = toka_unpack_process_argv(packed, packed_len, argc, &argv);
    if (error != 0) return error;

    int stdout_pipe[2];
    int stderr_pipe[2];
    int exec_pipe[2];
    if (pipe(stdout_pipe) != 0) {
        error = errno;
        free(argv);
        return error;
    }
    if (pipe(stderr_pipe) != 0) {
        error = errno;
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        free(argv);
        return error;
    }
    error = toka_process_make_exec_pipe(exec_pipe);
    if (error != 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        free(argv);
        return error;
    }

    pid_t pid = fork();
    if (pid < 0) {
        error = errno;
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        close(exec_pipe[0]); close(exec_pipe[1]);
        free(argv);
        return error;
    }
    if (pid == 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        close(exec_pipe[0]);
        if (dup2(stdout_pipe[1], STDOUT_FILENO) < 0 ||
            dup2(stderr_pipe[1], STDERR_FILENO) < 0) {
            int child_error = errno;
            while (write(exec_pipe[1], &child_error, sizeof(child_error)) < 0 &&
                   errno == EINTR) {}
            _exit(127);
        }
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        toka_process_child_exec(argv, exec_pipe[1]);
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    close(exec_pipe[1]);
    free(argv);

    int exec_error = 0;
    ssize_t exec_read;
    do {
        exec_read = read(exec_pipe[0], &exec_error, sizeof(exec_error));
    } while (exec_read < 0 && errno == EINTR);
    int exec_read_error = exec_read < 0 ? errno : 0;
    close(exec_pipe[0]);

    struct toka_process_buffer stdout_buffer = {0};
    struct toka_process_buffer stderr_buffer = {0};
    struct pollfd fds[2] = {
        {stdout_pipe[0], POLLIN | POLLHUP, 0},
        {stderr_pipe[0], POLLIN | POLLHUP, 0},
    };
    int open_fds = 2;
    while (open_fds > 0) {
        int ready;
        do {
            ready = poll(fds, 2, -1);
        } while (ready < 0 && errno == EINTR);
        if (ready < 0) {
            error = errno;
            break;
        }
        for (int i = 0; i < 2; ++i) {
            if (fds[i].fd < 0 || !(fds[i].revents & (POLLIN | POLLHUP | POLLERR)))
                continue;
            char chunk[4096];
            ssize_t count;
            do {
                count = read(fds[i].fd, chunk, sizeof(chunk));
            } while (count < 0 && errno == EINTR);
            if (count > 0) {
                struct toka_process_buffer *buffer =
                    i == 0 ? &stdout_buffer : &stderr_buffer;
                int append_error = toka_process_buffer_append(
                    buffer, chunk, (size_t)count);
                if (append_error != 0) {
                    error = append_error;
                    open_fds = 0;
                    break;
                }
            } else if (count == 0 || (count < 0 && errno != EAGAIN)) {
                close(fds[i].fd);
                fds[i].fd = -1;
                --open_fds;
            }
        }
    }
    for (int i = 0; i < 2; ++i)
        if (fds[i].fd >= 0) close(fds[i].fd);

    int wait_error = toka_process_wait((int)pid, out_exit_code, out_signal);
    if (error == 0 && wait_error != 0) error = wait_error;
    if (error == 0 && exec_read < 0) error = exec_read_error;
    if (error == 0 && exec_read > 0) error = exec_error;
    if (error == 0) error = toka_process_buffer_finish(&stdout_buffer);
    if (error == 0) error = toka_process_buffer_finish(&stderr_buffer);
    if (error != 0) {
        free(stdout_buffer.data);
        free(stderr_buffer.data);
        return error;
    }

    *out_stdout = stdout_buffer.data;
    *out_stdout_len = stdout_buffer.len;
    *out_stderr = stderr_buffer.data;
    *out_stderr_len = stderr_buffer.len;
    return 0;
}

#else

#ifdef _WIN32
#include <process.h>
#endif

int toka_process_spawn_packed(const char *packed, size_t packed_len,
                              size_t argc) {
    (void)packed; (void)packed_len; (void)argc;
    return -ENOSYS;
}

int toka_process_wait(int pid, int *out_exit_code, int *out_signal) {
    (void)pid; (void)out_exit_code; (void)out_signal;
    return ENOSYS;
}

int toka_process_status_packed(const char *packed, size_t packed_len,
                               size_t argc, int *out_exit_code,
                               int *out_signal) {
#ifdef _WIN32
    char **argv = NULL;
    int error = toka_unpack_process_argv(packed, packed_len, argc, &argv);
    if (error != 0) return error;
    intptr_t status = _spawnvp(_P_WAIT, argv[0], (const char *const *)argv);
    error = status < 0 ? errno : 0;
    if (error == 0) {
        *out_exit_code = (int)status;
        *out_signal = 0;
    }
    free(argv);
    return error;
#else
    (void)packed; (void)packed_len; (void)argc;
    (void)out_exit_code; (void)out_signal;
    return ENOSYS;
#endif
}

int toka_process_output_packed(const char *packed, size_t packed_len,
                               size_t argc, char **out_stdout,
                               size_t *out_stdout_len, char **out_stderr,
                               size_t *out_stderr_len, int *out_exit_code,
                               int *out_signal) {
    (void)packed; (void)packed_len; (void)argc;
    (void)out_stdout; (void)out_stdout_len;
    (void)out_stderr; (void)out_stderr_len;
    (void)out_exit_code; (void)out_signal;
    return ENOSYS;
}

#endif

struct TokaString {
    const char* buf;
    size_t len;
};

static FILE* get_stderr_stream() {
    static FILE* s_stderr = NULL;
    if (!s_stderr) {
#ifdef _WIN32
        s_stderr = _fdopen(2, "w");
#else
        s_stderr = fdopen(2, "w");
#endif
    }
    return s_stderr;
}

void __toka_panic(struct TokaString* message, struct TokaString* file_name, int line) {
    FILE* stream = get_stderr_stream();
    if (message && file_name) {
        fprintf(stream, "\n*** %.*s:%d runtime error: Panic with \"%.*s\" ***\n\n",
                (int)file_name->len, file_name->buf, (int)line,
                (int)message->len, message->buf);
    } else {
        fprintf(stream, "\n*** runtime error: Panic at line %d ***\n\n", (int)line);
    }
    fflush(stream);
    abort();
}

void toka_panic_impl(const char* msg_buf, size_t msg_len, const char* file_buf, size_t file_len, int line) {
    FILE* stream = get_stderr_stream();
    fprintf(stream, "\n*** %.*s:%d runtime error: Panic with \"%.*s\" ***\n\n",
            (int)file_len, file_buf, (int)line,
            (int)msg_len, msg_buf);
    fflush(stream);
    abort();
}

void toka_task_unhandled_cancellation(void) {
    FILE* stream = get_stderr_stream();
    fprintf(stream, "\n*** runtime error: unhandled task cancellation at synchronous wait ***\n\n");
    fflush(stream);
    abort();
}

void* __toka_get_coro_handle(void* task_handle_ptr) {
    if (!task_handle_ptr) return NULL;
    return *(void**)task_handle_ptr;
}

// =========================================================================
// Toka Phase 1 Async Runtime C Infrastructure (TCB & Unified Ready Queue)
// =========================================================================
#include <stdatomic.h>
#include <stdint.h>

#if defined(_WIN32)
#include <windows.h>
typedef SRWLOCK toka_mutex_t;
#define TOKA_MUTEX_INIT SRWLOCK_INIT
static void toka_mutex_lock(toka_mutex_t *m) { AcquireSRWLockExclusive(m); }
static void toka_mutex_unlock(toka_mutex_t *m) { ReleaseSRWLockExclusive(m); }
#elif defined(__wasi__)
typedef int toka_mutex_t;
#define TOKA_MUTEX_INIT 0
static void toka_mutex_lock(toka_mutex_t *m) { (void)m; }
static void toka_mutex_unlock(toka_mutex_t *m) { (void)m; }
#else
#include <pthread.h>
typedef pthread_mutex_t toka_mutex_t;
#define TOKA_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER
static void toka_mutex_lock(toka_mutex_t *m) { pthread_mutex_lock(m); }
static void toka_mutex_unlock(toka_mutex_t *m) { pthread_mutex_unlock(m); }
#endif

#define TOKA_RESULT_STATE_PENDING 0
#define TOKA_RESULT_STATE_READYLIVE 1
#define TOKA_RESULT_STATE_TAKEN 2
#define TOKA_RESULT_STATE_CANCELED 3

#define TOKA_WAKE_GROUP_CANCELLED 0xFFFFFFFF

// Promise Header layout matching LLVM CodeGen
struct TokaPromiseHeader {
    _Atomic uint8_t result_state;   // 0: Pending, 1: ReadyLive, 2: Taken
    void *self_tcb;                 // TokaTCB*
    _Atomic uintptr_t continuation; // 0: None, 1: Completed, or (uintptr_t)(TokaTCB*)
};

#define TOKA_NO_WAIT_ID 0xFFFFFFFF

typedef enum {
    TOKA_TCB_CREATED = 0,
    TOKA_TCB_RUNNING = 1,
    TOKA_TCB_SUSPENDED = 2,
    TOKA_TCB_QUEUED = 3,
    TOKA_TCB_COMPLETED = 4,
    TOKA_TCB_PREPARING = 5,
    TOKA_TCB_PREPARING_WITH_PENDING_WAKE = 6,
    TOKA_TCB_COMPLETED_CANCELED = 7
} TokaTCBState;

static inline int toka_tcb_is_terminal(uint32_t st) {
    return (st == TOKA_TCB_COMPLETED || st == TOKA_TCB_COMPLETED_CANCELED);
}

typedef struct TokaTCB TokaTCB;
void toka_task_release(void *tcb_ptr);
int toka_task_request_cancel(void *tcb_ptr);
int toka_wait_registry_try_wake(uint32_t wait_id, uint32_t slot_gen);
int toka_wait_registry_release(uint32_t wait_id, uint32_t slot_gen);

typedef struct TokaCompletionSubscriber {
    uint32_t wait_id;
    uint32_t slot_gen;
} TokaCompletionSubscriber;

typedef struct TokaTCB {
    uint64_t id;
    void *coro_frame;
    void *promise;
    _Atomic uint64_t task_schedule_generation;
    _Atomic uint32_t state;
    _Atomic uint8_t cancel_requested;
    _Atomic uint32_t ref_count;
    _Atomic uint8_t detached;
    _Atomic uint8_t detached_counted;
    _Atomic uint8_t owner_released;
    _Atomic uint32_t active_wait_id;
    _Atomic uint32_t active_slot_gen;
    _Atomic uintptr_t active_child_tcb;
    _Atomic uintptr_t parent_tcb;
    TokaCompletionSubscriber *subscribers;
    uint32_t subscriber_count;
    uint32_t subscriber_capacity;
    TokaTCB **cancel_children;
    uint32_t cancel_child_count;
    uint32_t cancel_child_capacity;
} TokaTCB;

static void toka_wait_registry_cancel_active(TokaTCB *tcb);

static void toka_task_try_release_owner(TokaTCB *tcb) {
    if (!tcb) return;
    uint32_t st = atomic_load(&tcb->state);
    if (atomic_load(&tcb->detached) && (toka_tcb_is_terminal(st) || st == TOKA_TCB_CREATED)) {
        uint8_t expected = 0;
        if (atomic_compare_exchange_strong(&tcb->owner_released, &expected, 1)) {
            toka_task_release(tcb);
        }
    }
}

typedef struct {
    uint64_t task_id;
    uint64_t task_schedule_generation;
    TokaTCB *tcb;
} TokaScheduledItem;

void toka_task_release(void *tcb_ptr);

static toka_mutex_t g_rt_mutex = TOKA_MUTEX_INIT;
static _Atomic uint64_t g_next_task_id = 1;

static TokaScheduledItem *g_ready_queue = NULL;
static size_t g_ready_capacity = 0;
static size_t g_ready_head = 0;
static size_t g_ready_tail = 0;
static size_t g_ready_count = 0;

static void ensure_ready_queue_capacity_locked(void) {
    if (g_ready_queue == NULL) {
        g_ready_capacity = 256;
        g_ready_queue = (TokaScheduledItem*)calloc(g_ready_capacity, sizeof(TokaScheduledItem));
        if (!g_ready_queue) {
            fprintf(stderr, "Fatal error: Out of memory during Toka ready queue initialization.\n");
            abort();
        }
        g_ready_head = 0;
        g_ready_tail = 0;
        g_ready_count = 0;
    } else if (g_ready_count == g_ready_capacity) {
        if (g_ready_capacity > SIZE_MAX / 2 || (g_ready_capacity * 2) > SIZE_MAX / sizeof(TokaScheduledItem)) {
            fprintf(stderr, "Fatal error: Integer overflow during ready queue capacity calculation.\n");
            abort();
        }
        size_t new_capacity = g_ready_capacity * 2;
        TokaScheduledItem *new_queue = (TokaScheduledItem*)calloc(new_capacity, sizeof(TokaScheduledItem));
        if (!new_queue) {
            fprintf(stderr, "Fatal error: Out of memory during Toka ready queue auto-expansion.\n");
            abort();
        }
        for (size_t i = 0; i < g_ready_count; ++i) {
            new_queue[i] = g_ready_queue[(g_ready_head + i) % g_ready_capacity];
        }
        free(g_ready_queue);
        g_ready_queue = new_queue;
        g_ready_head = 0;
        g_ready_tail = g_ready_count;
        g_ready_capacity = new_capacity;
    }
}

static void push_ready_queue_locked(uint64_t task_id, uint64_t gen, TokaTCB *tcb) {
    ensure_ready_queue_capacity_locked();
    atomic_fetch_add(&tcb->ref_count, 1);
    g_ready_queue[g_ready_tail].task_id = task_id;
    g_ready_queue[g_ready_tail].task_schedule_generation = gen;
    g_ready_queue[g_ready_tail].tcb = tcb;
    g_ready_tail = (g_ready_tail + 1) % g_ready_capacity;
    g_ready_count++;
}

uint32_t toka_ready_queue_capacity(void) {
    toka_mutex_lock(&g_rt_mutex);
    uint32_t cap = (uint32_t)g_ready_capacity;
    toka_mutex_unlock(&g_rt_mutex);
    return cap;
}

uint32_t toka_ready_queue_count(void) {
    toka_mutex_lock(&g_rt_mutex);
    uint32_t cnt = (uint32_t)g_ready_count;
    toka_mutex_unlock(&g_rt_mutex);
    return cnt;
}

typedef struct {
    void *frame;
    TokaTCB *tcb;
} TokaFrameMapEntry;

static TokaFrameMapEntry *g_frame_map = NULL;
static size_t g_frame_map_capacity = 0;
static size_t g_frame_map_count = 0;

static void register_frame_map(void *frame, TokaTCB *tcb) {
    if (!frame || !tcb) return;
    toka_mutex_lock(&g_rt_mutex);
    for (size_t i = 0; i < g_frame_map_count; ++i) {
        if (g_frame_map[i].frame == frame) {
            g_frame_map[i].tcb = tcb;
            toka_mutex_unlock(&g_rt_mutex);
            return;
        }
    }
    if (g_frame_map == NULL) {
        g_frame_map_capacity = 256;
        g_frame_map = (TokaFrameMapEntry*)calloc(g_frame_map_capacity, sizeof(TokaFrameMapEntry));
        if (!g_frame_map) {
            fprintf(stderr, "Fatal error: Out of memory during Toka frame map initialization.\n");
            abort();
        }
    } else if (g_frame_map_count == g_frame_map_capacity) {
        if (g_frame_map_capacity > SIZE_MAX / 2 || (g_frame_map_capacity * 2) > SIZE_MAX / sizeof(TokaFrameMapEntry)) {
            fprintf(stderr, "Fatal error: Integer overflow during frame map capacity calculation.\n");
            abort();
        }
        size_t new_cap = g_frame_map_capacity * 2;
        TokaFrameMapEntry *new_map = (TokaFrameMapEntry*)realloc(g_frame_map, new_cap * sizeof(TokaFrameMapEntry));
        if (!new_map) {
            fprintf(stderr, "Fatal error: Out of memory during Toka frame map expansion.\n");
            abort();
        }
        g_frame_map = new_map;
        g_frame_map_capacity = new_cap;
    }
    g_frame_map[g_frame_map_count].frame = frame;
    g_frame_map[g_frame_map_count].tcb = tcb;
    g_frame_map_count++;
    toka_mutex_unlock(&g_rt_mutex);
}

static _Thread_local TokaTCB *g_current_tcb = NULL;

static TokaTCB* lookup_tcb_by_frame_retained(void *frame) {
    toka_mutex_lock(&g_rt_mutex);
    if (frame) {
        for (size_t i = 0; i < g_frame_map_count; ++i) {
            if (g_frame_map[i].frame == frame) {
                TokaTCB *tcb = g_frame_map[i].tcb;
                if (tcb) {
                    atomic_fetch_add(&tcb->ref_count, 1);
                }
                toka_mutex_unlock(&g_rt_mutex);
                return tcb;
            }
        }
    }
    if (g_current_tcb) {
        TokaTCB *tcb = g_current_tcb;
        atomic_fetch_add(&tcb->ref_count, 1);
        toka_mutex_unlock(&g_rt_mutex);
        return tcb;
    }
    toka_mutex_unlock(&g_rt_mutex);
    return NULL;
}

static void unregister_frame_map(void *frame) {
    if (!frame) return;
    toka_mutex_lock(&g_rt_mutex);
    for (size_t i = 0; i < g_frame_map_count; ++i) {
        if (g_frame_map[i].frame == frame) {
            g_frame_map[i] = g_frame_map[g_frame_map_count - 1];
            g_frame_map_count--;
            break;
        }
    }
    toka_mutex_unlock(&g_rt_mutex);
}

void* toka_task_create(void *coro_frame, void *promise) {
    TokaTCB *tcb = (TokaTCB*)calloc(1, sizeof(TokaTCB));
    if (!tcb) return NULL;

    tcb->id = atomic_fetch_add(&g_next_task_id, 1);
    tcb->coro_frame = coro_frame;
    tcb->promise = promise;
    atomic_store(&tcb->task_schedule_generation, 0);
    atomic_store(&tcb->state, TOKA_TCB_CREATED);
    atomic_store(&tcb->cancel_requested, 0);
    atomic_store(&tcb->ref_count, 1);
    atomic_store(&tcb->detached, 0);
    atomic_store(&tcb->detached_counted, 0);
    atomic_store(&tcb->owner_released, 0);
    atomic_store(&tcb->active_wait_id, TOKA_NO_WAIT_ID);
    atomic_store(&tcb->active_slot_gen, 0);
    atomic_store(&tcb->active_child_tcb, 0);

    if (promise) {
        struct TokaPromiseHeader *hdr = (struct TokaPromiseHeader*)promise;
        hdr->self_tcb = tcb;
        atomic_store(&hdr->result_state, 0);
        atomic_store(&hdr->continuation, 0);
    }

    if (coro_frame) {
        register_frame_map(coro_frame, tcb);
    }
    return (void*)tcb;
}

int toka_task_start(void *tcb_ptr) {
    if (!tcb_ptr) return 0;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;

    uint32_t expected = TOKA_TCB_CREATED;
    if (atomic_compare_exchange_strong(&tcb->state, &expected, TOKA_TCB_QUEUED)) {
        atomic_store(&tcb->task_schedule_generation, 1);

        toka_mutex_lock(&g_rt_mutex);
        push_ready_queue_locked(tcb->id, 1, tcb);
        toka_mutex_unlock(&g_rt_mutex);
        return 1;
    }
    return 0;
}

int toka_task_suspend_and_register(void *tcb_ptr) {
    if (!tcb_ptr) return 0;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;

    uint32_t expected = TOKA_TCB_RUNNING;
    if (atomic_compare_exchange_strong(&tcb->state, &expected, TOKA_TCB_SUSPENDED)) {
        atomic_fetch_add(&tcb->task_schedule_generation, 1);
        return 1;
    }
    return 0;
}

int toka_task_prepare_suspend(void *coro_frame, uint64_t *out_task_id, uint64_t *out_gen) {
    TokaTCB *tcb = lookup_tcb_by_frame_retained(coro_frame);
    if (!tcb) return 0;

    uint32_t expected = TOKA_TCB_RUNNING;
    if (atomic_compare_exchange_strong(&tcb->state, &expected, TOKA_TCB_PREPARING)) {
        uint64_t new_gen = atomic_fetch_add(&tcb->task_schedule_generation, 1) + 1;
        if (out_task_id) *out_task_id = tcb->id;
        if (out_gen) *out_gen = new_gen;

        if (atomic_load(&tcb->cancel_requested)) {
            uint32_t prep_st = TOKA_TCB_PREPARING;
            atomic_compare_exchange_strong(&tcb->state, &prep_st, TOKA_TCB_PREPARING_WITH_PENDING_WAKE);
        }

        toka_task_release(tcb);
        return 1;
    }
    toka_task_release(tcb);
    return 0;
}

int toka_task_commit_suspend(void *coro_frame) {
    TokaTCB *tcb = lookup_tcb_by_frame_retained(coro_frame);
    if (!tcb) return 0;

    while (1) {
        uint32_t st = atomic_load(&tcb->state);
        if (st == TOKA_TCB_PREPARING) {
            uint32_t expected = TOKA_TCB_PREPARING;
            if (atomic_compare_exchange_strong(&tcb->state, &expected, TOKA_TCB_SUSPENDED)) {
                toka_task_release(tcb);
                return 1; // Committed to SUSPENDED
            }
            continue;
        }
        if (st == TOKA_TCB_PREPARING_WITH_PENDING_WAKE) {
            uint32_t expected = TOKA_TCB_PREPARING_WITH_PENDING_WAKE;
            if (atomic_compare_exchange_strong(&tcb->state, &expected, TOKA_TCB_QUEUED)) {
                toka_wait_registry_cancel_active(tcb);
                toka_mutex_lock(&g_rt_mutex);
                push_ready_queue_locked(tcb->id, atomic_load(&tcb->task_schedule_generation), tcb);
                toka_mutex_unlock(&g_rt_mutex);
                toka_task_release(tcb);
                return 1; // Immediately enqueued due to pending wake
            }
            continue;
        }
        toka_task_release(tcb);
        return 0;
    }
}

int toka_task_abort_suspend(void *coro_frame) {
    TokaTCB *tcb = lookup_tcb_by_frame_retained(coro_frame);
    if (!tcb) return 0;

    while (1) {
        uint32_t st = atomic_load(&tcb->state);
        if (st == TOKA_TCB_PREPARING || st == TOKA_TCB_PREPARING_WITH_PENDING_WAKE) {
            uint32_t expected = st;
            if (atomic_compare_exchange_strong(&tcb->state, &expected, TOKA_TCB_RUNNING)) {
                toka_task_release(tcb);
                return 1; // Aborted back to RUNNING!
            }
            continue;
        }
        toka_task_release(tcb);
        return 0;
    }
}

int toka_task_try_schedule(uint64_t task_id, uint64_t gen) {
    toka_mutex_lock(&g_rt_mutex);
    TokaTCB *target_tcb = NULL;
    for (size_t i = 0; i < g_frame_map_count; ++i) {
        if (g_frame_map[i].tcb && g_frame_map[i].tcb->id == task_id) {
            target_tcb = g_frame_map[i].tcb;
            atomic_fetch_add(&target_tcb->ref_count, 1);
            break;
        }
    }
    toka_mutex_unlock(&g_rt_mutex);

    if (!target_tcb) return 0;

    if (atomic_load(&target_tcb->task_schedule_generation) != gen) {
        toka_task_release(target_tcb);
        return 0; // Stale: Generation mismatch
    }

    while (1) {
        uint32_t st = atomic_load(&target_tcb->state);
        if (st == TOKA_TCB_PREPARING) {
            uint32_t expected = TOKA_TCB_PREPARING;
            if (atomic_compare_exchange_strong(&target_tcb->state, &expected, TOKA_TCB_PREPARING_WITH_PENDING_WAKE)) {
                toka_task_release(target_tcb);
                return 1; // Atomic pending wake set
            }
            continue;
        }
        if (st == TOKA_TCB_PREPARING_WITH_PENDING_WAKE) {
            toka_task_release(target_tcb);
            return 1; // Already pending wake
        }
        if (st == TOKA_TCB_SUSPENDED) {
            uint32_t expected = TOKA_TCB_SUSPENDED;
            if (atomic_compare_exchange_strong(&target_tcb->state, &expected, TOKA_TCB_QUEUED)) {
                toka_mutex_lock(&g_rt_mutex);
                push_ready_queue_locked(target_tcb->id, gen, target_tcb);
                toka_mutex_unlock(&g_rt_mutex);
                toka_task_release(target_tcb);
                return 1; // Scheduled
            }
            continue;
        }
        toka_task_release(target_tcb);
        return 0;
    }
}

int toka_task_schedule_frame_compat(void *frame) {
    TokaTCB *tcb = lookup_tcb_by_frame_retained(frame);
    if (!tcb) return 0;
    int res = toka_task_try_schedule(tcb->id, atomic_load(&tcb->task_schedule_generation));
    toka_task_release(tcb);
    return res;
}

int toka_task_pop_ready(uint64_t *out_task_id, uint64_t *out_gen, void **out_tcb_ptr) {
    while (1) {
        toka_mutex_lock(&g_rt_mutex);
        if (g_ready_count == 0 || g_ready_queue == NULL) {
            toka_mutex_unlock(&g_rt_mutex);
            return 0;
        }

        TokaScheduledItem item = g_ready_queue[g_ready_head];
        g_ready_head = (g_ready_head + 1) % g_ready_capacity;
        g_ready_count--;
        toka_mutex_unlock(&g_rt_mutex);

        if (!item.tcb) continue;

        uint32_t expected = TOKA_TCB_QUEUED;
        if (atomic_compare_exchange_strong(&item.tcb->state, &expected, TOKA_TCB_RUNNING)) {
            g_current_tcb = item.tcb;
            if (out_task_id) *out_task_id = item.task_id;
            if (out_gen) *out_gen = item.task_schedule_generation;
            if (out_tcb_ptr) *out_tcb_ptr = item.tcb;
            return 1;
        }

        toka_task_release(item.tcb);
    }
}

static uint32_t g_active_detached_task_count = 0;

uint32_t toka_task_active_detached_count(void) {
    toka_mutex_lock(&g_rt_mutex);
    uint32_t cnt = g_active_detached_task_count;
    toka_mutex_unlock(&g_rt_mutex);
    return cnt;
}

static void toka_task_clear_await_link(TokaTCB *child_tcb, TokaTCB *parent_tcb) {
    if (!parent_tcb) return;
    toka_mutex_lock(&g_rt_mutex);
    if (child_tcb && atomic_load(&child_tcb->parent_tcb) == (uintptr_t)parent_tcb) {
        atomic_store(&child_tcb->parent_tcb, 0);
    }
    if (!child_tcb || atomic_load(&parent_tcb->active_child_tcb) == (uintptr_t)child_tcb) {
        atomic_store(&parent_tcb->active_child_tcb, 0);
    }
    toka_mutex_unlock(&g_rt_mutex);
}

void toka_task_complete(void *promise_ptr) {
    if (!promise_ptr) return;
    struct TokaPromiseHeader *hdr = (struct TokaPromiseHeader*)promise_ptr;
    TokaTCB *tcb = (TokaTCB*)hdr->self_tcb;

    uint8_t expected_res = TOKA_RESULT_STATE_PENDING;
    atomic_compare_exchange_strong_explicit(&hdr->result_state, &expected_res, TOKA_RESULT_STATE_READYLIVE, memory_order_release, memory_order_relaxed);

    if (tcb) {
        uint32_t final_st = atomic_load(&tcb->cancel_requested) ? TOKA_TCB_COMPLETED_CANCELED : TOKA_TCB_COMPLETED;
        atomic_store(&tcb->state, final_st);
        atomic_store(&tcb->active_child_tcb, 0);
        atomic_store(&tcb->active_wait_id, TOKA_NO_WAIT_ID);

        toka_mutex_lock(&g_rt_mutex);
        uint8_t expected_counted = 1;
        if (atomic_compare_exchange_strong(&tcb->detached_counted, &expected_counted, 0)) {
            if (g_active_detached_task_count > 0) {
                g_active_detached_task_count--;
            }
        }
        TokaCompletionSubscriber *subs = NULL;
        uint32_t sub_count = tcb->subscriber_count;
        if (sub_count > 0 && tcb->subscribers) {
            subs = tcb->subscribers;
            tcb->subscribers = NULL;
            tcb->subscriber_count = 0;
            tcb->subscriber_capacity = 0;
        }
        toka_mutex_unlock(&g_rt_mutex);

        if (subs) {
            for (uint32_t i = 0; i < sub_count; i++) {
                toka_wait_registry_try_wake(subs[i].wait_id, subs[i].slot_gen);
            }
            free(subs);
        }

        toka_task_try_release_owner(tcb);
    }

    uintptr_t old_cont = atomic_exchange(&hdr->continuation, 1);
    if (old_cont > 1) {
        TokaTCB *awaiter_tcb = (TokaTCB*)old_cont;
        toka_task_clear_await_link(tcb, awaiter_tcb);
        toka_task_try_schedule(awaiter_tcb->id, atomic_load(&awaiter_tcb->task_schedule_generation));
        toka_task_release(awaiter_tcb);
    }
}

void toka_task_complete_canceled(void *promise_ptr) {
    if (!promise_ptr) return;
    struct TokaPromiseHeader *hdr = (struct TokaPromiseHeader*)promise_ptr;
    TokaTCB *tcb = (TokaTCB*)hdr->self_tcb;

    uint8_t expected_res = TOKA_RESULT_STATE_PENDING;
    atomic_compare_exchange_strong_explicit(&hdr->result_state, &expected_res, TOKA_RESULT_STATE_CANCELED, memory_order_release, memory_order_relaxed);

    if (tcb) {
        atomic_store(&tcb->state, TOKA_TCB_COMPLETED_CANCELED);
        atomic_store(&tcb->active_child_tcb, 0);
        atomic_store(&tcb->active_wait_id, TOKA_NO_WAIT_ID);

        toka_mutex_lock(&g_rt_mutex);
        uint8_t expected_counted = 1;
        if (atomic_compare_exchange_strong(&tcb->detached_counted, &expected_counted, 0)) {
            if (g_active_detached_task_count > 0) {
                g_active_detached_task_count--;
            }
        }
        TokaCompletionSubscriber *subs = NULL;
        uint32_t sub_count = tcb->subscriber_count;
        if (sub_count > 0 && tcb->subscribers) {
            subs = tcb->subscribers;
            tcb->subscribers = NULL;
            tcb->subscriber_count = 0;
            tcb->subscriber_capacity = 0;
        }
        toka_mutex_unlock(&g_rt_mutex);

        if (subs) {
            for (uint32_t i = 0; i < sub_count; i++) {
                toka_wait_registry_try_wake(subs[i].wait_id, subs[i].slot_gen);
            }
            free(subs);
        }

        toka_task_try_release_owner(tcb);
    }

    uintptr_t old_cont = atomic_exchange(&hdr->continuation, 1);
    if (old_cont > 1) {
        TokaTCB *awaiter_tcb = (TokaTCB*)old_cont;
        toka_task_clear_await_link(tcb, awaiter_tcb);
        toka_task_try_schedule(awaiter_tcb->id, atomic_load(&awaiter_tcb->task_schedule_generation));
        toka_task_release(awaiter_tcb);
    }
}

int toka_task_await_prepare(void *child_promise_ptr, void *parent_tcb_ptr) {
    if (!child_promise_ptr || !parent_tcb_ptr) return 0;
    struct TokaPromiseHeader *hdr = (struct TokaPromiseHeader*)child_promise_ptr;
    TokaTCB *parent_tcb = (TokaTCB*)parent_tcb_ptr;

    uint32_t expected_state = TOKA_TCB_RUNNING;
    if (!atomic_compare_exchange_strong(&parent_tcb->state, &expected_state, TOKA_TCB_SUSPENDED)) {
        return 0;
    }
    atomic_fetch_add(&parent_tcb->task_schedule_generation, 1);

    atomic_fetch_add(&parent_tcb->ref_count, 1);
    if (hdr->self_tcb) {
        toka_mutex_lock(&g_rt_mutex);
        atomic_store(&parent_tcb->active_child_tcb, (uintptr_t)hdr->self_tcb);
        atomic_store(&((TokaTCB*)hdr->self_tcb)->parent_tcb, (uintptr_t)parent_tcb);
        toka_mutex_unlock(&g_rt_mutex);
    }

    uintptr_t expected_cont = 0;
    uintptr_t desired_cont = (uintptr_t)parent_tcb;
    if (atomic_compare_exchange_strong(&hdr->continuation, &expected_cont, desired_cont)) {
        return 1;
    }

    toka_task_clear_await_link((TokaTCB*)hdr->self_tcb, parent_tcb);
    atomic_fetch_sub(&parent_tcb->ref_count, 1);
    atomic_store(&parent_tcb->state, TOKA_TCB_RUNNING);
    return 0;
}

int toka_task_register_continuation(void *child_promise_ptr, void *parent_tcb_ptr) {
    return toka_task_await_prepare(child_promise_ptr, parent_tcb_ptr);
}

void toka_task_detach(void *tcb_ptr) {
    if (!tcb_ptr) return;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;

    // 1. Transient retain to prevent UAF window during detach execution
    atomic_fetch_add(&tcb->ref_count, 1);

    // 2. Mark as detached
    atomic_store(&tcb->detached, 1);

    // 3. Linearized counter increment under g_rt_mutex
    toka_mutex_lock(&g_rt_mutex);
    uint32_t st = atomic_load(&tcb->state);
    if (!toka_tcb_is_terminal(st) && st != TOKA_TCB_CREATED) {
        uint8_t expected_counted = 0;
        if (atomic_compare_exchange_strong(&tcb->detached_counted, &expected_counted, 1)) {
            g_active_detached_task_count++;
        }
    }
    toka_mutex_unlock(&g_rt_mutex);

    // 4. Try release owner if completed or created
    toka_task_try_release_owner(tcb);

    // 5. Release transient reference
    toka_task_release(tcb);
}

void toka_tcb_get_wait_token(void *tcb_ptr, uint64_t *out_task_id, uint64_t *out_gen) {
    if (!tcb_ptr) return;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;
    if (out_task_id) *out_task_id = tcb->id;
    if (out_gen) *out_gen = atomic_load(&tcb->task_schedule_generation);
}

void toka_task_publish_result_state(void *promise_ptr, uint8_t state) {
    if (!promise_ptr) return;
    struct TokaPromiseHeader *hdr = (struct TokaPromiseHeader*)promise_ptr;
    atomic_store_explicit(&hdr->result_state, state, memory_order_release);
}

uint8_t toka_task_get_result_state(void *promise_ptr) {
    if (!promise_ptr) return 0;
    struct TokaPromiseHeader *hdr = (struct TokaPromiseHeader*)promise_ptr;
    return atomic_load_explicit(&hdr->result_state, memory_order_acquire);
}

// Stable ABI accessor for the typed result payload that follows the promise
// header. CodeGen must use this accessor instead of depending on the private
// header field offsets.
void *toka_task_result_value_ptr(void *promise_ptr) {
    if (!promise_ptr) return NULL;
    return (void *)((char *)promise_ptr + sizeof(struct TokaPromiseHeader));
}

int toka_task_take_result(void *promise_ptr) {
    if (!promise_ptr) return 0;
    struct TokaPromiseHeader *hdr = (struct TokaPromiseHeader*)promise_ptr;
    uint8_t expected = TOKA_RESULT_STATE_READYLIVE;
    if (atomic_compare_exchange_strong_explicit(&hdr->result_state, &expected, TOKA_RESULT_STATE_TAKEN, memory_order_acq_rel, memory_order_acquire)) {
        return 1;
    }
    uint8_t st = atomic_load_explicit(&hdr->result_state, memory_order_acquire);
    if (st == TOKA_RESULT_STATE_CANCELED) {
        return -1;
    }
    return 0;
}

void toka_task_clear_result_payload(void *promise_ptr) {
    if (!promise_ptr) return;
    void *val_ptr = toka_task_result_value_ptr(promise_ptr);
    if (val_ptr) {
        memset(val_ptr, 0xFF, 8);
    }
}

int32_t toka_get_errno_impl(void) {
    return (int32_t)errno;
}

void toka_disarm_enum(void *ptr) {
    if (ptr) {
        *(uint8_t*)ptr = 1;
    }
}

static void destroy_coro_frame(void *frame) {
    if (!frame) return;
    typedef void (*coro_fn_t)(void*);
    coro_fn_t *fn_ptrs = (coro_fn_t*)frame;
    if (fn_ptrs[1]) {
        fn_ptrs[1](frame);
    }
}

void toka_task_release(void *tcb_ptr) {
    if (!tcb_ptr) return;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;
    void *frame_to_destroy = NULL;
    TokaCompletionSubscriber *subs_to_free = NULL;
    TokaTCB **children_to_release = NULL;
    uint32_t child_count = 0;

    toka_mutex_lock(&g_rt_mutex);
    if (atomic_fetch_sub(&tcb->ref_count, 1) == 1) {
        if (tcb->coro_frame) {
            for (size_t i = 0; i < g_frame_map_count; ++i) {
                if (g_frame_map[i].tcb == tcb) {
                    g_frame_map[i] = g_frame_map[g_frame_map_count - 1];
                    g_frame_map_count--;
                    break;
                }
            }
            frame_to_destroy = tcb->coro_frame;
            tcb->coro_frame = NULL;
        }
        subs_to_free = tcb->subscribers;
        tcb->subscribers = NULL;
        tcb->subscriber_count = 0;
        tcb->subscriber_capacity = 0;
        children_to_release = tcb->cancel_children;
        child_count = tcb->cancel_child_count;
        tcb->cancel_children = NULL;
        tcb->cancel_child_count = 0;
        tcb->cancel_child_capacity = 0;

        toka_mutex_unlock(&g_rt_mutex);

        if (subs_to_free) {
            free(subs_to_free);
        }
        if (frame_to_destroy) {
            uint32_t st = atomic_load(&tcb->state);
            if (st == TOKA_TCB_COMPLETED) {
                free(frame_to_destroy);
            } else {
                destroy_coro_frame(frame_to_destroy);
            }
        }
        for (uint32_t i = 0; i < child_count; ++i) {
            toka_task_release(children_to_release[i]);
        }
        free(children_to_release);
        free(tcb);
        return;
    }
    toka_mutex_unlock(&g_rt_mutex);
}

void toka_task_retain(void *tcb_ptr) {
    if (!tcb_ptr) return;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;
    atomic_fetch_add(&tcb->ref_count, 1);
}

int toka_task_register_cancel_child(void *parent_frame, void *child_ptr) {
    if (!parent_frame || !child_ptr) return 0;
    TokaTCB *parent = lookup_tcb_by_frame_retained(parent_frame);
    if (!parent) return 0;
    TokaTCB *child = (TokaTCB*)child_ptr;
    int request_cancel = 0;

    toka_mutex_lock(&g_rt_mutex);
    for (uint32_t i = 0; i < parent->cancel_child_count; ++i) {
        if (parent->cancel_children[i] == child) {
            toka_mutex_unlock(&g_rt_mutex);
            toka_task_release(parent);
            return 1;
        }
    }
    if (parent->cancel_child_count >= parent->cancel_child_capacity) {
        uint32_t new_cap = parent->cancel_child_capacity == 0 ? 2 : parent->cancel_child_capacity * 2;
        TokaTCB **new_children = (TokaTCB**)realloc(parent->cancel_children, new_cap * sizeof(TokaTCB*));
        if (!new_children) {
            toka_mutex_unlock(&g_rt_mutex);
            toka_task_release(parent);
            return 0;
        }
        parent->cancel_children = new_children;
        parent->cancel_child_capacity = new_cap;
    }
    atomic_fetch_add(&child->ref_count, 1);
    parent->cancel_children[parent->cancel_child_count++] = child;
    request_cancel = atomic_load(&parent->cancel_requested) != 0;
    toka_mutex_unlock(&g_rt_mutex);

    if (request_cancel) {
        toka_task_request_cancel(child);
    }
    toka_task_release(parent);
    return 1;
}

int toka_task_unregister_cancel_child(void *parent_frame, void *child_ptr) {
    if (!parent_frame || !child_ptr) return 0;
    TokaTCB *parent = lookup_tcb_by_frame_retained(parent_frame);
    if (!parent) return 0;
    TokaTCB *removed = NULL;
    toka_mutex_lock(&g_rt_mutex);
    for (uint32_t i = 0; i < parent->cancel_child_count; ++i) {
        if (parent->cancel_children[i] == (TokaTCB*)child_ptr) {
            removed = parent->cancel_children[i];
            for (uint32_t j = i + 1; j < parent->cancel_child_count; ++j) {
                parent->cancel_children[j - 1] = parent->cancel_children[j];
            }
            parent->cancel_child_count--;
            break;
        }
    }
    toka_mutex_unlock(&g_rt_mutex);
    if (removed) toka_task_release(removed);
    toka_task_release(parent);
    return removed != NULL;
}

void* toka_tcb_get_coro_frame(void *tcb_ptr) {
    if (!tcb_ptr) return NULL;
    return ((TokaTCB*)tcb_ptr)->coro_frame;
}

int toka_tcb_is_done(void *tcb_ptr) {
    if (!tcb_ptr) return 1;
    uint32_t st = atomic_load(&((TokaTCB*)tcb_ptr)->state);
    return (st == TOKA_TCB_COMPLETED || st == TOKA_TCB_COMPLETED_CANCELED);
}

int toka_tcb_is_canceled(void *tcb_ptr) {
    if (!tcb_ptr) return 0;
    return atomic_load(&((TokaTCB*)tcb_ptr)->state) == TOKA_TCB_COMPLETED_CANCELED;
}

// === AR-P2 Generation-based WaitRegistry ===

typedef struct {
    uint32_t wait_id;
    uint32_t wait_slot_generation;
} TokaWaitToken;

typedef enum {
    TOKA_WAIT_STATE_WAITING = 0,
    TOKA_WAIT_STATE_WON = 1,
    TOKA_WAIT_STATE_CANCELLED = 2,
    TOKA_WAIT_STATE_EXPIRED = 3
} TokaWaitState;

typedef struct {
    TokaWaitToken token;
    uint64_t task_id;
    uint64_t task_schedule_generation;
    _Atomic uint32_t state;
    uint16_t source_tag;
    void *wait_set; // Reserved for AR-P5 WaitSet
    TokaTCB *tcb;   // Retained strong TCB pointer while registration active
    uint8_t in_use;
} TokaWaitRegistration;

static TokaWaitRegistration *g_wait_registry = NULL;
static size_t g_wait_registry_capacity = 0;
static size_t g_wait_registry_count = 0;

static void ensure_free_slots_locked(size_t needed_slots) {
    if (needed_slots > UINT32_MAX / 2) {
        fprintf(stderr, "Fatal error: Requested slots exceed maximum WaitRegistry capacity.\n");
        abort();
    }
    if (g_wait_registry == NULL) {
        g_wait_registry_capacity = 256;
        if (needed_slots > g_wait_registry_capacity) {
            g_wait_registry_capacity = needed_slots * 2;
        }
        g_wait_registry = (TokaWaitRegistration*)calloc(g_wait_registry_capacity, sizeof(TokaWaitRegistration));
        if (!g_wait_registry) {
            fprintf(stderr, "Fatal error: Out of memory during Toka WaitRegistry initialization.\n");
            abort();
        }
        for (uint32_t i = 0; i < (uint32_t)g_wait_registry_capacity; ++i) {
            g_wait_registry[i].token.wait_id = i;
            g_wait_registry[i].token.wait_slot_generation = 1;
        }
    } else if (g_wait_registry_capacity - g_wait_registry_count < needed_slots) {
        if (g_wait_registry_capacity >= (SIZE_MAX / 2) || g_wait_registry_capacity >= (UINT32_MAX / 2)) {
            fprintf(stderr, "Fatal error: WaitRegistry capacity overflow protection triggered.\n");
            abort();
        }
        size_t old_cap = g_wait_registry_capacity;
        size_t new_cap = g_wait_registry_capacity * 2;
        while (new_cap - g_wait_registry_count < needed_slots) {
            if (new_cap >= (UINT32_MAX / 2)) break;
            new_cap *= 2;
        }
        TokaWaitRegistration *new_reg = (TokaWaitRegistration*)realloc(g_wait_registry, new_cap * sizeof(TokaWaitRegistration));
        if (!new_reg) {
            fprintf(stderr, "Fatal error: Out of memory during Toka WaitRegistry expansion.\n");
            abort();
        }
        g_wait_registry = new_reg;
        g_wait_registry_capacity = new_cap;
        for (uint32_t i = (uint32_t)old_cap; i < (uint32_t)new_cap; ++i) {
            memset(&g_wait_registry[i], 0, sizeof(TokaWaitRegistration));
            g_wait_registry[i].token.wait_id = i;
            g_wait_registry[i].token.wait_slot_generation = 1;
        }
    }
}

int toka_wait_registry_allocate(uint64_t task_id, uint64_t gen, uint16_t source_tag, uint32_t *out_wait_id, uint32_t *out_slot_gen) {
    toka_mutex_lock(&g_rt_mutex);
    ensure_free_slots_locked(1);

    TokaTCB *tcb = NULL;
    for (size_t i = 0; i < g_frame_map_count; ++i) {
        if (g_frame_map[i].tcb && g_frame_map[i].tcb->id == task_id) {
            tcb = g_frame_map[i].tcb;
            atomic_fetch_add(&tcb->ref_count, 1);
            break;
        }
    }
    if (!tcb) {
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }

    uint32_t slot_idx = UINT32_MAX;
    for (size_t i = 0; i < g_wait_registry_capacity; ++i) {
        if (!g_wait_registry[i].in_use) {
            slot_idx = (uint32_t)i;
            break;
        }
    }
    if (slot_idx == UINT32_MAX) {
        ensure_free_slots_locked(1);
        for (size_t i = 0; i < g_wait_registry_capacity; ++i) {
            if (!g_wait_registry[i].in_use) {
                slot_idx = (uint32_t)i;
                break;
            }
        }
    }

    TokaWaitRegistration *reg = &g_wait_registry[slot_idx];
    reg->in_use = 1;
    reg->task_id = task_id;
    reg->task_schedule_generation = gen;
    atomic_store(&reg->state, TOKA_WAIT_STATE_WAITING);
    reg->source_tag = source_tag;
    reg->wait_set = NULL;
    reg->tcb = tcb;
    atomic_store(&tcb->active_wait_id, reg->token.wait_id);
    atomic_store(&tcb->active_slot_gen, reg->token.wait_slot_generation);
    g_wait_registry_count++;

    if (out_wait_id) *out_wait_id = reg->token.wait_id;
    if (out_slot_gen) *out_slot_gen = reg->token.wait_slot_generation;

    toka_mutex_unlock(&g_rt_mutex);
    return 1;
}

typedef struct {
    _Atomic uint32_t winner_wait_id; // 0 = WAITING, (wait_id + 1) = WON by wait_id
    _Atomic uint32_t ref_count;      // 2 for twin tokens
} TokaWaitSet;

int toka_wait_registry_allocate_pair(uint64_t task_id, uint64_t gen, uint16_t tag1, uint16_t tag2,
                                     uint32_t *out_id1, uint32_t *out_gen1,
                                     uint32_t *out_id2, uint32_t *out_gen2) {
    toka_mutex_lock(&g_rt_mutex);
    ensure_free_slots_locked(2);

    TokaTCB *tcb = NULL;
    for (size_t i = 0; i < g_frame_map_count; ++i) {
        if (g_frame_map[i].tcb && g_frame_map[i].tcb->id == task_id) {
            tcb = g_frame_map[i].tcb;
            atomic_fetch_add(&tcb->ref_count, 2);
            break;
        }
    }
    if (!tcb) {
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }

    uint32_t slot1 = UINT32_MAX, slot2 = UINT32_MAX;
    for (size_t i = 0; i < g_wait_registry_capacity; ++i) {
        if (!g_wait_registry[i].in_use) {
            if (slot1 == UINT32_MAX) slot1 = (uint32_t)i;
            else if (slot2 == UINT32_MAX) { slot2 = (uint32_t)i; break; }
        }
    }

    if (slot1 == UINT32_MAX || slot2 == UINT32_MAX) {
        atomic_fetch_sub(&tcb->ref_count, 2);
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }

    TokaWaitSet *ws = (TokaWaitSet*)calloc(1, sizeof(TokaWaitSet));
    if (!ws) {
        atomic_fetch_sub(&tcb->ref_count, 2);
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }
    atomic_store(&ws->winner_wait_id, 0);
    atomic_store(&ws->ref_count, 2);

    TokaWaitRegistration *reg1 = &g_wait_registry[slot1];
    reg1->in_use = 1;
    reg1->task_id = task_id;
    reg1->task_schedule_generation = gen;
    atomic_store(&reg1->state, TOKA_WAIT_STATE_WAITING);
    reg1->source_tag = tag1;
    reg1->wait_set = ws;
    reg1->tcb = tcb;
    g_wait_registry_count++;

    TokaWaitRegistration *reg2 = &g_wait_registry[slot2];
    reg2->in_use = 1;
    reg2->task_id = task_id;
    reg2->task_schedule_generation = gen;
    atomic_store(&reg2->state, TOKA_WAIT_STATE_WAITING);
    reg2->source_tag = tag2;
    reg2->wait_set = ws;
    reg2->tcb = tcb;
    g_wait_registry_count++;

    atomic_store(&tcb->active_wait_id, reg1->token.wait_id);
    atomic_store(&tcb->active_slot_gen, reg1->token.wait_slot_generation);

    if (out_id1) *out_id1 = reg1->token.wait_id;
    if (out_gen1) *out_gen1 = reg1->token.wait_slot_generation;
    if (out_id2) *out_id2 = reg2->token.wait_id;
    if (out_gen2) *out_gen2 = reg2->token.wait_slot_generation;

    toka_mutex_unlock(&g_rt_mutex);
    return 1;
}

int toka_wait_registry_allocate_nway(uint64_t task_id, uint64_t gen, uint16_t tag_base, uint32_t count,
                                      uint32_t *out_ids, uint32_t *out_gens) {
    if (count == 0 || !out_ids || !out_gens) return 0;
    toka_mutex_lock(&g_rt_mutex);
    ensure_free_slots_locked(count);

    TokaTCB *tcb = NULL;
    for (size_t i = 0; i < g_frame_map_count; ++i) {
        if (g_frame_map[i].tcb && g_frame_map[i].tcb->id == task_id) {
            tcb = g_frame_map[i].tcb;
            atomic_fetch_add(&tcb->ref_count, (int32_t)count);
            break;
        }
    }
    if (!tcb) {
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }

    uint32_t found = 0;
    for (size_t i = 0; i < g_wait_registry_capacity && found < count; ++i) {
        if (!g_wait_registry[i].in_use) {
            out_ids[found] = (uint32_t)i;
            found++;
        }
    }

    if (found < count) {
        atomic_fetch_sub(&tcb->ref_count, (int32_t)count);
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }

    TokaWaitSet *ws = (TokaWaitSet*)calloc(1, sizeof(TokaWaitSet));
    if (!ws) {
        atomic_fetch_sub(&tcb->ref_count, (int32_t)count);
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }
    atomic_store(&ws->winner_wait_id, 0);
    atomic_store(&ws->ref_count, (int32_t)count);

    for (uint32_t k = 0; k < count; ++k) {
        uint32_t slot = out_ids[k];
        TokaWaitRegistration *reg = &g_wait_registry[slot];
        reg->in_use = 1;
        reg->task_id = task_id;
        reg->task_schedule_generation = gen;
        atomic_store(&reg->state, TOKA_WAIT_STATE_WAITING);
        reg->source_tag = tag_base + (uint16_t)k;
        reg->wait_set = ws;
        reg->tcb = tcb;
        g_wait_registry_count++;

        out_ids[k] = reg->token.wait_id;
        out_gens[k] = reg->token.wait_slot_generation;
    }

    atomic_store(&tcb->active_wait_id, out_ids[0]);
    atomic_store(&tcb->active_slot_gen, out_gens[0]);

    toka_mutex_unlock(&g_rt_mutex);
    return 1;
}

typedef enum {
    TOKA_WAKE_STALE = 0,
    TOKA_WAKE_SINGLETON_WON = 1,
    TOKA_WAKE_PAIR_WON = 2,
    TOKA_WAKE_PAIR_DUPLICATE = 3,
    TOKA_WAKE_PAIR_LOST = 4
} TokaWakeOutcome;

int toka_wait_registry_try_wake(uint32_t wait_id, uint32_t slot_gen) {
    toka_mutex_lock(&g_rt_mutex);
    if (wait_id >= g_wait_registry_capacity) {
        toka_mutex_unlock(&g_rt_mutex);
        return TOKA_WAKE_STALE;
    }
    TokaWaitRegistration *reg = &g_wait_registry[wait_id];
    if (!reg->in_use || reg->token.wait_slot_generation != slot_gen) {
        toka_mutex_unlock(&g_rt_mutex);
        return TOKA_WAKE_STALE;
    }

    if (reg->wait_set) {
        TokaWaitSet *ws = (TokaWaitSet*)reg->wait_set;
        uint32_t target_winner = wait_id + 1;
        uint32_t current_winner = atomic_load(&ws->winner_wait_id);

        if (current_winner == TOKA_WAKE_GROUP_CANCELLED) {
            atomic_store(&reg->state, TOKA_WAIT_STATE_CANCELLED);
            toka_mutex_unlock(&g_rt_mutex);
            return TOKA_WAKE_PAIR_LOST;
        }

        if (current_winner == target_winner) {
            toka_mutex_unlock(&g_rt_mutex);
            return TOKA_WAKE_PAIR_DUPLICATE;
        }

        uint32_t expected = 0;
        if (!atomic_compare_exchange_strong(&ws->winner_wait_id, &expected, target_winner)) {
            atomic_store(&reg->state, TOKA_WAIT_STATE_CANCELLED);
            toka_mutex_unlock(&g_rt_mutex);
            return TOKA_WAKE_PAIR_LOST;
        }
    }

    uint32_t expected = TOKA_WAIT_STATE_WAITING;
    if (!atomic_compare_exchange_strong(&reg->state, &expected, TOKA_WAIT_STATE_WON)) {
        toka_mutex_unlock(&g_rt_mutex);
        return TOKA_WAKE_PAIR_LOST;
    }

    uint64_t tid = reg->task_id;
    uint64_t gen = reg->task_schedule_generation;
    int is_singleton = (reg->wait_set == NULL);

    TokaTCB *tcb_to_release = NULL;
    if (is_singleton) {
        if (reg->tcb) {
            atomic_store(&reg->tcb->active_wait_id, TOKA_NO_WAIT_ID);
            atomic_store(&reg->tcb->active_slot_gen, 0);
        }
        tcb_to_release = reg->tcb;
        reg->tcb = NULL;
        reg->in_use = 0;
        reg->token.wait_slot_generation++;
        if (reg->token.wait_slot_generation == 0) {
            reg->token.wait_slot_generation = 1;
        }
        g_wait_registry_count--;
    }

    toka_mutex_unlock(&g_rt_mutex);

    int sched_ok = toka_task_try_schedule(tid, gen);
    if (tcb_to_release) {
        toka_task_release(tcb_to_release);
    }
    if (!sched_ok) return TOKA_WAKE_STALE;
    return is_singleton ? TOKA_WAKE_SINGLETON_WON : TOKA_WAKE_PAIR_WON;
}

int toka_wait_registry_invalidate(uint32_t wait_id, uint32_t slot_gen) {
    toka_mutex_lock(&g_rt_mutex);
    if (wait_id >= g_wait_registry_capacity) {
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }
    TokaWaitRegistration *reg = &g_wait_registry[wait_id];
    if (!reg->in_use || reg->token.wait_slot_generation != slot_gen) {
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }

    uint32_t expected = TOKA_WAIT_STATE_WAITING;
    if (atomic_compare_exchange_strong(&reg->state, &expected, TOKA_WAIT_STATE_CANCELLED)) {
        toka_mutex_unlock(&g_rt_mutex);
        return 1;
    }
    toka_mutex_unlock(&g_rt_mutex);
    return 0;
}

int toka_wait_registry_is_winner(uint32_t wait_id, uint32_t slot_gen) {
    toka_mutex_lock(&g_rt_mutex);
    if (wait_id >= g_wait_registry_capacity) {
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }
    TokaWaitRegistration *reg = &g_wait_registry[wait_id];
    if (!reg->in_use || reg->token.wait_slot_generation != slot_gen) {
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }
    if (reg->wait_set) {
        TokaWaitSet *ws = (TokaWaitSet*)reg->wait_set;
        if (atomic_load(&ws->winner_wait_id) == TOKA_WAKE_GROUP_CANCELLED) {
            toka_mutex_unlock(&g_rt_mutex);
            return 0;
        }
    }
    int won = (atomic_load(&reg->state) == TOKA_WAIT_STATE_WON);
    toka_mutex_unlock(&g_rt_mutex);
    return won;
}

int toka_wait_registry_release(uint32_t wait_id, uint32_t slot_gen) {
    toka_mutex_lock(&g_rt_mutex);
    if (wait_id >= g_wait_registry_capacity) {
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }
    TokaWaitRegistration *reg = &g_wait_registry[wait_id];
    if (!reg->in_use || reg->token.wait_slot_generation != slot_gen) {
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }

    TokaWaitSet *ws_to_free = NULL;
    if (reg->wait_set) {
        TokaWaitSet *ws = (TokaWaitSet*)reg->wait_set;
        if (atomic_fetch_sub(&ws->ref_count, 1) == 1) {
            ws_to_free = ws;
        }
        reg->wait_set = NULL;
    }

    if (reg->tcb) {
        atomic_store(&reg->tcb->active_wait_id, TOKA_NO_WAIT_ID);
        atomic_store(&reg->tcb->active_slot_gen, 0);
    }
    TokaTCB *tcb_to_release = reg->tcb;
    reg->tcb = NULL;
    reg->in_use = 0;
    reg->token.wait_slot_generation++;
    if (reg->token.wait_slot_generation == 0) {
        reg->token.wait_slot_generation = 1;
    }
    g_wait_registry_count--;
    toka_mutex_unlock(&g_rt_mutex);

    if (ws_to_free) free(ws_to_free);
    if (tcb_to_release) {
        toka_task_release(tcb_to_release);
    }
    return 1;
}

typedef struct {
    TokaWaitSet *wait_set_to_free;
    TokaTCB *tcb_to_release;
    uint32_t tcb_release_count;
} TokaWaitSetCancelCleanup;

static int toka_wait_set_cancel_group_and_wake_locked(
    TokaWaitSet *ws,
    TokaWaitSetCancelCleanup *cleanup,
    int force_cancel
) {
    if (!ws || !cleanup) return 0;
    if (force_cancel) {
        uint32_t previous = atomic_exchange(&ws->winner_wait_id, TOKA_WAKE_GROUP_CANCELLED);
        if (previous == TOKA_WAKE_GROUP_CANCELLED) return 0;
    } else {
        uint32_t expected = 0;
        if (!atomic_compare_exchange_strong(&ws->winner_wait_id, &expected, TOKA_WAKE_GROUP_CANCELLED)) {
            return 0;
        }
    }

    TokaTCB *tcb_to_wake = NULL;
    uint64_t tid = 0, gen = 0;
    for (size_t i = 0; i < g_wait_registry_capacity; ++i) {
        TokaWaitRegistration *reg = &g_wait_registry[i];
        if (reg->in_use && reg->wait_set == ws) {
            atomic_store(&reg->state, TOKA_WAIT_STATE_CANCELLED);
            if (!tcb_to_wake && reg->tcb) {
                tcb_to_wake = reg->tcb;
                tid = reg->task_id;
                gen = reg->task_schedule_generation;
            }
            if (reg->tcb) {
                if (!cleanup->tcb_to_release) {
                    cleanup->tcb_to_release = reg->tcb;
                } else if (cleanup->tcb_to_release != reg->tcb) {
                    fprintf(stderr, "Fatal error: WaitSet registrations reference different tasks.\n");
                    abort();
                }
                cleanup->tcb_release_count++;
            }
            reg->tcb = NULL;
            reg->wait_set = NULL;
            reg->in_use = 0;
            reg->token.wait_slot_generation++;
            if (reg->token.wait_slot_generation == 0) {
                reg->token.wait_slot_generation = 1;
            }
            g_wait_registry_count--;
        }
    }

    if (tcb_to_wake) {
        atomic_store(&tcb_to_wake->active_wait_id, TOKA_NO_WAIT_ID);
        atomic_store(&tcb_to_wake->active_slot_gen, 0);
        uint32_t expected_st = TOKA_TCB_SUSPENDED;
        if (atomic_compare_exchange_strong(&tcb_to_wake->state, &expected_st, TOKA_TCB_QUEUED)) {
            push_ready_queue_locked(tid, gen, tcb_to_wake);
        }
    }
    cleanup->wait_set_to_free = ws;
    atomic_store(&ws->ref_count, 0);
    return 1;
}

static void toka_wait_set_finish_cancel_cleanup(TokaWaitSetCancelCleanup *cleanup) {
    if (!cleanup) return;
    for (uint32_t i = 0; i < cleanup->tcb_release_count; ++i) {
        toka_task_release(cleanup->tcb_to_release);
    }
    free(cleanup->wait_set_to_free);
}

static void toka_wait_registry_cancel_active(TokaTCB *tcb) {
    if (!tcb) return;
    TokaWaitSetCancelCleanup cleanup = {0};
    uint32_t singleton_id = TOKA_NO_WAIT_ID;
    uint32_t singleton_gen = 0;

    toka_mutex_lock(&g_rt_mutex);
    uint32_t wid = atomic_load(&tcb->active_wait_id);
    uint32_t wgen = atomic_load(&tcb->active_slot_gen);
    if (wid != TOKA_NO_WAIT_ID && wid < g_wait_registry_capacity) {
        TokaWaitRegistration *reg = &g_wait_registry[wid];
        if (reg->in_use && reg->token.wait_slot_generation == wgen) {
            if (reg->wait_set) {
                toka_wait_set_cancel_group_and_wake_locked(
                    (TokaWaitSet*)reg->wait_set,
                    &cleanup,
                    1
                );
            } else {
                singleton_id = wid;
                singleton_gen = wgen;
            }
        }
    }
    toka_mutex_unlock(&g_rt_mutex);

    toka_wait_set_finish_cancel_cleanup(&cleanup);
    if (singleton_id != TOKA_NO_WAIT_ID) {
        toka_wait_registry_release(singleton_id, singleton_gen);
    }
    atomic_store(&tcb->active_wait_id, TOKA_NO_WAIT_ID);
    atomic_store(&tcb->active_slot_gen, 0);
}

int toka_wait_set_cancel_group_and_wake(void *wait_set_ptr) {
    if (!wait_set_ptr) return 0;
    TokaWaitSetCancelCleanup cleanup = {0};
    toka_mutex_lock(&g_rt_mutex);
    int canceled = toka_wait_set_cancel_group_and_wake_locked(
        (TokaWaitSet*)wait_set_ptr,
        &cleanup,
        0
    );
    toka_mutex_unlock(&g_rt_mutex);
    toka_wait_set_finish_cancel_cleanup(&cleanup);
    return canceled;
}

int toka_task_request_cancel(void *tcb_ptr) {
    if (!tcb_ptr) return 0;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;
    atomic_store(&tcb->cancel_requested, 1);

    TokaTCB *child_tcb = NULL;
    TokaTCB **cancel_children = NULL;
    uint32_t cancel_child_count = 0;
    toka_mutex_lock(&g_rt_mutex);
    uintptr_t child_val = atomic_load(&tcb->active_child_tcb);
    if (child_val != 0) {
        child_tcb = (TokaTCB*)child_val;
        atomic_fetch_add(&child_tcb->ref_count, 1);
    }
    cancel_child_count = tcb->cancel_child_count;
    if (cancel_child_count > 0) {
        cancel_children = (TokaTCB**)malloc(cancel_child_count * sizeof(TokaTCB*));
        if (!cancel_children) {
            toka_mutex_unlock(&g_rt_mutex);
            fprintf(stderr, "Fatal error: unable to snapshot cancellation children.\n");
            abort();
        }
        for (uint32_t i = 0; i < cancel_child_count; ++i) {
            cancel_children[i] = tcb->cancel_children[i];
            atomic_fetch_add(&cancel_children[i]->ref_count, 1);
        }
    }
    toka_mutex_unlock(&g_rt_mutex);
    if (child_tcb) {
        toka_task_request_cancel(child_tcb);
        toka_task_release(child_tcb);
    }
    for (uint32_t i = 0; i < cancel_child_count; ++i) {
        toka_task_request_cancel(cancel_children[i]);
        toka_task_release(cancel_children[i]);
    }
    free(cancel_children);

    toka_mutex_lock(&g_rt_mutex);
    uint32_t expected_st = TOKA_TCB_CREATED;
    if (atomic_compare_exchange_strong(&tcb->state, &expected_st, TOKA_TCB_COMPLETED_CANCELED)) {
        toka_mutex_unlock(&g_rt_mutex);
        if (tcb->promise) {
            toka_task_complete_canceled(tcb->promise);
        } else {
            toka_task_try_release_owner(tcb);
        }
        return 1;
    }
    uint32_t wid = atomic_load(&tcb->active_wait_id);
    uint32_t wgen = atomic_load(&tcb->active_slot_gen);
    uint32_t st = atomic_load(&tcb->state);
    if (wid != TOKA_NO_WAIT_ID && wid < g_wait_registry_capacity) {
        TokaWaitRegistration *reg = &g_wait_registry[wid];
        if (reg->in_use && reg->wait_set) {
            TokaWaitSetCancelCleanup cleanup = {0};
            toka_wait_set_cancel_group_and_wake_locked(
                (TokaWaitSet*)reg->wait_set,
                &cleanup,
                1
            );
            if (st == TOKA_TCB_PREPARING) {
                uint32_t expected = TOKA_TCB_PREPARING;
                atomic_compare_exchange_strong(
                    &tcb->state,
                    &expected,
                    TOKA_TCB_PREPARING_WITH_PENDING_WAKE
                );
            }
            toka_mutex_unlock(&g_rt_mutex);
            toka_wait_set_finish_cancel_cleanup(&cleanup);
            return 1;
        }
    }
    if (st == TOKA_TCB_SUSPENDED) {
        if (wid != TOKA_NO_WAIT_ID && wid < g_wait_registry_capacity) {
            toka_mutex_unlock(&g_rt_mutex);
            toka_wait_registry_try_wake(wid, wgen);
        } else {
            toka_mutex_unlock(&g_rt_mutex);
            uint32_t expected = TOKA_TCB_SUSPENDED;
            if (atomic_compare_exchange_strong(&tcb->state, &expected, TOKA_TCB_QUEUED)) {
                toka_mutex_lock(&g_rt_mutex);
                uint64_t gen = atomic_load(&tcb->task_schedule_generation);
                push_ready_queue_locked(tcb->id, gen, tcb);
                toka_mutex_unlock(&g_rt_mutex);
            }
        }
        return 1;
    }
    if (st == TOKA_TCB_PREPARING) {
        uint32_t expected = TOKA_TCB_PREPARING;
        atomic_compare_exchange_strong(&tcb->state, &expected, TOKA_TCB_PREPARING_WITH_PENDING_WAKE);
    }
    toka_mutex_unlock(&g_rt_mutex);
    return 1;
}

int toka_task_is_cancel_requested(void *tcb_ptr) {
    if (!tcb_ptr) return 0;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;
    TokaTCB *curr = tcb;
    int canceled = 0;
    toka_mutex_lock(&g_rt_mutex);
    while (curr) {
        if (atomic_load(&curr->cancel_requested)) {
            canceled = 1;
            break;
        }
        curr = (TokaTCB*)atomic_load(&curr->parent_tcb);
    }
    toka_mutex_unlock(&g_rt_mutex);
    return canceled;
}

int toka_task_is_current_canceled(void *coro_frame) {
    if (!coro_frame) return 0;
    TokaTCB *tcb = lookup_tcb_by_frame_retained(coro_frame);
    int canceled = 0;
    TokaTCB *curr = tcb;
    toka_mutex_lock(&g_rt_mutex);
    while (curr) {
        if (atomic_load(&curr->cancel_requested)) {
            canceled = 1;
            break;
        }
        curr = (TokaTCB*)atomic_load(&curr->parent_tcb);
    }
    toka_mutex_unlock(&g_rt_mutex);
    if (tcb) {
        toka_task_release(tcb);
    }
    return canceled;
}

uint32_t toka_rt_live_tcb_count(void) {
    toka_mutex_lock(&g_rt_mutex);
    uint32_t cnt = 0;
    for (size_t i = 0; i < g_frame_map_count; ++i) {
        if (g_frame_map[i].tcb) cnt++;
    }
    toka_mutex_unlock(&g_rt_mutex);
    return cnt;
}

uint32_t toka_rt_live_wait_registry_count(void) {
    toka_mutex_lock(&g_rt_mutex);
    uint32_t cnt = g_wait_registry_count;
    toka_mutex_unlock(&g_rt_mutex);
    return cnt;
}

void toka_rt_dump_wait_registry(void) {
    toka_mutex_lock(&g_rt_mutex);
    for (size_t i = 0; i < g_wait_registry_capacity; ++i) {
        TokaWaitRegistration *reg = &g_wait_registry[i];
        if (reg->in_use) {
            fprintf(stderr, "wait[%zu] gen=%u state=%u tag=%u task=%llu tcb_state=%u set=%p\n",
                    i,
                    reg->token.wait_slot_generation,
                    atomic_load(&reg->state),
                    (unsigned)reg->source_tag,
                    (unsigned long long)reg->task_id,
                    reg->tcb ? atomic_load(&reg->tcb->state) : 999u,
                    reg->wait_set);
        }
    }
    toka_mutex_unlock(&g_rt_mutex);
}

int toka_task_subscribe_completion(void *tcb_ptr, uint32_t wait_id, uint32_t slot_gen) {
    if (!tcb_ptr) return 0;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;
    toka_mutex_lock(&g_rt_mutex);
    uint32_t st = atomic_load(&tcb->state);
    if (st == TOKA_TCB_COMPLETED || st == TOKA_TCB_COMPLETED_CANCELED) {
        toka_mutex_unlock(&g_rt_mutex);
        toka_wait_registry_try_wake(wait_id, slot_gen);
        return 1;
    }
    if (tcb->subscriber_count >= tcb->subscriber_capacity) {
        uint32_t new_cap = tcb->subscriber_capacity == 0 ? 4 : tcb->subscriber_capacity * 2;
        TokaCompletionSubscriber *new_subs = (TokaCompletionSubscriber*)realloc(tcb->subscribers, new_cap * sizeof(TokaCompletionSubscriber));
        if (!new_subs) {
            toka_mutex_unlock(&g_rt_mutex);
            return 0;
        }
        tcb->subscribers = new_subs;
        tcb->subscriber_capacity = new_cap;
    }
    tcb->subscribers[tcb->subscriber_count].wait_id = wait_id;
    tcb->subscribers[tcb->subscriber_count].slot_gen = slot_gen;
    tcb->subscriber_count++;
    toka_mutex_unlock(&g_rt_mutex);
    return 1;
}

int toka_task_unsubscribe_completion(void *tcb_ptr, uint32_t wait_id, uint32_t slot_gen) {
    if (!tcb_ptr) return 0;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;
    toka_mutex_lock(&g_rt_mutex);
    for (uint32_t i = 0; i < tcb->subscriber_count; i++) {
        if (tcb->subscribers[i].wait_id == wait_id && tcb->subscribers[i].slot_gen == slot_gen) {
            for (uint32_t j = i + 1; j < tcb->subscriber_count; j++) {
                tcb->subscribers[j - 1] = tcb->subscribers[j];
            }
            tcb->subscriber_count--;
            toka_mutex_unlock(&g_rt_mutex);
            return 1;
        }
    }
    toka_mutex_unlock(&g_rt_mutex);
    return 0;
}

#if defined(__linux__) || defined(__APPLE__)
#ifdef __linux__
#include <sys/epoll.h>
#endif
#ifdef __APPLE__
#include <sys/event.h>
#include <sys/time.h>
#endif

typedef struct {
    uint64_t read_key;
    uint64_t write_key;
} TokaReactorFdBinding;

static TokaReactorFdBinding g_reactor_fd_table[65536];
static int g_global_reactor_fd = -1;

void toka_reactor_del_fd(int rfd, int fd);

int32_t toka_reactor_register_fd(int32_t rfd) {
    g_global_reactor_fd = rfd;
    return 0;
}

void toka_trace_cleanup_fd(int32_t fd) {
    if (g_global_reactor_fd >= 0 && fd >= 0) {
        toka_reactor_del_fd(g_global_reactor_fd, fd);
    }
}

void toka_reactor_del_fd(int rfd, int fd) {
    if (fd < 0 || fd >= 65536) return;
    uint64_t r_key = 0, w_key = 0;
    toka_mutex_lock(&g_rt_mutex);
    r_key = g_reactor_fd_table[fd].read_key;
    w_key = g_reactor_fd_table[fd].write_key;
    g_reactor_fd_table[fd].read_key = 0;
    g_reactor_fd_table[fd].write_key = 0;

#ifdef __linux__
    epoll_ctl(rfd, EPOLL_CTL_DEL, fd, NULL);
#endif
#ifdef __APPLE__
    struct kevent evs[2];
    int n_evs = 0;
    if (r_key != 0) {
        EV_SET(&evs[n_evs++], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    }
    if (w_key != 0) {
        EV_SET(&evs[n_evs++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    }
    if (n_evs > 0) {
        kevent(rfd, evs, n_evs, NULL, 0, NULL);
    }
#endif
    toka_mutex_unlock(&g_rt_mutex);

    if (r_key != 0) {
        uint32_t wid = (uint32_t)(r_key >> 32);
        uint32_t wgen = (uint32_t)r_key;
        toka_wait_registry_try_wake(wid, wgen);
    }
    if (w_key != 0) {
        uint32_t wid = (uint32_t)(w_key >> 32);
        uint32_t wgen = (uint32_t)w_key;
        toka_wait_registry_try_wake(wid, wgen);
    }
}

void toka_reactor_del_read(int rfd, int fd, uint64_t expected_key) {
    if (fd < 0 || fd >= 65536) return;
    toka_mutex_lock(&g_rt_mutex);
    if (g_reactor_fd_table[fd].read_key != expected_key) {
        toka_mutex_unlock(&g_rt_mutex);
        return;
    }
    g_reactor_fd_table[fd].read_key = 0;

#ifdef __linux__
    uint64_t rem_w = g_reactor_fd_table[fd].write_key;
    if (rem_w == 0) {
        epoll_ctl(rfd, EPOLL_CTL_DEL, fd, NULL);
    } else {
        struct epoll_event ev;
        ev.events = EPOLLOUT | EPOLLONESHOT;
        ev.data.u64 = (uint64_t)fd;
        epoll_ctl(rfd, EPOLL_CTL_MOD, fd, &ev);
    }
#endif
#ifdef __APPLE__
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    kevent(rfd, &ev, 1, NULL, 0, NULL);
#endif
    toka_mutex_unlock(&g_rt_mutex);
}

void toka_reactor_del_write(int rfd, int fd, uint64_t expected_key) {
    if (fd < 0 || fd >= 65536) return;
    toka_mutex_lock(&g_rt_mutex);
    if (g_reactor_fd_table[fd].write_key != expected_key) {
        toka_mutex_unlock(&g_rt_mutex);
        return;
    }
    g_reactor_fd_table[fd].write_key = 0;

#ifdef __linux__
    uint64_t rem_r = g_reactor_fd_table[fd].read_key;
    if (rem_r == 0) {
        epoll_ctl(rfd, EPOLL_CTL_DEL, fd, NULL);
    } else {
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLONESHOT;
        ev.data.u64 = (uint64_t)fd;
        epoll_ctl(rfd, EPOLL_CTL_MOD, fd, &ev);
    }
#endif
#ifdef __APPLE__
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(rfd, &ev, 1, NULL, 0, NULL);
#endif
    toka_mutex_unlock(&g_rt_mutex);
}

int toka_reactor_add_read(int rfd, int fd, uint64_t key) {
    if (fd < 0 || fd >= 65536) return -1;
    toka_mutex_lock(&g_rt_mutex);

    if (g_reactor_fd_table[fd].read_key != 0 && g_reactor_fd_table[fd].read_key != key) {
        toka_mutex_unlock(&g_rt_mutex);
        return -1;
    }

    g_reactor_fd_table[fd].read_key = key;

#ifdef __linux__
    uint32_t events = EPOLLIN | EPOLLONESHOT;
    if (g_reactor_fd_table[fd].write_key != 0) {
        events |= EPOLLOUT;
    }

    struct epoll_event ev;
    ev.events = events;
    ev.data.u64 = (uint64_t)fd;

    int res = epoll_ctl(rfd, EPOLL_CTL_ADD, fd, &ev);
    if (res != 0) {
        res = epoll_ctl(rfd, EPOLL_CTL_MOD, fd, &ev);
    }
    if (res != 0) {
        g_reactor_fd_table[fd].read_key = 0;
        toka_mutex_unlock(&g_rt_mutex);
        return -1;
    }
#endif
#ifdef __APPLE__
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_ONESHOT, 0, 0, (void*)(uintptr_t)key);
    int res = kevent(rfd, &ev, 1, NULL, 0, NULL);
    if (res != 0) {
        g_reactor_fd_table[fd].read_key = 0;
        toka_mutex_unlock(&g_rt_mutex);
        return -1;
    }
#endif
    toka_mutex_unlock(&g_rt_mutex);
    return 0;
}

int toka_reactor_add_write(int rfd, int fd, uint64_t key) {
    if (fd < 0 || fd >= 65536) return -1;
    toka_mutex_lock(&g_rt_mutex);

    if (g_reactor_fd_table[fd].write_key != 0 && g_reactor_fd_table[fd].write_key != key) {
        toka_mutex_unlock(&g_rt_mutex);
        return -1;
    }

    g_reactor_fd_table[fd].write_key = key;

#ifdef __linux__
    uint32_t events = EPOLLOUT | EPOLLONESHOT;
    if (g_reactor_fd_table[fd].read_key != 0) {
        events |= EPOLLIN;
    }

    struct epoll_event ev;
    ev.events = events;
    ev.data.u64 = (uint64_t)fd;

    int res = epoll_ctl(rfd, EPOLL_CTL_ADD, fd, &ev);
    if (res != 0) {
        res = epoll_ctl(rfd, EPOLL_CTL_MOD, fd, &ev);
    }
    if (res != 0) {
        g_reactor_fd_table[fd].write_key = 0;
        toka_mutex_unlock(&g_rt_mutex);
        return -1;
    }
#endif
#ifdef __APPLE__
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_ONESHOT, 0, 0, (void*)(uintptr_t)key);
    int res = kevent(rfd, &ev, 1, NULL, 0, NULL);
    if (res != 0) {
        g_reactor_fd_table[fd].write_key = 0;
        toka_mutex_unlock(&g_rt_mutex);
        return -1;
    }
#endif
    toka_mutex_unlock(&g_rt_mutex);
    return 0;
}

#ifdef __linux__
int toka_linux_epoll_wait(int epfd, int timeout_ms, uint64_t *out_keys, int max_events) {
    if (max_events <= 0) return 0;
    int epoll_max = max_events > 64 ? 64 : max_events;

    struct epoll_event events_buf[64];
    int n = epoll_wait(epfd, events_buf, epoll_max, timeout_ms);
    if (n <= 0) return n;

    int out_count = 0;
    toka_mutex_lock(&g_rt_mutex);

    for (int i = 0; i < n; i++) {
        int fd = (int)events_buf[i].data.u64;
        uint32_t ev_mask = events_buf[i].events;

        if (fd >= 0 && fd < 65536) {
            uint64_t r_key = g_reactor_fd_table[fd].read_key;
            uint64_t w_key = g_reactor_fd_table[fd].write_key;

            int read_ready = (ev_mask & (EPOLLIN | EPOLLHUP | EPOLLERR)) && (r_key != 0);
            int write_ready = (ev_mask & (EPOLLOUT | EPOLLHUP | EPOLLERR)) && (w_key != 0);

            if (read_ready) {
                if (out_count < max_events) {
                    out_keys[out_count++] = r_key;
                    g_reactor_fd_table[fd].read_key = 0;
                }
            }
            if (write_ready) {
                if (out_count < max_events) {
                    out_keys[out_count++] = w_key;
                    g_reactor_fd_table[fd].write_key = 0;
                }
            }

            uint64_t rem_r = g_reactor_fd_table[fd].read_key;
            uint64_t rem_w = g_reactor_fd_table[fd].write_key;
            if (rem_r != 0 || rem_w != 0) {
                struct epoll_event mod_ev;
                mod_ev.events = EPOLLONESHOT;
                if (rem_r != 0) mod_ev.events |= EPOLLIN;
                if (rem_w != 0) mod_ev.events |= EPOLLOUT;
                mod_ev.data.u64 = (uint64_t)fd;
                epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &mod_ev);
            }
        }
    }

    toka_mutex_unlock(&g_rt_mutex);
    return out_count;
}
#endif

int toka_reactor_wait(int rfd, int timeout_ms, uint64_t *out_keys, int max_events) {
    if (max_events <= 0) return 0;
#ifdef __linux__
    return toka_linux_epoll_wait(rfd, timeout_ms, out_keys, max_events);
#endif
#ifdef __APPLE__
    int k_max = max_events > 64 ? 64 : max_events;
    struct kevent events_buf[64];
    struct timespec ts;
    struct timespec *pts = NULL;
    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000;
        pts = &ts;
    }
    int n = kevent(rfd, NULL, 0, events_buf, k_max, pts);
    if (n <= 0) return n;

    int out_count = 0;
    toka_mutex_lock(&g_rt_mutex);
    for (int i = 0; i < n; i++) {
        int fd = (int)events_buf[i].ident;
        uint64_t key = (uint64_t)(uintptr_t)events_buf[i].udata;
        if (fd >= 0 && fd < 65536 && key != 0) {
            if (events_buf[i].filter == EVFILT_READ) {
                if (g_reactor_fd_table[fd].read_key == key) {
                    g_reactor_fd_table[fd].read_key = 0;
                    if (out_count < max_events) {
                        out_keys[out_count++] = key;
                    }
                }
            } else if (events_buf[i].filter == EVFILT_WRITE) {
                if (g_reactor_fd_table[fd].write_key == key) {
                    g_reactor_fd_table[fd].write_key = 0;
                    if (out_count < max_events) {
                        out_keys[out_count++] = key;
                    }
                }
            }
        }
    }
    toka_mutex_unlock(&g_rt_mutex);
    return out_count;
#endif
    return 0;
}
#endif

#ifndef __linux__
void toka_linux_epoll_del_fd(int epfd, int fd) {}
void toka_linux_epoll_del_read(int epfd, int fd, uint64_t expected_key) {}
void toka_linux_epoll_del_write(int epfd, int fd, uint64_t expected_key) {}
#endif

#ifdef __wasi__
extern int __wasm_argc;
extern char **__wasm_argv;

int toka_wasi_argc() {
    return __wasm_argc;
}

const char* toka_wasi_argv(int index) {
    if (index >= 0 && index < __wasm_argc) {
        return __wasm_argv[index];
    }
    return "";
}


// WASI thread stubs
int pthread_create(void *thread, const void *attr, void *(*start_routine)(void*), void *arg) { return -1; }
int pthread_join(void *thread, void **retval) { return -1; }
int pthread_detach(void *thread) { return -1; }
int pthread_mutex_init(void *mutex, const void *attr) { return 0; }
int pthread_mutex_destroy(void *mutex) { return 0; }
int pthread_mutex_lock(void *mutex) { return 0; }
int pthread_mutex_unlock(void *mutex) { return 0; }

// WASI compiler builtin __muloti4 stub
typedef int ti_int __attribute__((mode(TI)));
ti_int __muloti4(ti_int a, ti_int b, int *overflow) {
    long long al = (long long)a;
    long long bl = (long long)b;
    long long result_l = al * bl;
    if (al != 0 && result_l / al != bl) {
        *overflow = 1;
    } else {
        *overflow = 0;
    }
    return (ti_int)result_l;
}
#endif
