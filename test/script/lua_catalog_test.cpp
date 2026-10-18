#include <irreden/script/lua_script.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <random>
#include <string>

namespace {

const std::filesystem::path kFixtureRoot = std::filesystem::path(IR_CATALOG_FIXTURE_ROOT) / "root";

void expectLuaError(
    IRScript::LuaScript &script, const std::string &source, const std::string &message
) {
    try {
        script.lua().safe_script(source);
        FAIL() << "expected Lua error containing: " << message;
    } catch (const sol::error &error) {
        EXPECT_NE(std::string(error.what()).find(message), std::string::npos) << error.what();
    }
}

TEST(LuaCatalogTest, MirrorsCatalogAndAssetFields) {
    IRScript::LuaScript script;
    script.bindLuaCatalog(kFixtureRoot);

    const auto result = script.lua().safe_script(R"lua(
        local catalog = IRCatalog.scan{}
        assert(#catalog.creations == 3)
        local valid, invalid, unsorted
        for _, creation in ipairs(catalog.creations) do
            if creation.manifest and creation.manifest.name == "Fixture creation" then
                valid = creation
            elseif creation.error then
                invalid = creation
            else
                unsorted = creation
            end
        end
        assert(valid and invalid and unsorted)
        assert(valid.root == ".")
        assert(valid.dir == "creations/demos/valid")
        assert(valid.manifest.version == 1)
        assert(valid.manifest.name == "Fixture creation")
        assert(valid.manifest.description == "Catalog fixture")
        assert(valid.manifest.group == "tests/catalog")
        assert(valid.manifest.tags[1] == "fixture")
        assert(valid.manifest.kind == "test")
        assert(valid.manifest.tags[2] == "catalog")
        assert(valid.manifest.thumbnail == "creations/demos/valid/thumbnail.png")
        assert(valid.manifest.docs[1] == "docs/design/x.md")
        assert(valid.manifest.targets[1].name == "IRFixtureBuilt")
        assert(valid.manifest.targets[1].display == "Built fixture")
        assert(valid.manifest.targets[1].description == "Has a fake executable")
        assert(valid.manifest.targets[1].presets[1].name == "Interactive")
        assert(valid.manifest.targets[1].presets[1].args[1] == "--fixture")
        assert(valid.manifest.targets[2].name == "IRFixtureMissing")
        assert(valid.manifest.targets[2].display == "Missing fixture")
        assert(valid.manifest.targets[2].description == "")
        assert(#valid.manifest.targets[2].presets == 0)
        assert(valid.targets[1].name == "IRFixtureBuilt")
        assert(valid.targets[1].display == "Built fixture")
        assert(valid.targets[1].built == true)
        assert(valid.targets[1].exePath == "build/creations/demos/valid/IRFixtureBuilt.exe")
        assert(valid.targets[1].mtime > 0)
        assert(valid.targets[1].lastRun.verdict == "CLEAN")
        assert(valid.targets[1].lastRun.exe == "IRFixtureBuilt")
        assert(valid.targets[1].lastRun.exit == 0)
        assert(valid.targets[1].lastRun.signal == nil)
        assert(valid.targets[1].lastRun.argv[1] == "IRFixtureBuilt")
        assert(valid.targets[1].lastRun.argv[2] == "--fixture")
        assert(valid.targets[1].lastRun.at == "2026-10-17T12:00:00Z")
        assert(valid.targets[2].name == "IRFixtureMissing")
        assert(valid.targets[2].display == "Missing fixture")
        assert(valid.targets[2].built == false)
        assert(valid.targets[2].exePath == "")
        assert(valid.targets[2].mtime == 0)
        assert(valid.targets[2].lastRun == nil)

        assert(invalid.root == ".")
        assert(invalid.dir == "creations/demos/invalid")
        assert(invalid.manifest == nil)
        assert(invalid.error.line == 5)
        assert(invalid.error.column == 14)
        assert(invalid.error.path == "targets[0].name")
        assert(type(invalid.error.message) == "string" and #invalid.error.message > 0)
        assert(#invalid.targets == 0)

        assert(unsorted.root == ".")
        assert(unsorted.dir == "creations/demos/unsorted")
        assert(unsorted.manifest == nil)
        assert(unsorted.error == nil)
        assert(#unsorted.targets == 0)

        local assets = IRCatalog.assets{}
        local voxel, prefab, png, data
        for _, asset in ipairs(assets.entries) do
            if asset.kind == "voxel_set" then
                voxel = asset
            elseif asset.kind == "prefab" then
                prefab = asset
            elseif asset.kind == "png" then
                png = asset
            elseif asset.kind == "data" then
                data = asset
            end
        end
        assert(voxel and prefab and png and data)
        assert(voxel.path == "assets/voxel/entities/crate.vxs")
        assert(voxel.mtime > 0)
        assert(voxel.fields.mode == "DENSE")
        assert(voxel.fields.frame_count == "6")
        assert(voxel.fields.bounds == "min=0,1,2;max=3,4,5")
        assert(#voxel.parts == 0)
        assert(prefab.path == "assets/voxel/entities/composite.prefab.lua")
        assert(prefab.mtime > 0)
        assert(prefab.fields.part_count == "2")
        assert(prefab.parts[1] == "stem" and prefab.parts[2] == "crown")
        assert(png.path == "test/references/fixture.png")
        assert(png.mtime > 0 and next(png.fields) == nil and #png.parts == 0)
        assert(data.path == "creations/demos/valid/data/readme.txt")
        assert(data.mtime > 0 and next(data.fields) == nil and #data.parts == 0)
    )lua");
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
}

TEST(LuaCatalogTest, RefusesEscapingPathsBeforeScanning) {
    const auto base = std::filesystem::temp_directory_path() /
                      ("ir_lua_catalog_path_test_" + std::to_string(std::random_device{}()));
    const auto root = base / "root";
    const auto sibling = base / "root2";
    const auto outside = base / "outside";
    std::error_code error;
    std::filesystem::create_directories(root, error);
    ASSERT_FALSE(error);
    std::filesystem::create_directories(sibling, error);
    ASSERT_FALSE(error);
    std::filesystem::create_directories(outside, error);
    ASSERT_FALSE(error);
    std::filesystem::create_directory_symlink(outside, root / "escape", error);
    if (error) {
        const std::string message = error.message();
        std::filesystem::remove_all(base, error);
        GTEST_SKIP() << "host cannot create directory symlinks: " << message;
    }
    std::filesystem::create_directory_symlink(sibling, root / "sibling", error);
    ASSERT_FALSE(error);

    IRScript::LuaScript script;
    script.bindLuaCatalog(root);

    script.lua()["absoluteRoot"] = root.string();
    expectLuaError(
        script,
        "return IRCatalog.scan{root = absoluteRoot}",
        "catalog root must stay under the tool root"
    );
    expectLuaError(
        script,
        "return IRCatalog.scan{buildDir = absoluteRoot}",
        "catalog buildDir must stay under the tool root"
    );
    expectLuaError(script, "return IRCatalog.scan{root = '../root'}", "catalog root must stay");
    expectLuaError(
        script,
        "return IRCatalog.scan{buildDir = '../x'}",
        "catalog buildDir must stay"
    );
    expectLuaError(script, "return IRCatalog.scan{root = 'escape'}", "catalog root must stay");
    expectLuaError(script, "return IRCatalog.scan{root = 'sibling'}", "catalog root must stay");

    std::filesystem::remove_all(base, error);
}

TEST(LuaCatalogTest, UnboundStateHasNoCatalogTable) {
    IRScript::LuaScript script;
    const auto result = script.lua().safe_script("return IRCatalog == nil");
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_TRUE(result.get<bool>());
}

} // namespace
