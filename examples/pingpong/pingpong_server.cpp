//#define TINYASYNC_TRACE
#include "echo_common.h"

int nc = 0;
Pool pool;

Task<> start(IoContext &ctx, Session s)
{
	co_spawn(s.read(ctx));
	co_await s.send(ctx);

	// read join
	for(;!s.read_finish;) {
		co_await s.read_finish_event;
	}

	--nc;
	printf("%d conn\n", nc);
}


Task<> listen(IoContext &ctx)
{
	Acceptor acceptor(ctx, Protocol::ip_v4(), Endpoint(Address::Any(), 8899));
	for (;;) {
		Connection conn = co_await acceptor.async_accept();
		++nc;
		co_spawn(start(ctx, Session(ctx, std::move(conn), &pool)));
		printf("%d conn\n", nc);
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
	try {
		server();
	} catch(...) {		
	}
	return 0;
}
