#define TINYASYNC_TRACE
#include <tinyasync/tinyasync.h>

using namespace tinyasync;

Task echo_once(Connection &conn, bool &run, Name = "echo_once") {
	char buf[1000];
	std::size_t nread = co_await conn.async_read(buf, 1000);
	if(nread == 0) {
		printf("peer shutdown\n");
		run = false;
		co_return;		
	}

	char *send_buf = buf;
	std::size_t remain = nread;

	// repeat send until all are sent
	for(;;) {
		std::size_t nsent = co_await conn.async_send(send_buf, remain);
		send_buf += nsent;
		remain -= nsent;

		if(nsent == 0) {
			printf("peer shutdown\n");
			run = false;
			break;
		} else if(remain == 0) {
			break;
		}
	}
}

Spawn handle_connection(IoContext& ctx, Connection conn, Name="handle_connection") {
	bool run = true;
	for(;run;) {
		// suspend initially
		auto task = echo_once(conn, run);
		// run until blocking by io
		// after task is fully done, then return
		co_await task;
	}
}


Task listen(IoContext &ctx, Name="listen") {

	Acceptor acceptor(ctx, Protocol::ip_v4(), Endpoint(Address::Any(), 8899));

	for (;;) {
		Connection conn = co_await acceptor.async_accept();

		// run until blocking by io, then return immediately
		// so that we have a chance to go back to acceptor.async_accept()
		// where this coroutine is blocked again
		// the handle_connection will be resumed if coresponding io of handle_connection is ready
		// note `handle_connection` returns Spawn
		handle_connection(ctx, std::move(conn));
	}

}

void server() {	
	TINYASYNC_GUARD("server():");

	IoContext ctx;
	// run until blocking by io then return immediately,
	// so that we can goto event loop in ctx.run()
	// where we will resume the coroutine when io is ready
	// note `listen` returns Task
	// you can let listen return Spawn, and replace co_spawn(listen()) with listen()	
	co_spawn(listen(ctx), "co_spawn listen");

	TINYASYNC_LOG("run");
	ctx.run();
}

int main()
{
	server();
	return 0;
}


