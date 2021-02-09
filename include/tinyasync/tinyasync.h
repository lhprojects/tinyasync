#ifndef TINYASYNC_H
#define TINYASYNC_H

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

using SystemHandle = int;

#endif

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


    void sleep_microseconds(uint64_t microseconds)
    {

        DWORD miliseconds;
        if (microseconds == UINT64_MAX) {
            miliseconds = INFINITE;
        } else {
            uint64_t miliseconds_ = (microseconds / 1000);
            assert((DWORD)(miliseconds_) == miliseconds_);
            miliseconds = (DWORD)(miliseconds_);
        }
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

    timespec to_timespec(uint64_t microseconds)
    {

        timespec time;
        auto seconds = microseconds / (1000 * 1000);
        auto nanos = (microseconds - seconds * (1000 * 1000)) * 1000;

        time.tv_sec = seconds;
        time.tv_nsec = nanos;

        return time;
    }

    void sleep_microseconds(uint64_t microseconds)
    {
        auto timespec = to_timespec(microseconds);
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
    T initialize_once_doinit(std::atomic<T>& atom, T never_called_falg, std::mutex& mtx, L func)
    {
        std::lock_guard<std::mutex> g(mtx);
        T t = atom.load(std::memory_order_relaxed);
        if (t == never_called_falg) {
            t = func();
            atom.store(t, std::memory_order_release);
        }
        return t;
    };

    template<class T, class L>
    TINYASYNC_VCINL T initialize_once(std::atomic<T>& atom, T never_called_falg, std::mutex& mtx, L func)
    {
        T t = atom.load(std::memory_order_acquire);
        if (t == never_called_falg) [[unlikely]] {
            t = initialize_once_doinit(atom, never_called_falg, mtx, func);
        }
        return t;
    };


    void sleep_seconds(uint64_t seconds)
    {
        sleep_microseconds(1000 * 1000 * seconds);
    }

    NativeHandle const NULL_HANDLE = 0;
    NativeSocket const NULL_SOCKET = NativeSocket(0);

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

    struct ResumeResult;
    class Task
    {
    public:
        struct Promise
        {

            ResumeResult* m_resume_result;

#ifdef TINYASYNC_TRACE
            static void* operator new(std::size_t size)
            {
                TINYASYNC_GUARD("Task.Promise.operator new(): ");
                auto ptr = ::operator new(size);
                TINYASYNC_LOG("%d bytes at %p", (int)size, ptr);
                return ptr;
            }

            static void operator delete(void* ptr, std::size_t size)
            {
                TINYASYNC_GUARD("Task.Promise.operator delete(): ");
                TINYASYNC_LOG("%d bytes at %p", (int)size, ptr);
                ::operator delete(ptr, size);
            }
#endif

            std::coroutine_handle<Promise> coroutine_handle()
            {
                return std::coroutine_handle<Promise>::from_promise(*this);
            }

            Task get_return_object()
            {
                auto h = std::coroutine_handle<Promise>::from_promise(*this);
                TINYASYNC_GUARD("Task(`%s`).Promise.get_return_object(): ", c_name(h));
                TINYASYNC_LOG("");
                return { h };
            }

            template <class... T>
            Promise(T const &...args)
            {
                auto h = std::coroutine_handle<Promise>::from_promise(*this);
                TINYASYNC_GUARD("Task(`%s`).Promise.Promise(): ", c_name(h));
                if (!set_name_r(h, args...)) {
                    TINYASYNC_LOG("");
                }
            }

            constexpr bool is_dangling() const
            {
                return m_dangling;
            }

            Promise(Promise&& r) = delete;
            Promise(Promise const& r) = delete;
            ~Promise()
            {
                auto h = std::coroutine_handle<Promise>::from_promise(*this);
                TINYASYNC_GUARD("Task(`%s`).Promise.~Promise(): ", c_name(h));
                TINYASYNC_LOG("");
            }

            struct InitialAwaityer : std::suspend_always
            {
                std::coroutine_handle<Promise> m_sub_coroutine;

                InitialAwaityer(std::coroutine_handle<Promise> h) : m_sub_coroutine(h)
                {
                }

                void await_suspend(std::coroutine_handle<> suspended_coroutine) const noexcept
                {
                    TINYASYNC_GUARD("Task(`%s`).InitialAwaityer.await_suspend(): ", c_name(m_sub_coroutine));
                    TINYASYNC_LOG("`%s` suspended, back to caller", c_name(suspended_coroutine));
                    // return to caller
                }

                void await_resume() const noexcept
                {
                    TINYASYNC_GUARD("Task(`%s`).InitialAwaityer.await_resume(): ", c_name(m_sub_coroutine));
                    TINYASYNC_LOG("`%s` resumed", c_name(m_sub_coroutine));
                }
            };

            InitialAwaityer initial_suspend()
            {
                return { std::coroutine_handle<Promise>::from_promise(*this) };
            }

            struct FinalAwaiter : std::suspend_always
            {
                Promise* m_promise;

                FinalAwaiter(Promise& promise) noexcept : m_promise(&promise)
                {
                }

                bool await_ready() noexcept { return false; }

                std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) const noexcept;

                void await_resume() const noexcept
                {
                    TINYASYNC_GUARD("Task(`?`).FinalAwaiter.await_resume(): ");
                    TINYASYNC_LOG("Bug!");
                    // never reach here
                    assert(false);
                }
            };

            FinalAwaiter final_suspend() noexcept
            {
                return { *this };
            }

            void unhandled_exception()
            {
                auto h = std::coroutine_handle<Promise>::from_promise(*this);
                TINYASYNC_GUARD("Task(`%s`).Promise.unhandled_exception(): ", c_name(h));
                TINYASYNC_LOG("%s", to_string(std::current_exception()).c_str());
                m_unhandled_exception = std::current_exception();
            }

            void return_void()
            {
            }

            std::exception_ptr m_unhandled_exception = nullptr;
            std::coroutine_handle<Task::Promise> m_continuum = nullptr;
            bool m_dangling = false;
        };

        using promise_type = Promise;
        std::coroutine_handle<promise_type> m_h;

        std::coroutine_handle<promise_type> coroutine_handle()
        {
            return m_h;
        }

        promise_type& promise()
        {
            return m_h.promise();
        }

        struct Awaiter
        {
            std::coroutine_handle<promise_type> m_sub_coroutine;

            Awaiter(std::coroutine_handle<promise_type> h) : m_sub_coroutine(h)
            {
            }
            bool await_ready() noexcept
            {
                return false;
            }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> suspend_coroutine);
            void await_resume();

        };

        // Resume coroutine
        // This is a help function for:
        // resume_coroutine(task.coroutine_handle())
        //
        // Return false, if coroutine is done. otherwise, return true
        // The coroutine is destroyed, if it is done and detached (inside of the coroutine)
        // E.g:
        // Task foo(Task **task) {  (*task)->detach(); }
        // Task *ptask;
        // Task task(&ptask);
        // ptask = &task;
        // task.resume();
        // the coroutine will is destoryed
        bool resume();

        Awaiter operator co_await()
        {
            return { m_h };
        }

        Task() : m_h(nullptr)
        {
        }

        Task(std::coroutine_handle<Promise> h) : m_h(h)
        {
            TINYASYNC_LOG("Task(`%s`).Task(): ", c_name(m_h));
        }

        Task(Task&& r) noexcept : m_h(std::exchange(r.m_h, nullptr))
        {
        }

        Task& operator=(Task&& r) noexcept
        {
            this->~Task();
            m_h = r.m_h;
            return *this;
        }

        ~Task()
        {
            if (m_h) {
                // unset_name();
                TINYASYNC_GUARD("Task(`%s`).~Task(): ", c_name(m_h));
                {
                    TINYASYNC_GUARD("coroutine_handle.destroy(): ");
                    TINYASYNC_LOG("");
                    m_h.destroy();
                }
            }
        }

        // Release the ownership of cocoutine.
        // Now you are responsible for destroying the coroutine.
        std::coroutine_handle<promise_type> release()
        {
            auto h = m_h;
            m_h = nullptr;
            return h;
        }

        // Release the ownership of coroutine.
        // Tell the coroutine it is detached.
        // Coroutine will destroy itself, when it is finished.
        std::coroutine_handle<promise_type> detach()
        {
            promise().m_dangling = true;
            return release();
        }


        Task(Task const& r) = delete;
        Task& operator=(Task const& r) = delete;
    };


    struct ResumeResult {
        std::coroutine_handle<Task::Promise> m_return_from;
    };

    inline std::coroutine_handle<> Task::Promise::FinalAwaiter::await_suspend(std::coroutine_handle<Promise> h) const noexcept
    {
        TINYASYNC_GUARD("Task.FinalAwaiter.await_suspend(): ");
        TINYASYNC_LOG("`%s` suspended", c_name(h));

        auto *promise = m_promise;
        auto continuum = promise->m_continuum;
        if (continuum) {
            // co_await ...
            // back to awaiter
            TINYASYNC_LOG("resume its continuum `%s`", c_name(m_promise->m_continuum));

            continuum.promise().m_resume_result = h.promise().m_resume_result;
            return continuum;
        } else {
            // directly .resume()
            // back to resumer
            TINYASYNC_LOG("resume its continuum `%s` (caller/resumer)", c_name(m_promise->m_continuum));

            // return to caller!
            // we need to set last coroutine
            promise->m_resume_result->m_return_from = h;
            return std::noop_coroutine();
        }
    }

    inline void destroy_and_throw_if_necessary_impl(std::coroutine_handle<Task::Promise> coroutine, char const* func)
    {

        Task::promise_type& promise = coroutine.promise();

        TINYASYNC_GUARD("`destroy_and_throw_if_necessary(): ");
        TINYASYNC_LOG("`%s` done", c_name(coroutine));

        if (coroutine.promise().m_unhandled_exception) [[ unlikely ]] {
            auto name_ = c_name(coroutine);
        // exception is reference counted
        auto unhandled_exception = promise.m_unhandled_exception;

        if (promise.is_dangling()) [[ unlikely ]] {
            coroutine.destroy();
        }

        try {
            std::rethrow_exception(unhandled_exception);
        } catch (...) {
         std::throw_with_nested(std::runtime_error(format("in function <%s>: `%s` thrown, rethrow", func, name_)));
        }

        } else {
            if (promise.is_dangling()) [[ unlikely ]] {
                coroutine.destroy();
            }
        }
    }

    inline bool destroy_and_throw_if_necessary(std::coroutine_handle<Task::Promise> coroutine, char const* func)
    {
        if (coroutine.done()) [[ unlikely ]] {
            destroy_and_throw_if_necessary_impl(coroutine, func);
            return true;
        }
        return false;
    }

    inline void throw_impl(std::coroutine_handle<Task::Promise> coroutine, char const* func)
    {

        Task::Promise& promise = coroutine.promise();
        TINYASYNC_GUARD("`throw_if_necessary(): ");
        TINYASYNC_LOG("`%s` exception", c_name(coroutine));
        auto name_ = c_name(coroutine);
        // exception is reference counted
        auto unhandled_exception = promise.m_unhandled_exception;
        try {
            std::rethrow_exception(promise.m_unhandled_exception);
        }
        catch (...) {
            std::throw_with_nested(std::runtime_error(format("in function <%s>: `%s` thrown, rethrow", func, name_)));
        }
    }

    inline bool throw_if_necessary(std::coroutine_handle<Task::Promise> coroutine, char const* func)
    {

        Task::promise_type& promise = coroutine.promise();
        if (coroutine.promise().m_unhandled_exception) TINYASYNC_UNLIKELY {
            throw_impl(coroutine, func);
            return true;
        }
        return false;

    }

    inline bool resume_coroutine(std::coroutine_handle<Task::Promise> coroutine, char const* func = "")
    {
        TINYASYNC_GUARD("resume_coroutine(): ");
        TINYASYNC_LOG("resume `%s`", c_name(coroutine));
        assert(coroutine);
        ResumeResult res;

        // awiaters except Task::Awaiter don't touch `m_return_from`
        // if no coroutine switch, the m_return_from will not change
        // initialize it with first coroutine
        res.m_return_from = coroutine;

        coroutine.promise().m_resume_result = &res;
        coroutine.resume();

        TINYASYNC_LOG("resumed from `%s`", c_name(res.m_return_from));
        return !destroy_and_throw_if_necessary(res.m_return_from, func);
    }

