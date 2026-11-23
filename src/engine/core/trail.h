#pragma once

// Flight recorder for crashes nothing in the process can catch (Switch 2 under its HEN, where a CPU exception closes the title before any
// handler runs). Each thread overwrites a small ring of its last activities in its own file, sdmc:/switch/pt/save/trail/<n>.txt, with plain
// write() calls: the data reaches the file system service (another process) at once, so it is on the card even when the program dies
// without flushing. On after PT_TRAIL=1 (romfs:/test_env.txt); a no-op otherwise and on other platforms.

namespace pt {

// the calling thread's current activity; text must stay valid (a string literal) or is copied (TrailF)
void Trail(const char* text);
void TrailF(const char* format, ...) __attribute__((format(printf, 1, 2)));
bool TrailOn();

}  // namespace pt
