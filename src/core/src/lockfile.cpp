#include <lightroot/lockfile.h>

#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace lightroot {

namespace {

/// Lockfile schema version written into every lockfile.
constexpr int SCHEMA_VERSION = 1;

/// Parse a scope name; nullopt if unrecognized.
std::optional<Scope> parseScope(std::string_view s) {
    if (s == "host")
        return Scope::Host;
    if (s == "target")
        return Scope::Target;
    return std::nullopt;
}

/// Canonical lockfile key for a step: "scope:name".
std::string stepKey(Scope scope, std::string_view name) {
    return std::string(to_string(scope)) + ":" + std::string(name);
}

} // namespace

std::string serializeLockfile(const BuildPlan& plan, const ToolchainConfig& tc,
                              const std::vector<std::string>& roots,
                              const std::map<std::string, std::string>& providers) {
    using nlohmann::json;
    json j;
    j["schema"] = SCHEMA_VERSION;
    j["target"] = tc.target;
    j["roots"] = roots;
    j["providers"] = providers;

    json sarr = json::array();
    for (const auto& s : plan.steps) {
        json sj;
        sj["scope"] = to_string(s.scope);
        sj["name"] = s.name;
        sj["version"] = s.version.to_string();
        sarr.push_back(std::move(sj));
    }
    j["steps"] = std::move(sarr);

    return j.dump(2);
}

std::optional<Lockfile> parseLockfileText(std::string_view text,
                                          std::vector<LockfileError>& errors) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& e) {
        errors.push_back({std::string(e.what())});
        return std::nullopt;
    }

    Lockfile lock;
    lock.schema_version = j.value("schema", SCHEMA_VERSION);
    lock.target = j.value("target", std::string{});
    if (auto r = j.value("roots", std::vector<std::string>{}); !r.empty())
        lock.roots = std::move(r);
    if (auto p = j.value("providers", std::map<std::string, std::string>{}); !p.empty())
        lock.providers = std::move(p);

    const auto steps = j.value("steps", std::vector<nlohmann::json>{});
    for (const auto& sj : steps) {
        LockedStep ls;
        ls.name = sj.value("name", std::string{});
        if (ls.name.empty()) {
            errors.push_back({"lockfile step missing 'name'"});
            continue;
        }
        const auto scopeStr = sj.value("scope", std::string{"target"});
        const auto scope = parseScope(scopeStr);
        if (!scope) {
            errors.push_back({"lockfile step '" + ls.name + "' has unknown scope: " + scopeStr});
            continue;
        }
        ls.scope = *scope;
        const auto verStr = sj.value("version", std::string{});
        if (auto v = parseVersion(verStr)) {
            ls.version = *v;
        } else {
            errors.push_back({"lockfile step '" + ls.name + "' has invalid version '" + verStr +
                              "': " + v.error()});
            continue;
        }
        lock.steps.push_back(std::move(ls));
    }

    return lock;
}

std::optional<Lockfile> parseLockfile(const std::filesystem::path& file,
                                      std::vector<LockfileError>& errors) {
    std::ifstream in(file);
    if (!in) {
        errors.push_back({"cannot open file: " + file.string()});
        return std::nullopt;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return parseLockfileText(ss.str(), errors);
}

bool writeLockfile(const std::filesystem::path& file, const BuildPlan& plan,
                   const ToolchainConfig& tc, const std::vector<std::string>& roots,
                   const std::map<std::string, std::string>& providers) {
    std::error_code ec;
    if (file.has_parent_path())
        std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream of(file);
    if (!of)
        return false;
    of << serializeLockfile(plan, tc, roots, providers);
    return static_cast<bool>(of);
}

bool LockfileDrift::empty() const {
    return added.empty() && removed.empty() && changed.empty();
}

std::string LockfileDrift::summary() const {
    std::ostringstream os;
    for (const auto& a : added)
        os << "  + added:   " << a << "\n";
    for (const auto& r : removed)
        os << "  - removed: " << r << "\n";
    for (const auto& c : changed)
        os << "  ~ changed: " << c << "\n";
    return os.str();
}

LockfileDrift checkLockfileDrift(const Lockfile& lock, const BuildPlan& plan) {
    // Index the locked closure by (scope, name) -> version.
    std::map<std::string, std::string> locked;
    for (const auto& s : lock.steps)
        locked[stepKey(s.scope, s.name)] = s.version.to_string();

    std::set<std::string> seen;
    LockfileDrift drift;
    for (const auto& s : plan.steps) {
        const auto key = stepKey(s.scope, s.name);
        seen.insert(key);
        const auto planVer = s.version.to_string();
        const auto it = locked.find(key);
        if (it == locked.end()) {
            drift.added.push_back(s.name + "@" + planVer);
        } else if (it->second != planVer) {
            drift.changed.push_back(s.name + ": " + it->second + " -> " + planVer);
        }
    }
    for (const auto& s : lock.steps) {
        const auto key = stepKey(s.scope, s.name);
        if (seen.find(key) == seen.end())
            drift.removed.push_back(s.name + "@" + s.version.to_string());
    }
    return drift;
}

} // namespace lightroot
