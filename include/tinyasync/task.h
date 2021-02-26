#ifndef TINYASYNC_TASK_H
#define TINYASYNC_TASK_H

#include <exception>

namespace tinyasync {

    struct ResumeResult;

    template<class Result>
    class Task;


    class TaskPromiseBase {
    public:
        ResumeResult* m_resume_result;
        std::exception_ptr m_unhandled_exception = nullptr;
        std::coroutine_handle<TaskPromiseBase> m_continuum = nullptr;
        bool m_dangling = false;


        inline static void * do_alloc(std::size_t size, std::pmr::memory_resource *memory_resource)
        {
            // put allocator at the end of the frame
            auto memory_resource_size =  sizeof(std::pmr::memory_resource*);
            auto memory_resource_align =  alignof(std::pmr::memory_resource*);
            auto memory_resource_offset = (size  + memory_resource_align - 1u) & ~(memory_resource_align - 1u);

            auto ptr = memory_resource->allocate(memory_resource_offset + memory_resource_size);
            new((char*)ptr + memory_resource_offset) decltype(memory_resource)(memory_resource);
            return ptr;
        }


        template<class T>
        static void *alloc_1(std::size_t size, T &&) {
            auto memory_resource = std::pmr::get_default_resource();
            auto ptr = do_alloc(size, memory_resource);
            return ptr;
        }

        template<class T>
        static auto alloc_1(std::size_t size, T &a) -> std::enable_if_t<
            std::is_same_v<decltype(std::declval<std::remove_cvref_t<T> >().get_memory_resource_for_task()), std::pmr::memory_resource*>,
            void*>
        {
            auto memory_resource = a.get_memory_resource_for_task();
            auto ptr = do_alloc(size, memory_resource);
            return ptr;
        }

        template<class T, class... Args>
        static void* operator new(std::size_t size, T && a, Args &&... )
        {
            TINYASYNC_GUARD("Task.Promise.operator new(): ");
            auto ptr = alloc_1(size, a);
            TINYASYNC_LOG("%d bytes at %p", (int)(size), ptr);
            return ptr;
        }

        static void* operator new(std::size_t size)
        {
            TINYASYNC_GUARD("Task.Promise.operator new(): ");
            auto ptr = alloc_1(size, 0);
            TINYASYNC_LOG("%d bytes at %p", (int)(size), ptr);
            return ptr;
        }

        static void operator delete(void* ptr, std::size_t size)
        {
            TINYASYNC_GUARD("Task.Promise.operator delete(): ");
            TINYASYNC_LOG("%d bytes at %p", (int)size, ptr);

            auto memory_resource_size =  sizeof(std::pmr::memory_resource*);
            auto memory_resource_align =  alignof(std::pmr::memory_resource*);
            auto memory_resource_offset = (size  + memory_resource_align - 1u) & ~(memory_resource_align - 1u);

            auto memory_resource = *(std::pmr::memory_resource**)((char*)ptr + memory_resource_offset);
            memory_resource->deallocate(ptr, size);
        }

        bool is_dangling() const noexcept
        {
            return m_dangling;
        }

        std::coroutine_handle<TaskPromiseBase> coroutine_handle_base() noexcept
        {
            return std::coroutine_handle<TaskPromiseBase>::from_promise(*this);
        }


    };

    struct ResumeResult {
        std::coroutine_handle<TaskPromiseBase> m_return_from;
    };


    template<class Result>
    class PromiseResultMixin  {
    public:
        Result m_result;

        PromiseResultMixin() = default;
        
        template<class T>
        void return_value(T &&value)
        {
            m_result = std::forward<T>(value);
        }
        
    };

    template<>
    class PromiseResultMixin<void>  {
    public:
        void return_void() { }
    };

