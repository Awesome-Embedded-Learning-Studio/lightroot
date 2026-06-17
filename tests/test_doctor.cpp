#include <lightroot/doctor.h>
#include <lightroot/exec.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <unistd.h>

namespace fs = std::filesystem;
using namespace lightroot;

namespace {

/// RAII temp directory under the system temp root; removed on destruction.
class TempDir {
  public:
    TempDir() {
        static int counter = 0;
        p_ = fs::temp_directory_path() /
             ("lightroot-test-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
        fs::create_directories(p_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(p_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return p_; }

  private:
    fs::path p_;
};

/// Write an executable shell script `p` with the given body.
void makeSh(const fs::path& p, const std::string& body) {
    {
        std::ofstream of(p);
        of << "#!/bin/sh\n" << body << "\n";
    }
    std::error_code ec;
    fs::permissions(p, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, ec);
}

/// Create a fake x-gcc that reports `triple` for -dumpmachine and a version otherwise.
void makeFakeGcc(const fs::path& dir, std::string_view triple) {
    makeSh(dir / "x-gcc", "case \"$1\" in -dumpmachine) echo '" + std::string(triple) +
                              "' ;; *) echo 'fake-gcc (lightroot) 1.0' ;; esac");
}

ToolchainConfig tcWith(const fs::path& path, std::string target = "aarch64-linux-gnu") {
    ToolchainConfig tc;
    tc.prefix = "x-";
    tc.path = path.string();
    tc.target = std::move(target);
    return tc;
}

} // namespace

TEST(Doctor, FindToolResolvesUnderToolchainPath) {
    TempDir dir;
    makeSh(dir.path() / "x-gcc", "exit 0");

    const auto tc = tcWith(dir.path());
    EXPECT_EQ(findTool(tc, "gcc"), dir.path() / "x-gcc");
    EXPECT_EQ(findTool(tc, "nope"), std::nullopt);
}

TEST(Doctor, CheckSysrootFailPassAndWarn) {
    TempDir dir;

    auto tc = tcWith(dir.path());
    tc.sysroot = (dir.path() / "nope").string(); // nonexistent path
    EXPECT_EQ(checkSysroot(tc).status, CheckStatus::Fail);

    tc.sysroot = dir.path().string(); // existing directory
    EXPECT_EQ(checkSysroot(tc).status, CheckStatus::Pass);

    EXPECT_EQ(checkSysroot(tcWith(dir.path())).status, CheckStatus::Warn); // unspecified
}

TEST(Doctor, CheckTargetMatchAndMismatch) {
    TempDir dir;
    makeFakeGcc(dir.path(), "aarch64-linux-gnu");

    EXPECT_EQ(checkTarget(tcWith(dir.path(), "aarch64-linux-gnu")).status, CheckStatus::Pass);
    EXPECT_EQ(checkTarget(tcWith(dir.path(), "x86_64-linux-gnu")).status, CheckStatus::Fail);
}

TEST(Doctor, CheckTargetWarnsWhenUnspecified) {
    TempDir dir;
    makeFakeGcc(dir.path(), "aarch64-linux-gnu");

    auto tc = tcWith(dir.path(), "");
    EXPECT_EQ(checkTarget(tc).status, CheckStatus::Warn);
}

TEST(Doctor, CheckToolsPassWhenAllPresent) {
    TempDir dir;
    makeFakeGcc(dir.path(), "aarch64-linux-gnu");
    for (auto n : {"x-g++", "x-ar", "x-strip", "x-ld"})
        makeSh(dir.path() / n, "exit 0");

    const auto r = checkTools(tcWith(dir.path()));
    EXPECT_EQ(r.status, CheckStatus::Pass);
    EXPECT_EQ(r.name, "tools");
}

TEST(Doctor, CheckToolsFailsWhenMissing) {
    TempDir dir;
    makeFakeGcc(dir.path(), "aarch64-linux-gnu"); // only gcc present

    const auto r = checkTools(tcWith(dir.path()));
    EXPECT_EQ(r.status, CheckStatus::Fail);
    EXPECT_NE(r.detail.find("x-g++"), std::string::npos);
}

TEST(Doctor, RunDoctorAggregatesAndOrdersChecks) {
    TempDir dir;
    makeFakeGcc(dir.path(), "aarch64-linux-gnu");
    for (auto n : {"x-g++", "x-ar", "x-strip", "x-ld"})
        makeSh(dir.path() / n, "exit 0");

    auto tc = tcWith(dir.path(), "aarch64-linux-gnu");
    tc.sysroot = dir.path().string();

    DoctorOptions opts;
    opts.skipSmoke = true;
    opts.hostTools = {"sh"}; // deterministically present

    const auto rep = runDoctor(tc, opts);
    ASSERT_EQ(rep.checks.size(), 4u);
    EXPECT_EQ(rep.checks[0].name, "tools");
    EXPECT_EQ(rep.checks[1].name, "target");
    EXPECT_EQ(rep.checks[2].name, "sysroot");
    EXPECT_EQ(rep.checks[3].name, "host-tools");
    EXPECT_TRUE(rep.ok());
}

TEST(Doctor, RunDoctorNotOkOnAnyFail) {
    TempDir dir;
    makeFakeGcc(dir.path(), "aarch64-linux-gnu");
    for (auto n : {"x-g++", "x-ar", "x-strip", "x-ld"})
        makeSh(dir.path() / n, "exit 0");

    auto tc = tcWith(dir.path(), "x86_64-linux-gnu"); // target mismatch
    tc.sysroot = dir.path().string();

    DoctorOptions opts;
    opts.skipSmoke = true;
    opts.hostTools = {"sh"};

    const auto rep = runDoctor(tc, opts);
    EXPECT_FALSE(rep.ok());
}

TEST(Doctor, CheckHostToolsIsWellFormed) {
    DoctorOptions opts;
    const auto r = checkHostTools(opts);
    EXPECT_TRUE(r.status == CheckStatus::Pass || r.status == CheckStatus::Warn ||
                r.status == CheckStatus::Fail);
    EXPECT_EQ(r.name, "host-tools");
}

TEST(Doctor, HostToolsReportsFakeroot) {
    DoctorOptions opts;
    opts.hostTools = {"sh"}; // deterministically present
    const auto r = checkHostTools(opts);
    EXPECT_EQ(r.name, "host-tools");
    if (findOnPath("fakeroot")) {
        // Present: must not be flagged as absent, and must not fail the check.
        EXPECT_EQ(r.detail.find("fakeroot absent"), std::string::npos);
    } else {
        EXPECT_NE(r.detail.find("fakeroot absent"), std::string::npos);
        EXPECT_NE(r.status, CheckStatus::Pass);
    }
}

TEST(Doctor, CheckCompileSmokeAgainstRealToolchain) {
    ToolchainConfig probe;
    probe.prefix = "aarch64-linux-gnu-";
    if (!findTool(probe, "gcc"))
        GTEST_SKIP() << "aarch64-linux-gnu-gcc not on PATH";

    ToolchainConfig tc;
    tc.prefix = "aarch64-linux-gnu-";
    tc.target = "aarch64-linux-gnu";
    // Rely on the compiler's default sysroot (verified present on this host).

    const auto r = checkCompileSmoke(tc);
    EXPECT_EQ(r.status, CheckStatus::Pass);
    EXPECT_EQ(r.name, "compile-smoke");
}

TEST(Doctor, CheckCppSmokeAgainstRealToolchain) {
    ToolchainConfig probe;
    probe.prefix = "aarch64-linux-gnu-";
    if (!findTool(probe, "g++"))
        GTEST_SKIP() << "aarch64-linux-gnu-g++ not on PATH";

    ToolchainConfig tc;
    tc.prefix = "aarch64-linux-gnu-";
    tc.target = "aarch64-linux-gnu";

    const auto r = checkCppSmoke(tc);
    EXPECT_EQ(r.status, CheckStatus::Pass);
    EXPECT_EQ(r.name, "c++-smoke");
}

TEST(Doctor, CheckStripAgainstRealToolchain) {
    ToolchainConfig probe;
    probe.prefix = "aarch64-linux-gnu-";
    if (!findTool(probe, "gcc") || !findTool(probe, "strip"))
        GTEST_SKIP() << "aarch64-linux-gnu gcc/strip not on PATH";

    ToolchainConfig tc;
    tc.prefix = "aarch64-linux-gnu-";
    tc.target = "aarch64-linux-gnu";

    const auto r = checkStrip(tc);
    EXPECT_EQ(r.status, CheckStatus::Pass);
    EXPECT_EQ(r.name, "strip");
}

TEST(Doctor, CheckCppSmokeFailsWhenGppMissing) {
    TempDir dir;
    makeSh(dir.path() / "x-gcc", "exit 0"); // gcc present, g++ absent
    const auto r = checkCppSmoke(tcWith(dir.path()));
    EXPECT_EQ(r.status, CheckStatus::Fail);
    EXPECT_NE(r.detail.find("g++"), std::string::npos);
}

TEST(Doctor, CheckStripFailsWhenStripMissing) {
    TempDir dir;
    makeFakeGcc(dir.path(), "aarch64-linux-gnu"); // gcc present, strip absent
    const auto r = checkStrip(tcWith(dir.path()));
    EXPECT_EQ(r.status, CheckStatus::Fail);
    EXPECT_NE(r.detail.find("strip"), std::string::npos);
}

TEST(Doctor, RunDoctorFullAgainstRealToolchain) {
    ToolchainConfig probe;
    probe.prefix = "aarch64-linux-gnu-";
    if (!findTool(probe, "gcc"))
        GTEST_SKIP() << "aarch64-linux-gnu-gcc not on PATH";

    ToolchainConfig tc;
    tc.prefix = "aarch64-linux-gnu-";
    tc.target = "aarch64-linux-gnu";

    const auto rep = runDoctor(tc); // skipSmoke defaults to false -> 7 checks
    ASSERT_EQ(rep.checks.size(), 7u);
    EXPECT_EQ(rep.checks[0].name, "tools");
    EXPECT_EQ(rep.checks[1].name, "target");
    EXPECT_EQ(rep.checks[2].name, "sysroot");
    EXPECT_EQ(rep.checks[3].name, "compile-smoke");
    EXPECT_EQ(rep.checks[4].name, "c++-smoke");
    EXPECT_EQ(rep.checks[5].name, "strip");
    EXPECT_EQ(rep.checks[6].name, "host-tools");
    EXPECT_TRUE(rep.ok());
}
