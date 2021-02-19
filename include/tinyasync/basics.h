
#ifndef TINYASYNC_BASICS_H
#define TINYASYNC_BASICS_H

#ifdef __clang__

#include <experimental/coroutine>
namespace std {
    using std::experimental::suspend_always;
    using std::experimental::suspend_never;
    using std::experimental::coroutine_handle;
    using std::experimental::noop_coroutine;
}
#else

#include <coroutine>

#endif

#include <atomic>
#include <exception>
#include <utility>
#include <map>
#include <unordered_map>
#include <string>
#include <vector>
#include <type_traits>
#include <system_error>

#include <stdarg.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <memory.h>
#include <memory>
#include <list>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <new>

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <Windows.h>

#pragma comment(lib, "Ws2_32.lib") 

#elif defined(__unix__)

#include <netdb.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <cxxabi.h>
#include <sys/eventfd.h>
#include <pthread.h>

using SystemHandle = int;

#endif


#ifndef TINYASYNC_NDEBUG

#ifndef TINYASYNC_DEF_NDEBUG

#ifdef NDEBUG
#define TINYASYNC_NDEBUG NDEBUG
#endif // NDEBUG

#else // TINYASYNC_DEF_NDEBUG

#if TINYASYNC_DEF_NDEBUG
#define TINYASYNC_NDEBUG 1
#endif // TINYASYNC_DEF_NDEBUG

#endif // TINYASYNC_DEF_NDEBUG

#endif // TINYASYNC_NDEBUG



#ifdef TINYASYNC_NDEBUG
#define TINYASYNC_ASSERT(x)  ((void)0)
#else
#define TINYASYNC_ASSERT(x)  assert(x)
#endif


// Coroutine Basics

namespace tinyasync {

    std::string vformat(char const* fmt, va_list args)
    {
        va_list args2;
        va_copy(args2, args);
        std::size_t n = vsnprintf(NULL, 0, fmt, args2);
        std::string ret;
        ret.resize(n);
        vsnprintf((char*)ret.data(), ret.size() + 1, fmt, args);
        return ret;
    }

    std::string format(char const* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        std::string str = vformat(fmt, args);
        va_end(args);
        return str;
    }

#ifdef _WIN32
    using NativeHandle = HANDLE;
    using NativeSocket = SOCKET;

    // https://docs.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-closehandle
    // do not mixing using close_handle and close_socket
    inline int close_socket(NativeSocket socket)
    {
        return ::closesocket(socket);
    }

    inline BOOL close_handle(NativeHandle h)
    {
        return ::CloseHandle(h);
    }


    void sync_sleep(std::chrono::nanoseconds nanoseconds)
    {
        uint64_t miliseconds_ = (nanoseconds.count() / (1000*1000));
        assert((DWORD)(miliseconds_) == miliseconds_);
        DWORD miliseconds = (DWORD)(miliseconds_);
        ::Sleep(miliseconds);
    }

#elif defined(__unix__)

    using NativeHandle = int;
    using NativeSocket = int;

    int close_socket(NativeSocket h)
    {
        return ::close(h);
    }

    int close_handle(NativeHandle h)
    {
        return ::close(h);
    }

    timespec to_timespec(std::chrono::nanoseconds nanoseconds)
    {

        timespec time;
        auto seconds = nanoseconds.count() / (1000 * 1000 * 1000);
        auto nanos = (nanoseconds.count() - seconds * (1000 * 1000 * 1000));

        time.tv_sec = seconds;
        time.tv_nsec = nanos;

        return time;
    }

    void sync_sleep(std::chrono::nanoseconds nanoseconds)
    {
        auto timespec = to_timespec(nanoseconds);
        ::nanosleep(&timespec, NULL);
    }

    std::string abi_name_demangle(const char* abi_name)
    {
        // https://gcc.gnu.org/onlinedocs/libstdc++/libstdc++-html-USERS-4.3/a01696.html
        // https://stackoverflow.com/questions/4939636/function-to-mangle-demangle-functions
        int status;
        char const* name = abi::__cxa_demangle(abi_name, NULL, 0, &status);

        std::string ret;
        // 0: The demangling operation succeeded.
        if (status != 0) {
            ret = "<unknown-type-name>";
        } else {
            ret = name;
            ::free((void*)name);
        }
        return ret;
    }

#endif


