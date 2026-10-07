#include "editor/unirom.hh"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <system_error>

namespace fs = std::filesystem;

namespace editor {

namespace {

constexpr size_t kChunk = 2048;
constexpr int kWaitMs = 5000;  // longest a console is given to answer inside a call

using Clock = std::chrono::steady_clock;

bool sendWord(Link& link, uint32_t v) {
    uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
    return link.write(b, 4);
}

bool sendText(Link& link, const char* s) { return link.write(s, std::strlen(s)); }

uint32_t byteSum(const uint8_t* p, size_t n) {
    uint32_t s = 0;
    for (size_t i = 0; i < n; ++i) s += p[i];
    return s;
}

uint32_t readLE(const std::vector<uint8_t>& v, size_t at) {
    return uint32_t(v[at]) | uint32_t(v[at + 1]) << 8 | uint32_t(v[at + 2]) << 16 | uint32_t(v[at + 3]) << 24;
}

bool cancelled(const std::atomic<bool>* c) { return c && c->load(); }

// Discards whatever the console sends until it has been quiet for `quietMs`.
void drain(Link& link, int quietMs) {
    uint8_t buf[256];
    const auto give_up = Clock::now() + std::chrono::seconds(2);
    while (Clock::now() < give_up && link.read(buf, sizeof buf, quietMs) > 0) {
    }
}

// The upload half: Unirom answers in four-letter words, and may offer a newer
// protocol (OKV2, OKV3) before the answer, which is accepted with UPV2/UPV3.
struct Uploader {
    Link& link;
    int version = 1;
    std::string* err;

    // Waits for `expected`; true when it arrives.
    bool await(const char* expected, int timeoutMs) {
        char tail[4] = {};
        int have = 0;
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        while (Clock::now() < deadline) {
            uint8_t c;
            int n = link.read(&c, 1, 50);
            if (n < 0) {
                *err = "The connection closed.";
                return false;
            }
            if (n == 0) continue;
            std::memmove(tail, tail + 1, 3);
            tail[3] = char(c);
            if (++have < 4) continue;
            if (!std::memcmp(tail, "OKV3", 4) || !std::memcmp(tail, "OKV2", 4)) {
                const bool v3 = tail[3] == '3';
                if (!sendText(link, v3 ? "UPV3" : "UPV2")) break;
                version = std::max(version, v3 ? 3 : 2);
                have = 0;
                continue;
            }
            if (!std::memcmp(tail, "UNSP", 4)) {
                *err = "Unirom refused the command: it is already in debug mode. Reset the console and try again.";
                return false;
            }
            if (!std::memcmp(tail, expected, 4)) return true;
        }
        if (err->empty()) *err = std::string("No answer to the upload (waited for ") + expected + ").";
        return false;
    }

    // Sends `cmd` and waits for OKAY, accepting a newer protocol on the way.
    bool command(const char* cmd) {
        if (!sendText(link, cmd)) {
            *err = "The connection closed.";
            return false;
        }
        if (await("OKAY", 10000)) return true;
        if (err->rfind("No answer", 0) == 0)
            *err = std::string("The console did not answer ") + cmd + ". Is it at the Unirom shell?";
        return false;
    }

    uint32_t checksum(const std::vector<uint8_t>& data) const {
        if (version == 3) {
            uint32_t h = 5381;
            for (uint8_t b : data) h = ((h << 5) + h) ^ b;
            return h;
        }
        return byteSum(data.data(), data.size());
    }

