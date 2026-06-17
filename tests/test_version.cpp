#include <lightroot/version.h>

#include <gtest/gtest.h>

using namespace lightroot;

TEST(Version, ParseBasic) {
    EXPECT_EQ(parseVersion("3.0.1")->to_string(), "3.0.1");
    EXPECT_EQ(parseVersion("3")->to_string(), "3.0.0");
    EXPECT_EQ(parseVersion("3.0")->to_string(), "3.0.0");
    EXPECT_EQ(parseVersion("0.0.0")->to_string(), "0.0.0");
}

TEST(Version, ParseRejects) {
    EXPECT_FALSE(parseVersion(""));
    EXPECT_FALSE(parseVersion("3."));
    EXPECT_FALSE(parseVersion(".5"));
    EXPECT_FALSE(parseVersion("x.1"));
    EXPECT_FALSE(parseVersion("3.0.1.2"));
    EXPECT_FALSE(parseVersion("3.0a"));
}

TEST(Version, Compare) {
    const Version v300{3, 0, 0};
    const Version v301{3, 0, 1};
    const Version v310{3, 1, 0};
    const Version v309{3, 0, 9};
    const Version v400{4, 0, 0};
    const Version v399{3, 9, 9};
    EXPECT_LT(v300.compare(v301), 0);
    EXPECT_EQ(v300.compare(v300), 0);
    EXPECT_GT(v310.compare(v309), 0);
    EXPECT_GT(v400.compare(v399), 0);
}

TEST(VersionConstraint, Satisfies) {
    auto c = parseVersionConstraint(">=", "3.0");
    ASSERT_TRUE(c);
    const Version v300{3, 0, 0};
    const Version v400{4, 0, 0};
    const Version v299{2, 9, 9};
    EXPECT_TRUE(c->satisfies(v300));
    EXPECT_TRUE(c->satisfies(v400));
    EXPECT_FALSE(c->satisfies(v299));

    auto eq = parseVersionConstraint("==", "1.36.1");
    ASSERT_TRUE(eq);
    const Version v1361{1, 36, 1};
    const Version v1362{1, 36, 2};
    EXPECT_TRUE(eq->satisfies(v1361));
    EXPECT_FALSE(eq->satisfies(v1362));
}

TEST(VersionConstraint, BadOperator) {
    EXPECT_FALSE(parseVersionConstraint("~>", "1.0"));
    EXPECT_FALSE(parseVersionConstraint(">=", "notaversion"));
}

TEST(VersionConstraint, ToString) {
    EXPECT_EQ(parseVersionConstraint(">=", "3.0")->to_string(), ">= 3.0.0");
    VersionConstraint none;
    EXPECT_EQ(none.to_string(), "");
}
