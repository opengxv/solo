#include "coroutine.h"
#include "log.h"
#include "context.h"

#define GX_CO_CAP       4096
#define GX_CO_MEMSIZE   (64 * 1024)
#define GX_CO_GROW      32
#define GX_CO_CTXSIZE   gx_align_default(sizeof(coctx_t))
#define GX_CO_STKSIZE   (GX_CO_MEMSIZE - gx::offsetof_member(&Coroutine::_placeholder) - sizeof(long) - GX_CO_CTXSIZE)

GX_NS_BEGIN


static object<CoManager> __mgr;
CoManager *Coroutine::_mgr = __mgr;

CoManager::CoManager() noexcept {
    size_t n = sizeof(Coroutine*) * GX_CO_CAP;
    _coroutines = (Coroutine**)::malloc(n);
    memset(_coroutines, 0, n);
    _size = 0;
    _busy_list.push_front(&_main);
    _main._status = Coroutine::DEAD;
    _main._ctx = &_mainctx;

#ifdef GX_PLATFORM_WIN32
	if (!::IsThreadAFiber()) {
		_mainctx = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
	}
#endif
}

CoManager::~CoManager() noexcept {
    for (unsigned i = 0; i < GX_CO_CAP; ++i) {
        Coroutine *co = _coroutines[i];
        if (co) {
            Page *page = co->_page;
            co->~Coroutine();
            PageAllocator::instance()->free(page);
        }
    }
    ::free(_coroutines);
}

void CoManager::init() noexcept {
    _main._context = Context::factory();
    _main._context->_co = &_main;
}

bool CoManager::grow() noexcept {
    if (_size + GX_CO_GROW > GX_CO_CAP) {
        return false;
    }
    unsigned i = _size;
    _size += GX_CO_GROW;
    for (; i < _size; i++) {
        Page *page = PageAllocator::instance()->alloc(GX_CO_MEMSIZE);
        Coroutine *co = new(page->firstp) Coroutine;
        co->_page = page;
        co->_status = Coroutine::DEAD;
        co->_ctx = (coctx_t*)co->_placeholder;
        co->_stack = co->_placeholder + GX_CO_CTXSIZE;
        _free_list.push_front(co);
        _coroutines[i] = co;
    }
    return true;
}

void CoManager::routine() noexcept {
    CoManager *mgr = __mgr;

    Coroutine *co = mgr->_busy_list.front();
    co->_routine(co->_ud);
    co->_status = Coroutine::DEAD;
    mgr->_busy_list.pop_front();
    mgr->_free_list.push_front(co);
    Coroutine *caller = mgr->_busy_list.front();
    assert(caller);
    Coroutine::switch_context(co->_ctx, caller->_ctx);
}

Coroutine *CoManager::spawn(Coroutine::routine_t routine, void *ud) noexcept {
    Coroutine *co;
    if (!(co = _free_list.pop_front())) {
        if (!grow()) {
            return nullptr;
        }
        co = _free_list.pop_front();
    }
    _yield_list.push_front(co);
    co->_routine = routine;
    co->_ud = ud;
    co->_status = Coroutine::READY;
    if (!co->_context) {
        co->_context = Context::factory();
        co->_context->_co = co;
    }
    return co;
}

bool CoManager::resume(Coroutine *co) noexcept {
    Coroutine *caller = _busy_list.front();
    assert(caller);
    if (caller == co) {
        return false;
    }

    switch (co->_status) {
    case Coroutine::READY:
        Coroutine::set_context(co->_ctx, routine, co->_stack, GX_CO_STKSIZE);
    case Coroutine::SUSPEND:
        co_list_t::remove(co);
        _busy_list.push_front(co);
        co->_status = Coroutine::RUNNING;
        Coroutine::switch_context(caller->_ctx, co->_ctx);
		return true;
    default:
        return false;
    }
}

bool CoManager::yield() noexcept {
    Coroutine *co = _busy_list.front();
    assert(co);
    if (co == &_main) {
        return false;
    }

    co_list_t::remove(co);
    _yield_list.push_front(co);
    co->_status = Coroutine::SUSPEND;

    Coroutine *caller = _busy_list.front();
    assert(caller);
    Coroutine::switch_context(co->_ctx, caller->_ctx);
	return true;
}

void Coroutine::init() noexcept {
    _mgr->init();
}

inline void Coroutine::set_context(coctx_t *ctx, void (*func)(), char *stkbase, long stksiz) noexcept {
#ifdef GX_PLATFORM_WIN32
	*ctx = CreateFiberEx(stksiz, stksiz, FIBER_FLAG_FLOAT_SWITCH, (LPFIBER_START_ROUTINE)func, 0);
#else
    getcontext(ctx);

    ctx->uc_link = nullptr;
    ctx->uc_stack.ss_sp = stkbase;
    ctx->uc_stack.ss_size = stksiz - sizeof(long);
    ctx->uc_stack.ss_flags = 0;

    makecontext(ctx, (void(*)(void))func, 0);
#endif
}

inline void Coroutine::switch_context(coctx_t *octx, coctx_t *nctx) noexcept {
    assert(octx != nctx);
#ifdef GX_PLATFORM_WIN32
	*octx = GetCurrentFiber();
	SwitchToFiber(*nctx);
#else
	swapcontext(octx, nctx);
#endif
}

GX_NS_END

