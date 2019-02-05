#ifndef __GX_ALLOCATOR_H__
#define __GX_ALLOCATOR_H__

#include "platform.h"
#include <vector>
#include <string>
#include <map>
#include <array>

#include "memory.h"
#include "obstack.h"
#include "pool.h"

GX_NS_BEGIN

template <typename _T, typename _Pool>
class object_cache {
public:
    typedef _T type;
    typedef _Pool pool_type;
    static constexpr const int object_size = gx_align_default(sizeof(type));

private:
    struct node {
        node *_next;
    };
public:
    object_cache(ptr<pool_type> pool) noexcept : _pool(pool), _list() { }

    template <typename ..._Args>
    type* construct(_Args&&...args) noexcept {
        void *p;
        if (_list) {
            p = _list;
            _list = _list->_next;
        }
        else {
            p = _pool->alloc(object_size);
        }
        return new(p) type(std::forward<_Args>(args)...);
    };

    void destroy(type *obj) noexcept {
        obj->~type();
        node *p = reinterpret_cast<node*>(obj);
        p->_next = _list;
        _list = p;
    }
private:
    ptr<pool_type> _pool;
    node *_list;
};

template <typename _Tp>
class pool_allocator {
    template <typename> friend class pool_allocator;
public:
    typedef size_t     size_type;
    typedef ptrdiff_t  difference_type;
    typedef _Tp*       pointer;
    typedef const _Tp* const_pointer;
    typedef _Tp&       reference;
    typedef const _Tp& const_reference;
    typedef _Tp        value_type;

    template<typename _Tp1>
    struct rebind {
        typedef pool_allocator<_Tp1> other;
    };

    pool_allocator(Pool *pool) noexcept
    : _pool(pool)
    { }

    pool_allocator(const pool_allocator &other) noexcept
    : _pool(other._pool)
    { }

    template<typename _Tp1>
    pool_allocator(const pool_allocator<_Tp1> &other) noexcept
    : _pool(other._pool)
    { }

    pointer address(reference __x) const noexcept {
        return std::__addressof(__x);
    }

    const_pointer address(const_reference __x) const noexcept {
        return std::__addressof(__x);
    }

    pointer allocate(size_type __n, const void * = 0) {
        return static_cast<_Tp *>(_pool->alloc(__n * sizeof(_Tp)));
    }

    void deallocate(pointer __p, size_type) {
    }

    size_type max_size() const noexcept {
        return size_t(-1) / sizeof(_Tp);
    }

    template<typename _Up, typename... _Args>
    void construct(_Up *__p, _Args&&... __args) {
        ::new((void *)__p)_Up(std::forward<_Args>(__args)...);
    }

    template<typename _Up>
    void destroy(_Up *__p) {
        __p->~_Up();
    }

    bool operator==(const pool_allocator &rhs) const noexcept {
        return _pool == rhs._pool;
    }

    bool operator!=(const pool_allocator &rhs) const noexcept {
        return _pool != rhs._pool;
    }

    Pool* pool() const noexcept {
        return _pool;
    }
private:
    pool_allocator() noexcept : _pool() { }

private:
    Pool *_pool;
};

class allocator_base {
public:
    static constexpr const unsigned max_size = 1024;
    static constexpr const unsigned bucket_count = max_size / gx_align_size;
private:
    struct node {
        node *_next;
    };
    struct bucket {
        bucket() : _node() { }
        node *_node;
    };
public:
    void *alloc(size_t size) noexcept {
        assert(size <= max_size);
        node **pbucket = (node**)_buckets.data() + ((gx_align_default(size) >> gx_align_order) - 1);
        node *bucket = *pbucket;
        void *p;
        if (bucket) {
            p = bucket;
            *pbucket = bucket->_next;
        }
        else {
            p = Pool::instance()->alloc(size);
        }
        return p;
    }
    void free(void *p, size_t size) noexcept {
        node **pbucket = (node**)_buckets.data() + ((gx_align_default(size) >> gx_align_order) - 1);
        node *bucket = (node*)p;
        bucket->_next = *pbucket;
        *pbucket = bucket;
    }
private:
    static std::array<node*, bucket_count> _buckets;
};

template <typename _Tp>
class allocator : protected allocator_base {
    template <typename> friend class pool_allocator;
public:
    typedef size_t     size_type;
    typedef ptrdiff_t  difference_type;
    typedef _Tp*       pointer;
    typedef const _Tp* const_pointer;
    typedef _Tp&       reference;
    typedef const _Tp& const_reference;
    typedef _Tp        value_type;

    template<typename _Tp1>
    struct rebind {
        typedef allocator<_Tp1> other;
    };

    allocator() noexcept
    { }

    allocator(const allocator &other) noexcept
    { }

    template<typename _Tp1>
    allocator(const allocator<_Tp1> &other) noexcept
    { }

    pointer address(reference __x) const noexcept {
        return std::__addressof(__x);
    }

    const_pointer address(const_reference __x) const noexcept {
        return std::__addressof(__x);
    }

    pointer allocate(size_type __n, const void * = 0) {
        return static_cast<_Tp *>(alloc(__n * sizeof(_Tp)));
    }

    void deallocate(pointer __p, size_type __n) {
        free(__p, __n * sizeof(_Tp));
    }

    size_type max_size() const noexcept {
        return size_t(-1) / sizeof(_Tp);
    }

    template<typename _Up, typename... _Args>
    void construct(_Up *__p, _Args&&... __args) {
        ::new((void *)__p)_Up(std::forward<_Args>(__args)...);
    }

