#include "reactor.h"
#include "log.h"

#include <time.h>
#ifdef GX_PLATFORM_LINUX
#include <sys/epoll.h>
#include <sys/socket.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

GX_NS_BEGIN

/* Reactor */
Reactor::Reactor(ptr<TimerManager> timermgr, unsigned maxfds, unsigned maxevents) noexcept
: _maxfds(maxfds), _maxevents(maxevents), _timermgr(timermgr)
{
#if defined(GX_REACTOR_USE_EPOLL)
	_fds.resize(maxfds);
    _events = (struct ::epoll_event *)std::malloc(sizeof(struct ::epoll_event) * _maxevents);
    if ((_fd = epoll_create(maxfds)) < 0) {
        log_die("epoll create failed");
    }
#elif defined(GX_REACTOR_USE_SELECT)
#endif
}

Reactor::~Reactor() noexcept {
#if defined(GX_REACTOR_USE_EPOLL)
	if (fd_valid(_fd)) {
        fd_close(_fd);
    }
    if (_events) {
        std::free(_events);
    }
#elif defined(GX_REACTOR_USE_SELECT)
#endif
}

void Reactor::push() noexcept {
    weak_ptr<Socket> socket;
    while ((socket = _send_list.pop_front())) {
        if (socket->push() < 0) {
            if (!socket->_handler(*socket, poll_err)) {
                if (socket) {
                    close(socket->fd());
                    continue;
                }
            }
        }
        _sock_list.push_front(socket);
		if (socket->_output.size()) {
#ifdef GX_REACTOR_USE_SELECT
			socket->flags(socket->flags() | poll_out);
#endif
		}
		else {
#ifdef GX_REACTOR_USE_SELECT
			socket->flags(socket->flags() & (~poll_out));
#endif
			if (socket->_timer) {
#ifdef GX_PLATFORM_WIN32
				shutdown(socket->fd(), SD_SEND);
#else
				shutdown(socket->fd(), SHUT_WR);
#endif
			}
		}
    }
}

void Reactor::send(Socket *socket) {
    if (socket->_reactor == this) {
        SocketList::remove(socket);
        _send_list.push_front(socket);
    }
}

Socket *Reactor::open(int fd, unsigned flags, Socket::handler_type handler, bool et) noexcept {
#if defined(GX_REACTOR_USE_EPOLL)
    if (!fd_valid(fd) || (unsigned)fd >= maxfds()) {
        return nullptr;
    }

    if (_fds[fd]) {
        return nullptr;
    }

	fd_block(fd, false);
	fd_nodelay(fd, true);

    struct epoll_event event;
    event.data.fd = fd;
    event.events = (et ? EPOLLET : 0) | EPOLLHUP | EPOLLRDHUP;

    if (flags & poll_in) {
        event.events |= EPOLLIN;
    }

    if (flags & poll_out) {
        event.events |= EPOLLOUT;
    }

    if (flags & poll_err) {
        event.events |= EPOLLERR;
    }

    if (epoll_ctl(_fd, EPOLL_CTL_ADD, fd, &event) < 0) {
        return nullptr;
    }

    object<Socket> socket;
    socket->_fd = fd;
    socket->_handler = handler;
    socket->_flags = flags;
    socket->_reactor = this;
    _fds[socket->fd()] = socket;
    _sock_list.push_front(socket);
	return socket;
#elif defined(GX_REACTOR_USE_SELECT)
	if (!fd_valid(fd)) {
		return nullptr;
	}

	fd_block(fd, false);
	fd_nodelay(fd, true);

	object<Socket> socket;
	auto em = _fds.emplace(fd, socket);
	if (!em.second) {
		return nullptr;
	}
	socket->_fd = fd;
	socket->_handler = handler;
	socket->_flags = flags;
	socket->_reactor = this;
	_sock_list.push_front(socket);
	return socket;
#endif
}

bool Reactor::on_linger_data(Socket &socket, unsigned flags) noexcept {
    if (flags & Reactor::poll_close) {
        fd_close(socket.fd());
        if (socket._timer) {
            socket._timer->close();
        }
        return false;
    }
    if (flags & Reactor::poll_err) {
        return false;
    }
    if (flags & Reactor::poll_in) {
        char buf[8192];
        while (1) {
            int n = fd_read(socket.fd(), buf, sizeof(buf));
            if (gx_likely(n > 0)) {
                continue;
            }
            else if (gx_likely(n < 0)) {
                if (gx_likely(errno == EAGAIN)) {
                    break;
                }
                else if (gx_likely(errno == EINTR)) {
                    continue;
                }
                else {
                    return false;
                }
            }
            else {
                return false;
            }
        }
    }
    return true;
}

timeval_t Reactor::on_linger_timer(Socket *socket, Timer&, timeval_t) noexcept {
    socket->_timer = nullptr;
    socket->close();
    return 0;
}

