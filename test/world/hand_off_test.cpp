#include <irreden/world/hand_off.hpp>
#include <irreden/world/main_loop_frame.hpp>

#include <gtest/gtest.h>

#include "../common/env_var.hpp"

#include <functional>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <string>
#include <vector>

namespace {

using IREngine::ChildExit;
using IREngine::HandOffEnvironment;
using IREngine::HandOffMidiBracket;
using IREngine::HandOffOutcome;
using IREngine::HandOffPlatform;
using IREngine::HandOffRequest;
using IREngine::runHandOff;

const std::string kWrapper = "/engine/tools/bin/ir-run";
const std::string kAcquire = "/engine/tools/bin/ir-acquire";
const std::string kToken = "0123456789abcdef0123456789abcdef";

// Records every step in order and answers each launch from a script. A step
// also records the loop's update/render counts at that moment, so a test can
// prove no pipeline ran while the process was handed off.
class StubPlatform : public HandOffPlatform {
  public:
    struct Step {
        std::string kind_;
        std::vector<std::string> argv_;
        int updatesSeen_ = 0;
        int rendersSeen_ = 0;
    };

    void drainGpu() override {
        record("drain", {});
    }

    ChildExit runToExit(const std::vector<std::string> &argv) override {
        const std::string kind = classify(argv);
        record(kind, argv);
        if (kind == "release") {
            return release_;
        }
        if (kind == "reacquire") {
            return reacquire_;
        }
        if (childHook_) {
            childHook_(argv);
        }
        return child_;
    }

    std::vector<std::string> kinds() const {
        std::vector<std::string> out;
        for (const Step &step : steps_) {
            out.push_back(step.kind_);
        }
        return out;
    }

    int count(const std::string &kind) const {
        int n = 0;
        for (const Step &step : steps_) {
            n += step.kind_ == kind ? 1 : 0;
        }
        return n;
    }

    const Step &step(const std::string &kind) const {
        for (const Step &s : steps_) {
            if (s.kind_ == kind) {
                return s;
            }
        }
        static const Step kMissing{};
        return kMissing;
    }

    ChildExit release_{true, 0, ""};
    ChildExit child_{true, 0, ""};
    ChildExit reacquire_{true, 0, ""};
    const int *updates_ = nullptr;
    const int *renders_ = nullptr;
    std::vector<Step> steps_;
    std::function<void(const std::vector<std::string> &)> childHook_;

  private:
    static std::string classify(const std::vector<std::string> &argv) {
        for (const std::string &arg : argv) {
            if (arg == "--lease-release") {
                return "release";
            }
            if (arg == "--lease-reacquire") {
                return "reacquire";
            }
        }
        return "child";
    }

    void record(const std::string &kind, const std::vector<std::string> &argv) {
        steps_.push_back(
            {kind, argv, updates_ != nullptr ? *updates_ : 0, renders_ != nullptr ? *renders_ : 0}
        );
    }
};

class StubMidiBracket : public HandOffMidiBracket {
  public:
    void release() override {
        events_.push_back("midi-release");
        ++releaseCount_;
    }
    std::vector<std::string> restore() override {
        events_.push_back("midi-restore");
        ++restoreCount_;
        return lost_;
    }

