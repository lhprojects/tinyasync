#ifndef TINYASYNC_DNS_RESOLVER_H
#define TINYASYNC_DNS_RESOLVER_H

#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>

namespace tinyasync
{
    class DnsResolver;
    class DnsResolverFactory;

    struct DsnResult
    {
        int native_errc() const
        {
            return m_errc;
        }
        Address address() const
        {
            return m_address;
        }
    private:
        friend class DnsResolver;
        int m_errc = 0;
        Address m_address;
    };


    struct [[nodiscard]] DnsResolverAwaiter : ListNode
    {
        DnsResolver *m_dns_resolver;
        IoCtxBase *m_ctx;
        DsnResult m_result;
        std::coroutine_handle<TaskPromiseBase> m_suspend_coroutine;
        char const *m_name;
        PostTask m_local_task;

        DnsResolverAwaiter(DnsResolver &resolver, IoCtxBase &ctx, char const *name) {
            m_ctx = &ctx;
            m_dns_resolver = &resolver;
            m_local_task.set_callback(do_local_task);         
            m_name = name;   
        }

        // resolve done
        // resume our coroutine
        static void do_local_task(PostTask *posttask)
        {
            TINYASYNC_POINT_FROM_MEMBER(awaiter, posttask, DnsResolverAwaiter, m_local_task);
            TINYASYNC_RESUME(awaiter->m_suspend_coroutine);            
        }

        bool await_ready()
        {
            return false;
        }

        template<class P>
        void await_suspend(std::coroutine_handle<P> h) {
            auto suspend_coroutine = h.promise().coroutine_handle_base();
            await_suspend(suspend_coroutine);
        }

        void await_suspend(std::coroutine_handle<TaskPromiseBase> h);

        DsnResult await_resume();

        

    };

    class DnsResolver
    {

        friend class DnsResolverAwaiter;

        // public for dns thread
        std::condition_variable m_condv;
        std::mutex m_mutex;
        Queue m_requests;

        DefaultSpinLock m_spinlock;
        std::vector<std::thread> m_threads;
        bool m_abort = false;

    public:
        DnsResolver() = default;
        DnsResolver(DnsResolver &&) = delete;

        void add_workers(size_t n) {

            for(size_t i = 0; i < n; ++i) {

                std::unique_lock<std::mutex> guard(m_mutex);
                m_threads.emplace_back([this] () {

                    int id = this->m_threads.size();
                    TINYASYNC_GUARD("[dns thread %d]", id);                    

                    this->work(id);
                });
                guard.unlock();
            }
        }


        ~DnsResolver()
        {
            std::unique_lock<std::mutex> guard(m_mutex);
            m_abort = true;
            guard.unlock();
            
            m_condv.notify_all();

            for(auto &thread : m_threads) {
                thread.join();
            }


        }

        void gethostaddr(DnsResolverAwaiter &req)
        {
            addrinfo *result;

            addrinfo hints;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = PF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags |= AI_CANONNAME;

            int errc = getaddrinfo(req.m_name, NULL, &hints, &result);
            if (errc)
            {
                req.m_result.m_errc = errc;
                return;
            }

            for (auto res = result; res;)
            {

                if (res->ai_family == AF_INET)
                {
                    auto addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
                    req.m_result.m_address.m_addr4 = addr;
                    break;
                }
                else if (res->ai_family == AF_INET6)
                {
                    auto addr = ((struct sockaddr_in6 *)res->ai_addr)->sin6_addr;
                    req.m_result.m_address.m_addr6 = addr;
                    break;
                }
                res = res->ai_next;
            }

            freeaddrinfo(result);
        }

        void work(int id)
        {
            for (;!m_abort;)
            {
                DnsResolverAwaiter *awaiter = nullptr;   
                std::unique_lock<std::mutex> guard(m_mutex);
                for(;!m_abort;) {                           
                    m_spinlock.lock();
                    auto node = m_requests.pop();                    
                    m_spinlock.unlock();
                    if(node) {
                        awaiter = static_cast<DnsResolverAwaiter *>(node);
                        break;
                    }
                    m_condv.wait(guard);
                }
                guard.unlock();
                if(m_abort) {
                    break;
                }
                
                gethostaddr(*awaiter);


                TINYASYNC_ASSERT(awaiter->m_ctx);
                TINYASYNC_LOG("post response to %p", awaiter->m_ctx);
                // should thread safe
                awaiter->m_ctx->post_task(&awaiter->m_local_task);
            }

            TINYASYNC_LOG("worker %d abort", id);
        }




        static DnsResolver dns_resolver;

        static DnsResolver &instance() {
            return dns_resolver;
        }

        // ctx must be thread safe for posting task
        DnsResolverAwaiter resolve(IoContext &ctx, char const *name)
        {
            return {*this, *ctx.get_io_ctx_base(), name };
        }
    };

    inline DnsResolver DnsResolver::dns_resolver;

    DnsResolverAwaiter async_dns_resolve(IoContext &ctx, char const *name)
    {
        auto inst = &DnsResolver::instance();   
        return inst->resolve(ctx, name);
    }

    inline void DnsResolverAwaiter::await_suspend(std::coroutine_handle<TaskPromiseBase> h)
    {
        TINYASYNC_GUARD("DnsResolverAwaiter::await_suspend(): ");
        TINYASYNC_LOG("enqueue");

        m_suspend_coroutine = h;        

        auto resolver = m_dns_resolver;

        resolver->m_spinlock.lock();
        std::size_t n  = resolver->m_threads.size();
        
        if(n == 0) TINYASYNC_UNLIKELY {
            resolver->m_spinlock.unlock(); 
            resolver->add_workers(1);
            resolver->m_spinlock.lock();
        }

        resolver->m_requests.push(this);
        resolver->m_spinlock.unlock(); 

        resolver->m_condv.notify_one();      
    }

    DsnResult DnsResolverAwaiter::await_resume()
    {
        TINYASYNC_GUARD("DnsResolverAwaiter::await_suspend(): ");
        TINYASYNC_LOG("resolved");
        return this->m_result;
    }

} // namespace tinyasync

#endif
