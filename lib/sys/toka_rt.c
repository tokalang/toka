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

void* __toka_get_coro_handle(void* task_handle_ptr) {
    if (!task_handle_ptr) return NULL;
    return *(void**)task_handle_ptr;
}

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
