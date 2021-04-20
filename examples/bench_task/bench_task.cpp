
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
		co_return 1;
	} else if(n == 1) {
		co_return 1;
	}
	auto c1 =  co_await task(n - 1);
	auto c2 =  co_await task(n - 2);
	co_return c1 + c2;
}

Task<uint64_t> task(StackfulPool &sp, uint64_t n)
{
	if(n == 0) {
		co_return 1;
	} else if(n == 1) {
		co_return 1;
	}  	
	auto c1 =  co_await task(sp, n - 1);
	auto c2 =  co_await task(sp, n - 2);
	co_return c1 + c2;

}

uint64_t n_call(uint64_t n)
{
	if(n == 0) {
		return 1;
	} else if(n == 1) {
		return 1;
	}
	return n_call(n - 1) + n_call(n - 2) + 1;
}

uint64_t func(uint64_t n)
{
	if(n == 0) {
		return 1;
	} else if(n == 1) {
		return 1;
	}
	return func(n - 1) + func(n - 2);
}

struct VC {
	virtual uint64_t func(uint64_t n);
	virtual uint64_t func_stackful(StackfulPool &sp, uint64_t n) { return 0; }
	virtual ~VC();
};

struct VC2 : VC {
	uint64_t func(uint64_t n) override;
	uint64_t func_stackful(StackfulPool &sp, uint64_t n) override;
};

TINYASYNC_NOINL VC *get_virtual_class() {
	return new VC;
}

TINYASYNC_NOINL VC *get_virtual_class2() {
	return new VC2;
}

TINYASYNC_NOINL VC2 *get_virtual_class2(StackfulPool &sp) {
	return new(sp.allocate(sizeof(VC2))) VC2();
}

void free_virtual_class2(StackfulPool &sp, VC2 *p) {
	p->~VC2();
	sp.deallocate(p, sizeof(VC2));
}

TINYASYNC_NOINL uint64_t VC::func(uint64_t n) {
	
	if(n == 0) {
		return 1;
	} else if(n == 1) {
		return 1;
	}
	return func(n - 1) + func(n - 2);
}

using FuncP = uint64_t(*)(void *, uint64_t);

TINYASYNC_NOINL uint64_t func_p(void *p, uint64_t n) {
	if(n == 0) {
		return 1;
	} else if(n == 1) {
		return 1;
	}
	auto f = (FuncP)p;
	return f(p, n - 1) + f(p, n - 2);

}

TINYASYNC_NOINL FuncP get_func_p() {
	return func_p;
}

TINYASYNC_NOINL uint64_t VC2::func(uint64_t n) {
	if(n == 0) {
		return 1;
	} else if(n == 1) {
		return 1;
	}
	auto p1 = get_virtual_class2();
	auto c1 = p1->func(n - 1);
	delete p1;

	auto p2 = get_virtual_class2();
	auto c2 = p2->func(n - 2);
	delete p2;

	return c1 + c2;
}

TINYASYNC_NOINL uint64_t VC2::func_stackful(StackfulPool &sp, uint64_t n) {
	if(n == 0) {
		return 1;
	} else if(n == 1) {
		return 1;
	}
	auto p1 = get_virtual_class2(sp);
	auto c1 = p1->func_stackful(sp, n - 1);
	free_virtual_class2(sp, p1);

	auto p2 = get_virtual_class2(sp);
	auto c2 = p2->func_stackful(sp, n - 2);
	free_virtual_class2(sp, p2);

	return c1 + c2;
}

TINYASYNC_NOINL VC::~VC() {
}


template<class T>
void timeit(T t, uint64_t r, uint64_t n, char const *title)
{

    auto t0 = std::chrono::high_resolution_clock::now();
    volatile auto total = t();
    auto t1 = std::chrono::high_resolution_clock::now();
    auto d = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

    printf("%30s: %f ns/call\n", title, double(1. * d / n_call(n) / r));

}

#include <csetjmp>

__attribute__((aligned(4096)))
int main(int argc, char *[])
{

	std::jmp_buf a;
	std::size_t nCreate = 1000;
	std::size_t N = 20;

    timeit([&]() {  
		uint64_t total = 0;
		for(uint64_t r = 0; r < nCreate; ++r) {
			Task<uint64_t> gen = task(N);
			gen.resume();
		}
		return total;
    }, nCreate, N, "task");

    timeit([&]() {  
		StackfulPool sp(N*200);
		uint64_t total = 0;
		for(uint64_t r = 0; r < nCreate; ++r) {
			Task<uint64_t> gen = task(sp, N);
			gen.resume();
		}
		return total;
    }, nCreate, N, "task(stackful pool)");

    timeit([&]() { 
		VC vc;
		uint64_t total = 0;
		for(uint64_t r = 0; r < nCreate; ++r) {
			auto p = get_virtual_class();
			total += p->func(N);
			delete p;
		}
		return total;
    }, nCreate, N, "func(virtual)");

    timeit([&]() { 
		uint64_t total = 0;
		for(uint64_t r = 0; r < nCreate; ++r) {
			auto p = get_virtual_class2();
			total += p->func(N);
			delete p;
		}
		return total;
    }, nCreate, N, "func(virtual,new)");

    timeit([&]() { 
		StackfulPool sp(N*200);
		uint64_t total = 0;
		for(uint64_t r = 0; r < nCreate; ++r) {
			auto p = get_virtual_class2(sp);
			total += p->func_stackful(sp, N);
			free_virtual_class2(sp, p);
		}
		return total;
    }, nCreate, N, "func(virtual,stackful)");

    timeit([&]() {  
		uint64_t total = 0;
		for(uint64_t r = 0; r < nCreate; ++r) {
			auto p = get_func_p();
			total += p((void*)p, N);
		}
		return total;
    }, nCreate, N, "func pointer");

    timeit([&]() {  
		uint64_t total = 0;
		for(uint64_t r = 0; r < nCreate; ++r) {
			total += func(N);
		}
		return total;
    }, nCreate, N, "func");

	sqrt(1);

	return 0;
}
