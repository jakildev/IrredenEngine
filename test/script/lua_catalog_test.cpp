#include <irreden/script/lua_script.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace {

const std::filesystem::path kFixtureRoot = std::filesystem::path(IR_CATALOG_FIXTURE_ROOT) / "root";

TEST(LuaCatalogTest, MirrorsCatalogAndAssetFields) {
    IRScript::LuaScript script;
    script.bindLuaCatalog(kFixtureRoot);

    const auto result = script.lua().safe_script(R"lua(
        local catalog = IRCatalog.scan{buildDir = "fake_build"}
        assert(#catalog.creations == 3)
        local valid
        for _, creation in ipairs(catalog.creations) do
            if creation.manifest and creation.manifest.name == "Fixture creation" then
                valid = creation
            end
        end
        assert(valid)
        assert(valid.root == ".")
        assert(valid.dir == "creations/demos/valid")
        assert(valid.manifest.kind == "test")
        assert(valid.manifest.tags[2] == "catalog")
        assert(valid.manifest.thumbnail == "creations/demos/valid/thumbnail.png")
        assert(valid.manifest.docs[1] == "docs/design/x.md")
        assert(valid.manifest.targets[1].presets[1].args[1] == "--fixture")
        assert(valid.targets[1].built == true)
        assert(valid.targets[1].lastRun.verdict == "CLEAN")
        assert(valid.targets[1].lastRun.exit == 0)
        assert(valid.targets[1].lastRun.signal == nil)

        local assets = IRCatalog.assets{}
        local foundVoxel, foundPrefab, foundPng, foundData = false, false, false, false
        for _, asset in ipairs(assets.entries) do
            if asset.kind == "voxel_set" then
                foundVoxel = asset.fields.mode == "DENSE" and asset.fields.frame_count == "6"
            elseif asset.kind == "prefab" then
                foundPrefab = asset.parts[1] == "stem" and asset.parts[2] == "crown"
            elseif asset.kind == "png" then
                foundPng = asset.path == "test/references/fixture.png"
            elseif asset.kind == "data" then
                foundData = asset.path == "creations/demos/valid/data/readme.txt"
            end
        end
        assert(foundVoxel and foundPrefab and foundPng and foundData)
    )lua");
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
}

TEST(LuaCatalogTest, RefusesEscapingPathsBeforeScanning) {
    IRScript::LuaScript script;
    script.bindLuaCatalog(kFixtureRoot);

    const std::string absolute = kFixtureRoot.string();
    script.lua()["absoluteFixtureRoot"] = absolute;
    EXPECT_THROW(script.lua().safe_script("return IRCatalog.scan{root = '../root'}"), sol::error);
    EXPECT_THROW(
        script.lua().safe_script("return IRCatalog.assets{root = absoluteFixtureRoot}"),
        sol::error
    );
}

TEST(LuaCatalogTest, UnboundStateHasNoCatalogTable) {
    IRScript::LuaScript script;
    const auto result = script.lua().safe_script("return IRCatalog == nil");
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_TRUE(result.get<bool>());
}

} // namespace
