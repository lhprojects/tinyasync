#ifndef TINYASYNC_BUFFER_H
#define TINYASYNC_BUFFER_H

#include <cstddef> // std::size_t, std::byte

namespace tinyasync
{

    struct Buffer;
    struct BufferRef;
    struct ConstBuffer;
    struct ConstBufferRef;

    struct Buffer
    {
        std::byte *m_data;
        std::size_t m_size;

        Buffer() : m_data(nullptr), m_size(0)
        {
        }

        Buffer(Buffer const &r) : m_data(r.m_data), m_size(r.m_size)
        {
        }

        Buffer(std::byte *data, std::size_t size) : m_data(data), m_size(size)
        {
        }

        Buffer(ConstBuffer &b) = delete;

        
        template <class T, std::size_t n>
        Buffer(T (&a)[n])
        {
            m_data = static_cast<std::byte *>(&a[0]);
            m_size = sizeof(a);
        }

        template <class C>
        Buffer(C &a)
        {
            m_data = static_cast<std::byte *>(a.data());
            m_size = a.size() * sizeof(a.data()[0]);
        }

        Buffer sub_buffer(size_t offset)
        {
            TINYASYNC_ASSERT(offset <= m_size);
            return {m_data + offset, m_size - offset};
        }

        Buffer sub_buffer(size_t offset, std::size_t cnt)
        {
            TINYASYNC_ASSERT(offset + cnt <= m_size);
            return {m_data + offset, cnt};
        }

        std::byte *data()
        {
            return m_data;
        }

        std::byte const *data() const
        {
            return m_data;
        }

        std::size_t size() const
        {
            return m_size;
        }
    };

    struct ConstBuffer
    {
        std::byte const *m_data;
        std::size_t m_size;

        ConstBuffer() : m_data(nullptr), m_size(0)
        {
        }

        ConstBuffer(ConstBuffer const &r) : m_data(r.m_data), m_size(r.m_size)
        {
        }


        ConstBuffer(std::byte const *data, std::size_t size) : m_data(data), m_size(size)
        {
        }

        template <class T, std::size_t n>
        ConstBuffer(T const (&a)[n])
        {
            m_data = (std::byte const*)(&a[0]);
            m_size = sizeof(a);
        }
        
        ConstBuffer(Buffer const &r) : m_data(r.m_data), m_size(r.m_size)
        {
        }

        template <class C>
        ConstBuffer(C const &a)
        {
            m_data = static_cast<std::byte const *>(a.data());
            m_size = a.size() * sizeof(a.data()[0]);
        }

        ConstBuffer sub_buffer(size_t offset) const
        {
            TINYASYNC_ASSERT(offset <= m_size);
            return {m_data + offset, m_size - offset};
        }

        ConstBuffer sub_buffer(size_t offset, std::size_t cnt) const
        {
            TINYASYNC_ASSERT(offset + cnt <= m_size);
            return {m_data + offset, cnt};
        }

        std::byte const *data() const
        {
            return m_data;
        }

        std::size_t size() const
        {
            return m_size;
        }
    };

    class Pool
    {
        
    public:
        Pool() noexcept : m_block_size(0), m_block_per_chunk(0), m_head(nullptr)
        {
        }

        Pool(std::size_t block_size, std::size_t block_per_chunk) noexcept
        {
            initialize(block_size, block_per_chunk);
        }

        void initialize(std::size_t block_size, std::size_t block_per_chunk) {
            m_block_size = std::max(sizeof(void *), block_size);
            m_block_per_chunk = block_per_chunk;
            m_head = nullptr;
        }

        size_t m_block_size;
        size_t m_block_per_chunk;
        ListNode *m_head;

        std::vector<void *> m_chunks;

        void *alloc() noexcept
        {
            auto head = m_head;
            if (!head) TINYASYNC_UNLIKELY
            {
                    auto block_per_chunk = m_block_per_chunk;
                    auto block_size = m_block_size;
                    std::size_t memsize = block_size * block_per_chunk;
                    if(!memsize) {
                        return nullptr;
                    }
                    // max alignment
                    auto *h = ::malloc(memsize);
                    if (!h)
                    {
                        return h;
                    }
                    m_chunks.push_back(h);
                    for (std::size_t i = 0; i < block_per_chunk - 1; ++i)
                    {
                        ListNode *p = (ListNode *)(((char *)h) + block_size * i);
                        ListNode *p2 = (ListNode *)(((char *)h) + block_size * (i + 1));
                        p->m_next = p2;
                    }
                    ListNode *p = (ListNode *)(((char *)h) + block_size * (block_per_chunk - 1));
                    p->m_next = nullptr;
                    m_head = (ListNode *)h;
                    head = m_head;
            }
            m_head = head->m_next;
            return head;
        }

        void free(void *node_) noexcept
        {
            auto node = (ListNode *)node_;
            node->m_next = m_head;
            m_head = node;
        }

        ~Pool() noexcept
        {
            for (auto *p : m_chunks)
            {
                ::free(p);
            }
        }
    };

} // namespace tinyasync

#endif
