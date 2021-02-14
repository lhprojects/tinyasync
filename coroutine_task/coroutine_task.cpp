#define TINYASYNC_TRACE

#include <tinyasync/tinyasync.h>
using namespace tinyasync;

std::coroutine_handle<> suspended_coroutine;
int data_;

Task<int> task(Name = "task") {

    suspended_coroutine = co_await this_coroutine<>();
    co_await std::suspend_always{};
    printf("recv %d\n", data_);

    suspended_coroutine = co_await this_coroutine<>();
    co_await std::suspend_always{};
    printf("recv %d\n", data_);

	co_return 2;
}

Task<> spawn_task(Name = "spawn_task") {
	int v = co_await task();
	printf("task return %d\n", v);
}


int main() {

	co_spawn(spawn_task());

	printf("io 1 waiting\n");
	sync_sleep(std::chrono::seconds(1));
	data_ = 1;
	printf("io 1 finished\n");
	suspended_coroutine.resume();

	printf("io 2 waiting\n");
	sync_sleep(std::chrono::seconds(1));
    data_ = 2;
	printf("io 2 finished\n");
	suspended_coroutine.resume();

	return 0;

}
