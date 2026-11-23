#include "engine/core/trail.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef __SWITCH2__
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace pt {

#ifdef __SWITCH2__
namespace {

constexpr int kLine = 120;  // bytes per record, newline included
constexpr int kRing = 16;   // records kept per thread

bool Enabled() {
    static const bool on = [] {
        const char* v = std::getenv("PT_TRAIL");
        if (!v || std::atoi(v) == 0) return false;
        mkdir("sdmc:/switch/pt/save/trail", 0777);
        return true;
    }();
    return on;
}

double Seconds() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

struct ThreadTrail {
    int fd = -1;
    int slot = -1;
    unsigned sequence = 0;
};

void Write(const char* text) {
    thread_local ThreadTrail t;
    static std::atomic<int> next_slot{0};
    if (t.slot < 0) {
        t.slot = next_slot.fetch_add(1);
        char path[64];
        std::snprintf(path, sizeof(path), "sdmc:/switch/pt/save/trail/%02d.txt", t.slot);
        t.fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    }
    if (t.fd < 0) return;
    char line[kLine];
    int n = std::snprintf(line, sizeof(line), "%08u [%9.3f] %s", t.sequence, Seconds(), text);
    if (n < 0) return;
    if (n > kLine - 1) n = kLine - 1;
    std::memset(line + n, ' ', static_cast<size_t>(kLine - 1 - n));
    line[kLine - 1] = '\n';
    lseek(t.fd, static_cast<off_t>(t.sequence % kRing) * kLine, SEEK_SET);
    (void)write(t.fd, line, kLine);
    ++t.sequence;
}

}  // namespace

bool TrailOn() { return Enabled(); }

void Trail(const char* text) {
    if (Enabled()) Write(text);
}

void TrailF(const char* format, ...) {
    if (!Enabled()) return;
    char text[kLine];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    Write(text);
}
#else
bool TrailOn() { return false; }
void Trail(const char*) {}
void TrailF(const char*, ...) {}
#endif

}  // namespace pt
