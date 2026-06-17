#include <lightroot/resolver.h>

#include <algorithm>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace lightroot {
namespace {

constexpr std::string_view VIRTUAL_PREFIX = "virtual:";

std::string join(const std::vector<std::string>& v, std::string_view sep) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i)
            out += sep;
        out += v[i];
    }
    return out;
}

std::string label(const std::pair<Scope, std::string>& k) {
    return std::string(to_string(k.first)) + ":" + k.second;
}

/// Extract capability names (without the "virtual:" prefix) from a provides list.
std::vector<std::string> capabilitiesFrom(const std::vector<std::string>& provides) {
    std::vector<std::string> out;
    for (const auto& s : provides) {
        if (s.size() >= VIRTUAL_PREFIX.size() &&
            std::string_view(s).substr(0, VIRTUAL_PREFIX.size()) == VIRTUAL_PREFIX) {
            out.emplace_back(s.substr(VIRTUAL_PREFIX.size()));
        }
    }
    return out;
}

} // namespace

bool BuildPlan::ok() const {
    for (const auto& d : diagnostics) {
        if (d.severity == DiagSeverity::Error)
            return false;
    }
    return true;
}

Resolver::Resolver(const PackageRegistry& reg,
                   std::map<std::string, std::string> explicit_providers)
    : reg_(reg), explicit_(std::move(explicit_providers)) {}

void Resolver::indexProviders() {
    for (const auto& pkg : reg_.packages()) {
        for (const auto& cap : capabilitiesFrom(pkg.provides)) {
            providers_[cap].push_back(Key{pkg.scope, pkg.name});
        }
    }
}

std::optional<Resolver::Key> Resolver::resolveDep(const DependencyRef& dep) {
    if (dep.kind == DepKind::Virtual)
        return resolveVirtual(dep.name);
    return Key{dep.scope, dep.name};
}

std::optional<Resolver::Key> Resolver::resolveVirtual(std::string_view cap) {
    const std::string c(cap);
    if (const auto it = resolved_caps_.find(c); it != resolved_caps_.end())
        return it->second;

    std::vector<Key> cands;
    if (const auto pit = providers_.find(c); pit != providers_.end())
        cands = pit->second;

    auto names = [&] {
        std::vector<std::string> n;
        for (const auto& k : cands)
            n.push_back(label(k));
        return n;
    };

    // Explicit selection wins and is validated.
    if (const auto eit = explicit_.find(c); eit != explicit_.end()) {
        const std::string& want = eit->second;
        const auto found = std::find_if(cands.begin(), cands.end(),
                                        [&](const Key& k) { return k.second == want; });
        if (found == cands.end()) {
            diag({DiagSeverity::Error, "provider-invalid",
                  "explicit provider '" + want + "' for virtual:" + c +
                      " does not provide it (providers: " + join(names(), ", ") + ")",
                  c, names()});
            return std::nullopt;
        }
        resolved_caps_[c] = *found;
        return *found;
    }

    if (cands.empty()) {
        diag({DiagSeverity::Error,
              "provider-missing",
              "virtual:" + c + " is required but no package provides it",
              c,
              {}});
        return std::nullopt;
    }
    if (cands.size() == 1) {
        resolved_caps_[c] = cands[0];
        return cands[0];
    }
    diag({DiagSeverity::Error, "provider-ambiguous",
          "virtual:" + c + " has multiple providers [" + join(names(), ", ") +
              "]; choose one in [providers]",
          c, names()});
    return std::nullopt;
}

