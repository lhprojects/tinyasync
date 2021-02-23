#ifndef TINYASYNC_IOCONTEXT_H
#define TINYASYNC_IOCONTEXT_H

namespace tinyasync
{

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

    struct Callback
    {

        // we don't use virtual table for two reasons
        //     1. virtual function let Callback to be non-standard_layout, though we have solution without offsetof using inherit
        //     2. we have only one function ptr, thus ... we can save a memory load without virtual functions table
        using CallbackPtr = void (*)(Callback *self, IoEvent &);

        CallbackPtr m_callback;

        void callback(IoEvent &evt)
        {
            this->m_callback(this, evt);
        }

#ifdef _WIN32
        OVERLAPPED m_overlapped;

        static Callback *from_overlapped(OVERLAPPED *o)
        {
            constexpr std::size_t offset = offsetof(Callback, m_overlapped);
            Callback *callback = reinterpret_cast<Callback *>((reinterpret_cast<char *>(o) - offset));
            return callback;
        }
#endif
    };

    static constexpr std::size_t Callback_size = sizeof(Callback);
    static_assert(std::is_standard_layout_v<Callback>);

    struct CallbackImplBase : Callback
    {

        // implicit null is not allowed
        CallbackImplBase(std::nullptr_t)
        {
            m_callback = nullptr;
        }

        template <class SubclassCallback>
        CallbackImplBase(SubclassCallback *)
        {
            (void)static_cast<SubclassCallback *>(this);
            //memset(&m_overlapped, 0, sizeof(m_overlapped));
            m_callback = &invoke_impl_callback<SubclassCallback>;
        }

        template <class SubclassCallback>
        static void invoke_impl_callback(Callback *this_, IoEvent &evt)
        {
            // invoke subclass' on_callback method
            SubclassCallback *subclass_this = static_cast<SubclassCallback *>(this_);
            subclass_this->on_callback(evt);
        }
    };
    static constexpr std::size_t CallbackImplBase_size = sizeof(CallbackImplBase);
    static_assert(std::is_standard_layout_v<CallbackImplBase>);

    struct PostTask
    {
        // use internally
        ListNode m_node;
        // your callback
        using callback_type = void (*)(PostTask *);
        callback_type m_callback;

        static PostTask *from_node(ListNode *node)
        {
            return (PostTask *)((char *)node - offsetof(PostTask, m_node));
        }
    };

    class IoCtxBase
    {
    public:
        virtual void run() = 0;
        virtual void post_task(PostTask *) = 0;
        virtual void request_abort() = 0;
        virtual ~IoCtxBase() {}

        NativeHandle m_epoll_handle = NULL_HANDLE;
        std::pmr::memory_resource *m_memory_resource;
        NativeHandle event_poll_handle()
        {
            return m_epoll_handle;
        }
    };

    class IoContext
    {
        std::unique_ptr<IoCtxBase> m_ctx;

    public:

        template <bool multiple_thread = false>
        IoContext(std::integral_constant<bool, multiple_thread> = std::false_type());

        IoCtxBase *get_io_ctx_base() {
            return m_ctx.get();
        }

        std::pmr::memory_resource *get_memory_resource_for_task()
        {
            auto *ctx = m_ctx.get();
            return ctx->m_memory_resource;
        }
        void run()
        {
            auto *ctx = m_ctx.get();
            ctx->run();
        }

        void post_task(PostTask *task)
        {
            auto *ctx = m_ctx.get();
            ctx->post_task(task);
        }
        
        void request_abort()
        {
            auto *ctx = m_ctx.get();
            ctx->request_abort();
        }

        NativeHandle event_poll_handle()
        {
            auto *ctx = m_ctx.get();
            return ctx->m_epoll_handle;
        }
    };



    struct SingleThreadTrait
    {
        using spinlock_type = NaitveLock;
        static constexpr bool multiple_thread = false;
        static std::pmr::memory_resource *get_memory_resource() {
            return std::pmr::get_default_resource();
        }
    };

