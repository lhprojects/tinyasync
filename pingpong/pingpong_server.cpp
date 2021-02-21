//#define TINYASYNC_TRACE
#include "echo_common.h"

int nc = 0;

Task<> start(IoContext &ctx, Session s)
{
	co_spawn(s.read(ctx));
	co_await s.send(ctx);
	for(;s.m_run;) {
		co_await s.all_done;
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

void server() {	
	TINYASYNC_GUARD("server():");

	IoContext ctx;

	co_spawn(listen(ctx));

	TINYASYNC_LOG("run");
	ctx.run();
}

int main()
{
    block_size = 1024;
    initialize_pool();
	server();
	return 0;
}
