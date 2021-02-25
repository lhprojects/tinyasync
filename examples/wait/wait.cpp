#define TINYASYNC_TRACE
#include <tinyasync/tinyasync.h>
#include <queue>

using namespace tinyasync;

int max_que_size=2;
IoContext ctx;
Mutex mtx;
ConditionVariable producer(ctx);
ConditionVariable consumer(ctx);

bool producer_done;
Event producer_done_event(ctx);
bool consumer_done;
Event consumer_done_event(ctx);

bool timeout;

std::queue<int> que;

Task<> sche(Name = "sche")
{

    co_await async_sleep(ctx, std::chrono::seconds(10));

    co_await mtx.lock(ctx);
    timeout = true;
    mtx.unlock();

    producer.notify_all();
    consumer.notify_all();

    for(;!producer_done;)
        co_await producer_done_event;
    for(;!consumer_done;)
        co_await consumer_done_event;
    

    ctx.request_abort();

}

Task<> produce(Name = "producer")
{


    for(int i = 0;  ; ++i) {


        //co_await async_sleep(ctx, std::chrono::milliseconds(500));

        co_await mtx.lock(ctx);
        auto mtx_ = auto_unlock(mtx);

        for(;!(que.size() < max_que_size) && !timeout;) {
            co_await consumer.wait(mtx);
        }
        if(timeout) {
            mtx_.unlock();
            break;
        } else {
            printf("%d enqueued\n", i);
            que.push(i);
            mtx_.unlock();
            producer.notify_all();
        }
    }

    co_await mtx.lock(ctx);
    producer_done = true;
    mtx.unlock();
    producer_done_event.notify_all();
    printf("produce done\n");
    
}

Task<> consume(Name = "consumer") {

    for(int i = 0; ; ++i) {

        co_await mtx.lock(ctx);
        auto mtx_ = auto_unlock(mtx);

        for(;!(que.size() > 0) && !timeout;) {
            co_await producer.wait(mtx);
        }

        if(timeout) {
            auto que_ = std::move(que);
            mtx_.unlock();
            
            for(;que.size();) {
                printf("%d dequeued\n", que.back());
                que.pop();
                co_await async_sleep(ctx, std::chrono::seconds(1));
            }

            break;
        } else {
            printf("%d dequeued\n", que.back());
            que.pop();
            mtx_.unlock();

            consumer.notify_all();

            co_await async_sleep(ctx, std::chrono::seconds(1));

        }

    }

    co_await mtx.lock(ctx);
    consumer_done = true;
    mtx.unlock();
    consumer_done_event.notify_all();

    printf("consume done\n");
}

int main() {

    auto t0 = std::chrono::steady_clock::now();
    co_spawn(produce());
    co_spawn(consume());
    co_spawn(sche());
    ctx.run();
    auto t1 = std::chrono::steady_clock::now();

    printf("all   done: %f s\n", std::chrono::duration_cast<std::chrono::duration<double> >(t1-t0).count() );
}