#define TINYASYNC_RESUME(coroutine)  resume_coroutine(coroutine, TINYASYNC_FUNCNAME)

    inline void co_spawn(Task task)
    {
        TINYASYNC_GUARD("co_spawn(): ");
        auto coroutine = task.coroutine_handle();
        task.detach();
        TINYASYNC_RESUME(coroutine);
    }

    inline bool Task::resume()
    {
        return resume_coroutine(this->coroutine_handle());
    }

    TINYASYNC_VCINL std::coroutine_handle<> Task::Awaiter::await_suspend(std::coroutine_handle<Task::Promise> suspend_coroutine)
    {
        TINYASYNC_GUARD("Task(`%s`).Awaiter.await_suspend(): ", c_name(m_sub_coroutine));

        TINYASYNC_LOG("set continuum of `%s` to `%s`", c_name(m_sub_coroutine), c_name(suspend_coroutine));
        TINYASYNC_LOG("`%s` suspended, resume `%s`", c_name(suspend_coroutine), c_name(m_sub_coroutine));

        auto sub_coroutine = m_sub_coroutine;
        sub_coroutine.promise().m_continuum = suspend_coroutine;
        sub_coroutine.promise().m_resume_result = suspend_coroutine.promise().m_resume_result;
        return sub_coroutine;
    }

    inline void Task::Awaiter::await_resume()
    {
        auto sub_coroutine = m_sub_coroutine;
        TINYASYNC_GUARD("Task(`%s`).Awaiter.await_resume(): ", c_name(sub_coroutine));
        assert(sub_coroutine.done());
        throw_if_necessary(sub_coroutine, TINYASYNC_FUNCNAME);
    }


#if defined(_WIN32)
    struct IoEvent {
        DWORD transfered_bytes;
        union {
            void* user_data_per_handle;
            ULONG_PTR key;
        };
    };

