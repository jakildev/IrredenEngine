#include <irreden/window/file_dialog.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

using IRWindow::FileDialogFilter;
using IRWindow::FileDialogHost;
using IRWindow::FileDialogMode;
using IRWindow::FileDialogRequest;
using IRWindow::FileDialogResult;
using IRWindow::FileDialogStatus;
using IRWindow::WindowMode;

std::string absolutePath(const char *name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

// A panel that records what it was asked and answers with a fixed result.
struct RecordingPanel {
    FileDialogResult answer_;
    std::vector<FileDialogRequest> requests_;

    IRWindow::FileDialogPanel panel() {
        return [this](const FileDialogRequest &request) {
            requests_.push_back(request);
            return answer_;
        };
    }
};

FileDialogResult accepted(std::vector<std::string> paths) {
    FileDialogResult result;
    result.status_ = FileDialogStatus::ACCEPTED;
    result.paths_ = std::move(paths);
    return result;
}

// ---- extension and request normalization ------------------------------------

TEST(FileDialogExtension, StripsTheWildcardAndDotSpellings) {
    EXPECT_EQ(IRWindow::normalizeFileDialogExtension("png"), "png");
    EXPECT_EQ(IRWindow::normalizeFileDialogExtension(".png"), "png");
    EXPECT_EQ(IRWindow::normalizeFileDialogExtension("*.png"), "png");
    EXPECT_EQ(IRWindow::normalizeFileDialogExtension("tar.gz"), "tar.gz");
    EXPECT_EQ(IRWindow::normalizeFileDialogExtension("PNG"), "PNG");
    EXPECT_EQ(IRWindow::normalizeFileDialogExtension("*"), "*");
    EXPECT_EQ(IRWindow::normalizeFileDialogExtension("*.*"), "*");
}

TEST(FileDialogExtension, RejectsWhatIsNotAnExtension) {
    for (const char *bad :
         {"",
          ".",
          "*.",
          "..png",
          "png.",
          "p*g",
          "*.p?g",
          "a/b",
          "a\\b",
          "a b",
          "a;b",
          "a|b",
          "a:b",
          "a\"b",
          "a<b",
          "a>b",
          "a\tb",
          "a\nb"}) {
        EXPECT_FALSE(IRWindow::normalizeFileDialogExtension(bad).has_value()) << '"' << bad << '"';
    }
    EXPECT_FALSE(IRWindow::normalizeFileDialogExtension(std::string_view{"p\0g", 3}).has_value());
}

TEST(FileDialogRequestNormalization, NormalizesFiltersAndFillsInNames) {
    FileDialogRequest request;
    request.mode_ = FileDialogMode::OPEN_FILES;
    request.filters_ = {
        {"Images", {"*.png", ".jpg", "png"}},
        {"", {"vxs", "txt"}},
        {"", {"txt", "*"}},
    };
    const auto normalized = IRWindow::normalizeFileDialogRequest(request);
    ASSERT_TRUE(normalized.has_value()) << normalized.error();
    ASSERT_EQ(normalized->filters_.size(), 3u);
    EXPECT_EQ(normalized->filters_[0].name_, "Images");
    EXPECT_EQ(normalized->filters_[0].extensions_, (std::vector<std::string>{"png", "jpg"}));
    EXPECT_EQ(normalized->filters_[1].name_, "*.vxs, *.txt");
    EXPECT_EQ(normalized->filters_[2].name_, "All files");
    EXPECT_EQ(normalized->filters_[2].extensions_, std::vector<std::string>{"*"});
}

TEST(FileDialogRequestNormalization, IsIdempotent) {
    FileDialogRequest request;
    request.title_ = "Pick";
    request.directory_ = "some/relative/dir";
    request.filters_ = {{"", {"*.png"}}};
    const auto once = IRWindow::normalizeFileDialogRequest(request);
    ASSERT_TRUE(once.has_value());
    const auto twice = IRWindow::normalizeFileDialogRequest(*once);
    ASSERT_TRUE(twice.has_value());
    EXPECT_EQ(twice->directory_, once->directory_);
    EXPECT_EQ(twice->filters_[0].name_, once->filters_[0].name_);
    EXPECT_EQ(twice->filters_[0].extensions_, once->filters_[0].extensions_);
}

TEST(FileDialogRequestNormalization, MakesARelativeDirectoryAbsolute) {
    FileDialogRequest request;
    request.directory_ = "data/../images";
    const auto normalized = IRWindow::normalizeFileDialogRequest(request);
    ASSERT_TRUE(normalized.has_value());
    EXPECT_EQ(
        std::filesystem::path(normalized->directory_),
        (std::filesystem::current_path() / "images").lexically_normal()
    );

    request.directory_.clear();
    EXPECT_TRUE(IRWindow::normalizeFileDialogRequest(request)->directory_.empty());
}

TEST(FileDialogRequestNormalization, RejectsInvalidRequests) {
    const auto errorOf = [](const FileDialogRequest &request) {
        const auto normalized = IRWindow::normalizeFileDialogRequest(request);
        return normalized.has_value() ? std::string{} : normalized.error();
    };

    FileDialogRequest request;
    request.title_ = std::string{"a\0b", 3};
    EXPECT_NE(errorOf(request).find("title"), std::string::npos);

    request = {};
    request.directory_ = std::string{"a\0b", 3};
    EXPECT_NE(errorOf(request).find("directory"), std::string::npos);

    request = {};
    request.mode_ = FileDialogMode::OPEN_FOLDER;
    request.filters_ = {{"Images", {"png"}}};
    EXPECT_NE(errorOf(request).find("OPEN_FOLDER"), std::string::npos);

    request = {};
    request.filters_ = {{"Images", {}}};
    EXPECT_NE(errorOf(request).find("filters[1] has no extensions"), std::string::npos);

    request.filters_ = {{"Images", {"png"}}, {"Bad", {"a/b"}}};
    EXPECT_NE(errorOf(request).find("filters[2]: 'a/b'"), std::string::npos);

    request = {};
    request.mode_ = static_cast<FileDialogMode>(17);
    EXPECT_NE(errorOf(request).find("mode"), std::string::npos);
}

// ---- runFileDialog: gates, panel injection, result checks -------------------

TEST(RunFileDialog, PassesTheNormalizedRequestAndReturnsThePick) {
    RecordingPanel panel;
    panel.answer_ = accepted({absolutePath("a.png"), absolutePath("b.png")});
    FileDialogRequest request;
    request.mode_ = FileDialogMode::OPEN_FILES;
    request.title_ = "Choose images";
    request.filters_ = {{"Images", {"*.png"}}};

    const FileDialogResult result = IRWindow::runFileDialog(request, {}, panel.panel());
    EXPECT_EQ(result.status_, FileDialogStatus::ACCEPTED);
    EXPECT_EQ(result.paths_.size(), 2u);
    EXPECT_TRUE(result.error_.empty());
    ASSERT_EQ(panel.requests_.size(), 1u);
    EXPECT_EQ(panel.requests_[0].title_, "Choose images");
    EXPECT_EQ(panel.requests_[0].filters_[0].extensions_, std::vector<std::string>{"png"});
}

TEST(RunFileDialog, CancelReturnsNoPaths) {
    RecordingPanel panel;
    panel.answer_.status_ = FileDialogStatus::CANCELLED;
    panel.answer_.paths_ = {absolutePath("stale.png")};
    const FileDialogResult result = IRWindow::runFileDialog({}, {}, panel.panel());
    EXPECT_EQ(result.status_, FileDialogStatus::CANCELLED);
    EXPECT_TRUE(result.paths_.empty());
}

TEST(RunFileDialog, ACaptureRunNeverReachesThePanel) {
    RecordingPanel panel;
    panel.answer_ = accepted({absolutePath("a.png")});
    FileDialogHost host;
    host.captureRun_ = true;
    const FileDialogResult result = IRWindow::runFileDialog({}, host, panel.panel());
    EXPECT_EQ(result.status_, FileDialogStatus::REFUSED);
    EXPECT_NE(result.error_.find("capture run"), std::string::npos) << result.error_;
    EXPECT_TRUE(result.paths_.empty());
    EXPECT_TRUE(panel.requests_.empty());
}

TEST(RunFileDialog, EveryUnattendedWindowModeIsRefused) {
    for (const WindowMode mode :
         {WindowMode::BACKGROUND, WindowMode::HIDDEN, WindowMode::OFFSCREEN}) {
        RecordingPanel panel;
        panel.answer_ = accepted({absolutePath("a.png")});
        FileDialogHost host;
        host.windowMode_ = mode;
        const FileDialogResult result = IRWindow::runFileDialog({}, host, panel.panel());
        EXPECT_EQ(result.status_, FileDialogStatus::REFUSED) << IRWindow::windowModeName(mode);
        EXPECT_NE(result.error_.find(IRWindow::windowModeName(mode)), std::string::npos);
        EXPECT_TRUE(panel.requests_.empty());
    }
}

TEST(RunFileDialog, OffTheMainThreadIsRefused) {
    RecordingPanel panel;
    panel.answer_ = accepted({absolutePath("a.png")});
    FileDialogHost offThread;
    offThread.onMainThread_ = false;
    EXPECT_EQ(
        IRWindow::runFileDialog({}, offThread, panel.panel()).status_,
        FileDialogStatus::REFUSED
    );
    EXPECT_TRUE(panel.requests_.empty());
}

TEST(RunFileDialog, AnInvalidRequestFailsEvenInACaptureRun) {
    RecordingPanel panel;
    FileDialogRequest request;
    request.filters_ = {{"Bad", {"a/b"}}};
    FileDialogHost host;
    host.captureRun_ = true;
    const FileDialogResult result = IRWindow::runFileDialog(request, host, panel.panel());
    EXPECT_EQ(result.status_, FileDialogStatus::FAILED);
    EXPECT_NE(result.error_.find("invalid request"), std::string::npos) << result.error_;
    EXPECT_TRUE(panel.requests_.empty());
}

TEST(RunFileDialog, NoPanelIsUnsupported) {
    const FileDialogResult result = IRWindow::runFileDialog({}, {}, {});
    EXPECT_EQ(result.status_, FileDialogStatus::UNSUPPORTED);
    EXPECT_FALSE(result.error_.empty());
}

TEST(RunFileDialog, PanelStatusesPassThroughWithoutPaths) {
    for (const FileDialogStatus status :
         {FileDialogStatus::UNSUPPORTED, FileDialogStatus::FAILED}) {
        RecordingPanel panel;
        panel.answer_.status_ = status;
        panel.answer_.error_ = "why";
        panel.answer_.paths_ = {absolutePath("stale.png")};
        const FileDialogResult result = IRWindow::runFileDialog({}, {}, panel.panel());
        EXPECT_EQ(result.status_, status);
        EXPECT_EQ(result.error_, "why");
        EXPECT_TRUE(result.paths_.empty());
    }
}

TEST(RunFileDialog, ChecksWhatThePanelReturned) {
    RecordingPanel panel;

    panel.answer_ = accepted({});
    EXPECT_EQ(IRWindow::runFileDialog({}, {}, panel.panel()).status_, FileDialogStatus::CANCELLED);

    panel.answer_ = accepted({"relative/a.png"});
    FileDialogResult result = IRWindow::runFileDialog({}, {}, panel.panel());
    EXPECT_EQ(result.status_, FileDialogStatus::FAILED);
    EXPECT_TRUE(result.paths_.empty());

    panel.answer_ = accepted({absolutePath("a.png"), absolutePath("b.png")});
    result = IRWindow::runFileDialog({}, {}, panel.panel());
    EXPECT_EQ(result.status_, FileDialogStatus::FAILED) << "OPEN_FILE takes exactly one path";
    EXPECT_TRUE(result.paths_.empty());

    FileDialogRequest folder;
    folder.mode_ = FileDialogMode::OPEN_FOLDER;
    EXPECT_EQ(IRWindow::runFileDialog(folder, {}, panel.panel()).status_, FileDialogStatus::FAILED);
}

// ---- backend argument builders (pure, so every host covers them) ------------

TEST(FileDialogBackendArguments, WindowsFilterPattern) {
    EXPECT_EQ(IRWindow::detail::windowsFilterPattern({"Images", {"png", "jpg"}}), "*.png;*.jpg");
    EXPECT_EQ(IRWindow::detail::windowsFilterPattern({"All files", {"*"}}), "*.*");
}

TEST(FileDialogBackendArguments, ZenityArgumentsPerMode) {
    FileDialogRequest request;
    EXPECT_EQ(
        IRWindow::detail::zenityArguments(request),
        std::vector<std::string>{"--file-selection"}
    );

    request.mode_ = FileDialogMode::OPEN_FILES;
    request.title_ = "Choose images";
    request.directory_ = "/home/user/pictures";
    request.filters_ = {{"Images | all", {"png", "jpg"}}, {"All files", {"*"}}};
    EXPECT_EQ(
        IRWindow::detail::zenityArguments(request),
        (std::vector<std::string>{
            "--file-selection",
            "--title=Choose images",
            "--multiple",
            "--separator=\n",
            "--filename=/home/user/pictures/",
            "--file-filter=Images / all | *.png *.jpg",
            "--file-filter=All files | *",
        })
    );

    FileDialogRequest folder;
    folder.mode_ = FileDialogMode::OPEN_FOLDER;
    folder.directory_ = "/home/user/";
    EXPECT_EQ(
        IRWindow::detail::zenityArguments(folder),
        (std::vector<std::string>{"--file-selection", "--directory", "--filename=/home/user/"})
    );
}

TEST(FileDialogBackendArguments, ParsesZenityOutput) {
    EXPECT_TRUE(IRWindow::detail::parseZenityOutput("").empty());
    EXPECT_TRUE(IRWindow::detail::parseZenityOutput("\n").empty());
    EXPECT_EQ(
        IRWindow::detail::parseZenityOutput("/a/one.png\n"),
        std::vector<std::string>{"/a/one.png"}
    );
    EXPECT_EQ(
        IRWindow::detail::parseZenityOutput("/a/one.png\n/b/two words.png\n"),
        (std::vector<std::string>{"/a/one.png", "/b/two words.png"})
    );
    EXPECT_EQ(
        IRWindow::detail::parseZenityOutput("/a/one.png\n/b/two.png"),
        (std::vector<std::string>{"/a/one.png", "/b/two.png"})
    );
}

} // namespace
