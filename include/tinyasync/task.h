#ifndef TINYASYNC_TASK_H
#define TINYASYNC_TASK_H

#include <exception>

namespace tinyasync {

    struct ResumeResult;


    template<class Result>
    class Generator {

        struct Promise
        {

            inline static void * do_alloc(std::size_t size, std::pmr::memory_resource *memory_resource)
            {
                // put allocator at the end of the frame
                auto constexpr memory_resource_size =  sizeof(std::pmr::memory_resource*);
                auto constexpr memory_resource_align =  alignof(std::pmr::memory_resource*);
                auto memory_resource_offset = (size  + memory_resource_align - 1u) & ~(memory_resource_align - 1u);

                auto ptr = memory_resource->allocate(memory_resource_offset + memory_resource_size);
                new((char*)ptr + memory_resource_offset) decltype(memory_resource)(memory_resource);
                return ptr;
            }

            static void* operator new(std::size_t size)
            {
                auto ptr = do_alloc(size, get_default_resource());
                return ptr;
            }

            static void operator delete(void* ptr, std::size_t size)
            {
                auto constexpr memory_resource_size =  sizeof(std::pmr::memory_resource*);
                auto constexpr memory_resource_align =  alignof(std::pmr::memory_resource*);
                auto memory_resource_offset = (size  + memory_resource_align - 1u) & ~(memory_resource_align - 1u);

                auto memory_resource = *(std::pmr::memory_resource**)((char*)ptr + memory_resource_offset);
                memory_resource->deallocate(ptr, size);
            }

            std::suspend_always initial_suspend() { return{}; }
            std::suspend_always final_suspend() noexcept { return{}; }
            void unhandled_exception() { throw; }

            Generator get_return_object() {
                auto coro = std::coroutine_handle<Promise>::from_promise(*this);
                return {coro};
            }

            template<class V>
            std::suspend_always yield_value(V &&v) {
                m_result = std::forward<V>(v);
                return {};
            }

            void return_void() {                
            }

            Result m_result;
        };
    public:        
        using promise_type = Promise;

        Generator(std::coroutine_handle<Promise> coro) : m_coro(coro) {
        }
        Generator(Generator &&r) : m_coro(r.m_coro){
            r.m_coro = nullptr;
        }
        Generator(Generator const &) = delete;        
        Generator &operator=(Generator &&r) = delete;
        Generator operator=(Generator const &) = delete;
        ~Generator() {
            if(m_coro)
                m_coro.destroy();            
        }


        bool next() {
            m_coro.resume();
            return !m_coro.done();
        }

        Result &get() {
            auto &promise = m_coro.promise();
            return promise.m_result;
        }

        struct IteratorEnd {
        };

        struct Iterator {
            std::coroutine_handle<Promise> m_coro;
            Iterator(std::coroutine_handle<Promise> coro) : m_coro(coro) { }

            Result &operator*() {
                auto &promise = m_coro.promise();
                return promise.m_result;
            }

            Iterator &operator++() {
                m_coro.resume();
                return *this;
            }

            bool operator==(IteratorEnd) const {
                return m_coro.done();
            }

        };

        Iterator begin() {
            m_coro.resume();
            return m_coro;
        }

        IteratorEnd end() {
            return {};
        }

    private:
        std::coroutine_handle<Promise> m_coro;

    };

    template<class Result>
    class Task;


    struct ExceptionPtrWrapper
    {

        std::exception_ptr &exception() {
            return (std::exception_ptr &)m_exception;
        }

        ExceptionPtrWrapper() {
            new(m_exception) std::exception_ptr();
        }        

        ~ExceptionPtrWrapper()
        {
        }
    private:
        alignas(std::exception_ptr) char m_exception[sizeof(std::exception_ptr)];
    };

    class TaskPromiseBase {
    public:
        // resumer to destruct exception
        ExceptionPtrWrapper m_unhandled_exception;
        std::coroutine_handle<void> m_continuation;

        inline static void * do_alloc(std::size_t size, std::pmr::memory_resource *memory_resource)
        {
            // put allocator at the end of the frame
            auto constexpr memory_resource_size =  sizeof(std::pmr::memory_resource*);
            auto constexpr memory_resource_align =  alignof(std::pmr::memory_resource*);
            auto memory_resource_offset = (size  + memory_resource_align - 1u) & ~(memory_resource_align - 1u);

            auto ptr = memory_resource->allocate(memory_resource_offset + memory_resource_size);
            new((char*)ptr + memory_resource_offset) decltype(memory_resource)(memory_resource);
            return ptr;
        }


        template<class T>
        static void *alloc_1(std::size_t size, T &&) {
            auto memory_resource = get_default_resource();
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

            auto constexpr memory_resource_size =  sizeof(std::pmr::memory_resource*);
            auto constexpr memory_resource_align =  alignof(std::pmr::memory_resource*);
            auto memory_resource_offset = (size  + memory_resource_align - 1u) & ~(memory_resource_align - 1u);

            auto memory_resource = *(std::pmr::memory_resource**)((char*)ptr + memory_resource_offset);
            memory_resource->deallocate(ptr, size);
        }

        std::coroutine_handle<TaskPromiseBase> coroutine_handle_base() noexcept
        {
            return std::coroutine_handle<TaskPromiseBase>::from_promise(*this);
        }


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

