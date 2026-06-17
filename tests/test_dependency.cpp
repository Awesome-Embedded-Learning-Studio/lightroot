#include <lightroot/dependency.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace lightroot;

TEST(Dependency, SimplePackage) {
    auto d = parseDependency("busybox");
    ASSERT_TRUE(d.has_value()) << d.error().message;
    EXPECT_EQ(d->scope, Scope::Target);
    EXPECT_EQ(d->kind, DepKind::Package);
    EXPECT_EQ(d->name, "busybox");
    EXPECT_EQ(d->version.cmp, Cmp::Any);
}

TEST(Dependency, VersionedTarget) {
    auto d = parseDependency("target:openssl >= 3.0");
    ASSERT_TRUE(d.has_value()) << d.error().message;
    EXPECT_EQ(d->scope, Scope::Target);
    EXPECT_EQ(d->kind, DepKind::Package);
    EXPECT_EQ(d->name, "openssl");
    ASSERT_EQ(d->version.cmp, Cmp::Ge);
    EXPECT_EQ(d->version.ver.to_string(), "3.0.0");
}

TEST(Dependency, VirtualDefaultTarget) {
    auto d = parseDependency("virtual:ssl");
    ASSERT_TRUE(d.has_value()) << d.error().message;
    EXPECT_EQ(d->kind, DepKind::Virtual);
    EXPECT_EQ(d->name, "ssl");
    EXPECT_EQ(d->scope, Scope::Target);
}

TEST(Dependency, HostScoped) {
    auto d = parseDependency("host:ninja");
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->scope, Scope::Host);
    EXPECT_EQ(d->kind, DepKind::Package);
    EXPECT_EQ(d->name, "ninja");
}

TEST(Dependency, HostScopedVirtual) {
    auto d = parseDependency("host:virtual:libc");
    ASSERT_TRUE(d.has_value()) << d.error().message;
    EXPECT_EQ(d->scope, Scope::Host);
    EXPECT_EQ(d->kind, DepKind::Virtual);
    EXPECT_EQ(d->name, "libc");
}

TEST(Dependency, Features) {
    auto d = parseDependency("busybox[static,nls]");
    ASSERT_TRUE(d.has_value()) << d.error().message;
    EXPECT_EQ(d->features, (std::vector<std::string>{"static", "nls"}));
}

TEST(Dependency, Errors) {
    EXPECT_FALSE(parseDependency("").has_value());
    EXPECT_FALSE(parseDependency("target:openssl >=").has_value());
    EXPECT_FALSE(parseDependency(">= 3").has_value());
    EXPECT_FALSE(parseDependency("busybox[static").has_value());
    EXPECT_FALSE(parseDependency("busybox trailing").has_value());
}

TEST(Dependency, RoundTrip) {
    struct Case {
        std::string in;
        std::string want;
    };
    // Versions normalize to major.minor.patch, so the versioned case is not a
    // literal round-trip — that is intended.
    const Case cases[] = {
        {"target:busybox", "target:busybox"},
        {"host:ninja", "host:ninja"},
        {"target:virtual:ssl", "target:virtual:ssl"},
        {"target:openssl >= 3.0", "target:openssl >= 3.0.0"},
        {"target:busybox[static,nls]", "target:busybox[static,nls]"},
    };
    for (const auto& c : cases) {
        auto d = parseDependency(c.in);
        ASSERT_TRUE(d.has_value()) << "for '" << c.in << "': " << d.error().message;
        EXPECT_EQ(d->to_string(), c.want) << "round-trip for '" << c.in << "'";
    }
}