    struct MultiThreadTrait
    {
        using spinlock_type = DefaultSpinLock;
        static constexpr bool multiple_thread = true;
        static std::pmr::memory_resource *get_memory_resource() {
            return std::pmr::get_default_resource();
        }
    };

    template <class CtxTrait>
    class IoCtx : public IoCtxBase
    {
        IoCtx(IoCtx &&r) = delete;
        IoCtx &operator=(IoCtx &&r) = delete;

        NativeHandle m_wakeup_handle = NULL_HANDLE;

        typename CtxTrait::spinlock_type m_que_lock;

        std::size_t m_thread_waiting = 0;
        std::size_t m_task_queue_size = 0;
        Queue m_task_queue;
        bool m_abort_requested = false;
        static const bool k_multiple_thread = CtxTrait::multiple_thread;

    public:
        IoCtx();
        void post_task(PostTask *callback) override;
        void request_abort() override;
        void run() override;
        ~IoCtx() override;
    };


    template <bool multiple_thread>
    IoContext::IoContext(std::integral_constant<bool, multiple_thread>)
    {
        // if you needn't multiple thread
        // you don't have to link with e.g. pthread library
        if constexpr (multiple_thread)
        {
            m_ctx = std::make_unique<IoCtx<MultiThreadTrait>>();
        }
        else
        {
            m_ctx = std::make_unique<IoCtx<SingleThreadTrait>>();
        }
    }

    template <class T>
    IoCtx<T>::IoCtx()
    {

        TINYASYNC_GUARD("IoContext.IoContext(): ");

        m_memory_resource = T::get_memory_resource();
#ifdef _WIN32

        WSADATA wsaData;
        int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != NO_ERROR)
        {
            throw_WASError("WSAStartup failed with error ", iResult);
        }

        int num_thread = 1;
        m_native_handle = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, num_thread);
        if (m_native_handle == NULL)
        {
            throw_LastError("can't create event poll");
        }

#elif defined(__unix__)

        auto fd = epoll_create1(EPOLL_CLOEXEC);
        if (fd == -1)
        {
            throw_errno("IoContext().IoContext(): can't create epoll");
        }
        m_epoll_handle = fd;

        fd = eventfd(1, EFD_NONBLOCK);
        if (fd == -1)
        {
            throw_errno("IoContext().IoContext(): can't create eventfd");
        }
        m_wakeup_handle = fd;

