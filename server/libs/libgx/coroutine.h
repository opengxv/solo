#ifndef __GX_COROUTINE_H__
#define __GX_COROUTINE_H__

#include "platform.h"
#ifdef GX_PLATFORM_WIN32
typedef LPVOID coctx_t;
#else
#include <ucontext.h>
struct ucontext;
typedef ucontext coctx_t;
#endif

#include "object.h"
#include "list.h"
#include "page.h"

GX_NS_BEGIN

class Context;
class CoManager;

class Coroutine {
    friend class CoManager;
public:
    typedef void(*routine_t)(void *);

    enum {
        DEAD,
        READY,
        RUNNING,
        SUSPEND,
    };

public:
    static Coroutine *spawn(Coroutine::routine_t routine, void *ud) noexcept;
    static void init() noexcept;
    bool resume() noexcept;
    static bool yield() noexcept;
    static Coroutine *self() noexcept;
    static bool is_main_routine() noexcept;

    int status() const noexcept {
        return _status;
    }
    Context *context() noexcept {
        return _context;
    }
    bool running() const noexcept {
        return _status == RUNNING;
    }
private:
    static void set_context(coctx_t *cc, void (*func)(), char *stkbase, long stksiz) noexcept;
    static void switch_context(coctx_t *octx, coctx_t *nctx) noexcept;

private:
    list_entry _entry;
    int _status;
    ptr<Context> _context;
    Page *_page;
    routine_t _routine;
    char *_stack;
    void *_ud;
    coctx_t *_ctx;
    char _placeholder[1];
    static CoManager *_mgr;
};

class CoManager : public Object {
    friend class Coroutine;
    typedef gx_list(Coroutine, _entry) co_list_t;
public:
    CoManager() noexcept;
    ~CoManager() noexcept;
private:
    bool grow() noexcept;
    static void routine() noexcept;
    Coroutine *spawn(Coroutine::routine_t routine, void *ud) noexcept;
    bool resume(Coroutine *co) noexcept;
    bool yield() noexcept;
    Coroutine *self() noexcept {
        Coroutine *co = _busy_list.front();
        assert(co);
        return co;
    }
    void init() noexcept;
    bool is_main_routine() noexcept {
        return self() == &_main;
    }
private:
    Coroutine **_coroutines;
    size_t _size;
    Coroutine _main;
    coctx_t _mainctx;
    co_list_t _free_list;
    co_list_t _yield_list;
    co_list_t _busy_list;
};

inline Coroutine *Coroutine::spawn(Coroutine::routine_t routine, void *ud) noexcept {
    return _mgr->spawn(routine, ud);
}

inline bool Coroutine::resume() noexcept {
    return _mgr->resume(this);
}

inline bool Coroutine::yield() noexcept {
    return _mgr->yield();
}

inline Coroutine *Coroutine::self() noexcept {
    return _mgr->self();
}
inline bool Coroutine::is_main_routine() noexcept {
    return _mgr->is_main_routine();
}

GX_NS_END

#endif

