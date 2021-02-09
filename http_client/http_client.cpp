#define TINYASYNC_TRACE
#include <tinyasync/tinyasync.h>

using namespace tinyasync;

Task do_download(IoContext &ctx, Name="download") {


    hostent * record = gethostbyname("www.baidu.com");
	in_addr * address = (in_addr * )record->h_addr;
    uint32_t ip = ntohl(address->s_addr);
    Connection conn = co_await async_connect(ctx, Protocol::ip_v4(), Endpoint(Address(ip), 80));

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