    // Sends `data` in chunks; from V2 on each chunk is acknowledged, and resent on ERR!.
    bool chunks(const std::vector<uint8_t>& data, const UploadProgress& progress, const std::atomic<bool>* cancel) {
        for (size_t at = 0; at < data.size();) {
            if (cancelled(cancel)) {
                *err = "Cancelled.";
                return false;
            }
            const size_t n = std::min(kChunk, data.size() - at);
            if (!link.write(data.data() + at, n)) {
                *err = "The connection closed.";
                return false;
            }
            if (version >= 2) {
                if (!await("CHEK", kWaitMs)) return false;
                if (!sendWord(link, byteSum(data.data() + at, n))) {
                    *err = "The connection closed.";
                    return false;
                }
                // MORE moves on, ERR! asks for the same chunk again.
                char tail[4] = {};
                int have = 0;
                bool more = false, again = false;
                const auto deadline = Clock::now() + std::chrono::milliseconds(kWaitMs);
                while (!more && !again && Clock::now() < deadline) {
                    uint8_t c;
                    int r = link.read(&c, 1, 50);
                    if (r < 0) {
                        *err = "The connection closed.";
                        return false;
                    }
                    if (r == 0) continue;
                    std::memmove(tail, tail + 1, 3);
                    tail[3] = char(c);
                    if (++have < 4) continue;
                    more = !std::memcmp(tail, "MORE", 4);
                    again = !std::memcmp(tail, "ERR!", 4);
                }
                if (!more && !again) {
                    *err = "The console stopped acknowledging the upload.";
                    return false;
                }
                if (again) continue;
            }
            at += n;
            if (progress) progress(int(at * 100 / data.size()));
        }
        return true;
    }
};

}  // namespace

bool uniromUpload(Link& link, const std::vector<uint8_t>& exe, std::string* err, const UploadProgress& progress,
                  const std::atomic<bool>* cancel) {
    std::string local;
    if (!err) err = &local;
    err->clear();
    if (exe.size() <= 0x800 || std::memcmp(exe.data(), "PS-X EXE", 8) != 0) {
        *err = "Not a PS-X EXE.";
        return false;
    }
    const uint32_t entry = readLE(exe, 0x10), dest = readLE(exe, 0x18);
    std::vector<uint8_t> body(exe.begin() + 0x800, exe.end());
    body.resize((body.size() + kChunk - 1) / kChunk * kChunk, 0);

    Uploader up{link, 1, err};
    drain(link, 100);
    if (cancelled(cancel)) {
        *err = "Cancelled.";
        return false;
    }
    // SEXE: the 2048-byte header, then where to jump and load, the size and
    // checksum of what follows, then the program. It starts on the last chunk.
    if (!up.command("SEXE")) return false;
    drain(link, 200);
    const uint32_t sum = up.checksum(body);
    if (!link.write(exe.data(), 0x800) || !sendWord(link, entry) || !sendWord(link, dest) ||
        !sendWord(link, uint32_t(body.size())) || !sendWord(link, sum)) {
        *err = "The connection closed.";
        return false;
    }
    if (!up.chunks(body, progress, cancel)) return false;
    return true;
}

// ---- PCdrv host

namespace {

constexpr uint32_t kInit = 0x101, kOpen = 0x103, kClose = 0x104, kRead = 0x105, kSeek = 0x107;
constexpr uint32_t kMaxTransfer = 64u << 20;

// Exact reads over the link, with a timeout per wait.
struct In {
    Link& link;
    std::string* err;
    bool bytes(uint8_t* p, size_t n) {
        while (n) {
            int r = link.read(p, n, kWaitMs);
            if (r < 0) {
                *err = "The connection closed.";
                return false;
            }
            if (r == 0) {
                *err = "The console stopped in the middle of a file call.";
                return false;
            }
            p += r;
            n -= size_t(r);
        }
        return true;
    }
    bool u32(uint32_t* v) {
        uint8_t b[4];
        if (!bytes(b, 4)) return false;
        *v = uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
        return true;
    }
};

}  // namespace

// A refusal is one byte. psxsplash compares O, K, A, Y one byte at a time and
// stops at the first mismatch, so a longer word would leave the rest of it in
// the console's receive buffer, to be read as the answer to its next call.
static bool refuse(Link& link) { return link.write("N", 1); }

PcdrvHost::PcdrvHost(fs::path base) : base_(std::move(base)) {}

PcdrvHost::~PcdrvHost() {
    for (auto& [h, f] : files_) std::fclose(f);
}

fs::path PcdrvHost::resolve(const std::string& name, bool* ok) const {
    std::string n = name;
    std::replace(n.begin(), n.end(), '\\', '/');
    while (!n.empty() && n.front() == '/') n.erase(n.begin());
    // libsn-style device prefixes ("host:", "pcdrv:") name the PC itself.
    if (size_t colon = n.find(':'); colon != std::string::npos && n.find('/') > colon) n = n.substr(colon + 1);
    fs::path rel = fs::path(std::u8string(n.begin(), n.end())).lexically_normal();
    *ok = !n.empty() && !rel.empty() && rel.is_relative() && *rel.begin() != "..";
    return base_ / rel;
}

bool PcdrvHost::serve(Link& link, const std::atomic<bool>& cancel, const std::function<void(const std::string&)>& line,
                      const std::function<void(const std::string&)>& event, std::string* err) {
    std::string local;
    if (!err) err = &local;
    err->clear();
    std::string text;
    bool escape = false;
    uint8_t buf[512];
    while (!cancel.load()) {
        int n = link.read(buf, sizeof buf, 50);
        if (n < 0) {
            if (!text.empty() && line) line(text);
            *err = "The connection closed.";
            return false;
        }
        if (n == 0) {
            if (!text.empty()) {
                if (line) line(text);
                text.clear();
            }
            continue;
        }
        for (int i = 0; i < n; ++i) {
            const uint8_t c = buf[i];
            if (escape) {
                escape = false;
                if (c != 'p') continue;
                if (!text.empty()) {
                    if (line) line(text);
                    text.clear();
                }
                // The call reads the link itself; give it the bytes already read.
                pending_.assign(buf + i + 1, buf + n);
                if (!handleCall(link, event, err)) return false;
                // Whatever followed the call in this buffer was consumed through pending_.
                std::vector<uint8_t> rest = std::move(pending_);
                pending_.clear();
                // Re-run the loop body on the leftovers.
                n = int(std::min(rest.size(), sizeof buf));
                std::copy(rest.begin(), rest.begin() + n, buf);
                i = -1;
                continue;
            }
            if (c == 0) {
                escape = true;
            } else if (c == '\n' || c == '\r') {
                if (!text.empty()) {
                    if (line) line(text);
                    text.clear();
                }
            } else if (c >= 0x20 && c < 0x7f) {
                text.push_back(char(c));
                if (text.size() >= 200) {
                    if (line) line(text);
                    text.clear();
                }
            }
        }
    }
    if (!text.empty() && line) line(text);
    return true;
}

namespace {

// A Link that first hands out bytes already read off the real one.
class Prefixed : public Link {
  public:
    Prefixed(Link& inner, std::vector<uint8_t>& prefix) : inner_(inner), prefix_(prefix) {}
    bool write(const void* d, size_t n) override { return inner_.write(d, n); }
    int read(void* d, size_t n, int timeoutMs) override {
        if (!prefix_.empty()) {
            size_t k = std::min(n, prefix_.size());
            std::memcpy(d, prefix_.data(), k);
            prefix_.erase(prefix_.begin(), prefix_.begin() + k);
            return int(k);
        }
        return inner_.read(d, n, timeoutMs);
    }

