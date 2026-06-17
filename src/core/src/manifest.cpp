#include <lightroot/manifest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <toml++/toml.hpp>

namespace lightroot {

namespace {
ManifestError wrap(const toml::parse_error& e) {
    return {std::string(e.what()), static_cast<std::size_t>(e.source().begin.line)};
}
} // namespace

std::optional<Package> parseManifestText(std::string_view text, const std::filesystem::path& origin,
                                         std::vector<ManifestError>& errors) {
    toml::table tbl;
    try {
        tbl = toml::parse(text, origin.string());
    } catch (const toml::parse_error& e) {
        errors.push_back(wrap(e));
        return std::nullopt;
    }

    const auto pkg = tbl["package"];
    if (!pkg.is_table()) {
        errors.push_back({"missing [package] table", 0});
        return std::nullopt;
    }

    Package out;
    out.origin = origin;

    const auto name = pkg["name"].value<std::string>();
    if (!name) {
        errors.push_back({"[package] missing required 'name'", 0});
        return std::nullopt;
    }
    out.name = *name;

    if (auto v = pkg["version"].value<std::string>()) {
        if (auto parsed = parseVersion(*v)) {
            out.version = *parsed;
        } else {
            errors.push_back({"[package] invalid version '" + *v + "': " + parsed.error(), 0});
        }
    }
    out.summary = pkg["summary"].value_or(std::string{});
    out.license = pkg["license"].value_or(std::string{});

    if (auto s = pkg["scope"].value<std::string>()) {
        if (*s == "host")
            out.scope = Scope::Host;
        else if (*s == "target")
            out.scope = Scope::Target;
        else
            errors.push_back({"[package] unknown scope: " + *s + " (expected host|target)", 0});
    }

    if (auto* arr = pkg["provides"].as_array()) {
        for (const auto& el : *arr) {
            if (auto s = el.value<std::string>())
                out.provides.push_back(*s);
        }
    }

    if (auto* arr = pkg["depends"].as_array()) {
        for (const auto& el : *arr) {
            const auto spec = el.value<std::string>();
            if (!spec)
                continue;
            auto dep = parseDependency(*spec);
            if (dep) {
                out.depends.push_back(std::move(*dep));
            } else {
                errors.push_back({"invalid dependency '" + *spec + "': " + dep.error().message, 0});
            }
        }
    }

    if (auto* t = tbl["source"].as_table()) {
        SourceInfo si;
        si.kind = (*t)["kind"].value_or(std::string{"archive"});
        si.url = (*t)["url"].value_or(std::string{});
        si.sha256 = (*t)["sha256"].value_or(std::string{});
        out.source = si;
    }

    if (auto* t = tbl["build"].as_table()) {
        BuildInfo bi;
        bi.backend = (*t)["backend"].value_or(std::string{});

        if (auto* env = (*t)["env"].as_table()) {
            for (const auto& [k, v] : *env) {
                if (auto vs = v.value<std::string>())
                    bi.env.emplace(std::string(k), *vs);
            }
        }

        const auto readRun = [&](std::string_view phase) -> PhaseCommands {
            PhaseCommands pc;
            if (auto* pht = (*t)[phase].as_table()) {
                if (auto* arr = (*pht)["run"].as_array()) {
                    for (const auto& el : *arr)
                        if (auto s = el.value<std::string>())
                            pc.run.push_back(*s);
                }
            }
            return pc;
        };
        for (auto phase : {"configure", "build", "install_staging", "install_target"}) {
            auto pc = readRun(phase);
            if (!pc.run.empty())
                bi.phases.emplace(std::string(phase), std::move(pc));
        }

        out.build = bi;
    }

    return out;
}

std::optional<Package> parseManifest(const std::filesystem::path& file,
                                     std::vector<ManifestError>& errors) {
    std::ifstream in(file);
    if (!in) {
        errors.push_back({"cannot open file: " + file.string(), 0});
        return std::nullopt;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return parseManifestText(ss.str(), file, errors);
}

} // namespace lightroot
