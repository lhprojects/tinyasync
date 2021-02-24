
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

        conn.ensure_close();
        m_on_buffer_has_data.notify_one();
        read_finish = true;
        read_finish_event.notify_one();
    }

    Task<> send(IoContext &ctx)
    {

        bool run = true;
        for (;run;)
        {

            LB *b = nullptr;

            // wait for data to send
            for (;;)
            {
                if(!conn.is_connected()) {  
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

            // repeat send until all are sent
            Buffer buffer = b->buffer;
            for (;;)
            {
                if(!conn.is_connected())
                    break;
                std::size_t nsent;
                try {
                    nsent = co_await conn.async_send(buffer);
                } catch(...) {     
                    printf("send exception: %s", to_string(std::current_exception()).c_str());
                    run = false;
                    break;             
                }
                if (nsent == 0)
                {
                    printf("send peer shutdown\n");
                    run = false;
                    break;
                }
                buffer = buffer.sub_buffer(nsent);
                nwrite_total += nsent;
                if(!buffer.size())
                {
                    break;
                }
            }
            deallocate(m_pool, b);
        }

        conn.ensure_close();
        send_finish = true;
        send_finish_event.notify_one();
    }
};

#endif
