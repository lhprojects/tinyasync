//#define TINYASYNC_TRACE
#ifndef TINYASYNC_BASICS_H
#include <tinyasync/tinyasync.h>
#endif

using namespace tinyasync;

Task<> dns1(IoContext &ctx, char const *name)
{
    DsnResult res = co_await dns_resolve(ctx, name);
    
    if(res.native_errc()) {
        printf("%30s errc:%d\n", name, res.native_errc());
    } else {
        printf("%30s %s\n", name, res.address().to_string().c_str());
    }
}

Task<> dns(IoContext &ctx)
{
    std::vector<std::string> names = {
        "www.baidu.com",
        "www.google.com",
        "-wrong host name format-",
        "www.baidu.omc",
    };

    std::vector<Task<> > tasks;
    for(auto &name : names) {
        tasks.push_back(dns1(ctx, name.c_str()));
    }
    for(auto &task : tasks) {
        co_await task;
    }
    
    ctx.request_abort();
}

int main()
{

    TINYASYNC_GUARD("[1] ");
    IoContext ctx;
        
    co_spawn(dns(ctx));

    ctx.run();

    printf("all done\n");
}