    template<class Result>
    class TaskPromise : public TaskPromiseBase,
        public PromiseResultMixin<Result>
    {
    public:
        using promise_type = TaskPromise<Result>;

        std::coroutine_handle<promise_type> coroutine_handle() noexcept
        {
            return std::coroutine_handle<promise_type>::from_promise(*this);
        }

        Task<Result> get_return_object();

        template <class... T>
        TaskPromise(T const &...args)
        {
            auto h = std::coroutine_handle<promise_type>::from_promise(*this);
            TINYASYNC_GUARD("Task(`%s`).Promise.Promise(): ", c_name(h));
            if (!set_name_r(h, args...)) {
                TINYASYNC_LOG("");
            }
        }

        TaskPromise(promise_type&& r) = delete;
        TaskPromise(promise_type const& r) = delete;
        ~TaskPromise()
        {
            auto h = std::coroutine_handle<promise_type>::from_promise(*this);
            TINYASYNC_GUARD("Task(`%s`).Promise.~Promise(): ", c_name(h));
            TINYASYNC_LOG("");
        }

        struct InitialAwaityer : std::suspend_always
        {
            std::coroutine_handle<promise_type> m_sub_coroutine;

            InitialAwaityer(std::coroutine_handle<promise_type> h) : m_sub_coroutine(h)
            {
            }

            void await_suspend(std::coroutine_handle<promise_type> suspended_coroutine) const noexcept
            {
                TINYASYNC_GUARD("Task(`%s`).InitialAwaityer.await_suspend(): ", c_name(m_sub_coroutine));
                TINYASYNC_LOG("`%s` suspended, back to caller", c_name(suspended_coroutine));
                // return to caller
                (void)suspended_coroutine;
            }

            void await_resume() const noexcept
            {
                TINYASYNC_GUARD("Task(`%s`).InitialAwaityer.await_resume(): ", c_name(m_sub_coroutine));
                TINYASYNC_LOG("`%s` resumed", c_name(m_sub_coroutine));
            }
        };


        InitialAwaityer initial_suspend()
        {
            return { coroutine_handle() };
        }

        struct FinalAwaiter : std::suspend_always
        {
            promise_type* m_promise;

            FinalAwaiter(promise_type& promise) noexcept : m_promise(&promise)
            {
            }

            bool await_ready() noexcept { return false; }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) const noexcept
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
                    promise->m_resume_result->m_return_from = h.promise().coroutine_handle_base();
                    return std::noop_coroutine();
                }
    }

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
            auto h = this->coroutine_handle();
            TINYASYNC_GUARD("Task(`%s`).Promise.unhandled_exception(): ", c_name(h));
            TINYASYNC_LOG("%s", to_string(std::current_exception()).c_str());
            (void)h;
            m_unhandled_exception = std::current_exception();
        }

    };

    template<class ToPromise, class Promise>
    std::coroutine_handle<ToPromise> change_promsie(std::coroutine_handle<Promise> h) {
        Promise &p = h.promise();
        return std::coroutine_handle<ToPromise>::from_promise(p);  
    }
        
    template<class Result = void>
    class TINYASYNC_NODISCARD Task
    {
    public:
        using promise_type = TaskPromise<Result>;
        using coroutine_handle_type =  std::coroutine_handle<TaskPromise<Result> >;
        using result_type = Result;

        coroutine_handle_type m_h;

        coroutine_handle_type coroutine_handle()
        {
            return m_h;
        }

        std::coroutine_handle<TaskPromiseBase> coroutine_handle_base() noexcept
        {
            TaskPromiseBase &promise = m_h.promise();
            return std::coroutine_handle<TaskPromiseBase>::from_promise(promise);
        }

        promise_type& promise()
        {
            return m_h.promise();
        }

        struct TINYASYNC_NODISCARD Awaiter
        {
            std::coroutine_handle<promise_type> m_sub_coroutine;

            Awaiter(std::coroutine_handle<promise_type> h) : m_sub_coroutine(h)
            {
            }

            bool await_ready() noexcept
            {
                return false;
            }

            template<class Promise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> suspend_coroutine) {
                std::coroutine_handle<TaskPromiseBase> h = suspend_coroutine.promise().coroutine_handle_base();
                return await_suspend(h);
            }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<TaskPromiseBase> suspend_coroutine)
            {
                TINYASYNC_GUARD("Task(`%s`).Awaiter.await_suspend(): ", c_name(m_sub_coroutine));

                TINYASYNC_LOG("set continuum of `%s` to `%s`", c_name(m_sub_coroutine), c_name(suspend_coroutine));
                TINYASYNC_LOG("`%s` suspended, resume `%s`", c_name(suspend_coroutine), c_name(m_sub_coroutine));

                TINYASYNC_ASSERT(!m_sub_coroutine.done());

                auto sub_coroutine = m_sub_coroutine;
                sub_coroutine.promise().m_continuum = suspend_coroutine;
                sub_coroutine.promise().m_resume_result = suspend_coroutine.promise().m_resume_result;
                return sub_coroutine;
            }

            Result await_resume();

        };

        struct TINYASYNC_NODISCARD JoinAwaiter
        {
            std::coroutine_handle<promise_type> m_sub_coroutine;

            JoinAwaiter(std::coroutine_handle<promise_type> h) : m_sub_coroutine(h)
            {
            }

            bool await_ready() noexcept
            {
                return m_sub_coroutine.done();
            }

            template<class Promise>
            void await_suspend(std::coroutine_handle<Promise> suspend_coroutine) {
                std::coroutine_handle<TaskPromiseBase> h = suspend_coroutine.promise().coroutine_handle_base();
                await_suspend(h);
            }

            void await_suspend(std::coroutine_handle<TaskPromiseBase> suspend_coroutine)
            {
                TINYASYNC_GUARD("Task(`%s`).Awaiter.await_suspend(): ", c_name(m_sub_coroutine));

                TINYASYNC_LOG("set continuum of `%s` to `%s`", c_name(m_sub_coroutine), c_name(suspend_coroutine));
                TINYASYNC_LOG("`%s` suspended, resume `%s`", c_name(suspend_coroutine), c_name(m_sub_coroutine));

                TINYASYNC_ASSERT(!m_sub_coroutine.done());

                auto sub_coroutine = m_sub_coroutine;
                sub_coroutine.promise().m_continuum = suspend_coroutine;
                sub_coroutine.promise().m_resume_result = suspend_coroutine.promise().m_resume_result;
            }

            Result await_resume();

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

        JoinAwaiter join()
        {
            return { m_h };
        }


        Task() : m_h(nullptr)
        {
        }

        Task(coroutine_handle_type h) : m_h(h)
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
            r.m_h = nullptr;
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
        // mark the coroutine it is detached.
        std::coroutine_handle<promise_type> detach()
        {
            promise().m_dangling = true;
            return release();
        }


        Task(Task const& r) = delete;
        Task& operator=(Task const& r) = delete;
    };


    inline void destroy_and_throw_if_necessary_impl(std::coroutine_handle<TaskPromiseBase> coroutine, char const* func)
    {

        TaskPromiseBase& promise = coroutine.promise();

        TINYASYNC_GUARD("`destroy_and_throw_if_necessary(): ");
        TINYASYNC_LOG("`%s` done", c_name(coroutine));

        if (coroutine.promise().m_unhandled_exception) TINYASYNC_UNLIKELY {
                auto name_ = c_name(coroutine);
            // exception is reference counted
            auto unhandled_exception = promise.m_unhandled_exception;

            if (promise.is_dangling()) TINYASYNC_UNLIKELY {
                coroutine.destroy();
            }

            try {
                std::rethrow_exception(unhandled_exception);
            } catch (...) {
            std::throw_with_nested(std::runtime_error(format("in function <%s>: `%s` thrown, rethrow", func, name_)));
            }

        } else {
            if (promise.is_dangling()) TINYASYNC_UNLIKELY {
                coroutine.destroy();
            }
        }
    }


    // if coroutine is done
    //     if coroutine is dangling coroutine, coroutine is detroyed
    //     and if coroutine has unhandled_exception, rethrow
    //     return true
    // else
    //     return false
    inline bool destroy_and_throw_if_necessary(std::coroutine_handle<TaskPromiseBase> coroutine, char const* func)
    {
        if (coroutine.done()) TINYASYNC_UNLIKELY {
            destroy_and_throw_if_necessary_impl(coroutine, func);
            return true;
        }
        return false;
    }

    inline void throw_impl(std::coroutine_handle<TaskPromiseBase> coroutine, char const* func)
    {

        TaskPromiseBase& promise = coroutine.promise();
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

    inline bool throw_if_necessary(std::coroutine_handle<TaskPromiseBase> coroutine, char const* func)
    {

        TaskPromiseBase& promise = coroutine.promise();
        if (coroutine.promise().m_unhandled_exception) TINYASYNC_UNLIKELY {
            throw_impl(coroutine, func);
            return true;
        }
        return false;

    }

    template<class Result>
    Task<Result> TaskPromise<Result>::get_return_object()
    {
        auto h = this->coroutine_handle();
        TINYASYNC_GUARD("Task(`%s`).Promise.get_return_object(): ", c_name(h));
        TINYASYNC_LOG("");
        return { h };
    }

    inline bool resume_coroutine(std::coroutine_handle<TaskPromiseBase> coroutine, char const* func = "")
    {
        TINYASYNC_GUARD("resume_coroutine(): ");
        TINYASYNC_LOG("resume `%s`", c_name(coroutine));
        assert(coroutine);
        ResumeResult res;

        // awiaters except Task::Awaiter don't touch `m_return_from`
        // If no coroutine switch, the m_return_from will not change,
        // initialize it with first coroutine.
        res.m_return_from = coroutine;

        coroutine.promise().m_resume_result = &res;
        coroutine.resume();

        TINYASYNC_LOG("resumed from `%s`", c_name(res.m_return_from));
        return !destroy_and_throw_if_necessary(res.m_return_from, func);
    }

#define TINYASYNC_RESUME(coroutine)  resume_coroutine(coroutine, TINYASYNC_FUNCNAME)


    // you can't get result of task here
    // so use Task<void>
    inline void co_spawn(Task<void> task)
    {
        TINYASYNC_GUARD("co_spawn(): ");
        auto coroutine = task.coroutine_handle_base();
        task.detach();
        TINYASYNC_RESUME(coroutine);
    }


    // return true is coroutine is not done
    template<class Result>
    inline bool Task<Result>::resume()
    {
        return resume_coroutine(this->coroutine_handle_base());
    }


    template<class Result>
    Result Task<Result>::Awaiter::await_resume()
    {
        auto sub_coroutine = m_sub_coroutine;
        TINYASYNC_GUARD("Task(`%s`).Awaiter.await_resume(): ", c_name(sub_coroutine));
        TINYASYNC_ASSERT(sub_coroutine.done());
        auto base_coro = sub_coroutine.promise().coroutine_handle_base();
        throw_if_necessary(base_coro, TINYASYNC_FUNCNAME);        
        if constexpr (!std::is_same_v<void, Result>) {
            return std::move(sub_coroutine.promise().m_result);
        }
    }

    template<class Result>
    Result Task<Result>::JoinAwaiter::await_resume()
    {
        auto sub_coroutine = m_sub_coroutine;
        TINYASYNC_GUARD("Task(`%s`).JoinAwaiter.await_resume(): ", c_name(sub_coroutine));
        TINYASYNC_ASSERT(sub_coroutine.done());
        auto base_coro = sub_coroutine.promise().coroutine_handle_base();
        throw_if_necessary(base_coro, TINYASYNC_FUNCNAME);        
        if constexpr (!std::is_same_v<void, Result>) {
            return std::move(sub_coroutine.promise().m_result);
        }
    }

    class TINYASYNC_NODISCARD YieldAwaiter {
    public:
        bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<>) noexcept { }
        void await_resume() noexcept { }
    };

    YieldAwaiter yield() {
        return { };
    }

    class TINYASYNC_NODISCARD YieldAwaiterC {
        std::coroutine_handle<> m_coroutine;
    public:
        YieldAwaiterC(std::coroutine_handle<> h) {
            m_coroutine = h;
        }
        bool await_ready() noexcept { return false; }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept  {
            return m_coroutine;
        }
        void await_resume() noexcept { }
    };

    YieldAwaiterC yield(std::coroutine_handle<> h) {
        return { h };
    }

} // tinyasync

#endif
