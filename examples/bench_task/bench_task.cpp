
#ifndef TINYASYNC_BASICS_H

#include <tinyasync/basics.h>
#include <tinyasync/task.h>
#include <tinyasync/memory_pool.h>
#endif

#include <chrono>
using namespace tinyasync;


Task<uint64_t> task(uint64_t n)
{
	if(n == 0) {
		co_return 0;
	}
	co_return (co_await task(n - 1)) + 1;
}

TINYASYNC_NOINL uint64_t func(uint64_t n)
{
	if(n == 0) {
		return 0;
	}
	return func(n - 1) + 1;
}

template<class T>
void timeit(T t, uint64_t n, char const *title)
{

    auto t0 = std::chrono::high_resolution_clock::now();
    volatile auto total = t();
    auto t1 = std::chrono::high_resolution_clock::now();
    auto d = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

    printf("%30s: %f ns/iteration\n", title, double(1. * d/ n));

}

__attribute__((aligned(4096)))
int main(int argc, char *[])
{

	std::size_t nCreate = 10000;
	std::size_t N = 100;
	auto d = nCreate * N;

    timeit([&]() {  
		uint64_t total = 0;
		for(uint64_t r = 0; r < nCreate; ++r) {
			Task<uint64_t> gen = task(N);
			gen.resume();
		}
		return total;
    }, d, "generator");

    timeit([&]() {  
		uint64_t total = 0;
		for(uint64_t r = 0; r < nCreate; ++r) {
			total += func(N);
		}
		return total;
    }, d, "func(no-inline)");



	return 0;
}
