#pragma once

#include <string_view>

// Leaves a note when the process dies of a fatal signal (a bad memory access,
// an abort), so the next start can say what happened and the user can send
// it: the signal, the address, what the shell said it was doing, and the
// call stack as module + offset (what a stripped build can still give).
// The handler writes with async-signal-safe calls only and then hands the
// signal on to whoever handled it before (Android's own crash reporter).
// Without POSIX signals (Windows) every call does nothing.
namespace dmcresource::crash_recorder {

// Records fatal signals of this process into `path` (replaced by each crash).
void install(std::string_view path) noexcept;

// What the shell is doing now, e.g. "opening pl000.pac"; empty clears it.
void set_context(std::string_view text) noexcept;

}  // namespace dmcresource::crash_recorder