    int releaseCount_ = 0;
    int restoreCount_ = 0;
    std::vector<std::string> lost_;
    std::vector<std::string> events_;
};

HandOffEnvironment wrapperOnly() {
    HandOffEnvironment environment;
    environment.wrapperPath_ = kWrapper;
    return environment;
}

HandOffEnvironment withLease() {
    HandOffEnvironment environment = wrapperOnly();
    environment.leaseToken_ = kToken;
    environment.acquireToolPath_ = kAcquire;
    environment.heldVerb_ = "gpu";
    return environment;
}

HandOffRequest takeRequest() {
    return HandOffRequest{{"IRLuaWidgetsDemo", "--auto-screenshot", "3"}};
}

TEST(HandOffControllerTest, LeaseHolderDrainsReleasesLaunchesWaitsReacquires) {
    StubPlatform platform;
    const HandOffOutcome outcome = runHandOff(takeRequest(), withLease(), platform);

    EXPECT_EQ(
        platform.kinds(),
        (std::vector<std::string>{"drain", "release", "child", "reacquire"})
    );
    EXPECT_EQ(platform.count("child"), 1);
    EXPECT_EQ(platform.count("release"), 1);
    EXPECT_EQ(platform.count("reacquire"), 1);
    EXPECT_EQ(
        platform.step("child").argv_,
        (std::vector<std::string>{kWrapper, "--", "IRLuaWidgetsDemo", "--auto-screenshot", "3"})
    );
    EXPECT_EQ(
        platform.step("release").argv_,
        (std::vector<std::string>{kAcquire, "--lease-release", kToken})
    );
    EXPECT_EQ(
        platform.step("reacquire").argv_,
        (std::vector<std::string>{kAcquire, "--lease-reacquire", kToken})
    );
    EXPECT_TRUE(outcome.result_.ok_);
    EXPECT_TRUE(outcome.result_.supported_);
    ASSERT_TRUE(outcome.result_.exitCode_.has_value());
    EXPECT_EQ(*outcome.result_.exitCode_, 0);
    EXPECT_TRUE(outcome.result_.error_.empty());
    EXPECT_TRUE(outcome.suspended_);
    EXPECT_FALSE(outcome.leaseLost_);
}

TEST(HandOffControllerTest, RunWithoutLeaseStillDrainsAndLaunchesThroughTheWrapper) {
    StubPlatform platform;
    const HandOffOutcome outcome = runHandOff(takeRequest(), wrapperOnly(), platform);

    EXPECT_EQ(platform.kinds(), (std::vector<std::string>{"drain", "child"}));
    EXPECT_EQ(platform.step("child").argv_.front(), kWrapper);
    EXPECT_TRUE(outcome.result_.ok_);
    EXPECT_TRUE(outcome.suspended_);
}

TEST(HandOffTest, FixedBuildDirArgv) {
    const std::filesystem::path rawRoot =
        std::filesystem::temp_directory_path() / "ir_handoff_build_dir";
    std::filesystem::remove_all(rawRoot);
    std::filesystem::create_directories(rawRoot / "build");
    const std::filesystem::path root = std::filesystem::weakly_canonical(rawRoot);
    IRTest::ScopedEnv parentBuildDir("IRREDEN_BUILD_DIR", "/different/parent/build");
    StubPlatform platform;
    platform.childHook_ = [](const std::vector<std::string> &argv) {
        const auto result = std::ranges::find(argv, "--result-file");
        ASSERT_NE(result, argv.end());
        const std::filesystem::path resultPath = *std::next(result);
        std::filesystem::create_directories(resultPath.parent_path());
        std::ofstream output(resultPath);
        output
            << R"({"verdict":"C\u004cEAN","exe":"IRDemo","exit":0,"signal":null,"argv":["IRDemo"],"at":"now"})";
    };
    HandOffRequest request{{"IRDemo", "--flag"}};
    const HandOffOutcome outcome = runHandOff(request, wrapperOnly(), platform, root);

    const std::filesystem::path build = std::filesystem::weakly_canonical(root / "build");
    EXPECT_EQ(
        platform.step("child").argv_,
        (std::vector<std::string>{
            kWrapper,
            "--build-dir",
            build.string(),
            "--result-file",
            (build / "ir-run-results" / "IRDemo.last.json").string(),
            "--",
            "IRDemo",
            "--flag"
        })
    );
    ASSERT_TRUE(outcome.result_.verdict_.has_value());
    EXPECT_EQ(*outcome.result_.verdict_, "CLEAN");
    std::filesystem::remove_all(rawRoot);
}

TEST(HandOffTest, DoesNotReuseAStaleResult) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "ir_handoff_stale_result";
    const std::filesystem::path result = root / "build" / "ir-run-results" / "IRDemo.last.json";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(result.parent_path());
    {
        std::ofstream output(result);
        output << R"({"verdict":"CLEAN"})";
    }
    StubPlatform platform;
    const HandOffOutcome outcome =
        runHandOff(HandOffRequest{{"IRDemo"}}, wrapperOnly(), platform, root);

