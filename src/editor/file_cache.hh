// Readings of files kept while the files stay as they were. The Project
// settings tab looks at its music tracks and movies every second, so an edit
// made outside shows up; reading them again each time (a whole WAV loaded, a
// whole MP3 decoded for its length) stalled the editor's frame, and with it
// the pad input Play gets (#369). A stat a second is all an unchanged file
// costs now.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <system_error>
#include <utility>

namespace editor {

// What a file looks like from outside: there or not, its size and its
// modification time. A reading of the file holds while this does.
struct FileStamp {
    bool exists = false;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type mtime{};
    bool operator==(const FileStamp&) const = default;
};

inline FileStamp fileStamp(const std::filesystem::path& p) {
    std::error_code ec;
    FileStamp s;
    if (!std::filesystem::is_regular_file(p, ec)) return s;
    s.size = std::filesystem::file_size(p, ec);
    if (ec) return {};
    s.mtime = std::filesystem::last_write_time(p, ec);
    if (ec) return {};
    s.exists = true;
    return s;
}

// One value per file, read again only when the file's stamp changes (it
// appears, goes, or is written).
template <class T>
class FileCache {
public:
    // The value for `p`: `read(p)` when `p` is new here or its stamp changed,
    // else the one kept.
    template <class Read>
    const T& get(const std::filesystem::path& p, Read&& read) {
        const FileStamp now = fileStamp(p);
        auto it = m_entries.find(p);
        if (it == m_entries.end() || !(it->second.first == now)) {
            ++m_reads;
            T v = read(p);
            it = m_entries.insert_or_assign(p, std::make_pair(now, std::move(v))).first;
        }
        return it->second.second;
    }
    // How many times get() has called `read`.
    std::uint64_t reads() const { return m_reads; }

private:
    std::map<std::filesystem::path, std::pair<FileStamp, T>> m_entries;
    std::uint64_t m_reads = 0;
};

}  // namespace editor
