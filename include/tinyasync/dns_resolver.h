#ifndef TINYASYNC_DNS_RESOLVER_H
#define TINYASYNC_DNS_RESOLVER_H

#include <thread>

namespace tinyasync
{
    class DnsResolver;

    struct DsnResult
    {
        int errc() const
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
        DnsResolver *m_dns_resolver;
        IoCtxBase *m_ctx;
        DsnResult m_result;
        std::coroutine_handle<TaskPromiseBase> m_suspend_coroutine;
        char const *m_name;
        PostTask m_remote_task;     
        PostTask m_local_task;

        DnsResolverAwaiter(DnsResolver &resolver, IoCtxBase &ctx, char const *name) {
            m_ctx = &ctx;
            m_dns_resolver = &resolver;
            m_remote_task.set_callback(do_remote_task);
            m_local_task.set_callback(do_local_task);         
            m_name = name;   
        }


        static void do_remote_task(PostTask *posttask);

        // resolve done
        // resume our coroutine
        static void do_local_task(PostTask *posttask)
        {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
            auto awaiter = (DnsResolverAwaiter*)((char*)posttask - offsetof(DnsResolverAwaiter, m_local_task));
#pragma GCC diagnostic pop

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

        DsnResult await_resume()
        {
            return this->m_result;
        }

        

    };

    class DnsResolver
    {
        friend class DnsResolverFactory;
        friend class DnsResolverAwaiter;

        IoContext m_ctx;
        Queue m_requests;
        std::thread m_thread;
        std::coroutine_handle<TaskPromiseBase> m_main_coroutine;

        TINYASYNC_NOINL DnsResolver()
        {
            m_thread = std::thread([this]() {
                TINYASYNC_GUARD("[dns thread]");

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
                awaiter->m_ctx->post_task(&awaiter->m_local_task);
            }
        }

    public:
        // ctx must be thread safe for posting task
        DnsResolverAwaiter resolve(IoContext &ctx, char const *name)
        {
            return {*this, *ctx.get_io_ctx_base(), name };
        }
    };

    class DnsResolverFactory
    {
    public:
        static DnsResolver &dns_thread()
        {
            static DnsResolver dns_thread;
            return dns_thread;
        }
    };

    inline DnsResolver &dns_resolver()
    {
        return DnsResolverFactory::dns_thread();
    }


    inline void DnsResolverAwaiter::await_suspend(std::coroutine_handle<TaskPromiseBase> h)
    {
        m_suspend_coroutine = h;        

        // should thread safe
        m_dns_resolver->m_ctx.post_task(&this->m_remote_task);
        
    }

    inline void DnsResolverAwaiter::do_remote_task(PostTask *posttask)
    {

        TINYASYNC_GUARD("DnsResolverAwaiter::do_remote_task(): ");
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
            auto awaiter = (DnsResolverAwaiter*)((char*)posttask - offsetof(DnsResolverAwaiter, m_remote_task));
#pragma GCC diagnostic pop

        TINYASYNC_LOG("run ...");
        auto resolver = awaiter->m_dns_resolver;
        resolver->m_requests.push(awaiter);        
        if(resolver->m_main_coroutine) {
            TINYASYNC_LOG("resume main coroutine");
            TINYASYNC_RESUME(resolver->m_main_coroutine);
        }

    }

} // namespace tinyasync

#endif