    EXPECT_TRUE(outcome.result_.ok_);
    EXPECT_FALSE(outcome.result_.verdict_.has_value());
    EXPECT_FALSE(std::filesystem::exists(result));
    std::filesystem::remove_all(root);
}

TEST(HandOffTest, OversizedResultFileYieldsNoVerdict) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "ir_handoff_oversized_result";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "build");
    StubPlatform platform;
    platform.childHook_ = [](const std::vector<std::string> &argv) {
        const auto result = std::ranges::find(argv, "--result-file");
        ASSERT_NE(result, argv.end());
        const std::filesystem::path resultPath = *std::next(result);
        std::filesystem::create_directories(resultPath.parent_path());
        std::ofstream output(resultPath);
        output << R"({"verdict":"CLEAN","pad":")" << std::string(64 * 1024, 'x') << R"("})";
    };
    const HandOffOutcome outcome =
        runHandOff(HandOffRequest{{"IRDemo"}}, wrapperOnly(), platform, root);

    EXPECT_TRUE(outcome.result_.ok_);
    ASSERT_TRUE(outcome.result_.exitCode_.has_value());
    EXPECT_EQ(*outcome.result_.exitCode_, 0);
    EXPECT_FALSE(outcome.result_.verdict_.has_value());
    std::filesystem::remove_all(root);
}

TEST(HandOffTest, WithoutToolRootUsesNoBuildDirOrResultFile) {
    StubPlatform platform;
    const HandOffOutcome outcome = runHandOff(HandOffRequest{{"IRDemo"}}, wrapperOnly(), platform);

    EXPECT_TRUE(outcome.result_.ok_);
    EXPECT_FALSE(outcome.result_.verdict_.has_value());
    EXPECT_EQ(platform.step("child").argv_, (std::vector<std::string>{kWrapper, "--", "IRDemo"}));
}

TEST(HandOffTest, InvalidTargetRefusesBeforeDrain) {
    for (const std::string &target : {"--build-dir", "a/b", "IR*", "NUL.txt"}) {
        StubPlatform platform;
        EXPECT_FALSE(runHandOff(HandOffRequest{{target}}, wrapperOnly(), platform).result_.ok_);
        EXPECT_TRUE(platform.steps_.empty()) << target;
    }
}

TEST(HandOffControllerTest, NativeWindowsRunsTheWrapperAndLeaseVerbsThroughBash) {
    HandOffEnvironment environment = withLease();
    environment.shellPath_ = "C:\\msys64\\usr\\bin\\bash.exe";
    environment.shellRequired_ = true;
    StubPlatform platform;
    runHandOff(takeRequest(), environment, platform);

    EXPECT_EQ(
        platform.step("child").argv_,
        (std::vector<std::string>{
            environment.shellPath_,
            kWrapper,
            "--",
            "IRLuaWidgetsDemo",
            "--auto-screenshot",
            "3"
        })
    );
    EXPECT_EQ(
        platform.step("release").argv_,
        (std::vector<std::string>{environment.shellPath_, kAcquire, "--lease-release", kToken})
    );
}

