#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/render/image_data.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_widget_image.hpp>
#include <irreden/render/widget_theme.hpp>
#include <irreden/render/widgets.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/script/lua_widget_bindings.hpp>

#include <sol/sol.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

using IRComponents::C_GuiPosition;
using IRComponents::C_Widget;
using IRComponents::C_WidgetImage;
using IRComponents::WidgetKind;

void writeFixturePng(const fs::path &path) {
    const std::vector<std::uint8_t> rgba{90, 60, 30, 255, 90, 60, 30, 255};
    IRRender::writePNG(path.string().c_str(), 2, 1, 4, rgba.data());
}

// A fresh `<tmp>/<name>/base` run directory holding `thumb.png`, with
// `<tmp>/<name>/outside.png` and `<tmp>/<name>/base2/thumb.png` beside it.
struct FixtureTree {
    explicit FixtureTree(const char *name)
        : root_{fs::weakly_canonical(fs::path(testing::TempDir()) / name)}
        , base_{root_ / "base"} {
        fs::remove_all(root_);
        fs::create_directories(base_);
        fs::create_directories(root_ / "base2");
        writeFixturePng(base_ / "thumb.png");
        writeFixturePng(root_ / "outside.png");
        writeFixturePng(root_ / "base2" / "thumb.png");
    }
    ~FixtureTree() {
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }

    fs::path root_;
    fs::path base_;
};

// A host that cannot create symlinks (native Windows without the privilege)
// skips the symlink cases rather than failing them.
bool trySymlink(const fs::path &target, const fs::path &link) {
    std::error_code error;
    fs::create_symlink(target, link, error);
    return !error;
}

// Records every decode, then runs the real one so loaded images keep their
// texels. The refusal rows prove a refused path never reaches the decoder.
std::vector<std::string> g_decodedPaths;

bool recordingDecoder(
    const std::string &path,
    IRMath::ivec2 texelSize,
    IRMath::Color background,
    std::vector<IRMath::Color> &texels,
    std::string &error
) {
    g_decodedPaths.push_back(path);
    return IRPrefab::Widget::detail::decodeImageTexels(path, texelSize, background, texels, error);
}

// ---- IRGui.makeImage / setImagePath / imageError ----------------------------

// IRGui is re-bound with the fixture's `base` as its image base, and the
// working directory is moved elsewhere (`base2`), so a path that resolved
// against the run directory instead of the bound base would be caught.
//
// m_lua is declared FIRST so it is destroyed LAST (test/CLAUDE.md).
class LuaWidgetImage : public testing::Test {
  protected:
    LuaWidgetImage()
        : m_tree{"lua_widget_image"} {
        m_lua.bindLuaDrivenEcs();
    }

    void SetUp() override {
        m_previousDirectory = fs::current_path();
        fs::current_path(m_tree.root_ / "base2");
        IRScript::detail::bindWidgets(m_lua, m_tree.base_);
        m_previousDecoder = IRPrefab::Widget::detail::imageDecoder();
        IRPrefab::Widget::detail::imageDecoder() = &recordingDecoder;
        g_decodedPaths.clear();
    }

    void TearDown() override {
        IRPrefab::Widget::detail::imageDecoder() = m_previousDecoder;
        m_entityManager.destroyAllEntities();
        fs::current_path(m_previousDirectory);
    }

    void run(const char *script) {
        auto result = m_lua.lua().safe_script(script, sol::script_pass_on_error);
        ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    }

    std::string error(const char *script) {
        auto result = m_lua.lua().safe_script(script, sol::script_pass_on_error);
        EXPECT_FALSE(result.valid());
        if (result.valid()) {
            return {};
        }
        sol::error err = result;
        return err.what();
    }

