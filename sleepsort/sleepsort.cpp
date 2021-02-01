#include <tinyasync/tinyasync.h>
#include <array>
#include <initializer_list>
#include <span>
#include <stdio.h>
#include <unordered_map>
#include <algorithm>

using namespace tinyasync;

int nc = 0;

Task sleep_task(IoContext &ctx, int n) {
	co_await async_sleep(ctx, 1000 *1000 * (uint64_t)n);

	printf("%d ", (int)n);
	fflush(stdout);

	if(--nc == 0) {
		ctx.abort(); 
		printf("\n");
		fflush(stdout);
	}
}

void sleepsort(std::vector<int> ints) {

	IoContext ctx;

	nc = (int)ints.size();
	std::vector<int> a;
	for (auto v : ints) {
		co_spawn(sleep_task(ctx, v));
	}
	ctx.run();

}

int main(int argc, char *argv[]) {

	std::vector<int> ints;
	for(int i = 1; i < argc; ++i) {
		ints.push_back( atoi(argv[i]) );
	}

	if(ints.size() == 0) {
		return 0;
	}

	auto min = *std::min_element(ints.begin(), ints.end());
	if (min < 0) {
		printf("negative int is not allowed\n");
		printf("I can shift them to be positive!\n");
		printf("But I won't do that!\n");
		exit(1);
	}

	sleepsort(ints);

}

