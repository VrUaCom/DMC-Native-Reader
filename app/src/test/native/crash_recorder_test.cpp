// A fatal signal leaves a note the next start can show: the signal, what
// the shell said it was doing, and a call stack.
#include "dmcresource/crash_recorder.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

#if !defined(_WIN32)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

namespace {

std::string read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

// Dies in a child process the way `die` says and returns the note it left.
template <typename Die>
std::string note_of_crash(const std::string& path, int expected_signal, Die die) {
    std::remove(path.c_str());
    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        dmcresource::crash_recorder::install(path);
        dmcresource::crash_recorder::set_context("opening test.mod\nsecond line");
        die();
        _exit(0);  // not reached
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    // The signal still ends the process: the recorder hands it on.
    assert(WIFSIGNALED(status) && WTERMSIG(status) == expected_signal);
    return read_all(path);
}

}  // namespace

int main() {
    const std::string path = "crash_recorder_test_note.txt";

    const auto aborted = note_of_crash(path, SIGABRT, [] { std::abort(); });
    assert(aborted.rfind("Native crash\nSignal: 6 (SIGABRT", 0) == 0U);
    assert(aborted.find("\nDoing: opening test.mod second line\n") != std::string::npos);
    assert(aborted.find("\nCall stack:\n#00 pc ") != std::string::npos);

    const auto faulted = note_of_crash(path, SIGSEGV, [] {
        volatile int* nowhere = nullptr;
        *nowhere = 1;
    });
    assert(faulted.rfind("Native crash\nSignal: 11 (SIGSEGV", 0) == 0U);
    assert(faulted.find(", address 0x0000000000000000") != std::string::npos);
    assert(faulted.find("\nWhere it stopped:\n#00 pc ") != std::string::npos);

    // Without a context the note says so; an empty path installs nothing.
    std::remove(path.c_str());
    const pid_t child = fork();
    if (child == 0) {
        dmcresource::crash_recorder::install("");
        dmcresource::crash_recorder::install(path);
        dmcresource::crash_recorder::set_context("x");
        dmcresource::crash_recorder::set_context("");
        raise(SIGILL);
        _exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGILL);
    assert(read_all(path).find("\nDoing: (nothing noted)\n") != std::string::npos);
    std::remove(path.c_str());
    return 0;
}

#else

int main() {
    dmcresource::crash_recorder::install("unused");
    dmcresource::crash_recorder::set_context("unused");
    return 0;
}

#endif