        Result &result() {
            return m_result;
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

        struct YieldValueAwaiter {

            YieldValueAwaiter(promise_type &p) {

            }
            void await_suspend(std::coroutine_handle<promise_type>) const noexcept
            {
            }

            void await_resume() const noexcept
            {
            }
        };

        template<class T>
        typename std::enable_if<!std::is_same_v<Result, void>||(false&&std::is_same_v<T,void>), std::suspend_always>::type
        yield_value(T &&v) {
            this->return_value(std::forward<T>(v));
            return {};
        }


        std::suspend_always initial_suspend()
        {
            return { };
        }

        struct FinalAwaiter : std::suspend_always
        {
            bool await_ready() noexcept { return false; }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) const noexcept
            {
                auto &promise = h.promise();
                auto continuum = promise.m_continuation;
                return continuum;

            }

            void await_resume() const noexcept
            {
                assert(false);
            }
        };

        FinalAwaiter final_suspend() noexcept
        {
            return {};
        }

        void unhandled_exception()
        {
            m_unhandled_exception.exception() = std::current_exception();
        }

    };

    template<class T>
    struct AddRef {
        using type = T &;
    };

    template<>
    struct AddRef<void> {
        using type = void;
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
    private:
        coroutine_handle_type m_h;
    public:


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

        template<class R = Result>
        std::enable_if_t<!std::is_same_v<R,void>, typename AddRef<R>::type>
        result() {
            auto &promise = m_h.promise();
            return promise.m_result;
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
            std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> awaiting_coro)
            {
                auto sub_coroutine = m_sub_coroutine;
                sub_coroutine.promise().m_continuation = awaiting_coro;
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
            void await_suspend(std::coroutine_handle<Promise> awaiting_coro)
            {
                auto sub_coroutine = m_sub_coroutine;
                sub_coroutine.promise().m_continuation = awaiting_coro;
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

    template<class Result>
    Task<Result> TaskPromise<Result>::get_return_object()
    {
        auto h = this->coroutine_handle();
        TINYASYNC_GUARD("Task(`%s`).Promise.get_return_object(): ", c_name(h));
        TINYASYNC_LOG("");
        return { h };
    }

    struct DscExpPtr {

        std::exception_ptr *m_e;
        ~DscExpPtr() {
            *m_e = nullptr;
        }
    };

    [[noreturn]]
    inline void reset_and_throw_exception(std::exception_ptr &e) {
        DscExpPtr dep;
        dep.m_e = &e;

        std::rethrow_exception(e);
    }

    inline void resume_coroutine_task(std::coroutine_handle<TaskPromiseBase> coroutine)
    {
        coroutine.resume();
        if(coroutine.promise().m_unhandled_exception.exception()) TINYASYNC_UNLIKELY {
            reset_and_throw_exception(coroutine.promise().m_unhandled_exception.exception());
        }
    }

    void resume_coroutine_callback(std::coroutine_handle<TaskPromiseBase> coroutine)
    {
        coroutine.resume();
        // the last coroutine is not always the same as the resume coroutine
    }

    // resume a non dangling coroutine
#define TINYASYNC_RESUME(coroutine)  resume_coroutine_callback(coroutine)


    struct SpawnTask {

        struct promise_type : TaskPromiseBase {
            std::suspend_always initial_suspend() { return {}; }
            std::suspend_never  final_suspend() noexcept { return {}; }

            void unhandled_exception() {
                TINYASYNC_RETHROW();
            }
            SpawnTask get_return_object() {
                return {std::coroutine_handle<promise_type>::from_promise(*this)};
            }
            void return_void() { }
        };

        SpawnTask(std::coroutine_handle<promise_type> h) : m_handle(h) {   
        }
        std::coroutine_handle<promise_type> m_handle;
    };

    [[noreturn]]
    void destroy_and_throw(std::coroutine_handle<TaskPromiseBase> task) {
        auto &exception = task.promise().m_unhandled_exception.exception();
        auto e = exception;
        exception = nullptr;
        task.destroy();
        std::rethrow_exception(e);
    }

    // body of spawn_task will not suspend
    SpawnTask spawn_task(std::coroutine_handle<TaskPromiseBase> task)
    {
        auto &exception = task.promise().m_unhandled_exception.exception();
        if(!exception) TINYASYNC_LIKELY {
            task.destroy();
        } else {
            destroy_and_throw(task);
        }
        co_return;
    }

    // you can't get result of task here
    // so use Task<void>
    TINYASYNC_NOINL inline void co_spawn(Task<void> task)
    {
        TINYASYNC_GUARD("co_spawn(): ");

        auto coroutine = task.coroutine_handle_base();
        task.release();

        auto spawn_task_ = spawn_task(coroutine);
        auto spawn_coro = spawn_task_.m_handle;

        coroutine.promise().m_continuation = std::coroutine_handle<TaskPromiseBase>::from_promise(spawn_coro.promise());
        resume_coroutine_task(coroutine);
    }


    // return true is coroutine is not done, otherwise return false
    // if the the coroutine has unhandled exception, destroy the couroutine and rethrow the exception
    // pre-condition: the task is not detached and not done!
    // Note: the coroutine is destroied automatically if the coroutine is done
    template<class Result>
    inline bool Task<Result>::resume()
    {
        resume_coroutine_task(this->coroutine_handle_base());
        return !coroutine_handle().done();
    }

    template<class Result>
    Result Task<Result>::Awaiter::await_resume()
    {
        auto sub_coroutine = m_sub_coroutine;
        TINYASYNC_GUARD("Task(`%s`).Awaiter.await_resume(): ", c_name(sub_coroutine));
        TINYASYNC_ASSERT(sub_coroutine.done());
        auto &promise = sub_coroutine.promise();
        
        if(promise.m_unhandled_exception.exception()) {
            reset_and_throw_exception(promise.m_unhandled_exception.exception());
        }

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
        auto &promise = sub_coroutine.promise();

        if(promise.m_unhandled_exception.exception()) {
            reset_and_throw_exception(promise.m_unhandled_exception.exception());
        }
         
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