TEST(HandOffControllerTest, ChildThatFailsToStartResumesAfterReacquireWithTheError) {
    StubPlatform platform;
    platform.child_ = ChildExit{false, -1, "posix_spawn failed: No such file or directory"};
    const HandOffOutcome outcome = runHandOff(takeRequest(), withLease(), platform);

    EXPECT_EQ(
        platform.kinds(),
        (std::vector<std::string>{"drain", "release", "child", "reacquire"})
    );
    EXPECT_FALSE(outcome.result_.ok_);
    EXPECT_TRUE(outcome.result_.supported_);
    EXPECT_FALSE(outcome.result_.exitCode_.has_value());
    EXPECT_NE(outcome.result_.error_.find("No such file"), std::string::npos);
    EXPECT_FALSE(outcome.leaseLost_);
}

TEST(HandOffControllerTest, NonzeroChildExitIsReportedAfterResuming) {
    StubPlatform platform;
    platform.child_ = ChildExit{true, 3, ""};
    const HandOffOutcome outcome = runHandOff(takeRequest(), withLease(), platform);

    EXPECT_EQ(platform.count("reacquire"), 1);
    EXPECT_FALSE(outcome.result_.ok_);
    ASSERT_TRUE(outcome.result_.exitCode_.has_value());
    EXPECT_EQ(*outcome.result_.exitCode_, 3);
    EXPECT_NE(outcome.result_.error_.find("exited with 3"), std::string::npos);
}

TEST(MidiHandOffControllerTest, OptInBracketRestoresOnceOnEveryPostCloseOutcome) {
    for (const ChildExit child :
         {ChildExit{true, 0, ""}, ChildExit{false, -1, "spawn"}, ChildExit{true, 3, ""}}) {
        StubPlatform platform;
        platform.child_ = child;
        StubMidiBracket midi;
        midi.lost_ = {"missing"};
        HandOffRequest request = takeRequest();
        request.releaseMidi_ = true;

        const HandOffOutcome outcome =
            runHandOff(request, wrapperOnly(), platform, std::nullopt, &midi);

        EXPECT_EQ(midi.releaseCount_, 1);
        EXPECT_EQ(midi.restoreCount_, 1);
        EXPECT_EQ(outcome.result_.midiLost_, (std::vector<std::string>{"missing"}));
    }
}

TEST(MidiHandOffControllerTest, OptOutAndPreflightRefusalDoNotTouchMidi) {
    StubPlatform platform;
    StubMidiBracket midi;
    runHandOff(takeRequest(), wrapperOnly(), platform, std::nullopt, &midi);
    HandOffRequest invalid{{"not/a/target"}, true};
    runHandOff(invalid, wrapperOnly(), platform, std::nullopt, &midi);
    EXPECT_EQ(midi.releaseCount_, 0);
    EXPECT_EQ(midi.restoreCount_, 0);
}

TEST(MidiHandOffControllerTest, RestoresOnceWhenLeaseReleaseOrReacquireFails) {
    for (const bool failRelease : {true, false}) {
        StubPlatform platform;
        if (failRelease) {
            platform.release_ = ChildExit{true, 1, ""};
        } else {
            platform.reacquire_ = ChildExit{true, 1, ""};
        }
        StubMidiBracket midi;
        HandOffRequest request = takeRequest();
        request.releaseMidi_ = true;

        const HandOffOutcome outcome =
            runHandOff(request, withLease(), platform, std::nullopt, &midi);

        EXPECT_EQ(midi.releaseCount_, 1);
        EXPECT_EQ(midi.restoreCount_, 1);
        EXPECT_EQ(outcome.leaseLost_, !failRelease);
    }
}

TEST(HandOffControllerTest, FailedReleaseLaunchesNothing) {
    StubPlatform platform;
    platform.release_ = ChildExit{true, 1, ""};
    const HandOffOutcome outcome = runHandOff(takeRequest(), withLease(), platform);

    EXPECT_EQ(platform.kinds(), (std::vector<std::string>{"drain", "release"}));
    EXPECT_FALSE(outcome.result_.ok_);
    EXPECT_TRUE(outcome.result_.supported_);
    EXPECT_FALSE(outcome.leaseLost_);
    EXPECT_NE(outcome.result_.error_.find("release"), std::string::npos);
}