    template<typename _Up>
    void destroy(_Up *__p) {
        __p->~_Up();
    }

    bool operator==(const allocator &rhs) const noexcept {
        return true;
    }

    bool operator!=(const allocator &rhs) const noexcept {
        return false;
    }
};

template <typename _Tp>
class obstack_allocator {
    template <typename> friend class obstack_allocator;
    template <typename, typename, typename> friend class std::basic_string;
public:
      typedef size_t     size_type;
      typedef ptrdiff_t  difference_type;
      typedef _Tp*       pointer;
      typedef const _Tp* const_pointer;
      typedef _Tp&       reference;
      typedef const _Tp& const_reference;
      typedef _Tp        value_type;

      template<typename _Tp1>
      struct rebind { 
          typedef obstack_allocator<_Tp1> other; 
      };

      obstack_allocator(Obstack *pool) noexcept
      : _pool(pool)
      { }

      obstack_allocator(const obstack_allocator &other) noexcept
      : _pool(other._pool)
      { }

      template<typename _Tp1>
      obstack_allocator(const obstack_allocator<_Tp1> &other) noexcept
      : _pool(other._pool) 
      { }

      pointer address(reference __x) const noexcept { 
          return std::__addressof(__x); 
      }

      const_pointer address(const_reference __x) const noexcept { 
          return std::__addressof(__x); 
      }

      pointer allocate(size_type __n, const void* = 0) {
          return static_cast<_Tp*>(_pool->alloc(__n * sizeof(_Tp)));
      }

      void deallocate(pointer __p, size_type) { 
      }

      size_type max_size() const noexcept { 
          return size_t(-1) / sizeof(_Tp); 
      }

      template<typename _Up, typename... _Args>
      void construct(_Up* __p, _Args&&... __args) { 
          ::new((void *)__p) _Up(std::forward<_Args>(__args)...); 
      }

      template<typename _Up>
      void destroy(_Up* __p) {
           __p->~_Up(); 
      }

      bool operator==(const obstack_allocator &rhs) const noexcept {
          return _pool == rhs._pool;
      }

      bool operator!=(const obstack_allocator &rhs) const noexcept {
          return _pool != rhs._pool;
      }
      Obstack *pool() const noexcept {
          return _pool;
      }
private:
    obstack_allocator() noexcept : _pool() { }

private:
    Obstack *_pool;
};

template <class _Key, typename _T, typename _Compare = std::less<_Key>, typename _Alloc = obstack_allocator<std::pair<const _Key, _T> > >
struct obstack_map : std::map<_Key, _T, _Compare, _Alloc> {
	//typedef typename std::map<_Key, _T, _Compare, _Alloc> base_type;
    //typedef typename base_type::allocator_type allocator_type;
    //using base_type::base_type;

	obstack_map(Obstack *pool) noexcept : std::map<_Key, _T, _Compare, _Alloc>(_Alloc(pool)) { }
};

template <typename _Tp, typename _Alloc = obstack_allocator<_Tp> >
struct obstack_vector : std::vector<_Tp, _Alloc> {
    typedef std::vector<_Tp, _Alloc> base_type;
    typedef _Alloc allocator_type;

    //using base_type::base_type;
    using base_type::begin;
    using base_type::end;

	obstack_vector(Obstack *pool) noexcept : std::vector<_Tp, _Alloc>(_Alloc(pool)) { }
#if 0
	void emplace_back() noexcept{
        do_emplace<_Tp>(this);
    }
#endif
    const std::vector<_Tp> &operator=(const std::vector<_Tp> &rhs) noexcept {
        *this = obstack_vector(rhs.begin(), rhs.end(), base_type::get_allocator());
        return rhs;
    }

    operator std::vector<_Tp>() const noexcept {
        return std::vector<_Tp>(base_type::begin(), base_type::end());
    }
		/*
private:
    template <typename _T1>
    static typename std::enable_if<
        std::is_class<_T1>::value, 
        void>::type
    do_emplace(obstack_vector<_Tp> *vec) noexcept {
        vec->base_type::emplace_back(vec->get_allocator().pool());
    }

    template <typename _T1>
    static typename std::enable_if<
        !std::is_class<_T1>::value, 
        void>::type
    do_emplace(obstack_vector<_Tp> *vec) noexcept {
        vec->base_type::emplace_back();
    }*/
};

struct obstack_string : std::basic_string<char, std::char_traits<char>, gx::obstack_allocator<char>> {
    typedef std::basic_string<char, std::char_traits<char>, gx::obstack_allocator<char>> base_type;
	typedef gx::obstack_allocator<char> allocator_t;
    using base_type::operator=;
    //using base_type::base_type;

	obstack_string(const std::string &x, const allocator_t &alloc) noexcept 
	: base_type(x.c_str(), x.size(), alloc) 
	{ }

    const obstack_string &operator=(const std::string &rhs) noexcept {
        *this = obstack_string(rhs, get_allocator());
        return *this;
    }

    operator std::string() const noexcept {
        return std::string(c_str(), size());
    }
};

inline bool operator==(const obstack_string &lhs, const std::string &rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    return memcmp(lhs.c_str(), rhs.c_str(), lhs.size()) == 0;
}

inline bool operator==(const std::string lhs, const obstack_string &rhs) noexcept {
    return rhs == lhs;
}

inline bool operator!=(const obstack_string &lhs, const std::string &rhs) noexcept {
    return !(lhs == rhs);
};

inline bool operator!=(const std::string lhs, const obstack_string &rhs) noexcept {
    return !(lhs == rhs);
};

GX_NS_END

#endif

