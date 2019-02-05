#ifndef __GX_IO_H__
#define __GX_IO_H__

#include "platform.h"

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

inline void fd_close(fd_t fd) noexcept {
	if (!fd_valid(fd)) {
		return;
	}
#ifdef GX_PLATFORM_WIN32
	closesocket(fd);
#else
	while (1) {
		if (gx_likely(!::close(fd))) {
			return;
		}
		if (errno == EINTR) {
			continue;
		}
		return;
	}
#endif
}

inline void fd_block(fd_t fd, bool value) noexcept {
#ifdef GX_PLATFORM_WIN32
	u_long mode = value ? 0 : 1;
	ioctlsocket(fd, FIONBIO, &mode);
#else 
	int n;
	n = ::fcntl(fd, F_GETFL);
	if (n < 0) {
		return;
	}

	if (value) {
		n &= ~O_NONBLOCK;
	}
	else {
		n |= O_NONBLOCK;
	}

	::fcntl(fd, F_SETFL, n);
#endif
}

inline void fd_nodelay(fd_t fd, bool value) noexcept{
#ifdef GX_PLATFORM_WIN32
	int nodelay = value ? 1 : 0;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
#else
	int nodelay = value ? 1 : 0;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
#endif
}


class IO {
public:
    virtual int read(void *buf, size_t size) noexcept = 0;
    virtual int write(const char *buf, size_t size) noexcept = 0;
    virtual void close() noexcept = 0;
};

GX_NS_END

#endif

