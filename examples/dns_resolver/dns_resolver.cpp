
//#define TINYASYNC_TRACE
#ifndef TINYASYNC_BASICS_H
#include <tinyasync/tinyasync.h>
#endif

using namespace tinyasync;

Task<DsnResult> dns1(IoContext &ctx, char const *str)
{
    DsnResult res = co_await dns_resolver().resolve(ctx, str);
    co_return res;
}

Task<> dns(IoContext &ctx)
{
    std::vector<std::string> names = {
        "www.baidu.com",
        "www.google.com"  
    };

    for(auto &name : names) {
        DsnResult res = co_await dns1(ctx, name.c_str());
        if(res.errc()) {
            printf("%s %s\n", name.c_str(), "err");
        } else {
            printf("%s %s\n", name.c_str(), res.address().to_string().c_str());
        }
    }
    ctx.request_abort();
}

int main()
{

    TINYASYNC_GUARD("[1]");
    {
        IoContext ctx;
        
        co_spawn(dns(ctx));

        ctx.run();

    }

    printf("done\n");
}
