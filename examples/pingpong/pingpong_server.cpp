//#define TINYASYNC_TRACE
#include "echo_common.h"

int nc = 0;
Pool pool;
bool tcp_no_delay = false;

Task<> start(IoContext &ctx, Session s)
{
	co_spawn(s.send(ctx));

	co_await s.read(ctx);

	for(;!s.read_finish;) {
		co_await s.read_finish_event;
	}
	
	// --- recv FIN

	// send FIN
	s.conn.safe_close();

	// await all send abort	
	for(;!s.send_finish;) {
		co_await s.send_finish_event;
	}

	--nc;
	printf("%d conn\n", nc);
}


Task<> listen(IoContext &ctx)
{
	Acceptor acceptor(ctx, Protocol::ip_v4(), Endpoint(Address::Any(), 8899));
	for (;;) {
		Connection conn = co_await acceptor.async_accept();
        if(tcp_no_delay)
            conn.set_tcp_no_delay();

		++nc;
		printf("%d conn\n", nc);
		co_spawn(start(ctx, Session(ctx, std::move(conn), &pool)));
	}

}

Task<> exit_timeout(IoContext &ctx) {
	co_await async_sleep(ctx, std::chrono::seconds(15));
	exit(0);
}

void server() {	
	TINYASYNC_GUARD("server():");

	IoContext ctx;

	co_spawn(listen(ctx));

	// enable only if profile
	//co_spawn(exit_timeout(ctx));

	TINYASYNC_LOG("run");
	ctx.run();
}

int main()
{
    block_size = 1024;
    initialize_pool(pool);
	tcp_no_delay = true;

	try {
		server();
	} catch(...) {		
	}
	return 0;
}
