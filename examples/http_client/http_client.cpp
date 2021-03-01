//#define TINYASYNC_TRACE
#include <tinyasync/tinyasync.h>
#include <regex>

using namespace tinyasync;

void print(std::string const &s) {

    for(auto c : s) {
        
        if(c == '\r') {
            printf("\\r");
        } else if(c == '\n') {
            printf("\\n\n");
        } else {
            printf("%c", c);
        }
    }
}

Task<> do_download(IoContext &ctx, Name="download") {


    char const *hostname = "www.baidu.com";
    auto dns_result = co_await dns_resolve(ctx, hostname);
    if(dns_result.native_errc()) {
        printf("can't resolve hostname\n");
        co_return;
    }

    printf("ip: %s\n", dns_result.address().to_string().c_str());


    Connection conn = co_await async_connect(ctx, Protocol::ip_v4(), Endpoint(dns_result.address(), 80));

    std::string http_header =
R"(GET / HTTP/1.1
Host: HOSTNAME
User-Agent: curl/7.71.1
Connection: close
Accept: */*

)";

    http_header = std::regex_replace(http_header, std::regex("\r?\n"), "\r\n");
    http_header = std::regex_replace(http_header, std::regex("HOSTNAME"), hostname);
    
    char const *buf = http_header.data();
    std::size_t remain = http_header.size();

    print(http_header);

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
        printf("%s", b);
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


