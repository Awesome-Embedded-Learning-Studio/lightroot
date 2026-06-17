#include <lightroot/exec.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
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
        p_ = fs::temp_directory_path() /
             ("lightroot-exec-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
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

} // namespace

TEST(Exec, RunCommandWritesCombinedOutputToLog) {
    TempDir d;
    const auto log = d.path() / "out.log";
    const auto res = runCommand({"/bin/sh", "-c", "echo hello"}, {}, log);
    EXPECT_TRUE(res.launched);
    EXPECT_EQ(res.exit_code, 0);
    EXPECT_TRUE(res.ok());
    EXPECT_NE(readFile(log).find("hello"), std::string::npos);
}

TEST(Exec, RunCommandCapturesStdoutAndStderr) {
    TempDir d;
    const auto log = d.path() / "o.log";
    runCommand({"/bin/sh", "-c", "echo OUT; echo ERR 1>&2"}, {}, log);
    const auto c = readFile(log);
    EXPECT_NE(c.find("OUT"), std::string::npos);
    EXPECT_NE(c.find("ERR"), std::string::npos);
}

TEST(Exec, RunCommandHonorsCwd) {
    TempDir d;
    const auto log = d.path() / "o.log";
    runCommand({"/bin/pwd"}, d.path(), log);
    EXPECT_NE(readFile(log).find(d.path().string()), std::string::npos);
}

TEST(Exec, RunCommandCreatesLogParentDir) {
    TempDir d;
    const auto log = d.path() / "nested/deep/o.log";
    const auto res = runCommand({"/bin/sh", "-c", "echo x"}, {}, log);
    EXPECT_TRUE(res.ok());
    EXPECT_TRUE(fs::exists(log));
    EXPECT_NE(readFile(log).find("x"), std::string::npos);
}

TEST(Exec, RunCommandEmptyArgvNotLaunched) {
    TempDir d;
    const auto res = runCommand({}, {}, d.path() / "o.log");
    EXPECT_FALSE(res.launched);
}

TEST(Exec, RunCommandPropagatesExitCode) {
    TempDir d;
    const auto res = runCommand({"/bin/sh", "-c", "exit 3"}, {}, d.path() / "o.log");
    EXPECT_TRUE(res.launched);
    EXPECT_EQ(res.exit_code, 3);
    EXPECT_FALSE(res.ok());
}

TEST(Exec, RunCommandMissingCommandExits127) {
    TempDir d;
    const auto res = runCommand({"definitely-not-a-real-cmd-xyz"}, {}, d.path() / "o.log");
    EXPECT_TRUE(res.launched); // forked fine; execvp failed inside the child
    EXPECT_EQ(res.exit_code, 127);
}

TEST(Exec, RunCaptureReturnsOutputAndExit) {
    std::string cap;
    const auto res = runCapture({"/bin/sh", "-c", "echo captured"}, {}, cap);
    EXPECT_TRUE(res.ok());
    EXPECT_NE(cap.find("captured"), std::string::npos);
}

TEST(Exec, RunCaptureEmptyArgvNotLaunched) {
    std::string cap;
    const auto res = runCapture({}, {}, cap);
    EXPECT_FALSE(res.launched);
    EXPECT_TRUE(cap.empty());
}

TEST(Exec, RunCaptureHonorsCwd) {
    TempDir d;
    std::string cap;
    runCapture({"/bin/pwd"}, d.path(), cap);
    EXPECT_NE(cap.find(d.path().string()), std::string::npos);
}

TEST(Exec, EventLogWritesValidJsonl) {
    TempDir d;
    const auto log = d.path() / "events.jsonl";
    {
        EventLog el(log);
        ASSERT_TRUE(el.ok());
        el.event("busybox", "fetch", "curl ...", "ok", 123, "fetch.log", "done");
        el.event("openssl", "build", "", "fail", 456, "", "oops");
    }

    const auto c = readFile(log);
    EXPECT_EQ(std::count(c.begin(), c.end(), '\n'), 2);

    std::istringstream is(c);
    std::string line;
    ASSERT_TRUE(std::getline(is, line));
    const auto j0 = nlohmann::json::parse(line);
    EXPECT_EQ(j0["package"], "busybox");
    EXPECT_EQ(j0["phase"], "fetch");
    EXPECT_EQ(j0["command"], "curl ...");
    EXPECT_EQ(j0["status"], "ok");
    EXPECT_EQ(j0["duration_ms"], 123);
    EXPECT_EQ(j0["log"], "fetch.log");

    ASSERT_TRUE(std::getline(is, line));
    const auto j1 = nlohmann::json::parse(line);
    EXPECT_EQ(j1["package"], "openssl");
    EXPECT_EQ(j1["status"], "fail");
    EXPECT_FALSE(j1.contains("command")); // empty fields are omitted
    EXPECT_FALSE(j1.contains("log"));
}

TEST(Exec, ReadFileReturnsContents) {
    TempDir d;
    const auto f = d.path() / "f.txt";
    {
        std::ofstream of(f);
        of << "abc123";
    }
    EXPECT_EQ(readFile(f), "abc123");
}

TEST(Exec, ReadFileMissingIsEmpty) {
    EXPECT_TRUE(readFile("/no/such/file/lightroot-xyz").empty());
}

TEST(Exec, CpuCountPositive) {
    EXPECT_GT(cpuCount(), 0);
}

TEST(Exec, NowMsPositive) {
    EXPECT_GT(nowMs(), 0);
}

TEST(Exec, RunCommandInjectsEnvironment) {
    TempDir d;
    const std::vector<std::pair<std::string, std::string>> env{{"LIGHTROOT_FOO", "bar"}};
    runCommand({"/bin/sh", "-c", "echo $LIGHTROOT_FOO"}, {}, d.path() / "o.log", env);
    EXPECT_NE(readFile(d.path() / "o.log").find("bar"), std::string::npos);
}

TEST(Exec, RunCommandEnvPreservesInheritedPath) {
    TempDir d;
    const std::vector<std::pair<std::string, std::string>> env{{"LIGHTROOT_X", "1"}};
    // `sh` must still be found via inherited PATH despite the custom env.
    const auto res = runCommand({"/bin/sh", "-c", "true"}, {}, d.path() / "o.log", env);
    EXPECT_TRUE(res.ok());
}