#endif
        TINYASYNC_LOG("event poll created");
    }

    template <class T>
    IoCtx<T>::~IoCtx()
    {
#ifdef _WIN32

        WSACleanup();
#elif defined(__unix__)

        if (m_wakeup_handle)
        {
            ::epoll_ctl(m_epoll_handle, EPOLL_CTL_DEL, m_wakeup_handle, NULL);
            close_handle(m_wakeup_handle);
        }
        close_handle(m_epoll_handle);
#endif
    }

    template <class T>
    void IoCtx<T>::post_task(PostTask *task)
    {

        if constexpr (k_multiple_thread)
        {
            m_que_lock.lock();
            m_task_queue.push(&task->m_node);
            m_task_queue_size += 1;
            auto thread_wating = m_thread_waiting;
            m_que_lock.unlock();

            if (thread_wating > 0)
            {
                epoll_event evt;
                evt.data.ptr = (void *)1;
                evt.events = EPOLLIN | EPOLLONESHOT;
                epoll_ctl(m_epoll_handle, EPOLL_CTL_MOD, m_wakeup_handle, &evt);
            }
        }
        else
        {
            m_task_queue.push(&task->m_node);
        }
    }

    template <class T>
    void IoCtx<T>::request_abort()
    {
        m_que_lock.lock();
        m_abort_requested = true;
        m_que_lock.unlock();
    }

    template <class T>
    void IoCtx<T>::run()
    {

        Callback *const CallbackGuard = (Callback *)8;

        TINYASYNC_GUARD("IoContex::run(): ");
        int const maxevents = 5;

        for (;;)
        {

            if constexpr (k_multiple_thread)
            {
                m_que_lock.lock();
            }
            bool empty__;
            auto node = m_task_queue.pop(empty__);
            bool abort_requested = m_abort_requested;

            if (abort_requested)
                TINYASYNC_UNLIKELY
                {
                    if constexpr (k_multiple_thread)
                    {
                        m_que_lock.unlock();
                    }
                    // very rude ...
                    // TODO: clean up more carefully
                    break;
                }

            if (node)
            {
                // we have task to do
                if constexpr (k_multiple_thread)
                {
                    m_task_queue_size -= 1;
                    m_que_lock.unlock();
                }

                PostTask *task = PostTask::from_node(node);
                try
                {
                    (task->m_callback)(task);
                }
                catch (...)
                {
                    terminate_with_unhandled_exception();
                }
            }
            else
            {
                // no task
                // blocking by epoll_wait
                if constexpr (k_multiple_thread)
                {
                    m_thread_waiting += 1;
                    m_que_lock.unlock();
                }

                TINYASYNC_LOG("waiting event ...");

#ifdef _WIN32

                IoEvent evt;
                OVERLAPPED *overlapped;

                if (::GetQueuedCompletionStatus(m_native_handle,
                                                &evt.transfered_bytes,
                                                &evt.key,
                                                &overlapped, INFINITE) == 0)
                {
                    throw_LastError("GetQueuedCompletionStatus failed");
                }
                TINYASYNC_LOG("Get one event");
                Callback *callback = Callback::from_overlapped(overlapped);
                callback->callback(evt);

#elif defined(__unix__)

                IoEvent events[maxevents];
                const auto epfd = this->event_poll_handle();
                int const timeout = -1; // indefinitely

                int nfds = epoll_wait(epfd, (epoll_event *)events, maxevents, timeout);

                if constexpr (k_multiple_thread)
                {
                    m_que_lock.lock();
                    m_thread_waiting -= 1;
                    const auto task_queue_size = m_task_queue_size;
                    m_que_lock.unlock();

                    if (nfds == -1)
                    {
                        throw_errno("epoll_wait error");
                    }

                    // let's have a overview of event
                    size_t effective_event = 0;
                    size_t wakeup_event = 0;
                    for (auto i = 0; i < nfds; ++i)
                    {
                        auto &evt = events[i];
                        auto callback = (Callback *)evt.data.ptr;
                        if (callback < CallbackGuard)
                        {
                            wakeup_event = 1;
                            ++i;
                            for (; i < nfds; ++i)
                            {
                                auto &evt = events[i];
                                auto callback = (Callback *)evt.data.ptr;
                                if (callback < CallbackGuard)
                                {
                                    //
                                }
                                else
                                {
                                    effective_event = 1;
                                    break;
                                }
                            }
                            break;
                        }
                        else
                        {
                            effective_event = 1;
                            ++i;
                            for (; i < nfds; ++i)
                            {
                                auto &evt = events[i];
                                auto callback = (Callback *)evt.data.ptr;
                                if (callback < CallbackGuard)
                                {
                                    wakeup_event = 1;
                                    break;
                                }
                            }
                            break;
                        }
                    }

                    if (wakeup_event && m_thread_waiting && (task_queue_size + effective_event > 1))
                    {
                        // this is an wakeup event
                        // it means we may need to wakeup thread
                        // this is thread is to deal with effective_event
                        epoll_event evt;
                        evt.data.ptr = (void *)1;
                        evt.events = EPOLLIN | EPOLLONESHOT;
                        epoll_ctl(m_epoll_handle, EPOLL_CTL_MOD, m_wakeup_handle, &evt);
                    }
                }

                for (auto i = 0; i < nfds; ++i)
                {
                    auto &evt = events[i];
                    TINYASYNC_LOG("event %d of %d", i, nfds);
                    TINYASYNC_LOG("event = %x (%s)", evt.events, ioe2str(evt).c_str());
                    auto callback = (Callback *)evt.data.ptr;
                    if (callback >= CallbackGuard)
                    {
                        try
                        {
                            callback->callback(evt);
                        }
                        catch (...)
                        {
                            terminate_with_unhandled_exception();
                        }
                    }
                }

#endif

            } // if(node) ... else
        }     // for
    }         // run

} // namespace tinyasync

#endif
