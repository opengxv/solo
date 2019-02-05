#ifndef __GX_IO_H__
#define __GX_IO_H__

#include "platform.h"

#ifndef GX_PLATFORM_WIN32
    #include <unistd.h>
#endif

GX_NS_BEGIN

#ifdef GX_PLATFORM_WIN32
    #define GX_FD_INVALID_VALUE INVALID_SOCKET
    typedef SOCKET fd_t;
#else
    #define GX_FD_INVALID_VALUE -1
    typedef int fd_t;
#endif

inline bool fd_valid(fd_t fd) noexcept {
	return fd != GX_FD_INVALID_VALUE;
}

inline int fd_read(fd_t fd, char *buf, size_t size) noexcept {
#ifdef GX_PLATFORM_WIN32
	return ::recv(fd, buf, size, 0);
#else
	return ::read(fd, buf, size);
#endif
}

inline int fd_write(fd_t fd, const char *buf, size_t size) noexcept {
#ifdef GX_PLATFORM_WIN32
	return ::send(fd, buf, size, 0);
#else
	return ::write(fd, buf, size);
#endif
}

void fd_close(fd_t fd) noexcept;
void fd_block(fd_t fd, bool value) noexcept;
void fd_nodelay(fd_t fd, bool value) noexcept;

class IO {
public:
    virtual int read(void *buf, size_t size) noexcept = 0;
    virtual int write(const char *buf, size_t size) noexcept = 0;
    virtual void close() noexcept = 0;
};

GX_NS_END

#endif

