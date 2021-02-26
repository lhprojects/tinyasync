//#define TINYASYNC_TRACE
#ifndef TINYASYNC_BASICS_H
#include <tinyasync/tinyasync.h>
#endif

#include <random>

using namespace tinyasync;

Task<> dns1(IoContext &ctx, char const *name)
{
    DsnResult res = co_await dns_resolve(ctx, name);
    //DsnResult res;
    
    if(res.native_errc()) {
         printf("%30s errc:%d\n", name, res.native_errc());
    } else {
        printf("%30s %s\n", name, res.address().to_string().c_str());
    }
    co_return;
}

Task<> dns(IoContext &ctx)
{
    std::vector<std::string> names = {
        "www.baidu.com",
        "www.google.com",
        "-wrong host name format-",
    };

    auto now = std::chrono::high_resolution_clock::now();
    uint32_t seed = now.time_since_epoch().count();
    std::default_random_engine eng(seed);
    std::uniform_int_distribution<int> dist;
    
    
    for(int i = 0; i < 20; ++i) {
        uint32_t rnd = dist(eng);
        std::string name = "www." + std::to_string(rnd) + ".com";
        names.push_back(name);
    }

    std::vector<Task<> > tasks;
    for(auto &name : names) {
        tasks.push_back(dns1(ctx, name.c_str()));
    }
    int i = 0;
    for(auto &task : tasks) {
        co_await task;
    }
    
    ctx.request_abort();
}

int main()
{

    DnsResolverFactory::instance().add_dsn_resolvers(10);

    TINYASYNC_GUARD("[1] ");
    
    IoContext ctx;
        
    auto t0 = std::chrono::high_resolution_clock::now();
    
    co_spawn(dns(ctx));
    ctx.run();

    auto t1 = std::chrono::high_resolution_clock::now();
    auto d = std::chrono::duration_cast<std::chrono::seconds>(t1 - t0);

    printf("%.2f s\n", (double)d.count());
    printf("all done\n");
}
