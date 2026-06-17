#include <lightroot/exec.h>
#include <lightroot/executor.h>
#include <lightroot/manifest.h>
#include <lightroot/registry.h>
#include <lightroot/resolver.h>
#include <lightroot/types.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <unistd.h>

namespace fs = std::filesystem;
using namespace lightroot;

namespace {

/// RAII temp directory; removed on destruction.
class TempDir {
  public:
    TempDir() {
        static int counter = 0;
        p_ = fs::temp_directory_path() / ("lightroot-exec-plan-" + std::to_string(::getpid()) +
                                          "-" + std::to_string(counter++));
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

void writeFile(const fs::path& p, const std::string& body) {
    std::ofstream of(p);
    of << body;
}

std::string sha256Of(const fs::path& p) {
    std::string out;
    runCapture({"sha256sum", p.string()}, {}, out);
    const auto sp = out.find(' ');
    return sp == std::string::npos ? out : out.substr(0, sp);
}

/// A trivial target package built from a local source dir via the script backend.
Package makeLocalPackage(std::string name, const fs::path& src) {
    Package p;
    p.name = std::move(name);
    p.scope = Scope::Target;
    SourceInfo si;
    si.kind = "local";
    si.url = src.string();
    p.source = si;
    return p;
}

BuildPlan singleStepPlan(std::string name) {
    BuildPlan plan;
    BuildStep s;
    s.scope = Scope::Target;
    s.name = std::move(name);
    plan.steps.push_back(std::move(s));
    return plan;
}

ExecutorConfig basicConfig(const fs::path& out) {
    ExecutorConfig cfg;
    cfg.toolchain.prefix = "aarch64-linux-gnu-";
    cfg.toolchain.target = "aarch64-linux-gnu";
    cfg.output_dir = out;
    return cfg;
}

} // namespace

TEST(Executor, LocalSourceInstallsToTarget) {
    TempDir work;
    const auto src = work.path() / "src";
    fs::create_directories(src);
    writeFile(src / "hello", "hi");
    writeFile(src / "Makefile", "install:\n\tinstall -D hello $(DESTDIR)/usr/bin/hello\n");

    Package p = makeLocalPackage("hello", src);
    BuildInfo bi;
    bi.backend = "script";
    bi.phases["install_target"].run = {"make install"};
    p.build = bi;

    PackageRegistry reg;
    reg.add(std::move(p));
    const auto out = work.path() / "out";
    const auto res = executePlan(reg, singleStepPlan("hello"), basicConfig(out));

    ASSERT_TRUE(res.ok()) << "failed: " << (res.failed.empty() ? "" : res.failed[0]);
    ASSERT_EQ(res.built.size(), 1u);
    EXPECT_TRUE(fs::exists(out / "target/usr/bin/hello"));
    EXPECT_TRUE(fs::exists(out / "logs/events.jsonl"));
    EXPECT_TRUE(fs::exists(out / "logs/packages/hello/install_target.log"));
    EXPECT_FALSE(res.tar_path.empty());
    EXPECT_TRUE(fs::exists(res.tar_path));
}

TEST(Executor, RelativeLocalSourceResolvesAgainstManifest) {
    TempDir work;
    const auto manifest = work.path() / "pkg.toml"; // origin for relative resolution
    writeFile(manifest, "[package]\nname = \"p\"\n");
    const auto src = work.path() / "src"; // relative url "src" -> work.path()/src
    fs::create_directories(src);
    writeFile(src / "hello", "hi");
    writeFile(src / "Makefile", "install:\n\tinstall -D hello $(DESTDIR)/usr/bin/hello\n");

    Package p = makeLocalPackage("p", src);
    p.origin = manifest;
    p.source->url = "src"; // relative -> resolved against manifest dir
    BuildInfo bi;
    bi.backend = "script";
    bi.phases["install_target"].run = {"make install"};
    p.build = bi;

    PackageRegistry reg;
    reg.add(std::move(p));
    const auto out = work.path() / "out";
    const auto res = executePlan(reg, singleStepPlan("p"), basicConfig(out));

    ASSERT_TRUE(res.ok());
    EXPECT_TRUE(fs::exists(out / "target/usr/bin/hello"));
}

TEST(Executor, LocalSourceCrossCompilesAarch64) {
    std::string cap;
    if (!runCapture({"aarch64-linux-gnu-gcc", "--version"}, {}, cap).ok())
        GTEST_SKIP() << "aarch64-linux-gnu-gcc not on PATH";

    TempDir work;
    const auto src = work.path() / "src";
    fs::create_directories(src);
    writeFile(src / "hello.c", "int main(void){return 0;}\n");
    writeFile(src / "Makefile", "hello: hello.c\n\t$(CC) --static -o hello hello.c\n"
                                "install: hello\n\tinstall -D hello $(DESTDIR)/usr/bin/hello\n");

    Package p = makeLocalPackage("hello", src);
    BuildInfo bi;
    bi.backend = "script";
    bi.phases["build"].run = {"make"};
    bi.phases["install_target"].run = {"make install"};
    p.build = bi;

    PackageRegistry reg;
    reg.add(std::move(p));
    auto cfg = basicConfig(work.path() / "out");
    cfg.toolchain.sysroot = "/usr/aarch64-linux-gnu";
    const auto res = executePlan(reg, singleStepPlan("hello"), cfg);

    ASSERT_TRUE(res.ok());
    const auto bin = work.path() / "out/target/usr/bin/hello";
    ASSERT_TRUE(fs::exists(bin)) << "installed binary missing";
}

TEST(Executor, ArchiveSourceFetchesVerifiesExtractsInstalls) {
    TempDir work;
    const auto srcroot = work.path() / "pkg-1.0";
    fs::create_directories(srcroot);
    writeFile(srcroot / "hello", "hi");
    writeFile(srcroot / "Makefile", "install:\n\tinstall -D hello $(DESTDIR)/usr/bin/hello\n");

    const auto tarball = work.path() / "pkg-1.0.tar.gz";
    std::string tarcap;
    ASSERT_TRUE(runCapture({"tar", "czf", tarball.string(), "-C", work.path().string(), "pkg-1.0"},
                           {}, tarcap)
                    .ok());
    const auto sha = sha256Of(tarball);
    ASSERT_FALSE(sha.empty());

    Package p;
    p.name = "pkg";
    p.scope = Scope::Target;
    SourceInfo si;
    si.kind = "archive";
    si.url = "file://" + tarball.string();
    si.sha256 = sha;
    p.source = si;
    BuildInfo bi;
    bi.backend = "script";
    bi.phases["install_target"].run = {"make install"};
    p.build = bi;

    PackageRegistry reg;
    reg.add(std::move(p));
    const auto out = work.path() / "out";
    const auto res = executePlan(reg, singleStepPlan("pkg"), basicConfig(out));

    ASSERT_TRUE(res.ok()) << "failed: " << (res.failed.empty() ? "" : res.failed[0]);
    EXPECT_TRUE(fs::exists(out / "target/usr/bin/hello"));
    EXPECT_TRUE(fs::exists(out / "downloads/pkg-1.0.tar.gz"));
}

TEST(Executor, FailingPhaseIsRecordedAndStops) {
    TempDir work;
    Package p;
    p.name = "bad";
    p.scope = Scope::Target;
    BuildInfo bi;
    bi.backend = "script";
    bi.phases["build"].run = {"false"};
    p.build = bi;

    PackageRegistry reg;
    reg.add(std::move(p));
    const auto out = work.path() / "out";
    const auto res = executePlan(reg, singleStepPlan("bad"), basicConfig(out));

    EXPECT_FALSE(res.ok());
    ASSERT_EQ(res.failed.size(), 1u);
    const auto events = readFile(res.events_log);
    EXPECT_NE(events.find("\"status\":\"fail\""), std::string::npos);
}

TEST(Executor, DefaultPhasesMakeKconfigShape) {
    const auto ph = defaultPhases("make-kconfig");
    ASSERT_EQ(ph.size(), 3u);
    ASSERT_EQ(ph.count("configure"), 1u);
    ASSERT_EQ(ph.count("build"), 1u);
    ASSERT_EQ(ph.count("install_target"), 1u);

    const auto& cfg = ph.at("configure").run;
    ASSERT_FALSE(cfg.empty());
    EXPECT_NE(cfg[0].find("defconfig"), std::string::npos);
    bool has_static = false, has_tc_off = false;
    for (const auto& c : cfg) {
        if (c.find("CONFIG_STATIC=y") != std::string::npos)
            has_static = true;
        if (c.find("# CONFIG_TC is not set") != std::string::npos)
            has_tc_off = true;
    }
    EXPECT_TRUE(has_static);
    EXPECT_TRUE(has_tc_off);

    EXPECT_NE(ph.at("build").run[0].find("-j$LIGHTROOT_JOBS"), std::string::npos);
    EXPECT_NE(ph.at("install_target").run[0].find("CONFIG_PREFIX"), std::string::npos);
}

TEST(Executor, DefaultPhasesMakeHasConfigureBuildInstall) {
    const auto ph = defaultPhases("make");
    ASSERT_EQ(ph.size(), 3u);
    EXPECT_NE(ph.at("configure").run[0].find("--host="), std::string::npos);
    EXPECT_NE(ph.at("install_target").run[0].find("DESTDIR"), std::string::npos);
}

TEST(Executor, DefaultPhasesUnknownIsEmpty) {
    EXPECT_TRUE(defaultPhases("nonsense-backend").empty());
    EXPECT_TRUE(defaultPhases("").empty());
}

namespace {

/// Whether fakeroot is available on PATH (assemble's hardware-correct mode needs it).
bool hasFakeroot() {
    return findOnPath("fakeroot").has_value();
}

/// From `tar tvf`, return the owner/group token (e.g. "0/0") for the entry whose
/// line contains `name`, or empty if not found.
std::string tarOwnerGroup(const fs::path& tar, std::string_view name) {
    std::string out;
    runCapture({"tar", "tvf", tar.string()}, {}, out);
    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find(std::string(name)) == std::string::npos)
            continue;
        std::istringstream ls(line);
        std::string perms, og;
        ls >> perms >> og;
        return og;
    }
    return {};
}

/// From `tar tvf`, return the full listing line for `name`, or empty if not found.
std::string tarListingLine(const fs::path& tar, std::string_view name) {
    std::string out;
    runCapture({"tar", "tvf", tar.string()}, {}, out);
    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line))
        if (line.find(std::string(name)) != std::string::npos)
            return line;
    return {};
}

