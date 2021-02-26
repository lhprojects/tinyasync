#ifndef TINYASYNC_DNS_RESOLVER_H
#define TINYASYNC_DNS_RESOLVER_H

#include <thread>
#include <set>

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


    struct DnsResolverAwaiter : ListNode
    {
        DnsResolverFactory *m_dns_resolver_factory;
        DnsResolver *m_dns_resolver;
        IoCtxBase *m_ctx;
        DsnResult m_result;
        std::coroutine_handle<TaskPromiseBase> m_suspend_coroutine;
        char const *m_name;
        PostTask m_remote_task;     
        PostTask m_local_task;

        DnsResolverAwaiter(DnsResolverFactory &resolver_factory, IoCtxBase &ctx, char const *name) {
            m_ctx = &ctx;
            m_dns_resolver_factory = &resolver_factory;
            m_remote_task.set_callback(do_remote_task);
            m_local_task.set_callback(do_local_task);         
            m_name = name;   
        }


        static void do_remote_task(PostTask *posttask);

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

        friend class DnsResolverFactory;
        friend class DnsResolverAwaiter;

        // protected by DnsResolverFactory::m_mutex
        std::size_t m_acc_size = 0;
        typename std::multiset<DnsResolver>::iterator m_map_iter;


        // public for dns thread
        IoContext m_ctx;
        Queue m_requests;
        std::thread m_thread;
        std::coroutine_handle<TaskPromiseBase> m_main_coroutine;

    public:
        DnsResolver(DnsResolver &&) = delete;

        TINYASYNC_NOINL DnsResolver()
        {
            m_thread = std::thread([this]() {
                TINYASYNC_GUARD("[dns thread] ");

                co_spawn(this->work(m_ctx));
                this->m_ctx.run();
            });
        }


        ~DnsResolver()
        {
            m_ctx.request_abort();
            m_thread.join();
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

        Task<> work(IoContext &)
        {
            m_main_coroutine = co_await this_coroutine<TaskPromiseBase>();
            for (;;)
            {
                DnsResolverAwaiter *awaiter;   
                for(;;) {       
                    auto node = m_requests.pop();                    
                    if(node) {
                        awaiter = static_cast<DnsResolverAwaiter *>(node);
                        break;
                    }
                    co_await std::suspend_always{ };
                }
                gethostaddr(*awaiter);

                // should thread safe
                TINYASYNC_ASSERT(awaiter->m_ctx);
                TINYASYNC_LOG("post response to %p", awaiter->m_ctx);
                awaiter->m_ctx->post_task(&awaiter->m_local_task);
            }
        }


        bool operator<(DnsResolver const &r) const {
            return this->m_acc_size < this->m_acc_size;
        }
        bool operator==(DnsResolver const &r) const {
            return this == &r;
        }


    };

    class DnsResolverFactory
    {

        SysSpinLock m_mutex;
        std::multiset<DnsResolver> m_dns_resolvers;

        void add_dsn_resolvers_1_nolock()
        {
            m_dns_resolvers.emplace();
        }


    public:
        void add_dsn_resolvers(size_t n)
        {
            for(int i = 0; i < n; ++i) {                
                m_mutex.lock();
                add_dsn_resolvers_1_nolock();
                m_mutex.unlock();
            }
        }

        void release_resolver(DnsResolver *resolver)
        {
            m_mutex.lock();
            auto iter = resolver->m_map_iter;
            auto node = m_dns_resolvers.extract(iter);
            resolver->m_acc_size -= 1;
            m_dns_resolvers.insert(std::move(node));
            m_mutex.unlock();
        }

        DnsResolver *accquire_resolver()
        {
            DnsResolver * resolver;
            m_mutex.lock();

            if(m_dns_resolvers.empty()) TINYASYNC_UNLIKELY
            {
                add_dsn_resolvers_1_nolock();
            }

            auto begin = m_dns_resolvers.begin();
            auto begin_node = m_dns_resolvers.extract(begin);
            resolver = &begin_node.value();
            resolver->m_acc_size += 1;
            auto new_iter = m_dns_resolvers.insert(std::move(begin_node));
            resolver->m_map_iter = new_iter;
            m_mutex.unlock();

            return resolver;
        }

        // ctx must be thread safe for posting task
        DnsResolverAwaiter resolve(IoContext &ctx, char const *name)
        {
            return {*this, *ctx.get_io_ctx_base(), name };
        }

        static DnsResolverFactory dns_factory;

        static DnsResolverFactory &instance() {
            return dns_factory;
        }
    };

    inline DnsResolverFactory DnsResolverFactory::dns_factory;

    DnsResolverAwaiter dns_resolve(IoContext &ctx, char const *name)
    {
        auto inst = &DnsResolverFactory::instance();   
        return inst->resolve(ctx, name);
    }

    inline void DnsResolverAwaiter::await_suspend(std::coroutine_handle<TaskPromiseBase> h)
    {
        TINYASYNC_GUARD("DnsResolverAwaiter::await_suspend(): ");
        m_suspend_coroutine = h;        

        auto resolver = m_dns_resolver_factory->accquire_resolver();
        m_dns_resolver = resolver;

        TINYASYNC_LOG("post to resolver %p", m_dns_resolver);
        // should thread safe
        resolver->m_ctx.post_task(&this->m_remote_task);
        
    }

    DsnResult DnsResolverAwaiter::await_resume()
    {
        TINYASYNC_GUARD("DnsResolverAwaiter::await_suspend(): ");
        TINYASYNC_LOG("release resolver %p", m_dns_resolver);
        m_dns_resolver_factory->release_resolver(m_dns_resolver);
        return this->m_result;
    }


    inline void DnsResolverAwaiter::do_remote_task(PostTask *posttask)
    {


        TINYASYNC_GUARD("DnsResolverAwaiter::do_remote_task(): ");

        TINYASYNC_POINT_FROM_MEMBER(awaiter, posttask,  DnsResolverAwaiter, m_remote_task);

        TINYASYNC_LOG("awaiter %p", awaiter);

        auto resolver = awaiter->m_dns_resolver;
        resolver->m_requests.push(awaiter);    

        if(resolver->m_main_coroutine) {
            TINYASYNC_LOG("resume main coroutine");
            TINYASYNC_RESUME(resolver->m_main_coroutine);
        }

    }

} // namespace tinyasync

#endif
