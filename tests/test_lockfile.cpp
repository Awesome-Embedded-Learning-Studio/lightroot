#include <lightroot/lockfile.h>
#include <lightroot/resolver.h>
#include <lightroot/version.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

using namespace lightroot;

namespace {

/// Build a target-scope step with the given name/version.
BuildStep step(std::string name, std::string ver, Scope scope = Scope::Target) {
    BuildStep s;
    s.scope = scope;
    s.name = std::move(name);
    s.version = *parseVersion(ver);
    return s;
}

BuildPlan planOf(std::vector<BuildStep> steps) {
    BuildPlan p;
    p.steps = std::move(steps);
    return p;
}

/// Build a locked step with the given name/version.
LockedStep lstep(std::string name, std::string ver, Scope scope = Scope::Target) {
    LockedStep s;
    s.scope = scope;
    s.name = std::move(name);
    s.version = *parseVersion(ver);
    return s;
}

Lockfile lockOf(std::vector<LockedStep> steps) {
    Lockfile l;
    l.steps = std::move(steps);
    return l;
}

} // namespace

TEST(Lockfile, RoundTripPreservesSteps) {
    BuildPlan plan = planOf({step("busybox", "1.36.1"), step("base-files", "1.0.0"),
                             step("helper", "0.2", Scope::Host)});
    ToolchainConfig tc;
    tc.target = "aarch64-linux-gnu";

    const auto text =
        serializeLockfile(plan, tc, {"busybox", "base-files"}, {{"virtual:ssl", "openssl"}});

    std::vector<LockfileError> errs;
    auto lock = parseLockfileText(text, errs);
    ASSERT_TRUE(errs.empty()) << errs[0].message;
    ASSERT_TRUE(lock);
    EXPECT_EQ(lock->target, "aarch64-linux-gnu");
    ASSERT_EQ(lock->steps.size(), 3u);
    EXPECT_EQ(lock->steps[0].name, "busybox");
    EXPECT_EQ(lock->steps[0].version.to_string(), "1.36.1");
    EXPECT_EQ(lock->steps[0].scope, Scope::Target);
    EXPECT_EQ(lock->steps[2].name, "helper");
    EXPECT_EQ(lock->steps[2].scope, Scope::Host);
}

TEST(Lockfile, DriftNoneWhenPlansMatch) {
    const auto lock = lockOf({lstep("a", "1"), lstep("b", "2")});
    const auto plan = planOf({step("a", "1"), step("b", "2")});
    EXPECT_TRUE(checkLockfileDrift(lock, plan).empty());
}

TEST(Lockfile, DriftAddedWhenPlanHasExtra) {
    const auto lock = lockOf({lstep("a", "1")});
    const auto plan = planOf({step("a", "1"), step("b", "2")});
    const auto d = checkLockfileDrift(lock, plan);
    ASSERT_EQ(d.added.size(), 1u);
    EXPECT_NE(d.added[0].find("b@2"), std::string::npos);
    EXPECT_TRUE(d.removed.empty());
    EXPECT_TRUE(d.changed.empty());
}

TEST(Lockfile, DriftRemovedWhenLockHasExtra) {
    const auto lock = lockOf({lstep("a", "1"), lstep("c", "3")});
    const auto plan = planOf({step("a", "1")});
    const auto d = checkLockfileDrift(lock, plan);
    ASSERT_EQ(d.removed.size(), 1u);
    EXPECT_NE(d.removed[0].find("c@3"), std::string::npos);
}

TEST(Lockfile, DriftChangedOnVersionBump) {
    const auto lock = lockOf({lstep("a", "1.0")});
    const auto plan = planOf({step("a", "1.1")});
    const auto d = checkLockfileDrift(lock, plan);
    ASSERT_EQ(d.changed.size(), 1u);
    EXPECT_NE(d.changed[0].find("1.0"), std::string::npos);
    EXPECT_NE(d.changed[0].find("1.1"), std::string::npos);
}

TEST(Lockfile, DriftDistinguishesScope) {
    // target:a@1 and host:a@1 are different packages.
    const auto lock = lockOf({lstep("a", "1", Scope::Target)});
    const auto plan = planOf({step("a", "1", Scope::Host)});
    const auto d = checkLockfileDrift(lock, plan);
    EXPECT_EQ(d.added.size(), 1u);
    EXPECT_EQ(d.removed.size(), 1u);
}

TEST(Lockfile, ParseRejectsBadJson) {
    std::vector<LockfileError> errs;
    auto lock = parseLockfileText("{ not valid json", errs);
    EXPECT_FALSE(lock);
    ASSERT_FALSE(errs.empty());
}

TEST(Lockfile, ParseSkipsBadStepVersion) {
    // A step with an unparseable version is skipped (non-fatal); the good one survives.
    std::vector<LockfileError> errs;
    auto lock = parseLockfileText(R"({
    "schema": 1,
    "steps": [
        {"scope": "target", "name": "good", "version": "1.0"},
        {"scope": "target", "name": "bad", "version": "not-a-version"}
    ]
})",
                                  errs);
    ASSERT_TRUE(lock);
    ASSERT_EQ(lock->steps.size(), 1u);
    EXPECT_EQ(lock->steps[0].name, "good");
    EXPECT_FALSE(errs.empty());
}
