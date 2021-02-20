//#define TINYASYNC_TRACE
#include <tinyasync/tinyasync.h>

using namespace tinyasync;

Task<> send_all(Connection &conn, bool &run, void *buf, size_t bytes)
{

	char const *send_buf = (char const *)buf;
	std::size_t remain = bytes;

	// repeat send until all are sent
	for(;;) {
		std::size_t nsent = co_await conn.async_send(send_buf, remain);
		send_buf += nsent;
		remain -= nsent;

		if(nsent == 0) {
			run = false;
			break;
		} else if(remain == 0) {
			break;
		}
	}
}

Task<> handle_connection(IoContext& ctx, Connection conn, Name="handle_connection")
{
	bool run = true;
	for(;run;) {

		char buf[1000];
		std::size_t nread = co_await conn.async_read(buf, 1000);
		if(nread == 0) {
			run = false;
			co_return;		
		}
		co_spawn(send_all(conn, run, buf, nread));
	}
}


Task<> listen(IoContext &ctx, Name="listen")
{

	Acceptor acceptor(ctx, Protocol::ip_v4(), Endpoint(Address::Any(), 8899));

	for (;;) {
		Connection conn = co_await acceptor.async_accept();

		// run until blocking by io, then return immediately
		// so that we have a chance to go back to acceptor.async_accept()
		// where this coroutine is blocked again
		// the handle_connection will be resumed if coresponding io of handle_connection is ready
		co_spawn(handle_connection(ctx, std::move(conn)));
	}

}

void server() {	
	TINYASYNC_GUARD("server():");

	IoContext ctx;

	// run until blocking by io, then return immediately,
	// so that we can goto event loop in ctx.run(),
	// where we will resume the coroutine when io is ready

	co_spawn(listen(ctx));

	TINYASYNC_LOG("run");
	ctx.run();
}

int main()
{
	server();
	return 0;
}