void Resolver::visit(Scope scope, std::string_view name, std::vector<Key>& path) {
    const Key key{scope, std::string(name)};
    const Mark m = marks_[key];
    if (m == Mark::Done)
        return;
    if (m == Mark::Visiting) {
        // Back edge -> cycle. Report the path from the repeated node back to itself.
        std::vector<std::string> cyc;
        bool started = false;
        for (const auto& k : path) {
            if (k == key)
                started = true;
            if (started)
                cyc.push_back(label(k));
        }
        cyc.push_back(label(key));
        diag({DiagSeverity::Error,
              "dependency-cycle",
              "dependency cycle: " + join(cyc, " -> "),
              "",
              {}});
        return;
    }

    const Package* pkg = reg_.find(scope, name);
    if (!pkg) {
        diag({DiagSeverity::Error, "missing-package", "package not found: " + label(key), "", {}});
        marks_[key] = Mark::Done;
        return;
    }

    marks_[key] = Mark::Visiting;
    path.push_back(key);

    for (const auto& dep : pkg->depends) {
        const auto target = resolveDep(dep);
        if (!target)
            continue; // already diagnosed
        if (dep.version.cmp != Cmp::Any) {
            if (const Package* tp = reg_.find(target->first, target->second)) {
                if (!dep.version.satisfies(tp->version)) {
                    diag({DiagSeverity::Error,
                          "version-mismatch",
                          label(*target) + " (" + tp->version.to_string() + ") does not satisfy " +
                              dep.to_string(),
                          "",
                          {}});
                }
            }
        }
        visit(target->first, target->second, path);
    }

    path.pop_back();
    marks_[key] = Mark::Done;
    order_.push_back(BuildStep{pkg->scope, pkg->name, pkg->version,
                               pkg->build ? pkg->build->backend : std::string{}, pkg->provides,
                               pkg->depends});
}

void Resolver::scanConflicts() {
    // Exclusive (first-stage default) capability present via >1 provider in the
    // closure is a conflict.
    std::map<std::string, std::vector<std::string>> present;
    for (const auto& s : order_) {
        for (const auto& cap : capabilitiesFrom(s.provides)) {
            present[cap].push_back(label(Key{s.scope, s.name}));
        }
    }
    for (auto& [cap, provs] : present) {
        std::sort(provs.begin(), provs.end());
        provs.erase(std::unique(provs.begin(), provs.end()), provs.end());
        if (provs.size() > 1) {
            diag({DiagSeverity::Error, "provider-conflict",
                  "virtual:" + cap + " has multiple providers in the build [" + join(provs, ", ") +
                      "]; an exclusive capability may have only one",
                  cap, provs});
        }
    }
}

BuildPlan Resolver::resolve(const std::vector<DependencyRef>& roots) {
    indexProviders();
    std::vector<Key> path;
    for (const auto& r : roots) {
        const auto target = resolveDep(r);
        if (!target)
            continue;
        visit(target->first, target->second, path);
    }
    scanConflicts();
    return BuildPlan{std::move(order_), std::move(diags_)};
}

void Resolver::diag(Diagnostic d) {
    diags_.push_back(std::move(d));
}

std::string to_json(const BuildPlan& plan) {
    using nlohmann::json;
    json j;
    j["ok"] = plan.ok();

    json darr = json::array();
    for (const auto& d : plan.diagnostics) {
        json dj;
        dj["severity"] = to_string(d.severity);
        dj["code"] = d.code;
        dj["message"] = d.message;
        if (!d.capability.empty())
            dj["capability"] = d.capability;
        if (!d.providers.empty())
            dj["providers"] = d.providers;
        darr.push_back(std::move(dj));
    }
    j["diagnostics"] = std::move(darr);

    json sarr = json::array();
    for (const auto& s : plan.steps) {
        json sj;
        sj["scope"] = to_string(s.scope);
        sj["name"] = s.name;
        sj["version"] = s.version.to_string();
        sj["backend"] = s.backend;
        json prov = json::array();
        for (const auto& p : s.provides)
            prov.push_back(p);
        sj["provides"] = std::move(prov);
        json deps = json::array();
        for (const auto& dp : s.depends)
            deps.push_back(dp.to_string());
        sj["depends"] = std::move(deps);
        sarr.push_back(std::move(sj));
    }
    j["steps"] = std::move(sarr);

    return j.dump(2);
}

} // namespace lightroot