#elif defined(__unix__)

    using IoEvent = epoll_event;

    std::string ioe2str(IoEvent& evt)
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

    struct Callback
    {
        void callback(IoEvent &evt)
        {
            this->m_callback(this, evt);
        }

        // we don't use virtual table for two reasons
        //     1. virtual function let Callback to be non-standard_layout, though we have solution without offsetof using inherit
        //     2. we have only one function ptr, thus ... we can save a memory load without virtual functions table
        using CallbackPtr = void (*)(Callback* self, IoEvent&);

        CallbackPtr m_callback;

#if !defined(NDEBUG)
        char const* m_info = "not defined";
#endif

#ifdef _WIN32
        OVERLAPPED m_overlapped;

        static Callback* from_overlapped(OVERLAPPED* o)
        {
            constexpr std::size_t offset = offsetof(Callback, m_overlapped);
            Callback* callback = reinterpret_cast<Callback*>((reinterpret_cast<char*>(o) - offset));
            return callback;
        }
#endif
    };

    // requried by offsetof
    static_assert(std::is_standard_layout_v<Callback>);

    struct CallbackImplBase : Callback
    {

        // implicit null is not allowed
        CallbackImplBase(std::nullptr_t)
        {
            m_callback = nullptr;
        }

        template<class SubclassCallback>
        CallbackImplBase(SubclassCallback*)
        {
            (void)static_cast<SubclassCallback*>(this);
            //memset(&m_overlapped, 0, sizeof(m_overlapped));
            m_callback = &invoke_impl_callback<SubclassCallback>;
        }

        template<class SubclassCallback>
        static void invoke_impl_callback(Callback* this_, IoEvent &evt)
        {
            // invoke subclass' on_callback method
            SubclassCallback* subclass_this = static_cast<SubclassCallback*>(this_);
            subclass_this->on_callback(evt);
        }
    };


    class IoContext
    {

        NativeHandle m_native_handle = NULL_HANDLE;
        bool m_abort_requested = false;

    public:
        IoContext();

        std::list<Task> m_post_tasks;
        void post_task(Task task)
        {
            m_post_tasks.emplace_back(std::move(task));
        }

        IoContext(IoContext&& r)
        {
            this->m_native_handle = r.m_native_handle;
            r.m_native_handle = NULL_HANDLE;
        }

        IoContext& operator=(IoContext&& r)
        {
            this->~IoContext();
            this->m_native_handle = r.m_native_handle;
            r.m_native_handle = NULL_HANDLE;
            return *this;
        }

        ~IoContext()
        {
#ifdef _WIN32

            WSACleanup();
#elif defined(__unix__)
            close_handle(m_native_handle);
#endif
        }

        NativeHandle handle() const
        {
            return m_native_handle;
        }

        void request_abort()
        {
            m_abort_requested = true;
        }

        void run();

    };

    struct Protocol
    {

        static Protocol ip_v4()
        {
            return {};
        }
    };

    static_assert(is_trivial_parameter_in_itanium_abi<Protocol>::value);

    enum class AddressType {
        IpV4,
        IpV6,
    };

    struct Address
    {

        // ip_v32 host bytes order
        Address(uint32_t ip_v32)
        {
            // set the unused part to zero by the way
            m_v6 = ip_v32;
            m_address_type = AddressType::IpV4;
        }

        // ip_v64 host bytes order
        Address(uint64_t ip_v64)
        {
            m_v6 = ip_v64;
            m_address_type = AddressType::IpV6;
        }

        Address()
        {
            // if we bind on this address,
            // We will listen to all network card(s)
            m_v6 = INADDR_ANY;
            m_address_type = AddressType::IpV4;
        }

        std::string to_string() const
        {
            if (m_address_type == AddressType::IpV4) {
                char buf[256];

#if _WIN32
#elif defined(__unix__)
                // host long -> network long -> string
                // TODO: this is not thread safe
                in_addr addr;
                addr.s_addr = htonl(m_v4);
                inet_ntop(AF_INET, &addr, buf, sizeof(buf));
#endif
                return buf;
            } else if (m_address_type == AddressType::IpV6) {
                //TODO: implement
            }
            return "";
        }

        static Address Any()
        {
            return {};
        }

        union
        {
            uint32_t m_v4;
            uint64_t m_v6;
        };
        AddressType m_address_type;
    };
    static_assert(is_trivial_parameter_in_itanium_abi<Address>::value);
    static_assert(has_trivial_five<Address>::value);

    struct Endpoint
    {
        Endpoint(Address address, uint16_t port)
        {
            m_address = address;
            m_port = port;
        }
        Endpoint() : Endpoint(Address(), 0)
        {
        }

        Address m_address;
        uint16_t m_port;
    };
    static_assert(is_trivial_parameter_in_itanium_abi<Endpoint>::value);
    static_assert(has_trivial_five<Endpoint>::value);

    inline void setnonblocking(NativeSocket fd)
    {
#ifdef __WIN32
        //
#elif defined(__unix__)
        //int status = fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        int status = fcntl(fd, F_SETFL, O_NONBLOCK);

        if (status == -1) {
            throw std::system_error(errno, std::system_category(), "can't set fd nonblocking");
        }
#endif
    }


    inline NativeSocket open_socket(Protocol const& protocol, bool blocking = false)
    {
#ifdef TINYASYNC_THROW_ON_OPEN
        throw_error("throw on open", 0);
#endif
        TINYASYNC_GUARD("open_socket(): ");


#ifdef _WIN32

        DWORD flags = 0;
        if (!blocking) {
            flags = WSA_FLAG_OVERLAPPED;
        }

        auto socket_ = ::WSASocketW(AF_INET, SOCK_STREAM, 0, NULL, 0, flags);
        if (socket_ == INVALID_SOCKET) {
            throw_WASError("can't create socket");
        }

#elif defined(__unix__)
        // PF means protocol
        auto socket_ = ::socket(PF_INET, SOCK_STREAM, 0);
        if (socket_ == -1) {
            throw_errno("can't create socket");
        }
        if (!blocking)
            setnonblocking(socket_);
#endif

        TINYASYNC_LOG("create socket %s, nonblocking = %d", socket_c_str(socket_), int(!blocking));
        return socket_;

    }

    inline void bind_socket(NativeSocket socket, Endpoint const& endpoint)
    {

        TINYASYNC_GUARD("bind_socket(): ");
        TINYASYNC_LOG("socket = %s", socket_c_str(socket));


        sockaddr_in serveraddr;
        // do this! no asking!
        memset(&serveraddr, 0, sizeof(serveraddr));
        // AF means address
        serveraddr.sin_family = AF_INET;
        // hton*: host bytes order to network bytes order
        // *l: uint32
        // *s: uint16
        serveraddr.sin_port = htons(endpoint.m_port);
        serveraddr.sin_addr.s_addr = htonl(endpoint.m_address.m_v4);

        auto binderr = ::bind(socket, (sockaddr*)&serveraddr, sizeof(serveraddr));

#ifdef _WIN32

        if (binderr == SOCKET_ERROR) {
            throw_WASError(format("can't bind socket, socket = %x", socket));
        }

#elif defined(__unix__)

        if (binderr == -1) {
            throw_errno(format("can't bind socket, fd = %x", socket));
        }
#endif
    }


    template<class Awaiter, class Buffer>
    class DataAwaiterMixin {
    public:
        friend class ConnImpl;
        friend class ConnCallback;

        Awaiter* m_next;
        IoContext* m_ctx;
        ConnImpl* m_conn;
        std::coroutine_handle<Task::Promise> m_suspend_coroutine;
        Buffer m_buffer_addr;
        std::size_t m_buffer_size;
        std::size_t m_bytes_transfer;
#ifdef _WIN32
        WSABUF win32_single_buffer;
#endif
    };

    class AsyncReceiveAwaiter : public std::suspend_always,
        public DataAwaiterMixin<AsyncReceiveAwaiter, void*>
    {
    public:
        friend class ConnImpl;
        AsyncReceiveAwaiter(ConnImpl& conn, void* b, std::size_t n);
        bool await_suspend(std::coroutine_handle<Task::Promise> h);
        std::size_t await_resume();
    };

    class AsyncSendAwaiter : public std::suspend_always,
        public DataAwaiterMixin<AsyncSendAwaiter, void const*>
    {
    public:
        friend class ConnImpl;
        AsyncSendAwaiter(ConnImpl& conn, void const* b, std::size_t n);
        bool await_suspend(std::coroutine_handle<Task::Promise> h);
        std::size_t await_resume();
    };

    class ConnCallback : public CallbackImplBase
    {
    public:
        ConnImpl* m_conn;
        ConnCallback(ConnImpl* conn) : CallbackImplBase(this), m_conn(conn)
        {
        }
        void on_callback(IoEvent&);

    };

    class ConnImpl
    {
        friend class AsyncReceiveAwaiter;
        friend class AsyncSendAwaiter;
        friend class ConnCallback;


        IoContext* m_ctx;
        NativeSocket m_conn_handle;
        ConnCallback m_callback{ this };
        AsyncReceiveAwaiter* m_recv_awaiter = nullptr;
        AsyncSendAwaiter* m_send_awaiter = nullptr;
        bool m_added_to_event_pool = false;

    public:
        void reset()
        {
            if (m_conn_handle) {
                // ubind from epool ...
                close_socket(m_conn_handle);
                m_conn_handle = NULL_SOCKET;
            }
        }

        ConnImpl(IoContext& ctx, NativeSocket conn_sock, bool added_event_poll)
        {
            TINYASYNC_GUARD("Connection.Connection(): ");
            TINYASYNC_LOG("conn_socket %p", conn_sock);

            m_ctx = &ctx;
            m_conn_handle = conn_sock;
            m_added_to_event_pool = added_event_poll;

        }

        ConnImpl(ConnImpl&& r) = delete;
        ConnImpl& operator=(ConnImpl&& r) = delete;
        ConnImpl(ConnImpl const&) = delete;
        ConnImpl& operator=(ConnImpl const&) = delete;

        ~ConnImpl() noexcept
        {
            this->reset();
        }

        AsyncReceiveAwaiter async_read(void* buffer, std::size_t bytes)
        {
            return { *this, buffer, bytes };
        }

        AsyncSendAwaiter async_send(void const* buffer, std::size_t bytes)
        {
            TINYASYNC_GUARD("Connection.send(): ");
            TINYASYNC_LOG("try to send %d bytes", bytes);
            return { *this, buffer, bytes };
        }

    };

    void ConnCallback::on_callback(IoEvent& evt)
    {
        TINYASYNC_GUARD("ConnCallback.callback(): ");

#ifdef _WIN32

        if (m_conn->m_recv_awaiter) {
            assert(!m_conn->m_send_awaiter);
            auto awaiter = m_conn->m_recv_awaiter;
            awaiter->m_bytes_transfer = (size_t)evt.transfered_bytes;
            TINYASYNC_RESUME(awaiter->m_suspend_coroutine);

        } else if (m_conn->m_send_awaiter) {
            assert(!m_conn->m_recv_awaiter);
            auto awaiter = m_conn->m_send_awaiter;
            awaiter->m_bytes_transfer = (size_t)evt.transfered_bytes;
            TINYASYNC_RESUME(awaiter->m_suspend_coroutine);
        } else {
            assert(false);
            throw std::runtime_error("Why conn callback called");
        }

#elif defined(__unix__)

        if ((evt.events | EPOLLIN) && m_conn->m_recv_awaiter) {
            // we want to read and it's ready to read

            TINYASYNC_LOG("ready to read for conn_handle %d", m_conn->m_conn_handle);

            auto awaiter = m_conn->m_recv_awaiter;
            int nbytes = recv(m_conn->m_conn_handle, awaiter->m_buffer_addr, (int)awaiter->m_buffer_size, 0);

            if (nbytes < 0) {

                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    TINYASYNC_LOG("EAGAIN, fd = %d", m_conn->m_conn_handle);
                } else {
                    TINYASYNC_LOG("ERROR = %d, fd = %d", errno, m_conn->m_conn_handle);
                    throw_errno("recv error");
                }
            } else {
                if (nbytes == 0) {
                    if (errno == ESHUTDOWN) {
                        TINYASYNC_LOG("ESHUTDOWN, fd = %d", m_conn->m_conn_handle);
                    }
                }
                TINYASYNC_LOG("fd = %d, %d bytes read", m_conn->m_conn_handle, nbytes);
                awaiter->m_bytes_transfer = nbytes;

                // may cause Connection self deleted
                // so we should not want to read and send at the same
                TINYASYNC_RESUME(awaiter->m_suspend_coroutine);

            }

        } else if ((evt.events | EPOLLOUT) && m_conn->m_send_awaiter) {
            // we want to send and it's ready to read

            auto awaiter = m_conn->m_send_awaiter;
            TINYASYNC_LOG("ready to send for conn_handle %d, %d bytes at %p sending", m_conn->m_conn_handle, (int)awaiter->m_buffer_size, awaiter->m_buffer_addr);
            int nbytes = ::send(m_conn->m_conn_handle, awaiter->m_buffer_addr, (int)awaiter->m_buffer_size, 0);
            TINYASYNC_LOG("sent %d bytes", nbytes);

            if (nbytes < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    TINYASYNC_LOG("EAGAIN, fd = %d", m_conn->m_conn_handle);
                } else {
                    TINYASYNC_LOG("ERROR = %d, fd = %d", errno, m_conn->m_conn_handle);
                    throw_errno("send error");
                }
            } else {
                if (nbytes == 0) {
                    if (errno == ESHUTDOWN) {
                        TINYASYNC_LOG("ESHUTDOWN, fd = %d", m_conn->m_conn_handle);
                    }
                }

                TINYASYNC_LOG("fd = %d, %d bytes sent", m_conn->m_conn_handle, nbytes);
                awaiter->m_bytes_transfer = nbytes;

                // may cause Connection self deleted
                // so we should not want to read and send at the same
                TINYASYNC_RESUME(awaiter->m_suspend_coroutine);
            }
        } else {
            TINYASYNC_LOG("not processed event for conn_handle %x", errno, m_conn->m_conn_handle);
            exit(1);
        }
