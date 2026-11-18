#include <doctest/doctest.h>
#include <rk/rt_swap.hpp>

#include <atomic>
#include <thread>

using namespace rk;

namespace {
std::atomic<int> g_alive{0};
struct Obj {
    int value;
    explicit Obj(int v) : value(v) { ++g_alive; }
    ~Obj() { --g_alive; }
};
} // namespace

TEST_CASE("RtSwap: hand over, retire and collect") {
    {
        RtSwap<Obj> s;
        CHECK(s.current() == nullptr);
        s.publish(std::make_unique<Obj>(1));
        CHECK(s.hasPending());
        CHECK(s.acquire());
        CHECK(s.current()->value == 1);
        s.publish(std::make_unique<Obj>(2));
        s.publish(std::make_unique<Obj>(3)); // replaces 2 before the audio thread saw it
        CHECK(s.acquire());
        CHECK(s.current()->value == 3);
        s.publish(std::make_unique<Obj>(4)); // collects the retired 1
        CHECK(g_alive.load() == 2);
        CHECK(s.acquire());
        // retired slot (3) is full now: the next handover must wait for collectGarbage
        s.publish(std::make_unique<Obj>(5));
        s.collectGarbage();
        CHECK(s.acquire());
        CHECK(s.current()->value == 5);
    }
    CHECK(g_alive.load() == 0);
}

TEST_CASE("RtSwap: concurrent producer and consumer (run with TSan)") {
    {
        RtSwap<Obj> s;
        std::atomic<bool> stop{false};
        std::thread audio([&] {
            int last = -1;
            while (!stop.load()) {
                s.acquire();
                if (Obj* o = s.current()) {
                    CHECK(o->value >= last); // values only ever increase
                    last = o->value;
                }
            }
        });
        for (int i = 0; i < 20000; ++i) {
            s.publish(std::make_unique<Obj>(i));
            if (i % 3 == 0) s.collectGarbage();
        }
        stop = true;
        audio.join();
    }
    CHECK(g_alive.load() == 0);
}
