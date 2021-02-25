
#define TINYASYNC_TRACE
#include <tinyasync/tinyasync.h>

using namespace tinyasync;

Task<> task(IoContext &ctx, Mutex &mutex, int id, int &cnt, Name) {

    printf("task %d locking\n", id);
    co_await mutex.lock(ctx);
    printf("task %d locked\n", id);

    printf("task %d sleeping\n", id);
    co_await async_sleep(ctx, std::chrono::seconds(5));
    printf("task %d sleeping done\n", id);
    mutex.unlock();
    printf("task %d unlocked\n", id);

    cnt--;
    if(cnt == 0) {
        ctx.request_abort();
    }

}

int main() {

    IoContext ctx;
    Mutex mtx;
    int cnt = 2;

    co_spawn(task(ctx, mtx, 0, cnt, "t1"));
    co_spawn(task(ctx, mtx, 1, cnt, "t2"));
    ctx.run();
    printf("all   done\n");
}