    // compiler related
#ifdef __GNUC__
#define TINYASYNC_VCINL inline
#define TINYASYNC_FUNCNAME __PRETTY_FUNCTION__
#define TINYASYNC_LIKELY [[likely]]
#define TINYASYNC_UNLIKELY [[unlikely]]

#elif defined(_MSC_VER)
#define TINYASYNC_VCINL __forceinline
#define TINYASYNC_FUNCNAME __func__
#define TINYASYNC_LIKELY [[likely]]
#define TINYASYNC_UNLIKELY [[unlikely]]

#else
#define TINYASYNC_VCINL inline
#define TINYASYNC_FUNCNAME __func__
#define TINYASYNC_LIKELY
#define TINYASYNC_UNLIKELY

#endif


    template<class T, class L>
    T do_initialize_once(std::atomic<T>& atom, T uninitialized_flag, std::mutex& mtx, L func)
    {
        std::lock_guard<std::mutex> g(mtx);
        T t = atom.load(std::memory_order_relaxed);
        if (t == uninitialized_flag) {
            t = func();
            atom.store(t, std::memory_order_release);
        }
        return t;
    };

    // double checking lock
    template<class T, class L>
    TINYASYNC_VCINL T initialize_once(std::atomic<T>& atom, T uninitialized_flag, std::mutex& mtx, L func)
    {
        T t = atom.load(std::memory_order_acquire);
        if (t == uninitialized_flag) [[unlikely]] {
            t = do_initialize_once(atom, uninitialized_flag, mtx, func);
        }
        return t;
    };

    NativeHandle const NULL_HANDLE = 0;
    NativeSocket const NULL_SOCKET = NativeSocket(0);

    template<class Result>
    class TaskPromise;

    template<class Result>
    class Task;

    class ConnImpl;
    class ConnAwaiter;
    class ConnCallback;

    class AcceptorImpl;
    class AcceptorCallback;
    class AcceptorAwaiter;

    class ConnectorImpl;
    class ConnectorCallback;
    class ConnectorAwaiter;


    class TimerAwaiter;
    class IoContext;

    class Mutex;
    
    template<class T>
    class Task;

    inline std::map<std::coroutine_handle<>, std::string> name_map;
    inline void set_name(std::coroutine_handle<> h, std::string name)
    {
        auto& name_ = name_map[h];
        name_ = std::move(name);
    }

    inline char const* c_name(std::coroutine_handle<> h)
    {
        if (h == nullptr)
            return "null";
        else if (h == std::noop_coroutine())
            return "noop";

        auto& name = name_map[h];
        if (name.empty()) {
            name = format("%p", h.address());
        }
        return name.c_str();
    }

    using TypeInfoRef = std::reference_wrapper<const std::type_info>;
    struct TypeInfoRefHahser {
        std::size_t operator()(TypeInfoRef info) const
        {
            return info.get().hash_code();
        }
    };
    struct TypeInfoRefEqualer {
        std::size_t operator()(TypeInfoRef l, TypeInfoRef r) const
        {
            return l.get() == r.get();
        }
    };

    inline char const* c_name(std::type_info const& info)
    {

#ifdef _WIN32
        return info.name();
#elif defined(__unix__)

        static std::unordered_map<TypeInfoRef, std::string, TypeInfoRefHahser, TypeInfoRefEqualer> map;
        auto& name = map[std::ref(info)];
        if (name.empty()) {
            name = abi_name_demangle(info.name());
        }
        return name.c_str();
#endif
    }

    inline char const* handle_c_str(NativeHandle handle)
    {
        static std::map<NativeHandle, std::string> handle_map;
        auto& str = handle_map[handle];
        if (str.empty()) {
#ifdef _WIN32
            str = format("%d", handle);
#elif defined(__unix__)
            str = format("%d", handle);
#endif
        }
        return str.c_str();
    }

    inline char const* socket_c_str(NativeSocket handle)
    {
        return handle_c_str((NativeHandle)handle);
    }

