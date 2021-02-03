#ifndef TINYASYNC_H
#define TINYASYNC_H

#include <coroutine>
#include <exception>
#include <utility>
#include <map>
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

#ifdef _WIN32

#include <Windows.h>

#elif defined(__unix__)

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

namespace tinyasync
{

#ifdef _WIN32

    using NativeHandle = HANDLE;

#elif defined(__unix__)

    using NativeHandle = int;

#endif

    void close_handle(NativeHandle h)
    {
#ifdef _WIN32
        Close(h);

#elif defined(__unix__)

        close(h);

#endif
    }

    NativeHandle const NULL_HANDLE = 0;

    struct AutoClose
    {

        NativeHandle m_h;
        AutoClose(NativeHandle h) noexcept : m_h(h)
        {
        }

        NativeHandle release() noexcept
        {
            auto h = m_h;
            m_h = NULL_HANDLE;
            return h;
        }

        ~AutoClose() noexcept
        {
            if (m_h)
            {
                close_handle(m_h);
            }
        }
    };

    class ConnImpl;
    class ConnAwaiter;
    class ConnCallback;

    class AcceptorImpl;
    class AcceptorCallback;
    class AcceptorAwaiter;

    class ConnectorImpl;
    class ConnectorCallback;
    class ConnectorAwaiter;

    inline std::map<std::coroutine_handle<>, std::string> name_map;
    inline void set_name(std::coroutine_handle<> h, std::string name)
    {
        auto &name_ = name_map[h];
        name_ = std::move(name);
    }

    inline char const *name(std::coroutine_handle<> h)
    {
        if (h == nullptr)
            return "null";
        else if (h == std::noop_coroutine())
            return "noop";

        auto &name = name_map[h];
        if (name.empty())
        {
            static char c = 'A';
            name.push_back(c++);
        }
        return name.c_str();
    }



    std::string vformat(char const *fmt, va_list args) {
        va_list args2;
        va_copy(args2, args);
        std::size_t n = vsnprintf(NULL, 0, fmt, args2);
        
        char static_buffer[256];
        std::unique_ptr<char[]> dynamic_buffer;
        char *b = nullptr;
        std::size_t bs = 0;
        if(n > 255) {
            dynamic_buffer.reset(new char[n+1]);
            b = dynamic_buffer.get();
            bs = n + 1;
        } else{
            b = static_buffer;
            bs = 256;
        }
        
        vsnprintf(b, bs, fmt, args);
        return b;
    }

    std::string format(char const *fmt, ...) {
        va_list args;
        va_start(args, fmt);
        std::string str = vformat(fmt, args);
        va_end(args);
        return str;
    }

    auto abi_demangle (const char *abi_name)
    {

        // https://gcc.gnu.org/onlinedocs/libstdc++/libstdc++-html-USERS-4.3/a01696.html
        // https://stackoverflow.com/questions/4939636/function-to-mangle-demangle-functions
        int status;
        char const *name = abi::__cxa_demangle(abi_name, 0 /* output buffer */, 0 /* length */, &status);

        auto deallocator = [](char const *mem) { 
            if (mem)
                ::free((void*)mem);
        };

        // 0: The demangling operation succeeded.
        if (status != 0) {
            name = nullptr;
        }

        std::unique_ptr<const char, decltype(deallocator) > unique_name(name, deallocator);
        return unique_name;
    }

    void print_exception(const std::exception& e)
    {
        auto type_name = abi_demangle(typeid(e).name());
        printf("%s: %s\n", type_name.get(), e.what());

        try {           
            std::rethrow_if_nested(e);
        } catch(const std::exception& e) {
            printf("raised from: ");
            print_exception(e);
        } catch(...) {
        }
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
    } while (0)
#define TINYASYNC_LOG_NNL(...) log(__VA_ARGS__)



    inline std::vector<std::string> log_prefix;

    struct log_prefix_guad
    {
        std::size_t l;

        log_prefix_guad(char const *fmt, ...) : l(0)
        {

            char buf[1000];
            va_list args;
            va_start(args, fmt);
            vsnprintf(buf, 1000, fmt, args);
            va_end(args);

            buf[1000 - 1] = 0;

            l = log_prefix.size();

            log_prefix.emplace_back(buf);
        }

        ~log_prefix_guad()
        {
            if (l < log_prefix.size())
            {
                log_prefix.resize(l);
            }
        }
    };

    inline void log(char const *fmt, ...)
    {
        for (auto &p : log_prefix)
        {
            printf("%s", p.c_str());
        }
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
    }

#else

#define TINYASYNC_LOG(...) \
    do                     \
    {                      \
    } while (0)


    struct log_prefix_guad
    {
        log_prefix_guad(std::string_view)
        {
        }
    };

    inline void log(char const *fmt, ...)
    {
    }


#define TINYASYNC_GUARD(...) \
    do                       \
    {                        \
    } while (0)
#define TINYASYNC_LOG(...) \
    do                     \
    {                      \
    } while (0)
#define TINYASYNC_LOG_NNL(...) \
    ldo {}                     \
    while (0)

#endif // TINYASYNC_TRACE

    struct Noise
    {
        char const *src_loc;
        Noise(char const *src_loc) : src_loc(src_loc)
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

    template <class T>
    struct is_trivial_parameter_in_itanium_abi : std::bool_constant<
                                                     std::is_trivially_destructible_v<T>
                                                     && (!std::is_copy_constructible_v<T> || std::is_trivially_copy_constructible_v<T>)
                                                     &&(!std::is_move_constructible_v<T> || std::is_trivially_move_constructible_v<T>)
                                                     &&(std::is_copy_constructible_v<T> || std::is_move_constructible_v<T>)>
    {

    };

