#include "editor/psxmon.hh"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>

namespace editor {

namespace psxmon {

uint32_t fletcher(const uint16_t* words, size_t count) {
    uint32_t s1 = 0, s2 = 0;
    for (size_t i = 0; i < count; ++i) {
        s1 += words[i];
        s2 += s1;
    }
    const uint32_t ck = (s2 % 65535) << 16 | (s1 % 65535);
    return ck ? ck : 0xffffffffu;
}

std::vector<uint8_t> encode(uint16_t type, const std::vector<uint16_t>& payload) {
    std::vector<uint16_t> words{type, uint16_t(payload.size())};
    words.insert(words.end(), payload.begin(), payload.end());
    const uint32_t ck = fletcher(words.data(), words.size());
    words.push_back(uint16_t(ck));
    words.push_back(uint16_t(ck >> 16));
    std::vector<uint8_t> out{0x00, 0xaa, 0x55};
    for (uint16_t w : words) {
        out.push_back(uint8_t(w));
        out.push_back(uint8_t(w >> 8));
    }
    return out;
}

}  // namespace psxmon

namespace {

using Clock = std::chrono::steady_clock;
constexpr size_t kLoadChunk = 8192;   // 4 header words + 4096 data words, under the monitor's cap
constexpr size_t kMaxLen = 4096 + 16;  // a longer header is noise, as on the monitor side
constexpr uint32_t kDefaultStack = 0x801ffff0;

uint32_t readLE(const std::vector<uint8_t>& v, size_t at) {
    return uint32_t(v[at]) | uint32_t(v[at + 1]) << 8 | uint32_t(v[at + 2]) << 16 | uint32_t(v[at + 3]) << 24;
}

void push32(std::vector<uint16_t>& w, uint32_t v) {
    w.push_back(uint16_t(v));
    w.push_back(uint16_t(v >> 16));
}

struct Frame {
    uint16_t type = 0;
    std::vector<uint16_t> words;
    bool ok = false;
};

// Splits what the console sends into frames, dropping console text.
struct Reader {
    Link& link;
    std::deque<uint8_t> buf;
    bool closed = false;
    // Read one byte at a time, so nothing past the awaited frame is taken off the link.
    bool exact = false;

    // Next frame of a type in `types`, or false at the deadline. Frames of
    // other types are dropped.
    bool wait(std::initializer_list<uint16_t> types, Clock::time_point deadline, Frame* out) {
        for (;;) {
            while (parse(out)) {
                for (uint16_t t : types)
                    if (out->type == t) return true;
            }
            if (closed) return false;
            const auto now = Clock::now();
            if (now >= deadline) return false;
            uint8_t tmp[512];
            const int ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
            const int n = link.read(tmp, exact ? 1 : sizeof tmp, ms < 1 ? 1 : ms > 50 ? 50 : ms);
            if (n < 0) closed = true;
            for (int i = 0; i < n; ++i) buf.push_back(tmp[i]);
        }
    }