#endif

    }


    AsyncReceiveAwaiter::AsyncReceiveAwaiter(ConnImpl& conn, void* b, std::size_t n)
    {
        TINYASYNC_GUARD("AsyncReceiveAwaiter.AsyncReceiveAwaiter(): ");
        m_next = nullptr;
        m_ctx = conn.m_ctx;
        m_conn = &conn;
        m_buffer_addr = b;
        m_buffer_size = n;
        m_bytes_transfer = 0;
        TINYASYNC_LOG("conn: %p", m_conn);
    }

    bool AsyncReceiveAwaiter::await_suspend(std::coroutine_handle<Task::Promise> h)
    {
        TINYASYNC_GUARD("AsyncReceiveAwaiter.await_suspend(): ");
        TINYASYNC_LOG("try to receive %zu bytes from %s", m_buffer_size, socket_c_str(m_conn->m_conn_handle));
        m_suspend_coroutine = h;

        // insert into front of list
        this->m_next = m_conn->m_recv_awaiter;
        m_conn->m_recv_awaiter = this;
        TINYASYNC_LOG("set recv_awaiter of conn(%p) to %p", m_conn, m_conn->m_recv_awaiter);

#ifndef NDEBUG
        m_conn->m_callback.m_info = "recv";
#endif

#ifdef _WIN32

        DWORD flags = 0;
        win32_single_buffer.buf = (CHAR*)m_buffer_addr;
        win32_single_buffer.len = (ULONG)m_buffer_size;
        LPWSABUF                           lpBuffers = &win32_single_buffer;
        DWORD                              dwBufferCount = 1;
        LPDWORD                            lpNumberOfBytesRecvd = NULL;
        LPDWORD                            lpFlags = &flags;
        LPWSAOVERLAPPED                    lpOverlapped = &m_conn->m_callback.m_overlapped;
        LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine = NULL;

        memset(&m_conn->m_callback.m_overlapped, 0, sizeof(m_conn->m_callback.m_overlapped));

        if (::WSARecv(m_conn->m_conn_handle, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd,
            lpFlags, lpOverlapped, lpCompletionRoutine) == 0) {
            TINYASYNC_LOG("WSARecv completed");
            // completed, luckly
            // always use GetQueuedCompletionStatus
        } else {
            if (WSAGetLastError() == ERROR_IO_PENDING) {
                TINYASYNC_LOG("WSARecv ERROR_IO_PENDING");
            } else {
                throw_LastError("WSARecv failed:");
            }
        }
        return true;

#elif defined(__unix__)

        epoll_event evt;
        evt.data.ptr = &m_conn->m_callback;
        evt.events = EPOLLIN | EPOLLONESHOT;

        int epoll_clt_addmod;
        if(!m_conn->m_added_to_event_pool) {
            epoll_clt_addmod = EPOLL_CTL_ADD;
            TINYASYNC_LOG("epoll_ctl(EPOLL_CTL_ADD, EPOOLIN|EPOLLONESHOT) for conn_handle = %d", m_conn->m_conn_handle);
            m_conn->m_added_to_event_pool = true;
        } else {
            epoll_clt_addmod = EPOLL_CTL_MOD;
            TINYASYNC_LOG("epoll_ctl(EPOLL_CTL_MOD, EPOOLIN|EPOLLONESHOT) for conn_handle = %d", m_conn->m_conn_handle);
        }

        int clterr = epoll_ctl(m_ctx->handle(), epoll_clt_addmod, m_conn->m_conn_handle, &evt);
        if(clterr == -1) {
            TINYASYNC_LOG("epoll_ctl failed for conn_handle = %d", m_conn->m_conn_handle);
            throw_errno(format("can't epoll_clt for conn_handle = %s", socket_c_str(m_conn->m_conn_handle)));
        }
        return true;
#endif

    }


    std::size_t AsyncReceiveAwaiter::await_resume()
    {
        TINYASYNC_GUARD("AsyncReceiveAwaiter.await_resume(): ");

        // pop from front of list
        m_conn->m_recv_awaiter = m_conn->m_recv_awaiter->m_next;
        TINYASYNC_LOG("set recv_awaiter of conn(%p) to %p", m_conn, m_conn->m_recv_awaiter);

        return m_bytes_transfer;
    }

    AsyncSendAwaiter::AsyncSendAwaiter(ConnImpl& conn, void const* b, std::size_t n)
    {
        m_next = nullptr;
        m_ctx = conn.m_ctx;
        m_conn = &conn;
        m_buffer_addr = b;
        m_buffer_size = n;
        m_bytes_transfer = 0;
    }

    bool AsyncSendAwaiter::await_suspend(std::coroutine_handle<Task::Promise> h)
    {
        TINYASYNC_LOG("AsyncSendAwaiter::await_suspend(): ");
        m_suspend_coroutine = h;

        // insert front of list
        this->m_next = m_conn->m_send_awaiter;
        m_conn->m_send_awaiter = this;
        TINYASYNC_LOG("set send_awaiter of conn(%p) to %p", m_conn, m_conn->m_send_awaiter);

#ifndef NDEBUG
        m_conn->m_callback.m_info = "send";
#endif

#ifdef _WIN32

        DWORD flags = 0;
        win32_single_buffer.buf = (CHAR*)m_buffer_addr;
        win32_single_buffer.len = (ULONG)m_buffer_size;
        LPWSABUF                           lpBuffers = &win32_single_buffer;
        DWORD                              dwBufferCount = 1;
        LPDWORD                            lpNumberOfBytesRecvd = NULL;
        LPDWORD                            lpFlags = &flags;
        LPWSAOVERLAPPED                    lpOverlapped = &m_conn->m_callback.m_overlapped;
        LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine = NULL;

        memset(&m_conn->m_callback.m_overlapped, 0, sizeof(m_conn->m_callback.m_overlapped));

        if (::WSASend(m_conn->m_conn_handle, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd,
            flags, lpOverlapped, lpCompletionRoutine) == 0) {
            TINYASYNC_LOG("WSASend completed");
            // completed, luckly
            // always use GetQueuedCompletionStatus
            //return false;
        } else {
            if (WSAGetLastError() == ERROR_IO_PENDING) {
                TINYASYNC_LOG("WSASend ERROR_IO_PENDING");
            } else {
                throw_LastError("WSASend failed:");
            }
        }
        return true;

#elif defined(__unix__)
        epoll_event evt;
        evt.data.ptr = &m_conn->m_callback;
        evt.events = EPOLLOUT | EPOLLONESHOT;

        int epoll_clt_addmod;
        if(!m_conn->m_added_to_event_pool) {
            epoll_clt_addmod = EPOLL_CTL_ADD;
            TINYASYNC_LOG("epoll_ctl(EPOLL_CTL_ADD, EPOLLOUT|EPOLLONESHOT) for conn_handle = %d", m_conn->m_conn_handle);
            m_conn->m_added_to_event_pool = true;
        } else {
            epoll_clt_addmod = EPOLL_CTL_MOD;
            TINYASYNC_LOG("epoll_ctl(EPOLL_CTL_MOD, EPOLLOUT|EPOLLONESHOT) for conn_handle = %d", m_conn->m_conn_handle);
        }

        int clterr = epoll_ctl(m_ctx->handle(), epoll_clt_addmod, m_conn->m_conn_handle, &evt);
        if(clterr == -1) {
            TINYASYNC_LOG("epoll_ctl failed for conn_handle = %d", m_conn->m_conn_handle);
            throw_errno(format("can't epoll_clt for conn_handle = %s", socket_c_str(m_conn->m_conn_handle)));
        }
        return true;
#endif
    }

    std::size_t AsyncSendAwaiter::await_resume()
    {
        TINYASYNC_GUARD("AsyncSendAwaiter.await_resume(): ");
        // pop from front of list
        m_conn->m_send_awaiter = m_conn->m_send_awaiter->m_next;
        TINYASYNC_LOG("set recv_awaiter of conn(%p) to %p", m_conn, m_conn->m_send_awaiter);

        return m_bytes_transfer;
    }

    class Connection
    {
        // use unique_ptr because
        // 1. fast move/swap
        // 2. reference stable, usefull for callbacks
        std::unique_ptr<ConnImpl> m_impl;
    public:
        Connection(IoContext& ctx, NativeSocket conn_sock, bool added_event_poll)
        {
            m_impl.reset(new ConnImpl(ctx, conn_sock, added_event_poll));
        }

        AsyncReceiveAwaiter async_read(void* buffer, std::size_t bytes)
        {
            return m_impl->async_read(buffer, bytes);
        }

        AsyncSendAwaiter async_send(void const* buffer, std::size_t bytes)
        {
            return m_impl->async_send(buffer, bytes);
        }

    };



    class SocketMixin {

    protected:
        friend class AsyncReceiveAwaiter;
        friend class AsyncSendAwaiter;

        IoContext* m_ctx;
        Protocol m_protocol;
        Endpoint m_endpoint;
        NativeSocket m_socket;
        bool m_added_to_event_pool;

    public:


        NativeSocket native_handle() const noexcept
        {
            return m_socket;

        }

        SocketMixin(IoContext& ctx)
        {
            m_ctx = &ctx;
            m_socket = NULL_SOCKET;
            m_added_to_event_pool = false;

        }

        void open(Protocol const& protocol, bool blocking = false)
        {
            m_socket = open_socket(protocol, blocking);
            m_protocol = protocol;
        }


        void reset()
        {
            TINYASYNC_GUARD("SocketMixin.reset(): ");
            if (m_socket) {
#ifdef _WIN32
                //
#elif defined(__unix__)
                if (m_added_to_event_pool) {
                    auto ctlerr = epoll_ctl(m_ctx->handle(), EPOLL_CTL_DEL, m_socket, NULL);
                    if (ctlerr == -1) {
                        auto what = format("can't remove (from epoll) socket %x", m_socket);
                        throw_errno(what);
                    }
                }
#endif
                TINYASYNC_LOG("close socket = %s", socket_c_str(m_socket));
                close_socket(m_socket);
                m_socket = NULL_SOCKET;
            }
        }
    };


    class AcceptorCallback : public CallbackImplBase
    {
        friend class AcceptorImpl;
        AcceptorImpl* m_acceptor;
    public:
        AcceptorCallback(AcceptorImpl* acceptor) : CallbackImplBase(this)
        {
            m_acceptor = acceptor;
        };

        void on_callback(IoEvent& evt);

    };

    class AcceptorAwaiter
    {

        friend class AcceptorImpl;
        friend class AcceptorCallback;

        AcceptorImpl* m_acceptor;
        std::coroutine_handle<Task::Promise> m_suspend_coroutine;

    public:
        bool await_ready() { return false; }
        AcceptorAwaiter(AcceptorImpl& acceptor);
        bool await_suspend(std::coroutine_handle<Task::Promise> h);
        Connection await_resume();
    };

    class AcceptorImpl : SocketMixin
    {
        friend class AcceptorAwaiter;
        friend class AcceptorCallback;
        AcceptorAwaiter* m_awaiter = nullptr;
        AcceptorCallback m_callback = this;

#ifdef _WIN32
        NativeSocket m_accept_socket = NULL_SOCKET;
        LPFN_ACCEPTEX m_lpfnAcceptEx = NULL;
        char m_accept_buffer[(sizeof(sockaddr_in) + 16) * 2];
#endif


    public:
        AcceptorImpl(IoContext& ctx) : SocketMixin(ctx)
        {
        }

        AcceptorImpl(IoContext& ctx, Protocol const& protocol, Endpoint const& endpoint) : AcceptorImpl(ctx)
        {
            try {
                // one effort triple successes
                open(protocol);
                bind_socket(m_socket, endpoint);
                m_endpoint = endpoint;
                listen();
            }
            catch (...) {
                reset();
                auto what = format("open/bind/listen failed %s:%d", endpoint.m_address.to_string().c_str(), (int)(unsigned)endpoint.m_port);
                std::throw_with_nested(std::runtime_error(what));
            }
        }

        AcceptorImpl(AcceptorImpl&& r) = delete;
        AcceptorImpl(AcceptorImpl const&) = delete;
        AcceptorImpl& operator=(AcceptorImpl&& r) = delete;
        AcceptorImpl& operator=(AcceptorImpl const&) = delete;
        ~AcceptorImpl()
        {
            TINYASYNC_GUARD("AcceptorImpl::~AcceptorImpl(): ");
            reset();            
        }

        void listen()
        {
            TINYASYNC_GUARD("Acceptor.listen(): ");
            TINYASYNC_LOG("socket = %s, address = %X, port = %d", socket_c_str(m_socket), m_endpoint.m_address.m_v4, m_endpoint.m_port);

            int max_pendding_connection = 5;
            int err = ::listen(m_socket, max_pendding_connection);

#ifdef _WIN32
            if (err == SOCKET_ERROR) {
                throw_WASError(format("can't listen socket, socket = %s", socket_c_str(m_socket)));
            }

            // prepare Accept function
            NativeSocket listen_socket = m_socket;
            DWORD dwBytes;
            GUID GuidAcceptEx = WSAID_ACCEPTEX;
            int iResult = ::WSAIoctl(listen_socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
                &GuidAcceptEx, sizeof(GuidAcceptEx),
                &m_lpfnAcceptEx, sizeof(m_lpfnAcceptEx),
                &dwBytes, NULL, NULL);

            if (iResult == SOCKET_ERROR) {
                throw_WASError("can't get AcceptEx function pointer");
            }
            assert(m_lpfnAcceptEx);


            ULONG_PTR key = m_socket;
            HANDLE iocp = ::CreateIoCompletionPort((NativeHandle)listen_socket, m_ctx->handle(), key, 0);
            if (iocp == NULL) {
                throw_LastError("can't create IoCompletionPort");
            }


#elif defined(__unix__)

            if (err == -1) {
                throw_errno("can't listen socket");
            }
#endif

        }


        AcceptorAwaiter async_accept()
        {
            return { *this };
        }
    };


    AcceptorAwaiter::AcceptorAwaiter(AcceptorImpl& acceptor) : m_acceptor(&acceptor)
    {
    }

    bool AcceptorAwaiter::await_suspend(std::coroutine_handle<Task::Promise> h)
    {
        assert(!m_acceptor->m_awaiter);
        m_acceptor->m_awaiter = this;
        m_suspend_coroutine = h;
        TINYASYNC_GUARD("AcceptorAwaiter::await_suspend(): ");

#ifndef NDEBUG
        m_acceptor->m_callback.m_info = "send";
#endif

#ifdef _WIN32

        NativeSocket listen_socket = m_acceptor->m_socket;
        NativeSocket conn_socket = open_socket(m_acceptor->m_protocol, false);
        m_acceptor->m_accept_socket = conn_socket;

        PVOID        lpOutputBuffer = m_acceptor->m_accept_buffer;
        DWORD        dwReceiveDataLength = 0;
        DWORD        dwLocalAddressLength = sizeof(sockaddr_in) + 16;
        DWORD        dwRemoteAddressLength = sizeof(sockaddr_in) + 16;
        LPDWORD      lpdwBytesReceived = NULL;

        memset(&m_acceptor->m_callback.m_overlapped, 0, sizeof(m_acceptor->m_callback.m_overlapped));
        BOOL accret = m_acceptor->m_lpfnAcceptEx(listen_socket,
            conn_socket,
            lpOutputBuffer,
            dwReceiveDataLength,
            dwLocalAddressLength,
            dwRemoteAddressLength,
            lpdwBytesReceived,
            &m_acceptor->m_callback.m_overlapped);

        if (accret) TINYASYNC_UNLIKELY {
                TINYASYNC_LOG("AcceptEx completed");
        } else {
            if (WSAGetLastError() == ERROR_IO_PENDING) TINYASYNC_LIKELY {
                TINYASYNC_LOG("AcceptEx io_pending");
            } else {
                throw_LastError("AcceptEx failed");
            }
        }
        return true;
#elif defined(__unix__)

        int listenfd = m_acceptor->m_socket;

        epoll_event evt;
        evt.data.ptr = &m_acceptor->m_callback;

        if (!m_acceptor->m_added_to_event_pool) {
            // level triger by default
            // one thread one event
            evt.events = EPOLLIN | EPOLLEXCLUSIVE;
            auto ctlerr = epoll_ctl(m_acceptor->m_ctx->handle(), EPOLL_CTL_ADD, listenfd, &evt);
            if (ctlerr == -1) {
                TINYASYNC_LOG("can't listen %s", socket_c_str(listenfd));
                throw_errno(format("can't listen %x", listenfd));
            }
            m_acceptor->m_added_to_event_pool = true;
        }

        return true;
#endif

    }

    // connection is set nonblocking
    Connection AcceptorAwaiter::await_resume()
    {
        TINYASYNC_GUARD("AcceptorAwaiter.await_resume(): ");
        TINYASYNC_LOG("acceptor %p, awaiter %p", &m_acceptor, m_acceptor->m_awaiter);

        NativeSocket conn_sock = NULL_SOCKET;
#ifdef _WIN32
        conn_sock = m_acceptor->m_accept_socket;

        ULONG_PTR key = conn_sock;
        HANDLE iocp = ::CreateIoCompletionPort((NativeHandle)conn_sock, m_acceptor->m_ctx->handle(), key, 0);
        if (iocp == NULL) {
            TINYASYNC_LOG("can't create iocp for accepted connection");
            throw_LastError("can't create IoCompletionPort");
        }
#elif defined(__unix__)

        // it's ready to accept
        conn_sock = ::accept(m_acceptor->m_socket, NULL, NULL);
        if (conn_sock == -1) {
            throw_errno(format("can't accept %s", socket_c_str(conn_sock)));
        }
        TINYASYNC_LOG("accepted, socket = %s", socket_c_str(conn_sock));

        TINYASYNC_LOG("setnonblocking, socket = %s", socket_c_str(conn_sock));
        setnonblocking(conn_sock);

#endif
        m_acceptor->m_awaiter = nullptr;
        assert(conn_sock != NULL_SOCKET);
        return { *m_acceptor->m_ctx, conn_sock, false};
    }



    void AcceptorCallback::on_callback(IoEvent& evt)
    {
        TINYASYNC_GUARD("AcceptorCallback.callback(): ");
        TINYASYNC_LOG("acceptor %p, awaiter %p", m_acceptor, m_acceptor->m_awaiter);
        AcceptorAwaiter* awaiter = m_acceptor->m_awaiter;
        if (awaiter) {
            TINYASYNC_RESUME(awaiter->m_suspend_coroutine);
        } else {
            // this will happen when after accetor.listen() but not yet co_await acceptor.async_accept()
            // you should have been using level triger to get this event the next time
            TINYASYNC_LOG("No awaiter found, event ignored");
        }
    }



    class Acceptor {

    public:
        Acceptor(IoContext& ctx, Protocol protocol, Endpoint endpoint)
        {
            m_impl.reset(new AcceptorImpl(ctx, protocol, endpoint));
        }

        AcceptorAwaiter async_accept()
        {
            return m_impl->async_accept();
        }

    private:
        std::unique_ptr<AcceptorImpl> m_impl;
    };
























    class ConnectorCallback : public CallbackImplBase
    {
        friend class ConnectorAwaiter;
        friend class ConnectorImpl;
        ConnectorImpl* m_connector;


    public:
        ConnectorCallback(ConnectorImpl& connector) : CallbackImplBase(this),
            m_connector(&connector)
        {
        }
        void on_callback(IoEvent& evt);

    };


    class ConnectorAwaiter {
        friend class ConnectorCallback;

        ConnectorImpl* m_connector;
        std::coroutine_handle<Task::Promise> m_suspend_coroutine;
        void unregister(NativeSocket conn_handle);

    public:
        ConnectorAwaiter(ConnectorImpl& connector) : m_connector(&connector)
        {
        }

        constexpr bool await_ready() const { return false; }

        bool await_suspend(std::coroutine_handle<Task::Promise> suspend_coroutine);

        Connection await_resume();

    };


    class ConnectorImpl : public SocketMixin {

        friend class ConnectorAwaiter;
        friend class ConnectorCallback;
        ConnectorAwaiter* m_awaiter = nullptr;
        ConnectorCallback m_callback = { *this };
    public:

        ConnectorImpl(IoContext& ctx) : SocketMixin(ctx)
        {
        }

        ConnectorImpl(IoContext& ctx, Protocol const& protocol, Endpoint const& endpoint) : ConnectorImpl(ctx)
        {
            try {
                this->open(protocol);
                m_endpoint = endpoint;
            }
            catch (...) {
                this->reset();
                throw;
            }
        }

        ConnectorImpl(ConnectorImpl const& r) :
            SocketMixin(static_cast<SocketMixin const&>(r)),
            m_callback(*this)
        {
            m_awaiter = r.m_awaiter;
            // don't copy, m_callback is fine
            // m_callback = r.m_callback;
        }

        ConnectorImpl& operator=(ConnectorImpl const& r)
        {
            static_cast<SocketMixin&>(*this) = static_cast<SocketMixin const&>(r);
            m_awaiter = r.m_awaiter;
            m_callback = r.m_callback;
            m_callback.m_connector = this;
            return *this;
        }

        // don't reset
        ~ConnectorImpl() = default;

        ConnectorAwaiter async_connect()
        {
            return { *this };
        }

        ConnectorAwaiter operator co_await()
        {
            return async_connect();
        }

    private:

        inline static std::mutex s_mutex;

        bool connect(Endpoint const& endpoint)
        {

            TINYASYNC_GUARD("Connector::connect():");

            sockaddr_in serveraddr;
            memset(&serveraddr, 0, sizeof(serveraddr));
            // AF means address
            serveraddr.sin_family = AF_INET;
            // hton*: host bytes order to network bytes order
            // *l: uint32
            // *s: uint16
            serveraddr.sin_port = htons(endpoint.m_port);
            serveraddr.sin_addr.s_addr = htonl(endpoint.m_address.m_v4);

            bool connected = false;

#ifdef _WIN32

            NativeSocket conn_socket = m_socket;

            bind_socket(conn_socket, Endpoint(Address::Any(), 0));

            static std::atomic<LPFN_CONNECTEX> s_ConnectEx;
            auto LpfnConnectex = initialize_once(s_ConnectEx, (LPFN_CONNECTEX)nullptr, s_mutex, [&]() -> LPFN_CONNECTEX {

                // prepare Accept function
                DWORD dwBytes;
                GUID GuidConnectEx = WSAID_CONNECTEX;
                LPFN_CONNECTEX LpfnConnectex;
                int iResult = ::WSAIoctl(conn_socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
                    &GuidConnectEx, sizeof(GuidConnectEx),
                    &LpfnConnectex, sizeof(LpfnConnectex),
                    &dwBytes, NULL, NULL);

                if (iResult == SOCKET_ERROR) {
                    throw_WASError("can't get AcceptEx function pointer");
                }
                return LpfnConnectex;
            });

            assert(LpfnConnectex);

            ULONG_PTR key = m_socket;
            HANDLE iocp = ::CreateIoCompletionPort((NativeHandle)m_socket, m_ctx->handle(), key, 0);
            if (iocp == NULL) {
                TINYASYNC_LOG("can't create iocp");
                throw_LastError("can't create IoCompletionPort");
            }

            memset(&m_callback.m_overlapped, 0, sizeof(m_callback.m_overlapped));

            PVOID lpSendBuffer = NULL;
            DWORD dwSendDataLength = 0;
            LPDWORD lpdwBytesSent = NULL;
            BOOL connerr = LpfnConnectex(conn_socket, (sockaddr*)&serveraddr, sizeof(serveraddr),
                lpSendBuffer, dwSendDataLength, lpdwBytesSent,
                &m_callback.m_overlapped);


            if (connerr) TINYASYNC_UNLIKELY {
                TINYASYNC_LOG("ConnectEx complemented");
            } else {
                int err;
                if ((err=WSAGetLastError()) == ERROR_IO_PENDING) TINYASYNC_LIKELY{
                    TINYASYNC_LOG("ConnectEx io_pending");
                } else {
                    throw_WASError("ConnectEx failed:");
                }
            }

#elif defined(__unix__)
            NativeSocket connfd = m_socket;
            auto connerr = ::connect(connfd, (sockaddr*)&serveraddr, sizeof(serveraddr));
            if (connerr == -1) {
                if (errno == EINPROGRESS) {
                    TINYASYNC_LOG("EINPROGRESS, conn_handle = %X", connfd);
                    // just ok ...
                } else {
                    throw_errno(format("can't connect, conn_handle = %X", connfd));
                }
            } else if (connerr == 0) {
                TINYASYNC_LOG("connected, conn_handle = %X", connfd);
                // it's connected ??!!
                connected = true;
            }
#endif


            TINYASYNC_LOG("conn_handle = %s", socket_c_str(m_socket));
            m_endpoint = endpoint;
            return connected;
        }


    };


    ConnectorImpl async_connect(IoContext& ctx, Protocol const& protocol, Endpoint const& endpoint)
    {
        return ConnectorImpl(ctx, protocol, endpoint);
    }

    void ConnectorCallback::on_callback(IoEvent& evt)
    {
        TINYASYNC_GUARD("ConnectorCallback::callback():");
        assert(m_connector);
        assert(m_connector->m_awaiter);

        auto connfd = m_connector->m_socket;

#ifdef _WIN32
        //
#elif defined(__unix__)
        if (evt.events & (EPOLLERR | EPOLLHUP)) {
            throw_errno(format("error, fd = %d", connfd));
        }

        int result;
        socklen_t result_len = sizeof(result);
        if (getsockopt(connfd, SOL_SOCKET, SO_ERROR, &result, &result_len) < 0) {
            throw_errno(format("getsockopt failed for conn_handle = %d", m_connector->m_socket));
        }
        if (result != 0) {
            if (errno == EINPROGRESS) {
                TINYASYNC_LOG("EINPROGRESS, fd = %d", connfd);
                return;
            } else {
                TINYASYNC_LOG("bad!, fd = %d", connfd);
                throw_errno(format("connect failed for conn_handle = %d", m_connector->m_socket));
            }
        }
#endif

        TINYASYNC_LOG("connected");
        TINYASYNC_RESUME(m_connector->m_awaiter->m_suspend_coroutine);

    }


    void ConnectorAwaiter::unregister(NativeSocket conn_handle)
    {
        auto connfd = conn_handle;
        m_connector->m_awaiter = nullptr;

#ifdef _WIN32
        //
#elif defined(__unix__)
        //auto clterr = epoll_ctl(m_connector->m_ctx->handle(), EPOLL_CTL_DEL, connfd, NULL);
        //if (clterr == -1) {
        //    throw_errno(format("can't unregister conn_handle = %d", connfd));
        //}
#endif

        TINYASYNC_LOG("unregister connect for conn_handle = %s", socket_c_str(connfd));
    }

    bool ConnectorAwaiter::await_suspend(std::coroutine_handle<Task::Promise> suspend_coroutine)
    {
        TINYASYNC_LOG("ConnectorAwaiter::await_suspend():");
        m_suspend_coroutine = suspend_coroutine;
        NativeSocket connfd = m_connector->m_socket;

        assert(!m_connector->m_awaiter);
        m_connector->m_awaiter = this;

#ifdef _WIN32
        //
#elif defined(__unix__)
        epoll_event evt;
        evt.data.ptr = &m_connector->m_callback;
        // level triger by default
        evt.events = EPOLLOUT;
        auto clterr = epoll_ctl(m_connector->m_ctx->handle(), EPOLL_CTL_ADD, m_connector->m_socket, &evt);
        if (clterr == -1) {
            throw_errno(format("epoll_ctl(EPOLLOUT) failed for conn_handle = %d", m_connector->m_socket));
        }
#endif

        TINYASYNC_LOG("register connect for conn_handle = %d", m_connector->m_socket);

        try {
            if (m_connector->connect(m_connector->m_endpoint)) {
                return false;
            }
            return true;
        } catch (...) {
            std::throw_with_nested(std::runtime_error("can't connect"));
        }
    }


    Connection ConnectorAwaiter::await_resume()
    {
        TINYASYNC_GUARD("ConnectorAwaiter::await_resume():");
        auto connfd = m_connector->m_socket;
        this->unregister(connfd);
        // added in event poll, because of connect
        return { *m_connector->m_ctx, m_connector->m_socket, true};
    }

    class TimerAwaiter;

    class TimerCallback : public CallbackImplBase
    {
    public:
        friend class TimerAwaiter;
        TimerAwaiter* m_awaiter;

        TimerCallback(TimerAwaiter* awaiter) : CallbackImplBase(this), m_awaiter(awaiter)
        {
        }
        void on_callback(IoEvent& evt);

    };

    class TimerAwaiter
    {
    public:
        friend class TimerCallback;
    private:
        IoContext* m_ctx;
        uint64_t m_elapse; // mili-second
        NativeHandle m_timer_handle = NULL_HANDLE;
        std::coroutine_handle<Task::Promise> m_suspend_coroutine;
        TimerCallback m_callback = this;

    public:
        // elapse in micro-second
        TimerAwaiter(IoContext& ctx, uint64_t elapse) : m_ctx(&ctx), m_elapse(elapse)
        {
        }

        constexpr bool await_ready() const noexcept { return false; }
        void await_resume() const noexcept {
#ifdef _WIN32
            // timer thread have done cleaning up
#elif defined(__unix__)
            // remove from epoll list
            epoll_ctl(m_ctx->handle(), m_timer_handle, EPOLL_CTL_DEL, NULL);
            close(m_timer_handle);
#endif
        }

#ifdef _WIN32

        static void CALLBACK onTimeOut(LPVOID lpArg, DWORD dwTimerLowValue, DWORD dwTimerHighValue)
        {
            UNREFERENCED_PARAMETER(dwTimerLowValue);
            UNREFERENCED_PARAMETER(dwTimerHighValue);

            auto awaiter = (TimerAwaiter*)lpArg;
            CloseHandle(awaiter->m_timer_handle);

            constexpr DWORD numDataTransfered = 0;
            const auto poverlapped = &awaiter->m_callback.m_overlapped;
            constexpr ULONG_PTR key = 0;

            memset(&awaiter->m_callback.m_overlapped, 0, sizeof(awaiter->m_callback.m_overlapped));
            // tell the io thread time out
            PostQueuedCompletionStatus(awaiter->m_ctx->handle(), numDataTransfered, key, poverlapped);
        };

        // run on timer thread
        static void CALLBACK onQueueUserAPC(ULONG_PTR Parameter)
        {
            TimerAwaiter* awaiter = (TimerAwaiter*)Parameter;

            HANDLE timer_handle = CreateWaitableTimerW(NULL, FALSE, NULL);
            // false sharing
            awaiter->m_timer_handle = timer_handle;

            if (timer_handle != NULL_HANDLE) {
                LARGE_INTEGER elapse;
                // 10 * 100ns = 1us
                elapse.QuadPart = -10 * awaiter->m_elapse;

                SetWaitableTimer(timer_handle, &elapse, 0, onTimeOut, (PVOID)awaiter, FALSE);
            } else {
                throw_LastError("can't create timer");
            }
        }
        static DWORD TimerThradProc(LPVOID lpParameter)
        {
            TINYASYNC_GUARD("[timer thread]: ");


            for (;;) {
                TINYASYNC_LOG("sleeping ...");
                // get thread into sleep
                // in thi state, the callback get a chance to be invoked
                constexpr bool alertable = true;
                ::SleepEx(INFINITE, alertable);
                TINYASYNC_LOG("wake up");
            }
        };

        struct TimerThread {

            std::mutex m_mutex;
            std::atomic<void*> m_timer_thread_handle = NULL_HANDLE;

            HANDLE initialize() noexcept
            {
                HANDLE thread_handle = ::CreateThread(
                    NULL,                   // default security attributes
                    0,                      // use default stack size  
                    TimerThradProc,         // thread function name
                    NULL,                   // argument to thread function 
                    0,                      // use default creation flags 
                    NULL);                  // returns the thread identifier
                return thread_handle;
            }

            inline HANDLE get_timer_thread_handle()
            {
                return initialize_once(m_timer_thread_handle, (void*)NULL_HANDLE, m_mutex, [&]() {
                    return initialize();
                });
            }

        };

        inline static TimerThread timer_thread;

        inline void await_suspend(std::coroutine_handle<Task::Promise> h)
        {
            HANDLE thread_handle = timer_thread.get_timer_thread_handle();
            if (thread_handle == NULL_HANDLE) {
                throw_LastError("can't create thread");
            }

            m_suspend_coroutine = h;
            if (QueueUserAPC(onQueueUserAPC, thread_handle, (ULONG_PTR)this) == 0) [[unlikely]] {
                throw_LastError("can't post timer to timer thread");
            }
        }

#elif defined(__unix__)

        inline void await_suspend(std::coroutine_handle<Task::Promise> h)
        {

            // create a timer
            m_suspend_coroutine = h;
            itimerspec time;
            time.it_value = to_timespec(m_elapse);
            time.it_interval = to_timespec(0);

            auto fd = timerfd_create(CLOCK_REALTIME, 0);
            if (fd == -1) {
                throw_errno("can't create timer");
            }

            try {
                auto fd2 = timerfd_settime(fd, 0, &time, NULL);
                if (fd2 == -1) {
                    throw_errno("can't set timer");
                }

                auto epfd = m_ctx->handle();
                assert(epfd);

                epoll_event evt;
                evt.data.ptr = &m_callback;
                evt.events = EPOLLIN;
                auto fd3 = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &evt);

                if (fd3 == -1) {
                    throw_errno("can't bind timer fd with epoll");
                }

            }
            catch (...) {
                close_handle(m_timer_handle);
                throw;
            }
            m_timer_handle = fd;
        }