    template <class T>
    struct has_trivail_five : std::bool_constant<
        std::is_trivially_destructible_v<T>
        &&std::is_trivially_copy_constructible_v<T>
        && std::is_trivially_copy_assignable_v<T>
        && std::is_trivially_move_constructible_v<T>
        && std::is_trivially_move_assignable_v<T> >
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
        Name(char const *name) : m_name(name)
        {
        }

        std::string m_name;
    };

    inline void set_name_r(std::coroutine_handle<> const &h, Name const &name)
    {
        TINYASYNC_GUARD("set_name_r():");
        TINYASYNC_LOG("set name `%s` for %p", name.m_name.c_str(), h.address());
        ::tinyasync::set_name(h, name.m_name);
    }

    void set_name_r(std::coroutine_handle<> const &h) {}

    template <class F, class... T>
    inline void set_name_r(std::coroutine_handle<> const &h, F const &f, T const &...args)
    {
        set_name_r(h, args...);
    }



    void throw_errno(std::string const &what) {
        throw std::system_error(errno, std::system_category(), what);
    }


    struct Task
    {

        struct Promise
        {

            static void *operator new(std::size_t size)
            {
                TINYASYNC_GUARD("Task::Promise::operator new():");
                auto ptr = ::operator new(size);
                TINYASYNC_LOG("%d bytes at %p", (int)size, ptr);                
                return ptr;
            }

            static void operator delete(void *ptr, std::size_t size)
            {
                TINYASYNC_GUARD("Task::Promise::operator delete():");
                TINYASYNC_LOG("%d bytes at %p", (int)size, ptr);
                ::operator delete(ptr, size);
            }

            Task get_return_object()
            {
                TINYASYNC_GUARD("Task::Promise::get_return_object():");
                auto h = std::coroutine_handle<Promise>::from_promise(*this);
                TINYASYNC_LOG("`%s`", name(h));
                return {h};
            }

            template <class... T>
            Promise(T const &...args)
            {
                TINYASYNC_GUARD("Task::Promise::Promise():");
                TINYASYNC_LOG("");
                auto h = std::coroutine_handle<Promise>::from_promise(*this);
                set_name_r(h, args...);
            }

            Promise(Promise &&r) = delete;
            Promise(Promise const &r) = delete;

            struct InitialAwaityer : std::suspend_always
            {
                std::coroutine_handle<> m_h;

                InitialAwaityer(std::coroutine_handle<Promise> h) : m_h(h)
                {
                }

                void await_suspend(std::coroutine_handle<> h) const noexcept
                {
                    TINYASYNC_GUARD("InitialAwaityer::await_suspend():");
                    TINYASYNC_LOG("%s suspended, back to caller", name(h));
                    // return to caller
                }

                void await_resume() const noexcept
                {
                    TINYASYNC_GUARD("InitialAwaityer::await_resume():");
                    TINYASYNC_LOG("%s resumed", name(m_h));
                }
            };

            InitialAwaityer initial_suspend()
            {
                return {std::coroutine_handle<Promise>::from_promise(*this)};
            }

            struct FinalAwater : std::suspend_always
            {
                std::coroutine_handle<> m_continuum;

                FinalAwater(std::coroutine_handle<> continuum) noexcept : m_continuum(continuum)
                {
                }

                std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) const noexcept
                {
                    TINYASYNC_GUARD("Task::FinalAwater::await_suspend():");
                    TINYASYNC_LOG("`%s` suspended, resume its continuum `%s`", name(h), name(m_continuum));
                    if (m_continuum)
                    {
                        // co_wait ...
                        // back to awaiter
                        return m_continuum;
                    }
                    // directly .resume()
                    // back to resumer
                    return std::noop_coroutine();
                }

                void await_resume() const noexcept
                {
                    TINYASYNC_GUARD("Task::FinalAwater::await_resume():");
                    TINYASYNC_LOG("Bug!");
                    // never reach here
                    assert(false);
                }
            };

            FinalAwater final_suspend() noexcept
            {
                return { m_continuum };
            }

            void unhandled_exception() { 
                TINYASYNC_GUARD("Task::FinalAwater::await_resume():");
                try {
                    std::rethrow_exception(std::current_exception());
                } catch(std::exception &e) {
                    print_exception(e);
                }

                TINYASYNC_LOG("will be terminated");
                std::terminate();
            }

            void return_void()
            {
            }
            std::coroutine_handle<> m_continuum = nullptr;
        };

        using promise_type = Promise;
        std::coroutine_handle<promise_type> m_h;

        std::coroutine_handle<promise_type> coroutine_handle()
        {
            return m_h;
        }

        promise_type &promise()
        {
            return m_h.promise();
        }

        struct Awaiter
        {
            std::coroutine_handle<promise_type> m_h;

            Awaiter(std::coroutine_handle<promise_type> h) : m_h(h)
            {
            }
            bool await_ready()
            {
                return false;
            }

            void await_suspend(std::coroutine_handle<> suspend_coroutine)
            {
                TINYASYNC_GUARD("Task::Awaiter::await_resume()(`%s`):", name(m_h));

                TINYASYNC_LOG("set continuum of `%s` to `%s`", name(m_h), name(suspend_coroutine));
                m_h.promise().m_continuum = suspend_coroutine;
                TINYASYNC_LOG("`%s` suspended, resume `%s`", name(suspend_coroutine), name(m_h));
                m_h.resume();
                TINYASYNC_LOG("resumed from `%s`, `%s` already suspend, back to caller/resumer", name(m_h), name(suspend_coroutine));
            }

            void await_resume()
            {
                TINYASYNC_GUARD("Task::Awaiter::await_resume()(`%s`):", name(m_h));
            }
        };

        Awaiter operator co_await()
        {
            return {m_h};
        }

