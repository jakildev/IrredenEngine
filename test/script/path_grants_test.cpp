#include <irreden/script/path_grants.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;

// `<tmp>/<name>/` holding `picked/a.png`, `picked/sub/b.png`, `picked2/c.png`
// (a string-prefix sibling of `picked`) and `outside.png`.
struct GrantTree {
    explicit GrantTree(const char *name)
        : root_{fs::weakly_canonical(fs::path(testing::TempDir()) / name)} {
        fs::remove_all(root_);
        fs::create_directories(root_ / "picked" / "sub");
        fs::create_directories(root_ / "picked2");
        for (const char *file :
             {"picked/a.png", "picked/sub/b.png", "picked2/c.png", "outside.png"}) {
            std::ofstream(root_ / file) << file;
        }
    }
    ~GrantTree() {
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }

    std::string at(const char *relative) const {
        return (root_ / relative).string();
    }

    fs::path root_;
};

bool trySymlink(const fs::path &target, const fs::path &link) {
    std::error_code error;
    fs::create_symlink(target, link, error);
    return !error;
}

TEST(PathGrants, StartsEmptyAndRefusesEverything) {
    const GrantTree tree{"path_grants_empty"};
    const IRScript::PathGrants grants;
    EXPECT_EQ(grants.size(), 0u);
    EXPECT_FALSE(grants.resolve(tree.at("picked/a.png")).has_value());
    EXPECT_FALSE(grants.resolve(tree.at("picked")).has_value());
}

TEST(PathGrants, AFileGrantCoversExactlyThatFile) {
    const GrantTree tree{"path_grants_file"};
    IRScript::PathGrants grants;
    ASSERT_TRUE(grants.grantFile(tree.at("picked/a.png")));
    EXPECT_EQ(grants.resolve(tree.at("picked/a.png")), tree.root_ / "picked" / "a.png");
    EXPECT_EQ(grants.resolve(tree.at("picked/sub/../a.png")), tree.root_ / "picked" / "a.png");

    EXPECT_FALSE(grants.resolve(tree.at("picked/sub/b.png")).has_value()) << "a sibling file";
    EXPECT_FALSE(grants.resolve(tree.at("picked")).has_value()) << "its folder";
    EXPECT_FALSE(grants.resolve(tree.at("picked/a.png/x")).has_value());
    EXPECT_FALSE(grants.resolve(tree.at("outside.png")).has_value());
}

TEST(PathGrants, ADirectoryGrantCoversItsSubtreeOnly) {
    const GrantTree tree{"path_grants_directory"};
    IRScript::PathGrants grants;
    ASSERT_TRUE(grants.grantDirectory(tree.at("picked")));
    EXPECT_TRUE(grants.resolve(tree.at("picked")).has_value());
    EXPECT_TRUE(grants.resolve(tree.at("picked/a.png")).has_value());
    EXPECT_TRUE(grants.resolve(tree.at("picked/sub/b.png")).has_value());
    EXPECT_TRUE(grants.resolve(tree.at("picked/not-there-yet.png")).has_value());

    EXPECT_FALSE(grants.resolve(tree.at("picked2/c.png")).has_value()) << "string-prefix sibling";
    EXPECT_FALSE(grants.resolve(tree.at("outside.png")).has_value());
    EXPECT_FALSE(grants.resolve(tree.at("picked/../outside.png")).has_value());
    EXPECT_FALSE(grants.resolve(tree.at("picked/missing/../../outside.png")).has_value());
    EXPECT_FALSE(grants.resolve(tree.root_.string()).has_value()) << "the parent";
}

TEST(PathGrants, RefusesRelativeEmptyAndNulPaths) {
    const GrantTree tree{"path_grants_spelling"};
    IRScript::PathGrants grants;
    ASSERT_TRUE(grants.grantDirectory(tree.at("picked")));
    EXPECT_FALSE(grants.resolve("").has_value());
    EXPECT_FALSE(grants.resolve("picked/a.png").has_value());
    EXPECT_FALSE(grants.resolve("a.png").has_value());
    const std::string withNul = tree.at("picked/a.png") + std::string{"\0x", 2};
    EXPECT_FALSE(grants.resolve(withNul).has_value());
}

