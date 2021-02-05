#define TINYASYNC_TRACE

//#define TINYASYNC_THROW_ON_OPEN

#include <tinyasync/tinyasync.h>
#include <string_view>

using namespace tinyasync;

Spawn handle_connection(IoContext& ctx, Connection conn, Name="handle_connection") {
	
	std::string buffer;
	for(;;) {
		char b[1000];
		auto nread = co_await conn.async_read(b, 1000);
		if(nread == 0) {
			throw std::runtime_error("recv error");
		}
		buffer.append(b, b+nread);
		if(buffer.find("\r\n\r\n") || buffer.size() > 1000*4) {
			break;
		}
	}
	printf("Recv Header:\n%s", buffer.c_str());


	bool do_send = false;
	do {
		std::string_view sv = buffer;
		if(sv.ends_with("\r\n\r\n")) {
			if(sv.starts_with("GET ")) { 
				sv = sv.substr(4);
				do_send = sv.starts_with("/ ");
			}
		}
	} while(false);

	char const *response = nullptr;

	if(do_send) {

		response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n"
		R"(<html>
<head><title>Hello, World</title></head>
<body>Hello, World</body>
</html>)";

	} else {
		response = "HTTP/1.1 404 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n"
		R"(<html>
	<head><title>404!</title></head>
	<body>404!</body>
	</html>)";
	}

	auto remain = strlen(response);
	char const * b = response;
	for(;remain;) {

		auto nsent = co_await conn.async_send(b, remain);
		if(nsent == 0) {
			throw std::runtime_error("send error");
		}
		remain -= nsent;
		b += nsent;
	}
	printf("send done\n");

}

Task listen(IoContext &ctx, Name="listen") {

	Acceptor acceptor(ctx, Protocol::ip_v4(), Endpoint(Address::Any(), 8899));

	for (;;) {
		Connection conn = co_await acceptor.async_accept();
		try {
			handle_connection(ctx, std::move(conn));
		} catch(...) {
			printf("error in handle connection\n");
		}
	}

}

void server() {	
	TINYASYNC_GUARD("server(): ");

	IoContext ctx;
	co_spawn(listen(ctx), "co_spawn listen");

	TINYASYNC_LOG("run");
	ctx.run();
}


int main()
{

	try {
		server();
	} catch(...) {
		printf("%s\n", to_string(std::current_exception()).c_str());
	}
	return 0;
}


