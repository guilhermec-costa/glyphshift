#include <GL/glx.h>
#include <dlfcn.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <thread>

int main() {
    // This executable intentionally links no GL implementation, so the failure
    // case really has no RTLD_NEXT symbol. The fake library supplies it
    // otherwise.
    using Swap = void (*)(Display*, GLXDrawable);
    auto swap = reinterpret_cast<Swap>(dlsym(RTLD_DEFAULT, "glXSwapBuffers"));
    if (!swap) {
        std::cerr << "Missing interposer\n";
        return 1;
    }
    const bool missing = std::getenv("GLYPHSHIFT_TEST_MISSING") != nullptr;
    std::atomic<bool> wrong_errno{false};
    std::array<std::thread, 8> threads;
    for (auto& thread : threads) {
        thread = std::thread([&] {
            for (int i = 0; i < 1000; ++i) {
                errno = EDOM;
                swap(nullptr, 42);
                if (errno != (missing ? EDOM : ERANGE))
                    wrong_errno = true;
            }
        });
    }
    for (auto& thread : threads)
        thread.join();
    auto calls = reinterpret_cast<unsigned (*)()>(dlsym(RTLD_DEFAULT, "fake_calls"));
    auto bad = reinterpret_cast<bool (*)()>(dlsym(RTLD_DEFAULT, "fake_bad_arguments"));
    if (wrong_errno) {
        std::cerr << "Interposer changed errno\n";
        return 1;
    }
    if (missing)
        return calls ? 1 : 0;
    if (!calls || !bad || calls() != 8000 || bad()) {
        std::cerr << "Incorrect forwarding count/arguments\n";
        return 1;
    }
    return 0;
}
