//#define TINYASYNC_TRACE

#include <tinyasync/tinyasync.h>
#include "echo_common.h"
using namespace tinyasync;

Pool pool;
int nc = 0;
Task<> send(IoContext &ctx, Connection &c, LB *lb, int &nsending, Event &evt)
{

	// repeat to send all read
	auto remain = lb->buffer.size();
	std::byte *buf = lb->buffer.data();
	for(;;) {
		size_t sent;
		try {
			auto sent = co_await c.async_send(buf, remain);
			if(!sent) {
				c.ensure_close();
				break;
			}
			buf += sent;
			remain -= sent;
			if(!remain) {
				break;
			}
		} catch(...) {
			c.ensure_close();
			break;
		}

	}

	deallocate(&pool, lb);

	--nsending;
	if(nsending == 0) {
		evt.notify_one();
	}
}

Task<> echo(IoContext &ctx, Connection c)
{
	++nc;
	printf("%d conn\n", nc);
	int nsending = 0;
	Event sending_evt(ctx);

	for(;;) {

		
		LB *lb = allocate(&pool);
		// read some
		try {
			auto nread = co_await c.async_read(lb->buffer);
			if(!nread) {
				// send FIN
				c.ensure_send_shutdown();
				break;
			}
			lb->buffer = lb->buffer.sub_buffer(0, nread);
		} catch(...) {
			break;
		}

		++nsending;
		co_spawn(send(ctx, c, lb, nsending, sending_evt));
	}


	if(nsending) {
		co_await sending_evt;
	}

	c.ensure_close();

	--nc;
	printf("%d conn\n", nc);
	
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
	block_size = 1024;
	initialize_pool(pool);
	server();
	return 0;
}


