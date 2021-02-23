//#define TINYASYNC_TRACE
#include "echo_common.h"

#include <thread>
#include <atomic>

std::atomic_int32_t nc;

struct Server
{
	IoContext m_ctx;
	Pool m_pool;
	Acceptor m_acceptor;
	std::thread m_thread;
	int m_id;

	Server(int i, Acceptor &acc) 
	{
		m_id = i;
		m_acceptor = acc.reset_io_context(m_ctx);
	}

	Task<> start(IoContext &ctx, Session s)
	{
		co_spawn(s.read(ctx));
		co_await s.send(ctx);

		// read join
		for(;!s.read_finish;) {
			co_await s.read_finish_event;
		}

		--nc;
		printf("[%d] %d conn\n", m_id, nc.load());
	}

	Task<> listen(IoContext &ctx)
	{
		for (int i = 0; ; ++i) {
			Connection conn = co_await m_acceptor.async_accept();
			++nc;
			co_spawn(start(ctx, Session(ctx, std::move(conn), &m_pool)));
			printf("[%d] %d conn\n", m_id, nc.load());
		}

	}

	void serve()
	{
		m_thread = std::thread([this]() {

			try {
				initialize_pool(m_pool);
				TINYASYNC_GUARD("server():");
				printf("[%d] start\n", m_id);

				auto &ctx = m_ctx;

				co_spawn(listen(ctx));

				TINYASYNC_LOG("run");
				ctx.run();

				printf("[%d] finish\n", m_id);
			} catch(...) {
				printf("%s\n", to_string(std::current_exception()).c_str());
			}
		});

	}

};


int main()
{
    block_size = 1024;
	Acceptor acceptor(Protocol::ip_v4(), Endpoint(Address::Any(), 8899));

	printf("hardware_concurrency %d\n", (int)std::thread::hardware_concurrency());
	std::vector<Server> servers;
	int n = 1;
	for(int i = 0; i < n; ++i) {
		servers.push_back(Server(i, acceptor));
	}
	for(int i = 0; i < n; ++i) {
		servers[i].serve();
	}
	for(int i = 0; i < n; ++i) {		
		servers[i].m_thread.join();
	}

	printf("done!\n");
	return 0;
}
