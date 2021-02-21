#ifndef TINYASYNC_AWAITERS_H
#define TINYASYNC_AWAITERS_H

namespace tinyasync {

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
        std::coroutine_handle<TaskPromiseBase> m_suspend_coroutine;
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

        template<class Promise>
        inline void await_suspend(std::coroutine_handle<Promise> suspend_coroutine) {
            std::coroutine_handle<TaskPromiseBase> h = suspend_coroutine.promise().coroutine_handle_base();
            await_suspend(h);
        }

        bool await_suspend(std::coroutine_handle<TaskPromiseBase> h);
        std::size_t await_resume();
    };

    class AsyncSendAwaiter : public std::suspend_always,
        public DataAwaiterMixin<AsyncSendAwaiter, void const*>
    {
    public:
        friend class ConnImpl;
        AsyncSendAwaiter(ConnImpl& conn, void const* b, std::size_t n);

        template<class Promise>
        inline void await_suspend(std::coroutine_handle<Promise> suspend_coroutine) {
            std::coroutine_handle<TaskPromiseBase> h = suspend_coroutine.promise().coroutine_handle_base();
            await_suspend(h);
        }

        bool await_suspend(std::coroutine_handle<TaskPromiseBase> h);
        std::size_t await_resume();
    };

    class AsyncCloseAwaiter {
        friend class ConnImpl;
        ConnImpl *m_conn;

    public:
        AsyncCloseAwaiter(ConnImpl *conn) {
            m_conn = conn;
        }

        bool await_ready();

        void await_suspend(std::coroutine_handle<>) {            
            assert(false);
        }

        void await_resume() {            
            assert(false);
        }

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
        friend class AsyncCloseAwaiter;
        friend class ConnCallback;


        IoContext* m_ctx;
        NativeSocket m_conn_handle;
        ConnCallback m_callback{ this };
        AsyncReceiveAwaiter* m_recv_awaiter = nullptr;
        AsyncSendAwaiter* m_send_awaiter = nullptr;
        bool m_added_to_event_pool = false;

    public:


        void register_()
        {
            auto m_conn = this;
            epoll_event evt;
            evt.events = 0;
            if(m_conn->m_recv_awaiter) {
                evt.events |= EPOLLIN;
            }
            if(m_conn->m_send_awaiter) {
                evt.events |= EPOLLOUT;
            }
            if(!evt.events) {
                return;
            }
            evt.events |= EPOLLONESHOT;
            evt.data.ptr = &m_conn->m_callback;

            int epoll_clt_addmod;
            if(!m_conn->m_added_to_event_pool) {
                epoll_clt_addmod = EPOLL_CTL_ADD;
                TINYASYNC_LOG("epoll_ctl(EPOLL_CTL_ADD, %s) for conn_handle = %d",
                    ioe2str(evt).c_str(), m_conn->m_conn_handle);
                m_conn->m_added_to_event_pool = true;
            } else {
                epoll_clt_addmod = EPOLL_CTL_MOD;
                TINYASYNC_LOG("epoll_ctl(EPOLL_CTL_MOD, %s) for conn_handle = %d",
                    ioe2str(evt).c_str(), m_conn->m_conn_handle);
            }

            int clterr = epoll_ctl(m_ctx->event_poll_handle(), epoll_clt_addmod, m_conn->m_conn_handle, &evt);
            if(clterr == -1) {
                TINYASYNC_LOG("epoll_ctl failed for conn_handle = %d", m_conn->m_conn_handle);
                throw_errno(format("can't epoll_clt for conn_handle = %s", socket_c_str(m_conn->m_conn_handle)));
            }
        }

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

        AsyncCloseAwaiter async_close() {
            return {this};
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

        if ((evt.events & EPOLLIN) && m_conn->m_recv_awaiter) {
            // we want to read and it's ready to read

            TINYASYNC_LOG("ready to read for conn_handle %d", m_conn->m_conn_handle);

            auto awaiter = m_conn->m_recv_awaiter;
            int nbytes = recv(m_conn->m_conn_handle, awaiter->m_buffer_addr, (int)awaiter->m_buffer_size, 0);
            TINYASYNC_LOG("recv %d bytes", nbytes);

            if(nbytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { 
                // try again latter ...
            } else {
                // may cause Connection self deleted
                // so we should not want to read and send at the same
                awaiter->m_bytes_transfer = (std::ptrdiff_t)nbytes;
                TINYASYNC_RESUME(awaiter->m_suspend_coroutine);
            }
        } else if ((evt.events & EPOLLOUT) && m_conn->m_send_awaiter) {
            // we want to send and it's ready to read

            auto awaiter = m_conn->m_send_awaiter;
            TINYASYNC_LOG("ready to send for conn_handle %d, %d bytes at %p sending", m_conn->m_conn_handle, (int)awaiter->m_buffer_size, awaiter->m_buffer_addr);
            int nbytes = ::send(m_conn->m_conn_handle, awaiter->m_buffer_addr, (int)awaiter->m_buffer_size, 0);
            TINYASYNC_LOG("sent %d bytes", nbytes);

            if(nbytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { 
                // try again latter ...
            } else {
                // may cause Connection self deleted
                // so we should not want to read and send at the same
                awaiter->m_bytes_transfer = (std::ptrdiff_t)nbytes;
                TINYASYNC_RESUME(awaiter->m_suspend_coroutine);
            }
        } else {
            TINYASYNC_LOG("not processed event for conn_handle %x", errno, m_conn->m_conn_handle);
            fprintf(stderr, "%s\n", ioe2str(evt).c_str());
            exit(1);
        }
#endif

    }

    bool AsyncCloseAwaiter::await_ready() {
        m_conn->reset();
        return true;
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

    bool AsyncReceiveAwaiter::await_suspend(std::coroutine_handle<TaskPromiseBase> h)
    {
        TINYASYNC_GUARD("AsyncReceiveAwaiter.await_suspend(): ");
        TINYASYNC_LOG("try to receive %zu bytes from %s", m_buffer_size, socket_c_str(m_conn->m_conn_handle));
        m_suspend_coroutine = h;

        // insert into front of list
        this->m_next = m_conn->m_recv_awaiter;
        m_conn->m_recv_awaiter = this;
        TINYASYNC_LOG("set recv_awaiter of conn(%p) to %p", m_conn, m_conn->m_recv_awaiter);


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
        m_conn->register_();
        return true;
#endif

    }


    std::size_t AsyncReceiveAwaiter::await_resume()
    {
        TINYASYNC_GUARD("AsyncReceiveAwaiter.await_resume(): ");

        auto nbytes = (std::ptrdiff_t)m_bytes_transfer;
        if (nbytes < 0) {
            TINYASYNC_LOG("ERROR = %d, fd = %d", errno, m_conn->m_conn_handle);
            throw_errno("AsyncReceiveAwaiter::await_resume(): recv error");
        } else {
            if (nbytes == 0) {
                if (errno == ESHUTDOWN) {
                    TINYASYNC_LOG("ESHUTDOWN, fd = %d", m_conn->m_conn_handle);
                }
            }
            TINYASYNC_LOG("fd = %d, %d bytes read", m_conn->m_conn_handle, nbytes);
        }

        // pop from front of list
        m_conn->m_recv_awaiter = m_conn->m_recv_awaiter->m_next;
        TINYASYNC_LOG("set recv_awaiter of conn(%p) to %p", m_conn, m_conn->m_recv_awaiter);
        m_conn->register_();
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

    bool AsyncSendAwaiter::await_suspend(std::coroutine_handle<TaskPromiseBase> h)
    {
        TINYASYNC_LOG("AsyncSendAwaiter::await_suspend(): ");
        m_suspend_coroutine = h;

        // insert front of list
        this->m_next = m_conn->m_send_awaiter;
        m_conn->m_send_awaiter = this;
        TINYASYNC_LOG("set send_awaiter of conn(%p) to %p", m_conn, m_conn->m_send_awaiter);

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
        m_conn->register_();
        return true;
#endif
    }

    std::size_t AsyncSendAwaiter::await_resume()
    {
        TINYASYNC_GUARD("AsyncSendAwaiter.await_resume(): ");

        auto nbytes = (std::ptrdiff_t)m_bytes_transfer;
        if (nbytes < 0) {
            TINYASYNC_LOG("ERROR = %d, fd = %d", errno, m_conn->m_conn_handle);
            throw_errno("AsyncSendAwaiter::await_resume(): send error");
        } else {
            if (nbytes == 0) {
                if (errno == ESHUTDOWN) {
                    TINYASYNC_LOG("ESHUTDOWN, fd = %d", m_conn->m_conn_handle);
                }
            }
            TINYASYNC_LOG("fd = %d, %d bytes read", m_conn->m_conn_handle, nbytes);
        }

        // pop from front of list
        m_conn->m_send_awaiter = m_conn->m_send_awaiter->m_next;
        TINYASYNC_LOG("set recv_awaiter of conn(%p) to %p", m_conn, m_conn->m_send_awaiter);

        m_conn->register_();
        return m_bytes_transfer;
    }

    class Connection
    {
        // use unique_ptr because
        // 1. fast move/swap
        // 2. reference stable, usefull for callbacks
        std::unique_ptr<ConnImpl> m_impl;
    public:
    
        Connection() = default;

        Connection(IoContext& ctx, NativeSocket conn_sock, bool added_event_poll)
        {
            m_impl.reset(new ConnImpl(ctx, conn_sock, added_event_poll));
        }

        AsyncReceiveAwaiter async_read(void* buffer, std::size_t bytes)
        {
            return m_impl->async_read(buffer, bytes);
        }

        AsyncReceiveAwaiter async_read(Buffer buffer)
        {
            return m_impl->async_read(buffer.data(), buffer.size());
        }

        AsyncSendAwaiter async_send(void const* buffer, std::size_t bytes)
        {
            return m_impl->async_send(buffer, bytes);
        }

        AsyncSendAwaiter async_send(ConstBuffer buffer)
        {
            return m_impl->async_send(buffer.data(), buffer.size());
        }

        AsyncCloseAwaiter async_close() {
            return m_impl->async_close();
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
                    auto ctlerr = epoll_ctl(m_ctx->event_poll_handle(), EPOLL_CTL_DEL, m_socket, NULL);
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
        std::coroutine_handle<TaskPromiseBase> m_suspend_coroutine;

    public:
        bool await_ready() { return false; }
        AcceptorAwaiter(AcceptorImpl& acceptor);

        template<class Promise>
        inline void await_suspend(std::coroutine_handle<Promise> suspend_coroutine) {
            auto h = suspend_coroutine.promise().coroutine_handle_base();
            await_suspend(h);
        }
        bool await_suspend(std::coroutine_handle<TaskPromiseBase> h);
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

    bool AcceptorAwaiter::await_suspend(std::coroutine_handle<TaskPromiseBase> h)
    {
        assert(!m_acceptor->m_awaiter);
        m_acceptor->m_awaiter = this;
        m_suspend_coroutine = h;
        TINYASYNC_GUARD("AcceptorAwaiter::await_suspend(): ");

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
            auto ctlerr = epoll_ctl(m_acceptor->m_ctx->event_poll_handle(), EPOLL_CTL_ADD, listenfd, &evt);
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
        std::coroutine_handle<TaskPromiseBase> m_suspend_coroutine;
        void unregister(NativeSocket conn_handle);

    public:
        ConnectorAwaiter(ConnectorImpl& connector) : m_connector(&connector)
        {
        }

        constexpr bool await_ready() const { return false; }

        template<class Promise>
        inline void await_suspend(std::coroutine_handle<Promise> suspend_coroutine) {
            auto h = suspend_coroutine.promise().coroutine_handle_base();
            await_suspend(h);
        }

        bool await_suspend(std::coroutine_handle<TaskPromiseBase> suspend_coroutine);

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
        // epoll_event evt;
        // evt.data.ptr = nullptr;
        // evt.events = 0;
        // we can't handle error?
        // so we remove it from epoll
        auto clterr = epoll_ctl(m_connector->m_ctx->event_poll_handle(), EPOLL_CTL_DEL, connfd, NULL);
        if (clterr == -1) {
            throw_errno(format("can't unregister conn_handle = %d", connfd));
        }
#endif

        TINYASYNC_LOG("unregister connect for conn_handle = %s", socket_c_str(connfd));
    }

    bool ConnectorAwaiter::await_suspend(std::coroutine_handle<TaskPromiseBase> suspend_coroutine)
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
        auto clterr = epoll_ctl(m_connector->m_ctx->event_poll_handle(), EPOLL_CTL_ADD, m_connector->m_socket, &evt);
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
        return { *m_connector->m_ctx, m_connector->m_socket, false};
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
        std::chrono::nanoseconds m_elapse; // mili-second
        NativeHandle m_timer_handle = NULL_HANDLE;
        std::coroutine_handle<TaskPromiseBase> m_suspend_coroutine;
        TimerCallback m_callback_ = this;
        Callback *m_callback = &m_callback_;

    public:
        // interface to wait
        using callback_type = TimerCallback;

        TimerCallback *get_callback() {
            return &m_callback_;
        }

        void set_callback(Callback *c) {
            m_callback = c;
        }


        // elapse in micro-second
        TimerAwaiter(IoContext& ctx, std::chrono::nanoseconds elapse) : m_ctx(&ctx), m_elapse(elapse)
        {
        }

        constexpr bool await_ready() const noexcept { return false; }

        template<class Promise>
        inline void await_suspend(std::coroutine_handle<Promise> suspend_coroutine)
        {
            auto h = suspend_coroutine.promise().coroutine_handle_base();
            await_suspend(h);
        }

        void await_resume() const noexcept {
#ifdef _WIN32
            // timer thread have done cleaning up
#elif defined(__unix__)
            // remove from epoll list
            epoll_ctl(m_ctx->event_poll_handle(), m_timer_handle, EPOLL_CTL_DEL, NULL);
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
                // QuadPart is in 100ns
                elapse.QuadPart = -awaiter->m_elapse.count()/100;

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

        inline void await_suspend(std::coroutine_handle<TaskPromiseBase> h)
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

        inline void await_suspend(std::coroutine_handle<TaskPromiseBase> h)
        {

            // create a timer
            m_suspend_coroutine = h;
            itimerspec time;
            time.it_value = to_timespec(m_elapse);
            time.it_interval = to_timespec(std::chrono::nanoseconds{0});

            auto fd = timerfd_create(CLOCK_REALTIME, 0);
            if (fd == -1) {
                throw_errno("can't create timer");
            }

            try {
                auto fd2 = timerfd_settime(fd, 0, &time, NULL);
                if (fd2 == -1) {
                    throw_errno("can't set timer");
                }

                auto epfd = m_ctx->event_poll_handle();

                epoll_event evt;
                evt.data.ptr = m_callback;
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




    TimerAwaiter async_sleep(IoContext& ctx, std::chrono::nanoseconds elapse)
    {
        return TimerAwaiter(ctx, elapse);
    }




} // namespace tinyasync


#endif
