
#ifndef ECHO_COMMON_H
#define ECHO_COMMON_H

#include <tinyasync/tinyasync.h>

#include <utility>
#include <stack>
#include <queue>

using namespace tinyasync;

uint64_t nwrite_total;
uint64_t nread_total;
size_t block_size;
Pool pool;


struct LB : ListNode
{
    Buffer buffer;
    std::byte data[1];
};

inline LB *allocate(Pool *pool)
{
    auto b = (LB *)pool->alloc();
    b->buffer.m_data = b->data;
    b->buffer.m_size = block_size;
    return b;
}

inline void deallocate(Pool *pool, LB *b)
{
    pool->free(b);
}

void initialize_pool() {
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
    Connection conn;
    Event m_on_buffer_has_data{*m_ctx};
    Event all_done{*m_ctx};
    Queue m_que;
    bool m_run = true;
    Pool *m_pool;

    Task<> read(IoContext &ctx)
    {

        for (; m_run;)
        {

            LB *b = allocate(m_pool);
            // read some
            std::size_t nread = co_await conn.async_read(b->buffer);
            if (nread == 0)
            {
                m_run = false;
            }
            nread_total += nread;

            b->buffer = b->buffer.sub_buffer(0, nread);
            m_que.push(b);
            m_on_buffer_has_data.notify_one();

        }

        all_done.notify_one();
    }

    Task<> send(IoContext &ctx)
    {

        for (; m_run;)
        {

            LB *b;
            // wait for data to send
            for (;;)
            {
                auto node = m_que.pop();
                if (node)
                {
                    b = (LB *)node;
                    break;
                }
                co_await m_on_buffer_has_data;
            }

            // repeat send until all are sent
            Buffer buffer = b->buffer;
            for (;;)
            {
                std::size_t nsent = co_await conn.async_send(buffer);
                buffer = buffer.sub_buffer(nsent);
                nwrite_total += nsent;

                if (nsent == 0)
                {
                    // peer shutdown
                    m_run = false;
                    break;
                }
                else if (!buffer.size())
                {
                    break;
                }
            }
            deallocate(m_pool, b);
        }
    }
};

#endif
