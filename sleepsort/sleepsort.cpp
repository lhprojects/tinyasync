//#define TINYASYNC_TRACE
#include <tinyasync/tinyasync.h>
#include <span>
#include <stdio.h>
#include <algorithm>

using namespace tinyasync;

int nc = 0;

Task<> sleep_task(IoContext &ctx, int n) {

	co_await async_sleep(ctx, 1000 *1000 * (uint64_t)n);
	printf("%d ", (int)n);
	fflush(stdout);

	if(--nc == 0) {
		ctx.request_abort(); 
		printf("\n");
		fflush(stdout);
	}
}

void sleepsort(std::span<int> ints) {

	TINYASYNC_GUARD("[main thread]: ");

	IoContext ctx;

	nc = (int)ints.size();
	std::vector<int> a;
	for (auto v : ints) {
		co_spawn(sleep_task(ctx, v));
	}
	ctx.run();

}

int main(int argc, char *argv[]) {

	std::vector<int> ints = { 1, 3, 2};
	printf("sorting 1 3 2\n");

	auto min = *std::min_element(ints.begin(), ints.end());
	if (min < 0) {
		printf("negative int is not allowed\n");
		printf("I can shift them to be positive!\n");
		printf("But I won't do that!\n");
		exit(1);
	}

	sleepsort(ints);

}

