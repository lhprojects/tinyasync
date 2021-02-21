#define TINYASYNC_TRACE
#include <tinyasync/tinyasync.h>
#include <utility>
#include <stack>
#include <queue>

using namespace tinyasync;

// linked buffer

struct LB : ListNode {
	Buffer buffer;
	std::byte data[1];
};

Pool pool { sizeof(LB) + 1000, 10 };

inline LB *allocate()
{
	auto b = (LB*)pool.alloc();
	b->buffer.m_data = b->data;
	b->buffer.m_size = 1000;
	return b;
}

inline void deallocate(LB *b)
{
	pool.free(b);
}

struct Session
{
	Session(IoContext &ctx, Connection conn_)
		: m_ctx(&ctx),
		  conn(std::move(conn_))
		{

	}

	std::pmr::memory_resource *get_memory_resource_for_task() {
		return m_ctx->get_memory_resource_for_task();
	}

	IoContext *m_ctx;
	Connection conn;
	Event m_on_buffer_has_data { *m_ctx };
	Event all_done { *m_ctx };
	Queue m_que;
	bool m_run = true;

	Task<> read(IoContext &ctx)
	{

		for(;m_run;) {

			LB *b = allocate();
			// read some
			std::size_t nread = co_await conn.async_read(b->buffer);
			if(nread == 0) {
				m_run = false;
			}
			b->buffer = b->buffer.sub_buffer(0, nread);
			m_que.push(b);
			m_on_buffer_has_data.notify_one();

		}

		all_done.notify_one();

	}

	Task<> send(IoContext &ctx)
	{

		for(;m_run;) {

			LB *b;
			// wait for data to send
			for(;;) {
				auto node = m_que.pop();
				if(node) {
					b = (LB*)node;
					break;
				}
				co_await m_on_buffer_has_data;
			}

			// repeat send until all are sent
			Buffer buffer = b->buffer;
			for(;;) {
				std::size_t nsent = co_await conn.async_send(buffer);
				buffer = buffer.sub_buffer(nsent);

				if(nsent == 0) {
					// peer shutdown
					m_run = false;
					break;
				} else if(!buffer.size()) {
					break;
				}
			}
			deallocate(b);
		}
	}
};

Task<> start(IoContext &ctx, Session s)
{
	co_spawn(s.read(ctx));
	co_await s.send(ctx);
	for(;s.m_run;) {
		co_await s.all_done;
	}
}


Task<> listen(IoContext &ctx)
{
	Acceptor acceptor(ctx, Protocol::ip_v4(), Endpoint(Address::Any(), 8899));

	for (;;) {
		Connection conn = co_await acceptor.async_accept();
		co_spawn(start(ctx, Session(ctx, std::move(conn))));
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


