#include <lightroot/config.h>

#include <gtest/gtest.h>

using namespace lightroot;

TEST(Config, Fixture) {
    std::vector<ConfigError> errs;
    auto c = parseConfig("data/config.toml", errs);
    ASSERT_TRUE(errs.empty()) << (!errs.empty() ? errs[0].message : "");
    ASSERT_TRUE(c);

    EXPECT_EQ(c->toolchain.kind, "external");
    EXPECT_EQ(c->toolchain.prefix, "aarch64-linux-gnu-");
    EXPECT_EQ(c->toolchain.target, "aarch64-linux-gnu");
    EXPECT_FALSE(c->toolchain.sysroot.empty());

    EXPECT_EQ(c->profile, "minimal");

    ASSERT_EQ(c->providers.size(), 2u);
    EXPECT_EQ(c->providers["virtual:base-utils"], "busybox");
    EXPECT_EQ(c->providers["virtual:ssl"], "openssl");

    ASSERT_EQ(c->selected.size(), 2u);
    EXPECT_EQ(c->selected[0], "busybox");

    EXPECT_EQ(c->output_dir, "out");
}

TEST(Config, DefaultsWhenEmpty) {
    std::vector<ConfigError> errs;
    auto c = parseConfigText(R"([toolchain]
target = "aarch64-linux-gnu"
)",
                             errs);
    ASSERT_TRUE(c);
    EXPECT_EQ(c->toolchain.kind, "external"); // default
    EXPECT_EQ(c->toolchain.target, "aarch64-linux-gnu");
}

TEST(Config, TomlSyntaxError) {
    std::vector<ConfigError> errs;
    auto c = parseConfigText("this is = = not valid", errs);
    EXPECT_FALSE(c);
    ASSERT_FALSE(errs.empty());
}
