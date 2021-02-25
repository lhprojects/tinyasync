#ifndef TINYASYNC_DNS_RESOLVER_H
#define TINYASYNC_DNS_RESOLVER_H

#include <thread>

namespace tinyasync
{
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

    struct DnsRequest : ListNode
    {
        DnsRequest(IoContext &ctx, char const *name) : m_condv(ctx)
        {
            m_name = name;
        }

        ConditionVariable m_condv;
        DsnResult m_result;
        bool m_done = false;
        char const *m_name;
    };

    class DnsResolver
    {
        friend class DnsResolverFactory;

        IoContext m_ctx;
        Mutex m_mtx;

        Queue m_requests;
        ConditionVariable m_request_evt{m_ctx};
        std::thread m_thread;
        PostTask m_posttask;

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
            m_posttask.set_callback([](PostTask*) { });
            m_ctx.post_task(&m_posttask);
            m_thread.join();
        }

        void gethostaddr(DnsRequest &req)
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
            for (;;)
            {
                DnsRequest *preq;
                co_await m_mtx.lock(m_ctx);
                for (;;)
                {
                    auto node =m_requests.pop();
                    if(node) {
                        preq = static_cast<DnsRequest*>(node);
                        break;
                    }
                    co_await m_request_evt.wait(m_mtx);
                }
                m_mtx.unlock();

                auto &req = *preq;
                gethostaddr(req);
                req.m_done = true;
                req.m_condv.notify_one();
            }
        }

    public:
        // ctx must be thread safe for posting task
        Task<DsnResult> resolve(IoContext &ctx, char const *name)
        {
            DnsRequest req{ctx, name};

            co_await m_mtx.lock(ctx);

            auto mtx_ = auto_unlock(m_mtx);
            m_requests.push(&req);
            m_request_evt.notify_one();

            for (; !req.m_done;)
            {
                // remote dns thread may post task to ctx
                co_await req.m_condv.wait(m_mtx);
            }
            // any thread may post task to ctx
            mtx_.unlock();

            co_return req.m_result;
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

} // namespace tinyasync

#endif
