#include "dmcresource/crash_recorder.h"

#if defined(_WIN32)

namespace dmcresource::crash_recorder {
void install(std::string_view) noexcept {}
void set_context(std::string_view) noexcept {}
}  // namespace dmcresource::crash_recorder

#else

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <unwind.h>

namespace dmcresource::crash_recorder {
namespace {

constexpr int kSignals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL};
constexpr std::size_t kSignalCount = sizeof(kSignals) / sizeof(kSignals[0]);
constexpr int kMaxFrames = 48;

char g_path[512] = {};
char g_context[256] = {};
struct sigaction g_previous[kSignalCount] = {};
std::atomic<bool> g_installed{false};
volatile sig_atomic_t g_recording = 0;

void put(int fd, const char* text) noexcept {
    std::size_t length = 0;
    while (text[length] != '\0') ++length;
    while (length > 0) {
        const ssize_t written = ::write(fd, text, length);
        if (written <= 0) return;
        text += written;
        length -= static_cast<std::size_t>(written);
    }
}

void put_hex(int fd, std::uintptr_t value) noexcept {
    char text[2 + sizeof(value) * 2 + 1];
    text[0] = '0';
    text[1] = 'x';
    for (std::size_t i = 0; i < sizeof(value) * 2; ++i) {
        const unsigned digit = static_cast<unsigned>((value >> ((sizeof(value) * 2 - 1 - i) * 4)) & 0xFU);
        text[2 + i] = static_cast<char>(digit < 10 ? '0' + digit : 'a' + (digit - 10));
    }
    text[sizeof(text) - 1] = '\0';
    put(fd, text);
}

void put_number(int fd, long value) noexcept {
    char text[24];
    std::size_t at = sizeof(text) - 1;
    text[at] = '\0';
    const bool negative = value < 0;
    unsigned long rest = negative ? 0UL - static_cast<unsigned long>(value) : static_cast<unsigned long>(value);
    do {
        text[--at] = static_cast<char>('0' + rest % 10);
        rest /= 10;
    } while (rest != 0 && at > 1);
    if (negative) text[--at] = '-';
    put(fd, text + at);
}

[[nodiscard]] const char* signal_name(int signal) noexcept {
    switch (signal) {
    case SIGSEGV: return "SIGSEGV, a bad memory access";
    case SIGABRT: return "SIGABRT, the program stopped itself";
    case SIGBUS: return "SIGBUS, a bad memory access";
    case SIGFPE: return "SIGFPE, an arithmetic fault";
    case SIGILL: return "SIGILL, an illegal instruction";
    default: return "a fatal signal";
    }
}

// "#03 pc 0x0000000000123abc libdmcviewer.so (symbol)": the module's file
// name and the offset inside it, which a symbol file turns into a line.
void put_frame(int fd, int index, std::uintptr_t pc) noexcept {
    put(fd, "#");
    if (index < 10) put(fd, "0");
    put_number(fd, index);
    put(fd, " pc ");
    Dl_info module{};
    if (pc != 0 && ::dladdr(reinterpret_cast<void*>(pc), &module) != 0 && module.dli_fname != nullptr) {
        put_hex(fd, pc - reinterpret_cast<std::uintptr_t>(module.dli_fbase));
        put(fd, " ");
        const char* name = module.dli_fname;
        for (const char* c = module.dli_fname; *c != '\0'; ++c) {
            if (*c == '/') name = c + 1;
        }
        put(fd, name);
        if (module.dli_sname != nullptr) {
            put(fd, " (");
            put(fd, module.dli_sname);
            put(fd, ")");
        }
    } else {
        put_hex(fd, pc);
        put(fd, " (no module)");
    }
    put(fd, "\n");
}

struct Frames {
    std::uintptr_t pcs[kMaxFrames];
    int count;
};

_Unwind_Reason_Code on_frame(struct _Unwind_Context* context, void* argument) {
    auto* frames = static_cast<Frames*>(argument);
    if (frames->count >= kMaxFrames) return _URC_END_OF_STACK;
    const auto pc = static_cast<std::uintptr_t>(_Unwind_GetIP(context));
    if (pc != 0) frames->pcs[frames->count++] = pc;
    return _URC_NO_REASON;
}

[[nodiscard]] std::uintptr_t fault_pc(void* raw) noexcept {
    if (raw == nullptr) return 0;
    const auto* context = static_cast<const ucontext_t*>(raw);
#if defined(__aarch64__)
    return static_cast<std::uintptr_t>(context->uc_mcontext.pc);
#elif defined(__x86_64__)
    return static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_RIP]);
#elif defined(__arm__)
    return static_cast<std::uintptr_t>(context->uc_mcontext.arm_pc);
#elif defined(__i386__)
    return static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_EIP]);
#else
    (void)context;
    return 0;
#endif
}

void restore_previous() noexcept {
    for (std::size_t i = 0; i < kSignalCount; ++i) ::sigaction(kSignals[i], &g_previous[i], nullptr);
}

void on_fatal_signal(int signal, siginfo_t* info, void* context) {
    if (g_recording == 0) {
        g_recording = 1;
        const int fd = ::open(g_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd >= 0) {
            put(fd, "Native crash\nSignal: ");
            put_number(fd, signal);
            put(fd, " (");
            put(fd, signal_name(signal));
            put(fd, ")");
            if (info != nullptr) {
                put(fd, ", code ");
                put_number(fd, info->si_code);
                if (signal == SIGSEGV || signal == SIGBUS || signal == SIGFPE || signal == SIGILL) {
                    put(fd, ", address ");
                    put_hex(fd, reinterpret_cast<std::uintptr_t>(info->si_addr));
                }
            }
            put(fd, "\nDoing: ");
            put(fd, g_context[0] != '\0' ? g_context : "(nothing noted)");
            put(fd, "\nWhere it stopped:\n");
            put_frame(fd, 0, fault_pc(context));
            put(fd, "Call stack:\n");
            Frames frames{};
            _Unwind_Backtrace(on_frame, &frames);
            for (int i = 0; i < frames.count; ++i) put_frame(fd, i, frames.pcs[i]);
            ::fsync(fd);
            ::close(fd);
        }
    }
    // Hand the signal on: the handler from before (Android's crash reporter,
    // or the default action) ends the process as it would have.
    restore_previous();
    const bool sent = info != nullptr && info->si_code <= 0;  // raise(), abort(), kill()
    if (sent || signal == SIGABRT) ::raise(signal);
    // A fault of the code itself happens again on return and is now theirs.
}

}  // namespace

void install(std::string_view path) noexcept {
    if (path.empty() || path.size() >= sizeof(g_path)) return;
    std::memcpy(g_path, path.data(), path.size());
    g_path[path.size()] = '\0';
    if (g_installed.exchange(true)) return;  // the handlers stay; only the path changed
    struct sigaction action {};
    action.sa_sigaction = on_fatal_signal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (std::size_t i = 0; i < kSignalCount; ++i) ::sigaction(kSignals[i], &action, &g_previous[i]);
}

void set_context(std::string_view text) noexcept {
    const std::size_t length = text.size() < sizeof(g_context) - 1 ? text.size() : sizeof(g_context) - 1;
    const auto clean = [](char c) { return c == '\n' || c == '\r' ? ' ' : c; };
    // The first character is written last: a crash in the middle of the copy
    // reads an empty text, not half of the old one and half of the new.
    g_context[0] = '\0';
    for (std::size_t i = 1; i < length; ++i) g_context[i] = clean(text[i]);
    g_context[length] = '\0';
    if (length > 0) g_context[0] = clean(text[0]);
}

}  // namespace dmcresource::crash_recorder

#endif
