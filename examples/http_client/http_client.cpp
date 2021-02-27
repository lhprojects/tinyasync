#define TINYASYNC_TRACE
#include <tinyasync/tinyasync.h>

using namespace tinyasync;

Task<> do_download(IoContext &ctx, Name="download") {


    auto dns_result = co_await dns_resolve(ctx, "www.baidu.com");
    if(dns_result.native_errc()) {
        printf("can't resolve hostname\n");
        co_return;
    }

    printf("ip: %s\n", dns_result.address().to_string().c_str());


    Connection conn = co_await async_connect(ctx, Protocol::ip_v4(), Endpoint(dns_result.address(), 80));

    char const * http_header_baidu =
R"(GET / HTTP/1.1
Host: www.baidu.com
User-Agent: Mozilla/5.0
Accept: text/html

)";
    auto http_header = http_header_baidu;

    char const *buf = http_header;
    std::size_t remain = strlen(http_header);

    // send request
    for(;remain;) {
        auto nbytes = co_await conn.async_send(buf, remain);
        remain -= nbytes;
        buf += nbytes;
    }

    // receive response
    for(;;) {
        char b[1000];
        auto nbytes = co_await conn.async_read(b, sizeof(b)-1);
        if(nbytes == 0) {
            printf("server shutdown\n");
            break;
        }
        b[nbytes] = '\0';
        printf("---- %s", b);
    }

    ctx.request_abort();
}

void download() {	
	TINYASYNC_GUARD("download():");

	IoContext ctx;
    co_spawn(do_download(ctx));

	TINYASYNC_LOG("run");
	ctx.run();
}


int main()
{
	download();
	return 0;
}