    IREntity::EntityId entity(const char *global) {
        return m_lua.lua()[global].get<IREntity::EntityId>();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    FixtureTree m_tree;
    fs::path m_previousDirectory;
    IRPrefab::Widget::detail::ImageDecoder m_previousDecoder = nullptr;
};

TEST_F(LuaWidgetImage, MakeImageBuildsALoadedImage) {
    run(R"lua(
        image = IRGui.makeImage(12, 34, 1, 1, "thumb.png")
        errIsNil = IRGui.imageError(image) == nil
    )lua");
    const IREntity::EntityId id = entity("image");
    EXPECT_EQ(IREntity::getComponent<C_Widget>(id).kind_, WidgetKind::IMAGE);
    EXPECT_EQ(IREntity::getComponent<C_Widget>(id).size_, IRMath::ivec2(1, 1));
    EXPECT_EQ(IREntity::getComponent<C_GuiPosition>(id).pos_, IRMath::ivec2(12, 34));
    const C_WidgetImage &image = IREntity::getComponent<C_WidgetImage>(id);
    EXPECT_TRUE(image.loaded_);
    EXPECT_EQ(fs::path(image.path_), m_tree.base_ / "thumb.png");
    ASSERT_EQ(image.texels_.size(), 1u);
    EXPECT_EQ(image.texels_[0].toPackedRGBA(), (IRMath::Color{90, 60, 30, 255}.toPackedRGBA()));
    EXPECT_TRUE(m_lua.lua()["errIsNil"].get<bool>());
    EXPECT_EQ(g_decodedPaths, std::vector<std::string>{(m_tree.base_ / "thumb.png").string()});
}

TEST_F(LuaWidgetImage, MissingFileStillBuildsAndImageErrorNamesThePath) {
    run(R"lua(
        image = IRGui.makeImage(0, 0, 8, 4, "absent.png")
        err = IRGui.imageError(image)
    )lua");
    const C_WidgetImage &image = IREntity::getComponent<C_WidgetImage>(entity("image"));
    EXPECT_FALSE(image.loaded_);
    const std::string err = m_lua.lua()["err"].get<std::string>();
    EXPECT_NE(err.find("absent.png"), std::string::npos) << err;

    const IRPrefab::Widget::WidgetTheme &theme = IRPrefab::Widget::defaultTheme();
    ASSERT_EQ(image.texels_.size(), 32u);
    EXPECT_EQ(image.texels_[0].toPackedRGBA(), theme.textDisabled_.toPackedRGBA())
        << "the hatch line through the origin";
    EXPECT_EQ(image.texels_[4].toPackedRGBA(), theme.backgroundDisabled_.toPackedRGBA())
        << "between hatch lines";
}

TEST_F(LuaWidgetImage, SetImagePathReloads) {
    run(R"lua(
        image = IRGui.makeImage(0, 0, 1, 1, "absent.png")
        beforeIsString = type(IRGui.imageError(image)) == "string"
        IRGui.setImagePath(image, "thumb.png")
        afterIsNil = IRGui.imageError(image) == nil
    )lua");
    EXPECT_TRUE(m_lua.lua()["beforeIsString"].get<bool>());
    EXPECT_TRUE(m_lua.lua()["afterIsNil"].get<bool>());
    EXPECT_EQ(
        fs::path(IREntity::getComponent<C_WidgetImage>(entity("image")).path_),
        m_tree.base_ / "thumb.png"
    );
}

TEST_F(LuaWidgetImage, EscapingPathsRaiseWithoutADecode) {
    if (!trySymlink(m_tree.root_ / "outside.png", m_tree.base_ / "out.png") ||
        !trySymlink(m_tree.root_ / "base2", m_tree.base_ / "sibling")) {
        GTEST_SKIP() << "this host cannot create symlinks";
    }
    run(R"lua(image = IRGui.makeImage(0, 0, 1, 1, "thumb.png"))lua");
    const std::size_t entitiesBefore = m_entityManager.getLiveEntityCount();
    g_decodedPaths.clear();

    const char *rejected[] = {
        "/etc/x.png",
        "C:\\x.png",
        "\\\\host\\s\\x.png",
        "../x.png",
        "out.png",           // symlink to a PNG outside the base
        "sibling/thumb.png", // symlink into `<base>2`, a string-prefix sibling
    };
    for (const char *path : rejected) {
        m_lua.lua()["path"] = path;
        EXPECT_NE(
            error("IRGui.makeImage(0, 0, 4, 4, path)").find("IRGui.makeImage"),
            std::string::npos
        ) << path;
        EXPECT_NE(
            error("IRGui.setImagePath(image, path)").find("IRGui.setImagePath"),
            std::string::npos
        ) << path;
    }
    EXPECT_TRUE(g_decodedPaths.empty()) << "decoded '" << g_decodedPaths.front() << "'";
    EXPECT_EQ(m_entityManager.getLiveEntityCount(), entitiesBefore);
    EXPECT_EQ(
        fs::path(IREntity::getComponent<C_WidgetImage>(entity("image")).path_),
        m_tree.base_ / "thumb.png"
    ) << "a refused path must leave the widget's image untouched";
}

TEST_F(LuaWidgetImage, InBaseSymlinkDecodesOnceWithTheResolvedPath) {
    if (!trySymlink(m_tree.base_ / "thumb.png", m_tree.base_ / "alias.png")) {
        GTEST_SKIP() << "this host cannot create symlinks";
    }
    run(R"lua(image = IRGui.makeImage(0, 0, 1, 1, "alias.png"))lua");
    EXPECT_TRUE(IREntity::getComponent<C_WidgetImage>(entity("image")).loaded_);
    EXPECT_EQ(g_decodedPaths, std::vector<std::string>{(m_tree.base_ / "thumb.png").string()});
}

TEST_F(LuaWidgetImage, DefaultOverloadConfinesToTheRunDirectory) {
    IRScript::detail::bindWidgets(m_lua);
    run(R"lua(image = IRGui.makeImage(0, 0, 1, 1, "thumb.png"))lua");
    EXPECT_EQ(
        fs::path(IREntity::getComponent<C_WidgetImage>(entity("image")).path_),
        m_tree.root_ / "base2" / "thumb.png"
    );
    g_decodedPaths.clear();
    EXPECT_NE(
        error("IRGui.makeImage(0, 0, 1, 1, '../base/thumb.png')").find("image base"),
        std::string::npos
    );
    EXPECT_TRUE(g_decodedPaths.empty());
}

TEST_F(LuaWidgetImage, BadArgumentsRaise) {
    EXPECT_NE(
        error("IRGui.makeImage(0, 0, 0, 4, 'thumb.png')").find("IRGui.makeImage"),
        std::string::npos
    );
    run(R"lua(label = IRGui.makeLabel(0, 0, "HI"))lua");
    EXPECT_NE(
        error("IRGui.imageError(label)").find("is not a makeImage widget"),
        std::string::npos
    );
    EXPECT_NE(
        error("IRGui.setImagePath(label, 'thumb.png')").find("IRGui.setImagePath"),
        std::string::npos
    );
}

} // namespace
