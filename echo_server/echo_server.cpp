#define TINYASYNC_TRACE
#include <tinyasync/tinyasync.h>

using namespace tinyasync;


Task echo(IoContext& ctx, Connection conn, Name="echo") {

	bool run = true;
	char buf[1000];

	for(;run;) {

		std::size_t nread = co_await conn.async_receive(buf, 1000);
		if(nread == 0) {
			printf("peer shutdown\n");
			run = false;
			break;
		}

		char *send_buf = buf;
		std::size_t remain = nread;
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


