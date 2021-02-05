#define TINYASYNC_TRACE

#include <tinyasync/tinyasync.h>
using namespace tinyasync;

std::coroutine_handle<> suspended_coroutine;
int data_;

Task task(Name = "task") {

    suspended_coroutine = co_await this_coroutine<>();
    co_await std::suspend_always{};
    printf("recv %d\n", data_);

    suspended_coroutine = co_await this_coroutine<>();
    co_await std::suspend_always{};
    printf("recv %d\n", data_);
}

Task spawn_task(Name = "spawn_task") {
    co_await task();
}


int main() {

	co_spawn(spawn_task());

	printf("io 1 waiting\n");
	sleep_seconds(1);
	data_ = 1;
	printf("io 1 finished\n");
	suspended_coroutine.resume();

	printf("io 2 waiting\n");
	sleep_seconds(1);
    data_ = 2;
	printf("io 2 finished\n");
	suspended_coroutine.resume();

	return 0;

}
