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




