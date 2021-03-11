#ifndef TINYASYNC_MEMORY_POOL_H
#define TINYASYNC_MEMORY_POOL_H

#include <cstddef>   // std::size_t, std::byte
#include <bit>       // std::countx_zeros
#include <vector>
#include <stdlib.h>
#include <memory>
#include <assert.h>
#include <memory_resource>

namespace tinyasync
{

    struct PoolNode {
        PoolNode *m_next;
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

        Pool(Pool &&r)
        {
            m_block_size = r.m_block_size;
            m_block_per_chunk = r.m_block_per_chunk;
            m_head = r.m_head;
            m_chunks = std::move(r.m_chunks);
            r.m_head = nullptr;
            r.m_block_size = 0;
            r.m_block_per_chunk = 0;
        }

        void swap(Pool &r)
        {
            std::swap(m_block_size, r.m_block_size);
            std::swap(m_block_per_chunk, r.m_block_per_chunk);
            std::swap(m_head, r.m_head);
            std::swap(m_chunks, r.m_chunks);
        }

        Pool &operator=(Pool &&r)
        {
            Pool pool(std::move(r));
            return *this;
        }

        void initialize(std::size_t block_size, std::size_t block_per_chunk)
        {
            m_block_size = std::max(sizeof(void *), block_size);
            m_block_per_chunk = block_per_chunk;
            m_head = nullptr;
        }

        size_t m_block_size;
        size_t m_block_per_chunk;
        PoolNode *m_head;

        std::vector<void *> m_chunks;

        void *alloc() noexcept
        {
            auto head = m_head;
            if (!head) {
                    auto block_per_chunk = m_block_per_chunk;
                    auto block_size = m_block_size;
                    std::size_t memsize = block_size * block_per_chunk;
                    if (!memsize)
                    {
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
                        PoolNode *p = (PoolNode *)(((char *)h) + block_size * i);
                        PoolNode *p2 = (PoolNode *)(((char *)h) + block_size * (i + 1));
                        p->m_next = p2;
                    }
                    PoolNode *p = (PoolNode *)(((char *)h) + block_size * (block_per_chunk - 1));
                    p->m_next = nullptr;
                    m_head = (PoolNode *)h;
                    head = m_head;
            }
            m_head = head->m_next;
            return head;
        }