TEST(HandOffControllerTest, FailedReacquireReportsTheLeaseLost) {
    StubPlatform platform;
    platform.reacquire_ = ChildExit{true, 1, ""};
    const HandOffOutcome outcome = runHandOff(takeRequest(), withLease(), platform);

    EXPECT_TRUE(outcome.leaseLost_);
    EXPECT_FALSE(outcome.result_.ok_);
    EXPECT_NE(outcome.result_.error_.find("reacquire"), std::string::npos);
}

TEST(HandOffControllerTest, WithoutTheWrapperNothingHappens) {
    for (const HandOffEnvironment &environment :
         {HandOffEnvironment{}, HandOffEnvironment{kWrapper, "", true, kToken, kAcquire, "gpu"}}) {
        StubPlatform platform;
        const HandOffOutcome outcome = runHandOff(takeRequest(), environment, platform);

        EXPECT_TRUE(platform.steps_.empty());
        EXPECT_FALSE(outcome.result_.ok_);
        EXPECT_FALSE(outcome.result_.supported_);
        EXPECT_FALSE(outcome.result_.exitCode_.has_value());
        EXPECT_FALSE(outcome.suspended_);
    }
}

TEST(HandOffControllerTest, BenchmarkRunRefusesAProfilingChildBeforeDraining) {
    HandOffEnvironment environment = withLease();
    environment.heldVerb_ = "benchmark";
    for (const std::string &profileArg : {"--auto-profile", "--auto-profile=5"}) {
        StubPlatform platform;
        const HandOffOutcome outcome =
            runHandOff(HandOffRequest{{"IRShapeDebug", profileArg, "5"}}, environment, platform);
        EXPECT_TRUE(platform.steps_.empty());
        EXPECT_TRUE(outcome.result_.supported_);
        EXPECT_FALSE(outcome.result_.ok_);
        EXPECT_NE(outcome.result_.error_.find("auto-profile"), std::string::npos);
    }
    StubPlatform capture;
    runHandOff(takeRequest(), environment, capture);
    EXPECT_EQ(capture.count("child"), 1);
}

TEST(HandOffControllerTest, EmptyArgvLaunchesNothing) {
    StubPlatform platform;
    const HandOffOutcome outcome = runHandOff(HandOffRequest{}, withLease(), platform);
    EXPECT_TRUE(platform.steps_.empty());
    EXPECT_FALSE(outcome.result_.ok_);
}

TEST(HandOffControllerTest, ArgvWithANulByteLaunchesNothing) {
    for (const std::string &arg :
         {std::string{"--x\0y", 5}, std::string{"\0", 1}, std::string{"IRDemo\0", 7}}) {
        StubPlatform platform;
        const HandOffOutcome outcome =
            runHandOff(HandOffRequest{{"IRShapeDebug", arg}}, withLease(), platform);
        EXPECT_TRUE(platform.steps_.empty());
        EXPECT_FALSE(outcome.suspended_);
        EXPECT_FALSE(outcome.result_.ok_);
        EXPECT_NE(outcome.result_.error_.find("NUL"), std::string::npos);
    }
}

TEST(HandOffControllerTest, EnvironmentReadsWhatTheWrapperExported) {
    IRTest::ScopedEnv wrapper("IR_RUN_WRAPPER", kWrapper.c_str());
    IRTest::ScopedEnv token("IR_RUN_LOCK_TOKEN", kToken.c_str());
    IRTest::ScopedEnv tool("IR_ACQUIRE_TOOL", kAcquire.c_str());
    IRTest::ScopedEnv verb("IR_ACQUIRE_HELD_VERB", "benchmark");
    IRTest::ScopedEnv shell("IR_RUN_BASH", nullptr);
    const HandOffEnvironment environment = HandOffEnvironment::fromProcess();

    EXPECT_EQ(environment.wrapperPath_, kWrapper);
    EXPECT_EQ(environment.leaseToken_, kToken);
    EXPECT_EQ(environment.acquireToolPath_, kAcquire);
    EXPECT_EQ(environment.heldVerb_, "benchmark");
    EXPECT_TRUE(environment.shellPath_.empty());
#ifdef _WIN32
    EXPECT_TRUE(environment.shellRequired_);
#else
    EXPECT_FALSE(environment.shellRequired_);
#endif
}

