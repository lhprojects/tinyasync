
#include <thread>
#include <deque>
#include <atomic>
#include <thread>
#include <vector>
#include <functional>
#include <tinyasync/tinyasync.h>

using namespace tinyasync;

LockCore lc;

constexpr int N = 1000000;
constexpr int nt = 8;
ListNode b[nt+1][N];

int processed[5];
std::atomic<int> atomic_processed;
SysSpinLock spinLock;
TicketSpinLock ticketSpinLock;
std::mutex stdmtx;

thread_local double work_ = 1 + 1e-14;
__attribute_noinline__ void somework() {
    for(int i = 0; i < 1000; ++i) {
        work_ *= work_;
    }
}

class LC {
public:
    Queue que;
    bool locked;
    SysSpinLock sb;

    bool try_lock(ListNode *x) {
        sb.lock();
        if(locked) {
            que.push(x);
            sb.unlock();
            return false;
        } else {
            locked = true;
            sb.unlock();
            return true;
        }
    }

    ListNode *unlock(bool b) {
        sb.lock();
        
        bool empty;
        auto n = que.pop(empty);
        if(b) {
            if(empty)
                locked = false;
        } else {
            locked = false;
        }

        sb.unlock();

        return n;

    }
};


LC lockspincore;

void test_workonly(int idx) {

    for (int i = 0; i < N; ++i) {
        somework();
    }
}


void test_unlock(int idx)
{

    for (int i = 0; i < N; ++i) {

        ListNode* p = &b[idx][i];
        assert(p->m_next == nullptr);

        for (;;) {
            if (lc.try_lock(p)) {

                processed[1] += 1;

                p = lc.unlock(false);
                somework();
                if (!p) {
                    break;
                } else {
                    p->m_next = nullptr;
                }
                
            } else {
                break;
            }
        }

    }
}

void test_unlock_spinlock(int idx)
{

    for (int i = 0; i < N; ++i) {

        ListNode* p = &b[idx][i];
        assert(p->m_next == nullptr);

        for (;;) {
            if (lockspincore.try_lock(p)) {

                processed[1] += 1;

                p = lockspincore.unlock(false);
                somework();
                if (!p) {
                    break;
                } else {
                    p->m_next = nullptr;
                }
                
            } else {
                break;
            }
        }

    }
}

void test_try_unlock(int idx)
{

    for (int i = 0; i < N; ++i) {

        ListNode* p = &b[idx][i];
        assert(p->m_next == nullptr);

        int  n = 0;
        if (lc.try_lock(p)) {

            processed[0] += 1;
            n += 1;

            for (; lc.unlock(true);) {
                processed[0] += 1;
                n += 1;
            }
            for(int i =0; i <n ; ++i)
                somework();
        }
    }
}


void test_spinlock(int idx)
{

    for (int i = 0; i < N; ++i)
    {

        spinLock.lock();
        processed[2] += 1;
        spinLock.unlock();
        somework();

    }
}

void test_ticketspinlock(int idx)
{

    for (int i = 0; i < N; ++i)
    {

        ticketSpinLock.lock();
        processed[2] += 1;
        ticketSpinLock.unlock();
        somework();

    }
}

void test_mutex(int idx)
{

    for (int i = 0; i < N; ++i)
    {

        stdmtx.lock();
        processed[2] += 1;
        somework();
        stdmtx.unlock();

    }
}

void atomic(int idx)
{
    for (int i = 0; i < N; ++i) {
        ++atomic_processed;
        somework();
    }
}

void test(std::function<void(int)> f, char const *name)
{
    std::vector<std::thread> ts;

    for (int i = 0; i < nt; ++i) {
        ts.emplace_back(std::thread([i, f]() {
            f(i);
            }));

    }

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < nt; ++i) {
        ts[i].join();
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> d = t1 - t0;
    printf("%50s %fs\n", name, d.count());

    assert(lc._count() == 0);

}


int main()
{
    test(test_workonly, "test_workonly");
    test(test_unlock, "test_unlock");
    test(test_try_unlock, "test_try_unlock");
    test(test_unlock_spinlock, "test_unlock_spinlock");
    test(atomic, "atomic");
    test(test_spinlock, "test_spinlock");
    test(test_ticketspinlock, "test_ticketspinlock");
    test(test_mutex, "test_mutex");

    printf("%d\n", atomic_processed.load());
    printf("%d\n", processed[0]);
    printf("%d\n", processed[1]);
    printf("%d\n", processed[2]);
    return 0;
}

  