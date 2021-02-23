#ifndef TINYASYNC_BASICS_H
#include <tinyasync/tinyasync.h>
#endif

#include <chrono>
using namespace tinyasync;
Task<> task_generator(uint64_t n, uint64_t &x)
{
	for (uint64_t i = 0; i < n; ++i) {
		x = i;
		co_await std::suspend_always{};
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
	TINYASYNC_NOINL Iter &operator++()
	{
		v += 1;
		return *this;
	}

	uint64_t operator*()
	{
		return v;
	}
	bool operator!=(int)
	{
		return v != n;
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
        uint64_t x;
        Task<> task = task_generator(N, x);
        for (; task.resume(); ) {
            total += ((x << 1) + x%2);
        }
        return total;
    }, N, "task");

    timeit([&]() {  
        uint64_t total = 0;
		for (Iter iter(N); iter != 0; ++iter) {
            uint64_t x = *iter;
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