        Task() : m_h(nullptr)
        {
        }

        Task(std::coroutine_handle<Promise> h) : m_h(h)
        {
            TINYASYNC_LOG("Task::Task(): %s", name(m_h));
        }

        Task(Task &&r) noexcept : m_h(std::exchange(r.m_h, nullptr))
        {
        }

        Task &operator=(Task &&r) noexcept
        {
            this->~Task();
            m_h = r.m_h;
            return *this;
        }

        ~Task()
        {
            if (m_h)
            {
                m_h.destroy();
            }
        }

        Task(Task const &r) = delete;
        Task &operator=(Task const &r) = delete;
    };

    struct Spawn
    {

        struct Promise
        {

            template <class... T>
            Promise(T const &...args)
            {
                TINYASYNC_GUARD("Spawn::Promise::Promise():");
                TINYASYNC_LOG("");
                auto h = std::coroutine_handle<Promise>::from_promise(*this);
                set_name_r(h, args...);
            }

            static void *operator new(std::size_t size)
            {
                TINYASYNC_GUARD("Spawn::Promise::operator new():");
                auto ptr = ::operator new(size);
                TINYASYNC_LOG("%d bytes at %p", (int)size, ptr);
                return ptr;
            }

            static void operator delete(void *ptr, std::size_t size)
            {
                TINYASYNC_GUARD("Spawn::Promise::operator delete():");
                TINYASYNC_LOG("%d bytes at %p", (int)size, ptr);
                return ::operator delete(ptr, size);
            }

            std::coroutine_handle<> m_continuum = std::noop_coroutine();

            Spawn get_return_object()
            {
                return {std::coroutine_handle<Promise>::from_promise(*this)};
            }

            // get to run immediately
            std::suspend_never initial_suspend() { return {}; }
            // don't suspend
            // the coroutine will be destroied automatically
            std::suspend_never final_suspend() { return {}; }
            void unhandled_exception() { std::terminate(); }
            void return_void() {}
        };
        using promise_type = Promise;

        Spawn(std::coroutine_handle<Promise> h) : m_h(h)
        {
        }

        struct Awaiter : std::suspend_always
        {
            std::coroutine_handle<Promise> m_h;

            Awaiter(std::coroutine_handle<Promise> h) : m_h(h)
            {
            }

            void await_suspend(std::coroutine_handle<> h)
            {
                m_h.promise().m_continuum = h;
            }
        };
        Awaiter operator co_await()
        {
            return {m_h};
        }

        promise_type &promise()
        {
            return m_h.promise();
        }

        std::coroutine_handle<Promise> m_h;
    };

    static_assert(is_trivial_parameter_in_itanium_abi<std::coroutine_handle<Spawn::Promise>>::value);
    static_assert(is_trivial_parameter_in_itanium_abi<Spawn>::value);
    //static_assert(is_trivial_parameter_in_itanium_abi<Task>::value);

    template <class Awaitable>
    inline Spawn co_spawn(Awaitable awaitble)
    {
        co_await awaitble;
    }

    template <class Awaitable>
    inline Spawn co_spawn(Awaitable awaitble, Name)
    {
        co_await awaitble;
    }

