#ifndef TINYASYNC_BASICS_H
#include <tinyasync/tinyasync.h>
#endif

#include <chrono>
using namespace tinyasync;
Task<uint64_t> task_generator(uint64_t n)
{
	for (uint64_t i = 0; i < n; ++i) {
		co_yield i;
	}
}

struct Iter
{
	uint64_t v;
	uint64_t n;

	Iter(uint64_t n) : n(n)
	{

		v = 0;
	}
	TINYASYNC_NOINL void next()
	{
		v += 1;
	}

	uint64_t get()
	{
		return v;
	}

	bool done()
	{
		return v == n;
	}

};

template<class T>
void timeit(T t, uint64_t n, char const *title)
{

    auto t0 = std::chrono::high_resolution_clock::now();
    volatile auto total = t();
    auto t1 = std::chrono::high_resolution_clock::now();
    auto d = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

    printf("%30s: %f ns/iteration\n", title, double(1. * d/ n));

}
int main(int argc, char *[])
{

    uint64_t N = 100000000;
	N += argc;

    timeit([&]() {  
        uint64_t total = 0;
        Task<uint64_t> task = task_generator(N);
        for (; task.resume(); ) {
			auto x = task.result();
            total += ((x << 1) + x%2);
        }
        return total;
    }, N, "task");

    timeit([&]() {  
        uint64_t total = 0;
		for (Iter iter(N); !iter.done(); iter.next()) {
            uint64_t x = iter.get();
            total += ((x << 1) + x%2);
		}
        return total;
    }, N, "iter");

    timeit([&]() {  
        uint64_t total = 0;
        uint64_t n = N;
		for (uint64_t i = 0; i < n; ++i) {
            uint64_t x = i;
            total += ((x << 1) + x%2);
		}
        return total;
    }, N, "naive");



	return 0;
}
