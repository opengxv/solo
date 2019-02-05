#include "application.h"
#ifndef GX_PLATFORM_WIN32
#include <cstdlib>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#endif
#include "script.h"
#include "gxgetopt.h"
#include "log.h"
#include "utils.h"
#include "coroutine.h"

GX_NS_BEGIN

static volatile int __running = 1;
const object<Application> the_app;

#ifndef GX_PLATFORM_WIN32
static void __sig_handler(int sig) {
    print_back_trace();
    SIG_DFL(sig);
}
static void __sig_term_handler(int sig) {
	__running = 0;
}
#endif


Application::Application() noexcept
: _id(), _reactor(_timermgr)
#ifdef GX_USE_LUA
, _script(_filemonitor)
#endif
, _type()
{ }

Application::~Application() noexcept {
}

bool Application::init_env(int argc, char* const argv[], const char *optstring, std::function<void()> func) {
	_daemon = false;
	const char *home_dir = nullptr;
#ifndef GX_PLATFORM_WIN32
    signal(SIGSEGV, __sig_handler);
    signal(SIGABRT, __sig_handler);
    signal(SIGTERM, __sig_term_handler);
    signal(SIGHUP, __sig_term_handler);
    signal(SIGPIPE, SIG_IGN);
    _pid = (unsigned)getpid();

    if (!optstring) {
        optstring = "";
    }
    int opt;
    int option_index = 0;
    static struct option long_options[] = {
        {"home",    required_argument, 0,  0 },
        {"node",    required_argument, 0,  0 },
        {"daemon",  no_argument,       0,  0 },
        {0,         0,                 0,  0 }
    };

    while ((opt = getopt_long(argc, argv, optstring, long_options, &option_index)) != -1) {
        switch (opt) {
        case 0:
            switch (option_index) {
            case 0:
                home_dir = optarg;
                break;
            case 1:
                _id = strtoul(optarg, nullptr, 10);
                break;
            case 2:
                _daemon = true;
                break;
            }
            break;
        default:
            if (func) {
                func();
            }
            break;
        }
    }
#endif

    if (!home_dir) {
        home_dir = std::getenv("GX_HOME");
    }
    if (!home_dir) {
        home_dir = "..";
    }

    _app_dir = Path::pwd();
    _home_dir = home_dir;
    if (!_home_dir.is_absolute()) {
        _home_dir = _app_dir + home_dir;
    }
    _etc_dir = _home_dir + "etc";
    _script_dir = _home_dir + "script";
    _script_var_dir = _script_dir + "var";
    _var_dir = _home_dir + "var";
    _image_dir = _home_dir + "image";
    _log_dir = _home_dir + "log";

    _log_addr.resolve("*", 61234);
    adjust_time();
    return true;
}

bool Application::init(int argc, char* const argv[], const char *name, int type) {
    if (!init_env(argc, argv)) {
        return false;
    }

    if (!name) {
        name = argv[0];
    }
    init_name(name);

    the_log_printer = object<UdpLogPrinter>();
#ifdef GX_USE_LUA
    ScriptFunctionManager::instance()->upload(_script);

    if (_script->load(Path("/script") + _name) < 0) {
        return false;
    }
    timeval_t time = _script->read_integer("file_monitor_time");
    if (time) {
        _timermgr->schedule(time, std::bind(&Application::file_monitor_timer, this, time, _1, _2));
    }
    if (!_network->init(_script, _timermgr, _reactor)) {
        log_error("init network failed.");
    }
#endif

    if (type < 0 || (unsigned)type >= _network->nodes().size()) {
        for (auto &node : _network->nodes()) {
            if (_name == node->name()) {
                type = node->id();
                break;
            }
        }
        if (type < 0 || (unsigned)type >= _network->nodes().size()) {
            log_error("unknown node type '%d'.", type);
            return false;
        }
    }
    _type = type;
    return true;
}

void Application::init_name(const char *name) noexcept {
    auto path = Path(name).basename();
    auto parts = split(path, '-');
    if (parts.size() == 1) {
        _name = parts[0];
        _id = 0;
        return;
    }

    bool is_num = true;
    char *endptr = nullptr;
    _id = strtoul(parts.back().c_str(), &endptr, 10);
    if (endptr && *endptr) {
        is_num = false;
    }
    if (parts.size() == 2) {
        if (is_num) {
            _name = parts[0];
        }
        else {
            _name = parts[1];
        }
        return;
    }

    _name = parts[1];
}

bool Application::loop() noexcept {
    timeval_t t = _timermgr->loop();
    if (_reactor->loop(t) < 0) {
        return false;
    }
    return true;
}

void Application::daemon() noexcept {
#ifndef GX_PLATFORM_WIN32
    pid_t pid = fork();
    if (pid < 0) {
        exit(1);
    } else if (pid > 0) {
        exit(0);
    }
    setsid();
    _pid = pid;
    umask(0);
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    _daemon = false;
#endif
}

void Application::run() noexcept {
    if (_daemon) {
        daemon();
    }

    _network->startup(_type, _id);
    adjust_time();
    srand((unsigned)gettimeofday());
    Coroutine::init();
    while (__running) {
        loop();
    }

    _timermgr->clear();
    _network->shutdown_servlets();
    while (_network->call_count()) {
        the_context()->sleep(500);
    }

    Coroutine *co = Coroutine::spawn(shutdown_routine, this);
    co->resume();

    while (co->running()) {
        loop();
    }
}

timeval_t Application::file_monitor_timer(timeval_t r, Timer&, timeval_t) noexcept {
    _filemonitor->loop();
    return r;
}

void Application::term() noexcept {
    __running = false;
}

bool Application::termed() noexcept {
    return !__running;
}

void Application::shutdown_routine(void *param) noexcept {
    if (the_app->shutdown) {
        the_app->shutdown();
    }
}

GX_NS_END

