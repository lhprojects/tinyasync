
#ifndef TINYASYNC_BASICS_H

#include <tinyasync/basics.h>
#include <tinyasync/task.h>
#include <tinyasync/memory_pool.h>
#endif

#include <chrono>
using namespace tinyasync;


Task<uint64_t> task_generator(uint64_t n)
{
	for (uint64_t i = 0; i < n; ++i) {
		co_yield i;
	}
}

Generator<uint64_t> generator(uint64_t n)
{
	for (uint64_t i = 0; i < n; ++i) {
		co_yield i;
	}
}

struct Iter
{
	uint64_t v;
	uint64_t n;

	TINYASYNC_NOINL Iter(uint64_t n) : n(n)
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

	TINYASYNC_NOINL ~Iter() {

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

TINYASYNC_NOINL uint64_t foo(uint64_t N) {
	uint64_t total = 0;
	Task<uint64_t> task = task_generator(N);
	for (; task.resume(); ) {
		auto x = task.result();
		total += ((total >> 1) + x);
	}
	return total;
}

TINYASYNC_NOINL uint64_t foo2(uint64_t N) {
	uint64_t total = 0;
	Generator<uint64_t> gen = generator(N);
	for (; gen.next();) {
		auto x = gen.get();
		total += ((total >> 1) + x);
	}
	return total;
}
int main(int argc, char *[])
{

	uint64_t nCreate = 10000;
    uint64_t N = 10000;
	N += argc;
	uint64_t d = nCreate;

	StackfulPool<1000> sb;
	tinyasync::set_default_resource(&sb);
	
	if(true) {

		timeit([&]() {  
			uint64_t total = 0;
			for(uint64_t r = 0; r < nCreate; ++r) {
				Task<uint64_t> task = task_generator(N);
				for (; task.resume(); ) {
					auto x = task.result();
					total += ((total >> 1) + x);
				}
			}
			return total;
		}, d, "task");
	}

    timeit([&]() {  
		uint64_t total = 0;
		for(uint64_t r = 0; r < nCreate; ++r) {
			Generator<uint64_t> gen = generator(N);
			for (; gen.next(); ) {
				auto x = gen.get();
				total += ((total >> 1) + x);
			}
		}
		return total;
    }, d, "generator");

    timeit([&]() {  
		uint64_t total = 0;
		for(uint64_t r = 0; r < nCreate; ++r) {
			Generator<uint64_t> gen = generator(N);
			for (auto x: gen) {
				total += ((total >> 1) + x);
			}
		}
		return total;
    }, d, "generator");

    timeit([&]() {  
		uint64_t total = 0;
		for(uint64_t r = 0; r < nCreate; ++r) {
			for (Iter iter(N); !iter.done(); iter.next()) {
				uint64_t x = iter.get();
				total += ((total >> 1) + x);
			}
		}
		return total;
    }, d, "iter(no-inline)");

    timeit([&]() {  
        uint64_t total = 0;
        uint64_t n = N;
		for(uint64_t r = 0; r < nCreate; ++r) {
			for (uint64_t i = 0; i < n; ++i) {
				uint64_t x = i;
				total += ((total >> 1) + x);
			}
		}
        return total;
    }, d, "naive");



	return 0;
}
