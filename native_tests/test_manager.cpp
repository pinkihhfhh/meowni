#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include "../app/src/main/cpp/model_manager.h"
using namespace companion;

int main() {
    ManagerConfig cfg; cfg.max_resident = 1; cfg.idle_unload_seconds = 1; cfg.ram_headroom_mb = 1;
    ModelManager m(cfg);
    std::string err;
    ModelSpec a; a.id = "a"; ModelSpec b; b.id = "b";
    ModelSpec huge; huge.id = "huge"; huge.est_ram_mb = 100000000;
    assert(m.registerModel(a, err) && m.registerModel(b, err) && m.registerModel(huge, err));

    // 1. streaming
    std::string out;
    assert(m.generate("a", "hello there world", GenParams(), [&](const std::string& p){ out += p; return true; }, err));
    std::cout << "stream: " << out << "\n"; assert(out == "[a] hello there world ");

    // 2. LRU eviction with max_resident = 1
    assert(m.generate("b", "x", GenParams(), [](const std::string&){ return true; }, err));
    std::string st = m.statusJson(); std::cout << st << "\n";
    assert(st.find("\"resident\":[\"b\"]") != std::string::npos);

    // 3. RAM guard
    assert(!m.load("huge", err)); std::cout << "ram guard: " << err << "\n";
    assert(!m.load("nope", err));

    // 4. cancel from another thread
    std::string got; bool ok = false;
    std::thread t([&]{ ok = m.generate("a", "a b c d e f g h i j k l m n o p q r s t u v w x y z", GenParams(),
                        [&](const std::string& p){ got += p; return true; }, err); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); m.cancel(); t.join();
    std::cout << "cancelled output: " << got << "\n"; assert(ok && got.size() < 60);

    // 5. trim memory frees everything
    m.onTrimMemory(15); assert(m.statusJson().find("\"resident\":[]") != std::string::npos);

    // 6. idle watchdog (idle 1s, check every 5s)
    assert(m.load("a", err)); std::this_thread::sleep_for(std::chrono::seconds(7));
    assert(m.statusJson().find("\"resident\":[]") != std::string::npos);
    std::cout << "ALL OK\n";
}
