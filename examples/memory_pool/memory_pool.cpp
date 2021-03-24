#include <tinyasync/memory_pool.h>
#include <random>
#include <chrono>
#include <set>
#include <thread>
#include <algorithm>


using namespace tinyasync;

const size_t test_size = 15;
const size_t align_size = 8;


#if !defined(__clang__)
std::pmr::monotonic_buffer_resource mbr;
#endif

void test_memory_resource(char const *title, std::pmr::memory_resource *mr)
{

    std::vector<void*> mem;

    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    auto rng =std::default_random_engine(seed);

    int n = 100000;
    int m = n/2;
    int N = 100;
    mem.resize(n);


    std::chrono::nanoseconds d0{0};
    std::chrono::nanoseconds d1{0};
    for(int i = 0; i < N; ++i)
    {
        {

            auto t0 = std::chrono::high_resolution_clock::now();
            for(int i = 0; i < m; ++i) {
                void *p = mr->allocate(test_size, align_size);
                mem[i] = p;
            }
            auto t1 = std::chrono::high_resolution_clock::now();
            d0 += std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0);
        }

#if !defined(__clang__)
        if(&mbr == mr) {
            //
        } else
#endif        
        {
            std::shuffle(mem.begin(), mem.end(), rng);
        }
        //std::reverse(mem.begin(), mem.end());


        {
            auto t0 = std::chrono::high_resolution_clock::now();
            for(int i = 0; i < m; ++i)
            {
                if(mem[i]) {
                    mr->deallocate(mem[i], test_size, align_size);
                    mem[i] = nullptr;
                }
            }
#if !defined(__clang__)
            if(&mbr == mr) {
                mbr.release();
            }
#endif        
            auto t1 = std::chrono::high_resolution_clock::now();
            d1 += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0);
        }

    }
    for(int i = 0; i < n; ++i)
    {
        if(mem[i]) {
            mr->deallocate(mem[i], test_size);
            mem[i] = nullptr;
        }
    }

    printf("%s\n", title ? title : "");
    printf("%.2f ns/alloc\n", (double)d0.count()/(N*n));
    printf("%.2f ns/free \n", (double)d1.count()/(N*n));

}


struct Malloc : std::pmr::memory_resource {

    virtual void*
    do_allocate(size_t __bytes, size_t __alignment) {
        //return ::malloc(__bytes);
        return ::aligned_alloc(__alignment, __bytes);
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
        return (void*)1;
    }

    virtual void
    do_deallocate(void* __p, size_t __bytes, size_t __alignment) {
    }

    virtual bool
    do_is_equal(const std::pmr::memory_resource& __other) const noexcept {
        return true;
    }

};

struct Fix : std::pmr::memory_resource {

    Fix() {
        pool2.initialize(test_size, 1000);
    }

    Pool pool2;

    virtual void*
    do_allocate(size_t __bytes, size_t __alignment) {
        return pool2.alloc();
    }

    virtual void
    do_deallocate(void* __p, size_t __bytes, size_t __alignment) {
        pool2.free(__p);
    }

    virtual bool
    do_is_equal(const std::pmr::memory_resource& __other) const noexcept {
        return this == &__other;
    }

};


int main()
{
    
        Malloc mmr;
        Null nmr;
#if !defined(__clang__)
        std::pmr::unsynchronized_pool_resource upr;
        std::pmr::synchronized_pool_resource spr;
#endif        
        PoolResource pr;
        Fix fix;

#if !defined(__clang__)
        test_memory_resource("unsynchronized_pool_resource", &upr);
#endif        
        test_memory_resource("Null_resource", &nmr);
        test_memory_resource("PoolResource", &pr);
        test_memory_resource("malloc_resource", &mmr);
        test_memory_resource("Fix", &fix);
#if !defined(__clang__)
        test_memory_resource("new_delete_resource", std::pmr::new_delete_resource());
        test_memory_resource("synchronized_pool_resource", &spr);
        test_memory_resource("monotonic_buffer_resource", &mbr);
#endif        
        return 0;

    return 0;




    
}