    void to_string_to(std::exception_ptr const& e, std::string& string_builder)
    {
        if (!e) {
            string_builder += "<empty exception>\n";
            return;
        }
        try {
            std::rethrow_exception(e);
        }
        catch (const std::exception& e_) {
            string_builder += format("%s: what: %s\n", c_name(typeid(e_)), e_.what());

            // its endpoint class could be _Nest_exception
            try {
                std::rethrow_if_nested(e_);
            }
            catch (...) {
                string_builder += "raised from: ";
                to_string_to(std::current_exception(), string_builder);
            }
        }
        catch (const std::string& e_) {
            // should not throw std::string
            // I will print it out anyway
            string_builder += format("%s: %s\n", c_name(typeid(e_)), e_.c_str());

            // std::rethrow_if_nested not work for non-polymorphic class exception
            // e_ may have nested exception
            // but we don't know
        }
        catch (char const* c_str) {
            string_builder += format("%s: %s\n", c_name(typeid(c_str)), c_str);
        }
        catch (...) {
            string_builder += "<unkown type>\n";
        }
    }

    template<class Promise = void>
    struct ThisCoroutineAwaiter : std::suspend_always {

        template<class P>
        bool await_suspend(std::coroutine_handle<P> h)
        {
            if constexpr (std::is_same_v<void, Promise>) {
                m_coroutine = h;
            } else {
                Promise &promise = h.promise();
                m_coroutine = std::coroutine_handle<Promise>::from_promise(promise);
            }
            return false;
        }
        
        bool await_suspend(std::coroutine_handle<Promise> h)
        {
            m_coroutine = h;
            return false;
        }
        std::coroutine_handle<Promise> await_resume()
        {
            return m_coroutine;
        }
        std::coroutine_handle<Promise> m_coroutine = nullptr;
    };


    template<class Promise = void>
    ThisCoroutineAwaiter<Promise> this_coroutine()
    {
        return { };
    };

    std::string to_string(std::exception_ptr const& e)
    {
        std::string sb = "top exception: ";
        to_string_to(e, sb);
        return sb;
    }

#ifdef TINYASYNC_TRACE

#define TINYASYNC_CAT_(a, b) a##b
#define TINYASYNC_CAT(a, b) TINYASYNC_CAT_(a, b)
#define TINYASYNC_GUARD(...) log_prefix_guad TINYASYNC_CAT(log_prefix_guad_, __LINE__)(__VA_ARGS__)
#define TINYASYNC_LOG(...) \
    do                     \
    {                      \
        log(__VA_ARGS__);  \
        printf("\n");      \
        fflush(stdout);    \
    } while (0)
#define TINYASYNC_LOG_NNL(...) do {\
     log(__VA_ARGS__);\
    fflush(stdout);\
    } while(0)



    inline thread_local std::vector<std::string> log_prefix;



    inline void log(char const* fmt, ...)
    {
        for (auto& p : log_prefix) {
            printf("%s", p.c_str());
        }
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
    }

    struct log_prefix_guad
    {
        std::size_t l;

        log_prefix_guad(char const* fmt, ...) : l(0)
        {

            char buf[1000];
            va_list args;
            va_start(args, fmt);
            vsnprintf(buf, 1000, fmt, args);
            va_end(args);

            buf[1000 - 1] = 0;


            log_prefix.emplace_back(buf);
            l = log_prefix.size();
        }

        ~log_prefix_guad()
        {
            assert(l == log_prefix.size());
            log_prefix.pop_back();
        }
    };

#else

#define TINYASYNC_GUARD(...) \
    do                       \
    {                        \
    } while (0)
#define TINYASYNC_LOG(...) \
    do                     \
    {                      \
    } while (0)
#define TINYASYNC_LOG_NNL(...) \
    do                         \
    {                          \
    } while (0)

