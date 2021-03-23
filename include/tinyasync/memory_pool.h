#ifndef TINYASYNC_MEMORY_POOL_H
#define TINYASYNC_MEMORY_POOL_H

#include <cstddef>   // std::size_t, std::byte
#include <bit>       // std::countx_zeros
#include <vector>
#include <stdlib.h>
#include <memory>
#include <assert.h>

#if defined(__clang__)

#include <experimental/memory_resource>
namespace std {
    namespace pmr {
        using std::experimental::pmr::get_default_resource;
        using std::experimental::pmr::set_default_resource;
        using std::experimental::pmr::memory_resource;
        using std::experimental::pmr::new_delete_resource;
    }
}
#else

#include <memory_resource>

#endif

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

    struct PoolBlock
    {
        static const uint32_t k_free_mask = 1<<7;
        static const uint32_t k_order_mask = 0x7F;

        uint32_t m_prev_size;
        uint32_t m_size;          // total size include header
        FreeNode m_free_node;

        uint32_t prev_size() {
            return m_prev_size >> 8;
        }

        uint32_t prev_free() {
            return m_prev_size & k_free_mask;
        }

        std::size_t prev_order() {
            return m_prev_size & k_order_mask;
        }

        static uint32_t encode_size(uint32_t size, std::size_t order, bool free) {
            return (size<<8) | order | (free?k_free_mask:0);
        }

        uint32_t size() {
            return m_size >> 8;
        }

        uint32_t free_() {
            return m_size & k_free_mask;
        }

        std::size_t order() {
            return m_size & k_order_mask;
        }

        void *mem()
        {
            return (char *)this + 2*sizeof(uint32_t);
        }

        static PoolBlock *from_mem(void *p)
        {
            assert((void *)p > (void *)100);
            return (PoolBlock *)((char *)p - 2*sizeof(uint32_t));
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
        static const int k_max_order = 48;
        static const int k_malloc_order = 44;

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
            for(auto i = 0; i < k_max_order; ++i) {
                assert(m_free[i].m_next == &m_free[i]);
            }

            auto c = m_chuncks.m_next;
            for(;c != &m_chuncks;) {
                auto next = c->m_next;
                auto p = PoolBlock::from_free_node(c);
                ::free(p);
                c = next;
            }
        }

        static constexpr uint16_t s1(std::size_t s) {
            return s + s/4;
        }
        static constexpr uint16_t s2(std::size_t s) {
            return s + s/2;
        }
        static constexpr uint16_t s3(std::size_t s) {
            return s + 3*s/4;
        }

        static uint16_t block_order(std::size_t block_size)
        {
#if 0
            static constexpr uint16_t table[] = {
                                    8, s1(8), s2(8), s3(8),
                                    16, s1(16), s2(16), s3(16),
                                    32, s1(32), s2(32), s3(32),
                                    64, s1(64), s2(64), s3(64),
                                    128, s1(128), s2(128), s3(128),
                                    256, s1(256), s2(256), s3(256),
                                    512, s1(512), s2(512), s3(512),
                                    1<<10, s1(1<<10), s2(1<<10), s3(1<<10),
                                    1<<11, s1(1<<11), s2(1<<11), s3(1<<11),
                                    1<<12, s1(1<<12), s2(1<<12), s3(1<<12),
                                    1<<13, s1(1<<13), s2(1<<13), s3(1<<13),
                                    1<<14, s1(1<<14), s2(1<<14), s3(1<<14),
                                    1<<15, 
                                    };

            auto end = table + sizeof(table)/sizeof(table[0]);
            auto p = std::lower_bound(table, end, block_size);
            assert(p != end);
            return p - table;
#else
            const size_t block_size_ = block_size;
            const size_t shift = sizeof(block_size_)*8 - std::countl_zero(block_size_) - 3;
            const size_t block_size__ = block_size_ >> shift;
            const size_t block_size_2 = block_size__ & 3;
            const size_t block_size_3 = (shift-1)*4 + block_size_2;
            const size_t order = block_size_3 + bool(block_size_ & ((1<<shift)-1) );
            return order;
#endif
        }

        static std::size_t block_size(std::size_t block_order)
        {
            static constexpr uint16_t table[] = {
                                    8, s1(8), s2(8), s3(8),
                                    16, s1(16), s2(16), s3(16),
                                    32, s1(32), s2(32), s3(32),
                                    64, s1(64), s2(64), s3(64),
                                    128, s1(128), s2(128), s3(128),
                                    256, s1(256), s2(256), s3(256),
                                    512, s1(512), s2(512), s3(512),
                                    1<<10, s1(1<<10), s2(1<<10), s3(1<<10),
                                    1<<11, s1(1<<11), s2(1<<11), s3(1<<11),
                                    1<<12, s1(1<<12), s2(1<<12), s3(1<<12),
                                    1<<13, s1(1<<13), s2(1<<13), s3(1<<13),
                                    1<<14, s1(1<<14), s2(1<<14), s3(1<<14),
                                    1<<15, 
                                    };
            return table[block_order];
        }

        static std::size_t ffs64(uint64_t v)
        {
            return std::countr_zero(v);
        }

        void add_free_block(PoolBlock *block, std::size_t order)
        {
            std::size_t idx = order;
            m_free[idx].push(&(block->m_free_node));
            m_free_flags |= (uint64_t(1) << idx);
        }

        void remove_free_block(PoolBlock *block, std::size_t order)
        {
            if (block->m_free_node.remove_self())
            {
                std::size_t idx = order;
                m_free_flags &= ~(uint64_t(1) << idx);
            }
        }

        PoolBlock *break_(PoolBlock *block, std::size_t const block_size, std::size_t align)
        {

            auto old_size = block->size();
            assert(block_size <= old_size);

            void *ptr = (char*)block + old_size - block_size + 2*sizeof(uint32_t);

            void *ptr_align = (void*)((std::ptrdiff_t)ptr & ~(align-1));

            PoolBlock* block2 = (PoolBlock*)((char*)ptr_align - 2*sizeof(uint32_t));

            std::size_t new_size = (char*)block2 - (char*)block;


            if (new_size < sizeof(PoolBlock))
            {
                // use whole block
                remove_free_block(block, block->order());

                auto block_next = next_block(block, old_size);
                auto encode_size = block->m_size & ~PoolBlock::k_free_mask;
                block->m_size = encode_size;
                block_next->m_prev_size =  encode_size;
                return block;
            } else  {
                change_block_size(this, block, new_size, block2);

                auto block2_size = old_size - new_size;
                assert(block2_size >= block_size);

                auto block2_next = next_block(block2, block2_size);
                auto new_ord = block_order(block2_size);
                auto encode_size = PoolBlock::encode_size(block2_size, new_ord, false);
                block2->m_size = encode_size;
                block2_next->m_prev_size = encode_size;

                return block2;
            }

        }

        static void *alloc(PoolImpl *pool, std::size_t size, std::size_t align = alignof(std::max_align_t))
        {


            std::size_t size_ = std::max(size, 2*sizeof(void*));
            align = std::max(2*sizeof(uint32_t), align);

            const std::size_t block_size = 2*sizeof(uint32_t) + size_;

            const std::size_t block_size_ = size_ + align;
            
            if (block_size_ >= PoolImpl::block_size(k_malloc_order))
            {
                return ::aligned_alloc(align, size);
            }


            //             v--- allocated block
            //             [prev_size size][          size_         ]
            //             [           block_size                   ]
            // [    align                 ]
            // [              block_size_                           ]

            auto idx_ = PoolImpl::block_order(block_size_);
            std::size_t idx;


            PoolBlock *block;
            idx = ffs64(pool->m_free_flags & ~((uint64_t(1) << idx_) - 1));

            if (idx != 64)
            {
                FreeNode &node = pool->m_free[idx];
                auto next = node.m_next;
                if (next == &node)
                {
                    // empty
                    assert(next != &node);
                }
                block = PoolBlock::from_free_node(next);
            }
            else
            {
                auto size = PoolImpl::block_size(k_max_order);
                auto head = (PoolBlock *)::malloc(size + 2*sizeof(PoolBlock));
                auto tail = (PoolBlock *)((char*)head + size);
                block = (PoolBlock *)(head + 1);

                block->m_prev_size = PoolBlock::encode_size(0, 0, false);
                block->m_size = PoolBlock::encode_size(size, k_max_order, true);

                tail->m_size = PoolBlock::encode_size(0, 0, false);

                pool->m_chuncks.push(&head->m_free_node);
                pool->add_free_block(block, k_max_order);
            }

            block = pool->break_(block, block_size, align);

            assert(!block->free_());
            return block->mem();
        }

        static PoolBlock *next_block(PoolBlock* block, std::size_t size) {
            return (PoolBlock*)((char*)block + size);
        }

        static PoolBlock *prev_block(PoolBlock* block, std::size_t size) {
            return (PoolBlock*)((char*)block - size);
        }

        static void change_block_size(PoolImpl *pool, PoolBlock *block, std::size_t new_size, PoolBlock *next)
        {
            auto old_ord = block->order();
            std::size_t order = block_order(new_size);
            uint32_t encode_size = PoolBlock::encode_size(new_size, order, true);
            block->m_size = encode_size;
            next->m_prev_size = encode_size;

            if (old_ord != order)
            {
                pool->remove_free_block(block, old_ord);
                pool->add_free_block(block, order);
            }
        }

        static void change_block_size(PoolImpl *pool, PoolBlock *block, std::size_t new_size, PoolBlock *next,
            PoolBlock *new_block)
        {
            auto old_ord = block->order();
            std::size_t order = block_order(new_size);
            uint32_t encode_size = PoolBlock::encode_size(new_size, order, true);
            new_block->m_size = encode_size;
            next->m_prev_size = encode_size;

            pool->remove_free_block(block, old_ord);
            pool->add_free_block(new_block, order);
        }

        static void free(PoolImpl *pool, void *p, std::size_t size, std::size_t align)
        {
            std::size_t size_ = std::max(size, 2*sizeof(void*));
            align = std::max(2*sizeof(uint32_t), align);
            const std::size_t block_size_ = size_ + align;
            if (block_size_ >= block_size(k_malloc_order))
            {
                ::free(p);
                return;
            }
            
            PoolBlock *cur = PoolBlock::from_mem(p);
            auto cur_free = cur->free_();
            assert(!cur_free);

            auto cur_size = cur->size();
            auto prev_free = cur->prev_free();

            PoolBlock *next = next_block(cur, cur_size);
            auto next_free = next->free_();

            if (prev_free && next_free)
            {
                auto next_size = next->size();
                auto prev_size = cur->prev_size();
                
                auto size = prev_size + cur_size + next_size;
                auto prev = prev_block(cur, prev_size);

                pool->remove_free_block(next, next->order());

                auto nn = next_block(next, next_size);
                change_block_size(pool, prev, size, nn);
            }
            else if (prev_free)
            {
                auto prev_size = cur->prev_size();

                auto size = prev_size + cur_size;
                auto prev = prev_block(cur, prev_size);

                change_block_size(pool, prev, size, next);
            }
            else if (next_free)
            {
                auto next_size = next->size();
                auto size = cur_size + next_size;

                auto nn = next_block(next, next_size);
                change_block_size(pool, next, size, nn, cur);       
            }
            else
            {
                auto order = cur->order();
                uint32_t encode_size = cur->m_size | PoolBlock::k_free_mask;

                next->m_prev_size = encode_size;
                cur->m_size = encode_size;
                pool->add_free_block(cur, order);
            }
        }
    };

    class FixPoolResource : public std::pmr::memory_resource
    {
        Pool m_impl;
    public:

        FixPoolResource(size_t block_size) {
            m_impl.initialize(block_size, 100);
        }
        FixPoolResource(FixPoolResource&&) = delete;
        FixPoolResource &operator=(FixPoolResource&&) = delete;

        virtual void *
        do_allocate(size_t __bytes, size_t __alignment) override
        {
            return m_impl.alloc();
        }

        virtual void
        do_deallocate(void *__p, size_t __bytes, size_t __alignment) override
        {
            m_impl.free(__p);
        }

        virtual bool
        do_is_equal(const std::pmr::memory_resource &__other)  const noexcept override
        {
            return this == &__other;
        }
    };

    class PoolResource : public std::pmr::memory_resource
    {
        PoolImpl m_impl;
    public:

        PoolResource() = default;
        PoolResource(PoolResource&&) = delete;
        PoolResource &operator=(PoolResource&&) = delete;

        virtual void *
        do_allocate(size_t __bytes, size_t __alignment) override
        {
            return PoolImpl::alloc(&m_impl, __bytes, __alignment);
        }

        virtual void
        do_deallocate(void *__p, size_t __bytes, size_t __alignment) override
        {
            PoolImpl::free(&m_impl, __p, __bytes, __alignment);
        }

        virtual bool
        do_is_equal(const std::pmr::memory_resource &__other)  const noexcept override
        {
            return this == &__other;
        }
    };

} // namespace tinyasync

#endif
