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


    class AcceptorImpl;

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
                                                     std::is_trivially_destructible_v<T> && (!std::is_copy_constructible_v<T> || std::is_trivially_copy_constructible_v<T>)&&(!std::is_move_constructible_v<T> || std::is_trivially_move_constructible_v<T>)&&(std::is_copy_constructible_v<T> || std::is_move_constructible_v<T>)>
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

    template <class T>
    struct NamedMixin
    {
        T &set_name(std::string n) &
        {
            ::tinyasync::set_name(((T *)this)->m_h, std::move(n));
            return (T &)*this;
        }

        T &&set_name(std::string n) &&
        {
            ::tinyasync::set_name(((T *)this)->m_h, std::move(n));
            return std::move((T &&) * this);
        }
    };

    struct Task : NamedMixin<Task>
    {

        struct Promise
        {

            static void *operator new(std::size_t size)
            {
                TINYASYNC_GUARD("Task::Promise::operator new():");
                TINYASYNC_LOG("%d bytes", (int)size);

                return ::operator new(size);
            }

            static void operator delete(void *ptr, std::size_t size)
            {
                TINYASYNC_GUARD("Task::Promise::operator delete():");
                TINYASYNC_LOG("%d bytes", (int)size);

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

            struct InitialAwaityer
            {
                std::coroutine_handle<> m_h;

                InitialAwaityer(std::coroutine_handle<Promise> h) : m_h(h)
                {
                }

                bool await_ready() const noexcept
                {
                    return false;
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

            struct FinalAwater
            {
                std::coroutine_handle<> m_continuum;

                FinalAwater(std::coroutine_handle<> continuum) : m_continuum(continuum)
                {
                }

                bool await_ready() const noexcept
                {
                    return false;
                }

                std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) const noexcept
                {
                    TINYASYNC_GUARD("FinalAwater::await_suspend():");
                    TINYASYNC_LOG("%s suspended, resume continuum of it, it's %s\n", name(h), name(m_continuum));
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
                    TINYASYNC_GUARD("FinalAwater::await_resume():");
                    TINYASYNC_LOG("");
                    // never reach here
                    assert(false);
                }
            };

            FinalAwater final_suspend() noexcept
            {
                return {m_continue};
            }

            void unhandled_exception() { std::terminate(); }
            void return_void()
            {
            }
            std::coroutine_handle<> m_continue = nullptr;
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
            std::coroutine_handle<> m_suspend_coroutine;

            Awaiter(std::coroutine_handle<promise_type> h) : m_h(h)
            {
            }
            bool await_ready()
            {
                return false;
            }

            void await_suspend(std::coroutine_handle<> h)
            {
                m_suspend_coroutine = h;
                TINYASYNC_GUARD("Task::Awaiter::await_resume()(`%s`):", name(m_h));

                TINYASYNC_LOG("set continuum of `%s` to `%s`", name(m_h), name(h));
                m_h.promise().m_continue = h;
                TINYASYNC_LOG("`%s` suspended, resume task `%s`", name(h), name(m_h));
                m_h.resume();
                TINYASYNC_LOG("resumed from `%s`, `%s` already suspend, back to caller/resumer", name(m_h), name(h));
            }

            void await_resume()
            {
                TINYASYNC_GUARD("Task::Awaiter::await_resume()(`%s`):", name(m_h));
                TINYASYNC_LOG("resuming %s", name(m_suspend_coroutine));
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

    struct Spawn : NamedMixin<Spawn>
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
                TINYASYNC_LOG("%d bytes", (int)size);
                return ::operator new(size);
            }

            static void operator delete(void *ptr, std::size_t size)
            {
                TINYASYNC_GUARD("Spawn::Promise::operator delete():");
                TINYASYNC_LOG("%d bytes", (int)size);
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
    struct Callback
    {
        virtual void callback(IoEvent &evt) = 0;
    };
#endif

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

        void abort()
        {
            m_abort_requested = true;
        }

        void run()
        {

#ifdef _WIN32
            for (;;)
            {
                // get thread into alternative state
                // in thi state, the callback get a chance to be invoked
                SleepEx(INFINITE, TRUE);
            }

#elif defined(__unix__)

            for (; !m_abort_requested;)
            {
                int const maxevents = 5;
                epoll_event events[maxevents];
                auto epfd = m_native_handle;
                int const timeout = -1; // indefinitely

                //printf("epool_wait ...\n");

                TINYASYNC_GUARD("IoContex::run():");
                TINYASYNC_LOG("epoll_wait ...");
                int nfds = epoll_wait(epfd, (epoll_event *)events, maxevents, timeout);

                //printf("epool_wait finished: %d fd ready\n", nfds);

                if (nfds == -1)
                {
                    throw std::system_error(errno, std::system_category(), "epoll wait error");
                }

                // for each event
                for (auto i = 0; i < nfds; ++i)
                {

                    TINYASYNC_LOG("evt %d of %d", i, nfds);
                    auto &evt = events[i];
                    auto callback = (Callback *)evt.data.ptr;
                    callback->callback(evt);
                }
            }

#endif
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

    struct Address
    {
        Address()
        {
            // if we bind on this address,
            // We will listen to all network card(s)
            m_v6 = INADDR_ANY;
        }

        static Address Any()
        {
            return {};
        }

        union
        {
            uint64_t m_v4;
            uint64_t m_v6;
        };
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

    struct ConnImpl;

    class AsyncReceiveAwaiter : public std::suspend_always
    {

        friend class ConnImpl;
        AsyncReceiveAwaiter *m_next;
        IoContext *m_ctx;
        ConnImpl *m_conn;
        std::coroutine_handle<> m_suspend_coroutine;
        void *m_buffer_addr;
        std::size_t m_buffer_size;
        std::size_t m_bytes_transfer;

    public:
        AsyncReceiveAwaiter(ConnImpl &conn, void *b, std::size_t n);
        void await_suspend(std::coroutine_handle<> h);
        std::size_t await_resume();
    };

    class AsyncSendAwaiter : public std::suspend_always
    {
        friend class ConnImpl;
        AsyncSendAwaiter *m_next;
        IoContext *m_ctx;
        ConnImpl *m_conn;
        std::coroutine_handle<> m_suspend_coroutine;
        void const *m_buffer_addr;
        std::size_t m_buffer_size;
        std::size_t m_bytes_transfer;

    public:
        AsyncSendAwaiter(ConnImpl &conn, void const *b, std::size_t n);
        void await_suspend(std::coroutine_handle<> h);
        std::size_t await_resume();
    };

    class ConnImpl
    {

        friend class AsyncReceiveAwaiter;
        friend class AsyncSendAwaiter;

        IoContext *m_ctx;
        NativeHandle m_conn_handle;
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

                new (this) ConnImpl(*m_ctx, NULL_HANDLE, false);
            }
        }

        ConnImpl(IoContext &ctx, NativeHandle conn_sock, bool already_bind_to_epoll)
        {
            TINYASYNC_GUARD("ConnImpl");

            m_ctx = &ctx;
            m_conn_handle = conn_sock;

            if (conn_sock && !already_bind_to_epoll)
            {
                TINYASYNC_LOG("setnonblocking %d", conn_sock);
                setnonblocking(conn_sock);

                epoll_event ev;
                //ev.events = EPOLLIN | EPOLLOUT;
                ev.events = EPOLLIN;
                ev.data.ptr = &m_callback;
                TINYASYNC_LOG("epoll_ctl %d", conn_sock);
                if (epoll_ctl(m_ctx->handle(), EPOLL_CTL_ADD, conn_sock, &ev) == -1)
                {
                    throw std::system_error(errno, std::system_category(), "can't bind connect to epoll");
                }
            }
        }

        ConnImpl(ConnImpl &&r) = delete;
        ConnImpl &operator=(ConnImpl &&r) = delete;
        ConnImpl(ConnImpl const &) = delete;
        ConnImpl &operator=(ConnImpl const &) = delete;

        ~ConnImpl() noexcept
        {
            this->reset();
        }

        AsyncReceiveAwaiter async_receive(void *buffer, std::size_t bytes)
        {
            TINYASYNC_GUARD("Connection:receive()");
            TINYASYNC_LOG("");
            return {*this, buffer, bytes};
        }

        AsyncSendAwaiter async_send(void const *buffer, std::size_t bytes)
        {
            TINYASYNC_GUARD("Connection:send()");
            TINYASYNC_LOG("");
            return {*this, buffer, bytes};
        }

    public:
        struct ConnCallback : Callback
        {

            ConnImpl *m_conn;
            ConnCallback(ConnImpl *conn) : m_conn(conn)
            {
                TINYASYNC_GUARD("ConnCallback::ConnectionCallback():");
                TINYASYNC_LOG("conn %p", conn);
            }

            void callback(IoEvent &evt) override;

        } m_callback{this};
    };


    void ConnImpl::ConnCallback::callback(IoEvent &evt)
    {
        TINYASYNC_GUARD("ConnCallback::callback():");

        if (evt.events | EPOLLIN)
        {

            TINYASYNC_LOG("in for fd %d", m_conn->m_conn_handle);
            auto awaiter = m_conn->m_recv_awaiter;
            if(awaiter) {


                int nbytes = recv(m_conn->m_conn_handle, awaiter->m_buffer_addr, (int)awaiter->m_buffer_size, 0);

                if (nbytes < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        TINYASYNC_LOG("EAGAIN, fd = %d", m_conn->m_conn_handle);
                        // just ok...
                    }
                    else
                    {
                        TINYASYNC_LOG("ERROR = %d, fd = %d", errno, m_conn->m_conn_handle);
                        throw std::system_error(errno, std::system_category(), "recv error");
                    }
                } else {
                    TINYASYNC_LOG("fd = %d, %d bytes read", m_conn->m_conn_handle, nbytes);
                    awaiter->m_bytes_transfer = nbytes;
                    awaiter->m_suspend_coroutine.resume();
                }
            }

        }
        
        if (evt.events | EPOLLOUT)
        {
            TINYASYNC_LOG("out for fd %d", m_conn->m_conn_handle);
            auto awaiter = m_conn->m_send_awaiter;
            if(awaiter) {

                int nbytes = ::send(m_conn->m_conn_handle, awaiter->m_buffer_addr, (int)awaiter->m_buffer_size, 0);

                if (nbytes < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        TINYASYNC_LOG("EAGAIN, fd = %d", m_conn->m_conn_handle);
                        // just ok...
                    }
                    else
                    {
                        TINYASYNC_LOG("ERROR = %d, fd = %d", errno, m_conn->m_conn_handle);
                        throw std::system_error(errno, std::system_category(), "send error");
                    }
                } else {
                    TINYASYNC_LOG("fd = %d, %d bytes sent", m_conn->m_conn_handle, nbytes);
                    awaiter->m_bytes_transfer = nbytes;
                    awaiter->m_suspend_coroutine.resume();
                }

            }

        }

        if(!(evt.events &(EPOLLIN|EPOLLOUT)))
        {
            printf("unkonw events %x", evt.events);
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
        TINYASYNC_LOG("set recv_awaiter of %p to %p", m_conn, this);
    }

    std::size_t AsyncReceiveAwaiter::await_resume()
    {
        TINYASYNC_GUARD("AsyncReceiveAwaiter::await_resume():");
        TINYASYNC_LOG("");
        // pop from front of list
        m_conn->m_recv_awaiter = m_conn->m_recv_awaiter->m_next;
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
    }

    std::size_t AsyncSendAwaiter::await_resume()
    {
        TINYASYNC_GUARD("AsyncSendAwaiter::await_resume():");
        TINYASYNC_LOG("");
        // pop from front of list
        m_conn->m_send_awaiter = m_conn->m_send_awaiter->m_next;
        return m_bytes_transfer;
    }

    class Connection
    {
        // use unique_ptr because
        // 1. fast move/swap
        // 2. reference stable, usefull for callbacks
        std::unique_ptr<ConnImpl> m_impl;
    public:
        Connection(IoContext &ctx, NativeHandle conn_sock, bool already_bind_to_epoll = false)
        {
            m_impl.reset(new ConnImpl(ctx, conn_sock, already_bind_to_epoll));
        }

        AsyncReceiveAwaiter async_receive(void *buffer, std::size_t bytes)
        {
            return m_impl->async_receive(buffer, bytes);
        }

        AsyncSendAwaiter async_send(void const *buffer, std::size_t bytes)
        {
            return m_impl->async_send(buffer, bytes);
        }

    };


    class AcceptAwaiter : public std::suspend_always
    {

        friend class AcceptorImpl;

        AcceptorImpl *m_acceptor;
        IoContext *m_ctx;
        AcceptAwaiter *m_next;
        std::coroutine_handle<> m_suspend_coroutine;

    public:
        AcceptAwaiter(AcceptorImpl &acceptor);
        void await_suspend(std::coroutine_handle<> h);
        Connection await_resume();
    };

    class AcceptorImpl
    {
        friend class AcceptAwaiter;

    private:
        IoContext *m_ctx;
        NativeHandle m_listen_handle;
        bool m_listen_bind_epoll;

        Protocol m_protocol;
        Endpoint m_endpoint;
        std::coroutine_handle<> m_suspend_coroutine;
        AcceptAwaiter *m_awaiter;

    public:
        AcceptorImpl(IoContext &ctx)
        {
            m_ctx = &ctx;
            m_listen_handle = NULL_HANDLE;
            m_listen_bind_epoll = false;
            m_awaiter = nullptr;
        }

        AcceptorImpl(IoContext &ctx, Protocol protocol, Endpoint endpoint) : AcceptorImpl(ctx)
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
                this->reset();
            }
        }

        AcceptorImpl(AcceptorImpl &&r) = delete;
        AcceptorImpl &operator=(AcceptorImpl &&r) = delete;
        AcceptorImpl(AcceptorImpl const &) = delete;
        AcceptorImpl &operator=(AcceptorImpl const &) = delete;

        ~AcceptorImpl()
        {
            this->reset();
        }

        void reset()
        {

            if (m_listen_handle)
            {
                if (m_listen_bind_epoll)
                {
                    // ...
                }

                close_handle(m_listen_handle);
                new(this) AcceptorImpl(*m_ctx);
            }
        }

        void open(Protocol protocol)
        {

            // PF means protocol
            auto listenfd = ::socket(PF_INET, SOCK_STREAM, 0);
            if (listenfd == -1)
            {
                throw std::system_error(errno, std::system_category(), "can't open socket");
            }

            setnonblocking(listenfd);

            m_listen_handle = listenfd;
            m_protocol = protocol;
        }

        void bind(Endpoint endpoint)
        {

            int listenfd = m_listen_handle;

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
                throw std::system_error(errno, std::system_category(), "can't bind socket");
            }

            m_endpoint = endpoint;
        }

        struct AcceptCallback : Callback
        {

            AcceptorImpl *m_acceptor;

            AcceptCallback(AcceptorImpl *acceptor)
            {
                m_acceptor = acceptor;
            };

            virtual void callback(IoEvent &evt) override
            {
                TINYASYNC_GUARD("AcceptCallback::callback():");
                TINYASYNC_LOG("%p %p", m_acceptor, m_acceptor->m_awaiter);
                AcceptAwaiter *awaiter = m_acceptor->m_awaiter;
                awaiter->m_suspend_coroutine.resume();
            }

        } m_callback{this};

        void listen()
        {
            int listenfd = m_listen_handle;
            int max_pendding_connection = 5;
            int err = ::listen(listenfd, max_pendding_connection);
            if (err == -1)
            {
                throw std::system_error(errno, std::system_category(), "can't listen port");
            }

            // https://man7.org/linux/man-pages/man7/epoll.7.html
            epoll_event evt;
            evt.data.ptr = &m_callback;
            evt.events = EPOLLIN;
            auto fd3 = epoll_ctl(m_ctx->handle(), EPOLL_CTL_ADD, listenfd, &evt);
        }


        AcceptAwaiter async_accept()
        {
            return {*this};
        }
    };


    AcceptAwaiter::AcceptAwaiter(AcceptorImpl &acceptor) : m_acceptor(&acceptor)
    {
        assert(!m_acceptor->m_awaiter);
        m_acceptor->m_awaiter = this;
        m_ctx = m_acceptor->m_ctx;
    }

    void AcceptAwaiter::await_suspend(std::coroutine_handle<> h)
    {
        m_suspend_coroutine = h;
    }

    Connection AcceptAwaiter::await_resume()
    {

        TINYASYNC_GUARD("Acceptor::Awaiter::await_resume():");
        TINYASYNC_LOG("acceptor %p, awaiter %p", &m_acceptor, m_acceptor->m_awaiter);

        // it's ready to accept
        int conn_sock = ::accept(m_acceptor->m_listen_handle, NULL, NULL);
        if (conn_sock == -1)
        {
            throw std::system_error(errno, std::system_category(), "can't accept");
        }

        m_acceptor->m_awaiter = nullptr;
        return {*m_ctx, conn_sock};
    }

    class Acceptor {

    public:
        Acceptor(IoContext &ctx, Protocol protocol, Endpoint endpoint)  {
            m_impl.reset(new AcceptorImpl(ctx, protocol, endpoint));
        }

        AcceptAwaiter async_accept()
        {
            return m_impl->async_accept();
        }

    private:
        std::unique_ptr<AcceptorImpl> m_impl;
    };



    struct TimerAwaiter : std::suspend_always
    {

        IoContext &m_ctx;
        uint64_t m_elapse; // mili-second
        NativeHandle m_timer_handle = 0;
        std::coroutine_handle<> m_suspend_coroutine;

        // elapse in micro-second
        TimerAwaiter(IoContext &ctx, uint64_t elapse) : m_ctx(ctx), m_elapse(elapse)
        {
        }

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

} // namespace tinyasync

#endif // TINYASYNC_H