/// A one-step plan that installs /usr/bin/hello via a local-source script package.
ExecutorResult buildHelloPlan(const fs::path& out, std::vector<std::string> formats) {
    TempDir work;
    const auto src = work.path() / "src";
    fs::create_directories(src);
    writeFile(src / "hello", "hi");
    writeFile(src / "Makefile", "install:\n\tinstall -D hello $(DESTDIR)/usr/bin/hello\n");

    Package p = makeLocalPackage("hello", src);
    BuildInfo bi;
    bi.backend = "script";
    bi.phases["install_target"].run = {"make install"};
    p.build = bi;

    PackageRegistry reg;
    reg.add(std::move(p));
    auto cfg = basicConfig(out);
    cfg.image_formats = std::move(formats);
    return executePlan(reg, singleStepPlan("hello"), cfg);
}

} // namespace

TEST(Executor, AssembleProducesRootOwnedTar) {
    if (!hasFakeroot())
        GTEST_SKIP() << "fakeroot not on PATH";

    TempDir work;
    const auto out = work.path() / "out";
    const auto res = buildHelloPlan(out, {"tar"});
    ASSERT_TRUE(res.ok()) << (res.failed.empty() ? "" : res.failed[0]);
    ASSERT_FALSE(res.tar_path.empty());

    // Under fakeroot the assemble chowns to root; tar records uid/gid 0. The
    // long-listing owner/group token is "0/0" (numeric) or "root/root" (resolved).
    const auto og = tarOwnerGroup(res.tar_path, "./usr/bin/hello");
    ASSERT_FALSE(og.empty());
    const auto slash = og.find('/');
    ASSERT_NE(slash, std::string::npos);
    const auto owner = og.substr(0, slash);
    const auto group = og.substr(slash + 1);
    EXPECT_TRUE(owner == "0" || owner == "root") << "owner=" << owner;
    EXPECT_TRUE(group == "0" || group == "root") << "group=" << group;
}

