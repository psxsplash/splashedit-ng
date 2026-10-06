// Projects on disk: a folder holding one or more .scene files with the
// meshes/, textures/ and scripts/ they refer to by project-relative path.
// This half has no SDL; the dialogs live in the app.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace editor {

// The first .scene directly in `project`, by name; empty when there is none.
std::filesystem::path firstScene(const std::filesystem::path& project);

// Makes `dir` a project: creates it and meshes/, textures/, scripts/, and
// writes an empty scene named after the folder. A folder that already holds a
// scene is refused, so New never overwrites one; open it instead. On success
// returns the new scene's file name, relative to `dir`.
struct NewProject {
    std::filesystem::path scene;
    std::string error;  // empty on success
};
NewProject createProject(const std::filesystem::path& dir);

// Where Save As may put a scene: inside `project`, since every asset path in
// it is relative to the project folder. Returns the path relative to the
// project (with a .scene extension added if missing), or nullopt outside it.
std::optional<std::filesystem::path> sceneInProject(const std::filesystem::path& project, const std::filesystem::path& file);

// Recently opened scenes, newest first, as absolute paths. Stored one per line.
class RecentScenes {
  public:
    static constexpr size_t kMax = 8;
    explicit RecentScenes(std::filesystem::path file = {}) : m_file(std::move(file)) {}
    void load();
    // Moves `scene` to the front, drops the oldest past kMax, and saves.
    void add(const std::filesystem::path& scene);
    void remove(const std::filesystem::path& scene);
    const std::vector<std::filesystem::path>& items() const { return m_items; }

  private:
    void save() const;
    std::filesystem::path m_file;  // empty = kept in memory only
    std::vector<std::filesystem::path> m_items;
};

}  // namespace editor
