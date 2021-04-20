//#define TINYASYNC_TRACE

#include <tinyasync/tinyasync.h>
#include <string_view>
#include <string>
#include <queue>

using namespace tinyasync;

Task<size_t> async_send_all(Connection &conn, void const *buffer, size_t bytes)
{
    size_t sent = 0;
    for(;bytes;) {

        auto nsend = co_await conn.async_send(buffer, bytes);
        if(nsend == 0) {
            break;
        }
        sent += nsend;
        bytes -= nsend;
        (char const *&)buffer += nsend;
    }
    co_return sent;
}

std::atomic_uint64_t g_id = 0;

uint64_t get_id() {
    return g_id.fetch_add(1, std::memory_order_relaxed);
}

class ChatRoomServer;
struct Part
{

    ChatRoomServer *m_chatroom;
    std::string m_name;
    Connection m_conn;
    Mutex m_conn_mtx;
    uint64_t m_id;


    Mutex m_msg_mtx;
    std::queue<std::string> m_messages;
    ConditionVariable m_msg_condv;

    Part(ChatRoomServer &server, Connection conn);

    Part(Part const &r) = delete;
    Part &operator=(Part const &) = delete;

    uint64_t id() const {
        return m_id;
    }

    bool operator==(Part const &r) const {
        return id() == r.id();
    }

    void post_msg(std::string msg)
    {
        m_messages.push(std::move(msg));
        m_msg_condv.notify_all();
    }

    Task<> listen();

    Task<> send();
    Task<> start();

};

class ChatRoomServer {
public:

    IoContext m_ctx_;
    IoContext *m_ctx = &m_ctx_;
    std::list<Part> m_clients;
    Mutex m_mtx;

    ChatRoomServer()  {

    }

    Task<> broadcast(Part *client, std::string const &msg)
    {
        co_await m_mtx.lock(*m_ctx);
        auto m_mtx_ = auto_unlock(m_mtx);

        for(auto &c: m_clients) {
            if(client != &c)
                c.post_msg(msg);
            else
                c.post_msg("**** server received ****\n");
        }
    }

    Task<> on_join(Connection conn_)
    {
        co_await m_mtx.lock(*m_ctx);
        auto m_mtx_ = auto_unlock(m_mtx);

        m_clients.emplace_back(*this, std::move(conn_));
        auto *client = &m_clients.back();

        m_mtx_.unlock();

        co_spawn(client->start());

    }


    Task<> on_leave(Part *part)
    {
        co_await m_mtx.lock(*m_ctx);
        auto m_mtx_ = auto_unlock(m_mtx);

        m_clients.remove(*part);
    }



    Task<> listen()
    {
        Endpoint ep{Address::Any(), 8899};
        printf("listenning to %s:%d\n", ep.address().to_string().c_str(), ep.port());
        printf("connect server using: nc localhost %d\n", ep.port());
        Acceptor acceptor(*m_ctx, Protocol::ip_v4(), ep);
        for (;;) {
            Connection conn = co_await acceptor.async_accept();
            co_spawn(on_join(std::move(conn)));
        }
    }

    void start()
    {
        co_spawn(this->listen());
        m_ctx->run();

    }
};

Part::Part(ChatRoomServer &server, Connection conn) :
    m_chatroom(&server),
    m_msg_mtx(),
    m_msg_condv(*server.m_ctx),
    m_conn_mtx(),
    m_conn(std::move(conn)) {        
    m_id = get_id();
    m_name = tinyasync::format("<%d>", (int)m_id);
}

Task<> Part::listen() {

    auto *client = this;
    auto &conn = client->m_conn;
    std::string buffer;
    for (;;)
    {
        char sb[1000];

        printf("read...\n");
        auto nread = co_await client->m_conn.async_read(sb, sizeof(sb));
        printf("read\n");

        if (nread == 0)
        {
            break;
        }

        char *end = sb + nread;
        char *break_ = std::find(sb, end, '\n');

        if (break_ != end)
        {
            std::string output = std::move(buffer);
            output.append(sb, break_);
            buffer.assign(break_+1, end); // +1 skip \n
            std::string msg = client->m_name + ": " + output + "\n";
            co_await m_chatroom->broadcast(client, msg);
        }
        else
        {
            buffer.append(sb, break_);
        }
    }
}

Task<> Part::send() {
    for(;;)
    {
        co_await m_msg_mtx.lock(*(this->m_chatroom->m_ctx));
        for(;!m_messages.size();) {                
            printf("waiting..\n");
            co_await m_msg_condv.wait(m_msg_mtx);
            printf("!");
        }
        std::string msg = std::move(m_messages.back());
        m_messages.pop();
        m_msg_mtx.unlock();

        printf("%s sending\n", msg.c_str());
        size_t nsent = co_await async_send_all(m_conn, msg.data(), msg.size());
        printf("%d bytes sent\n", (int)nsent);
        if(nsent < msg.size()) {
            printf("send error\n");
            break;
        }
    }
}



Task<> Part::start() {

    auto *client = this;

    co_await m_chatroom->broadcast(client, "Welcome " + client->m_name + "\n");
    co_spawn(client->listen());
    co_await send();

}


int main()
{

    Connection conn;
	try {
        ChatRoomServer server;
		server.start();
	} catch(...) {
		printf("%s\n", to_string(std::current_exception()).c_str());
	}
	return 0;
}