void Reactor::close(int fd, timeval_t linger) noexcept {
#if defined(GX_REACTOR_USE_EPOLL)
    if (!fd_valid(fd) || (unsigned)fd >= maxfds()) {
        return;
    }

    Socket *socket = _fds[fd];
    if (!socket) {
        return;
    }
    if (!linger) {
        socket->_reactor = nullptr;
        epoll_ctl(_fd, EPOLL_CTL_DEL, fd, nullptr);
        socket->_handler(*socket, poll_close);
        SocketList::remove(socket);
        _fds[fd] = nullptr;
        return;
    }
    socket->_timer = _timermgr->schedule(linger, std::bind(on_linger_timer, socket, _1, _2));
    socket->handler(std::bind(on_linger_data, _1, _2));
    socket->flags(-1);
#elif defined(GX_REACTOR_USE_SELECT)
	if (!fd_valid(fd)) {
		return;
	}

	auto it = _fds.find(fd);
	if (it == _fds.end()) {
		return;
	}

	Socket *socket = it->second;
	if (!socket) {
		return;
	}
	if (!linger) {
		socket->_reactor = nullptr;
		socket->_handler(*socket, poll_close);
		SocketList::remove(socket);
		_fds.erase(it);
		return;
	}
	socket->_timer = _timermgr->schedule(linger, std::bind(on_linger_timer, socket, _1, _2));
	socket->handler(std::bind(on_linger_data, _1, _2));
	socket->flags(-1);
#endif
}

bool Reactor::modify(Socket *socket) noexcept {
#if defined(GX_REACTOR_USE_EPOLL)
	if (!fd_valid(socket->fd()) || (unsigned)socket->fd() >= maxfds()) {
        return false;
    }
    if (socket->_reactor != this) {
        return false;
    }
    if (socket != _fds[socket->fd()]) {
        return false;
    }

    unsigned flags = socket->flags();
    struct epoll_event event;
    event.data.fd = socket->fd();
    event.events = EPOLLET | EPOLLHUP | EPOLLRDHUP;

    if (flags & poll_in) {
        event.events |= EPOLLIN;
    }

    if (flags & poll_out) {
        event.events |= EPOLLOUT;
    }

    if (flags & poll_err) {
        event.events |= EPOLLERR;
    }

    if (epoll_ctl(_fd, EPOLL_CTL_MOD, socket->fd(), &event) < 0) {
        return false;
    }
    return true;
#elif defined(GX_REACTOR_USE_SELECT)
	if (socket->_reactor != this) {
		return false;
	}

	auto it = _fds.find(socket->fd());
	if (it == _fds.end()) {
		return false;
	}
	if (socket != it->second) {
		return false;
	}
	return true;
#endif
}

int Reactor::loop(timeval_t timeout) {
    int nfds, flags;
    weak_ptr<Socket> socket;

    timeval_t cur = adjust_time();
    if (timeout <= cur) {
        return 0;
    }

	push();
#if defined(GX_REACTOR_USE_EPOLL)
	struct epoll_event *event;
again:
    nfds = epoll_wait(_fd, _events, _maxevents, timeout - cur);
    if (gx_unlikely(nfds == -1)) {
        if (gx_likely(errno == EINTR)) {
            goto again;
        }
        return -errno;
    } else if (gx_likely(nfds > 0)) {
        adjust_time();
        for (event = _events; nfds--; event++) {
            socket = _fds[event->data.fd].get();
            flags = 0;
            if (event->events & (EPOLLIN | EPOLLRDHUP | EPOLLRDHUP)) {
                flags |= poll_in;
            }
            if (event->events & EPOLLOUT) {
                flags |= poll_out;
            }
            if (event->events & EPOLLERR) {
                flags |= poll_err;
            }
            if (!socket->_handler(*socket, flags)) {
                if (socket) {
                    close(socket->fd());
                }
            }
        }
    }
    return 0;
#elif defined(GX_REACTOR_USE_SELECT)
	fd_set rfds;
	fd_set wfds;
	fd_set efds;
	struct timeval tv;
	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	FD_ZERO(&efds);
	for (auto it = _fds.begin(); it != _fds.end(); ++it) {
		socket = it->second.get();
		flags = socket->flags();
		if (flags & poll_in) {
			FD_SET(socket->fd(), &rfds);
		}
		if (flags & poll_out) {
			FD_SET(socket->fd(), &wfds);
		}
		if (flags & poll_err) {
			FD_SET(socket->fd(), &efds);
		}
	}
	timeval_t d = cur - timeout;
	tv.tv_sec = (long)(d / 1000);
	tv.tv_usec = (long)((d % 1000) * 1000);
	nfds = select(0, &rfds, &wfds, &efds, &tv);
	if (nfds == SOCKET_ERROR) {
		return -1;
	}
	else if (!nfds) {
		return 0;
	}
	for (auto it = _fds.begin(); it != _fds.end();) {
		socket = it->second.get();
		++it;
		flags = 0;
		if (FD_ISSET(socket->fd(), &rfds)) {
			flags |= poll_in;
		}
		if (FD_ISSET(socket->fd(), &wfds)) {
			flags |= poll_out;
		}
		if (FD_ISSET(socket->fd(), &efds)) {
			flags |= poll_err;
		}
		if (!socket->_handler(*socket, flags)) {
			if (socket) {
				close(socket->fd());
			}
		}
	}
	return 0;
#endif
}

GX_NS_END

