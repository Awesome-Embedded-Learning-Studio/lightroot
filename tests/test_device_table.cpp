#include <lightroot/device_table.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>

using namespace lightroot;

namespace {

/// Find an entry by path, or nullptr.
const DeviceEntry* findByPath(const DeviceTable& t, std::string_view p) {
    for (const auto& e : t.entries)
        if (e.path == p)
            return &e;
    return nullptr;
}

} // namespace

TEST(DeviceTable, ParseKindRoundTrip) {
    EXPECT_STREQ(to_string(DeviceKind::Dir), "dir");
    EXPECT_STREQ(to_string(DeviceKind::Char), "char");
    EXPECT_STREQ(to_string(DeviceKind::Block), "block");
    EXPECT_EQ(parseDeviceKind("char"), DeviceKind::Char);
    EXPECT_FALSE(parseDeviceKind("socket"));
}

TEST(DeviceTable, ParsesValidDirsAndNodes) {
    std::vector<DeviceTableError> errs;
    const auto t = parseDeviceTableText(R"(
[[device]]
path = "/dev"
kind = "dir"
mode = "0755"

[[device]]
path = "/dev/null"
kind = "char"
mode = "0666"
major = 1
minor = 3
)",
                                        errs);
    ASSERT_TRUE(errs.empty()) << errs[0].message;
    ASSERT_TRUE(t);
    ASSERT_EQ(t->entries.size(), 2u);

    const auto* dev = findByPath(*t, "/dev");
    ASSERT_TRUE(dev);
    EXPECT_EQ(dev->kind, DeviceKind::Dir);
    EXPECT_EQ(dev->mode, 0755u);

    const auto* null = findByPath(*t, "/dev/null");
    ASSERT_TRUE(null);
    EXPECT_EQ(null->kind, DeviceKind::Char);
    EXPECT_EQ(null->major, 1);
    EXPECT_EQ(null->minor, 3);
    EXPECT_EQ(null->mode, 0666u);
}

TEST(DeviceTable, RejectsDeviceNodeWithoutMajor) {
    std::vector<DeviceTableError> errs;
    const auto t = parseDeviceTableText(R"(
[[device]]
path = "/dev/null"
kind = "char"
mode = "0666"
minor = 3
)",
                                        errs);
    ASSERT_TRUE(t);
    EXPECT_TRUE(t->entries.empty());
    ASSERT_FALSE(errs.empty());
    EXPECT_NE(errs[0].message.find("major"), std::string::npos);
    EXPECT_GT(errs[0].line, 0u);
}

TEST(DeviceTable, RejectsBadKindAndKeepsParsing) {
    std::vector<DeviceTableError> errs;
    const auto t = parseDeviceTableText(R"(
[[device]]
path = "/dev/frob"
kind = "frob"

[[device]]
path = "/dev/zero"
kind = "char"
major = 1
minor = 5
)",
                                        errs);
    ASSERT_TRUE(t);
    ASSERT_EQ(t->entries.size(), 1u); // the bad one was skipped
    EXPECT_TRUE(findByPath(*t, "/dev/zero"));
    ASSERT_FALSE(errs.empty());
    EXPECT_NE(errs[0].message.find("kind"), std::string::npos);
}

TEST(DeviceTable, RejectsRelativePath) {
    std::vector<DeviceTableError> errs;
    const auto t = parseDeviceTableText(R"(
[[device]]
path = "dev/null"
kind = "char"
major = 1
minor = 3
)",
                                        errs);
    ASSERT_TRUE(t);
    EXPECT_TRUE(t->entries.empty());
    ASSERT_FALSE(errs.empty());
    EXPECT_NE(errs[0].message.find("absolute"), std::string::npos);
}

TEST(DeviceTable, DefaultTableHasExpectedNodes) {
    const auto t = defaultDeviceTable();
    EXPECT_FALSE(t.entries.empty());

    const auto* null = findByPath(t, "/dev/null");
    ASSERT_TRUE(null);
    EXPECT_EQ(null->kind, DeviceKind::Char);
    EXPECT_EQ(null->major, 1);
    EXPECT_EQ(null->minor, 3);
    EXPECT_EQ(null->mode, 0666u);

    const auto* console = findByPath(t, "/dev/console");
    ASSERT_TRUE(console);
    EXPECT_EQ(console->major, 5);
    EXPECT_EQ(console->minor, 1);
    EXPECT_EQ(console->mode, 0600u);

    const auto* tmp = findByPath(t, "/tmp");
    ASSERT_TRUE(tmp);
    EXPECT_EQ(tmp->kind, DeviceKind::Dir);
    EXPECT_EQ(tmp->mode, 01777u);

    const auto* root = findByPath(t, "/root");
    ASSERT_TRUE(root);
    EXPECT_EQ(root->mode, 0700u);
}

TEST(DeviceTable, DefaultTableIsConsistent) {
    const auto t = defaultDeviceTable();
    for (const auto& e : t.entries) {
        ASSERT_FALSE(e.path.empty());
        EXPECT_EQ(e.path.front(), '/') << e.path;
        if (e.kind == DeviceKind::Dir) {
            EXPECT_EQ(e.major, 0);
            EXPECT_EQ(e.minor, 0);
        } else {
            // char/block nodes always live under /dev in the default table.
            EXPECT_EQ(e.path.rfind("/dev/", 0), 0u);
        }
    }
    // No duplicate paths.
    for (std::size_t i = 0; i < t.entries.size(); ++i)
        for (std::size_t j = i + 1; j < t.entries.size(); ++j)
            EXPECT_NE(t.entries[i].path, t.entries[j].path);
}

TEST(DeviceTable, MergeOverridesByPath) {
    DeviceTable t;
    t.entries.push_back({"/dev/null", DeviceKind::Char, 0666, 0, 0, 1, 3});

    std::vector<DeviceEntry> add;
    add.push_back({"/dev/null", DeviceKind::Char, 0600, 0, 0, 1, 3});    // override mode
    add.push_back({"/dev/custom", DeviceKind::Char, 0666, 0, 0, 10, 2}); // new
    mergeDeviceEntries(t, add);

    ASSERT_EQ(t.entries.size(), 2u);
    const auto* null = findByPath(t, "/dev/null");
    ASSERT_TRUE(null);
    EXPECT_EQ(null->mode, 0600u); // overridden
    EXPECT_TRUE(findByPath(t, "/dev/custom"));
}

TEST(DeviceTable, ResolveAppliesDefaultAndFile) {
    std::vector<DeviceTableError> errs;
    // Empty file path -> just the default table.
    auto t = resolveDeviceTable({}, ".", errs);
    EXPECT_TRUE(errs.empty());
    EXPECT_TRUE(findByPath(t, "/dev/null"));

    // File relative to the test data dir -> defaults + custom nodes merged.
    errs.clear();
    t = resolveDeviceTable("devices.toml", std::filesystem::path{"data"}, errs);
    EXPECT_TRUE(errs.empty()) << (errs.empty() ? "" : errs[0].message);
    EXPECT_TRUE(findByPath(t, "/dev/null"));    // from default
    EXPECT_TRUE(findByPath(t, "/dev/mmcblk0")); // from file
    EXPECT_TRUE(findByPath(t, "/mnt"));         // from file
}

TEST(DeviceTable, ResolveMissingFileKeepsDefault) {
    std::vector<DeviceTableError> errs;
    const auto t = resolveDeviceTable("does-not-exist.toml", "data", errs);
    EXPECT_FALSE(errs.empty());              // open failure reported
    EXPECT_TRUE(findByPath(t, "/dev/null")); // default intact
}