        void free(void *node_) noexcept
        {
            auto node = (PoolNode *)node_;
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

    struct FreeNode
    {
        FreeNode *m_next;
        FreeNode *m_prev;

        void init()
        {
            m_next = this;
            m_prev = this;
        }

        bool remove_self()
        {
            auto prev = this->m_prev;
            auto next = this->m_next;
            next->m_prev = prev;
            prev->m_next = next;
            return prev == next;
        }

        void push(FreeNode *node)
        {
            auto next = m_next;
            node->m_next = next;
            node->m_prev = this;
            this->m_next = node;
            next->m_prev = node;
        }
    };

    struct alignas(std::max_align_t) MemNode
    {
        MemNode *m_next;
        MemNode *m_prev;
        std::size_t m_size; // total size include header
        bool m_free;        // free?

        void init()
        {
            m_next = this;
            m_prev = this;
        }

        void push(MemNode *node)
        {
            assert(node > (void *)100);
            assert(this > (void *)100);
            auto const next = this->m_next;
            node->m_next = next;
            node->m_prev = this;
            this->m_next = node;
            next->m_prev = node;
        }

        void remove_self()
        {
            assert(this > (void *)100);
            auto prev = this->m_prev;
            auto next = this->m_next;
            next->m_prev = prev;
            prev->m_next = next;
        }
    };

    struct PoolBlock
    {
        MemNode m_mem_node;
        FreeNode m_free_node;
        std::size_t m_order;

        void *mem()
        {
            return (char *)this + sizeof(MemNode);
        }

        static PoolBlock *from_mem(void *p)
        {
            assert((void *)p > (void *)100);
            return (PoolBlock *)((char *)p - sizeof(MemNode));
        }

        static PoolBlock *from_mem_node(MemNode *node)
        {
            assert(node > (void *)100);
            return (PoolBlock *)((char *)node - offsetof(PoolBlock, m_mem_node));
        }

        static PoolBlock *from_free_node(FreeNode *node)
        {
            assert(node > (void *)100);
            return (PoolBlock *)((char *)node - offsetof(PoolBlock, m_free_node));
        }
    };

    // inline std::map<void *, bool> m_allocated;

    struct PoolImpl
    {
        static const int k_max_order = 12 * 4;

        uint64_t m_free_flags;
        FreeNode m_free[k_max_order + 1];

        FreeNode m_chuncks;

        PoolImpl()
        {
            for (auto &m : m_free)
            {
                m.m_next = &m;
                m.m_prev = &m;
            }
            m_free_flags = 0;
            m_chuncks.init();
        }

        ~PoolImpl()
        {
            auto c = m_chuncks.m_next;
            for(;c != &m_chuncks;) {
                auto next = c->m_next;
                auto p = PoolBlock::from_free_node(c);
                ::free(p);
                c = next;
            }
        }

        static std::size_t block_order(std::size_t block_size)
        {
            //if(block_size < 16) return 0;
            int offset = int(sizeof((unsigned)block_size) * 8) - std::countl_zero((unsigned)block_size) - 3;
            return ((block_size >> offset) & 3) + (offset * 4);
        }

        static std::size_t block_size(std::size_t block_order)
        {
            // 1xx * 2^ y
            // xx = block_order & 3
            // y = block_order / 4
            return (4 + (block_order & 3)) << (block_order / 4);
        }

        static std::size_t ffs64(uint64_t v)
        {
            return std::countr_zero(v);
        }

        void add_free_block(PoolBlock *block)
        {
            assert(block > (void *)100);
            block->m_mem_node.m_free = true;
            std::size_t idx = block_order(block->m_mem_node.m_size);
            block->m_order = idx;
            m_free[idx].push(&(block->m_free_node));
            m_free_flags |= (uint64_t(1) << idx);
        }

        void remove_free_block(PoolBlock *block)
        {
            assert(block > (void *)100);
            block->m_mem_node.m_free = false;
            if (block->m_free_node.remove_self())
            {
                std::size_t idx = block->m_order;
                m_free_flags &= ~(uint64_t(1) << idx);
            }
        }

        void change_size(PoolBlock *block, std::size_t new_size)
        {
            assert(block > (void *)100);
            auto old_ord = block->m_order;
            auto new_ord = block_order(new_size);
            block->m_mem_node.m_size = new_size;
            if (old_ord != new_ord)
            {
                block->m_order = new_ord;
                if (block->m_free_node.remove_self())
                {
                    std::size_t idx = old_ord;
                    m_free_flags &= ~(uint64_t(1) << idx);
                }
                m_free[new_ord].push(&block->m_free_node);
                std::size_t idx = new_ord;
                m_free_flags |= (uint64_t(1) << idx);
            }
        }

        PoolBlock *break_(PoolBlock *block, std::size_t block_size)
        {

            assert(block > (void *)100);

            auto old_size = block->m_mem_node.m_size;
            auto new_size = old_size - block_size;

            if (new_size < sizeof(PoolBlock))
            {
                remove_free_block(block);
                return block;
            }

            auto block2 = (PoolBlock *)((char *)block + new_size);
            block2->m_mem_node.m_size = block_size;
            block2->m_mem_node.m_free = false;

            block->m_mem_node.push(&block2->m_mem_node);

            change_size(block, new_size);

            return block2;
        }

        static void *alloc(PoolImpl *pool, std::size_t size)
        {
            size += sizeof(PoolBlock);
            const std::size_t max_align = alignof(std::max_align_t);
            const std::size_t block_size = (size + max_align - 1) & ~(max_align - 1);

            auto idx_ = PoolImpl::block_order(block_size);
            std::size_t idx;

            if (idx_ > k_max_order)
            {
                return ::malloc(size);
            }

            PoolBlock *block;
            idx = ffs64(pool->m_free_flags & ~((1 << idx_) - 1));

            if (idx != 64)
            {
                FreeNode &node = pool->m_free[idx];
                auto next = node.m_next;
                if (next == &node)
                {
                    assert(next != &node);
                }
                block = PoolBlock::from_free_node(next);
            }
            else
            {

                auto size = PoolImpl::block_size(k_max_order);
                auto head = (PoolBlock *)::malloc(size + sizeof(PoolBlock));
                head->m_mem_node.init();
                head->m_mem_node.m_free = false;
                head->m_mem_node.m_size = sizeof(PoolBlock);
                head->m_order = 0; // not used

                block = (PoolBlock *)(head + 1);
                block->m_mem_node.m_size = size;

                head->m_mem_node.push(&block->m_mem_node);

                pool->m_chuncks.push(&head->m_free_node);
                pool->add_free_block(block);
            }

            block = pool->break_(block, block_size);

            assert(block->m_mem_node.m_next > (void *)100);
            assert(block->m_mem_node.m_prev > (void *)100);
            assert(block->m_mem_node.m_free == false);

            // assert(m_allocated[block->mem()] == false);
            // assert(m_allocated[block->mem()] = true);

            return block->mem();
        }

        static void free(PoolImpl *pool, void *p)
        {

            // assert(m_allocated[p] == true);
            // assert((m_allocated[p] = false, true));

            PoolBlock *cur_block = PoolBlock::from_mem(p);

            const bool merge = true;
            assert(cur_block->m_mem_node.m_free == false);
            assert(cur_block->m_mem_node.m_next > (void *)100);
            assert(cur_block->m_mem_node.m_prev > (void *)100);

            MemNode *cur = &cur_block->m_mem_node;

            MemNode *next = merge ? cur->m_next : nullptr;
            MemNode *prev = merge ? cur->m_prev : nullptr;

            bool nf = merge && next->m_free;
            bool pf = merge && prev->m_free;

            std::size_t size;
            if (nf && pf)
            {

                size = prev->m_size + cur->m_size + next->m_size;
                auto nn = next->m_next;

                nn->m_prev = prev;
                prev->m_next = nn;

                prev->m_size = size;

                auto next_block = PoolBlock::from_mem_node(next);
                pool->remove_free_block(next_block);

                auto prev_block = PoolBlock::from_mem_node(prev);
                pool->change_size(prev_block, size);
            }
            else if (pf)
            {
                size = prev->m_size + cur->m_size;
                cur->remove_self();

                auto prev_block = PoolBlock::from_mem_node(prev);

                pool->change_size(prev_block, size);
            }
            else if (nf)
            {
                size = cur->m_size + next->m_size;
                next->remove_self();

                cur->m_size = size;
                auto next_block = PoolBlock::from_mem_node(next);

                pool->remove_free_block(next_block);
                pool->add_free_block(cur_block);
            }
            else
            {
                pool->add_free_block(cur_block);
            }
        }
    };

    struct PoolResource : std::pmr::memory_resource
    {

        PoolImpl m_impl;
        virtual void *
        do_allocate(size_t __bytes, size_t __alignment)
        {
            return PoolImpl::alloc(&m_impl, __bytes);
        }

        virtual void
        do_deallocate(void *__p, size_t __bytes, size_t __alignment)
        {
            PoolImpl::free(&m_impl, __p);
        }

        virtual bool
        do_is_equal(const std::pmr::memory_resource &__other) const noexcept
        {
            return true;
        }
    };

} // namespace tinyasync

#endif
