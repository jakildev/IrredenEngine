#ifndef TEST_MULTI_ROOT_FIXTURE_H
#define TEST_MULTI_ROOT_FIXTURE_H

#include "executable_file.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace IRTest {

/// A base checkout and its one live worktree `.claude/worktrees/second`, each
/// with a creation `creations/same` whose target is `IRSameTarget`, its own
/// `docs/root.md` (contents `base` / `second`), a built executable, a verdict
/// (`CLEAN` / `CRASH`) and a package archive. Beside it, the candidates
/// discovery must skip: `link` (a symlink to `second`), `nomarker` (no
/// `.git`), `linkmarker` (a symlinked `.git`), `bare` (no `creations/`), and a
/// worktree nested inside `second`.
class MultiRootFixture {
  public:
    explicit MultiRootFixture(const std::string &name)
        : m_base(std::filesystem::temp_directory_path() / name) {
        std::error_code error;
        std::filesystem::remove_all(m_base, error);
        writeRoot(m_base, "Base same", "base", "CLEAN");
        m_second = m_base / ".claude/worktrees/second";
        writeRoot(m_second, "Second same", "second", "CRASH");
        writeText(m_second / ".git", "gitdir: /elsewhere/.git/worktrees/second\n");

        const auto worktrees = m_base / ".claude/worktrees";
        std::filesystem::create_directories(worktrees / "nomarker/creations");
        std::filesystem::create_directories(worktrees / "linkmarker/creations");
        std::filesystem::create_directories(worktrees / "bare");
        writeText(worktrees / "bare/.git", "gitdir: /elsewhere\n");
        std::filesystem::create_directories(m_second / ".claude/worktrees/nested/creations");
        writeText(m_second / ".claude/worktrees/nested/.git", "gitdir: /elsewhere\n");
        std::filesystem::create_directory_symlink(m_second, worktrees / "link", error);
        m_symlinksAvailable = !error;
        std::filesystem::create_symlink(m_second / ".git", worktrees / "linkmarker/.git", error);
    }

    ~MultiRootFixture() {
        std::error_code error;
        std::filesystem::remove_all(m_base, error);
    }

    MultiRootFixture(const MultiRootFixture &) = delete;
    MultiRootFixture &operator=(const MultiRootFixture &) = delete;

    const std::filesystem::path &base() const {
        return m_base;
    }
    const std::filesystem::path &second() const {
        return m_second;
    }
    bool symlinksAvailable() const {
        return m_symlinksAvailable;
    }

  private:
    static void writeText(const std::filesystem::path &path, const std::string &text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path) << text;
    }

    static void writeRoot(
        const std::filesystem::path &root,
        const std::string &name,
        const std::string &docs,
        const std::string &verdict
    ) {
        writeText(
            root / "creations/same/creation.json",
            R"json({"version": 1, "name": ")json" + name +
                R"json(", "docs": ["docs/root.md"], "targets": [{"name": "IRSameTarget", "package": true}]})json"
        );
        writeText(root / "docs/root.md", docs);
        writeExecutableFile(root / "build/IRSameTarget.exe", "exe");
        writeText(
            root / "build/ir-run-results/IRSameTarget.exe.last.json",
            R"json({"verdict": ")json" + verdict +
                R"json(", "exe": "IRSameTarget", "exit": 0, "signal": null, "argv": [], "at": "now"})json"
        );
        writeText(root / "build/IRSameTarget-os-arch.zip", "zip");
    }

    std::filesystem::path m_base;
    std::filesystem::path m_second;
    bool m_symlinksAvailable = false;
};

} // namespace IRTest

#endif /* TEST_MULTI_ROOT_FIXTURE_H */
