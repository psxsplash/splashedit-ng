#pragma once

// The Windows taskbar Jump List: right-clicking SplashEdit's taskbar button
// lists its recent projects, each one starting the editor with --project.
// Other platforms have none; there the calls do nothing.

#include <filesystem>
#include <string>
#include <vector>

namespace editor {

// One Jump List line, all UTF-8: what it shows, what it runs the editor
// with and its tooltip.
struct JumpListEntry {
    std::string title;        // the project folder's name
    std::string arguments;    // --project "<folder>", quoted for CommandLineToArgvW
    std::string description;  // the folder's full path
};

// At most this many projects go in the list.
constexpr size_t kJumpListMax = 10;

// `arg` as one command-line argument: in double quotes, with backslashes
// before a quote (or the closing one) doubled and quotes escaped, so a path
// with spaces or a trailing backslash ("C:\") reads back whole.
std::string quoteArgument(const std::string& arg);

// The entries for `projects`, newest first as given: folders that no longer
// exist are left out, and at most kJumpListMax are kept.
std::vector<JumpListEntry> jumpListEntries(const std::vector<std::filesystem::path>& projects);

// Names the process (an explicit AppUserModelID) so its taskbar button and
// Jump List go together. Call once, before any window is made.
void setTaskbarAppId();

// Replaces the Jump List's "Recent projects" with `projects`.
void updateJumpList(const std::vector<std::filesystem::path>& projects);

}  // namespace editor
