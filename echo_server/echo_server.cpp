#define TINYASYNC_TRACE

#include <tinyasync/tinyasync.h>
using namespace tinyasync;

Task<> echo(IoContext &ctx, Connection c)
{
	for(;;) {
		char b[100];

		// read some
		auto nread = co_await c.async_read(b, 100);

		if(nread) {
			break;
		}
		// repeat to send all read
		auto remain = nread;
		char *buf = b;
		for(;;) {
			auto sent = co_await c.async_send(buf, remain);
			if(!sent)
				break;
			buf += sent;
			remain -= sent;
			if(!remain)
				break;
		}
	}
	
}

Task<> listen(IoContext &ctx)
{
	Acceptor acceptor(ctx, Protocol::ip_v4(), Endpoint(Address::Any(), 8899));
	for (;;) {
		Connection conn = co_await acceptor.async_accept();
		co_spawn(echo(ctx, std::move(conn)));
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
	server();
	return 0;
}


