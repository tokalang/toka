#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef TOKA_HAS_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#elif defined(__linux__)
#include <sys/random.h>
#include <fcntl.h>
#include <unistd.h>
#endif

void* toka_localtime_r(const time_t *timep, struct tm *result) {
#ifdef _WIN32
    if (localtime_s(result, timep) == 0) { return result; }
    return NULL;
#else
    return localtime_r(timep, result);
#endif
}

int toka_random_bytes(void *buf, size_t len) {
#ifndef _WIN32
    static int sigpipe_ignored = 0;
    if (!sigpipe_ignored) {
        signal(SIGPIPE, SIG_IGN);
        sigpipe_ignored = 1;
    }
#endif
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    arc4random_buf(buf, len);
    return 0;
#elif defined(_WIN32)
    return (BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0) ? 0 : -1;
#elif defined(__linux__)
    ssize_t res = getrandom(buf, len, 0);
    if (res == (ssize_t)len) {
        return 0;
    }
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    ssize_t r = read(fd, buf, len);
    close(fd);
    return (r == (ssize_t)len) ? 0 : -1;
#else
    return -1;
#endif
}

// Service shutdown capture deliberately has no scheduler or allocator path.
// POSIX only permits async-signal-safe work in a signal handler; retaining the
// first signal in sig_atomic_t lets ordinary Toka task code decide when and how
// to cancel service work.  Delivery is a separate one-shot flag: take never
// clears the recorded signal, so an interrupt arriving during a poll cannot be
// erased by ordinary task code.
#if !defined(_WIN32) && !defined(__wasi__)
static volatile sig_atomic_t toka_shutdown_signal = 0;
static volatile sig_atomic_t toka_shutdown_signal_delivered = 0;

static void toka_shutdown_signal_handler(int signal_number) {
    if (toka_shutdown_signal == 0) {
        toka_shutdown_signal = signal_number;
    }
}

int toka_shutdown_signal_supported(void) {
    return 1;
}

int toka_shutdown_signal_install(void) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = toka_shutdown_signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction(SIGINT, &action, NULL) != 0) return -1;
    if (sigaction(SIGTERM, &action, NULL) != 0) return -1;
    return 0;
}

int toka_shutdown_signal_take(void) {
    if (toka_shutdown_signal_delivered != 0) return 0;
    sig_atomic_t observed = toka_shutdown_signal;
    if (observed != 0) toka_shutdown_signal_delivered = 1;
    return (int)observed;
}

int toka_shutdown_signal_raise_for_test(int signal_number) {
    if (signal_number != SIGINT && signal_number != SIGTERM) return -1;
    return raise(signal_number);
}
#else
int toka_shutdown_signal_supported(void) { return 0; }
int toka_shutdown_signal_install(void) { return -1; }
int toka_shutdown_signal_take(void) { return 0; }
int toka_shutdown_signal_raise_for_test(int signal_number) {
    (void)signal_number;
    return -1;
}
#endif