TEST(HandOffControllerTest, WindowsCommandLineRoundTripsArgumentBoundaries) {
    using IREngine::detail::windowsCommandLine;
    EXPECT_EQ(windowsCommandLine({"bash.exe", "ir-run", "IRDemo"}), "bash.exe ir-run IRDemo");
    EXPECT_EQ(windowsCommandLine({"a b", ""}), "\"a b\" \"\"");
    EXPECT_EQ(windowsCommandLine({"say \"hi\""}), "\"say \\\"hi\\\"\"");
    EXPECT_EQ(windowsCommandLine({"C:\\dir with space\\"}), "\"C:\\dir with space\\\\\"");
    EXPECT_EQ(windowsCommandLine({"C:\\plain\\path"}), "C:\\plain\\path");
    EXPECT_EQ(windowsCommandLine({"a\\\"b c"}), "\"a\\\\\\\"b c\"");
    EXPECT_EQ(windowsCommandLine({"caf\xc3\xa9 \xe2\x9c\x93"}), "\"caf\xc3\xa9 \xe2\x9c\x93\"");
}

// A main-loop frame with stub stages; `input()` runs the frame's INPUT hook
// the way a Lua widget callback does inside World::input().
struct StubStages {
    int pendingTicks_ = 0;
    int updates_ = 0;
    int renders_ = 0;
    bool halted_ = false;
    std::function<void()> onInput_;

    bool shouldUpdate() {
        return pendingTicks_ > 0;
    }
    void pollEvents() {}
    void input() {
        if (onInput_) {
            onInput_();
        }
    }
    bool holdForFirstInput() {
        return false;
    }
    void update() {
        --pendingTicks_;
        ++updates_;
    }
    void render() {
        ++renders_;
    }
    bool halted() const {
        return halted_;
    }
};

TEST(HandOffMainLoopTest, NoPipelineRunsWhileHandedOff) {
    StubStages stages;
    stages.pendingTicks_ = 3;
    StubPlatform platform;
    platform.updates_ = &stages.updates_;
    platform.renders_ = &stages.renders_;
    bool handedOff = false;
    stages.onInput_ = [&]() {
        if (!handedOff) {
            handedOff = true;
            runHandOff(takeRequest(), withLease(), platform);
        }
    };

    const std::uint32_t ticks = IREngine::runMainLoopFrame(stages);

    ASSERT_EQ(platform.steps_.size(), 4u);
    for (const StubPlatform::Step &step : platform.steps_) {
        EXPECT_EQ(step.updatesSeen_, 0) << step.kind_;
        EXPECT_EQ(step.rendersSeen_, 0) << step.kind_;
    }
    EXPECT_EQ(ticks, 3u);
    EXPECT_EQ(stages.renders_, 1);
}

TEST(HandOffMainLoopTest, LostLeaseRunsNoFurtherUpdateOrRender) {
    StubStages stages;
    stages.pendingTicks_ = 3;
    StubPlatform platform;
    platform.reacquire_ = ChildExit{true, 1, ""};
    stages.onInput_ = [&]() {
        stages.halted_ = runHandOff(takeRequest(), withLease(), platform).leaseLost_;
    };

    const std::uint32_t ticks = IREngine::runMainLoopFrame(stages);

    EXPECT_TRUE(stages.halted_);
    EXPECT_EQ(ticks, 0u);
    EXPECT_EQ(stages.updates_, 0);
    EXPECT_EQ(stages.renders_, 0);
}

} // namespace
