#include <lightroot/manifest.h>

#include <gtest/gtest.h>

using namespace lightroot;

TEST(Manifest, BusyboxFixture) {
    std::vector<ManifestError> errs;
    auto p = parseManifest("data/busybox.toml", errs);
    ASSERT_TRUE(errs.empty()) << (!errs.empty() ? errs[0].message : "");
    ASSERT_TRUE(p);

    EXPECT_EQ(p->name, "busybox");
    EXPECT_EQ(p->version.to_string(), "1.36.1");
    EXPECT_EQ(p->scope, Scope::Target);
    EXPECT_EQ(p->license, "GPL-2.0-only");

    ASSERT_EQ(p->provides.size(), 3u);
    EXPECT_EQ(p->provides[0], "virtual:base-utils");
    EXPECT_EQ(p->provides[1], "virtual:shell");
    EXPECT_EQ(p->provides[2], "virtual:init");

    ASSERT_TRUE(p->build);
    EXPECT_EQ(p->build->backend, "make-kconfig");

    ASSERT_TRUE(p->source);
    EXPECT_EQ(p->source->kind, "archive");
    EXPECT_FALSE(p->source->url.empty());
}

TEST(Manifest, MissingPackageTable) {
    std::vector<ManifestError> errs;
    auto p = parseManifestText(R"([notpackage]
x = 1
)",
                               "inline", errs);
    EXPECT_FALSE(p);
    ASSERT_FALSE(errs.empty());
    EXPECT_NE(errs[0].message.find("[package]"), std::string::npos);
}

TEST(Manifest, BadDependencyIsReported) {
    std::vector<ManifestError> errs;
    auto p = parseManifestText(R"(
[package]
name = "x"
depends = ["!!!bad", "valid"]
)",
                               "inline", errs);
    ASSERT_TRUE(p);
    ASSERT_EQ(errs.size(), 1u);
    EXPECT_NE(errs[0].message.find("!!!bad"), std::string::npos);
    // the valid dep still parsed
    ASSERT_EQ(p->depends.size(), 1u);
    EXPECT_EQ(p->depends[0].name, "valid");
}

TEST(Manifest, MissingFile) {
    std::vector<ManifestError> errs;
    auto p = parseManifest("data/does-not-exist.toml", errs);
    EXPECT_FALSE(p);
    ASSERT_FALSE(errs.empty());
}

TEST(Manifest, BuildPhasesAndEnvParsed) {
    std::vector<ManifestError> errs;
    auto p = parseManifestText(R"(
[package]
name = "x"

[build]
backend = "script"
env = { CFLAGS = "-O2" }

[build.build]
run = ["make -j$LIGHTROOT_JOBS"]

[build.install_target]
run = ["make DESTDIR=$LIGHTROOT_DESTDIR install"]
)",
                               "inline", errs);
    ASSERT_TRUE(errs.empty()) << (!errs.empty() ? errs[0].message : "");
    ASSERT_TRUE(p);
    ASSERT_TRUE(p->build);
    EXPECT_EQ(p->build->backend, "script");
    ASSERT_EQ(p->build->env.size(), 1u);
    EXPECT_EQ(p->build->env["CFLAGS"], "-O2");
    ASSERT_EQ(p->build->phases.size(), 2u);
    ASSERT_EQ(p->build->phases.count("build"), 1u);
    EXPECT_EQ(p->build->phases.at("build").run.size(), 1u);
    EXPECT_EQ(p->build->phases.at("build").run[0], "make -j$LIGHTROOT_JOBS");
    ASSERT_EQ(p->build->phases.count("install_target"), 1u);
}

TEST(Manifest, BackendOnlyManifestHasNoPhases) {
    std::vector<ManifestError> errs;
    auto p = parseManifestText(R"(
[package]
name = "x"

[build]
backend = "make-kconfig"
)",
                               "inline", errs);
    ASSERT_TRUE(p);
    ASSERT_TRUE(p->build);
    EXPECT_EQ(p->build->backend, "make-kconfig");
    EXPECT_TRUE(p->build->phases.empty());
    EXPECT_TRUE(p->build->env.empty());
}
