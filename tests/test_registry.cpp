#include <lightroot/registry.h>

#include <gtest/gtest.h>

using namespace lightroot;

namespace {
Package make(std::string name, Scope scope, std::string origin = {}) {
    Package p;
    p.name = std::move(name);
    p.scope = scope;
    p.origin = std::move(origin);
    return p;
}
} // namespace

TEST(Registry, IndexAndFind) {
    PackageRegistry reg;
    reg.add(make("busybox", Scope::Target));
    reg.add(make("ninja", Scope::Host));

    EXPECT_NE(reg.findTarget("busybox"), nullptr);
    EXPECT_EQ(reg.findHost("busybox"), nullptr);
    EXPECT_NE(reg.findHost("ninja"), nullptr);
    EXPECT_EQ(reg.findTarget("nope"), nullptr);
    EXPECT_EQ(reg.size(), 2u);
}

TEST(Registry, HostAndTargetAreDistinct) {
    PackageRegistry reg;
    reg.add(make("foo", Scope::Host));
    reg.add(make("foo", Scope::Target));
    EXPECT_EQ(reg.size(), 2u);
    EXPECT_TRUE(reg.duplicates().empty());
    EXPECT_NE(reg.findHost("foo"), nullptr);
    EXPECT_NE(reg.findTarget("foo"), nullptr);
}

TEST(Registry, OverlayOverridesAndRecords) {
    PackageRegistry reg;
    reg.add(make("busybox", Scope::Target, "upstream.toml"));
    Package company = make("busybox", Scope::Target, "company.toml");
    company.summary = "company override";
    reg.add(std::move(company));

    ASSERT_EQ(reg.size(), 1u);
    ASSERT_EQ(reg.duplicates().size(), 1u);
    const auto& d = reg.duplicates()[0];
    EXPECT_EQ(d.first.string(), "upstream.toml");
    EXPECT_EQ(d.second.string(), "company.toml");

    const auto* f = reg.findTarget("busybox");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->summary, "company override");
}
