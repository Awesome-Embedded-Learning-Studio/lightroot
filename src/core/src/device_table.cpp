#include <lightroot/device_table.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include <toml++/toml.hpp>

namespace lightroot {

namespace {

/// Wrap a toml++ parse error into a DeviceTableError (1-based source line).
DeviceTableError wrap(const toml::parse_error& e) {
    return {std::string(e.what()), static_cast<std::size_t>(e.source().begin.line)};
}

/// Source line of a node, or 0 when unavailable.
std::size_t lineOf(const toml::node& node) {
    return static_cast<std::size_t>(node.source().begin.line);
}

/// Read a permission/mode field that may be a TOML integer (decimal or 0o-octal)
/// or a string ("0666", "0o666", "666"). Octal is the natural form for modes.
template <typename NodeView> unsigned readMode(const NodeView& node, unsigned fallback) {
    if (auto s = node.template value<std::string>()) {
        std::string str = *s;
        if (str.rfind("0o", 0) == 0 || str.rfind("0O", 0) == 0)
            str.erase(0, 2);
        char* end = nullptr;
        const auto v = std::strtoul(str.c_str(), &end, 8);
        if (end != str.c_str() && (*end == '\0'))
            return static_cast<unsigned>(v);
        return fallback;
    }
    if (auto i = node.template value<long long>())
        return static_cast<unsigned>(*i);
    return fallback;
}

/// Read an integer field (uid/gid/major/minor) with a default.
int readInt(const toml::table& t, std::string_view key, int fallback) {
    if (auto v = t[key].value<long long>())
        return static_cast<int>(*v);
    return fallback;
}

/// Build a directory entry concisely.
DeviceEntry dirEntry(const char* path, unsigned mode) {
    DeviceEntry e;
    e.path = path;
    e.kind = DeviceKind::Dir;
    e.mode = mode;
    return e;
}

/// Build a character-device entry concisely.
DeviceEntry charEntry(const char* path, int major, int minor, unsigned mode) {
    DeviceEntry e;
    e.path = path;
    e.kind = DeviceKind::Char;
    e.mode = mode;
    e.major = major;
    e.minor = minor;
    return e;
}

} // namespace

// NOLINTNEXTLINE
const char* to_string(DeviceKind k) {
    switch (k) {
        case DeviceKind::Dir:
            return "dir";
        case DeviceKind::Char:
            return "char";
        case DeviceKind::Block:
            return "block";
    }
    return "dir";
}

// NOLINTNEXTLINE
std::optional<DeviceKind> parseDeviceKind(std::string_view s) {
    if (s == "dir")
        return DeviceKind::Dir;
    if (s == "char")
        return DeviceKind::Char;
    if (s == "block")
        return DeviceKind::Block;
    return std::nullopt;
}

std::optional<DeviceTable> parseDeviceTableText(std::string_view text,
                                                std::vector<DeviceTableError>& errors) {
    toml::table tbl;
    try {
        tbl = toml::parse(text);
    } catch (const toml::parse_error& e) {
        errors.push_back(wrap(e));
        return std::nullopt;
    }

    DeviceTable out;
    if (auto* arr = tbl["device"].as_array()) {
        for (const auto& el : *arr) {
            auto* t = el.as_table();
            if (!t) {
                errors.push_back({"device entry is not a table", lineOf(el)});
                continue;
            }

            DeviceEntry e;
            const auto path = (*t)["path"].value<std::string>();
            if (!path || path->empty()) {
                errors.push_back({"device entry missing 'path'", lineOf(el)});
                continue;
            }
            if (path->front() != '/') {
                errors.push_back({"device path must be absolute: " + *path, lineOf(el)});
                continue;
            }
            e.path = *path;

            const auto kindStr = (*t)["kind"].value_or(std::string{"dir"});
            const auto kind = parseDeviceKind(kindStr);
            if (!kind) {
                errors.push_back({"unknown device kind '" + kindStr + "' (expected dir|char|block)",
                                  lineOf(el)});
                continue;
            }
            e.kind = *kind;
            e.mode = readMode((*t)["mode"], 0755);
            e.uid = static_cast<unsigned>(readInt(*t, "uid", 0));
            e.gid = static_cast<unsigned>(readInt(*t, "gid", 0));

            if (e.kind != DeviceKind::Dir) {
                const bool hasMajor = (*t)["major"].value<long long>().has_value();
                const bool hasMinor = (*t)["minor"].value<long long>().has_value();
                if (!hasMajor || !hasMinor) {
                    errors.push_back(
                        {"device node '" + *path + "' requires 'major' and 'minor'", lineOf(el)});
                    continue;
                }
                e.major = readInt(*t, "major", 0);
                e.minor = readInt(*t, "minor", 0);
            }

            out.entries.push_back(std::move(e));
        }
    }

    return out;
}

std::optional<DeviceTable> parseDeviceTable(const std::filesystem::path& file,
                                            std::vector<DeviceTableError>& errors) {
    std::ifstream in(file);
    if (!in) {
        errors.push_back({"cannot open file: " + file.string(), 0});
        return std::nullopt;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return parseDeviceTableText(ss.str(), errors);
}

DeviceTable defaultDeviceTable() {
    DeviceTable t;
    // Directories with correct permissions/ownership.
    t.entries.push_back(dirEntry("/dev", 0755));
    t.entries.push_back(dirEntry("/proc", 0555));
    t.entries.push_back(dirEntry("/sys", 0555));
    t.entries.push_back(dirEntry("/tmp", 01777));
    t.entries.push_back(dirEntry("/run", 0755));
    t.entries.push_back(dirEntry("/root", 0700));
    t.entries.push_back(dirEntry("/var", 0755));
    t.entries.push_back(dirEntry("/var/log", 0755));
    t.entries.push_back(dirEntry("/var/tmp", 01777));
    // Essential character device nodes.
    t.entries.push_back(charEntry("/dev/null", 1, 3, 0666));
    t.entries.push_back(charEntry("/dev/zero", 1, 5, 0666));
    t.entries.push_back(charEntry("/dev/console", 5, 1, 0600));
    t.entries.push_back(charEntry("/dev/tty", 5, 0, 0666));
    t.entries.push_back(charEntry("/dev/random", 1, 8, 0666));
    t.entries.push_back(charEntry("/dev/urandom", 1, 9, 0666));
    return t;
}

void mergeDeviceEntries(DeviceTable& table, const std::vector<DeviceEntry>& additions) {
    for (const auto& add : additions) {
        auto it = std::find_if(table.entries.begin(), table.entries.end(),
                               [&](const DeviceEntry& e) { return e.path == add.path; });
        if (it != table.entries.end())
            *it = add; // same-path override
        else
            table.entries.push_back(add);
    }
}

DeviceTable resolveDeviceTable(const std::filesystem::path& device_table_file,
                               const std::filesystem::path& config_dir,
                               std::vector<DeviceTableError>& errors) {
    DeviceTable table = defaultDeviceTable();
    if (device_table_file.empty())
        return table;

    std::error_code ec;
    const auto resolved = std::filesystem::weakly_canonical(config_dir / device_table_file, ec);
    if (auto parsed = parseDeviceTable(resolved, errors))
        mergeDeviceEntries(table, parsed->entries);
    return table;
}

} // namespace lightroot