// Replace a path through a uniquely-created sibling file. POSIX rename makes
// the target replacement atomically visible to readers on the same filesystem.
// This is intentionally an atomic-visibility primitive, not a crash-durable
// transaction: callers that need durability must use a future fsync contract.
int toka_atomic_write_file(const char *path, const unsigned char *data, size_t len) {
#if defined(_WIN32) || defined(__wasi__)
    (void)path;
    (void)data;
    (void)len;
    return -1;
#else
    if (!path || (!data && len != 0)) return -1;

    struct stat existing;
    int preserve_mode = stat(path, &existing) == 0 && S_ISREG(existing.st_mode);

    static const char suffix[] = ".toka-tmp-XXXXXX";
    size_t path_len = strlen(path);
    char *temporary = malloc(path_len + sizeof(suffix));
    if (!temporary) return -1;
    memcpy(temporary, path, path_len);
    memcpy(temporary + path_len, suffix, sizeof(suffix));

    int fd = mkstemp(temporary);
    if (fd < 0) {
        free(temporary);
        return -1;
    }

    if (preserve_mode && fchmod(fd, existing.st_mode & 0777) != 0) {
        close(fd);
        unlink(temporary);
        free(temporary);
        return -1;
    }

    size_t written = 0;
    int status = 0;
    while (written < len) {
        ssize_t count = write(fd, data + written, len - written);
        if (count > 0) {
            written += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        status = -1;
        break;
    }
    if (close(fd) != 0) status = -1;
    if (status == 0 && rename(temporary, path) != 0) status = -1;
    if (status != 0) unlink(temporary);
    free(temporary);
    return status;
#endif
}

// A live, exclusively-created temporary file for APIs that must hand a path to
// another process. Unlike toka_atomic_write_file, this keeps the file open for
// the caller and leaves removal under the resource owner's control.
typedef struct {
    FILE *stream;
    char *path;
} toka_temp_file;

void *toka_temp_file_create(const char *directory) {
#if defined(_WIN32) || defined(__wasi__)
    (void)directory;
    return NULL;
#else
    if (!directory || directory[0] == '\0') {
        errno = EINVAL;
        return NULL;
    }

    static const char suffix[] = ".toka-tmp-XXXXXX";
    size_t directory_len = strlen(directory);
    int needs_separator = directory[directory_len - 1] != '/';
    size_t path_len = directory_len + (size_t)needs_separator + sizeof(suffix);
    char *path = malloc(path_len);
    if (!path) return NULL;

    memcpy(path, directory, directory_len);
    size_t offset = directory_len;
    if (needs_separator) path[offset++] = '/';
    memcpy(path + offset, suffix, sizeof(suffix));

    int fd = mkstemp(path);
    if (fd < 0) {
        free(path);
        return NULL;
    }
    if (fchmod(fd, S_IRUSR | S_IWUSR) != 0) {
        close(fd);
        unlink(path);
        free(path);
        return NULL;
    }

    FILE *stream = fdopen(fd, "wb");
    if (!stream) {
        close(fd);
        unlink(path);
        free(path);
        return NULL;
    }

    toka_temp_file *file = malloc(sizeof(*file));
    if (!file) {
        fclose(stream);
        unlink(path);
        free(path);
        return NULL;
    }
    file->stream = stream;
    file->path = path;
    return file;
#endif
}

const char *toka_temp_file_path(void *handle) {
#if defined(_WIN32) || defined(__wasi__)
    (void)handle;
    return NULL;
#else
    toka_temp_file *file = handle;
    return file ? file->path : NULL;
#endif
}

long long toka_temp_file_write(void *handle, const unsigned char *data, size_t len) {
#if defined(_WIN32) || defined(__wasi__)
    (void)handle;
    (void)data;
    (void)len;
    return -1;
#else
    toka_temp_file *file = handle;
    if (!file || !file->stream || (!data && len != 0)) {
        errno = EINVAL;
        return -1;
    }
    size_t written = 0;
    while (written < len) {
        size_t count = fwrite(data + written, 1, len - written, file->stream);
        if (count > 0) {
            written += count;
            continue;
        }
        if (ferror(file->stream)) return -1;
        errno = EIO;
        return -1;
    }
    return (long long)written;
#endif
}

int toka_temp_file_close(void *handle) {
#if defined(_WIN32) || defined(__wasi__)
    (void)handle;
    return -1;
#else
    toka_temp_file *file = handle;
    if (!file || !file->stream) {
        errno = EINVAL;
        return -1;
    }
    FILE *stream = file->stream;
    file->stream = NULL;
    return fclose(stream);
#endif
}

int toka_temp_file_remove(void *handle) {
#if defined(_WIN32) || defined(__wasi__)
    (void)handle;
    return -1;
#else
    toka_temp_file *file = handle;
    if (!file || !file->path || file->stream) {
        errno = EINVAL;
        return -1;
    }
    return unlink(file->path);
#endif
}

void toka_temp_file_destroy(void *handle) {
#if !defined(_WIN32) && !defined(__wasi__)
    toka_temp_file *file = handle;
    if (!file) return;
    if (file->stream) fclose(file->stream);
    if (file->path) {
        unlink(file->path);
        free(file->path);
    }
    free(file);
#else
    (void)handle;
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
#include <netinet/in.h>
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
    fflush(stdout);
}

void toka_print_i32(int val) {
    printf("%d", val);
    fflush(stdout);
}

void toka_print_f64(double val) {
    printf("%g", val);
    fflush(stdout);
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

enum toka_process_stdio {
    TOKA_PROCESS_STDIO_INHERIT = 0,
    TOKA_PROCESS_STDIO_NULL = 1,
};

struct toka_process_config {
    const char *cwd;
    const char *env_blob;
    size_t env_blob_len;
    size_t env_count;
    int stdin_mode;
    int stdout_mode;
    int stderr_mode;
};

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

static int toka_process_apply_environment(const struct toka_process_config *config) {
    if (!config->env_blob && config->env_count != 0) return EINVAL;
    size_t offset = 0;
    for (size_t i = 0; i < config->env_count; ++i) {
        if (offset >= config->env_blob_len) return EINVAL;
        const char *key = config->env_blob + offset;
        const char *key_end = memchr(key, '\0', config->env_blob_len - offset);
        if (!key_end || key == key_end || memchr(key, '=', (size_t)(key_end - key)))
            return EINVAL;
        offset = (size_t)(key_end - config->env_blob) + 1;
        if (offset >= config->env_blob_len) return EINVAL;
        const char *value = config->env_blob + offset;
        const char *value_end = memchr(value, '\0', config->env_blob_len - offset);
        if (!value_end) return EINVAL;
        offset = (size_t)(value_end - config->env_blob) + 1;
        if (setenv(key, value, 1) != 0) return errno;
    }
    return offset == config->env_blob_len ? 0 : EINVAL;
}

static int toka_process_redirect_null(int fd, int flags) {
    int null_fd = open("/dev/null", flags);
    if (null_fd < 0) return errno;
    if (dup2(null_fd, fd) < 0) {
        int error = errno;
        close(null_fd);
        return error;
    }
    close(null_fd);
    return 0;
}

static int toka_process_apply_context(const struct toka_process_config *config) {
    if (config->cwd && config->cwd[0] != '\0' && chdir(config->cwd) != 0)
        return errno;
    return toka_process_apply_environment(config);
}

static int toka_process_apply_stdio(const struct toka_process_config *config) {
    const int modes[3] = {
        config->stdin_mode, config->stdout_mode, config->stderr_mode,
    };
    for (int fd = 0; fd < 3; ++fd) {
        if (modes[fd] == TOKA_PROCESS_STDIO_INHERIT) continue;
        if (modes[fd] != TOKA_PROCESS_STDIO_NULL) return EINVAL;
        int flags = fd == STDIN_FILENO ? O_RDONLY : O_WRONLY;
        int error = toka_process_redirect_null(fd, flags);
        if (error != 0) return error;
    }
    return 0;
}

static void toka_process_exec(char **argv, int exec_error_fd) {
    execvp(argv[0], argv);
    int error = errno;
    while (write(exec_error_fd, &error, sizeof(error)) < 0 && errno == EINTR) {}
    _exit(127);
}

static void toka_process_child_exec(char **argv, int exec_error_fd,
                                    const struct toka_process_config *config) {
    int error = toka_process_apply_context(config);
    if (error == 0) error = toka_process_apply_stdio(config);
    if (error != 0) {
        while (write(exec_error_fd, &error, sizeof(error)) < 0 && errno == EINTR) {}
        _exit(127);
    }
    toka_process_exec(argv, exec_error_fd);
}

int toka_process_spawn_config_packed(const char *packed, size_t packed_len,
                                     size_t argc, const char *cwd,
                                     const char *env_blob, size_t env_blob_len,
                                     size_t env_count, int stdin_mode,
                                     int stdout_mode, int stderr_mode) {
    char **argv = NULL;
    int error = toka_unpack_process_argv(packed, packed_len, argc, &argv);
    if (error != 0) return -error;

    const struct toka_process_config config = {
        cwd, env_blob, env_blob_len, env_count,
        stdin_mode, stdout_mode, stderr_mode,
    };

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
        toka_process_child_exec(argv, exec_pipe[1], &config);
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

int toka_process_spawn_packed(const char *packed, size_t packed_len,
                              size_t argc) {
    return toka_process_spawn_config_packed(
        packed, packed_len, argc, NULL, NULL, 0, 0,
        TOKA_PROCESS_STDIO_INHERIT, TOKA_PROCESS_STDIO_INHERIT,
        TOKA_PROCESS_STDIO_INHERIT);
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

int toka_process_status_config_packed(const char *packed, size_t packed_len,
                                      size_t argc, const char *cwd,
                                      const char *env_blob, size_t env_blob_len,
                                      size_t env_count, int stdin_mode,
                                      int stdout_mode, int stderr_mode,
                                      int *out_exit_code, int *out_signal) {
    int pid = toka_process_spawn_config_packed(
        packed, packed_len, argc, cwd, env_blob, env_blob_len, env_count,
        stdin_mode, stdout_mode, stderr_mode);
    if (pid < 0) return -pid;
    return toka_process_wait(pid, out_exit_code, out_signal);
}

int toka_process_output_config_packed(const char *packed, size_t packed_len,
                                      size_t argc, const char *cwd,
                                      const char *env_blob, size_t env_blob_len,
                                      size_t env_count, int stdin_mode,
                                      int stdout_mode, int stderr_mode,
                                      char **out_stdout, size_t *out_stdout_len,
                                      char **out_stderr, size_t *out_stderr_len,
                                      int *out_exit_code, int *out_signal) {
    if (!out_stdout || !out_stdout_len || !out_stderr || !out_stderr_len ||
        !out_exit_code || !out_signal) return EINVAL;
    *out_stdout = NULL;
    *out_stdout_len = 0;
    *out_stderr = NULL;
    *out_stderr_len = 0;

    char **argv = NULL;
    int error = toka_unpack_process_argv(packed, packed_len, argc, &argv);
    if (error != 0) return error;

    const struct toka_process_config config = {
        cwd, env_blob, env_blob_len, env_count,
        stdin_mode, stdout_mode, stderr_mode,
    };

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
        int child_error = toka_process_apply_context(&config);
        if (child_error == 0 && config.stdin_mode == TOKA_PROCESS_STDIO_NULL)
            child_error = toka_process_redirect_null(STDIN_FILENO, O_RDONLY);
        if (child_error == 0 && config.stdin_mode != TOKA_PROCESS_STDIO_INHERIT &&
            config.stdin_mode != TOKA_PROCESS_STDIO_NULL)
            child_error = EINVAL;
        if (child_error == 0 && config.stdout_mode == TOKA_PROCESS_STDIO_NULL)
            child_error = toka_process_redirect_null(STDOUT_FILENO, O_WRONLY);
        if (child_error == 0 && config.stdout_mode == TOKA_PROCESS_STDIO_INHERIT &&
            dup2(stdout_pipe[1], STDOUT_FILENO) < 0)
            child_error = errno;
        if (child_error == 0 && config.stdout_mode != TOKA_PROCESS_STDIO_INHERIT &&
            config.stdout_mode != TOKA_PROCESS_STDIO_NULL)
            child_error = EINVAL;
        if (child_error == 0 && config.stderr_mode == TOKA_PROCESS_STDIO_NULL)
            child_error = toka_process_redirect_null(STDERR_FILENO, O_WRONLY);
        if (child_error == 0 && config.stderr_mode == TOKA_PROCESS_STDIO_INHERIT &&
            dup2(stderr_pipe[1], STDERR_FILENO) < 0)
            child_error = errno;
        if (child_error == 0 && config.stderr_mode != TOKA_PROCESS_STDIO_INHERIT &&
            config.stderr_mode != TOKA_PROCESS_STDIO_NULL)
            child_error = EINVAL;
        if (child_error != 0) {
            while (write(exec_pipe[1], &child_error, sizeof(child_error)) < 0 &&
                   errno == EINTR) {}
            _exit(127);
        }
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        toka_process_exec(argv, exec_pipe[1]);
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

int toka_process_output_packed(const char *packed, size_t packed_len,
                               size_t argc, char **out_stdout,
                               size_t *out_stdout_len, char **out_stderr,
                               size_t *out_stderr_len, int *out_exit_code,
                               int *out_signal) {
    return toka_process_output_config_packed(
        packed, packed_len, argc, NULL, NULL, 0, 0,
        TOKA_PROCESS_STDIO_INHERIT, TOKA_PROCESS_STDIO_INHERIT,
        TOKA_PROCESS_STDIO_INHERIT, out_stdout, out_stdout_len,
        out_stderr, out_stderr_len, out_exit_code, out_signal);
}

int toka_process_cancel(int pid, int policy) {
    if (pid <= 0) return EINVAL;
    int signal_number = policy == 1 ? SIGTERM : policy == 2 ? SIGKILL : 0;
    if (signal_number == 0) return EINVAL;
    return kill((pid_t)pid, signal_number) == 0 ? 0 : errno;
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

int toka_process_spawn_config_packed(const char *packed, size_t packed_len,
                                     size_t argc, const char *cwd,
                                     const char *env_blob, size_t env_blob_len,
                                     size_t env_count, int stdin_mode,
                                     int stdout_mode, int stderr_mode) {
    if ((!cwd || cwd[0] == '\0') && env_blob_len == 0 && env_count == 0 &&
        stdin_mode == TOKA_PROCESS_STDIO_INHERIT &&
        stdout_mode == TOKA_PROCESS_STDIO_INHERIT &&
        stderr_mode == TOKA_PROCESS_STDIO_INHERIT)
        return toka_process_spawn_packed(packed, packed_len, argc);
    (void)env_blob;
    return -ENOSYS;
}

int toka_process_status_config_packed(const char *packed, size_t packed_len,
                                      size_t argc, const char *cwd,
                                      const char *env_blob, size_t env_blob_len,
                                      size_t env_count, int stdin_mode,
                                      int stdout_mode, int stderr_mode,
                                      int *out_exit_code, int *out_signal) {
    if ((!cwd || cwd[0] == '\0') && env_blob_len == 0 && env_count == 0 &&
        stdin_mode == TOKA_PROCESS_STDIO_INHERIT &&
        stdout_mode == TOKA_PROCESS_STDIO_INHERIT &&
        stderr_mode == TOKA_PROCESS_STDIO_INHERIT)
        return toka_process_status_packed(
            packed, packed_len, argc, out_exit_code, out_signal);
    (void)env_blob;
    return ENOSYS;
}

int toka_process_output_config_packed(const char *packed, size_t packed_len,
                                      size_t argc, const char *cwd,
                                      const char *env_blob, size_t env_blob_len,
                                      size_t env_count, int stdin_mode,
                                      int stdout_mode, int stderr_mode,
                                      char **out_stdout, size_t *out_stdout_len,
                                      char **out_stderr, size_t *out_stderr_len,
                                      int *out_exit_code, int *out_signal) {
    if ((!cwd || cwd[0] == '\0') && env_blob_len == 0 && env_count == 0 &&
        stdin_mode == TOKA_PROCESS_STDIO_INHERIT &&
        stdout_mode == TOKA_PROCESS_STDIO_INHERIT &&
        stderr_mode == TOKA_PROCESS_STDIO_INHERIT)
        return toka_process_output_packed(
            packed, packed_len, argc, out_stdout, out_stdout_len, out_stderr,
            out_stderr_len, out_exit_code, out_signal);
    (void)env_blob;
    return ENOSYS;
}

int toka_process_cancel(int pid, int policy) {
    (void)pid; (void)policy;
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
    TOKA_TCB_COMPLETED_CANCELED = 7,
    TOKA_TCB_COLD_FINALIZING = 8
} TokaTCBState;

static inline int toka_tcb_is_terminal(uint32_t st) {
    return (st == TOKA_TCB_COMPLETED || st == TOKA_TCB_COMPLETED_CANCELED);
}

typedef struct TokaTCB TokaTCB;
typedef struct TokaTaskScopeRegistry TokaTaskScopeRegistry;

typedef struct {
    uint64_t task_id;
    uint64_t task_instance_generation;
} TokaTaskToken;

static int toka_task_token_equals(TokaTaskToken lhs, TokaTaskToken rhs) {
    return lhs.task_id == rhs.task_id &&
           lhs.task_instance_generation == rhs.task_instance_generation;
}

void toka_task_release(void *tcb_ptr);
int toka_task_request_cancel(void *tcb_ptr);
int toka_wait_registry_try_wake(uint32_t wait_id, uint32_t slot_gen);
int toka_wait_registry_release(uint32_t wait_id, uint32_t slot_gen);
static int toka_wait_registry_try_wake_checked(
    TokaTaskToken expected_parent, uint64_t expected_wait_set_id,
    uint64_t expected_wait_set_generation, uint32_t wait_id,
    uint32_t slot_gen
);

typedef enum {
    TOKA_COMPLETION_SUB_ACTIVE = 0,
    TOKA_COMPLETION_SUB_SELECTED_PUBLISHER = 1,
    TOKA_COMPLETION_SUB_SELECTED_UNSUBSCRIBER = 2,
    TOKA_COMPLETION_SUB_COMMIT_CLAIMED = 3,
    TOKA_COMPLETION_SUB_INACTIVE = 4
} TokaCompletionSubscriptionState;

// Completion subscriptions are runtime-owned descriptor records.  The child
// retain is transferred to exactly one terminal publisher or unsubscriber;
// the parent wait token remains an identity, never a raw parent frame pointer.
typedef struct TokaCompletionSubscription {
    TokaTaskToken child;
    TokaTaskToken parent;
    uint32_t wait_id;
    uint32_t slot_gen;
    uint64_t wait_set_id;
    uint64_t wait_set_generation;
    _Atomic uint8_t state;
    TokaTCB *child_tcb;
    // Used only after logical unlink while the runtime arbiter is held.  The
    // selected teardown owner drains this private list outside that arbiter.
    struct TokaCompletionSubscription *teardown_next;
} TokaCompletionSubscription;

typedef enum {
    TOKA_RESULT_OWNER_CONSUMER = 0,
    TOKA_RESULT_OWNER_SCOPE = 1,
    TOKA_RESULT_OWNER_DETACHED = 2
} TokaResultOwner;

typedef enum {
    TOKA_RESULT_DISPOSITION_UNCLAIMED = 0,
    TOKA_RESULT_DISPOSITION_DROPPING = 1,
    TOKA_RESULT_DISPOSITION_DROPPED = 2,
    // A consumer has transferred the typed payload. This is the private
    // counterpart of the public ReadyLive -> Taken transition, so a later
    // detached/scope drain cannot mistake the consumed result for unclaimed.
    TOKA_RESULT_DISPOSITION_CLAIMED_BY_CONSUMER = 3
} TokaResultDisposition;

// Private direct-await arbitration. This is deliberately per-parent and
// single-child: generalized source/loser/scope cleanup remains outside this
// bounded 0.x substrate.
typedef enum {
    TOKA_AWAIT_RESOLUTION_IDLE = 0,
    TOKA_AWAIT_RESOLUTION_ARMED = 1,
    TOKA_AWAIT_RESOLUTION_CHILD_NORMAL = 2,
    TOKA_AWAIT_RESOLUTION_CHILD_CANCELED = 3,
    TOKA_AWAIT_RESOLUTION_CANCEL_CLAIMED = 4,
    TOKA_AWAIT_RESOLUTION_NORMAL_CLAIMED = 5
} TokaAwaitResolution;

typedef struct TokaTCB {
    TokaTaskToken token;
    void *coro_frame;
    void *promise;
    _Atomic uint64_t task_schedule_generation;
    _Atomic uint64_t queue_ticket_generation;
    _Atomic uint8_t queue_ticket_published;
    _Atomic uint32_t state;
    _Atomic uint8_t cancel_requested;
    _Atomic uint8_t cancel_handled;
    _Atomic uint8_t result_owner;
    _Atomic uint8_t result_disposition;
    _Atomic uint8_t cold_cleanup_supported;
    _Atomic uint8_t cold_cleanup_finished;
    void (*result_drop_fn)(void *);
    _Atomic uint32_t ref_count;
    _Atomic uint32_t frame_access_state;
    // The default executor may re-enter while a task is running (for example
    // through block_on). This is only the thread-local worker-context link;
    // the enclosing worker's queue reference keeps it alive until restored.
    TokaTCB *previous_current_tcb;
    _Atomic uint8_t detached;
    _Atomic uint8_t detached_counted;
    _Atomic uint8_t owner_released;
    _Atomic uint32_t active_wait_id;
    _Atomic uint32_t active_slot_gen;
    _Atomic uint64_t active_wait_set_id;
    _Atomic uint64_t active_wait_set_generation;
    _Atomic uintptr_t active_child_tcb;
    _Atomic uintptr_t parent_tcb;
    _Atomic uint8_t await_resolution;
    TokaCompletionSubscription **subscribers;
    uint32_t subscriber_count;
    uint32_t subscriber_capacity;
    TokaTCB **cancel_children;
    uint32_t cancel_child_count;
    uint32_t cancel_child_capacity;
    TokaTaskScopeRegistry **cancel_scopes;
    uint32_t cancel_scope_count;
    uint32_t cancel_scope_capacity;
} TokaTCB;

static void toka_wait_registry_cancel_active(TokaTCB *tcb);
static void toka_wait_registry_reap_terminal_outcomes(TokaTCB *tcb);
static int toka_wait_registry_help_pending_for_tcb(TokaTCB *tcb);
static int toka_task_try_drain_detached_result(TokaTCB *tcb);
static int toka_task_dispose_scope_result(TokaTCB *tcb);

static void toka_completion_subscription_release(
    TokaCompletionSubscription *subscription
) {
    if (!subscription) return;
    TokaTCB *child = subscription->child_tcb;
    subscription->child_tcb = NULL;
    free(subscription);
    if (child) toka_task_release(child);
}

static void toka_completion_subscription_publish(
    TokaCompletionSubscription *subscription
) {
    if (!subscription) return;
    uint8_t expected = TOKA_COMPLETION_SUB_SELECTED_PUBLISHER;
    if (!atomic_compare_exchange_strong_explicit(
            &subscription->state, &expected,
            TOKA_COMPLETION_SUB_COMMIT_CLAIMED,
            memory_order_acq_rel, memory_order_acquire)) {
        return;
    }
    // The registration lookup validates the complete parent wait identity
    // before it can schedule. It is deliberately outside the child terminal
    // arbiter, so no publisher waits on its own parent wake path.
    toka_wait_registry_try_wake_checked(
        subscription->parent, subscription->wait_set_id,
        subscription->wait_set_generation, subscription->wait_id,
        subscription->slot_gen
    );
    atomic_store_explicit(&subscription->state,
                          TOKA_COMPLETION_SUB_INACTIVE,
                          memory_order_release);
    toka_completion_subscription_release(subscription);
}

static void toka_completion_subscription_unsubscribe(
    TokaCompletionSubscription *subscription
) {
    if (!subscription) return;
    uint8_t expected = TOKA_COMPLETION_SUB_SELECTED_UNSUBSCRIBER;
    if (!atomic_compare_exchange_strong_explicit(
            &subscription->state, &expected,
            TOKA_COMPLETION_SUB_COMMIT_CLAIMED,
            memory_order_acq_rel, memory_order_acquire)) {
        return;
    }
    atomic_store_explicit(&subscription->state,
                          TOKA_COMPLETION_SUB_INACTIVE,
                          memory_order_release);
    toka_completion_subscription_release(subscription);
}

static void toka_task_try_release_owner(TokaTCB *tcb) {
    if (!tcb) return;
    uint32_t st = atomic_load(&tcb->state);
    if (atomic_load(&tcb->detached) && toka_tcb_is_terminal(st)) {
        uint8_t expected = 0;
        if (atomic_compare_exchange_strong(&tcb->owner_released, &expected, 1)) {
            toka_task_release(tcb);
        }
    }
}

typedef struct {
    TokaTaskToken task;
    uint64_t task_schedule_generation;
    TokaTCB *tcb;
} TokaScheduledItem;

void toka_task_release(void *tcb_ptr);
int toka_task_try_retain(void *tcb_ptr);

static toka_mutex_t g_rt_mutex = TOKA_MUTEX_INIT;
static _Atomic uint64_t g_next_task_id = 1;

typedef struct {
    TokaTCB *tcb;
    TokaTaskToken token;
} TokaTaskRegistryEntry;

typedef struct {
    uint64_t task_id;
    uint64_t next_instance_generation;
} TokaTaskSlotHistory;

static TokaTaskRegistryEntry *g_task_registry = NULL;
static size_t g_task_registry_capacity = 0;
static size_t g_task_registry_count = 0;
static TokaTaskSlotHistory *g_task_slot_history = NULL;
static size_t g_task_slot_history_capacity = 0;
static size_t g_task_slot_history_count = 0;

#define TOKA_FRAME_ACCESS_RETIRED UINT32_MAX

static int toka_allocate_nonzero_u64(_Atomic uint64_t *counter,
                                     uint64_t *out_value);
static int toka_reject_legacy_bare_task_id_api(const char *operation);

// A holder that already owns a TCB reference may acquire another one without
// the registry lock, but it may neither revive zero nor wrap the counter.
// Registry-originated acquisition additionally validates membership under the
// runtime arbiter before this CAS.
static int toka_tcb_try_retain_held(TokaTCB *tcb, uint32_t count) {
    if (!tcb || count == 0) return tcb != NULL;
    uint32_t refs = atomic_load_explicit(&tcb->ref_count, memory_order_acquire);
    while (refs != 0 && refs <= UINT32_MAX - count) {
        if (atomic_compare_exchange_weak_explicit(
                &tcb->ref_count, &refs, refs + count, memory_order_acq_rel,
                memory_order_acquire)) {
            return 1;
        }
    }
    return 0;
}

static void toka_tcb_require_retain_held(TokaTCB *tcb, uint32_t count,
                                         const char *operation) {
    if (toka_tcb_try_retain_held(tcb, count)) return;
    fprintf(stderr, "Fatal error: TCB reference retain failed during %s.\n",
            operation);
    abort();
}

// Callers use this only while g_rt_mutex is held and while dropping a retain
// that was acquired in the same transaction. The pre-existing owner prevents
// the temporary drop from becoming the final release.
static void toka_tcb_drop_temporary_retain_locked(TokaTCB *tcb,
                                                   uint32_t count) {
    uint32_t refs = atomic_load_explicit(&tcb->ref_count, memory_order_acquire);
    while (refs > count) {
        if (atomic_compare_exchange_weak_explicit(
                &tcb->ref_count, &refs, refs - count, memory_order_acq_rel,
                memory_order_acquire)) {
            return;
        }
    }
    fprintf(stderr, "Fatal error: invalid temporary TCB reference release.\n");
    abort();
}

// A frame pin is distinct from a TCB reference: it grants raw coroutine-frame
// access only while the frame remains Open. Every pin holder also has an
// independent TCB reference, so final release cannot race a live pin.
static int toka_tcb_try_acquire_frame_pin(TokaTCB *tcb) {
    if (!tcb || !tcb->coro_frame) return 0;
    uint32_t pins = atomic_load_explicit(&tcb->frame_access_state,
                                         memory_order_acquire);
    while (pins != TOKA_FRAME_ACCESS_RETIRED &&
           pins < TOKA_FRAME_ACCESS_RETIRED - 1) {
        if (atomic_compare_exchange_weak_explicit(
                &tcb->frame_access_state, &pins, pins + 1,
                memory_order_acq_rel, memory_order_acquire)) {
            return 1;
        }
    }
    return 0;
}

static void toka_tcb_release_frame_pin(TokaTCB *tcb) {
    if (!tcb || !tcb->coro_frame) return;
    uint32_t pins = atomic_load_explicit(&tcb->frame_access_state,
                                         memory_order_acquire);
    while (pins != TOKA_FRAME_ACCESS_RETIRED && pins != 0) {
        if (atomic_compare_exchange_weak_explicit(
                &tcb->frame_access_state, &pins, pins - 1,
                memory_order_acq_rel, memory_order_acquire)) {
            return;
        }
    }
    fprintf(stderr, "Fatal error: invalid TCB frame-pin release.\n");
    abort();
}

static void toka_tcb_retire_frame_locked(TokaTCB *tcb) {
    uint32_t expected = 0;
    if (!atomic_compare_exchange_strong_explicit(
            &tcb->frame_access_state, &expected, TOKA_FRAME_ACCESS_RETIRED,
            memory_order_acq_rel, memory_order_acquire)) {
        fprintf(stderr, "Fatal error: attempted to retire a pinned TCB frame.\n");
        abort();
    }
}

static void toka_task_registry_ensure_capacity_locked(void) {
    if (g_task_registry_count < g_task_registry_capacity) return;
    size_t new_capacity = g_task_registry_capacity == 0
                              ? 256
                              : g_task_registry_capacity * 2;
    if (new_capacity < g_task_registry_capacity ||
        new_capacity > SIZE_MAX / sizeof(TokaTaskRegistryEntry)) {
        fprintf(stderr, "Fatal error: TaskRegistry capacity overflow.\n");
        abort();
    }
    TokaTaskRegistryEntry *entries = (TokaTaskRegistryEntry*)realloc(
        g_task_registry, new_capacity * sizeof(TokaTaskRegistryEntry)
    );
    if (!entries) {
        fprintf(stderr, "Fatal error: Out of memory during TaskRegistry expansion.\n");
        abort();
    }
    g_task_registry = entries;
    g_task_registry_capacity = new_capacity;
}

static void toka_task_slot_history_ensure_capacity_locked(void) {
    if (g_task_slot_history_count < g_task_slot_history_capacity) return;
    size_t new_capacity = g_task_slot_history_capacity == 0
                              ? 256
                              : g_task_slot_history_capacity * 2;
    if (new_capacity < g_task_slot_history_capacity ||
        new_capacity > SIZE_MAX / sizeof(TokaTaskSlotHistory)) {
        fprintf(stderr, "Fatal error: TaskRegistry history capacity overflow.\n");
        abort();
    }
    TokaTaskSlotHistory *entries = (TokaTaskSlotHistory*)realloc(
        g_task_slot_history, new_capacity * sizeof(TokaTaskSlotHistory)
    );
    if (!entries) {
        fprintf(stderr, "Fatal error: Out of memory during TaskRegistry history expansion.\n");
        abort();
    }
    g_task_slot_history = entries;
    g_task_slot_history_capacity = new_capacity;
}

static int toka_task_registry_allocate_token_locked(TokaTaskToken *out_token) {
    if (!out_token) return 0;
    while (1) {
        uint64_t task_id = 0;
        if (!toka_allocate_nonzero_u64(&g_next_task_id, &task_id)) return 0;

        TokaTaskSlotHistory *history = NULL;
        for (size_t i = 0; i < g_task_slot_history_count; ++i) {
            if (g_task_slot_history[i].task_id == task_id) {
                history = &g_task_slot_history[i];
                break;
            }
        }
        if (!history) {
            toka_task_slot_history_ensure_capacity_locked();
            history = &g_task_slot_history[g_task_slot_history_count++];
            history->task_id = task_id;
            history->next_instance_generation = 1;
        }
        if (history->next_instance_generation == UINT64_MAX) {
            // This numeric task slot is permanently retired. The next loop
            // consumes a fresh numeric ID instead of wrapping an instance.
            continue;
        }
        out_token->task_id = task_id;
        out_token->task_instance_generation =
            history->next_instance_generation++;
        return 1;
    }
}

static int toka_task_registry_register(TokaTCB *tcb) {
    if (!tcb) return 0;
    toka_mutex_lock(&g_rt_mutex);
    if (!toka_task_registry_allocate_token_locked(&tcb->token)) {
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }
    toka_task_registry_ensure_capacity_locked();
    g_task_registry[g_task_registry_count++] = (TokaTaskRegistryEntry){
        .tcb = tcb,
        .token = tcb->token,
    };
    toka_mutex_unlock(&g_rt_mutex);
    return 1;
}

static void toka_task_registry_unregister_locked(TokaTCB *tcb) {
    for (size_t i = 0; i < g_task_registry_count; ++i) {
        if (g_task_registry[i].tcb == tcb) {
            g_task_registry[i] = g_task_registry[g_task_registry_count - 1];
            g_task_registry_count--;
            return;
        }
    }
    fprintf(stderr, "Fatal error: final TCB release missing TaskRegistry entry.\n");
    abort();
}

static TokaTCB *toka_task_registry_retain_by_pointer_locked(void *ptr,
                                                              uint32_t count) {
    if (!ptr || count == 0) return NULL;
    for (size_t i = 0; i < g_task_registry_count; ++i) {
        TokaTCB *tcb = g_task_registry[i].tcb;
        if (tcb == (TokaTCB*)ptr && toka_tcb_try_retain_held(tcb, count)) {
            return tcb;
        }
    }
    return NULL;
}

static TokaTCB *toka_task_registry_retain_by_token_locked(
    TokaTaskToken token, uint32_t count
) {
    if (token.task_id == 0 || token.task_instance_generation == 0 ||
        count == 0) {
        return NULL;
    }
    for (size_t i = 0; i < g_task_registry_count; ++i) {
        TokaTaskRegistryEntry *entry = &g_task_registry[i];
        if (toka_task_token_equals(entry->token, token) &&
            toka_tcb_try_retain_held(entry->tcb, count)) {
            return entry->tcb;
        }
    }
    return NULL;
}

// A completion descriptor belongs to the exact parent wait it was armed
// against. Once that wait has selected another source or becomes inactive,
// the descriptor cannot remain a child-owned orphan that merely retains the
// child until it happens to finish. Logical unlink is serialized with terminal
// publication under g_rt_mutex; the selected child's retain is released only
// after dropping the arbiter.
static void toka_completion_subscription_collect_parent_wait_teardown_locked(
    TokaTaskToken parent, uint64_t wait_set_id,
    uint64_t wait_set_generation, uint32_t wait_id, uint32_t slot_gen,
    TokaCompletionSubscription **out_head
) {
    if (!out_head || parent.task_id == 0 ||
        parent.task_instance_generation == 0) {
        return;
    }
    for (size_t task_index = 0; task_index < g_task_registry_count;
         ++task_index) {
        TokaTCB *child = g_task_registry[task_index].tcb;
        for (uint32_t sub_index = 0; sub_index < child->subscriber_count;) {
            TokaCompletionSubscription *subscription =
                child->subscribers[sub_index];
            const int matches_parent = subscription &&
                toka_task_token_equals(subscription->parent, parent);
            const int matches_wait = subscription && (wait_set_id != 0
                ? subscription->wait_set_id == wait_set_id &&
                  subscription->wait_set_generation == wait_set_generation
                : subscription->wait_set_id == 0 &&
                  subscription->wait_id == wait_id &&
                  subscription->slot_gen == slot_gen);
            if (!matches_parent || !matches_wait) {
                ++sub_index;
                continue;
            }

            uint8_t expected = TOKA_COMPLETION_SUB_ACTIVE;
            if (!atomic_compare_exchange_strong_explicit(
                    &subscription->state, &expected,
                    TOKA_COMPLETION_SUB_SELECTED_UNSUBSCRIBER,
                    memory_order_acq_rel, memory_order_acquire)) {
                ++sub_index;
                continue;
            }
            child->subscribers[sub_index] =
                child->subscribers[child->subscriber_count - 1];
            child->subscriber_count--;
            subscription->teardown_next = *out_head;
            *out_head = subscription;
        }
        if (child->subscriber_count == 0 && child->subscribers) {
            free(child->subscribers);
            child->subscribers = NULL;
            child->subscriber_capacity = 0;
        }
    }
}

static void toka_completion_subscription_finish_parent_wait_teardown(
    TokaCompletionSubscription *head
) {
    while (head) {
        TokaCompletionSubscription *next = head->teardown_next;
        head->teardown_next = NULL;
        toka_completion_subscription_unsubscribe(head);
        head = next;
    }
}

// Task and suspension identities are authority carriers, not counters whose
// wraparound may make an old token name a new task.  The caller may publish a
// new value only after this helper succeeds; exhaustion is therefore a
// fail-closed allocation/suspension failure rather than a partial transition.
static int toka_allocate_nonzero_u64(_Atomic uint64_t *counter,
                                     uint64_t *out_value) {
    uint64_t current = atomic_load_explicit(counter, memory_order_acquire);
    while (current != UINT64_MAX) {
        const uint64_t next = current + 1;
        if (atomic_compare_exchange_weak_explicit(
                counter, &current, next, memory_order_acq_rel,
                memory_order_acquire)) {
            if (out_value) *out_value = current;
            return 1;
        }
    }
    return 0;
}

static int toka_advance_schedule_generation(_Atomic uint64_t *generation,
                                            uint64_t *out_generation) {
    uint64_t current = atomic_load_explicit(generation, memory_order_acquire);
    while (current != UINT64_MAX) {
        const uint64_t next = current + 1;
        if (atomic_compare_exchange_weak_explicit(
                generation, &current, next, memory_order_acq_rel,
                memory_order_acquire)) {
            if (out_generation) *out_generation = next;
            return 1;
        }
    }
    return 0;
}

// This is deliberately a runtime-owned registry rather than a library Vec:
// enrollment and the Open -> Closing transition must share one arbiter.  It is
// only the first structured-scope substrate; typed result disposition and
// parent cancellation aggregation are added by later AS slices.
typedef enum {
    TOKA_TASK_SCOPE_OPEN = 0,
    TOKA_TASK_SCOPE_CLOSING = 1,
    TOKA_TASK_SCOPE_CLOSED = 2
} TokaTaskScopeState;

struct TokaTaskScopeRegistry {
    _Atomic uint32_t ref_count;
    uint32_t state;
    TokaTCB *parent;
    TokaTCB **children;
    uint32_t child_count;
    uint32_t child_capacity;
};

static int toka_task_scope_ensure_capacity_locked(TokaTaskScopeRegistry *scope) {
    if (scope->child_count < scope->child_capacity) return 1;
    if (scope->child_capacity > UINT32_MAX / 2) return 0;
    uint32_t new_capacity = scope->child_capacity == 0 ? 4 : scope->child_capacity * 2;
#if SIZE_MAX < UINT32_MAX
    if (new_capacity > SIZE_MAX / sizeof(TokaTCB*)) return 0;
#endif
    TokaTCB **children = (TokaTCB**)realloc(
        scope->children, new_capacity * sizeof(TokaTCB*)
    );
    if (!children) return 0;
    scope->children = children;
    scope->child_capacity = new_capacity;
    return 1;
}

static int toka_task_scope_parent_ensure_capacity_locked(TokaTCB *parent) {
    if (parent->cancel_scope_count < parent->cancel_scope_capacity) return 1;
    if (parent->cancel_scope_capacity > UINT32_MAX / 2) return 0;
    uint32_t new_capacity =
        parent->cancel_scope_capacity == 0 ? 2 : parent->cancel_scope_capacity * 2;
#if SIZE_MAX < UINT32_MAX
    if (new_capacity > SIZE_MAX / sizeof(TokaTaskScopeRegistry*)) return 0;
#endif
    TokaTaskScopeRegistry **scopes = (TokaTaskScopeRegistry**)realloc(
        parent->cancel_scopes, new_capacity * sizeof(TokaTaskScopeRegistry*)
    );
    if (!scopes) return 0;
    parent->cancel_scopes = scopes;
    parent->cancel_scope_capacity = new_capacity;
    return 1;
}

static int toka_task_scope_retain_locked(TokaTaskScopeRegistry *scope) {
    uint32_t refs = atomic_load(&scope->ref_count);
    if (refs == 0 || refs == UINT32_MAX) return 0;
    atomic_store(&scope->ref_count, refs + 1);
    return 1;
}

static TokaScheduledItem *g_ready_queue = NULL;
static size_t g_ready_capacity = 0;
static size_t g_ready_head = 0;
static size_t g_ready_tail = 0;
static size_t g_ready_count = 0;

static void ensure_ready_queue_capacity_locked(void) {
#ifndef _WIN32
    static int sigpipe_ignored = 0;
    if (!sigpipe_ignored) {
        signal(SIGPIPE, SIG_IGN);
        sigpipe_ignored = 1;
    }
#endif
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

static void push_ready_queue_locked(TokaTCB *tcb, uint64_t gen) {
    ensure_ready_queue_capacity_locked();
    toka_tcb_require_retain_held(tcb, 1, "ready queue publication");
    g_ready_queue[g_ready_tail].task = tcb->token;
    g_ready_queue[g_ready_tail].task_schedule_generation = gen;
    g_ready_queue[g_ready_tail].tcb = tcb;
    g_ready_tail = (g_ready_tail + 1) % g_ready_capacity;
    g_ready_count++;
}

// A task enters Queued only after its generation has an unpublished ticket.
// The runtime arbiter makes the ticket's first physical queue insertion the
// single publication point, so a later scheduler can finish a preempted
// publisher without adding a second entry.
static void toka_task_prepare_queue_ticket(TokaTCB *tcb, uint64_t gen) {
    atomic_store(&tcb->queue_ticket_generation, gen);
    atomic_store(&tcb->queue_ticket_published, 0);
}

static int toka_task_publish_queue_ticket_locked(TokaTCB *tcb, uint64_t gen) {
    if (atomic_load(&tcb->state) != TOKA_TCB_QUEUED ||
        atomic_load(&tcb->task_schedule_generation) != gen ||
        atomic_load(&tcb->queue_ticket_generation) != gen) {
        return 0;
    }
    if (atomic_load(&tcb->queue_ticket_published)) {
        return 1;
    }

    push_ready_queue_locked(tcb, gen);
    atomic_store(&tcb->queue_ticket_published, 1);
    return 1;
}

#ifdef TOKA_RUNTIME_TESTING
static _Atomic uint8_t g_test_pause_next_queue_publication = 0;
static _Atomic uint8_t g_test_pause_next_wait_set_commit = 0;
static _Atomic uint8_t g_test_fail_next_wait_set_create = 0;

// The CTest uses this to model an original publisher that is preempted after
// claiming Queued but before its physical insertion. It is absent from normal
// runtime builds and the language ABI.
void toka_task_pause_next_queue_publication_for_test(void) {
    atomic_store(&g_test_pause_next_queue_publication, 1);
}

// Model a publisher that has made the WaitSet logically inactive but is
// preempted before it can commit the selected descriptor. A later observer
// must complete the same descriptor rather than leave the parent suspended.
void toka_wait_set_pause_next_commit_for_test(void) {
    atomic_store(&g_test_pause_next_wait_set_commit, 1);
}

// Exercise the allocation-failure exit before a descriptor or any member slot
// becomes externally visible. This hook is test-only and never part of the
// runtime ABI.
void toka_wait_set_fail_next_create_for_test(void) {
    atomic_store(&g_test_fail_next_wait_set_create, 1);
}
#endif

static int toka_task_publish_queue_ticket(TokaTCB *tcb, uint64_t gen) {
#ifdef TOKA_RUNTIME_TESTING
    if (atomic_exchange(&g_test_pause_next_queue_publication, 0)) {
        return 1;
    }
#endif
    toka_mutex_lock(&g_rt_mutex);
    int published = toka_task_publish_queue_ticket_locked(tcb, gen);
    toka_mutex_unlock(&g_rt_mutex);
    return published;
}

static int toka_task_claim_queue_ticket(TokaTCB *tcb, uint32_t source_state,
                                        uint64_t gen) {
    toka_mutex_lock(&g_rt_mutex);
    int claimed = 0;
    if (atomic_load(&tcb->task_schedule_generation) == gen &&
        atomic_load(&tcb->state) == source_state) {
        toka_task_prepare_queue_ticket(tcb, gen);
        uint32_t expected = source_state;
        claimed = atomic_compare_exchange_strong(
            &tcb->state, &expected, TOKA_TCB_QUEUED
        );
    }
    toka_mutex_unlock(&g_rt_mutex);
    return claimed;
}

static int toka_task_claim_start_queue_ticket(TokaTCB *tcb) {
    toka_mutex_lock(&g_rt_mutex);
    int claimed = 0;
    if (atomic_load(&tcb->state) == TOKA_TCB_CREATED) {
        atomic_store(&tcb->task_schedule_generation, 1);
        toka_task_prepare_queue_ticket(tcb, 1);
        uint32_t expected = TOKA_TCB_CREATED;
        claimed = atomic_compare_exchange_strong(
            &tcb->state, &expected, TOKA_TCB_QUEUED
        );
    }
    toka_mutex_unlock(&g_rt_mutex);
    return claimed;
}

static void toka_task_clear_queue_ticket(TokaTCB *tcb) {
    atomic_store(&tcb->queue_ticket_published, 0);
    atomic_store(&tcb->queue_ticket_generation, 0);
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
                TokaTCB *tcb = toka_task_registry_retain_by_pointer_locked(
                    g_frame_map[i].tcb, 1
                );
                toka_mutex_unlock(&g_rt_mutex);
                return tcb;
            }
        }
    }
    if (g_current_tcb) {
        TokaTCB *tcb = toka_task_registry_retain_by_pointer_locked(
            g_current_tcb, 1
        );
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

static void* toka_task_create_impl(void *coro_frame, void *promise,
                                   void (*result_drop_fn)(void *),
                                   uint8_t cold_cleanup_supported) {
    TokaTCB *tcb = (TokaTCB*)calloc(1, sizeof(TokaTCB));
    if (!tcb) return NULL;

    tcb->coro_frame = coro_frame;
    tcb->promise = promise;
    atomic_store(&tcb->task_schedule_generation, 0);
    atomic_store(&tcb->queue_ticket_generation, 0);
    atomic_store(&tcb->queue_ticket_published, 0);
    atomic_store(&tcb->state, TOKA_TCB_CREATED);
    atomic_store(&tcb->cancel_requested, 0);
    atomic_store(&tcb->cancel_handled, 0);
    atomic_store(&tcb->result_owner, TOKA_RESULT_OWNER_CONSUMER);
    atomic_store(&tcb->result_disposition,
                 TOKA_RESULT_DISPOSITION_UNCLAIMED);
    atomic_store(&tcb->cold_cleanup_supported, cold_cleanup_supported);
    atomic_store(&tcb->cold_cleanup_finished, 0);
    tcb->result_drop_fn = result_drop_fn;
    atomic_store(&tcb->ref_count, 1);
    atomic_store(&tcb->frame_access_state, 0);
    atomic_store(&tcb->detached, 0);
    atomic_store(&tcb->detached_counted, 0);
    atomic_store(&tcb->owner_released, 0);
    atomic_store(&tcb->active_wait_id, TOKA_NO_WAIT_ID);
    atomic_store(&tcb->active_slot_gen, 0);
    atomic_store(&tcb->active_wait_set_id, 0);
    atomic_store(&tcb->active_wait_set_generation, 0);
    atomic_store(&tcb->active_child_tcb, 0);
    atomic_store(&tcb->await_resolution, TOKA_AWAIT_RESOLUTION_IDLE);

    if (!toka_task_registry_register(tcb)) {
        free(tcb);
        return NULL;
    }

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

void* toka_task_create_with_result_drop(void *coro_frame, void *promise,
                                        void (*result_drop_fn)(void *)) {
    return toka_task_create_impl(coro_frame, promise, result_drop_fn, 0);
}

// New compiler output opts into the cold-finalizer handshake. Older objects
// retain the three-argument ABI and its legacy cleanup behavior.
void* toka_task_create_with_result_drop_and_cold_cleanup(
    void *coro_frame, void *promise, void (*result_drop_fn)(void *)) {
    return toka_task_create_impl(coro_frame, promise, result_drop_fn, 1);
}

// Preserve the two-argument runtime ABI for direct C probes and older TKI
// artifacts. Compiler-generated tasks use the typed drop-aware entry point.
void* toka_task_create(void *coro_frame, void *promise) {
    return toka_task_create_with_result_drop(coro_frame, promise, NULL);
}

#ifdef TOKA_RUNTIME_TESTING
int toka_rt_test_set_next_task_id(uint64_t next_id) {
    if (next_id == 0) return 0;
    atomic_store_explicit(&g_next_task_id, next_id, memory_order_release);
    return 1;
}

int toka_rt_test_set_schedule_generation(void *tcb_ptr, uint64_t generation) {
    if (!tcb_ptr || generation == 0) return 0;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;
    toka_mutex_lock(&g_rt_mutex);
    const int running = atomic_load(&tcb->state) == TOKA_TCB_RUNNING;
    if (running) {
        atomic_store_explicit(&tcb->task_schedule_generation, generation,
                              memory_order_release);
    }
    toka_mutex_unlock(&g_rt_mutex);
    return running;
}

int toka_rt_test_set_tcb_ref_count(void *tcb_ptr, uint32_t ref_count) {
    if (!tcb_ptr) return 0;
    toka_mutex_lock(&g_rt_mutex);
    TokaTCB *tcb = NULL;
    for (size_t i = 0; i < g_task_registry_count; ++i) {
        if (g_task_registry[i].tcb == (TokaTCB*)tcb_ptr) {
            tcb = g_task_registry[i].tcb;
            break;
        }
    }
    if (tcb) {
        atomic_store_explicit(&tcb->ref_count, ref_count, memory_order_release);
    }
    toka_mutex_unlock(&g_rt_mutex);
    return tcb != NULL;
}

uint32_t toka_rt_test_get_frame_access_state(void *tcb_ptr) {
    if (!tcb_ptr) return TOKA_FRAME_ACCESS_RETIRED;
    toka_mutex_lock(&g_rt_mutex);
    uint32_t state = TOKA_FRAME_ACCESS_RETIRED;
    for (size_t i = 0; i < g_task_registry_count; ++i) {
        if (g_task_registry[i].tcb == (TokaTCB*)tcb_ptr) {
            state = atomic_load_explicit(&g_task_registry[i].tcb->frame_access_state,
                                         memory_order_acquire);
            break;
        }
    }
    toka_mutex_unlock(&g_rt_mutex);
    return state;
}
#endif

int toka_task_start(void *tcb_ptr) {
    if (!tcb_ptr) return 0;
    // `TaskHandle` ownership keeps ordinary calls alive, but this exported
    // runtime entry must not turn an arbitrary stale C pointer into a TCB
    // dereference. Convert it through the registry first.
    if (!toka_task_try_retain(tcb_ptr)) return 0;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;
    int started = 0;
    if (toka_task_claim_start_queue_ticket(tcb)) {
        started = toka_task_publish_queue_ticket(tcb, 1);
    }
    toka_task_release(tcb);
    return started;
}

int toka_task_suspend_and_register(void *tcb_ptr) {
    if (!tcb_ptr) return 0;
    if (!toka_task_try_retain(tcb_ptr)) return 0;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;

    toka_mutex_lock(&g_rt_mutex);
    uint64_t ignored_generation = 0;
    uint32_t expected = TOKA_TCB_RUNNING;
    const int advanced = toka_advance_schedule_generation(
        &tcb->task_schedule_generation, &ignored_generation);
    const int suspended = advanced && atomic_compare_exchange_strong(
        &tcb->state, &expected, TOKA_TCB_SUSPENDED);
    if (advanced && !suspended) {
        atomic_fetch_sub(&tcb->task_schedule_generation, 1);
    }
    toka_mutex_unlock(&g_rt_mutex);
    toka_task_release(tcb);
    return suspended;
}

int toka_task_prepare_suspend_token(void *coro_frame, uint64_t *out_task_id,
                                    uint64_t *out_instance_generation,
                                    uint64_t *out_gen) {
    TokaTCB *tcb = lookup_tcb_by_frame_retained(coro_frame);
    if (!tcb) return 0;

    toka_mutex_lock(&g_rt_mutex);
    uint64_t new_gen = 0;
    uint32_t expected = TOKA_TCB_RUNNING;
    if (toka_advance_schedule_generation(&tcb->task_schedule_generation,
                                         &new_gen) &&
        atomic_compare_exchange_strong(&tcb->state, &expected,
                                       TOKA_TCB_PREPARING)) {
        if (out_task_id) *out_task_id = tcb->token.task_id;
        if (out_instance_generation) {
            *out_instance_generation = tcb->token.task_instance_generation;
        }
        if (out_gen) *out_gen = new_gen;

        if (atomic_load(&tcb->cancel_requested)) {
            uint32_t prep_st = TOKA_TCB_PREPARING;
            atomic_compare_exchange_strong(&tcb->state, &prep_st, TOKA_TCB_PREPARING_WITH_PENDING_WAKE);
        }

        toka_mutex_unlock(&g_rt_mutex);
        toka_task_release(tcb);
        return 1;
    }
    if (new_gen != 0) {
        atomic_fetch_sub(&tcb->task_schedule_generation, 1);
    }
    toka_mutex_unlock(&g_rt_mutex);
    toka_task_release(tcb);
    return 0;
}

int toka_task_prepare_suspend(void *coro_frame, uint64_t *out_task_id,
                              uint64_t *out_gen) {
    (void)coro_frame;
    if (out_task_id) *out_task_id = 0;
    if (out_gen) *out_gen = 0;
    return toka_reject_legacy_bare_task_id_api("suspension preparation");
}

int toka_task_commit_suspend(void *coro_frame) {
    TokaTCB *tcb = lookup_tcb_by_frame_retained(coro_frame);
    if (!tcb) return 0;

    while (1) {
        if (toka_wait_registry_help_pending_for_tcb(tcb)) {
            continue;
        }
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
            uint64_t gen = atomic_load(&tcb->task_schedule_generation);
            if (toka_task_claim_queue_ticket(
                    tcb, TOKA_TCB_PREPARING_WITH_PENDING_WAKE, gen)) {
                toka_wait_registry_cancel_active(tcb);
                toka_task_publish_queue_ticket(tcb, gen);
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
        if (toka_wait_registry_help_pending_for_tcb(tcb)) {
            continue;
        }
        uint32_t st = atomic_load(&tcb->state);
        if (st == TOKA_TCB_PREPARING || st == TOKA_TCB_PREPARING_WITH_PENDING_WAKE) {
            // Roll back the logical wait installation before restoring
            // Running. This invalidates singleton and wait-set registrations,
            // releases their retained TCB references, and prevents a late
            // source from scheduling an attempt that never committed.
            toka_wait_registry_cancel_active(tcb);
            uint32_t expected = st;
            if (atomic_compare_exchange_strong(&tcb->state, &expected, TOKA_TCB_RUNNING)) {
                toka_task_release(tcb);
                return 1;
            }
            continue;
        }
        toka_task_release(tcb);
        return 0;
    }
}

static int toka_task_try_schedule_token_internal(TokaTaskToken token,
                                                 uint64_t gen) {
    toka_mutex_lock(&g_rt_mutex);
    TokaTCB *target_tcb =
        toka_task_registry_retain_by_token_locked(token, 1);
    toka_mutex_unlock(&g_rt_mutex);

    if (!target_tcb) return 0;

    if (atomic_load(&target_tcb->task_schedule_generation) != gen) {
        toka_task_release(target_tcb);
        return 0; // Stale: Generation mismatch
    }

    while (1) {
        if (toka_wait_registry_help_pending_for_tcb(target_tcb)) {
            continue;
        }
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
            if (toka_task_claim_queue_ticket(target_tcb, TOKA_TCB_SUSPENDED,
                                              gen)) {
                int published = toka_task_publish_queue_ticket(target_tcb, gen);
                toka_task_release(target_tcb);
                return published; // Scheduled
            }
            continue;
        }
        if (st == TOKA_TCB_QUEUED) {
            int published = toka_task_publish_queue_ticket(target_tcb, gen);
            toka_task_release(target_tcb);
            return published; // Help a preempted queue publisher
        }
        toka_task_release(target_tcb);
        return 0;
    }
}

static int toka_reject_legacy_bare_task_id_api(const char *operation) {
    fprintf(stderr,
            "Toka async runtime rejected legacy bare TaskId ABI during %s.\n",
            operation);
    return 0;
}

int toka_task_try_schedule_token(uint64_t task_id,
                                  uint64_t task_instance_generation,
                                  uint64_t gen) {
    return toka_task_try_schedule_token_internal(
        (TokaTaskToken){
            .task_id = task_id,
            .task_instance_generation = task_instance_generation,
        },
        gen
    );
}

// A bare numeric task ID cannot prove which reused instance it names. Keep the
// symbol only so an old artifact fails closed instead of scheduling a new TCB.
int toka_task_try_schedule(uint64_t task_id, uint64_t gen) {
    (void)task_id;
    (void)gen;
    return toka_reject_legacy_bare_task_id_api("task scheduling");
}

int toka_task_schedule_frame_compat(void *frame) {
    TokaTCB *tcb = lookup_tcb_by_frame_retained(frame);
    if (!tcb) return 0;
    int res = toka_task_try_schedule_token_internal(
        tcb->token, atomic_load(&tcb->task_schedule_generation)
    );
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
        if (item.tcb && toka_task_token_equals(item.task, item.tcb->token) &&
            atomic_load(&item.tcb->task_schedule_generation) ==
                item.task_schedule_generation &&
            atomic_load(&item.tcb->queue_ticket_generation) ==
                item.task_schedule_generation &&
            atomic_load(&item.tcb->queue_ticket_published)) {
            uint32_t expected = TOKA_TCB_QUEUED;
            if (atomic_compare_exchange_strong(&item.tcb->state, &expected,
                                               TOKA_TCB_RUNNING)) {
                if (item.tcb->coro_frame &&
                    !toka_tcb_try_acquire_frame_pin(item.tcb)) {
                    toka_mutex_unlock(&g_rt_mutex);
                    fprintf(stderr, "Fatal error: queued task frame is not pinnable.\n");
                    abort();
                }
                toka_task_clear_queue_ticket(item.tcb);
                toka_mutex_unlock(&g_rt_mutex);
                if (item.tcb->previous_current_tcb != NULL) {
                    fprintf(stderr, "Fatal error: TCB entered worker context twice.\n");
                    abort();
                }
                item.tcb->previous_current_tcb = g_current_tcb;
                g_current_tcb = item.tcb;
                if (out_task_id) *out_task_id = item.task.task_id;
                if (out_gen) *out_gen = item.task_schedule_generation;
                if (out_tcb_ptr) *out_tcb_ptr = item.tcb;
                return 1;
            }
        }
        toka_mutex_unlock(&g_rt_mutex);

        if (item.tcb) {
            toka_task_release(item.tcb);
        }
    }
}

void toka_task_clear_current(void *tcb_ptr) {
    if (!tcb_ptr) return;
    if (g_current_tcb != (TokaTCB*)tcb_ptr) {
        fprintf(stderr, "Fatal error: worker context exited out of order.\n");
        abort();
    }
    TokaTCB *finished = g_current_tcb;
    TokaTCB *previous = finished->previous_current_tcb;
    finished->previous_current_tcb = NULL;
    toka_tcb_release_frame_pin(finished);
    g_current_tcb = previous;
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

static void toka_task_note_await_child_terminal(TokaTCB *parent_tcb,
                                                 uint8_t result_state) {
    if (!parent_tcb) return;
    const uint8_t terminal_resolution =
        result_state == TOKA_RESULT_STATE_CANCELED
            ? TOKA_AWAIT_RESOLUTION_CHILD_CANCELED
            : TOKA_AWAIT_RESOLUTION_CHILD_NORMAL;
    uint8_t expected = TOKA_AWAIT_RESOLUTION_ARMED;
    // A parent cancellation claim wins over the child terminal observation;
    // a normal claimant can arise only after terminal publication has already
    // made the continuation runnable.
    atomic_compare_exchange_strong_explicit(
        &parent_tcb->await_resolution, &expected, terminal_resolution,
        memory_order_acq_rel, memory_order_acquire
    );
}

static int toka_task_claim_await_cancellation(TokaTCB *tcb) {
    if (!tcb) return 0;
    for (;;) {
        uint8_t resolution = atomic_load_explicit(&tcb->await_resolution,
                                                  memory_order_acquire);
        switch (resolution) {
        case TOKA_AWAIT_RESOLUTION_ARMED:
        case TOKA_AWAIT_RESOLUTION_CHILD_NORMAL:
        case TOKA_AWAIT_RESOLUTION_CHILD_CANCELED: {
            uint8_t expected = resolution;
            if (atomic_compare_exchange_weak_explicit(
                    &tcb->await_resolution, &expected,
                    TOKA_AWAIT_RESOLUTION_CANCEL_CLAIMED,
                    memory_order_acq_rel, memory_order_acquire)) {
                return 1;
            }
            continue;
        }
        case TOKA_AWAIT_RESOLUTION_CANCEL_CLAIMED:
            return 1;
        case TOKA_AWAIT_RESOLUTION_NORMAL_CLAIMED:
        case TOKA_AWAIT_RESOLUTION_IDLE:
        default:
            return 0;
        }
    }
}

#ifdef TOKA_RUNTIME_TESTING
static _Atomic uint8_t g_test_pause_terminal_after_publish = 0;
static _Atomic uint8_t g_test_terminal_publish_paused = 0;
static _Atomic uint8_t g_test_pause_terminal_after_result_commit = 0;
static _Atomic uint8_t g_test_terminal_result_commit_paused = 0;

void toka_rt_test_pause_terminal_after_publish(void) {
    atomic_store_explicit(&g_test_terminal_publish_paused, 0,
                          memory_order_release);
    atomic_store_explicit(&g_test_pause_terminal_after_publish, 1,
                          memory_order_release);
}

int toka_rt_test_terminal_publish_paused(void) {
    return atomic_load_explicit(&g_test_terminal_publish_paused,
                                memory_order_acquire) != 0;
}

void toka_rt_test_resume_terminal_publish(void) {
    atomic_store_explicit(&g_test_pause_terminal_after_publish, 0,
                          memory_order_release);
}

// Forces the externally observable ReadyLive -> Completed handoff window.
// This is test-only: a detach in that window must leave the private result
// claim for the terminal publisher's later detached drain, not strand it.
void toka_rt_test_pause_terminal_after_result_commit(void) {
    atomic_store_explicit(&g_test_terminal_result_commit_paused, 0,
                          memory_order_release);
    atomic_store_explicit(&g_test_pause_terminal_after_result_commit, 1,
                          memory_order_release);
}

int toka_rt_test_terminal_result_commit_paused(void) {
    return atomic_load_explicit(&g_test_terminal_result_commit_paused,
                                memory_order_acquire) != 0;
}

void toka_rt_test_resume_terminal_result_commit(void) {
    atomic_store_explicit(&g_test_pause_terminal_after_result_commit, 0,
                          memory_order_release);
}

static void toka_rt_test_pause_after_terminal_result_commit(void) {
    if (!atomic_load_explicit(&g_test_pause_terminal_after_result_commit,
                              memory_order_acquire)) {
        return;
    }
    atomic_store_explicit(&g_test_terminal_result_commit_paused, 1,
                          memory_order_release);
    while (atomic_load_explicit(&g_test_pause_terminal_after_result_commit,
                                memory_order_acquire)) {
#ifdef _WIN32
        Sleep(0);
#elif !defined(__wasi__)
        sched_yield();
#else
        break;
#endif
    }
}

static void toka_rt_test_pause_after_terminal_publish(void) {
    if (!atomic_load_explicit(&g_test_pause_terminal_after_publish,
                              memory_order_acquire)) {
        return;
    }
    atomic_store_explicit(&g_test_terminal_publish_paused, 1,
                          memory_order_release);
    while (atomic_load_explicit(&g_test_pause_terminal_after_publish,
                                memory_order_acquire)) {
#ifdef _WIN32
        Sleep(0);
#elif !defined(__wasi__)
        sched_yield();
#else
        break;
#endif
    }
}
#endif

static int toka_task_publish_terminal(void *promise_ptr, uint8_t result_state,
                                      uint32_t terminal_state) {
    if (!promise_ptr) return 0;
    struct TokaPromiseHeader *hdr = (struct TokaPromiseHeader*)promise_ptr;
    TokaTCB *tcb = (TokaTCB*)hdr->self_tcb;
    int retained_tcb = 0;
    int pinned_frame = 0;

    // A terminal publisher starts from frame-local promise storage. Convert
    // that association into a checked TCB lifetime reference before it reads
    // TCB state, and pin frame access through publication/subscription
    // selection. The final release cannot retire the frame while this path is
    // paused after terminal publication.
    if (tcb) {
        if (!toka_task_try_retain(tcb)) return 0;
        retained_tcb = 1;
        if (tcb->promise != promise_ptr) {
            toka_task_release(tcb);
            return 0;
        }
        if (tcb->coro_frame) {
            if (!toka_tcb_try_acquire_frame_pin(tcb)) {
                toka_task_release(tcb);
                return 0;
            }
            pinned_frame = 1;
        }
    }

    if (tcb && atomic_load(&tcb->state) == TOKA_TCB_COLD_FINALIZING &&
        (terminal_state != TOKA_TCB_COMPLETED_CANCELED ||
         !atomic_load(&tcb->cold_cleanup_finished))) {
        if (pinned_frame) toka_tcb_release_frame_pin(tcb);
        if (retained_tcb) toka_task_release(tcb);
        return 0;
    }

    // The result-state transition is the terminal-publication linearization
    // point. Only its winner may expose a terminal TCB state, wake observers,
    // or release terminal ownership. In particular, a late cancellation must
    // not relabel an already published normal result (or run cleanup twice).
    uint8_t expected_res = TOKA_RESULT_STATE_PENDING;
    if (!atomic_compare_exchange_strong_explicit(
            &hdr->result_state, &expected_res, result_state,
            memory_order_acq_rel, memory_order_acquire)) {
        if (pinned_frame) toka_tcb_release_frame_pin(tcb);
        if (retained_tcb) toka_task_release(tcb);
        return 0;
    }

#ifdef TOKA_RUNTIME_TESTING
    toka_rt_test_pause_after_terminal_result_commit();
#endif

    if (tcb) {
        atomic_store(&tcb->active_child_tcb, 0);
        // Terminal publication cannot strand a live source registration. The
        // group teardown releases every registration-held TCB reference before
        // a late source can observe this terminal task. Publish terminal state
        // through the same arbiter that gates unsubscription before teardown,
        // so an unsubscriber cannot win after it observes a terminal task.
        toka_mutex_lock(&g_rt_mutex);
        atomic_store_explicit(&tcb->state, terminal_state,
                              memory_order_release);
        toka_mutex_unlock(&g_rt_mutex);
        toka_wait_registry_cancel_active(tcb);
        toka_wait_registry_reap_terminal_outcomes(tcb);

        toka_mutex_lock(&g_rt_mutex);
        uint8_t expected_counted = 1;
        if (atomic_compare_exchange_strong(&tcb->detached_counted, &expected_counted, 0)) {
            if (g_active_detached_task_count > 0) {
                g_active_detached_task_count--;
            }
        }
        TokaCompletionSubscription **subs = NULL;
        uint32_t sub_count = tcb->subscriber_count;
        if (sub_count > 0 && tcb->subscribers) {
            subs = tcb->subscribers;
            tcb->subscribers = NULL;
            tcb->subscriber_count = 0;
            tcb->subscriber_capacity = 0;
            for (uint32_t i = 0; i < sub_count; ++i) {
                TokaCompletionSubscription *subscription = subs[i];
                uint8_t expected = TOKA_COMPLETION_SUB_ACTIVE;
                if (!subscription ||
                    !toka_task_token_equals(subscription->child, tcb->token) ||
                    !atomic_compare_exchange_strong_explicit(
                        &subscription->state, &expected,
                        TOKA_COMPLETION_SUB_SELECTED_PUBLISHER,
                        memory_order_acq_rel, memory_order_acquire)) {
                    fprintf(stderr,
                            "Fatal error: invalid completion subscription during terminal publication.\n");
                    abort();
                }
            }
        }
        toka_mutex_unlock(&g_rt_mutex);

#ifdef TOKA_RUNTIME_TESTING
        toka_rt_test_pause_after_terminal_publish();
#endif

        if (subs) {
            for (uint32_t i = 0; i < sub_count; i++) {
                toka_completion_subscription_publish(subs[i]);
            }
            free(subs);
        }

        toka_task_try_drain_detached_result(tcb);
        toka_task_try_release_owner(tcb);
    }

    uintptr_t old_cont = atomic_exchange(&hdr->continuation, 1);
    if (old_cont > 1) {
        TokaTCB *awaiter_tcb = (TokaTCB*)old_cont;
        toka_task_note_await_child_terminal(awaiter_tcb, result_state);
        toka_task_clear_await_link(tcb, awaiter_tcb);
        toka_task_try_schedule_token_internal(
            awaiter_tcb->token,
            atomic_load(&awaiter_tcb->task_schedule_generation)
        );
        toka_task_release(awaiter_tcb);
    }
    if (pinned_frame) toka_tcb_release_frame_pin(tcb);
    if (retained_tcb) toka_task_release(tcb);
    return 1;
}

void toka_task_complete(void *promise_ptr) {
    toka_task_publish_terminal(promise_ptr, TOKA_RESULT_STATE_READYLIVE,
                               TOKA_TCB_COMPLETED);
}

void toka_task_complete_canceled(void *promise_ptr) {
    toka_task_publish_terminal(promise_ptr, TOKA_RESULT_STATE_CANCELED,
                               TOKA_TCB_COMPLETED_CANCELED);
}

int toka_task_await_prepare(void *child_promise_ptr, void *parent_tcb_ptr) {
    if (!child_promise_ptr || !parent_tcb_ptr) return 0;
    struct TokaPromiseHeader *hdr = (struct TokaPromiseHeader*)child_promise_ptr;
    TokaTCB *child_tcb = (TokaTCB*)hdr->self_tcb;
    if (!child_tcb || !toka_task_try_retain(child_tcb)) return 0;
    if (child_tcb->promise != child_promise_ptr ||
        !toka_task_try_retain(parent_tcb_ptr)) {
        toka_task_release(child_tcb);
        return 0;
    }
    TokaTCB *parent_tcb = (TokaTCB*)parent_tcb_ptr;

    toka_mutex_lock(&g_rt_mutex);
    if (!toka_tcb_try_retain_held(parent_tcb, 1)) {
        toka_mutex_unlock(&g_rt_mutex);
        toka_task_release(parent_tcb);
        toka_task_release(child_tcb);
        return 0;
    }
    uint64_t ignored_generation = 0;
    uint32_t expected_state = TOKA_TCB_RUNNING;
    const int advanced = toka_advance_schedule_generation(
        &parent_tcb->task_schedule_generation, &ignored_generation);
    const int suspended = advanced && atomic_compare_exchange_strong(
        &parent_tcb->state, &expected_state, TOKA_TCB_SUSPENDED);
    if (!suspended) {
        if (advanced) {
            atomic_fetch_sub(&parent_tcb->task_schedule_generation, 1);
        }
        toka_tcb_drop_temporary_retain_locked(parent_tcb, 1);
        toka_mutex_unlock(&g_rt_mutex);
        toka_task_release(parent_tcb);
        toka_task_release(child_tcb);
        return 0;
    }

    atomic_store_explicit(&parent_tcb->await_resolution,
                          TOKA_AWAIT_RESOLUTION_ARMED,
                          memory_order_release);
    atomic_store(&parent_tcb->active_child_tcb, (uintptr_t)child_tcb);
    atomic_store(&child_tcb->parent_tcb, (uintptr_t)parent_tcb);
    toka_mutex_unlock(&g_rt_mutex);

    uintptr_t expected_cont = 0;
    uintptr_t desired_cont = (uintptr_t)parent_tcb;
    if (atomic_compare_exchange_strong(&hdr->continuation, &expected_cont, desired_cont)) {
        // The installed continuation keeps the parent alive. These were only
        // entry-validation references for the raw promise/TCB arguments.
        toka_task_release(parent_tcb);
        toka_task_release(child_tcb);
        return 1;
    }

    toka_task_clear_await_link(child_tcb, parent_tcb);
    atomic_store(&parent_tcb->state, TOKA_TCB_RUNNING);
    // Drop the installed-continuation retain, then the entry-validation one.
    toka_task_release(parent_tcb);
    toka_task_release(parent_tcb);
    toka_task_release(child_tcb);
    return 0;
}

int toka_task_register_continuation(void *child_promise_ptr, void *parent_tcb_ptr) {
    return toka_task_await_prepare(child_promise_ptr, parent_tcb_ptr);
}

// Returns 1 for a privately claimed normal result, -1 for the single
// cancellation/source outcome, and 0 while this direct await has not reached a
// terminal arbitration point. It is used only by compiler-generated await
// lowering; it is not a source-level runtime API.
int toka_task_resolve_await(void *child_promise_ptr, void *parent_tcb_ptr) {
    if (!child_promise_ptr || !parent_tcb_ptr ||
        !toka_task_try_retain(parent_tcb_ptr)) {
        return 0;
    }
    TokaTCB *parent_tcb = (TokaTCB*)parent_tcb_ptr;
    struct TokaPromiseHeader *hdr =
        (struct TokaPromiseHeader*)child_promise_ptr;
    TokaTCB *child_tcb = (TokaTCB*)hdr->self_tcb;
    const int child_retained = child_tcb && toka_task_try_retain(child_tcb);
    if (!child_retained ||
        child_tcb->promise != child_promise_ptr) {
        if (child_retained) toka_task_release(child_tcb);
        toka_task_release(parent_tcb);
        return 0;
    }

    int result = 0;
    for (;;) {
        uint8_t resolution = atomic_load_explicit(&parent_tcb->await_resolution,
                                                  memory_order_acquire);
        if (resolution == TOKA_AWAIT_RESOLUTION_CANCEL_CLAIMED ||
            resolution == TOKA_AWAIT_RESOLUTION_CHILD_CANCELED) {
            result = -1;
            break;
        }
        if (resolution == TOKA_AWAIT_RESOLUTION_NORMAL_CLAIMED) {
            result = 1;
            break;
        }

        if (resolution == TOKA_AWAIT_RESOLUTION_CHILD_NORMAL) {
            uint8_t expected = TOKA_AWAIT_RESOLUTION_CHILD_NORMAL;
            uint8_t desired = atomic_load_explicit(&parent_tcb->cancel_requested,
                                                   memory_order_acquire)
                ? TOKA_AWAIT_RESOLUTION_CANCEL_CLAIMED
                : TOKA_AWAIT_RESOLUTION_NORMAL_CLAIMED;
            if (atomic_compare_exchange_weak_explicit(
                    &parent_tcb->await_resolution, &expected, desired,
                    memory_order_acq_rel, memory_order_acquire)) {
                result = desired == TOKA_AWAIT_RESOLUTION_NORMAL_CLAIMED
                    ? 1 : -1;
                break;
            }
            continue;
        }

        if (resolution != TOKA_AWAIT_RESOLUTION_ARMED) {
            break;
        }
        if (atomic_load_explicit(&parent_tcb->cancel_requested,
                                 memory_order_acquire)) {
            uint8_t expected = TOKA_AWAIT_RESOLUTION_ARMED;
            atomic_compare_exchange_weak_explicit(
                &parent_tcb->await_resolution, &expected,
                TOKA_AWAIT_RESOLUTION_CANCEL_CLAIMED,
                memory_order_acq_rel, memory_order_acquire
            );
            continue;
        }

        uint32_t child_state = atomic_load_explicit(&child_tcb->state,
                                                    memory_order_acquire);
        uint8_t child_result = atomic_load_explicit(&hdr->result_state,
                                                    memory_order_acquire);
        uint8_t desired = TOKA_AWAIT_RESOLUTION_IDLE;
        if (child_state == TOKA_TCB_COMPLETED_CANCELED ||
            child_result == TOKA_RESULT_STATE_CANCELED) {
            desired = TOKA_AWAIT_RESOLUTION_CHILD_CANCELED;
        } else if (child_state == TOKA_TCB_COMPLETED &&
                   child_result == TOKA_RESULT_STATE_READYLIVE) {
            desired = TOKA_AWAIT_RESOLUTION_CHILD_NORMAL;
        } else {
            break;
        }
        uint8_t expected = TOKA_AWAIT_RESOLUTION_ARMED;
        atomic_compare_exchange_weak_explicit(
            &parent_tcb->await_resolution, &expected, desired,
            memory_order_acq_rel, memory_order_acquire
        );
    }

    toka_task_release(child_tcb);
    toka_task_release(parent_tcb);
    return result;
}

void toka_task_finish_await_resolution(void *parent_tcb_ptr) {
    if (!parent_tcb_ptr || !toka_task_try_retain(parent_tcb_ptr)) return;
    TokaTCB *parent_tcb = (TokaTCB*)parent_tcb_ptr;
    atomic_store_explicit(&parent_tcb->await_resolution,
                          TOKA_AWAIT_RESOLUTION_IDLE,
                          memory_order_release);
    toka_task_release(parent_tcb);
}

static void toka_task_detach_owned(TokaTCB *tcb, TokaResultOwner expected_owner) {
    if (!tcb) return;

    // The caller owns one reference. Hold an additional transient reference
    // while terminal result disposition runs outside the runtime arbiter.
    toka_tcb_require_retain_held(tcb, 1, "task detach");

    uint8_t expected = expected_owner;
    if (!atomic_compare_exchange_strong(&tcb->result_owner, &expected,
                                        TOKA_RESULT_OWNER_DETACHED)) {
        toka_task_release(tcb);
        return;
    }

    // Mark detached only after its result authority has moved. A scope-owned
    // task therefore cannot be silently detached through the public handle
    // path while it remains enrolled.
    atomic_store(&tcb->detached, 1);

    // Explicit detach keeps its historical meaning for a cold handle: it
    // starts the task under runtime ownership. TaskHandle destruction uses the
    // separate drop entry below, which instead claims cold cancellation.
    if (atomic_load(&tcb->state) == TOKA_TCB_CREATED) {
        toka_task_start(tcb);
    }

    // Linearize the observability counter with terminal completion.
    toka_mutex_lock(&g_rt_mutex);
    uint32_t st = atomic_load(&tcb->state);
    if (!toka_tcb_is_terminal(st) && st != TOKA_TCB_CREATED) {
        uint8_t expected_counted = 0;
        if (atomic_compare_exchange_strong(&tcb->detached_counted, &expected_counted, 1)) {
            g_active_detached_task_count++;
        }
    }
    toka_mutex_unlock(&g_rt_mutex);

    toka_task_try_drain_detached_result(tcb);
    toka_task_try_release_owner(tcb);

    toka_task_release(tcb);
}

void toka_task_detach(void *tcb_ptr) {
    toka_task_detach_owned((TokaTCB*)tcb_ptr, TOKA_RESULT_OWNER_CONSUMER);
}

void toka_task_drop_handle(void *tcb_ptr) {
    if (!tcb_ptr) return;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;

    // A TaskHandle is the consumer's unique owner. Preserve an internal
    // reference while its destructor transfers that ownership and any cold
    // finalizer may run arbitrary frame cleanup.
    toka_tcb_require_retain_held(tcb, 1, "TaskHandle drop");
    if (atomic_load(&tcb->state) == TOKA_TCB_CREATED) {
        uint8_t expected_owner = TOKA_RESULT_OWNER_CONSUMER;
        if (atomic_compare_exchange_strong(&tcb->result_owner, &expected_owner,
                                            TOKA_RESULT_OWNER_DETACHED)) {
            atomic_store(&tcb->detached, 1);
            toka_task_request_cancel(tcb);

            // A concurrent start can turn this into ordinary detached
            // cancellation. Count that observable live task exactly once;
            // the terminal publisher performs the matching decrement.
            toka_mutex_lock(&g_rt_mutex);
            uint32_t st = atomic_load(&tcb->state);
            if (!toka_tcb_is_terminal(st)) {
                uint8_t expected_counted = 0;
                if (atomic_compare_exchange_strong(&tcb->detached_counted,
                                                    &expected_counted, 1)) {
                    g_active_detached_task_count++;
                }
            }
            toka_mutex_unlock(&g_rt_mutex);
            toka_task_release(tcb);
            return;
        }
    }
    toka_task_release(tcb);
    toka_task_detach_owned(tcb, TOKA_RESULT_OWNER_CONSUMER);
}

void toka_tcb_get_wait_token(void *tcb_ptr, uint64_t *out_task_id, uint64_t *out_gen) {
    (void)tcb_ptr;
    if (out_task_id) *out_task_id = 0;
    if (out_gen) *out_gen = 0;
    (void)toka_reject_legacy_bare_task_id_api("task token observation");
}

void toka_tcb_get_wait_token_with_instance(
    void *tcb_ptr, uint64_t *out_task_id, uint64_t *out_instance_generation,
    uint64_t *out_gen
) {
    if (out_task_id) *out_task_id = 0;
    if (out_instance_generation) *out_instance_generation = 0;
    if (out_gen) *out_gen = 0;
    if (!tcb_ptr || !toka_task_try_retain(tcb_ptr)) return;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;
    if (out_task_id) *out_task_id = tcb->token.task_id;
    if (out_instance_generation) {
        *out_instance_generation = tcb->token.task_instance_generation;
    }
    if (out_gen) *out_gen = atomic_load(&tcb->task_schedule_generation);
    toka_task_release(tcb);
}

int toka_task_get_current_token(
    void *coro_frame, uint64_t *out_task_id,
    uint64_t *out_instance_generation, uint64_t *out_gen
) {
    TokaTCB *tcb = lookup_tcb_by_frame_retained(coro_frame);
    if (!tcb) return 0;
    if (out_task_id) *out_task_id = tcb->token.task_id;
    if (out_instance_generation) {
        *out_instance_generation = tcb->token.task_instance_generation;
    }
    if (out_gen) *out_gen = atomic_load(&tcb->task_schedule_generation);
    toka_task_release(tcb);
    return 1;
}

void toka_task_publish_result_state(void *promise_ptr, uint8_t state) {
    // Compatibility entry point for objects compiled before terminal
    // publication was unified. It must not write result_state directly: that
    // would expose ReadyLive without the matching exactly-once terminal path.
    if (state == TOKA_RESULT_STATE_READYLIVE) {
        toka_task_complete(promise_ptr);
    } else if (state == TOKA_RESULT_STATE_CANCELED) {
        toka_task_complete_canceled(promise_ptr);
    }
}

// The compiler's destroy path asks this before freeing a frame. A cold task
// retains its frame after destroy returns so the promise/result ABI remains
// readable until the final TCB reference is released.
int toka_task_should_defer_cold_frame_free(void *promise_ptr) {
    if (!promise_ptr) return 0;
    struct TokaPromiseHeader *hdr = (struct TokaPromiseHeader*)promise_ptr;
    TokaTCB *tcb = (TokaTCB*)hdr->self_tcb;
    return tcb && atomic_load(&tcb->cold_cleanup_supported) &&
           atomic_load(&tcb->state) == TOKA_TCB_COLD_FINALIZING;
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
    TokaTCB *tcb = (TokaTCB*)hdr->self_tcb;

    if (!tcb) {
        // Preserve the legacy promise-only ABI. TCB-backed promises use the
        // private claim below; promise-only objects have only result_state as
        // their ownership gate.
        uint8_t result_state = atomic_load_explicit(&hdr->result_state,
                                                    memory_order_acquire);
        if (result_state == TOKA_RESULT_STATE_CANCELED) return -1;
        if (result_state != TOKA_RESULT_STATE_READYLIVE) return 0;
        uint8_t expected = TOKA_RESULT_STATE_READYLIVE;
        return atomic_compare_exchange_strong_explicit(
            &hdr->result_state, &expected, TOKA_RESULT_STATE_TAKEN,
            memory_order_acq_rel, memory_order_acquire);
    }

    // A promise can outlive a frame-less test task's final TCB reference.
    // Preserve the legacy promise-only path above, but never dereference a
    // non-null stale `self_tcb`: typed compiler paths use the stronger access
    // guard below, while this compatibility claim fails closed.
    if (!toka_task_try_retain(tcb)) return 0;
    if (tcb->promise != promise_ptr) {
        toka_task_release(tcb);
        return 0;
    }

    // A public ReadyLive byte alone is not a payload authorization: terminal
    // publication writes it before release-publishing Completed. The consumer
    // must first observe normal completion, then ReadyLive, and finally win the
    // same private claim word used by scope and detached result disposal.
    uint32_t terminal_state = atomic_load_explicit(&tcb->state,
                                                   memory_order_acquire);
    if (terminal_state == TOKA_TCB_COMPLETED_CANCELED) {
        toka_task_release(tcb);
        return -1;
    }
    if (terminal_state != TOKA_TCB_COMPLETED) {
        toka_task_release(tcb);
        return 0;
    }
    if (atomic_load_explicit(&tcb->result_owner, memory_order_acquire) !=
            TOKA_RESULT_OWNER_CONSUMER) {
        toka_task_release(tcb);
        return 0;
    }

    uint8_t result_state = atomic_load_explicit(&hdr->result_state,
                                                memory_order_acquire);
    if (result_state == TOKA_RESULT_STATE_CANCELED) {
        toka_task_release(tcb);
        return -1;
    }
    if (result_state != TOKA_RESULT_STATE_READYLIVE) {
        toka_task_release(tcb);
        return 0;
    }

    uint8_t expected_disposition = TOKA_RESULT_DISPOSITION_UNCLAIMED;
    if (!atomic_compare_exchange_strong_explicit(
            &tcb->result_disposition, &expected_disposition,
            TOKA_RESULT_DISPOSITION_CLAIMED_BY_CONSUMER,
            memory_order_acq_rel, memory_order_acquire)) {
        toka_task_release(tcb);
        return 0;
    }

    // The private claim excludes every other runtime disposition path. A
    // normal terminal state already guarantees ReadyLive cannot be relabeled
    // Canceled, so release-publishing Taken is the consumer's final commit.
    atomic_store_explicit(&hdr->result_state, TOKA_RESULT_STATE_TAKEN,
                          memory_order_release);
    toka_task_release(tcb);
    return 1;
}

// Compiler-generated await and async-main lowering need the result storage to
// remain valid from the private claim through the typed load. The existing
// `toka_task_take_result` compatibility entry cannot express that interval,
// so this internal guard transfers one checked TCB retain (and, when needed,
// one frame pin) to the caller. It is deliberately not a source-level API.
int __toka_task_take_result_access(void *promise_ptr, void **out_value_ptr,
                                   void **out_access_guard) {
    if (out_value_ptr) *out_value_ptr = NULL;
    if (out_access_guard) *out_access_guard = NULL;
    if (!promise_ptr || !out_value_ptr || !out_access_guard) return 0;

    struct TokaPromiseHeader *hdr = (struct TokaPromiseHeader*)promise_ptr;
    TokaTCB *tcb = (TokaTCB*)hdr->self_tcb;
    // Do not dereference a promise-provided raw TCB pointer until the task
    // registry has converted it into a checked retained reference.
    if (!tcb || !toka_task_try_retain(tcb)) return 0;
    if (tcb->promise != promise_ptr) {
        toka_task_release(tcb);
        return 0;
    }

    uint32_t terminal_state = atomic_load_explicit(&tcb->state,
                                                   memory_order_acquire);
    if (terminal_state == TOKA_TCB_COMPLETED_CANCELED) {
        toka_task_release(tcb);
        return -1;
    }
    if (terminal_state != TOKA_TCB_COMPLETED ||
        atomic_load_explicit(&tcb->result_owner, memory_order_acquire) !=
            TOKA_RESULT_OWNER_CONSUMER) {
        toka_task_release(tcb);
        return 0;
    }

    uint8_t result_state = atomic_load_explicit(&hdr->result_state,
                                                memory_order_acquire);
    if (result_state == TOKA_RESULT_STATE_CANCELED) {
        toka_task_release(tcb);
        return -1;
    }
    if (result_state != TOKA_RESULT_STATE_READYLIVE) {
        toka_task_release(tcb);
        return 0;
    }

    const int pinned_frame = tcb->coro_frame != NULL;
    if (pinned_frame && !toka_tcb_try_acquire_frame_pin(tcb)) {
        toka_task_release(tcb);
        return 0;
    }

    uint8_t expected_disposition = TOKA_RESULT_DISPOSITION_UNCLAIMED;
    if (!atomic_compare_exchange_strong_explicit(
            &tcb->result_disposition, &expected_disposition,
            TOKA_RESULT_DISPOSITION_CLAIMED_BY_CONSUMER,
            memory_order_acq_rel, memory_order_acquire)) {
        if (pinned_frame) toka_tcb_release_frame_pin(tcb);
        toka_task_release(tcb);
        return 0;
    }

    atomic_store_explicit(&hdr->result_state, TOKA_RESULT_STATE_TAKEN,
                          memory_order_release);
    *out_value_ptr = toka_task_result_value_ptr(promise_ptr);
    *out_access_guard = tcb;
    return 1;
}

void __toka_task_release_result_access(void *access_guard) {
    TokaTCB *tcb = (TokaTCB*)access_guard;
    if (!tcb) return;
    // The guard's checked retain keeps both the TCB and its non-null frame
    // stable until this release. A frame-less compatibility task needs only
    // the lifetime reference.
    if (tcb->coro_frame) {
        toka_tcb_release_frame_pin(tcb);
    }
    toka_task_release(tcb);
}

// Both scope closing and detached completion use this one linear disposition.
// It first publishes Dropping as the private exclusion claim, runs the typed
// callback, then release-publishes ReadyLive -> Taken and Dropped. The caller
// owns a stable TCB reference; the extra retain keeps the TCB alive if the
// generated drop hook re-enters task code.
static int toka_task_dispose_result_for_owner(TokaTCB *tcb,
                                              TokaResultOwner owner) {
    if (!tcb || atomic_load(&tcb->result_owner) != owner) return 0;
    // ReadyLive becomes externally observable before the terminal publisher
    // release-publishes Completed. A runtime owner may not claim/drop until it
    // has observed that normal terminal state.
    if (atomic_load_explicit(&tcb->state, memory_order_acquire) !=
        TOKA_TCB_COMPLETED) {
        return 0;
    }
    if (!tcb->promise) return 1;

    struct TokaPromiseHeader *hdr =
        (struct TokaPromiseHeader*)tcb->promise;
    uint8_t state = atomic_load_explicit(&hdr->result_state,
                                         memory_order_acquire);
    if (state == TOKA_RESULT_STATE_CANCELED ||
        state == TOKA_RESULT_STATE_TAKEN) {
        return 1;
    }
    if (state != TOKA_RESULT_STATE_READYLIVE) return 0;

    toka_tcb_require_retain_held(tcb, 1, "result disposition");
    uint8_t expected_disposition = TOKA_RESULT_DISPOSITION_UNCLAIMED;
    if (!atomic_compare_exchange_strong_explicit(
            &tcb->result_disposition, &expected_disposition,
            TOKA_RESULT_DISPOSITION_DROPPING, memory_order_acq_rel,
            memory_order_acquire)) {
        toka_task_release(tcb);
        return expected_disposition == TOKA_RESULT_DISPOSITION_DROPPED ||
               expected_disposition ==
                   TOKA_RESULT_DISPOSITION_CLAIMED_BY_CONSUMER;
    }

    const int pinned_frame = toka_tcb_try_acquire_frame_pin(tcb);
    if (tcb->coro_frame && !pinned_frame) {
        fprintf(stderr, "Fatal error: result disposition lost its frame pin.\n");
        abort();
    }
    if (tcb->result_drop_fn) {
        tcb->result_drop_fn(toka_task_result_value_ptr(tcb->promise));
    }
    atomic_store_explicit(&hdr->result_state, TOKA_RESULT_STATE_TAKEN,
                          memory_order_release);
    atomic_store_explicit(&tcb->result_disposition,
                          TOKA_RESULT_DISPOSITION_DROPPED,
                          memory_order_release);
    if (pinned_frame) {
        toka_tcb_release_frame_pin(tcb);
    }
    toka_task_release(tcb);
    return 1;
}

static int toka_task_try_drain_detached_result(TokaTCB *tcb) {
    return toka_task_dispose_result_for_owner(tcb,
                                              TOKA_RESULT_OWNER_DETACHED);
}

static int toka_task_dispose_scope_result(TokaTCB *tcb) {
    return toka_task_dispose_result_for_owner(tcb, TOKA_RESULT_OWNER_SCOPE);
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

void toka_set_errno_impl(int32_t err) {
    errno = (int)err;
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
    TokaTCB **children_to_release = NULL;
    uint32_t child_count = 0;
    TokaTaskScopeRegistry **scopes_to_free = NULL;

    toka_mutex_lock(&g_rt_mutex);
    uint32_t refs = atomic_load_explicit(&tcb->ref_count, memory_order_acquire);
    while (refs != 0 && !atomic_compare_exchange_weak_explicit(
                            &tcb->ref_count, &refs, refs - 1,
                            memory_order_acq_rel, memory_order_acquire)) {
    }
    if (refs == 0) {
        toka_mutex_unlock(&g_rt_mutex);
        fprintf(stderr, "Fatal error: TCB reference underflow.\n");
        abort();
    }
    if (refs == 1) {
        toka_task_registry_unregister_locked(tcb);
        if (tcb->coro_frame) {
            toka_tcb_retire_frame_locked(tcb);
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
        // Every active completion subscription owns a checked child retain.
        // Reaching final release with one still linked would prove that a
        // terminal publisher/unsubscriber lost its unique release action.
        if (tcb->subscribers || tcb->subscriber_count != 0) {
            toka_mutex_unlock(&g_rt_mutex);
            fprintf(stderr,
                    "Fatal error: final TCB release with active completion subscription.\n");
            abort();
        }
        children_to_release = tcb->cancel_children;
        child_count = tcb->cancel_child_count;
        tcb->cancel_children = NULL;
        tcb->cancel_child_count = 0;
        tcb->cancel_child_capacity = 0;
        scopes_to_free = tcb->cancel_scopes;
        tcb->cancel_scopes = NULL;
        tcb->cancel_scope_count = 0;
        tcb->cancel_scope_capacity = 0;

        toka_mutex_unlock(&g_rt_mutex);

        if (frame_to_destroy) {
            uint32_t st = atomic_load(&tcb->state);
            if (st == TOKA_TCB_COMPLETED ||
                (st == TOKA_TCB_COMPLETED_CANCELED &&
                 atomic_load(&tcb->cold_cleanup_supported) &&
                 atomic_load(&tcb->cold_cleanup_finished))) {
                free(frame_to_destroy);
            } else {
                destroy_coro_frame(frame_to_destroy);
            }
        }
        for (uint32_t i = 0; i < child_count; ++i) {
            toka_task_release(children_to_release[i]);
        }
        free(children_to_release);
        free(scopes_to_free);
        free(tcb);
        return;
    }
    toka_mutex_unlock(&g_rt_mutex);
}

int toka_task_try_retain(void *tcb_ptr) {
    if (!tcb_ptr) return 0;
    toka_mutex_lock(&g_rt_mutex);
    TokaTCB *tcb = toka_task_registry_retain_by_pointer_locked(tcb_ptr, 1);
    toka_mutex_unlock(&g_rt_mutex);
    return tcb != NULL;
}

void toka_task_retain(void *tcb_ptr) {
    if (!tcb_ptr) return;
    if (!toka_task_try_retain(tcb_ptr)) {
        fprintf(stderr, "Fatal error: invalid TCB reference retain.\n");
        abort();
    }
}

void* toka_task_scope_create(void) {
    TokaTaskScopeRegistry *scope =
        (TokaTaskScopeRegistry*)calloc(1, sizeof(TokaTaskScopeRegistry));
    if (!scope) return NULL;
    atomic_store(&scope->ref_count, 1);
    scope->state = TOKA_TASK_SCOPE_OPEN;

    // A scope keeps its parent TCB alive, while the parent stores only a
    // mutex-protected weak list entry. Cancellation snapshots a temporary
    // scope reference under that same lock, so neither side can resurrect or
    // dereference a released registry.
    TokaTCB *parent = lookup_tcb_by_frame_retained(NULL);
    if (parent) {
        toka_mutex_lock(&g_rt_mutex);
        if (!toka_task_scope_parent_ensure_capacity_locked(parent)) {
            toka_mutex_unlock(&g_rt_mutex);
            toka_task_release(parent);
            free(scope);
            return NULL;
        }
        scope->parent = parent;
        parent->cancel_scopes[parent->cancel_scope_count++] = scope;
        if (atomic_load(&parent->cancel_requested)) {
            scope->state = TOKA_TASK_SCOPE_CLOSING;
        }
        toka_mutex_unlock(&g_rt_mutex);
    }
    return scope;
}

// On success, ownership of the caller-held TCB reference moves into the
// registry.  No retain is taken: the source TaskHandle is cleared immediately
// after this call by the standard-library wrapper.
int toka_task_scope_try_enroll(void *scope_ptr, void *tcb_ptr) {
    if (!scope_ptr || !tcb_ptr) return 0;
    TokaTaskScopeRegistry *scope = (TokaTaskScopeRegistry*)scope_ptr;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;

    toka_mutex_lock(&g_rt_mutex);
    if (scope->parent && atomic_load(&scope->parent->cancel_requested)) {
        scope->state = TOKA_TASK_SCOPE_CLOSING;
    }
    if (scope->state != TOKA_TASK_SCOPE_OPEN) {
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }
    if (!toka_task_scope_ensure_capacity_locked(scope)) {
        toka_mutex_unlock(&g_rt_mutex);
        return -1;
    }
    uint8_t expected_owner = TOKA_RESULT_OWNER_CONSUMER;
    if (!atomic_compare_exchange_strong(&tcb->result_owner, &expected_owner,
                                        TOKA_RESULT_OWNER_SCOPE)) {
        toka_mutex_unlock(&g_rt_mutex);
        return -2;
    }
    scope->children[scope->child_count++] = tcb;
    toka_mutex_unlock(&g_rt_mutex);
    return 1;
}

// Closing is idempotent.  A completed scope is never reopened.
int toka_task_scope_begin_close(void *scope_ptr) {
    if (!scope_ptr) return 0;
    TokaTaskScopeRegistry *scope = (TokaTaskScopeRegistry*)scope_ptr;
    toka_mutex_lock(&g_rt_mutex);
    if (scope->state == TOKA_TASK_SCOPE_OPEN) {
        scope->state = TOKA_TASK_SCOPE_CLOSING;
    }
    int active = scope->state == TOKA_TASK_SCOPE_CLOSING;
    toka_mutex_unlock(&g_rt_mutex);
    return active;
}

int toka_task_scope_is_done(void *scope_ptr) {
    if (!scope_ptr) return 1;
    TokaTaskScopeRegistry *scope = (TokaTaskScopeRegistry*)scope_ptr;
    toka_mutex_lock(&g_rt_mutex);
    for (uint32_t i = 0; i < scope->child_count; ++i) {
        if (!toka_tcb_is_terminal(atomic_load(&scope->children[i]->state))) {
            toka_mutex_unlock(&g_rt_mutex);
            return 0;
        }
    }
    toka_mutex_unlock(&g_rt_mutex);
    return 1;
}

// Snapshot retained references before cancellation.  Requesting cancellation
// is intentionally outside the scope arbiter: a child path may publish
// terminal state and must never wait on this registry lock.
void toka_task_scope_request_cancel_all(void *scope_ptr) {
    if (!scope_ptr) return;
    TokaTaskScopeRegistry *scope = (TokaTaskScopeRegistry*)scope_ptr;
    TokaTCB **children = NULL;
    uint32_t child_count = 0;

    toka_mutex_lock(&g_rt_mutex);
    child_count = scope->child_count;
    if (child_count > 0) {
        children = (TokaTCB**)malloc(child_count * sizeof(TokaTCB*));
        if (!children) {
            toka_mutex_unlock(&g_rt_mutex);
            fprintf(stderr, "Fatal error: unable to snapshot TaskScope children.\n");
            abort();
        }
        for (uint32_t i = 0; i < child_count; ++i) {
            children[i] = scope->children[i];
            toka_tcb_require_retain_held(children[i], 1,
                                         "TaskScope cancellation snapshot");
        }
    }
    toka_mutex_unlock(&g_rt_mutex);

    for (uint32_t i = 0; i < child_count; ++i) {
        toka_task_request_cancel(children[i]);
        toka_task_release(children[i]);
    }
    free(children);
}

uint32_t toka_task_scope_reap_finished(void *scope_ptr) {
    if (!scope_ptr) return 0;
    TokaTaskScopeRegistry *scope = (TokaTaskScopeRegistry*)scope_ptr;
    TokaTCB **finished = NULL;
    uint32_t finished_count = 0;

    toka_mutex_lock(&g_rt_mutex);
    if (scope->child_count > 0) {
        finished = (TokaTCB**)malloc(scope->child_count * sizeof(TokaTCB*));
        if (!finished) {
            toka_mutex_unlock(&g_rt_mutex);
            fprintf(stderr, "Fatal error: unable to reap TaskScope children.\n");
            abort();
        }
    }
    uint32_t i = 0;
    while (i < scope->child_count) {
        TokaTCB *child = scope->children[i];
        if (toka_tcb_is_terminal(atomic_load(&child->state))) {
            finished[finished_count++] = child;
            scope->children[i] = scope->children[scope->child_count - 1];
            scope->child_count--;
        } else {
            i++;
        }
    }
    toka_mutex_unlock(&g_rt_mutex);

    for (uint32_t j = 0; j < finished_count; ++j) {
        toka_task_dispose_scope_result(finished[j]);
        toka_task_release(finished[j]);
    }
    free(finished);
    return finished_count;
}

// Only an empty terminal registry can publish Closed.  The terminal entries'
// owning references are released after leaving the arbiter.
int toka_task_scope_finish_close(void *scope_ptr) {
    if (!scope_ptr) return 0;
    TokaTaskScopeRegistry *scope = (TokaTaskScopeRegistry*)scope_ptr;
    TokaTCB **finished = NULL;
    uint32_t finished_count = 0;

    toka_mutex_lock(&g_rt_mutex);
    if (scope->state == TOKA_TASK_SCOPE_CLOSED) {
        toka_mutex_unlock(&g_rt_mutex);
        return 1;
    }
    if (scope->state != TOKA_TASK_SCOPE_CLOSING) {
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }
    for (uint32_t i = 0; i < scope->child_count; ++i) {
        if (!toka_tcb_is_terminal(atomic_load(&scope->children[i]->state))) {
            toka_mutex_unlock(&g_rt_mutex);
            return 0;
        }
    }
    finished = scope->children;
    finished_count = scope->child_count;
    scope->children = NULL;
    scope->child_count = 0;
    scope->child_capacity = 0;
    scope->state = TOKA_TASK_SCOPE_CLOSED;
    toka_mutex_unlock(&g_rt_mutex);

    for (uint32_t i = 0; i < finished_count; ++i) {
        toka_task_dispose_scope_result(finished[i]);
        toka_task_release(finished[i]);
    }
    free(finished);
    return 1;
}

void toka_task_scope_release(void *scope_ptr) {
    if (!scope_ptr) return;
    TokaTaskScopeRegistry *scope = (TokaTaskScopeRegistry*)scope_ptr;
    TokaTCB **children = NULL;
    uint32_t child_count = 0;
    TokaTCB *parent = NULL;

    toka_mutex_lock(&g_rt_mutex);
    uint32_t refs = atomic_load(&scope->ref_count);
    if (refs == 0) {
        toka_mutex_unlock(&g_rt_mutex);
        fprintf(stderr, "Fatal error: TaskScope registry reference underflow.\n");
        abort();
    }
    if (refs > 1) {
        atomic_store(&scope->ref_count, refs - 1);
        toka_mutex_unlock(&g_rt_mutex);
        return;
    }
    atomic_store(&scope->ref_count, 0);

    parent = scope->parent;
    if (parent) {
        for (uint32_t i = 0; i < parent->cancel_scope_count; ++i) {
            if (parent->cancel_scopes[i] == scope) {
                parent->cancel_scopes[i] =
                    parent->cancel_scopes[parent->cancel_scope_count - 1];
                parent->cancel_scope_count--;
                break;
            }
        }
        scope->parent = NULL;
    }
    children = scope->children;
    child_count = scope->child_count;
    scope->children = NULL;
    scope->child_count = 0;
    scope->child_capacity = 0;
    scope->state = TOKA_TASK_SCOPE_CLOSED;
    toka_mutex_unlock(&g_rt_mutex);

    for (uint32_t i = 0; i < child_count; ++i) {
        // Scope destruction may be triggered by an enclosing cancellation
        // path. Transfer its owning reference to the detached protocol rather
        // than releasing a still-live child underneath its coroutine frame.
        toka_task_detach_owned(children[i], TOKA_RESULT_OWNER_SCOPE);
    }
    free(children);
    if (parent) toka_task_release(parent);
    free(scope);
}

int toka_task_register_cancel_child(void *parent_frame, void *child_ptr) {
    if (!parent_frame || !child_ptr) return 0;
    TokaTCB *parent = lookup_tcb_by_frame_retained(parent_frame);
    if (!parent) return 0;
    if (!toka_task_try_retain(child_ptr)) {
        toka_task_release(parent);
        return 0;
    }
    TokaTCB *child = (TokaTCB*)child_ptr;
    int request_cancel = 0;

    toka_mutex_lock(&g_rt_mutex);
    for (uint32_t i = 0; i < parent->cancel_child_count; ++i) {
        if (parent->cancel_children[i] == child) {
            toka_mutex_unlock(&g_rt_mutex);
            toka_task_release(child);
            toka_task_release(parent);
            return 1;
        }
    }
    if (parent->cancel_child_count >= parent->cancel_child_capacity) {
        uint32_t new_cap = parent->cancel_child_capacity == 0 ? 2 : parent->cancel_child_capacity * 2;
        TokaTCB **new_children = (TokaTCB**)realloc(parent->cancel_children, new_cap * sizeof(TokaTCB*));
        if (!new_children) {
            toka_mutex_unlock(&g_rt_mutex);
            toka_task_release(child);
            toka_task_release(parent);
            return 0;
        }
        parent->cancel_children = new_children;
        parent->cancel_child_capacity = new_cap;
    }
    // Transfer the checked entry retain into the parent's cancellation list.
    // Its removal path is the one unique release action for this authority.
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

void* toka_task_get_current_coro_frame(void) {
    TokaTCB *tcb = g_current_tcb;
    if (!tcb) return NULL;
    uint32_t state = atomic_load_explicit(&tcb->frame_access_state,
                                          memory_order_acquire);
    if (state == 0 || state == TOKA_FRAME_ACCESS_RETIRED) {
        return NULL;
    }
    return tcb->coro_frame;
}

// Result observation is TCB-owned authority. CodeGen must not recover this
// pointer by walking a raw coroutine frame: that would keep ordinary await and
// async-main result paths coupled to future frame-retirement timing.
void* toka_tcb_get_promise(void *tcb_ptr) {
    if (!tcb_ptr || !toka_task_try_retain(tcb_ptr)) return NULL;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;
    void *promise = tcb->promise;
    toka_task_release(tcb);
    return promise;
}

int toka_tcb_is_done(void *tcb_ptr) {
    if (!tcb_ptr) return 1;
    if (!toka_task_try_retain(tcb_ptr)) return 1;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;
    uint32_t st = atomic_load(&tcb->state);
    toka_task_release(tcb);
    return st == TOKA_TCB_COMPLETED || st == TOKA_TCB_COMPLETED_CANCELED;
}

int toka_tcb_is_canceled(void *tcb_ptr) {
    if (!tcb_ptr) return 0;
    if (!toka_task_try_retain(tcb_ptr)) return 0;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;
    int canceled = atomic_load(&tcb->state) == TOKA_TCB_COMPLETED_CANCELED;
    toka_task_release(tcb);
    return canceled;
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

typedef enum {
    TOKA_WAIT_SET_WAITING = 0,
    TOKA_WAIT_SET_WON_PENDING = 1,
    TOKA_WAIT_SET_WON_COMMITTED = 2,
    TOKA_WAIT_SET_INACTIVE = 3,
} TokaWaitSetState;

typedef struct {
    uint64_t wait_set_id;
    uint64_t wait_set_generation;
} TokaWaitSetToken;

typedef struct {
    TokaWaitSetToken token;
    TokaTaskToken task;
    uint64_t task_schedule_generation;
    _Atomic uint32_t state;
    _Atomic uint32_t winner_wait_id;
    // Every installed outcome slot owns one reference.  A selected winner
    // adds one private commit reference, so an observer can finish
    // WonPending even after the original publisher is preempted.
    uint32_t ref_count;
    uint8_t commit_ref_held;
    TokaTCB *tcb;
    TokaCompletionSubscription *subscriptions_to_release;
} TokaWaitSet;

#define TOKA_WAIT_SOURCE_TIMER 1

typedef struct {
    TokaWaitToken token;
    TokaTaskToken task;
    uint64_t task_schedule_generation;
    _Atomic uint32_t state;
    uint16_t source_tag;
    void *wait_set; // Reserved for AR-P5 WaitSet
    TokaTCB *tcb;   // Retained through the active wait or outcome query
    uint8_t in_use; // Physical slot reservation, retained through outcome read
    uint8_t active; // Event-eligible registry membership
    uint8_t retired; // Generation exhausted; never assign this slot again.
} TokaWaitRegistration;

static TokaWaitRegistration *g_wait_registry = NULL;
static size_t g_wait_registry_capacity = 0;
static size_t g_wait_registry_count = 0;
static _Atomic uint64_t g_next_wait_set_id = 1;

#ifdef TOKA_RUNTIME_TESTING
int toka_rt_test_set_next_wait_set_id(uint64_t next_id) {
    if (next_id == 0) return 0;
    atomic_store_explicit(&g_next_wait_set_id, next_id, memory_order_release);
    return 1;
}
#endif

typedef struct {
    TokaWaitSet *wait_set_to_free;
    TokaTCB *tcb_to_release;
    uint32_t tcb_release_count;
    TokaTCB *queue_publish_tcb;
    uint64_t queue_publish_generation;
    TokaCompletionSubscription *subscriptions_to_release;
} TokaWaitSetCancelCleanup;

static int toka_wait_set_cancel_group_and_wake_locked(
    TokaWaitSet *ws,
    TokaWaitSetCancelCleanup *cleanup,
    int force_cancel
);
static void toka_wait_set_publish_queue_ticket(TokaWaitSetCancelCleanup *cleanup);
static void toka_wait_set_finish_cancel_cleanup(TokaWaitSetCancelCleanup *cleanup);

static int toka_tcb_has_active_wait_set_locked(const TokaTCB *tcb,
                                                const TokaWaitSet *ws) {
    return tcb && ws &&
           atomic_load(&tcb->active_wait_set_id) == ws->token.wait_set_id &&
           atomic_load(&tcb->active_wait_set_generation) ==
               ws->token.wait_set_generation;
}

static void toka_tcb_clear_active_wait_set_locked(TokaTCB *tcb,
                                                   const TokaWaitSet *ws) {
    if (!toka_tcb_has_active_wait_set_locked(tcb, ws)) return;
    atomic_store(&tcb->active_wait_id, TOKA_NO_WAIT_ID);
    atomic_store(&tcb->active_slot_gen, 0);
    atomic_store(&tcb->active_wait_set_generation, 0);
    atomic_store(&tcb->active_wait_set_id, 0);
}

static int toka_tcb_can_install_wait_locked(TokaTCB *tcb,
                                            TokaTaskToken task,
                                            uint64_t generation) {
    if (!tcb || !toka_task_token_equals(tcb->token, task) ||
        atomic_load(&tcb->task_schedule_generation) != generation ||
        atomic_load(&tcb->active_wait_id) != TOKA_NO_WAIT_ID ||
        atomic_load(&tcb->active_wait_set_id) != 0) {
        return 0;
    }
    uint32_t state = atomic_load(&tcb->state);
    return state == TOKA_TCB_PREPARING ||
           state == TOKA_TCB_PREPARING_WITH_PENDING_WAKE;
}

static TokaWaitSet *toka_wait_set_create_locked(TokaTaskToken task,
                                                 uint64_t generation,
                                                 uint32_t member_count) {
    if (member_count == 0) return NULL;
#ifdef TOKA_RUNTIME_TESTING
    if (atomic_exchange(&g_test_fail_next_wait_set_create, 0)) return NULL;
#endif
    uint64_t wait_set_id = 0;
    if (!toka_allocate_nonzero_u64(&g_next_wait_set_id, &wait_set_id)) {
        return NULL;
    }
    TokaWaitSet *ws = (TokaWaitSet*)calloc(1, sizeof(TokaWaitSet));
    if (!ws) return NULL;
    ws->token = (TokaWaitSetToken){
        .wait_set_id = wait_set_id,
        .wait_set_generation = 1,
    };
    ws->task = task;
    ws->task_schedule_generation = generation;
    ws->ref_count = member_count;
    atomic_store(&ws->state, TOKA_WAIT_SET_WAITING);
    atomic_store(&ws->winner_wait_id, 0);
    return ws;
}

static void toka_wait_set_retain_locked(TokaWaitSet *ws) {
    if (!ws || ws->ref_count == UINT32_MAX) {
        fprintf(stderr, "Fatal error: WaitSet reference overflow.\n");
        abort();
    }
    ws->ref_count++;
}

static int toka_wait_set_drop_ref_locked(TokaWaitSet *ws) {
    if (!ws || ws->ref_count == 0) {
        fprintf(stderr, "Fatal error: WaitSet reference underflow.\n");
        abort();
    }
    ws->ref_count--;
    return ws->ref_count == 0;
}

static int toka_wait_slot_is_available(const TokaWaitRegistration *reg) {
    return reg && !reg->in_use && !reg->retired;
}

static void toka_wait_slot_advance_or_retire_locked(TokaWaitRegistration *reg) {
    if (!reg) return;
    if (reg->token.wait_slot_generation == UINT32_MAX) {
        reg->retired = 1;
        return;
    }
    reg->token.wait_slot_generation++;
}

static size_t toka_wait_registry_available_slots_locked(void) {
    size_t available = 0;
    for (size_t i = 0; i < g_wait_registry_capacity; ++i) {
        if (toka_wait_slot_is_available(&g_wait_registry[i])) {
            available++;
        }
    }
    return available;
}

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
        return;
    }

    const size_t available = toka_wait_registry_available_slots_locked();
    if (available >= needed_slots) return;

    if (g_wait_registry_capacity >= (SIZE_MAX / 2) ||
        g_wait_registry_capacity >= (UINT32_MAX / 2)) {
        fprintf(stderr, "Fatal error: WaitRegistry capacity overflow protection triggered.\n");
        abort();
    }

    const size_t old_cap = g_wait_registry_capacity;
    size_t new_cap = old_cap;
    while (available + (new_cap - old_cap) < needed_slots) {
        if (new_cap >= (SIZE_MAX / 2) || new_cap >= (UINT32_MAX / 2)) {
            fprintf(stderr, "Fatal error: WaitRegistry capacity overflow protection triggered.\n");
            abort();
        }
        new_cap *= 2;
    }
    TokaWaitRegistration *new_reg = (TokaWaitRegistration*)realloc(
        g_wait_registry, new_cap * sizeof(TokaWaitRegistration));
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

#ifdef TOKA_RUNTIME_TESTING
int toka_rt_test_set_wait_slot_generation(uint32_t wait_id,
                                           uint32_t generation) {
    if (generation == 0) return 0;
    toka_mutex_lock(&g_rt_mutex);
    if (wait_id >= g_wait_registry_capacity ||
        !g_wait_registry[wait_id].in_use) {
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }
    TokaWaitRegistration *reg = &g_wait_registry[wait_id];
    reg->token.wait_slot_generation = generation;
    if (reg->tcb && reg->active) {
        atomic_store(&reg->tcb->active_slot_gen, generation);
    }
    toka_mutex_unlock(&g_rt_mutex);
    return 1;
}
#endif

int toka_wait_registry_allocate_token(uint64_t task_id,
                                      uint64_t task_instance_generation,
                                      uint64_t gen, uint16_t source_tag,
                                      uint32_t *out_wait_id,
                                      uint32_t *out_slot_gen) {
    const TokaTaskToken task = {
        .task_id = task_id,
        .task_instance_generation = task_instance_generation,
    };
    toka_mutex_lock(&g_rt_mutex);
    ensure_free_slots_locked(1);

    TokaTCB *tcb = toka_task_registry_retain_by_token_locked(task, 1);
    if (!tcb || !toka_tcb_can_install_wait_locked(tcb, task, gen)) {
        if (tcb) toka_tcb_drop_temporary_retain_locked(tcb, 1);
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }

    uint32_t slot_idx = UINT32_MAX;
    for (size_t i = 0; i < g_wait_registry_capacity; ++i) {
        if (toka_wait_slot_is_available(&g_wait_registry[i])) {
            slot_idx = (uint32_t)i;
            break;
        }
    }
    if (slot_idx == UINT32_MAX) {
        ensure_free_slots_locked(1);
        for (size_t i = 0; i < g_wait_registry_capacity; ++i) {
            if (toka_wait_slot_is_available(&g_wait_registry[i])) {
                slot_idx = (uint32_t)i;
                break;
            }
        }
    }
    if (slot_idx == UINT32_MAX) {
        toka_tcb_drop_temporary_retain_locked(tcb, 1);
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }

    TokaWaitRegistration *reg = &g_wait_registry[slot_idx];
    reg->in_use = 1;
    reg->task = task;
    reg->task_schedule_generation = gen;
    atomic_store(&reg->state, TOKA_WAIT_STATE_WAITING);
    reg->source_tag = source_tag;
    reg->wait_set = NULL;
    reg->tcb = tcb;
    atomic_store(&tcb->active_wait_id, reg->token.wait_id);
    atomic_store(&tcb->active_slot_gen, reg->token.wait_slot_generation);
    reg->active = 1;
    g_wait_registry_count++;

    if (out_wait_id) *out_wait_id = reg->token.wait_id;
    if (out_slot_gen) *out_slot_gen = reg->token.wait_slot_generation;

    toka_mutex_unlock(&g_rt_mutex);
    return 1;
}

int toka_wait_registry_allocate(uint64_t task_id, uint64_t gen,
                                uint16_t source_tag, uint32_t *out_wait_id,
                                uint32_t *out_slot_gen) {
    (void)task_id;
    (void)gen;
    (void)source_tag;
    (void)out_wait_id;
    (void)out_slot_gen;
    return toka_reject_legacy_bare_task_id_api("wait registration");
}

int toka_wait_registry_allocate_pair_token(
    uint64_t task_id, uint64_t task_instance_generation, uint64_t gen,
    uint16_t tag1, uint16_t tag2, uint32_t *out_id1, uint32_t *out_gen1,
    uint32_t *out_id2, uint32_t *out_gen2
) {
    const TokaTaskToken task = {
        .task_id = task_id,
        .task_instance_generation = task_instance_generation,
    };
    toka_mutex_lock(&g_rt_mutex);
    ensure_free_slots_locked(2);

    TokaTCB *tcb = toka_task_registry_retain_by_token_locked(task, 2);
    if (!tcb || !toka_tcb_can_install_wait_locked(tcb, task, gen)) {
        if (tcb) toka_tcb_drop_temporary_retain_locked(tcb, 2);
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }

    uint32_t slot1 = UINT32_MAX, slot2 = UINT32_MAX;
    for (size_t i = 0; i < g_wait_registry_capacity; ++i) {
        if (toka_wait_slot_is_available(&g_wait_registry[i])) {
            if (slot1 == UINT32_MAX) slot1 = (uint32_t)i;
            else if (slot2 == UINT32_MAX) { slot2 = (uint32_t)i; break; }
        }
    }

    if (slot1 == UINT32_MAX || slot2 == UINT32_MAX) {
        toka_tcb_drop_temporary_retain_locked(tcb, 2);
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }

    TokaWaitSet *ws = toka_wait_set_create_locked(task, gen, 2);
    if (!ws) {
        toka_tcb_drop_temporary_retain_locked(tcb, 2);
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }
    ws->tcb = tcb;

    TokaWaitRegistration *reg1 = &g_wait_registry[slot1];
    reg1->in_use = 1;
    reg1->task = task;
    reg1->task_schedule_generation = gen;
    atomic_store(&reg1->state, TOKA_WAIT_STATE_WAITING);
    reg1->source_tag = tag1;
    reg1->wait_set = ws;
    reg1->tcb = tcb;
    reg1->active = 1;
    g_wait_registry_count++;

    TokaWaitRegistration *reg2 = &g_wait_registry[slot2];
    reg2->in_use = 1;
    reg2->task = task;
    reg2->task_schedule_generation = gen;
    atomic_store(&reg2->state, TOKA_WAIT_STATE_WAITING);
    reg2->source_tag = tag2;
    reg2->wait_set = ws;
    reg2->tcb = tcb;
    reg2->active = 1;
    g_wait_registry_count++;

    atomic_store(&tcb->active_wait_set_id, ws->token.wait_set_id);
    atomic_store(&tcb->active_wait_set_generation,
                 ws->token.wait_set_generation);
    atomic_store(&tcb->active_wait_id, reg1->token.wait_id);
    atomic_store(&tcb->active_slot_gen, reg1->token.wait_slot_generation);

    if (out_id1) *out_id1 = reg1->token.wait_id;
    if (out_gen1) *out_gen1 = reg1->token.wait_slot_generation;
    if (out_id2) *out_id2 = reg2->token.wait_id;
    if (out_gen2) *out_gen2 = reg2->token.wait_slot_generation;

    toka_mutex_unlock(&g_rt_mutex);
    return 1;
}

int toka_wait_registry_allocate_pair(
    uint64_t task_id, uint64_t gen, uint16_t tag1, uint16_t tag2,
    uint32_t *out_id1, uint32_t *out_gen1, uint32_t *out_id2,
    uint32_t *out_gen2
) {
    (void)task_id;
    (void)gen;
    (void)tag1;
    (void)tag2;
    (void)out_id1;
    (void)out_gen1;
    (void)out_id2;
    (void)out_gen2;
    return toka_reject_legacy_bare_task_id_api("wait-set registration");
}

int toka_wait_registry_allocate_nway_token(
    uint64_t task_id, uint64_t task_instance_generation, uint64_t gen,
    uint16_t tag_base, uint32_t count, uint32_t *out_ids, uint32_t *out_gens
) {
    if (count == 0 || !out_ids || !out_gens) return 0;
    const TokaTaskToken task = {
        .task_id = task_id,
        .task_instance_generation = task_instance_generation,
    };
    toka_mutex_lock(&g_rt_mutex);
    ensure_free_slots_locked(count);

    TokaTCB *tcb = toka_task_registry_retain_by_token_locked(task, count);
    if (!tcb || !toka_tcb_can_install_wait_locked(tcb, task, gen)) {
        if (tcb) toka_tcb_drop_temporary_retain_locked(tcb, count);
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }

#if SIZE_MAX < UINT64_MAX
    if (count > SIZE_MAX / sizeof(uint32_t)) {
        toka_tcb_drop_temporary_retain_locked(tcb, count);
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }
#endif
    uint32_t *slots = (uint32_t*)malloc((size_t)count * sizeof(*slots));
    if (!slots) {
        toka_tcb_drop_temporary_retain_locked(tcb, count);
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }

    uint32_t found = 0;
    for (size_t i = 0; i < g_wait_registry_capacity && found < count; ++i) {
        if (toka_wait_slot_is_available(&g_wait_registry[i])) {
            slots[found] = (uint32_t)i;
            found++;
        }
    }

    if (found < count) {
        free(slots);
        toka_tcb_drop_temporary_retain_locked(tcb, count);
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }

    TokaWaitSet *ws = toka_wait_set_create_locked(task, gen, count);
    if (!ws) {
        free(slots);
        toka_tcb_drop_temporary_retain_locked(tcb, count);
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }
    ws->tcb = tcb;

    for (uint32_t k = 0; k < count; ++k) {
        uint32_t slot = slots[k];
        TokaWaitRegistration *reg = &g_wait_registry[slot];
        reg->in_use = 1;
        reg->task = task;
        reg->task_schedule_generation = gen;
        atomic_store(&reg->state, TOKA_WAIT_STATE_WAITING);
        reg->source_tag = tag_base + (uint16_t)k;
        reg->wait_set = ws;
        reg->tcb = tcb;
        reg->active = 1;
        g_wait_registry_count++;

        out_ids[k] = reg->token.wait_id;
        out_gens[k] = reg->token.wait_slot_generation;
    }
    free(slots);

    atomic_store(&tcb->active_wait_set_id, ws->token.wait_set_id);
    atomic_store(&tcb->active_wait_set_generation,
                 ws->token.wait_set_generation);
    atomic_store(&tcb->active_wait_id, out_ids[0]);
    atomic_store(&tcb->active_slot_gen, out_gens[0]);

    toka_mutex_unlock(&g_rt_mutex);
    return 1;
}

int toka_wait_registry_allocate_nway(
    uint64_t task_id, uint64_t gen, uint16_t tag_base, uint32_t count,
    uint32_t *out_ids, uint32_t *out_gens
) {
    (void)task_id;
    (void)gen;
    (void)tag_base;
    (void)count;
    (void)out_ids;
    (void)out_gens;
    return toka_reject_legacy_bare_task_id_api("n-way wait registration");
}

typedef enum {
    TOKA_WAKE_STALE = 0,
    TOKA_WAKE_SINGLETON_WON = 1,
    TOKA_WAKE_PAIR_WON = 2,
    TOKA_WAKE_PAIR_DUPLICATE = 3,
    TOKA_WAKE_PAIR_LOST = 4
} TokaWakeOutcome;

typedef struct {
    TokaTCB *queue_publish_tcb;
    uint64_t queue_publish_generation;
    TokaCompletionSubscription *subscriptions_to_release;
} TokaWaitSetWinnerCleanup;

static int toka_wait_set_commit_winner_locked(
    TokaWaitSet *ws,
    TokaWaitSetWinnerCleanup *cleanup
) {
    if (!ws || !cleanup) return 0;
    uint32_t state = atomic_load_explicit(&ws->state, memory_order_acquire);
    if (state == TOKA_WAIT_SET_WON_COMMITTED) return 1;
    if (state != TOKA_WAIT_SET_WON_PENDING || !ws->tcb) return 0;

    TokaTCB *tcb = ws->tcb;
    // `active_wait_set` is a progress link while WonPending exists. Clear it
    // only at the commit point, so cancellation and generic scheduling cannot
    // bypass a selected-but-not-yet-committed descriptor.
    toka_tcb_clear_active_wait_set_locked(tcb, ws);

    // The logical uninstall occurred before WonPending. This release store is
    // the one commit point after which a queue ticket may become observable.
    atomic_store_explicit(&ws->state, TOKA_WAIT_SET_WON_COMMITTED,
                          memory_order_release);
    cleanup->subscriptions_to_release = ws->subscriptions_to_release;
    ws->subscriptions_to_release = NULL;
    if (!ws->commit_ref_held) {
        fprintf(stderr, "Fatal error: WaitSet winner commit lost its descriptor reference.\n");
        abort();
    }
    ws->commit_ref_held = 0;
    if (toka_wait_set_drop_ref_locked(ws)) {
        fprintf(stderr, "Fatal error: WaitSet committed without its outcome slots.\n");
        abort();
    }
    uint64_t gen = ws->task_schedule_generation;
    uint32_t tcb_state = atomic_load(&tcb->state);
    if (tcb_state == TOKA_TCB_PREPARING) {
        uint32_t preparing = TOKA_TCB_PREPARING;
        atomic_compare_exchange_strong(&tcb->state, &preparing,
                                       TOKA_TCB_PREPARING_WITH_PENDING_WAKE);
    } else if (tcb_state == TOKA_TCB_SUSPENDED) {
        toka_task_prepare_queue_ticket(tcb, gen);
        uint32_t suspended = TOKA_TCB_SUSPENDED;
        if (atomic_compare_exchange_strong(&tcb->state, &suspended,
                                           TOKA_TCB_QUEUED)) {
            cleanup->queue_publish_tcb = tcb;
            cleanup->queue_publish_generation = gen;
        }
    } else if (tcb_state == TOKA_TCB_QUEUED) {
        cleanup->queue_publish_tcb = tcb;
        cleanup->queue_publish_generation = gen;
    }
    // The stable winner record stays descriptor-referenced by the inactive
    // outcome slots, but the WaitSet itself is now logically uninstalled.
    // No worker, callback, or nested suspension can observe it as active.
    atomic_store_explicit(&ws->state, TOKA_WAIT_SET_INACTIVE,
                          memory_order_release);
    return 1;
}

static void toka_wait_set_finish_winner_cleanup(
    TokaWaitSetWinnerCleanup *cleanup
) {
    if (!cleanup) return;
    toka_completion_subscription_finish_parent_wait_teardown(
        cleanup->subscriptions_to_release
    );
    cleanup->subscriptions_to_release = NULL;
    if (cleanup->queue_publish_tcb) {
        toka_task_publish_queue_ticket(
            cleanup->queue_publish_tcb, cleanup->queue_publish_generation
        );
    }
}

// `ws` arrives with one caller-held descriptor reference.  That reference
// keeps the descriptor valid while this helper drops the arbiter, runs the
// selected teardown outside it, and lets another observer take over a
// preempted original winner publication.
static void toka_wait_set_help_commit_with_ref(TokaWaitSet *ws) {
    if (!ws) return;
    TokaWaitSetWinnerCleanup cleanup = {0};
    int free_wait_set = 0;
    toka_mutex_lock(&g_rt_mutex);
    toka_wait_set_commit_winner_locked(ws, &cleanup);
    free_wait_set = toka_wait_set_drop_ref_locked(ws);
    toka_mutex_unlock(&g_rt_mutex);

    toka_wait_set_finish_winner_cleanup(&cleanup);
    if (free_wait_set) free(ws);
}

// A TCB retains this progress link only while a selected WaitSet has not yet
// reached WonCommitted. Callers already hold a checked TCB reference; this
// helper takes a separate descriptor reference before dropping the arbiter.
static int toka_wait_registry_help_pending_for_tcb(TokaTCB *tcb) {
    if (!tcb) return 0;
    TokaWaitSet *wait_set = NULL;
    toka_mutex_lock(&g_rt_mutex);
    uint32_t wait_id = atomic_load(&tcb->active_wait_id);
    uint32_t slot_gen = atomic_load(&tcb->active_slot_gen);
    if (wait_id != TOKA_NO_WAIT_ID && wait_id < g_wait_registry_capacity) {
        TokaWaitRegistration *reg = &g_wait_registry[wait_id];
        if (reg->in_use && reg->token.wait_slot_generation == slot_gen &&
            reg->wait_set &&
            toka_tcb_has_active_wait_set_locked(
                tcb, (TokaWaitSet*)reg->wait_set
            ) &&
            atomic_load_explicit(
                &((TokaWaitSet*)reg->wait_set)->state, memory_order_acquire
            ) == TOKA_WAIT_SET_WON_PENDING) {
            wait_set = (TokaWaitSet*)reg->wait_set;
            toka_wait_set_retain_locked(wait_set);
        }
    }
    toka_mutex_unlock(&g_rt_mutex);
    if (!wait_set) return 0;
    toka_wait_set_help_commit_with_ref(wait_set);
    return 1;
}

static int toka_wait_set_select_source_locked(
    TokaWaitRegistration *winner,
    TokaWaitSet *ws,
    uint32_t wait_id
) {
    if (!winner || !ws || !winner->tcb || !winner->active ||
        atomic_load(&winner->state) != TOKA_WAIT_STATE_WAITING) {
        return TOKA_WAKE_PAIR_LOST;
    }

    uint64_t gen = winner->task_schedule_generation;
    TokaTCB *tcb = winner->tcb;
    if (!toka_task_token_equals(tcb->token, ws->task) ||
        ws->task_schedule_generation != gen ||
        atomic_load(&tcb->task_schedule_generation) != gen) {
        return TOKA_WAKE_STALE;
    }
    if (!toka_tcb_has_active_wait_set_locked(tcb, ws)) {
        return TOKA_WAKE_STALE;
    }

    uint32_t target_winner = wait_id + 1;
    uint32_t set_state = atomic_load(&ws->state);
    if (set_state == TOKA_WAIT_SET_WON_COMMITTED &&
        atomic_load(&ws->winner_wait_id) == target_winner) {
        return TOKA_WAKE_PAIR_DUPLICATE;
    }
    if (set_state != TOKA_WAIT_SET_WAITING) {
        return TOKA_WAKE_PAIR_LOST;
    }
    uint32_t expected_state = TOKA_WAIT_SET_WAITING;
    if (!atomic_compare_exchange_strong(&ws->state, &expected_state,
                                        TOKA_WAIT_SET_WON_PENDING)) {
        return TOKA_WAKE_PAIR_LOST;
    }
    atomic_store(&ws->winner_wait_id, target_winner);
    if (ws->commit_ref_held) {
        fprintf(stderr, "Fatal error: WaitSet selected twice.\n");
        abort();
    }
    ws->commit_ref_held = 1;
    toka_wait_set_retain_locked(ws);

    // The whole group becomes logically inactive before a worker, terminal
    // path, or nested wait can observe a queued parent.  The physical slots
    // retain the descriptor until their individual outcome reads release
    // them, so any observer can later help WonPending reach WonCommitted.
    for (size_t i = 0; i < g_wait_registry_capacity; ++i) {
        TokaWaitRegistration *member = &g_wait_registry[i];
        if (!member->in_use || !member->active || member->wait_set != ws) {
            continue;
        }
        atomic_store(&member->state,
                     member == winner ? TOKA_WAIT_STATE_WON
                                      : TOKA_WAIT_STATE_CANCELLED);
        member->active = 0;
        g_wait_registry_count--;
    }
    toka_completion_subscription_collect_parent_wait_teardown_locked(
        ws->task, ws->token.wait_set_id, ws->token.wait_set_generation,
        TOKA_NO_WAIT_ID, 0, &ws->subscriptions_to_release
    );
    return TOKA_WAKE_PAIR_WON;
}

static int toka_wait_registry_try_wake_checked(
    TokaTaskToken expected_parent, uint64_t expected_wait_set_id,
    uint64_t expected_wait_set_generation, uint32_t wait_id,
    uint32_t slot_gen
) {
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
    if ((expected_parent.task_id != 0 ||
         expected_parent.task_instance_generation != 0) &&
        !toka_task_token_equals(reg->task, expected_parent)) {
        toka_mutex_unlock(&g_rt_mutex);
        return TOKA_WAKE_STALE;
    }
    TokaWaitSet *wait_set = (TokaWaitSet*)reg->wait_set;
    if (wait_set && expected_wait_set_id != 0 &&
        (wait_set->token.wait_set_id != expected_wait_set_id ||
         wait_set->token.wait_set_generation != expected_wait_set_generation)) {
        toka_mutex_unlock(&g_rt_mutex);
        return TOKA_WAKE_STALE;
    }

    if (!reg->active) {
        int outcome = atomic_load(&reg->state) == TOKA_WAIT_STATE_WON
                        ? TOKA_WAKE_PAIR_DUPLICATE
                        : TOKA_WAKE_PAIR_LOST;
        int help_pending = wait_set &&
            atomic_load_explicit(&wait_set->state, memory_order_acquire) ==
                TOKA_WAIT_SET_WON_PENDING;
        if (help_pending) toka_wait_set_retain_locked(wait_set);
        toka_mutex_unlock(&g_rt_mutex);
        if (help_pending) toka_wait_set_help_commit_with_ref(wait_set);
        return outcome;
    }

    if (wait_set) {
        int outcome = toka_wait_set_select_source_locked(
            reg, wait_set, wait_id
        );
        if (outcome == TOKA_WAKE_PAIR_WON) {
            // This temporary descriptor reference survives a preempted
            // original publisher. An inactive slot can take over the same
            // commit later without touching a raw freed WaitSet pointer.
            toka_wait_set_retain_locked(wait_set);
        }
        toka_mutex_unlock(&g_rt_mutex);
#ifdef TOKA_RUNTIME_TESTING
        if (outcome == TOKA_WAKE_PAIR_WON &&
            atomic_exchange(&g_test_pause_next_wait_set_commit, 0)) {
            toka_mutex_lock(&g_rt_mutex);
            int free_wait_set = toka_wait_set_drop_ref_locked(wait_set);
            toka_mutex_unlock(&g_rt_mutex);
            if (free_wait_set) {
                fprintf(stderr, "Fatal error: paused WaitSet commit lost its slots.\n");
                abort();
            }
            return outcome;
        }
#endif
        if (outcome == TOKA_WAKE_PAIR_WON) {
            toka_wait_set_help_commit_with_ref(wait_set);
        }
        return outcome;
    }

    uint32_t expected = TOKA_WAIT_STATE_WAITING;
    if (!atomic_compare_exchange_strong(&reg->state, &expected, TOKA_WAIT_STATE_WON)) {
        toka_mutex_unlock(&g_rt_mutex);
        return TOKA_WAKE_PAIR_LOST;
    }

    TokaTaskToken task = reg->task;
    uint64_t gen = reg->task_schedule_generation;
    int is_singleton = (reg->wait_set == NULL);

    TokaTCB *tcb_to_release = NULL;
    TokaCompletionSubscription *subscriptions_to_release = NULL;
    if (is_singleton) {
        toka_completion_subscription_collect_parent_wait_teardown_locked(
            task, 0, 0, wait_id, slot_gen, &subscriptions_to_release
        );
        if (reg->tcb) {
            atomic_store(&reg->tcb->active_wait_id, TOKA_NO_WAIT_ID);
            atomic_store(&reg->tcb->active_slot_gen, 0);
        }
        tcb_to_release = reg->tcb;
        reg->tcb = NULL;
        reg->active = 0;
        reg->in_use = 0;
        toka_wait_slot_advance_or_retire_locked(reg);
        g_wait_registry_count--;
    }

    toka_mutex_unlock(&g_rt_mutex);

    toka_completion_subscription_finish_parent_wait_teardown(
        subscriptions_to_release
    );
    int sched_ok = toka_task_try_schedule_token_internal(task, gen);
    if (tcb_to_release) {
        toka_task_release(tcb_to_release);
    }
    if (!sched_ok) return TOKA_WAKE_STALE;
    return is_singleton ? TOKA_WAKE_SINGLETON_WON : TOKA_WAKE_PAIR_WON;
}

int toka_wait_registry_try_wake(uint32_t wait_id, uint32_t slot_gen) {
    return toka_wait_registry_try_wake_checked(
        (TokaTaskToken){0}, 0, 0, wait_id, slot_gen
    );
}

int toka_wait_registry_invalidate(uint32_t wait_id, uint32_t slot_gen) {
    toka_mutex_lock(&g_rt_mutex);
    if (wait_id >= g_wait_registry_capacity) {
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }
    TokaWaitRegistration *reg = &g_wait_registry[wait_id];
    if (!reg->in_use || !reg->active ||
        reg->token.wait_slot_generation != slot_gen) {
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
    TokaWaitSet *wait_set = (TokaWaitSet*)reg->wait_set;
    int help_pending = 0;
    if (wait_set) {
        if (atomic_load_explicit(&wait_set->state, memory_order_acquire) ==
            TOKA_WAIT_SET_WON_PENDING) {
            toka_wait_set_retain_locked(wait_set);
            help_pending = 1;
        }
        if (atomic_load(&wait_set->winner_wait_id) == TOKA_WAKE_GROUP_CANCELLED) {
            toka_mutex_unlock(&g_rt_mutex);
            if (help_pending) toka_wait_set_help_commit_with_ref(wait_set);
            return 0;
        }
    }
    int won = (atomic_load(&reg->state) == TOKA_WAIT_STATE_WON);
    toka_mutex_unlock(&g_rt_mutex);
    if (help_pending) toka_wait_set_help_commit_with_ref(wait_set);
    return won;
}

int toka_wait_registry_release(uint32_t wait_id, uint32_t slot_gen) {
    TokaWaitSetCancelCleanup group_cleanup = {0};
    TokaWaitSetWinnerCleanup winner_cleanup = {0};
    int free_wait_set = 0;
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

    // A live multi-source wait has one teardown obligation. Releasing any
    // member therefore retires the whole group before its task can be woken
    // or another source can select a now-partial registration set.
    if (reg->active && reg->wait_set) {
        int released = toka_wait_set_cancel_group_and_wake_locked(
            (TokaWaitSet *)reg->wait_set, &group_cleanup, 0
        );
        toka_mutex_unlock(&g_rt_mutex);
        toka_wait_set_publish_queue_ticket(&group_cleanup);
        toka_wait_set_finish_cancel_cleanup(&group_cleanup);
        return released;
    }

    // After source selection, each physical outcome slot still owns both its
    // TCB retain and one WaitSet descriptor reference. Releasing the last
    // such slot is the sole physical destruction point; an uncommitted
    // descriptor is first helped to WonCommitted here.
    if (!reg->active && reg->wait_set) {
        TokaWaitSet *wait_set = (TokaWaitSet*)reg->wait_set;
        toka_wait_set_commit_winner_locked(wait_set, &winner_cleanup);
        TokaTCB *tcb_to_release = reg->tcb;
        reg->tcb = NULL;
        reg->wait_set = NULL;
        reg->in_use = 0;
        toka_wait_slot_advance_or_retire_locked(reg);
        uint32_t wait_set_state = atomic_load_explicit(
            &wait_set->state, memory_order_acquire
        );
        if (wait_set_state == TOKA_WAIT_SET_WON_COMMITTED ||
            wait_set_state == TOKA_WAIT_SET_INACTIVE) {
            if (toka_wait_set_drop_ref_locked(wait_set)) {
                free_wait_set = 1;
            }
        } else {
            fprintf(stderr, "Fatal error: inactive WaitSet slot lacks a committed winner.\n");
            abort();
        }
        toka_mutex_unlock(&g_rt_mutex);
        toka_wait_set_finish_winner_cleanup(&winner_cleanup);
        if (tcb_to_release) toka_task_release(tcb_to_release);
        if (free_wait_set) free(wait_set);
        return 1;
    }

    int was_active = reg->active;
    TokaCompletionSubscription *subscriptions_to_release = NULL;
    if (was_active && reg->wait_set == NULL) {
        toka_completion_subscription_collect_parent_wait_teardown_locked(
            reg->task, 0, 0, wait_id, slot_gen, &subscriptions_to_release
        );
    }
    if (reg->tcb) {
        atomic_store(&reg->tcb->active_wait_id, TOKA_NO_WAIT_ID);
        atomic_store(&reg->tcb->active_slot_gen, 0);
    }
    TokaTCB *tcb_to_release = reg->tcb;
    reg->tcb = NULL;
    reg->active = 0;
    reg->in_use = 0;
    toka_wait_slot_advance_or_retire_locked(reg);
    if (was_active) {
        g_wait_registry_count--;
    }
    toka_mutex_unlock(&g_rt_mutex);

    toka_completion_subscription_finish_parent_wait_teardown(
        subscriptions_to_release
    );
    if (tcb_to_release) {
        toka_task_release(tcb_to_release);
    }
    return 1;
}

static int toka_wait_set_cancel_group_and_wake_locked(
    TokaWaitSet *ws,
    TokaWaitSetCancelCleanup *cleanup,
    int force_cancel
) {
    if (!ws || !cleanup) return 0;
    (void)force_cancel;
    TokaTCB *linked_tcb = NULL;
    for (size_t i = 0; i < g_wait_registry_capacity; ++i) {
        TokaWaitRegistration *reg = &g_wait_registry[i];
        if (reg->in_use && reg->active && reg->wait_set == ws) {
            linked_tcb = reg->tcb;
            break;
        }
    }
    if (!linked_tcb || !toka_task_token_equals(linked_tcb->token, ws->task) ||
        atomic_load(&linked_tcb->task_schedule_generation) !=
            ws->task_schedule_generation ||
        !toka_tcb_has_active_wait_set_locked(linked_tcb, ws)) {
        return 0;
    }
    uint32_t expected_state = TOKA_WAIT_SET_WAITING;
    if (!atomic_compare_exchange_strong(&ws->state, &expected_state,
                                        TOKA_WAIT_SET_INACTIVE)) {
        return 0;
    }
    atomic_store(&ws->winner_wait_id, TOKA_WAKE_GROUP_CANCELLED);

    TokaTCB *tcb_to_wake = NULL;
    int free_wait_set = 0;
    for (size_t i = 0; i < g_wait_registry_capacity; ++i) {
        TokaWaitRegistration *reg = &g_wait_registry[i];
        if (reg->in_use && reg->active && reg->wait_set == ws) {
            atomic_store(&reg->state, TOKA_WAIT_STATE_CANCELLED);
            if (!tcb_to_wake && reg->tcb) {
                tcb_to_wake = reg->tcb;
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
            reg->active = 0;
            reg->in_use = 0;
            toka_wait_slot_advance_or_retire_locked(reg);
            g_wait_registry_count--;
            if (toka_wait_set_drop_ref_locked(ws)) {
                free_wait_set = 1;
            }
        }
    }

    if (tcb_to_wake) {
        toka_tcb_clear_active_wait_set_locked(tcb_to_wake, ws);
        uint64_t gen = atomic_load(&tcb_to_wake->task_schedule_generation);
        toka_task_prepare_queue_ticket(tcb_to_wake, gen);
        uint32_t expected_st = TOKA_TCB_SUSPENDED;
        if (atomic_compare_exchange_strong(&tcb_to_wake->state, &expected_st, TOKA_TCB_QUEUED)) {
            // The WaitSet is now logically inactive. Carry the matching
            // unpublished ticket out of this arbiter so a preempted original
            // publisher can be helped before its registration retains drop.
            cleanup->queue_publish_tcb = tcb_to_wake;
            cleanup->queue_publish_generation = gen;
        }
    }
    toka_completion_subscription_collect_parent_wait_teardown_locked(
        ws->task, ws->token.wait_set_id, ws->token.wait_set_generation,
        TOKA_NO_WAIT_ID, 0, &cleanup->subscriptions_to_release
    );
    if (!free_wait_set || ws->ref_count != 0) {
        fprintf(stderr, "Fatal error: WaitSet cancellation did not release every slot.\n");
        abort();
    }
    cleanup->wait_set_to_free = ws;
    return 1;
}

static void toka_wait_set_publish_queue_ticket(TokaWaitSetCancelCleanup *cleanup) {
    if (!cleanup) return;
    toka_completion_subscription_finish_parent_wait_teardown(
        cleanup->subscriptions_to_release
    );
    cleanup->subscriptions_to_release = NULL;
    if (cleanup->queue_publish_tcb) {
        toka_task_publish_queue_ticket(
            cleanup->queue_publish_tcb, cleanup->queue_publish_generation
        );
        cleanup->queue_publish_tcb = NULL;
    }
}

static void toka_wait_set_finish_cancel_cleanup(TokaWaitSetCancelCleanup *cleanup) {
    if (!cleanup) return;
    toka_completion_subscription_finish_parent_wait_teardown(
        cleanup->subscriptions_to_release
    );
    cleanup->subscriptions_to_release = NULL;
    for (uint32_t i = 0; i < cleanup->tcb_release_count; ++i) {
        toka_task_release(cleanup->tcb_to_release);
    }
    free(cleanup->wait_set_to_free);
}

static void toka_wait_registry_cancel_active(TokaTCB *tcb) {
    if (!tcb) return;
    if (toka_wait_registry_help_pending_for_tcb(tcb)) {
        return;
    }
    TokaWaitSetCancelCleanup cleanup = {0};
    uint32_t singleton_id = TOKA_NO_WAIT_ID;
    uint32_t singleton_gen = 0;

    toka_mutex_lock(&g_rt_mutex);
    uint32_t wid = atomic_load(&tcb->active_wait_id);
    uint32_t wgen = atomic_load(&tcb->active_slot_gen);
    if (wid != TOKA_NO_WAIT_ID && wid < g_wait_registry_capacity) {
        TokaWaitRegistration *reg = &g_wait_registry[wid];
        if (reg->in_use && reg->active &&
            reg->token.wait_slot_generation == wgen) {
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

    toka_wait_set_publish_queue_ticket(&cleanup);
    toka_wait_set_finish_cancel_cleanup(&cleanup);
    if (singleton_id != TOKA_NO_WAIT_ID) {
        toka_wait_registry_release(singleton_id, singleton_gen);
    }
}

// A terminal task cannot return to user code to consume a selected wait
// outcome. After active-wait cancellation has either removed a Waiting group
// or helped a WonPending group commit, reclaim every remaining inactive outcome
// slot that still retains this terminal TCB. This is physical retirement only:
// the winner was already fixed before the slots are discarded.
static void toka_wait_registry_reap_terminal_outcomes(TokaTCB *tcb) {
    if (!tcb) return;
    uint32_t tcb_release_count = 0;

    toka_mutex_lock(&g_rt_mutex);
    for (size_t i = 0; i < g_wait_registry_capacity; ++i) {
        TokaWaitRegistration *reg = &g_wait_registry[i];
        if (!reg->in_use || reg->active || reg->tcb != tcb ||
            !reg->wait_set) {
            continue;
        }
        TokaWaitSet *wait_set = (TokaWaitSet*)reg->wait_set;
        if (atomic_load_explicit(&wait_set->state, memory_order_acquire) !=
            TOKA_WAIT_SET_INACTIVE) {
            continue;
        }
        reg->tcb = NULL;
        reg->wait_set = NULL;
        reg->in_use = 0;
        toka_wait_slot_advance_or_retire_locked(reg);
        tcb_release_count++;
        if (toka_wait_set_drop_ref_locked(wait_set)) {
            free(wait_set);
        }
    }
    toka_mutex_unlock(&g_rt_mutex);

    for (uint32_t i = 0; i < tcb_release_count; ++i) {
        toka_task_release(tcb);
    }
}

static int toka_task_finalize_cold_cancel(TokaTCB *tcb) {
    if (!toka_tcb_try_retain_held(tcb, 1)) {
        return 0;
    }
    uint32_t expected = TOKA_TCB_CREATED;
    if (!atomic_compare_exchange_strong(&tcb->state, &expected,
                                        TOKA_TCB_COLD_FINALIZING)) {
        toka_task_release(tcb);
        return 0;
    }

    // Keep the TCB and its promise alive if a frame-local destructor re-enters
    // task code or drops the final user handle.
    const int runs_cleanup = atomic_load(&tcb->cold_cleanup_supported) &&
                             tcb->coro_frame != NULL;
    const int pinned_frame = runs_cleanup && toka_tcb_try_acquire_frame_pin(tcb);
    if (runs_cleanup && !pinned_frame) {
        fprintf(stderr, "Fatal error: cold finalizer lost its frame pin.\n");
        abort();
    }
    if (runs_cleanup) {
        destroy_coro_frame(tcb->coro_frame);
        // New CodeGen deferred physical free, so the promise remains readable
        // after the destroy callback returns and before terminal publication.
    }
    // Completion is permitted only after the cleanup callback has returned.
    // Legacy objects may not provide that callback, but still need a terminal
    // canceled state; only new handshake objects use this bit for direct frame
    // reclamation in toka_task_release.
    atomic_store_explicit(&tcb->cold_cleanup_finished, 1,
                          memory_order_release);

    if (tcb->promise) {
        toka_task_complete_canceled(tcb->promise);
    } else {
        atomic_store(&tcb->state, TOKA_TCB_COMPLETED_CANCELED);
    }
    if (pinned_frame) {
        toka_tcb_release_frame_pin(tcb);
    }
    toka_task_release(tcb);
    return 1;
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
    toka_wait_set_publish_queue_ticket(&cleanup);
    toka_wait_set_finish_cancel_cleanup(&cleanup);
    return canceled;
}

int toka_task_request_cancel(void *tcb_ptr) {
    if (!tcb_ptr) return 0;
    if (!toka_task_try_retain(tcb_ptr)) return 0;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;
    atomic_store(&tcb->cancel_requested, 1);

    TokaTCB *child_tcb = NULL;
    TokaTCB **cancel_children = NULL;
    uint32_t cancel_child_count = 0;
    TokaTaskScopeRegistry **cancel_scopes = NULL;
    uint32_t cancel_scope_count = 0;
    int waits_for_active_await_child = 0;
    int waits_for_await_resolution =
        toka_task_claim_await_cancellation(tcb);
    toka_mutex_lock(&g_rt_mutex);
    uintptr_t child_val = atomic_load(&tcb->active_child_tcb);
    if (child_val != 0) {
        child_tcb = toka_task_registry_retain_by_pointer_locked(
            (void*)child_val, 1
        );
        waits_for_active_await_child = child_tcb != NULL;
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
            toka_tcb_require_retain_held(cancel_children[i], 1,
                                         "cancellation-child snapshot");
        }
    }
    cancel_scope_count = tcb->cancel_scope_count;
    if (cancel_scope_count > 0) {
        cancel_scopes = (TokaTaskScopeRegistry**)malloc(
            cancel_scope_count * sizeof(TokaTaskScopeRegistry*)
        );
        if (!cancel_scopes) {
            toka_mutex_unlock(&g_rt_mutex);
            fprintf(stderr, "Fatal error: unable to snapshot cancellation scopes.\n");
            abort();
        }
        for (uint32_t i = 0; i < cancel_scope_count; ++i) {
            TokaTaskScopeRegistry *scope = tcb->cancel_scopes[i];
            if (!toka_task_scope_retain_locked(scope)) {
                toka_mutex_unlock(&g_rt_mutex);
                fprintf(stderr, "Fatal error: invalid TaskScope cancellation reference.\n");
                abort();
            }
            cancel_scopes[i] = scope;
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
    for (uint32_t i = 0; i < cancel_scope_count; ++i) {
        toka_task_scope_begin_close(cancel_scopes[i]);
        toka_task_scope_request_cancel_all(cancel_scopes[i]);
        toka_task_scope_release(cancel_scopes[i]);
    }
    free(cancel_scopes);

    toka_mutex_lock(&g_rt_mutex);
    if (atomic_load(&tcb->state) == TOKA_TCB_CREATED) {
        toka_mutex_unlock(&g_rt_mutex);
        toka_task_finalize_cold_cancel(tcb);
        toka_task_release(tcb);
        return 1;
    }
    uint32_t expected_st = TOKA_TCB_CREATED;
    if (!tcb->promise && atomic_compare_exchange_strong(
                             &tcb->state, &expected_st,
                             TOKA_TCB_COMPLETED_CANCELED)) {
        toka_mutex_unlock(&g_rt_mutex);
        toka_task_try_release_owner(tcb);
        toka_task_release(tcb);
        return 1;
    }
    uint32_t wid = atomic_load(&tcb->active_wait_id);
    uint32_t wgen = atomic_load(&tcb->active_slot_gen);
    uint32_t st = atomic_load(&tcb->state);
    // A direct await is represented by the child promise continuation rather
    // than a WaitSet. Once cancellation has observed that live child, it must
    // not enqueue the parent ahead of the child's terminal publication: the
    // continuation is the one edge that closes the await link and makes the
    // child's terminal/result state observable to the parent. A child that is
    // already terminal will complete that continuation; if it won just before
    // this branch, the parent is already queued or running instead.
    if ((waits_for_active_await_child || waits_for_await_resolution) &&
        st == TOKA_TCB_SUSPENDED &&
        wid == TOKA_NO_WAIT_ID) {
        toka_mutex_unlock(&g_rt_mutex);
        toka_task_release(tcb);
        return 1;
    }
    if (wid != TOKA_NO_WAIT_ID && wid < g_wait_registry_capacity) {
        TokaWaitRegistration *reg = &g_wait_registry[wid];
        if (reg->in_use && reg->active && reg->wait_set) {
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
            toka_wait_set_publish_queue_ticket(&cleanup);
            toka_wait_set_finish_cancel_cleanup(&cleanup);
            toka_task_release(tcb);
            return 1;
        }
    }
    if (st == TOKA_TCB_SUSPENDED) {
        if (wid != TOKA_NO_WAIT_ID && wid < g_wait_registry_capacity) {
            toka_mutex_unlock(&g_rt_mutex);
            toka_wait_registry_try_wake(wid, wgen);
        } else {
            toka_mutex_unlock(&g_rt_mutex);
            uint64_t gen = atomic_load(&tcb->task_schedule_generation);
            if (toka_task_claim_queue_ticket(tcb, TOKA_TCB_SUSPENDED, gen)) {
                toka_task_publish_queue_ticket(tcb, gen);
            }
        }
        toka_task_release(tcb);
        return 1;
    }
    if (st == TOKA_TCB_PREPARING) {
        uint32_t expected = TOKA_TCB_PREPARING;
        atomic_compare_exchange_strong(&tcb->state, &expected, TOKA_TCB_PREPARING_WITH_PENDING_WAKE);
    }
    toka_mutex_unlock(&g_rt_mutex);
    toka_task_release(tcb);
    return 1;
}

int toka_task_is_cancel_requested(void *tcb_ptr) {
    if (!tcb_ptr) return 0;
    if (!toka_task_try_retain(tcb_ptr)) return 0;
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
    toka_task_release(tcb);
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

int toka_task_mark_current_cancellation_handled(void *coro_frame) {
    TokaTCB *tcb = lookup_tcb_by_frame_retained(coro_frame);
    if (!tcb) return 0;
    int was_requested = atomic_load(&tcb->cancel_requested) != 0;
    if (was_requested) {
        atomic_store(&tcb->cancel_handled, 1);
    }
    toka_task_release(tcb);
    return was_requested;
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
                    (unsigned long long)reg->task.task_id,
                    reg->tcb ? atomic_load(&reg->tcb->state) : 999u,
                    reg->wait_set);
        }
    }
    toka_mutex_unlock(&g_rt_mutex);
}

int toka_task_subscribe_completion(void *tcb_ptr, uint32_t wait_id, uint32_t slot_gen) {
    if (!tcb_ptr) return 0;
    TokaTCB *tcb = NULL;
    TokaCompletionSubscription *subscription = NULL;
    TokaTaskToken parent = {0};
    uint64_t wait_set_id = 0;
    uint64_t wait_set_generation = 0;

    toka_mutex_lock(&g_rt_mutex);
    tcb = toka_task_registry_retain_by_pointer_locked(tcb_ptr, 1);
    if (!tcb || wait_id >= g_wait_registry_capacity) {
        toka_mutex_unlock(&g_rt_mutex);
        if (tcb) toka_task_release(tcb);
        return 0;
    }

    TokaWaitRegistration *registration = &g_wait_registry[wait_id];
    if (!registration->in_use || !registration->active ||
        registration->token.wait_slot_generation != slot_gen ||
        atomic_load_explicit(&registration->state, memory_order_acquire) !=
            TOKA_WAIT_STATE_WAITING) {
        toka_mutex_unlock(&g_rt_mutex);
        toka_task_release(tcb);
        return 0;
    }
    parent = registration->task;
    if (registration->wait_set) {
        TokaWaitSet *wait_set = (TokaWaitSet*)registration->wait_set;
        wait_set_id = wait_set->token.wait_set_id;
        wait_set_generation = wait_set->token.wait_set_generation;
    }

    uint32_t st = atomic_load(&tcb->state);
    if (st == TOKA_TCB_COMPLETED || st == TOKA_TCB_COMPLETED_CANCELED) {
        toka_mutex_unlock(&g_rt_mutex);
        toka_wait_registry_try_wake_checked(
            parent, wait_set_id, wait_set_generation, wait_id, slot_gen
        );
        toka_task_release(tcb);
        return 1;
    }

    // Repeated arm requests for the same child and exact parent wait are
    // idempotent. They must not create a second publisher that can attempt
    // the same ChildTerminal group transition after the first has selected it.
    for (uint32_t i = 0; i < tcb->subscriber_count; ++i) {
        TokaCompletionSubscription *existing = tcb->subscribers[i];
        if (existing && existing->wait_id == wait_id &&
            existing->slot_gen == slot_gen &&
            toka_task_token_equals(existing->child, tcb->token) &&
            toka_task_token_equals(existing->parent, parent) &&
            atomic_load_explicit(&existing->state, memory_order_acquire) ==
                TOKA_COMPLETION_SUB_ACTIVE) {
            toka_mutex_unlock(&g_rt_mutex);
            toka_task_release(tcb);
            return 1;
        }
    }

    subscription = (TokaCompletionSubscription*)calloc(1, sizeof(*subscription));
    if (!subscription) {
        toka_mutex_unlock(&g_rt_mutex);
        toka_task_release(tcb);
        return 0;
    }
    if (tcb->subscriber_count >= tcb->subscriber_capacity) {
        if (tcb->subscriber_capacity > UINT32_MAX / 2 ||
            (size_t)(tcb->subscriber_capacity == 0 ? 4 :
                     tcb->subscriber_capacity * 2) >
                SIZE_MAX / sizeof(TokaCompletionSubscription*)) {
            toka_mutex_unlock(&g_rt_mutex);
            free(subscription);
            toka_task_release(tcb);
            return 0;
        }
        uint32_t new_cap = tcb->subscriber_capacity == 0 ? 4 : tcb->subscriber_capacity * 2;
        TokaCompletionSubscription **new_subs =
            (TokaCompletionSubscription**)realloc(
                tcb->subscribers, new_cap * sizeof(TokaCompletionSubscription*)
            );
        if (!new_subs) {
            toka_mutex_unlock(&g_rt_mutex);
            free(subscription);
            toka_task_release(tcb);
            return 0;
        }
        tcb->subscribers = new_subs;
        tcb->subscriber_capacity = new_cap;
    }

    subscription->child = tcb->token;
    subscription->parent = parent;
    subscription->wait_id = wait_id;
    subscription->slot_gen = slot_gen;
    subscription->wait_set_id = wait_set_id;
    subscription->wait_set_generation = wait_set_generation;
    atomic_store_explicit(&subscription->state, TOKA_COMPLETION_SUB_ACTIVE,
                          memory_order_release);
    // The checked retain acquired above transfers to this descriptor. It
    // prevents child storage reclamation until its unique publish/unsubscribe
    // commit has made the descriptor inactive.
    subscription->child_tcb = tcb;
    tcb->subscribers[tcb->subscriber_count++] = subscription;
    toka_mutex_unlock(&g_rt_mutex);
    return 1;
}

int toka_task_unsubscribe_completion(void *tcb_ptr, uint32_t wait_id, uint32_t slot_gen) {
    if (!tcb_ptr) return 0;
    TokaTCB *tcb = NULL;
    TokaCompletionSubscription *subscription = NULL;

    toka_mutex_lock(&g_rt_mutex);
    tcb = toka_task_registry_retain_by_pointer_locked(tcb_ptr, 1);
    if (!tcb) {
        toka_mutex_unlock(&g_rt_mutex);
        return 0;
    }
    uint32_t state = atomic_load_explicit(&tcb->state, memory_order_acquire);
    if (state == TOKA_TCB_COMPLETED || state == TOKA_TCB_COMPLETED_CANCELED) {
        toka_mutex_unlock(&g_rt_mutex);
        toka_task_release(tcb);
        return 0;
    }
    for (uint32_t i = 0; i < tcb->subscriber_count; i++) {
        TokaCompletionSubscription *candidate = tcb->subscribers[i];
        if (!candidate || candidate->wait_id != wait_id ||
            candidate->slot_gen != slot_gen) {
            continue;
        }
        uint8_t expected = TOKA_COMPLETION_SUB_ACTIVE;
        if (!atomic_compare_exchange_strong_explicit(
                &candidate->state, &expected,
                TOKA_COMPLETION_SUB_SELECTED_UNSUBSCRIBER,
                memory_order_acq_rel, memory_order_acquire)) {
            break;
        }
        tcb->subscribers[i] =
            tcb->subscribers[tcb->subscriber_count - 1];
        tcb->subscriber_count--;
        if (tcb->subscriber_count == 0) {
            free(tcb->subscribers);
            tcb->subscribers = NULL;
            tcb->subscriber_capacity = 0;
        }
        subscription = candidate;
        break;
    }
    toka_mutex_unlock(&g_rt_mutex);
    toka_task_release(tcb);
    if (!subscription) return 0;
    toka_completion_subscription_unsubscribe(subscription);
    return 1;
}

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
    if (expected_key != 0 && g_reactor_fd_table[fd].read_key != expected_key) {
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
    if (expected_key != 0 && g_reactor_fd_table[fd].write_key != expected_key) {
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
        if (fd >= 0 && fd < 65536) {
            int is_eof = (events_buf[i].flags & (EV_EOF | EV_ERROR)) != 0;
            if (is_eof) {
                if (g_reactor_fd_table[fd].read_key != 0) {
                    if (out_count < max_events) {
                        out_keys[out_count++] = g_reactor_fd_table[fd].read_key;
                    }
                    g_reactor_fd_table[fd].read_key = 0;
                }
                if (g_reactor_fd_table[fd].write_key != 0) {
                    if (out_count < max_events) {
                        out_keys[out_count++] = g_reactor_fd_table[fd].write_key;
                    }
                    g_reactor_fd_table[fd].write_key = 0;
                }
            } else if (events_buf[i].filter == EVFILT_READ) {
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

int toka_rt_test_reactor_is_fd_registered(int fd) {
    if (fd < 0 || fd >= 65536) return 0;
    toka_mutex_lock(&g_rt_mutex);
    int registered = (g_reactor_fd_table[fd].read_key != 0 ||
                      g_reactor_fd_table[fd].write_key != 0);
    toka_mutex_unlock(&g_rt_mutex);
    return registered;
}

uint32_t toka_rt_test_reactor_live_key_count(void) {
    toka_mutex_lock(&g_rt_mutex);
    uint32_t cnt = 0;
    for (size_t i = 0; i < 65536; ++i) {
        if (g_reactor_fd_table[i].read_key != 0) cnt++;
        if (g_reactor_fd_table[i].write_key != 0) cnt++;
    }
    toka_mutex_unlock(&g_rt_mutex);
    return cnt;
}

static int toka_task_matches_token_or_active_child_locked(TokaTCB *root_tcb, TokaTCB *leaf_tcb, TokaTaskToken target_token) {
    TokaTCB *curr = root_tcb;
    while (curr) {
        if (toka_task_token_equals(curr->token, target_token) || curr == leaf_tcb) {
            return 1;
        }
        curr = (TokaTCB*)atomic_load(&curr->active_child_tcb);
    }
    curr = leaf_tcb;
    while (curr) {
        if (curr == root_tcb || toka_task_token_equals(curr->token, root_tcb->token)) {
            return 1;
        }
        curr = (TokaTCB*)atomic_load(&curr->parent_tcb);
    }
    return 0;
}

// TEST-ONLY PROBE: Unstable ABI, not part of the public Toka runtime contract.
// Strictly intended for ecosystem qualification test witnesses (e.g. pool cancellation barrier)
// to verify task suspension without race conditions. May be changed or removed without notice.
int toka_rt_test_task_has_active_timer_wait(void *tcb_ptr) {
    if (!tcb_ptr) return 0;
    if (!toka_task_try_retain(tcb_ptr)) return 0;
    TokaTCB *tcb = (TokaTCB*)tcb_ptr;
    int has_timer = 0;
    toka_mutex_lock(&g_rt_mutex);
    if (g_wait_registry != NULL) {
        for (size_t i = 0; i < g_wait_registry_capacity; ++i) {
            TokaWaitRegistration *reg = &g_wait_registry[i];
            if (reg->in_use && reg->active && reg->source_tag == TOKA_WAIT_SOURCE_TIMER) {
                if (atomic_load(&reg->state) == TOKA_WAIT_STATE_WAITING &&
                    toka_task_matches_token_or_active_child_locked(tcb, reg->tcb, reg->task)) {
                    has_timer = 1;
                    break;
                }
            }
        }
    }
    toka_mutex_unlock(&g_rt_mutex);
    toka_task_release(tcb);
    return has_timer;
}

#ifndef __linux__
void toka_linux_epoll_del_fd(int epfd, int fd) {}
void toka_linux_epoll_del_read(int epfd, int fd, uint64_t expected_key) {}
void toka_linux_epoll_del_write(int epfd, int fd, uint64_t expected_key) {}
#endif

int32_t toka_rt_bind_with_diag(int32_t fd, uint32_t ip, uint16_t port, int32_t *out_errno, uint8_t *out_sockaddr) {
#ifdef __wasi__
    if (out_errno) *out_errno = 0;
    return -1;
#elif defined(_WIN32)
    toka_ensure_wsa_initialized();
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = (unsigned long)ip;

    if (out_sockaddr) {
        memcpy(out_sockaddr, &addr, sizeof(addr));
    }

    int res = bind((SOCKET)fd, (struct sockaddr*)&addr, (int)sizeof(addr));
    int err = (res != 0) ? WSAGetLastError() : 0;
    if (out_errno) {
        *out_errno = (int32_t)err;
    }
    return (int32_t)res;
#else
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
#ifdef __APPLE__
    addr.sin_len = sizeof(addr);
#endif
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = (in_addr_t)ip;

    if (out_sockaddr) {
        memcpy(out_sockaddr, &addr, sizeof(addr));
    }

    errno = 0;
    int res = bind((int)fd, (struct sockaddr*)&addr, (socklen_t)sizeof(addr));
    int err = (res != 0) ? errno : 0;
    if (out_errno) {
        *out_errno = (int32_t)err;
    }
    errno = err;
    return (int32_t)res;
#endif
}

int32_t toka_rt_listen_with_diag(int32_t fd, int32_t backlog, int32_t *out_errno) {
#ifdef __wasi__
    if (out_errno) *out_errno = 0;
    return -1;
#elif defined(_WIN32)
    toka_ensure_wsa_initialized();
    int res = listen((SOCKET)fd, (int)backlog);
    int err = (res != 0) ? WSAGetLastError() : 0;
    if (out_errno) {
        *out_errno = (int32_t)err;
    }
    return (int32_t)res;
#else
    errno = 0;
    int res = listen((int)fd, (int)backlog);
    int err = (res != 0) ? errno : 0;
    if (out_errno) {
        *out_errno = (int32_t)err;
    }
    errno = err;
    return (int32_t)res;
#endif
}

int32_t toka_rt_local_port_with_diag(int32_t fd, int32_t *out_errno) {
#ifdef __wasi__
    if (out_errno) *out_errno = 0;
    return -1;
#elif defined(_WIN32)
    toka_ensure_wsa_initialized();
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    int len = sizeof(addr);
    int res = getsockname((SOCKET)fd, (struct sockaddr*)&addr, &len);
    if (res != 0) {
        if (out_errno) *out_errno = (int32_t)WSAGetLastError();
        return -1;
    }
    if (out_errno) *out_errno = 0;
    return (int32_t)ntohs(addr.sin_port);
#else
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    socklen_t len = sizeof(addr);
    errno = 0;
    int res = getsockname((int)fd, (struct sockaddr*)&addr, &len);
    if (res != 0) {
        if (out_errno) *out_errno = (int32_t)errno;
        return -1;
    }
    if (out_errno) *out_errno = 0;
    return (int32_t)ntohs(addr.sin_port);
#endif
}

int32_t toka_rt_local_addr_with_diag(int32_t fd, uint32_t *out_ip, uint16_t *out_port, int32_t *out_errno) {
#ifdef __wasi__
    if (out_errno) *out_errno = 0;
    return -1;
#elif defined(_WIN32)
    toka_ensure_wsa_initialized();
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    int len = sizeof(addr);
    int res = getsockname((SOCKET)fd, (struct sockaddr*)&addr, &len);
    if (res != 0) {
        if (out_errno) *out_errno = (int32_t)WSAGetLastError();
        return -1;
    }
    if (out_ip) *out_ip = (uint32_t)addr.sin_addr.s_addr;
    if (out_port) *out_port = ntohs(addr.sin_port);
    if (out_errno) *out_errno = 0;
    return 0;
#else
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    socklen_t len = sizeof(addr);
    errno = 0;
    int res = getsockname((int)fd, (struct sockaddr*)&addr, &len);
    if (res != 0) {
        if (out_errno) *out_errno = (int32_t)errno;
        return -1;
    }
    if (out_ip) *out_ip = (uint32_t)addr.sin_addr.s_addr;
    if (out_port) *out_port = ntohs(addr.sin_port);
    if (out_errno) *out_errno = 0;
    return 0;
#endif
}

int32_t toka_rt_connect_with_diag(int32_t fd, uint32_t ip, uint16_t port, int32_t *out_errno) {
#ifdef __wasi__
    if (out_errno) *out_errno = 0;
    return -1;
#elif defined(_WIN32)
    toka_ensure_wsa_initialized();
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = (unsigned long)ip;

    int res = connect((SOCKET)fd, (struct sockaddr*)&addr, (int)sizeof(addr));
    int err = (res != 0) ? WSAGetLastError() : 0;
    if (out_errno) {
        *out_errno = (int32_t)err;
    }
    return (int32_t)res;
#else
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
#ifdef __APPLE__
    addr.sin_len = sizeof(addr);
#endif
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = (in_addr_t)ip;

    errno = 0;
    int res = connect((int)fd, (struct sockaddr*)&addr, (socklen_t)sizeof(addr));
    int err = (res != 0) ? errno : 0;
    if (out_errno) {
        *out_errno = (int32_t)err;
    }
    errno = err;
    return (int32_t)res;
#endif
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

#ifdef TOKA_HAS_OPENSSL
int toka_tls_backend_available(void) { return 1; }

typedef struct {
    SSL_CTX *ctx;
    SSL *ssl;
    int fd;
    int is_server;
    int verify_peer;
    char sni_host[256];
} TokaTlsSession;

void* toka_tls_context_new(void) {
    static int initialized = 0;
    if (!initialized) {
#ifndef _WIN32
        signal(SIGPIPE, SIG_IGN);
#endif
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        initialized = 1;
    }

    const SSL_METHOD *method = TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx) return NULL;

    SSL_CTX_set_default_verify_paths(ctx);

    TokaTlsSession *s = (TokaTlsSession*)calloc(1, sizeof(TokaTlsSession));
    if (!s) {
        SSL_CTX_free(ctx);
        return NULL;
    }
    s->ctx = ctx;
    s->fd = -1;
    s->is_server = 0;
    s->verify_peer = 1;
    return (void*)s;
}

void* toka_tls_server_context_new(void) {
    static int initialized = 0;
    if (!initialized) {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        initialized = 1;
    }

    const SSL_METHOD *method = TLS_server_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx) return NULL;

    EVP_PKEY *pkey = NULL;
    RSA *rsa = RSA_generate_key(2048, RSA_F4, NULL, NULL);
    pkey = EVP_PKEY_new();
    EVP_PKEY_assign_RSA(pkey, rsa);

    X509 *x509 = X509_new();
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), 31536000L);
    X509_set_pubkey(x509, pkey);

    X509_NAME *name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (const unsigned char*)"US", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (const unsigned char*)"Toka", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char*)"localhost", -1, -1, 0);
    X509_set_issuer_name(x509, name);

    X509V3_CTX ctx_v3;
    X509V3_set_ctx(&ctx_v3, x509, x509, NULL, NULL, 0);
    X509_EXTENSION *ext = X509V3_EXT_conf_nid(NULL, &ctx_v3, NID_subject_alt_name, "DNS:localhost,IP:127.0.0.1");
    if (ext) {
        X509_add_ext(x509, ext, -1);
        X509_EXTENSION_free(ext);
    }

    X509_sign(x509, pkey, EVP_sha256());

    SSL_CTX_use_certificate(ctx, x509);
    SSL_CTX_use_PrivateKey(ctx, pkey);

    X509_free(x509);
    EVP_PKEY_free(pkey);

    TokaTlsSession *s = (TokaTlsSession*)calloc(1, sizeof(TokaTlsSession));
    if (!s) {
        SSL_CTX_free(ctx);
        return NULL;
    }
    s->ctx = ctx;
    s->fd = -1;
    s->is_server = 1;
    return (void*)s;
}

void* toka_tls_server_context_new_with_cert_file(const char *cert_path, const char *key_path) {
    if (!cert_path || !key_path) return NULL;
    static int initialized = 0;
    if (!initialized) {
#ifndef _WIN32
        signal(SIGPIPE, SIG_IGN);
#endif
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        initialized = 1;
    }

    const SSL_METHOD *method = TLS_server_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx) return NULL;

    if (SSL_CTX_use_certificate_chain_file(ctx, cert_path) <= 0) {
        if (SSL_CTX_use_certificate_file(ctx, cert_path, SSL_FILETYPE_PEM) <= 0) {
            SSL_CTX_free(ctx);
            return NULL;
        }
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM) <= 0) {
        SSL_CTX_free(ctx);
        return NULL;
    }

    if (!SSL_CTX_check_private_key(ctx)) {
        SSL_CTX_free(ctx);
        return NULL;
    }

    TokaTlsSession *s = (TokaTlsSession*)calloc(1, sizeof(TokaTlsSession));
    if (!s) {
        SSL_CTX_free(ctx);
        return NULL;
    }
    s->ctx = ctx;
    s->fd = -1;
    s->is_server = 1;
    return (void*)s;
}

static void ensure_parent_dir_exists(const char *path) {
    if (!path) return;
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *p = strrchr(tmp, '/');
    if (!p) p = strrchr(tmp, '\\');
    if (p) {
        *p = '\0';
#ifdef _WIN32
        CreateDirectoryA(tmp, NULL);
#else
        mkdir(tmp, 0755);
#endif
    }
}

static int generate_test_cert_pair(const char *cert_path, const char *key_path);

int toka_ensure_test_cert_files(const char *cert_path, const char *key_path) {
    if (!cert_path || !key_path) return -1;
    FILE *f_cert = fopen(cert_path, "rb");
    FILE *f_key = fopen(key_path, "rb");
    if (f_cert && f_key) {
        fclose(f_cert);
        fclose(f_key);
        return 0;
    }
    if (f_cert) fclose(f_cert);
    if (f_key) fclose(f_key);

    ensure_parent_dir_exists(cert_path);
    ensure_parent_dir_exists(key_path);
    return generate_test_cert_pair(cert_path, key_path);
}

static int generate_test_cert_pair(const char *cert_path, const char *key_path) {
    EVP_PKEY *pkey = NULL;
    RSA *rsa = RSA_generate_key(2048, RSA_F4, NULL, NULL);
    if (!rsa) return -1;
    pkey = EVP_PKEY_new();
    if (!pkey) { RSA_free(rsa); return -1; }
    EVP_PKEY_assign_RSA(pkey, rsa);

    X509 *x509 = X509_new();
    if (!x509) { EVP_PKEY_free(pkey); return -1; }
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), 31536000L);
    X509_set_pubkey(x509, pkey);

    X509_NAME *name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (const unsigned char*)"US", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char*)"localhost", -1, -1, 0);
    X509_set_issuer_name(x509, name);

    X509V3_CTX ctx_v3;
    X509V3_set_ctx(&ctx_v3, x509, x509, NULL, NULL, 0);
    X509_EXTENSION *ext_ca = X509V3_EXT_conf_nid(NULL, &ctx_v3, NID_basic_constraints, "critical,CA:TRUE");
    if (ext_ca) {
        X509_add_ext(x509, ext_ca, -1);
        X509_EXTENSION_free(ext_ca);
    }
    X509_EXTENSION *ext = X509V3_EXT_conf_nid(NULL, &ctx_v3, NID_subject_alt_name, "DNS:localhost,IP:127.0.0.1");
    if (ext) {
        X509_add_ext(x509, ext, -1);
        X509_EXTENSION_free(ext);
    }
    X509_sign(x509, pkey, EVP_sha256());

    int status = 0;
    FILE *out_cert = fopen(cert_path, "wb");
    if (!out_cert || PEM_write_X509(out_cert, x509) <= 0) {
        status = -1;
    }
    if (out_cert) fclose(out_cert);

    FILE *out_key = fopen(key_path, "wb");
    if (!out_key || PEM_write_PrivateKey(out_key, pkey, NULL, NULL, 0, NULL, NULL) <= 0) {
        status = -1;
    }
    if (out_key) fclose(out_key);

    X509_free(x509);
    EVP_PKEY_free(pkey);
    return status;
}

void toka_tls_set_verify_mode(void *handle, int verify_peer) {
    if (!handle) return;
    TokaTlsSession *s = (TokaTlsSession*)handle;
    s->verify_peer = verify_peer;
}

int toka_tls_set_sni(void *handle, const char *host) {
    if (!handle || !host) return -1;
    TokaTlsSession *s = (TokaTlsSession*)handle;
    snprintf(s->sni_host, sizeof(s->sni_host), "%s", host);
    return 0;
}

int toka_tls_set_ca_file(void *handle, const char *ca_path) {
    if (!handle || !ca_path) return -1;
    TokaTlsSession *s = (TokaTlsSession*)handle;
    if (!s->ctx) return -1;
    if (SSL_CTX_load_verify_locations(s->ctx, ca_path, NULL) <= 0) {
        return -1;
    }
    return 0;
}

int toka_tls_connect(void *handle, int fd) {
    if (!handle || fd < 0) return -1;
    TokaTlsSession *s = (TokaTlsSession*)handle;
    s->fd = fd;

    if (!s->ssl) {
        s->ssl = SSL_new(s->ctx);
        if (!s->ssl) return -1;

        SSL_set_fd(s->ssl, fd);

        if (!s->verify_peer) {
            SSL_set_verify(s->ssl, SSL_VERIFY_NONE, NULL);
        } else {
            SSL_set_verify(s->ssl, SSL_VERIFY_PEER, NULL);
        }

        if (s->sni_host[0] != '\0') {
            SSL_set_tlsext_host_name(s->ssl, s->sni_host);
            X509_VERIFY_PARAM *param = SSL_get0_param(s->ssl);
            X509_VERIFY_PARAM_set1_host(param, s->sni_host, 0);
        }

        if (!s->is_server) {
            SSL_set_connect_state(s->ssl);
        }
    }

    int res = SSL_connect(s->ssl);
    if (res == 1) return 0;
    int err = SSL_get_error(s->ssl, res);
    if (err == SSL_ERROR_WANT_READ) return 1;
    if (err == SSL_ERROR_WANT_WRITE) return 2;
    ERR_clear_error();
    return -1;
}

int toka_tls_accept(void *handle, int fd) {
    if (!handle || fd < 0) return -1;
    TokaTlsSession *s = (TokaTlsSession*)handle;
    s->fd = fd;
    if (!s->ssl) {
        s->ssl = SSL_new(s->ctx);
        if (!s->ssl) return -1;
        SSL_set_fd(s->ssl, fd);
        if (s->is_server) {
            SSL_set_accept_state(s->ssl);
        }
    }

    int res = SSL_accept(s->ssl);
    if (res == 1) return 0;
    int err = SSL_get_error(s->ssl, res);
    if (err == SSL_ERROR_WANT_READ) return 1;
    if (err == SSL_ERROR_WANT_WRITE) return 2;
    ERR_clear_error();
    return -1;
}

int toka_tls_read(void *handle, void *buf, size_t len) {
    if (!handle || (!buf && len != 0) || len > (size_t)INT_MAX) return -3;
    TokaTlsSession *s = (TokaTlsSession*)handle;
    if (!s->ssl) return -3;

    int res = SSL_read(s->ssl, buf, (int)len);
    if (res > 0) return res;
    int err = SSL_get_error(s->ssl, res);
    if (err == SSL_ERROR_WANT_READ) return -1;
    if (err == SSL_ERROR_WANT_WRITE) return -2;
    if (err == SSL_ERROR_ZERO_RETURN) return 0;
    return -3;
}

int toka_tls_write(void *handle, const void *buf, size_t len) {
    if (!handle || (!buf && len != 0) || len > (size_t)INT_MAX) return -3;
    TokaTlsSession *s = (TokaTlsSession*)handle;
    if (!s->ssl) return -3;

    int res = SSL_write(s->ssl, buf, (int)len);
    if (res > 0) return res;
    int err = SSL_get_error(s->ssl, res);
    if (err == SSL_ERROR_WANT_READ) return -1;
    if (err == SSL_ERROR_WANT_WRITE) return -2;
    return -3;
}

void toka_tls_close(void *handle) {
    if (!handle) return;
    TokaTlsSession *s = (TokaTlsSession*)handle;
    ERR_clear_error();
    if (s->ssl) {
        SSL_free(s->ssl);
        s->ssl = NULL;
    }
    if (s->ctx) {
        SSL_CTX_free(s->ctx);
        s->ctx = NULL;
    }
    free(s);
    ERR_clear_error();
}
#else
// Keep the TLS C ABI available for non-TLS programs.  The std layer can link
// against the runtime without OpenSSL; TLS callers receive a deterministic
// unsupported-backend result instead of a link or loader failure.
int toka_tls_backend_available(void) { return 0; }
void* toka_tls_context_new(void) { return NULL; }
void* toka_tls_server_context_new(void) { return NULL; }
void* toka_tls_server_context_new_with_cert_file(const char *cert_path, const char *key_path) {
    (void)cert_path;
    (void)key_path;
    return NULL;
}
void toka_tls_set_verify_mode(void *handle, int verify_peer) {
    (void)handle;
    (void)verify_peer;
}
int toka_tls_set_ca_file(void *handle, const char *ca_path) {
    (void)handle;
    (void)ca_path;
    return -4;
}
int toka_tls_set_sni(void *handle, const char *host) {
    (void)handle;
    (void)host;
    return -4;
}
int toka_tls_connect(void *handle, int fd) {
    (void)handle;
    (void)fd;
    return -4;
}
int toka_tls_accept(void *handle, int fd) {
    (void)handle;
    (void)fd;
    return -4;
}
int toka_tls_read(void *handle, void *buf, size_t len) {
    (void)handle;
    (void)buf;
    (void)len;
    return -4;
}
int toka_tls_write(void *handle, const void *buf, size_t len) {
    (void)handle;
    (void)buf;
    (void)len;
    return -4;
}
void toka_tls_close(void *handle) { (void)handle; }
int toka_ensure_test_cert_files(const char *cert_path, const char *key_path) {
    (void)cert_path;
    (void)key_path;
    return -4;
}
#endif