#if defined(__unix__)

    using IoEvent = epoll_event;
    
    std::string ioe2str(IoEvent &evt) {
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
        using callback_base_type = Callback;
        virtual void callback(IoEvent &evt) = 0;
    };
    
    struct Callback2
    {
        using callback_base_type = Callback2;
        // virtual void callback(IoEvent &evt) = 0;        

        void callback(IoEvent &evt) {
            this->m_callback(this, evt);
        }

        using CallbackPtr = void (*)(callback_base_type *callback, IoEvent &);
        CallbackPtr m_callback;
    };

    struct TimerAwaiter;
    struct IoContext;

    struct IoContext
    {

        NativeHandle m_native_handle = NULL_HANDLE;
        bool m_abort_requested = false;

        IoContext()
        {
#ifdef _WIN32

#elif defined(__unix__)

            auto fd = epoll_create1(EPOLL_CLOEXEC);
            if (fd == -1)
            {
                throw std::system_error(errno, std::system_category(), "can't create epoll");
            }
            m_native_handle = fd;

#endif
        }

        IoContext(IoContext &&r)
        {
            this->m_native_handle = r.m_native_handle;
            r.m_native_handle = NULL_HANDLE;
        }

        IoContext &operator=(IoContext &&r)
        {
            this->~IoContext();
            this->m_native_handle = r.m_native_handle;
            r.m_native_handle = NULL_HANDLE;
            return *this;
        }

        ~IoContext()
        {
#ifdef _WIN32

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

        void run()
        {
            TINYASYNC_GUARD("IoContex::run():");

            for (; !m_abort_requested;)
            {
#ifdef _WIN32
                // get thread into alternative state
                // in thi state, the callback get a chance to be invoked
                SleepEx(INFINITE, TRUE);

#elif defined(__unix__)

                int const maxevents = 5;
                epoll_event events[maxevents];
                auto epfd = m_native_handle;
                int const timeout = -1; // indefinitely

                TINYASYNC_LOG("epoll_wait ...");
                int nfds = epoll_wait(epfd, (epoll_event *)events, maxevents, timeout);

                if (nfds == -1)
                {
                    throw_errno("epoll_wait error");
                }

                for (auto i = 0; i < nfds; ++i)
                {
                    auto &evt = events[i];
                    TINYASYNC_LOG("event %d of %d", i, nfds);
                    TINYASYNC_LOG("event(%x) = %s", evt.events, ioe2str(evt).c_str());
                    auto callback = (Callback *)evt.data.ptr;
                    callback->callback(evt);
                }
#endif
            }
        }

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

        std::string to_string() const {
            if(m_address_type == AddressType::IpV4) {
                // host long -> network long -> string
                // TODO: this is not thread safe
                in_addr addr;
                addr.s_addr = htonl(m_v4);
                char buf[256];
                inet_ntop(AF_INET, &addr, buf, sizeof(buf));
                return buf;
            } else if(m_address_type == AddressType::IpV6) {
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

    inline void setnonblocking(int fd)
    {

        //int status = fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        int status = fcntl(fd, F_SETFL, O_NONBLOCK);

        if (status == -1)
        {
            throw std::system_error(errno, std::system_category(), "can't set fd nonblocking");
        }
    }


    template<class Awaiter, class Buffer>
    class DataAwaiterMixin {
    protected:
        friend class ConnImpl;
        friend class ConnCallback;

        Awaiter *m_next;
        IoContext *m_ctx;
        ConnImpl *m_conn;
        std::coroutine_handle<> m_suspend_coroutine;
        Buffer m_buffer_addr;
        std::size_t m_buffer_start;
        std::size_t m_buffer_size;
        std::size_t m_bytes_transfer;
    };

    class AsyncReceiveAwaiter : public std::suspend_always,
        public DataAwaiterMixin<AsyncReceiveAwaiter, void*>
    {
    public:
        friend class ConnImpl;
        AsyncReceiveAwaiter(ConnImpl &conn, void *b, std::size_t n);
        void await_suspend(std::coroutine_handle<> h);
        std::size_t await_resume();
    };

    class AsyncSendAwaiter : public std::suspend_always,
        public DataAwaiterMixin<AsyncSendAwaiter, void const *>
    {
    public:
        friend class ConnImpl;
        AsyncSendAwaiter(ConnImpl &conn, void const *b, std::size_t n);
        void await_suspend(std::coroutine_handle<> h);
        std::size_t await_resume();
    };

    struct ConnCallback : Callback
    {
        ConnImpl *m_conn;
        ConnCallback(ConnImpl *conn) : m_conn(conn)
        {
            TINYASYNC_GUARD("ConnCallback::ConnCallback():");
            TINYASYNC_LOG("conn %p", conn);
        }
        void callback(IoEvent&) override;

    };

    class ConnImpl
    {
        friend class AsyncReceiveAwaiter;
        friend class AsyncSendAwaiter;
        friend class ConnCallback;


        IoContext *m_ctx;
        NativeHandle m_conn_handle;
        ConnCallback m_callback { this };
        AsyncReceiveAwaiter *m_recv_awaiter = nullptr;
        AsyncSendAwaiter *m_send_awaiter = nullptr;

    public:
        void reset()
        {
            if (m_conn_handle)
            {
                // ubind from epool ...
                close_handle(m_conn_handle);
                m_conn_handle = NULL_HANDLE;

                m_conn_handle = NULL_HANDLE;
            }
        }

        ConnImpl(IoContext &ctx, NativeHandle conn_sock)
        {
            TINYASYNC_GUARD("ConnImpl");

            m_ctx = &ctx;
            m_conn_handle = conn_sock;
        }

        ConnImpl(ConnImpl &&r) = delete;
        ConnImpl &operator=(ConnImpl &&r) = delete;
        ConnImpl(ConnImpl const &) = delete;
        ConnImpl &operator=(ConnImpl const &) = delete;

        ~ConnImpl() noexcept
        {
            this->reset();
        }

        AsyncReceiveAwaiter async_read(void *buffer, std::size_t bytes)
        {
            TINYASYNC_GUARD("Connection:receive():");
            TINYASYNC_LOG("try to receive %d bytes", bytes);

            return {*this, buffer, bytes};
        }

        AsyncSendAwaiter async_send(void const *buffer, std::size_t bytes)
        {
            TINYASYNC_GUARD("Connection:send():");
            TINYASYNC_LOG("try to send %d bytes", bytes);
            return {*this, buffer, bytes};
        }

    
    };

    void ConnCallback::callback(IoEvent &evt)
    {
        TINYASYNC_GUARD("ConnCallback::callback():");

        if ((evt.events | EPOLLIN) && m_conn->m_recv_awaiter) {
            // we want to read and it's ready to read

            TINYASYNC_LOG("ready to read for conn_handle %d", m_conn->m_conn_handle);

            auto awaiter = m_conn->m_recv_awaiter;
            int nbytes = recv(m_conn->m_conn_handle, awaiter->m_buffer_addr, (int)awaiter->m_buffer_size, 0);

            if (nbytes < 0)
            {

                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    TINYASYNC_LOG("EAGAIN, fd = %d", m_conn->m_conn_handle);
                }
                else
                {
                    TINYASYNC_LOG("ERROR = %d, fd = %d", errno, m_conn->m_conn_handle);
                    throw_errno("recv error");
                }
            } else {
                if(nbytes == 0) {
                    if(errno == ESHUTDOWN) {
                        TINYASYNC_LOG("ESHUTDOWN, fd = %d", m_conn->m_conn_handle);
                    }
                }
                TINYASYNC_LOG("fd = %d, %d bytes read", m_conn->m_conn_handle, nbytes);
                awaiter->m_bytes_transfer = nbytes;

                // may cause Connection self deleted
                // so we should not want to read and send at the same
                awaiter->m_suspend_coroutine.resume();
            }

        } else if((evt.events | EPOLLOUT) && m_conn->m_send_awaiter) {
            // we want to send and it's ready to read

            auto awaiter = m_conn->m_send_awaiter;
            TINYASYNC_LOG("ready to send for conn_handle %d, %d bytes at %p sending", m_conn->m_conn_handle, (int)awaiter->m_buffer_size, awaiter->m_buffer_addr);
            int nbytes = ::send(m_conn->m_conn_handle, awaiter->m_buffer_addr, (int)awaiter->m_buffer_size, 0);
            TINYASYNC_LOG("sent %d bytes", nbytes);

            if (nbytes < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    TINYASYNC_LOG("EAGAIN, fd = %d", m_conn->m_conn_handle);
                }
                else
                {
                    TINYASYNC_LOG("ERROR = %d, fd = %d", errno, m_conn->m_conn_handle);
                    throw_errno("send error");
                }
            } else {
                if(nbytes == 0) {
                    if(errno == ESHUTDOWN) {
                        TINYASYNC_LOG("ESHUTDOWN, fd = %d", m_conn->m_conn_handle);
                    }
                }

                TINYASYNC_LOG("fd = %d, %d bytes sent", m_conn->m_conn_handle, nbytes);
                awaiter->m_bytes_transfer = nbytes;

                // may cause Connection self deleted
                // so we should not want to read and send at the same
                awaiter->m_suspend_coroutine.resume();
            }
        } else {
            TINYASYNC_LOG("not processed event for conn_handle %x", errno, m_conn->m_conn_handle);
            exit(1);
        }
    }


    AsyncReceiveAwaiter::AsyncReceiveAwaiter(ConnImpl &conn, void *b, std::size_t n)
    {
        TINYASYNC_GUARD("AsyncReceiveAwaiter::AsyncReceiveAwaiter():");
        m_next = nullptr;
        m_ctx = conn.m_ctx;
        m_conn = &conn;
        m_buffer_addr = b;
        m_buffer_size = n;
        m_bytes_transfer = 0;
        TINYASYNC_LOG("conn: %p", m_conn);
    }

    void AsyncReceiveAwaiter::await_suspend(std::coroutine_handle<> h)
    {
        TINYASYNC_GUARD("AsyncReceiveAwaiter::await_suspend():");
        m_suspend_coroutine = h;
        // insert into front of list
        this->m_next = m_conn->m_recv_awaiter;
        m_conn->m_recv_awaiter = this;
        TINYASYNC_LOG("set recv_awaiter of conn(%p) to %p", m_conn, m_conn->m_recv_awaiter);

        epoll_event evt;
        evt.data.ptr = &m_conn->m_callback;            
        evt.events = EPOLLIN;
        auto clterr = epoll_ctl(m_ctx->handle(), EPOLL_CTL_ADD, m_conn->m_conn_handle, &evt);
        TINYASYNC_LOG("epoll_ctl(EPOOLIN|EPOLLLT) for conn_handle = %d", m_conn->m_conn_handle);

    }

    std::size_t AsyncReceiveAwaiter::await_resume()
    {
        TINYASYNC_GUARD("AsyncReceiveAwaiter::await_resume():");
        // pop from front of list
        m_conn->m_recv_awaiter = m_conn->m_recv_awaiter->m_next;
        TINYASYNC_LOG("set recv_awaiter of conn(%p) to %p", m_conn, m_conn->m_recv_awaiter);

        auto clterr = epoll_ctl(m_ctx->handle(), EPOLL_CTL_DEL, m_conn->m_conn_handle, NULL);
        TINYASYNC_LOG("unregister conn_handle = %d", m_conn->m_conn_handle);

        return m_bytes_transfer;
    }

    AsyncSendAwaiter::AsyncSendAwaiter(ConnImpl &conn, void const *b, std::size_t n)
    {
        m_next = nullptr;
        m_ctx = conn.m_ctx;
        m_conn = &conn;
        m_buffer_addr = b;
        m_buffer_size = n;
        m_bytes_transfer = 0;
    }

    void AsyncSendAwaiter::await_suspend(std::coroutine_handle<> h)
    {
        m_suspend_coroutine = h;
        // insert front of list
        this->m_next = m_conn->m_send_awaiter;
        m_conn->m_send_awaiter = this;
        TINYASYNC_LOG("set send_awaiter of conn(%p) to %p", m_conn, m_conn->m_send_awaiter);

        epoll_event evt;
        evt.data.ptr = &m_conn->m_callback;            
        evt.events = EPOLLOUT;
        auto clterr = epoll_ctl(m_ctx->handle(), EPOLL_CTL_ADD, m_conn->m_conn_handle, &evt);
        TINYASYNC_LOG("epoll_ctl(EPOLLOUT|EPOLLLT) for conn_handle = %d", m_conn->m_conn_handle);
    }

    std::size_t AsyncSendAwaiter::await_resume()
    {
        TINYASYNC_GUARD("AsyncSendAwaiter::await_resume():");
        // pop from front of list
        m_conn->m_send_awaiter = m_conn->m_send_awaiter->m_next;
        TINYASYNC_LOG("set recv_awaiter of conn(%p) to %p", m_conn, m_conn->m_send_awaiter);

        auto clterr = epoll_ctl(m_ctx->handle(), EPOLL_CTL_DEL, m_conn->m_conn_handle, NULL);
        TINYASYNC_LOG("unregister conn_handle = %d", m_conn->m_conn_handle);

        return m_bytes_transfer;
    }

    class Connection
    {
        // use unique_ptr because
        // 1. fast move/swap
        // 2. reference stable, usefull for callbacks
        std::unique_ptr<ConnImpl> m_impl;
    public:
        Connection(IoContext &ctx, NativeHandle conn_sock)
        {
            m_impl.reset(new ConnImpl(ctx, conn_sock));
        }

        AsyncReceiveAwaiter async_read(void *buffer, std::size_t bytes)
        {
            return m_impl->async_read(buffer, bytes);
        }

        AsyncSendAwaiter async_send(void const *buffer, std::size_t bytes)
        {
            return m_impl->async_send(buffer, bytes);
        }

    };



    class SocketMixin {

    protected:
        IoContext *m_ctx;
        Protocol m_protocol;
        Endpoint m_endpoint;
        NativeHandle m_socket;
        bool m_added_to_event_pool;

    public:


        NativeHandle native_handle() const noexcept {
            return m_socket;

        }

        SocketMixin(IoContext &ctx) {
            m_ctx = &ctx;
            m_socket = NULL_HANDLE;

        }

        void open(Protocol const &protocol, bool blocking = false)
        {
            TINYASYNC_GUARD("SocketMixin::open():");            
            // PF means protocol
            auto socketfd = ::socket(PF_INET, SOCK_STREAM, 0);
            if (socketfd == -1)
            {
                throw_errno("can't create socket");
            }

            if(!blocking)
                setnonblocking(socketfd);

            m_socket = socketfd;
            m_protocol = protocol;
            TINYASYNC_LOG("create socket %X, nonblocking = %d", socketfd, int(!blocking));            
        }


        void reset()
        {
            TINYASYNC_GUARD("SocketMixin::reset():");
            if (m_socket)
            {
                if(m_added_to_event_pool) {
                    auto ctlerr = epoll_ctl(m_ctx->handle(), EPOLL_CTL_DEL, m_socket, NULL);
                    if(ctlerr == -1) {
                        auto what = format("can't remove (from epoll) socket %x", m_socket);
                        throw_errno(what);
                    }
                }
                TINYASYNC_LOG("close fd = %d", m_socket);
                close_handle(m_socket);
                m_socket = NULL_HANDLE;
            }
        }
    };

    class AcceptorAwaiter 
    {

        friend class AcceptorImpl;
        friend class AcceptorCallback;

        AcceptorImpl *m_acceptor;
        AcceptorAwaiter *m_next;
        std::coroutine_handle<> m_suspend_coroutine;

    public:
        bool await_ready() { return false; }
        AcceptorAwaiter(AcceptorImpl &acceptor);
        void await_suspend(std::coroutine_handle<> h);
        Connection await_resume();
    };

    class AcceptorCallback : Callback
    {
        friend class AcceptorImpl;
        AcceptorImpl *m_acceptor;

    public:
        AcceptorCallback(AcceptorImpl *acceptor)
        {
            m_acceptor = acceptor;
        };

        virtual void callback(IoEvent &evt) override;

    };

    class AcceptorImpl : SocketMixin
    {
        friend class AcceptorAwaiter;
        friend class AcceptorCallback;
        AcceptorAwaiter *m_awaiter = nullptr;
        AcceptorCallback m_callback = this;

    public:
        AcceptorImpl(IoContext &ctx) : SocketMixin(ctx)
        {
        }

        AcceptorImpl(IoContext &ctx, Protocol const &protocol, Endpoint const &endpoint) : AcceptorImpl(ctx)
        {
            try
            {
                // one effort triple successes
                open(protocol);
                bind(endpoint);
                listen();
            }
            catch (...)
            {
                reset();
                auto what = format("accept error %s:%d", endpoint.m_address.to_string().c_str(), (int)(unsigned)endpoint.m_port);
                std::throw_with_nested(std::runtime_error(what));
            }
        }

        AcceptorImpl(AcceptorImpl &&r) = delete;
        AcceptorImpl(AcceptorImpl const &) = delete;
        AcceptorImpl &operator=(AcceptorImpl &&r) = delete;
        AcceptorImpl &operator=(AcceptorImpl const &) = delete;
        ~AcceptorImpl() {
            reset();
        }

        void bind(Endpoint const &endpoint)
        {

            TINYASYNC_GUARD("Acceptor::bind():");            
            int listenfd = m_socket;

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

            auto binderr = ::bind(listenfd, (sockaddr *)&serveraddr, sizeof(serveraddr));
            if (binderr == -1)
            {
                throw_errno(format("can't bind socket, fd = %x", listenfd));
            }

            m_endpoint = endpoint;
            TINYASYNC_LOG("fd = %X", listenfd);            
        }

        void listen()
        {
            TINYASYNC_GUARD("Acceptor::listen():");            
            int listenfd = m_socket;
            int max_pendding_connection = 5;
            int err = ::listen(listenfd, max_pendding_connection);
            if (err == -1)
            {
                throw std::system_error(errno, std::system_category(), "can't listen port");
            }

            epoll_event evt;
            evt.data.ptr = &m_callback;            
            // level triger by default
            evt.events = EPOLLIN;
            auto ctlerr = epoll_ctl(m_ctx->handle(), EPOLL_CTL_ADD, listenfd, &evt);
            if(ctlerr == -1) {
                throw_errno(format("can't listen %x", listenfd));
            }
            m_added_to_event_pool = true;
            TINYASYNC_LOG("fd = %X", listenfd);            
        }


        AcceptorAwaiter async_accept()
        {
            return {*this};
        }
    };


    AcceptorAwaiter::AcceptorAwaiter(AcceptorImpl &acceptor) : m_acceptor(&acceptor)
    {
    }

    void AcceptorAwaiter::await_suspend(std::coroutine_handle<> h)
    {
        assert(!m_acceptor->m_awaiter);
        m_acceptor->m_awaiter = this;
        m_suspend_coroutine = h;
    }

    // connection is set nonblocking
    Connection AcceptorAwaiter::await_resume()
    {

        TINYASYNC_GUARD("Acceptor::Awaiter::await_resume():");
        TINYASYNC_LOG("acceptor %p, awaiter %p", &m_acceptor, m_acceptor->m_awaiter);

        // it's ready to accept
        int conn_sock = ::accept(m_acceptor->m_socket, NULL, NULL);
        if (conn_sock == -1)
        {
            throw_errno(format("can't accept %x", conn_sock));
        }

        TINYASYNC_LOG("setnonblocking %d", conn_sock);
        setnonblocking(conn_sock);

        m_acceptor->m_awaiter = nullptr;
        return {*m_acceptor->m_ctx, conn_sock};
    }



    void AcceptorCallback::callback(IoEvent &evt)
    {
        TINYASYNC_GUARD("AcceptCallback::callback():");
        TINYASYNC_LOG("acceptor %p, awaiter %p", m_acceptor, m_acceptor->m_awaiter);
        AcceptorAwaiter *awaiter = m_acceptor->m_awaiter;
        if(awaiter) {
            awaiter->m_suspend_coroutine.resume();
        } else {
            // this will happen when after accetor.listen() but not yet co_await acceptor.async_accept()
            // you should have been using level triger to get this event the next time
            TINYASYNC_LOG("No awaiter found, event ignored");
        }
    }



    class Acceptor {

    public:
        Acceptor(IoContext &ctx, Protocol protocol, Endpoint endpoint)
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
























    class ConnectorCallback : public Callback
    {  
        friend class ConnectorAwaiter;
        friend class ConnectorImpl;
        ConnectorImpl *m_connector;
    public:
        ConnectorCallback(ConnectorImpl &connector) : m_connector(&connector)
        {
        }
        virtual void callback(IoEvent &evt) override;

    };

        
    class ConnectorAwaiter {
        friend class ConnectorCallback;

        ConnectorImpl *m_connector;
        std::coroutine_handle<> m_suspend_coroutine;
        void unregister(NativeHandle conn_handle);

    public:
        ConnectorAwaiter(ConnectorImpl &connector) : m_connector(&connector) {
        }

        constexpr bool await_ready() const { return false; }

        bool await_suspend(std::coroutine_handle<> suspend_coroutine);

        Connection await_resume();
        
    };


    class ConnectorImpl : public SocketMixin {

        friend class ConnectorAwaiter;
        friend class ConnectorCallback;
        ConnectorAwaiter *m_awaiter;
        ConnectorCallback m_callback = { *this };
    public:
    
        ConnectorImpl(IoContext &ctx) : SocketMixin(ctx) {
        }

        ConnectorImpl(IoContext &ctx, Protocol const &protocol, Endpoint const &endpoint): ConnectorImpl(ctx) {
            try {
                this->open(protocol);
                m_endpoint = endpoint;
            } catch(...) {
                this->reset();
                throw;
            }
        }

        ConnectorImpl(ConnectorImpl const &r) :
            SocketMixin(static_cast<SocketMixin const &>(r)),
            m_callback(*this) {     
            m_awaiter = r.m_awaiter;
            // don't copy, m_callback is fine
            // m_callback = r.m_callback;
        }

        ConnectorImpl &operator=(ConnectorImpl const &r) {
            static_cast<SocketMixin &>(*this) = static_cast<SocketMixin const &>(r);
            m_awaiter = r.m_awaiter;
            m_callback = r.m_callback;
            m_callback.m_connector = this;
            return *this;
        }

        // don't reset
        ~ConnectorImpl() = default;

        ConnectorAwaiter async_connect() {
            return { *this };
        }

        ConnectorAwaiter operator co_await() {
            return async_connect();
        }

    private:

        bool connect(Endpoint const &endpoint)
        {

            TINYASYNC_GUARD("Connector::connect():");            
            int connfd = m_socket;

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
            auto connerr = ::connect(connfd, (sockaddr *)&serveraddr, sizeof(serveraddr));
            if (connerr == -1)
            {
                if(errno == EINPROGRESS) {
                    TINYASYNC_LOG("EINPROGRESS, conn_handle = %X", connfd);
                    // just ok ...
                } else {
                    throw_errno(format("can't connect, conn_handle = %X", connfd));
                }
            } else if(connerr == 0) {
                TINYASYNC_LOG("connected, conn_handle = %X", connfd);
                // it's connected ??!!
                connected = true;
            }

            TINYASYNC_LOG("conn_handle = %X", connfd);
            m_endpoint = endpoint;
            return connected;
        }


    };


    ConnectorImpl async_connect(IoContext &ctx, Protocol const &protocol, Endpoint const &endpoint)
    {
        return ConnectorImpl(ctx, protocol, endpoint);
    }

    void ConnectorCallback::callback(IoEvent &evt)
    {
        TINYASYNC_GUARD("ConnectorCallback::callback():");
        assert(m_connector);
        assert(m_connector->m_awaiter);

        auto connfd = m_connector->m_socket;

        if(evt.events & (EPOLLERR|EPOLLHUP)) {
            throw_errno(format("error, fd = %d", connfd));
        }

        int result;
        socklen_t result_len = sizeof(result);
        if (getsockopt(connfd, SOL_SOCKET, SO_ERROR, &result, &result_len) < 0) {
            throw_errno(format("getsockopt failed for conn_handle = %d", m_connector->m_socket));
        }
        if (result != 0) {
            if(errno == EINPROGRESS) {
                TINYASYNC_LOG("EINPROGRESS, fd = %d", connfd);
                return;
            } else {
                TINYASYNC_LOG("bad!, fd = %d", connfd);
                throw_errno(format("connect failed for conn_handle = %d", m_connector->m_socket));
            }
        }

        TINYASYNC_LOG("connected");
        m_connector->m_awaiter->m_suspend_coroutine.resume();

    }


    void ConnectorAwaiter::unregister(NativeHandle conn_handle) {
        auto connfd = conn_handle;
        m_connector->m_awaiter = nullptr;
        auto clterr = epoll_ctl(m_connector->m_ctx->handle(), EPOLL_CTL_DEL, connfd, NULL);
        if(clterr == -1) {
            throw_errno(format("can't unregister conn_handle = %d", connfd));
        }
        TINYASYNC_LOG("unregister connect for conn_handle = %d", connfd);
    }

    bool ConnectorAwaiter::await_suspend(std::coroutine_handle<> suspend_coroutine) {
        TINYASYNC_LOG("ConnectorAwaiter::await_suspend():");            
        m_suspend_coroutine = suspend_coroutine;
        auto connfd = m_connector->m_socket;

        assert(!m_connector->m_awaiter);
        m_connector->m_awaiter = this;

        epoll_event evt;
        evt.data.ptr = &m_connector->m_callback;           
        // level triger by default
        evt.events = EPOLLOUT;
        auto clterr = epoll_ctl(m_connector->m_ctx->handle(), EPOLL_CTL_ADD, m_connector->m_socket, &evt);
        if(clterr == -1) {
            throw_errno(format("epoll_ctl(EPOLLOUT) failed for conn_handle = %d", m_connector->m_socket));
        }
        TINYASYNC_LOG("register connect for conn_handle = %d", m_connector->m_socket);            
        
        if(m_connector->connect(m_connector->m_endpoint)) {
            this->unregister(connfd);
            return false;
        }
        return true;
    }


    Connection ConnectorAwaiter::await_resume() {
        TINYASYNC_GUARD("ConnectorAwaiter::await_resume():");
        auto connfd = m_connector->m_socket;
        //this->unregister(connfd);
        return { *m_connector->m_ctx, m_connector->m_socket };
    }













    class TimerAwaiter
    {

        IoContext &m_ctx;
        uint64_t m_elapse; // mili-second
        NativeHandle m_timer_handle = 0;
        std::coroutine_handle<> m_suspend_coroutine;

    public:
        // elapse in micro-second
        TimerAwaiter(IoContext &ctx, uint64_t elapse) : m_ctx(ctx), m_elapse(elapse)
        {
        }

        constexpr bool await_ready() const noexcept { return false; }
        constexpr void await_resume() const noexcept { }

#ifdef _WIN32
        static void CALLBACK callback(LPVOID lpArg, DWORD dwTimerLowValue, DWORD dwTimerHighValue)
        {
            UNREFERENCED_PARAMETER(dwTimerLowValue);
            UNREFERENCED_PARAMETER(dwTimerHighValue);

            auto timer = (TimerAwaiter *)lpArg;
            CloseHandle(timer->m_timer_handle);

            // resume suspend coroutine
            timer->m_suspend_coroutine.resume();
        };

        void await_suspend(std::coroutine_handle<> h)
        {

            // create a timer
            m_suspend_coroutine = h;
            m_timer_handle = CreateWaitableTimerW(NULL, FALSE, NULL);
            if (m_timer_handle != NULL)
            {
                LARGE_INTEGER elapse;
                // 10 * 100ns = 1us
                elapse.QuadPart = -10 * m_elapse;

                // register callback
                // return immediately
                SetWaitableTimer(m_timer_handle, &elapse, 0, callback, this, FALSE);
            }
        }
#elif defined(__unix__)

        struct TimerCallback : Callback
        {

            TimerCallback(TimerAwaiter *awaiter) : m_awaiter(awaiter)
            {
            }

            TimerAwaiter *m_awaiter;

            virtual void callback(IoEvent &evt) override
            {

                // remove from epoll list
                epoll_ctl(m_awaiter->m_ctx.handle(), m_awaiter->m_timer_handle, EPOLL_CTL_DEL, NULL);

                close(m_awaiter->m_timer_handle);
                m_awaiter->m_suspend_coroutine.resume();
            }

        } m_callback{this};

        void await_suspend(std::coroutine_handle<> h)
        {

            // create a timer
            m_suspend_coroutine = h;

            itimerspec time;
            time.it_interval.tv_sec = 0;
            time.it_interval.tv_nsec = 0;

            auto seconds = m_elapse / (1000 * 1000);
            auto nanos = (m_elapse - seconds * (1000 * 1000)) * 1000;

            time.it_value.tv_sec = seconds;
            time.it_value.tv_nsec = nanos;

            auto fd = timerfd_create(CLOCK_REALTIME, 0);
            if (fd == -1)
            {
                throw std::system_error(errno, std::system_category(), "can't create timer");
            }
            AutoClose guad(fd);

            auto fd2 = timerfd_settime(fd, 0, &time, NULL);
            if (fd2 == -1)
            {
                throw std::system_error(errno, std::system_category(), "can't set timer");
            }

            auto epfd = m_ctx.handle();
            assert(epfd);

            epoll_event evt;
            evt.data.ptr = &m_callback;
            evt.events = EPOLLIN;
            auto fd3 = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &evt);

            if (fd3 == -1)
            {
                throw std::system_error(errno, std::system_category(), "can't bind timer fd with epoll");
            }

            m_timer_handle = guad.release();
        }

#endif
    };

    // elapse in micro-second
    TimerAwaiter async_sleep(IoContext &ctx, uint64_t elapse)
    {
        return TimerAwaiter(ctx, elapse);
    }

    static_assert(has_trivail_five<Endpoint>::value);
    static_assert(has_trivail_five<Address>::value);
    static_assert(has_trivail_five<Protocol>::value);
    static_assert(has_trivail_five<SocketMixin>::value);
    //static_assert(!std::is_standard_layout_v<Callback>);
    //static_assert(!std::is_standard_layout_v<ConnCallback>);
    static_assert(std::is_standard_layout_v<Callback2>);
    static_assert(std::is_standard_layout_v<std::coroutine_handle<> >);
} // namespace tinyasync

#endif // TINYASYNC_H