#endif
    };


    void TimerCallback::on_callback(IoEvent& evt)
    {
        TINYASYNC_RESUME(m_awaiter->m_suspend_coroutine);
    }

    IoContext::IoContext()
    {

        TINYASYNC_GUARD("IoContext.IoContext(): ");

#ifdef _WIN32

        WSADATA wsaData;
        int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != NO_ERROR) {
            throw_WASError("WSAStartup failed with error ", iResult);
        }

        int num_thread = 1;
        m_native_handle = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, num_thread);
        if (m_native_handle == NULL) {
            throw_LastError("can't create event poll");
        }

#elif defined(__unix__)

        auto fd = epoll_create1(EPOLL_CLOEXEC);
        if (fd == -1) {
            throw_errno("IoContext().IoContext(): can't create epoll");
        }
        m_native_handle = fd;

#endif
        TINYASYNC_LOG("event poll created");
    }

    void IoContext::run()
    {
        TINYASYNC_GUARD("IoContex::run(): ");

        for (; !m_abort_requested;) {

            if (!m_post_tasks.empty()) {
                auto task = std::move(m_post_tasks.front());
                m_post_tasks.pop_front();
                co_spawn(std::move(task));
            } else {

                TINYASYNC_LOG("waiting event ...");

#ifdef _WIN32


                IoEvent evt;
                OVERLAPPED* overlapped;

                if (::GetQueuedCompletionStatus(m_native_handle,
                    &evt.transfered_bytes,
                    &evt.key,
                    &overlapped, INFINITE) == 0) {
                    throw_LastError("GetQueuedCompletionStatus failed");
                }
                TINYASYNC_LOG("Get one event");
                Callback* callback = Callback::from_overlapped(overlapped);
                callback->callback(evt);

#elif defined(__unix__)

                int const maxevents = 5;
                epoll_event events[maxevents];
                auto epfd = m_native_handle;
                int const timeout = -1; // indefinitely

                int nfds = epoll_wait(epfd, (epoll_event*)events, maxevents, timeout);

                if (nfds == -1) {
                    throw_errno("epoll_wait error");
                }

                for (auto i = 0; i < nfds; ++i) {
                    auto& evt = events[i];
                    TINYASYNC_LOG("event %d of %d", i, nfds);
                    TINYASYNC_LOG("event = %x (%s)", evt.events, ioe2str(evt).c_str());
                    auto callback = (Callback*)evt.data.ptr;
                    try {
                        callback->callback(evt);
                    } catch (...) {
                        printf("exception: %s", to_string(std::current_exception()).c_str());
                        printf("ignored\n");
                    }
                }
#endif

            }
        } // for
    } // run

    // elapse in micro-second
    TimerAwaiter async_sleep(IoContext& ctx, uint64_t elapse)
    {
        return TimerAwaiter(ctx, elapse);
    }

} // namespace tinyasync

#endif // TINYASYNC_H
