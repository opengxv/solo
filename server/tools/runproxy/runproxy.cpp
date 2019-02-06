#include <unistd.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <netdb.h>
#include <cstring>
#include <cstdarg>
#include <string>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>

pid_t __pid = -1;
const char *__dir;

void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

bool resolve_addr(const char *host, const char *port, struct sockaddr_in *addr) {
    struct addrinfo hints, *ai;
    int error;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = 0;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = IPPROTO_TCP;

    error = ::getaddrinfo(host, port, &hints, &ai);
    if (error != 0) {
        return false;
    }

    if (!ai) {
        return false;
    }

    memcpy(addr, ai->ai_addr, sizeof(struct sockaddr_in));
    freeaddrinfo(ai);

    return true;
}

void prompt() {
    printf("\033[32m\033[01m%s> \033[0m", __dir);
    fflush(stdout);
}

void sig_chld(int signo) {
    pid_t pid;
    int stat;
    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
        if (pid == __pid) {
            signal(SIGINT, SIG_DFL);
            __pid = -1;
            prompt();
        }
    }
    return;
}

void do_system(const char *str) {
    if (__pid != -1) {
        int stat;
        signal(SIGCHLD, SIG_IGN);
        signal(SIGINT, SIG_IGN); 
        killpg(getpid(), SIGINT);
        waitpid(__pid, &stat, WNOHANG);
        __pid = -1;
    }
    signal(SIGCHLD, SIG_DFL);
    signal(SIGINT, SIG_DFL); 
    __pid = fork();
    if (__pid == -1) {
        perror("fork failed.");
        exit(1);
    }
    if (__pid == 0) {
        execl("/bin/sh", "sh", "-c", str, nullptr);
        perror("exec");
        exit(errno);
    }
    else {
        signal(SIGCHLD, sig_chld);
        signal(SIGINT, SIG_IGN);
    }
}

void do_request(int fd) {
    char buf[8192];
    int size = read(fd, buf, sizeof(buf));
    close(fd);
    if (size <= 0) {
        return;
    }
    buf[size] = '\0';
    printf("\033[31m\033[01m%s\033[0m\n", buf);
    do_system(buf);
}

void do_server(struct sockaddr_in *addr) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        die("create socket failed.");
    }
    int n = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(int))) {
        die("setsockopt failed.");
    }
    if (bind(fd, (struct sockaddr*)addr, sizeof(*addr))) {
        die("bind failed.");
    }

    if (listen(fd, 32)) {
        die("listen failed.");
    }

    __dir = get_current_dir_name();
    socklen_t len = sizeof(*addr);
    int cfd;
    while (1) {
        prompt();
        cfd = accept(fd, (struct sockaddr*)addr, &len);
        if (cfd == -1) {
            if (errno == EINTR) {
                continue;
            }
            die("accept failed.");
        }
        do_request(cfd);
    }
}

void do_client(struct sockaddr_in *addr, const std::string &dir, const std::string &cmd) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        die("create socket failed.");
    }

    while (1) {
        int n = connect(fd, (struct sockaddr*)addr, sizeof(*addr));
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            die("connect failed.");
        }
        break;
    }

    if (write(fd, cmd.c_str(), cmd.length()) > 0) {
        shutdown(fd, SHUT_WR);

        char buf[1024];
        while (1) {
            if (read(fd, buf, sizeof(buf)) <= 0) {
                break;
            }
        }
    }
    close(fd);
}

int main(int argc, char **argv) {
    std::string host("localhost");
    std::string port("4321");
    std::string dir(".");
    std::string cmd;
    bool is_server = false;
    struct sockaddr_in addr;

    int c;
    while ((c = getopt(argc, argv, "h:p:d:c:s")) != -1) {
        switch (c) {
        case 'h':
            host = optarg;
            break;
        case 'p':
            port = optarg;
            break;
        case 's':
            is_server = true;
            break;
        case 'd':
            dir = optarg;
            break;
        case 'c':
            cmd = optarg;
            break;
        default:
            die("runproxy -h host -p port -s");
        }
    }

    memset(&addr, 0, sizeof(addr));
    if (!resolve_addr(host.c_str(), port.c_str(), &addr)) {
        die("bad socket address '%s:%s'.", host.c_str(), port.c_str());
    }

    if (is_server) {
        do_server(&addr);
    }
    else {
        do_client(&addr, dir, cmd);
    }
	return 0;
}

