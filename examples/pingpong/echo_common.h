
#ifndef ECHO_COMMON_H
#define ECHO_COMMON_H

#include <tinyasync/tinyasync.h>

#include <utility>
#include <stack>
#include <queue>

using namespace tinyasync;

std::atomic_uint64_t nwrite_total;
std::atomic_uint64_t nread_total;
size_t block_size;


struct LB : ListNode
{
    Buffer buffer;
    std::byte data[1];
};

inline LB *allocate(Pool *pool)
{
    auto b = (LB *)pool->alloc();
    if(!b) {
        printf("memory ex\n");
        exit(1);
    }
    b->buffer.m_data = b->data;
    b->buffer.m_size = block_size;
    return b;
}

inline void deallocate(Pool *pool, LB *b)
{
    pool->free(b);
}

void initialize_pool(Pool &pool)
{
    pool.initialize(sizeof(LB) + block_size - 1, 20);
}

struct Session
{
    Session(IoContext &ctx, Connection conn_, Pool *pool)
        : m_ctx(&ctx),
          conn(std::move(conn_)),
          m_pool(pool)
    {
    }


    std::pmr::memory_resource *get_memory_resource_for_task()
    {
        return m_ctx->get_memory_resource_for_task();
    }

    IoContext *m_ctx;
    Pool *m_pool;
    Connection conn;

    Queue m_que;
    Event m_on_buffer_has_data{*m_ctx};

    Event read_finish_event{*m_ctx};
    Event send_finish_event{*m_ctx};
    bool read_finish = false;
    bool send_finish = false;

    Task<> read(IoContext &ctx)
    {
    
        for (; ;)
        {

            LB *b = allocate(m_pool);
            // read some
            std::size_t nread;
            //co_await async_sleep(ctx, std::chrono::milliseconds(100));            
            try {
                nread = co_await conn.async_read(b->buffer);
            } catch(...) {
                printf("read exception: %s", to_string(std::current_exception()).c_str());
                break;
            }
            if (nread == 0)
            {
                printf("read peer shutdown\n");
                break;
            }
            nread_total += nread;

            b->buffer = b->buffer.sub_buffer(0, nread);
            m_que.push(b);
            m_on_buffer_has_data.notify_one();

        }

        m_on_buffer_has_data.notify_one();
        read_finish = true;
        read_finish_event.notify_one();
    }


    // repeat send until all are sent
    static Task<size_t> send_all(IoContext &ctx, Connection &conn, Buffer buffer) {
        size_t total_sent = 0;
        for (;;)
        {
            auto nsent = 0;
            if(conn.is_closed() || conn.is_send_shutdown())
                break;
            nsent = co_await conn.async_send(buffer);
            if (nsent == 0)
            {
                printf("send peer shutdown\n");
                break;
            }
            total_sent += nsent;
            buffer = buffer.sub_buffer(nsent);
            if(!buffer.size())
            {
                break;
            }
        }
        co_return total_sent;
    }

    Task<> send(IoContext &ctx)
    {
        for (;;)
        {

            LB *b = nullptr;

            // wait for data to send
            for (;;)
            {
                if(conn.is_closed() || conn.is_send_shutdown()) {  
                    break;
                }                
                auto node = m_que.pop();
                if (node)
                {
                    b = (LB *)node;
                    break;
                }
                co_await m_on_buffer_has_data;
            }
            if(!b)
                break;

            if(conn.is_closed() || conn.is_send_shutdown())
                break;

            size_t nsent;

            try {
                nsent = co_await send_all(ctx, conn, b->buffer);
            } catch(...) {     
                printf("send exception: %s", to_string(std::current_exception()).c_str());
                break;             
            }
            nwrite_total += nsent;
            deallocate(m_pool, b);
            if (nsent < b->buffer.size())
            {
                printf("send peer shutdown\n");
                break;
            }
        }

        send_finish = true;
        send_finish_event.notify_one();
    }
};

#endif