TEST(Executor, AssembleIncludesDeviceNode) {
    if (!hasFakeroot())
        GTEST_SKIP() << "fakeroot not on PATH";

    TempDir work;
    const auto out = work.path() / "out";
    const auto res = buildHelloPlan(out, {"tar"});
    ASSERT_TRUE(res.ok());
    ASSERT_FALSE(res.tar_path.empty());

    // The default device table creates /dev/null as a char device; tar lists it
    // with a leading 'c' typeflag.
    const auto line = tarListingLine(res.tar_path, "./dev/null");
    ASSERT_FALSE(line.empty()) << "no /dev/null entry in rootfs tar";
    EXPECT_EQ(line.front(), 'c');
}

TEST(Executor, AssembleProducesRequestedFormats) {
    if (!hasFakeroot())
        GTEST_SKIP() << "fakeroot not on PATH";

    TempDir work;
    const auto out = work.path() / "out";
    const auto res = buildHelloPlan(out, {"tar", "cpio"});
    ASSERT_TRUE(res.ok());
    ASSERT_FALSE(res.tar_path.empty());
    EXPECT_TRUE(fs::exists(out / "rootfs.tar.gz"));
    EXPECT_TRUE(fs::exists(out / "rootfs.cpio.gz"));
    EXPECT_GE(res.image_paths.size(), 2u);
}
