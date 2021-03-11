#include <tinyasync/memory_pool.h>
#include <random>
#include <chrono>
#include <memory_resource>
#include <set>


using namespace tinyasync;



template<class F1, class F2>
void test(char const *title, F1 f1, F2 f2)
{

    std::vector<void*> mem;

    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    auto rng =std::default_random_engine(seed);

    int n = 10000;
    int N = 10000;
    mem.resize(n);

    std::chrono::nanoseconds d0{0};
    std::chrono::nanoseconds d1{0};
    for(int i = 0; i < N; ++i)
    {
        {

            auto t0 = std::chrono::high_resolution_clock::now();
            for(int i = 0; i < n; ++i) {
                void *p = f1(16);
                mem[i] = p;
            }
            auto t1 = std::chrono::high_resolution_clock::now();
            d0 += std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0);
        }

        shuffle(mem.begin(), mem.end(), rng);
        //std::set<void*> s(mem.begin(), mem.end());
        //assert(s.size() == mem.size());
        // for(int i = 0; i < n; ++i) {
        //     auto block = PoolBlock::from_mem(mem[i]);
        //     assert(block->m_mem_node.m_next > (void*)100);
        //     assert(block->m_mem_node.m_prev > (void*)100);
        // }

        {
            auto t0 = std::chrono::high_resolution_clock::now();
            for(int i = n - 1; i >= 0; --i) {
                f2(mem[i]);
            }
            auto t1 = std::chrono::high_resolution_clock::now();
            d1 += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0);
        }

    }

    printf("%s\n", title ? title : "");
    printf("%.2f ns/alloc\n", (double)d0.count()/(N*n));
    printf("%.2f ns/free \n", (double)d1.count()/(N*n));

}


struct Malloc : std::pmr::memory_resource {

    virtual void*
    do_allocate(size_t __bytes, size_t __alignment) {
        return ::malloc(__bytes);
    }

    virtual void
    do_deallocate(void* __p, size_t __bytes, size_t __alignment) {
        ::free(__p);
    }

    virtual bool
    do_is_equal(const std::pmr::memory_resource& __other) const noexcept {
        return true;
    }

};

struct Null : std::pmr::memory_resource {

    virtual void*
    do_allocate(size_t __bytes, size_t __alignment) {
        return nullptr;
    }

    virtual void
    do_deallocate(void* __p, size_t __bytes, size_t __alignment) {
    }

    virtual bool
    do_is_equal(const std::pmr::memory_resource& __other) const noexcept {
        return true;
    }

};


void test_memory_resource(char const *title, std::pmr::memory_resource *mr)
{
    test(title, [&](std::size_t n){  
        return mr->allocate(n);
        },
        [&](void*p) { 
            mr->deallocate(p, 16);
        }
    ); 
}

int main()
{
    
    Null nmr;

    Malloc mmr;
    std::pmr::unsynchronized_pool_resource upr;
    std::pmr::synchronized_pool_resource spr;
    PoolResource pr;

    test_memory_resource("PoolResource", &pr);
    test_memory_resource("Null_resource", &nmr);
    test_memory_resource("malloc_resource", &mmr);
    test_memory_resource("new_delete_resource", std::pmr::new_delete_resource());
    test_memory_resource("unsynchronized_pool_resource", &upr);
    test_memory_resource("synchronized_pool_resource", &spr);

    return 0;


    Pool pool2;
    pool2.initialize(64, 1000);
    test("fixpool", [&](std::size_t n){  
        return pool2.alloc();
        },
        [&](void*p) { 
            pool2.free(p);
        }
    );


    
}
