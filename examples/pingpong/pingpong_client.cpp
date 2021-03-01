//#define TINYASYNC_TRACE

#include "echo_common.h"
using namespace tinyasync;

Pool pool;
int nc = 0;
std::chrono::seconds timeout { 0 };
size_t nsess;
bool tcp_no_delay = false;

Task<> start(IoContext &ctx, Session &s)
{
    ++nc;
	printf("%d conn\n", nc);
    
    auto lb = allocate(&pool);
    ConstBuffer buffer = lb->buffer;
    co_await s.conn.async_send(buffer);

	co_spawn(s.read(ctx));
	co_await s.send(ctx);
    
    deallocate(&pool, lb);
}

Task<> connect_(IoContext &ctx)
{

    Endpoint endpoint(Address::Any(), 8899);
    Protocol protocol;
    std::vector<Session> sesses;
	for (size_t i = 0; i <  nsess; ++i) {
		Connection conn = co_await async_connect(ctx, protocol, endpoint);
        if(tcp_no_delay)
            conn.set_tcp_no_delay();
        sesses.push_back(Session(ctx, std::move(conn), &pool));
	}

	for (size_t i = 0; i <  nsess; ++i) {
        co_spawn(start(ctx, sesses[i]));
	}
    
    co_await async_sleep(ctx, timeout);

	for (size_t i = 0; i <  nsess; ++i) {
        // send FIN
        sesses[i].conn.safe_shutdown_send();
    }

	for (size_t i = 0; i <  nsess; ++i) {
        // until recv FIN
        for(;!sesses[i].read_finish;) {
            co_await sesses[i].read_finish_event;
        }
        sesses[i].conn.safe_close();
        --nc;
        printf("%d conn\n", nc);

    }

    printf("%d connection\n", (int)nsess);
    printf("%d block size\n", (int)block_size);
    printf("%.2f M/s bytes read\n", (long long)nread_total/timeout.count()/1E6);
    printf("%.2f M/s bytes write\n", (long long)nwrite_total/timeout.count()/1E6);

    co_await async_sleep(ctx, std::chrono::seconds(1));
    ctx.request_abort();
}


void client() {	

	IoContext ctx;
	co_spawn(connect_(ctx));
	ctx.run();
}

int main()
{
    nsess = 10;
    timeout = std::chrono::seconds(10);
    block_size = 1024;
    initialize_pool(pool);
    tcp_no_delay = true;

	client();
	return 0;
}


