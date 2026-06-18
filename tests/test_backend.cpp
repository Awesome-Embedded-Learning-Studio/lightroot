#include <lightroot/backend.h>

#include <gtest/gtest.h>

#include <string>

using namespace lightroot;

TEST(Backend, CmakeDefaultPhasesShape) {
    const auto ph = defaultPhases("cmake");
    ASSERT_EQ(ph.count("configure"), 1u);
    ASSERT_EQ(ph.count("build"), 1u);
    ASSERT_EQ(ph.count("install_target"), 1u);
    EXPECT_NE(ph.at("configure").run[0].find("CMAKE_TOOLCHAIN_FILE=$LIGHTROOT_CMAKE_TOOLCHAIN"),
              std::string::npos);
    EXPECT_NE(ph.at("build").run[0].find("cmake --build"), std::string::npos);
    EXPECT_NE(ph.at("install_target").run[0].find("cmake --install"), std::string::npos);
}

TEST(Backend, CmakeGeneratesToolchainFile) {
    ToolchainConfig tc;
    tc.prefix = "aarch64-linux-gnu-";
    tc.target = "aarch64-linux-gnu";
    tc.sysroot = "/usr/aarch64-linux-gnu";

    const auto files = backendFiles("cmake", tc, "/tmp/staging");
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].filename, "lightroot-toolchain.cmake");
    EXPECT_EQ(files[0].env_var, "LIGHTROOT_CMAKE_TOOLCHAIN");
    const auto& c = files[0].content;
    EXPECT_NE(c.find("CMAKE_C_COMPILER aarch64-linux-gnu-gcc"), std::string::npos);
    EXPECT_NE(c.find("CMAKE_CXX_COMPILER aarch64-linux-gnu-g++"), std::string::npos);
    EXPECT_NE(c.find("CMAKE_SYSTEM_PROCESSOR aarch64"), std::string::npos);
    EXPECT_NE(c.find("CMAKE_SYSROOT /usr/aarch64-linux-gnu"), std::string::npos);
    EXPECT_NE(c.find("/tmp/staging"), std::string::npos); // staging on the find path
    EXPECT_NE(c.find("CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY"), std::string::npos);
}

TEST(Backend, CmakeToolchainOmitsSysrootWhenUnset) {
    ToolchainConfig tc;
    tc.prefix = "aarch64-linux-gnu-";
    tc.target = "aarch64-linux-gnu";
    const auto files = backendFiles("cmake", tc, "/tmp/staging");
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].content.find("CMAKE_SYSROOT"), std::string::npos); // no sysroot line
    EXPECT_NE(files[0].content.find("/tmp/staging"), std::string::npos);  // staging still present
}

TEST(Backend, NonCmakeBackendsGenerateNoFiles) {
    ToolchainConfig tc;
    tc.prefix = "aarch64-linux-gnu-";
    EXPECT_TRUE(backendFiles("make", tc, "/tmp/s").empty());
    EXPECT_TRUE(backendFiles("autotools", tc, "/tmp/s").empty());
    EXPECT_TRUE(backendFiles("make-kconfig", tc, "/tmp/s").empty());
    EXPECT_TRUE(backendFiles("", tc, "/tmp/s").empty());
    EXPECT_TRUE(backendFiles("nonsense", tc, "/tmp/s").empty());
}

TEST(Backend, MesonDefaultPhasesShape) {
    const auto ph = defaultPhases("meson");
    ASSERT_EQ(ph.count("configure"), 1u);
    ASSERT_EQ(ph.count("build"), 1u);
    ASSERT_EQ(ph.count("install_target"), 1u);
    EXPECT_NE(ph.at("configure").run[0].find("--cross-file=$LIGHTROOT_MESON_CROSS"),
              std::string::npos);
    EXPECT_NE(ph.at("build").run[0].find("meson compile"), std::string::npos);
    EXPECT_NE(ph.at("install_target").run[0].find("meson install"), std::string::npos);
}

TEST(Backend, MesonGeneratesCrossFile) {
    ToolchainConfig tc;
    tc.prefix = "aarch64-linux-gnu-";
    tc.target = "aarch64-linux-gnu";
    tc.sysroot = "/usr/aarch64-linux-gnu";

    const auto files = backendFiles("meson", tc, "/tmp/staging");
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].filename, "lightroot-meson-cross.txt");
    EXPECT_EQ(files[0].env_var, "LIGHTROOT_MESON_CROSS");
    const auto& c = files[0].content;
    EXPECT_NE(c.find("c = 'aarch64-linux-gnu-gcc'"), std::string::npos);
    EXPECT_NE(c.find("cpu_family = 'aarch64'"), std::string::npos);
    EXPECT_NE(c.find("sys_root = '/usr/aarch64-linux-gnu'"), std::string::npos);
    EXPECT_NE(c.find("--sysroot=/usr/aarch64-linux-gnu"), std::string::npos);
    EXPECT_NE(c.find("-I/tmp/staging/include"), std::string::npos); // staging on the compile path
    EXPECT_NE(c.find("-L/tmp/staging/lib"), std::string::npos);
}