TEST(PathGrants, ASymlinkOutOfAGrantedDirectoryIsRefused) {
    const GrantTree tree{"path_grants_symlink_out"};
    if (!trySymlink(tree.root_ / "outside.png", tree.root_ / "picked" / "out.png") ||
        !trySymlink(tree.root_ / "picked2", tree.root_ / "picked" / "sibling")) {
        GTEST_SKIP() << "this host cannot create symlinks";
    }
    IRScript::PathGrants grants;
    ASSERT_TRUE(grants.grantDirectory(tree.at("picked")));
    EXPECT_FALSE(grants.resolve(tree.at("picked/out.png")).has_value());
    EXPECT_FALSE(grants.resolve(tree.at("picked/sibling/c.png")).has_value());
}

TEST(PathGrants, APickedSymlinkGrantsItsTarget) {
    const GrantTree tree{"path_grants_symlink_pick"};
    if (!trySymlink(tree.root_ / "outside.png", tree.root_ / "alias.png")) {
        GTEST_SKIP() << "this host cannot create symlinks";
    }
    IRScript::PathGrants grants;
    ASSERT_TRUE(grants.grantFile(tree.at("alias.png")));
    EXPECT_EQ(grants.resolve(tree.at("alias.png")), tree.root_ / "outside.png");
    EXPECT_EQ(grants.resolve(tree.at("outside.png")), tree.root_ / "outside.png");
}

TEST(PathGrants, GrantsNeedAnExistingPathOfTheRightKind) {
    const GrantTree tree{"path_grants_kind"};
    IRScript::PathGrants grants;
    EXPECT_FALSE(grants.grantFile(tree.at("missing.png")));
    EXPECT_FALSE(grants.grantFile(tree.at("picked"))) << "a directory is not a file grant";
    EXPECT_FALSE(grants.grantDirectory(tree.at("picked/a.png")));
    EXPECT_FALSE(grants.grantDirectory(tree.at("missing")));
    EXPECT_FALSE(grants.grantFile("picked/a.png")) << "relative";
    EXPECT_EQ(grants.size(), 0u);
}

TEST(PathGrants, RepeatsCollapseAndClearRevokes) {
    const GrantTree tree{"path_grants_clear"};
    IRScript::PathGrants grants;
    ASSERT_TRUE(grants.grantFile(tree.at("picked/a.png")));
    ASSERT_TRUE(grants.grantFile(tree.at("picked/sub/../a.png")));
    ASSERT_TRUE(grants.grantDirectory(tree.at("picked2")));
    EXPECT_EQ(grants.size(), 2u);
    grants.clear();
    EXPECT_EQ(grants.size(), 0u);
    EXPECT_FALSE(grants.resolve(tree.at("picked/a.png")).has_value());
    EXPECT_FALSE(grants.resolve(tree.at("picked2/c.png")).has_value());
}

TEST(ResolveReadablePath, ContainedRelativeOrGrantedAbsolute) {
    const GrantTree tree{"path_grants_readable"};
    IRScript::PathGrants grants;
    const fs::path base = tree.root_ / "picked";
    EXPECT_EQ(IRScript::resolveReadablePath(grants, base, "a.png"), base / "a.png");
    EXPECT_FALSE(IRScript::resolveReadablePath(grants, base, "../outside.png").has_value());
    EXPECT_FALSE(IRScript::resolveReadablePath(grants, base, tree.at("outside.png")).has_value());

    ASSERT_TRUE(grants.grantFile(tree.at("outside.png")));
    EXPECT_EQ(
        IRScript::resolveReadablePath(grants, base, tree.at("outside.png")),
        tree.root_ / "outside.png"
    );
    EXPECT_FALSE(IRScript::resolveReadablePath(grants, base, "../outside.png").has_value())
        << "a grant never widens the relative route";
    EXPECT_FALSE(IRScript::resolveReadablePath(grants, base, tree.at("picked2/c.png")).has_value());
}

} // namespace
