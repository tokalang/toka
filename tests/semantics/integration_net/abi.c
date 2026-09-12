#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stddef.h>
#include <string.h>
_Static_assert(sizeof(struct sockaddr_in) == 16, "Darwin IPv4 ABI");
_Static_assert(offsetof(struct sockaddr_in, sin_port) == 2, "port offset");
_Static_assert(offsetof(struct sockaddr_in, sin_addr) == 4, "address offset");
static int calls;
ssize_t sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *addr, socklen_t size) {
    const struct sockaddr_in *v4 = (const struct sockaddr_in *)addr;
    if (fd != 17 || flags || len != 4 || memcmp(buf, "ping", 4) || size != 16 ||
        v4->sin_len != 16 || v4->sin_family != AF_INET || ntohs(v4->sin_port) != 43210 ||
        v4->sin_addr.s_addr != htonl(0x7f000001)) return -1;
    for (int i = 0; i < 8; ++i) if (v4->sin_zero[i]) return -1;
    ++calls;
    return 4;
}
int getsockopt(int fd, int level, int option, void *value, socklen_t *size) {
    if (fd < 0) return -1;
    if (level != SOL_SOCKET || option != SO_ERROR || *size != sizeof(int)) return -1;
    *(int *)value = 42;
    *size = sizeof(int);
    ++calls;
    return 0;
}
int setsockopt(int fd, int level, int option, const void *value, socklen_t size) {
    if (fd != 17 || level != SOL_SOCKET || option != SO_REUSEADDR || size != sizeof(int) || *(const int *)value != 1) return -1;
    ++calls;
    return 0;
}
int network_abi_calls(void) { return calls; }