  private:
    Link& inner_;
    std::vector<uint8_t>& prefix_;
};

}  // namespace

bool PcdrvHost::handleCall(Link& raw, const std::function<void(const std::string&)>& event, std::string* err) {
    Prefixed link(raw, pending_);
    In in{link, err};
    auto report = [&](const std::string& s) {
        if (event) event(s);
    };
    auto closed = [&] {
        *err = "The connection closed.";
        return false;
    };
    uint32_t op;
    if (!in.u32(&op)) return false;
    // Presence: the console gives up on the call unless the host says OKAY.
    if (!sendText(link, "OKAY")) return closed();

    if (op == kInit) {
        report("init");
        const uint8_t zero = 0;
        return link.write(&zero, 1) || closed();
    }

    if (op == kOpen) {
        std::string name;
        for (;;) {
            uint8_t c;
            if (!in.bytes(&c, 1)) return false;
            if (!c) break;
            if (name.size() < 1024) name.push_back(char(c));
        }
        uint32_t flags;
        if (!in.u32(&flags)) return false;
        bool ok;
        const fs::path path = resolve(name, &ok);
        std::FILE* f = nullptr;
#ifdef _WIN32
        if (ok) f = _wfopen(path.c_str(), L"rb");
#else
        if (ok) f = std::fopen(path.c_str(), "rb");
#endif
        if (!f) {
            report("open " + name + ": " + (ok ? "not found" : "outside the play folder"));
            return refuse(link) || closed();
        }
        const uint32_t h = nextHandle_++;
        files_[h] = f;
        report("open " + name + " -> " + std::to_string(h));
        return (sendText(link, "OKAY") && sendWord(link, h)) || closed();
    }

    uint32_t a1, a2, a3;
    if (!in.u32(&a1) || !in.u32(&a2) || !in.u32(&a3)) return false;
    auto it = files_.find(a1);
    std::FILE* f = it == files_.end() ? nullptr : it->second;
    const std::string h = std::to_string(a1);

    switch (op) {
        case kClose:
            if (f) {
                std::fclose(f);
                files_.erase(it);
            }
            report("close " + h);
            return (sendText(link, "OKAY") && sendWord(link, a1)) || closed();

        case kSeek: {
            const int whence = a3 == 1 ? SEEK_CUR : a3 == 2 ? SEEK_END : SEEK_SET;
            if (!f || std::fseek(f, long(int32_t(a2)), whence) != 0) {
                report("seek " + h + ": failed");
                return refuse(link) || closed();
            }
            const long pos = std::ftell(f);
            report("seek " + h + " -> " + std::to_string(pos));
            return (sendText(link, "OKAY") && sendWord(link, uint32_t(pos))) || closed();
        }

        case kRead: {
            if (!f || a2 > kMaxTransfer) {
                report("read " + h + ": refused");
                return refuse(link) || closed();
            }
            // The console reads exactly the count it is told, so the count is what the file had.
            std::vector<uint8_t> data(a2);
            data.resize(std::fread(data.data(), 1, a2, f));
            if (!sendText(link, "OKAY") || !sendWord(link, uint32_t(data.size())) ||
                !sendWord(link, byteSum(data.data(), data.size())) || !link.write(data.data(), data.size()))
                return closed();
            report("read " + h + ": " + std::to_string(data.size()) + " of " + std::to_string(a2) + " bytes");
            return true;
        }

        default:
            report("unsupported file call 0x" + [&] {
                char b[16];
                std::snprintf(b, sizeof b, "%x", unsigned(op));
                return std::string(b);
            }());
            return refuse(link) || closed();
    }
}

}  // namespace editor
