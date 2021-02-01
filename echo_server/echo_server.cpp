#define TINYASYNC_TRACE
#include <tinyasync/tinyasync.h>

using namespace tinyasync;


Task echo(IoContext& ctx, Connection conn, Name="echo") {

	for(;;) {

		char buf[1000];
		std::size_t nread = co_await conn.async_receive(buf, 1000);
		co_await conn.async_send(buf, nread);
	}
}

Task listen(IoContext &ctx, Name="listen") {

	Acceptor acceptor(ctx, Protocol::ip_v4(), Endpoint(Address::Any(), 8899));

	for (;;) {
		Connection conn = co_await acceptor.async_accept();
		co_spawn(echo(ctx, std::move(conn)), "spawn echo");
	}

}

void echo_server() {	
	TINYASYNC_GUARD("echo_server():");
	IoContext ctx;
	co_spawn(listen(ctx), "spawn listen") ;
	TINYASYNC_LOG("run");
	ctx.run();
}

int main()
{
	echo_server();
	return 0;
}