    bool parse(Frame* out) {
        for (;;) {
            while (!buf.empty() && buf.front() != 0) buf.pop_front();  // console text
            if (buf.size() < 7) return false;
            // A run of zeros is one frame start.
            size_t z = 0;
            while (z < buf.size() && buf[z] == 0) ++z;
            if (z > 1) {
                buf.erase(buf.begin(), buf.begin() + std::ptrdiff_t(z - 1));
                continue;
            }
            auto word = [&](size_t at) { return uint16_t(buf[at] | buf[at + 1] << 8); };
            const uint16_t len = word(5);
            if (word(1) != 0x55aa || len > kMaxLen) {
                buf.pop_front();
                continue;
            }
            const size_t total = 7 + 2 * size_t(len) + 4;
            if (buf.size() < total) return false;
            std::vector<uint16_t> all{word(3), len};
            for (size_t i = 0; i < len; ++i) all.push_back(word(7 + 2 * i));
            const uint32_t ck = uint32_t(word(7 + 2 * len)) | uint32_t(word(9 + 2 * len)) << 16;
            out->type = all[0];
            out->words.assign(all.begin() + 2, all.end());
            out->ok = ck == psxmon::fletcher(all.data(), all.size());
            buf.erase(buf.begin(), buf.begin() + std::ptrdiff_t(total));
            return true;
        }
    }
};

bool send(Link& link, uint16_t type, const std::vector<uint16_t>& payload) {
    const std::vector<uint8_t> f = psxmon::encode(type, payload);
    return link.write(f.data(), f.size());
}

// Sends a command and waits for ACK. The monitor answers every command it
// reads, so silence means the frame (or the answer) was lost.
bool command(Reader& r, uint16_t type, const std::vector<uint16_t>& payload, int timeoutMs, const std::string& what,
             std::string* err) {
    if (!send(r.link, type, payload)) {
        *err = "The connection closed.";
        return false;
    }
    Frame f;
    if (!r.wait({psxmon::Ack, psxmon::Error}, Clock::now() + std::chrono::milliseconds(timeoutMs), &f)) {
        *err = r.closed ? "The connection closed." : "psxmon did not answer " + what + ".";
        return false;
    }
    if (f.type == psxmon::Error || !f.ok) {
        *err = "psxmon refused " + what + " (error " + std::to_string(f.words.empty() ? -1 : int(f.words[0])) + ").";
        return false;
    }
    return true;
}

}  // namespace

bool psxmonPresent(Link& link, int timeoutMs) {
    Reader r{link, {}, false, false};
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline && !r.closed) {
        if (!send(link, psxmon::Ping, {})) return false;
        Frame f;
        const auto slice = std::min(deadline, Clock::now() + std::chrono::milliseconds(500));
        if (r.wait({psxmon::Pong}, slice, &f) && f.ok) return true;
    }
    return false;
}

bool psxmonUpload(Link& link, const std::vector<uint8_t>& exe, std::string* err, const UploadProgress& progress,
                  const std::atomic<bool>* cancel) {
    std::string local;
    if (!err) err = &local;
    err->clear();
    if (exe.size() <= 0x800 || std::memcmp(exe.data(), "PS-X EXE", 8) != 0) {
        *err = "Not a PS-X EXE.";
        return false;
    }
    const uint32_t pc = readLE(exe, 0x10), gp = readLE(exe, 0x14), dest = readLE(exe, 0x18);
    uint32_t size = readLE(exe, 0x1c);
    if (!size || size > exe.size() - 0x800) size = uint32_t(exe.size() - 0x800);
    uint32_t sp = readLE(exe, 0x30) + readLE(exe, 0x34);
    if (!sp) sp = kDefaultStack;

    Reader r{link, {}, false, false};
    for (uint32_t at = 0; at < size; at += uint32_t(kLoadChunk)) {
        if (cancel && cancel->load()) {
            *err = "Cancelled.";
            return false;
        }
        const uint32_t n = std::min<uint32_t>(uint32_t(kLoadChunk), size - at);
        std::vector<uint16_t> payload;
        push32(payload, dest + at);
        push32(payload, n);
        const uint8_t* p = exe.data() + 0x800 + at;
        for (uint32_t i = 0; i < n; i += 2) payload.push_back(uint16_t(p[i] | (i + 1 < n ? p[i + 1] << 8 : 0)));
        char where[16];
        std::snprintf(where, sizeof where, "%08x", unsigned(dest + at));
        if (!command(r, psxmon::Load, payload, 5000, std::string("LOAD at ") + where, err)) return false;
        if (progress) progress(int(uint64_t(at + n) * 100 / size));
    }
    std::vector<uint16_t> run;
    push32(run, pc);
    push32(run, gp);
    push32(run, sp);
    // The program may speak right after the ACK: leave its bytes on the link.
    r.exact = true;
    return command(r, psxmon::Run, run, 2000, "RUN", err);
}

}  // namespace editor
