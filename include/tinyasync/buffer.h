#ifndef TINYASYNC_BUFFER_H
#define TINYASYNC_BUFFER_H

#include <cstddef> // std::size_t, std::byte
#include <bit>

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

        Buffer sub_buffer(size_t offset) const
        {
            TINYASYNC_ASSERT(offset <= m_size);
            return {m_data + offset, m_size - offset};
        }

        Buffer sub_buffer(size_t offset, std::size_t cnt) const
        {
            TINYASYNC_ASSERT(offset + cnt <= m_size);
            return {m_data + offset, cnt};
        }

        std::byte *data() const
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


} // namespace tinyasync

#endif
