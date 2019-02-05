#include "io.h"

#ifndef GX_PLATFORM_WIN32
    #include <fcntl.h>
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
#endif

GX_NS_BEGIN

void fd_close(fd_t fd) noexcept {
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

void fd_block(fd_t fd, bool value) noexcept {
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

void fd_nodelay(fd_t fd, bool value) noexcept {
#ifdef GX_PLATFORM_WIN32
	int nodelay = value ? 1 : 0;
	setsockopt(fd, IPPROTO_TCP, O_NDELAY, (const char*)&nodelay, sizeof(nodelay));
#else
	int nodelay = value ? 1 : 0;
	setsockopt(fd, IPPROTO_TCP, O_NDELAY, &nodelay, sizeof(nodelay));
#endif
}


GX_NS_END
