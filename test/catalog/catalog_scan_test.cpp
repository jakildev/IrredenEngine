#include <irreden/ir_catalog.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

const std::filesystem::path kFixtureRoot = std::filesystem::path(IR_CATALOG_FIXTURE_ROOT) / "root";

const IRCatalog::Creation *
findCreation(const IRCatalog::Catalog &catalog, const std::string &suffix) {
    for (const auto &creation : catalog.creations_) {
        if (creation.dir_.ends_with(suffix)) {
            return &creation;
        }
    }
    return nullptr;
}

void writeText(const std::filesystem::path &path, const std::string &text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    ASSERT_TRUE(output) << path;
    output << text;
}

std::string manifestWithPaths(const std::string &thumbnail, const std::string &docs) {
    return R"json({
  "version": 1,
  "name": "Path fixture",
  "thumbnail": ")json" +
           thumbnail + R"json(",
  "docs": [")json" +
           docs + R"json("],
  "targets": [{"name": "IRPathFixture"}]
})json";
}

TEST(CatalogScanTest, FixtureTree) {
    const IRCatalog::CatalogService service(kFixtureRoot);
    const auto start = std::chrono::steady_clock::now();
    const auto catalog = service.scan(".", "fake_build");
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(elapsed, std::chrono::milliseconds(100));
    ASSERT_EQ(catalog.creations_.size(), 3u);

    const auto *valid = findCreation(catalog, "creations/demos/valid");
    ASSERT_NE(valid, nullptr);
    ASSERT_TRUE(valid->manifest_.has_value());
    EXPECT_EQ(valid->manifest_->name_, "Fixture creation");
    ASSERT_EQ(valid->targets_.size(), 2u);
    EXPECT_TRUE(valid->targets_[0].built_);
    EXPECT_TRUE(valid->targets_[0].exePath_.ends_with("IRFixtureBuilt.exe"));
    EXPECT_NE(valid->targets_[0].mtime_, 0.0);
    ASSERT_TRUE(valid->targets_[0].lastRun_.has_value());
    EXPECT_EQ(valid->targets_[0].lastRun_->verdict_, "CLEAN");
    EXPECT_EQ(valid->targets_[0].lastRun_->exit_, 0);
    EXPECT_FALSE(valid->targets_[0].lastRun_->signal_.has_value());
    EXPECT_FALSE(valid->targets_[1].built_);

    const auto *invalid = findCreation(catalog, "creations/demos/invalid");
    ASSERT_NE(invalid, nullptr);
    ASSERT_TRUE(invalid->error_.has_value());
    EXPECT_EQ(invalid->error_->line_, 5u);
    EXPECT_EQ(invalid->error_->column_, 14u);
    EXPECT_EQ(invalid->error_->path_, "targets[0].name");

    const auto *unsorted = findCreation(catalog, "creations/demos/unsorted");
    ASSERT_NE(unsorted, nullptr);
    EXPECT_FALSE(unsorted->manifest_.has_value());
    EXPECT_FALSE(unsorted->error_.has_value());
}

TEST(CatalogScanTest, RefusesPathsOutsideToolRoot) {
    const IRCatalog::CatalogService service(kFixtureRoot);
    EXPECT_THROW(service.scan("../root", "build"), std::invalid_argument);
    EXPECT_THROW(service.scan(kFixtureRoot.string(), "build"), std::invalid_argument);
    EXPECT_THROW(service.scan(".", "../build"), std::invalid_argument);
}

TEST(CatalogScanTest, DocsResolve) {
    const IRCatalog::CatalogService service(kFixtureRoot);
    const auto catalog = service.scan(".", "fake_build");
    const auto *valid = findCreation(catalog, "creations/demos/valid");
    ASSERT_NE(valid, nullptr);
    ASSERT_TRUE(valid->manifest_.has_value());
    ASSERT_EQ(valid->manifest_->docs_.size(), 1u);

    EXPECT_TRUE(
        std::filesystem::equivalent(
            kFixtureRoot / valid->manifest_->thumbnail_,
            kFixtureRoot / "creations/demos/valid/thumbnail.png"
        )
    );
    EXPECT_TRUE(
        std::filesystem::equivalent(
            kFixtureRoot / valid->manifest_->docs_[0],
            kFixtureRoot / "docs/design/x.md"
        )
    );
}

