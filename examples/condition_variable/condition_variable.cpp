//#define TINYASYNC_TRACE
#include <tinyasync/tinyasync.h>
using namespace tinyasync;


Task<> sub_task(IoContext &ctx, Event &task_began, Event &sub_task_finished, int &i, bool &ready_to_start, int &r, bool &done)
{
    // wait only if you need to wait
    for(;!ready_to_start;)
        co_await task_began;    

    r += i*i;    
    
    done = true;
    sub_task_finished.notify_all();
}


Task<> task(IoContext &ctx) {
    Event task_began(ctx);
    Event sub_task_finished1(ctx);
    Event sub_task_finished2(ctx);

    int i = 0, r = 0;
    bool ready_to_start = false;
    bool task_done1 = false, task_done2 = false;

    co_spawn(sub_task(ctx, task_began, sub_task_finished1, i, ready_to_start, r, task_done1));
    co_spawn(sub_task(ctx, task_began, sub_task_finished2, i, ready_to_start, r, task_done2));

    i = 3;
    ready_to_start = true;
    task_began.notify_all();

    // wait only if you need to wait
    for(;!task_done1;)
        co_await sub_task_finished1;

    for(;!task_done2;)
        co_await sub_task_finished2;

    printf("3^2 + 3^2 = %d\n", r);
    ctx.request_abort();
}

int main() {
    IoContext ctx;
    co_spawn(task(ctx));
    ctx.run();    
}

