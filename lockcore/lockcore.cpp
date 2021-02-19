
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

int processed[2];
std::atomic<int> atomic_processed;

void test_unlock(int idx) {

    for (int i = 0; i < N; ++i) {

        ListNode* p = &b[idx][i];
        assert(p->m_next == nullptr);

        for (;;) {
            if (lc.try_lock(p)) {

                processed[1] += 1;

                p = lc.unlock(false);
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

        if (lc.try_lock(p)) {

            processed[0] += 1;

            for (; lc.unlock(true);) {
                processed[0] += 1;
            }
        }
    }
}

void atomic(int idx)
{
    for (int i = 0; i < N; ++i) {
        ++atomic_processed;
    }
}


void test(std::function<void(int)> f)
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
    printf("%fs\n", d.count());

    assert(lc._count() == 0);

}


int main()
{
    test(test_unlock);
    test(atomic);
    test(test_try_unlock);

    printf("%d\n", atomic_processed.load());
    printf("%d\n", processed[0]);
    printf("%d\n", processed[1]);
    return 0;
}

  