TEST(CatalogScanTest, ManifestPathEscape) {
    const auto base = std::filesystem::temp_directory_path() / "ir_catalog_manifest_path_test";
    const auto root = base / "root";
    const auto outside = base / "outside";
    std::error_code error;
    std::filesystem::remove_all(base, error);

    writeText(root / "docs/design/x.md", "fixture");
    writeText(outside / "outside.png", "outside");
    writeText(outside / "outside.md", "outside");

    const auto accepted = root / "creations/demos/accepted";
    writeText(accepted / "thumbnail.png", "thumbnail");
    std::filesystem::create_symlink("thumbnail.png", accepted / "thumbnail-link.png", error);
    if (error) {
        GTEST_SKIP() << "host cannot create file symlinks: " << error.message();
    }
    writeText(
        accepted / "creation.json",
        manifestWithPaths("thumbnail-link.png", "docs/design/x.md")
    );

    const auto badThumbnail = root / "creations/demos/bad_thumbnail";
    std::filesystem::create_directories(badThumbnail, error);
    ASSERT_FALSE(error);
    std::filesystem::create_symlink(outside / "outside.png", badThumbnail / "thumbnail.png", error);
    ASSERT_FALSE(error);
    writeText(
        badThumbnail / "creation.json",
        manifestWithPaths("thumbnail.png", "docs/design/x.md")
    );

    std::filesystem::create_symlink(outside / "outside.md", root / "docs/escape.md", error);
    ASSERT_FALSE(error);
    const auto badDocs = root / "creations/demos/bad_docs";
    writeText(badDocs / "thumbnail.png", "thumbnail");
    writeText(badDocs / "creation.json", manifestWithPaths("thumbnail.png", "docs/escape.md"));

    const IRCatalog::CatalogService service(root);
    const auto catalog = service.scan();

    const auto *acceptedCreation = findCreation(catalog, "creations/demos/accepted");
    ASSERT_NE(acceptedCreation, nullptr);
    ASSERT_TRUE(acceptedCreation->manifest_.has_value());
    EXPECT_EQ(acceptedCreation->manifest_->thumbnail_, "creations/demos/accepted/thumbnail.png");

    const auto *badThumbnailCreation = findCreation(catalog, "creations/demos/bad_thumbnail");
    ASSERT_NE(badThumbnailCreation, nullptr);
    ASSERT_TRUE(badThumbnailCreation->error_.has_value());
    EXPECT_EQ(badThumbnailCreation->error_->path_, "thumbnail");

    const auto *badDocsCreation = findCreation(catalog, "creations/demos/bad_docs");
    ASSERT_NE(badDocsCreation, nullptr);
    ASSERT_TRUE(badDocsCreation->error_.has_value());
    EXPECT_EQ(badDocsCreation->error_->path_, "docs[0]");

    std::filesystem::remove_all(base, error);
}

TEST(CatalogScanTest, SymlinkOutOfRoot) {
    const auto base = std::filesystem::temp_directory_path() / "ir_catalog_walk_symlink_test";
    const auto root = base / "root";
    const auto outside = base / "outside";
    std::error_code error;
    std::filesystem::remove_all(base, error);

    writeText(
        root / "creations/demos/visible/creation.json",
        manifestWithPaths("thumbnail.png", "docs/design/x.md")
    );
    writeText(root / "creations/demos/visible/thumbnail.png", "thumbnail");
    writeText(root / "docs/design/x.md", "fixture");
    writeText(
        outside / "hidden/creation.json",
        manifestWithPaths("thumbnail.png", "docs/design/x.md")
    );
    std::filesystem::create_directory_symlink(
        outside / "hidden",
        root / "creations/demos/hidden",
        error
    );
    if (error) {
        GTEST_SKIP() << "host cannot create directory symlinks: " << error.message();
    }

    const IRCatalog::CatalogService service(root);
    const auto catalog = service.scan();
    ASSERT_EQ(catalog.creations_.size(), 1u);
    EXPECT_NE(findCreation(catalog, "creations/demos/visible"), nullptr);

    std::filesystem::remove_all(base, error);
}

TEST(CatalogScanTest, RefusesSymlinkEscape) {
    const auto base = std::filesystem::temp_directory_path() / "ir_catalog_containment_test";
    const auto root = base / "root";
    const auto outside = base / "outside";
    std::error_code error;
    std::filesystem::remove_all(base, error);
    std::filesystem::create_directories(root, error);
    ASSERT_FALSE(error);
    std::filesystem::create_directories(outside, error);
    ASSERT_FALSE(error);
    std::filesystem::create_directory_symlink(outside, root / "escape", error);
    if (error) {
        GTEST_SKIP() << "host cannot create directory symlinks: " << error.message();
    }

    const IRCatalog::CatalogService service(root);
    EXPECT_THROW(service.scan("escape", "build"), std::invalid_argument);
    std::filesystem::remove_all(base, error);
}

} // namespace