#endif // TINYASYNC_TRACE

    struct Noise
    {
        char const* src_loc;
        Noise(char const* src_loc) : src_loc(src_loc)
        {
            TINYASYNC_GUARD("Noise::() %s", src_loc);
            TINYASYNC_LOG("");
        }

        ~Noise()
        {
            TINYASYNC_GUARD("Noise::~Noise() %s", src_loc);
            TINYASYNC_LOG("");
        }
    };

    // from https://itanium-cxx-abi.github.io/cxx-abi/abi.html:
    // non-trivial for the purposes of calls
    // A type is considered non-trivial for the purposes of calls if:
    // it has a non-trivial copy constructor, move constructor, or destructor, or
    // all of its copy and move constructors are deleted.

    template <class T>
    constexpr bool is_trivial_parameter_in_itanium_abi_v =
        std::is_trivially_destructible_v<T>
        && (!std::is_copy_constructible_v<T> || std::is_trivially_copy_constructible_v<T>)
        && (!std::is_move_constructible_v<T> || std::is_trivially_move_constructible_v<T>)
        && (std::is_copy_constructible_v<T> || std::is_move_constructible_v<T>);

    template <class T>
    struct is_trivial_parameter_in_itanium_abi :
        std::bool_constant<is_trivial_parameter_in_itanium_abi_v<T> >
    {
    };

    // almost everthing trivial, except construction
    // you can safely use memcpy
    // you can't safely use memset 
    template <class T>
    constexpr bool has_trivial_five_v =
        std::is_trivially_destructible_v<T>
        && std::is_trivially_copy_constructible_v<T>
        && std::is_trivially_copy_assignable_v<T>
        && std::is_trivially_move_constructible_v<T>
        && std::is_trivially_move_assignable_v<T>;

    template <class T>
    struct has_trivial_five :
        std::bool_constant<has_trivial_five_v<T> >
    {
    };

    struct Name
    {

        Name(std::string name) : m_name(std::move(name))
        {
        }
        Name(std::string_view name) : m_name(name)
        {
        }
        Name(char const* name) : m_name(name)
        {
        }

        std::string m_name;
    };

    inline bool set_name_r(std::coroutine_handle<> const& h, Name const& name)
    {
        TINYASYNC_GUARD("set_name_r(): ");
        TINYASYNC_LOG("set name `%s` for %p", name.m_name.c_str(), h.address());
        ::tinyasync::set_name(h, name.m_name);
        return true;
    }

    bool set_name_r(std::coroutine_handle<> const& h)
    {
        return false;
    }

    template <class F, class... T>
    inline bool set_name_r(std::coroutine_handle<> const& h, F const& f, T const &...args)
    {
        return set_name_r(h, args...);
    }


    void throw_error(std::string const& what, int ec)
    {
        throw std::system_error(ec, std::system_category(), what);
    }

#ifdef _WIN32

    inline void throw_WASError(std::string const& what, int ec = ::WSAGetLastError())
    {
        throw std::system_error(ec, std::system_category(), what);
    }

    inline void throw_socket_error(std::string const& what, int ec = ::WSAGetLastError())
    {
        throw std::system_error(ec, std::system_category(), what);
    }

    inline void throw_LastError(std::string const& what)
    {
        throw std::system_error(::GetLastError(), std::system_category(), what);
    }
    inline void throw_LastError(char const* what)
    {
        DWORD ec = ::GetLastError();
        throw std::system_error(ec, std::system_category(), what);
    }

#else
    inline void throw_socket_error(std::string const& what, int ec = errno)
    {
        throw std::system_error(ec, std::system_category(), what);
    }

    inline void throw_errno(std::string const& what)
    {
        throw std::system_error(errno, std::system_category(), what);
    }
    inline void throw_errno(char const* what)
    {
        throw std::system_error(errno, std::system_category(), what);
    }
#endif


#if defined(_WIN32)
    struct IoEvent {
        DWORD transfered_bytes;
        union {
            void* user_data_per_handle;
            ULONG_PTR key;
        };
    };
#elif defined(__unix__)

    struct IoEvent : epoll_event
    {
    };

    std::string ioe2str(epoll_event& evt)
    {
        std::string str;
        str += ((evt.events & EPOLLIN) ? "EPOLLIN " : "");;
        str += ((evt.events & EPOLLPRI) ? "EPOLLPRI " : "");
        str += ((evt.events & EPOLLOUT) ? "EPOLLOUT " : "");
        str += ((evt.events & EPOLLRDNORM) ? "EPOLLRDNORM " : "");
        str += ((evt.events & EPOLLRDBAND) ? "EPOLLRDBAND " : "");
        str += ((evt.events & EPOLLWRBAND) ? "EPOLLWRBAND " : "");
        str += ((evt.events & EPOLLMSG) ? "EPOLLMSG " : "");
        str += ((evt.events & EPOLLERR) ? "EPOLLERR " : "");
        str += ((evt.events & EPOLLHUP) ? "EPOLLHUP " : "");
        str += ((evt.events & EPOLLRDHUP) ? "EPOLLRDHUP " : "");
        str += ((evt.events & EPOLLEXCLUSIVE) ? "EPOLLEXCLUSIVE " : "");
        str += ((evt.events & EPOLLWAKEUP) ? "EPOLLWAKEUP " : "");
        str += ((evt.events & EPOLLONESHOT) ? "EPOLLONESHOT " : "");
        str += ((evt.events & EPOLLET) ? "EPOLLET " : "");
        return str;
    }
