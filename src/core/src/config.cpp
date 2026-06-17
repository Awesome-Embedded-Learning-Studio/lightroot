#include <lightroot/config.h>

#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include <toml++/toml.hpp>

namespace lightroot {

namespace {
ConfigError wrap(const toml::parse_error& e) {
    return {std::string(e.what()), static_cast<std::size_t>(e.source().begin.line)};
}
} // namespace

std::optional<ProjectConfig> parseConfigText(std::string_view text,
                                             std::vector<ConfigError>& errors) {
    toml::table tbl;
    try {
        tbl = toml::parse(text);
    } catch (const toml::parse_error& e) {
        errors.push_back(wrap(e));
        return std::nullopt;
    }

    ProjectConfig cfg;

    if (auto* t = tbl["toolchain"].as_table()) {
        cfg.toolchain.kind = (*t)["kind"].value_or(std::string{"external"});
        cfg.toolchain.prefix = (*t)["prefix"].value_or(std::string{});
        cfg.toolchain.path = (*t)["path"].value_or(std::string{});
        cfg.toolchain.sysroot = (*t)["sysroot"].value_or(std::string{});
        cfg.toolchain.target = (*t)["target"].value_or(std::string{});
    }

    cfg.profile = tbl["profile"].value_or(std::string{});

    if (auto* t = tbl["providers"].as_table()) {
        for (const auto& [k, v] : *t) {
            if (auto vs = v.value<std::string>()) {
                cfg.providers[std::string(k)] = *vs;
            }
        }
    }

    if (auto* arr = tbl["selected"].as_array()) {
        for (const auto& el : *arr) {
            if (auto s = el.value<std::string>())
                cfg.selected.push_back(*s);
        }
    }

    if (auto od = tbl["output_dir"].value<std::string>()) {
        cfg.output_dir = *od;
    }

    if (auto* t = tbl["rootfs"].as_table()) {
        if (auto* arr = (*t)["image_formats"].as_array()) {
            cfg.image_formats.clear();
            for (const auto& el : *arr)
                if (auto s = el.value<std::string>())
                    cfg.image_formats.push_back(*s);
        }
        if (auto f = (*t)["device_table"].value<std::string>())
            cfg.device_table_file = *f;
    }

    return cfg;
}

std::optional<ProjectConfig> parseConfig(const std::filesystem::path& file,
                                         std::vector<ConfigError>& errors) {
    std::ifstream in(file);
    if (!in) {
        errors.push_back({"cannot open file: " + file.string(), 0});
        return std::nullopt;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return parseConfigText(ss.str(), errors);
}

} // namespace lightroot