#endif

    class ListNode
    {
    public:
        ListNode *m_next = nullptr;
    };

    struct Queue
    {
        ListNode m_before_head;
        ListNode *m_tail = nullptr;

#ifndef TINYASYNC_NDEBUG
        std::atomic<int> queue_size = 0;
#endif

        std::size_t count()
        {
            std::size_t n = 0;
            for (auto h = m_before_head.m_next; h; h = h->m_next)
            {
                n += 1;
            }
            return n;
        }

        // consume a dangling ndoe
        void push(ListNode *node)
        {
            TINYASYNC_ASSERT(node);
            node->m_next = nullptr;
            auto tail = this->m_tail;
            if (tail == nullptr)
            {
                tail = &m_before_head;
            }
            tail->m_next = node;
            this->m_tail = node;

#ifndef TINYASYNC_NDEBUG
            ++queue_size;
#endif
        }

        // return a dangling node
        // node->m_next is not meaningful
        ListNode *pop(bool &is_empty)
        {
            auto *before_head = &this->m_before_head;
            auto head = before_head->m_next;
            if(head) {
                auto new_head = head->m_next;
                before_head->m_next = new_head;
                bool is_empty_ = new_head == nullptr;
                if (is_empty_)
                {
    #ifndef TINYASYNC_NDEBUG
                    TINYASYNC_ASSERT(queue_size == 1);
    #endif
                    m_tail = nullptr;
                }

    #ifndef TINYASYNC_NDEBUG
                --queue_size;
    #endif
    
                is_empty = is_empty_;
            }
            return head;
        }

        // return a dangling node
        // node->m_next is not meaningful
        ListNode *pop_nocheck(bool &is_empty)
        {
            auto *before_head = &this->m_before_head;
            auto head = before_head->m_next;
            TINYASYNC_ASSERT(head);
            if(true) {
                auto new_head = head->m_next;
                before_head->m_next = new_head;
                bool is_empty_ = new_head == nullptr;
                if (is_empty_)
                {
    #ifndef TINYASYNC_NDEBUG
                    TINYASYNC_ASSERT(queue_size == 1);
    #endif
                    m_tail = nullptr;
                }

    #ifndef TINYASYNC_NDEBUG
                --queue_size;
    #endif
    
                is_empty = is_empty_;
            }
            return head;
        }
    };


    class TicketSpinLock
    {
    public:
        void lock()
        {
            const auto ticket_no = m_tail_ticket_no.fetch_add(1, std::memory_order_relaxed);
    
            while (m_head_ticket_no.load(std::memory_order_acquire) != ticket_no) {
                //
            }
        }
    
        void unlock()
        {
            const auto ticket_no = m_head_ticket_no.load(std::memory_order_relaxed)+1;
            m_head_ticket_no.store(ticket_no, std::memory_order_release);
        }
    
    private:
        std::atomic_size_t m_head_ticket_no = 0;
        std::atomic_size_t m_tail_ticket_no = 0;
    };


    class SysSpinLock {
        pthread_spinlock_t m_sys_spinlock;
    public:

        SysSpinLock() {
            pthread_spin_init(&m_sys_spinlock, PTHREAD_PROCESS_PRIVATE);
        }

        void lock() {
            pthread_spin_lock(&m_sys_spinlock);
        }

        void unlock() {
            pthread_spin_unlock(&m_sys_spinlock);
        }

        ~SysSpinLock() {
            pthread_spin_destroy(&m_sys_spinlock);
        }

    };

    struct NaitveLock
    {
        void lock() { }
        void unlock() { }
    };


    using DefaultSpinLock = SysSpinLock;

    [[noreturn]] void terminate_with_unhandled_exception() noexcept
    {
        fprintf(stderr, "exception: %s", to_string(std::current_exception()).c_str());
        std::terminate();
    }

}


#endif
