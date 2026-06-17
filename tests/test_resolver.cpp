#include <lightroot/resolver.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace lightroot;
using json = nlohmann::json;

namespace {

Package makePkg(std::string name, Scope scope, std::vector<std::string> provides = {},
                std::vector<std::string> depends = {}, std::string backend = "make") {
    Package p;
    p.name = std::move(name);
    p.scope = scope;
    p.provides = std::move(provides);
    for (auto& d : depends) {
        p.depends.push_back(parseDependency(d).value());
    }
    p.build = BuildInfo{std::move(backend)};
    return p;
}

std::vector<std::string> names(const BuildPlan& p) {
    std::vector<std::string> v;
    for (const auto& s : p.steps)
        v.push_back(s.name);
    return v;
}

bool hasDiag(const BuildPlan& p, std::string_view code) {
    return std::any_of(p.diagnostics.begin(), p.diagnostics.end(),
                       [&](const Diagnostic& d) { return d.code == code; });
}

bool inPlan(const BuildPlan& p, std::string_view name) {
    return std::any_of(p.steps.begin(), p.steps.end(),
                       [&](const BuildStep& s) { return s.name == name; });
}

} // namespace

TEST(Resolver, ChainOrder) {
    PackageRegistry reg;
    reg.add(makePkg("c", Scope::Target));
    reg.add(makePkg("b", Scope::Target, {}, {"target:c"}));
    reg.add(makePkg("a", Scope::Target, {}, {"target:b"}));
    Resolver r(reg, {});
    auto plan = r.resolve({parseDependency("a").value()});
    ASSERT_TRUE(plan.ok()) << (plan.diagnostics.empty() ? "" : plan.diagnostics[0].message);
    EXPECT_EQ(names(plan), (std::vector<std::string>{"c", "b", "a"}));
}

TEST(Resolver, DiamondDedup) {
    PackageRegistry reg;
    reg.add(makePkg("d", Scope::Target));
    reg.add(makePkg("b", Scope::Target, {}, {"target:d"}));
    reg.add(makePkg("c", Scope::Target, {}, {"target:d"}));
    reg.add(makePkg("a", Scope::Target, {}, {"target:b", "target:c"}));
    Resolver r(reg, {});
    auto plan = r.resolve({parseDependency("a").value()});
    ASSERT_TRUE(plan.ok());
    auto ns = names(plan);
    // d appears once, is first; a is last.
    EXPECT_EQ(std::count(ns.begin(), ns.end(), "d"), 1);
    EXPECT_EQ(ns.front(), "d");
    EXPECT_EQ(ns.back(), "a");
}

TEST(Resolver, Cycle) {
    PackageRegistry reg;
    reg.add(makePkg("a", Scope::Target, {}, {"target:b"}));
    reg.add(makePkg("b", Scope::Target, {}, {"target:a"}));
    Resolver r(reg, {});
    auto plan = r.resolve({parseDependency("a").value()});
    EXPECT_FALSE(plan.ok());
    EXPECT_TRUE(hasDiag(plan, "dependency-cycle"));
}

TEST(Resolver, MissingPackage) {
    PackageRegistry reg;
    Resolver r(reg, {});
    auto plan = r.resolve({parseDependency("ghost").value()});
    EXPECT_FALSE(plan.ok());
    EXPECT_TRUE(hasDiag(plan, "missing-package"));
}

TEST(Resolver, ProviderExplicit) {
    PackageRegistry reg;
    reg.add(makePkg("openssl", Scope::Target, {"virtual:ssl"}));
    reg.add(makePkg("mbedtls", Scope::Target, {"virtual:ssl"}));
    reg.add(makePkg("app", Scope::Target, {}, {"virtual:ssl"}));
    Resolver r(reg, {{"ssl", "openssl"}});
    auto plan = r.resolve({parseDependency("app").value()});
    ASSERT_TRUE(plan.ok()) << (plan.diagnostics.empty() ? "" : plan.diagnostics[0].message);
    EXPECT_TRUE(inPlan(plan, "openssl"));
    EXPECT_FALSE(inPlan(plan, "mbedtls"));
    EXPECT_FALSE(inPlan(plan, "mbedtls"));
}

TEST(Resolver, ProviderSingleAuto) {
    PackageRegistry reg;
    reg.add(makePkg("openssl", Scope::Target, {"virtual:ssl"}));
    reg.add(makePkg("app", Scope::Target, {}, {"virtual:ssl"}));
    Resolver r(reg, {});
    auto plan = r.resolve({parseDependency("app").value()});
    ASSERT_TRUE(plan.ok());
    EXPECT_TRUE(inPlan(plan, "openssl"));
}

TEST(Resolver, ProviderMissing) {
    PackageRegistry reg;
    reg.add(makePkg("app", Scope::Target, {}, {"virtual:ssl"}));
    Resolver r(reg, {});
    auto plan = r.resolve({parseDependency("app").value()});
    EXPECT_FALSE(plan.ok());
    EXPECT_TRUE(hasDiag(plan, "provider-missing"));
}

TEST(Resolver, ProviderAmbiguous) {
    PackageRegistry reg;
    reg.add(makePkg("openssl", Scope::Target, {"virtual:ssl"}));
    reg.add(makePkg("mbedtls", Scope::Target, {"virtual:ssl"}));
    reg.add(makePkg("app", Scope::Target, {}, {"virtual:ssl"}));
    Resolver r(reg, {});
    auto plan = r.resolve({parseDependency("app").value()});
    EXPECT_FALSE(plan.ok());
    EXPECT_TRUE(hasDiag(plan, "provider-ambiguous"));
}

TEST(Resolver, ProviderExplicitInvalid) {
    PackageRegistry reg;
    reg.add(makePkg("openssl", Scope::Target, {"virtual:ssl"}));
    reg.add(makePkg("app", Scope::Target, {}, {"virtual:ssl"}));
    Resolver r(reg, {{"ssl", "wolfssl"}}); // not a provider
    auto plan = r.resolve({parseDependency("app").value()});
    EXPECT_FALSE(plan.ok());
    EXPECT_TRUE(hasDiag(plan, "provider-invalid"));
}

TEST(Resolver, ProviderConflict) {
    // virtual:ssl resolves explicitly to openssl, but mbedtls is also pulled in
    // directly -> two providers of an exclusive capability in the closure.
    PackageRegistry reg;
    reg.add(makePkg("openssl", Scope::Target, {"virtual:ssl"}));
    reg.add(makePkg("mbedtls", Scope::Target, {"virtual:ssl"}));
    reg.add(makePkg("app", Scope::Target, {}, {"virtual:ssl", "target:mbedtls"}));
    Resolver r(reg, {{"ssl", "openssl"}});
    auto plan = r.resolve({parseDependency("app").value()});
    EXPECT_FALSE(plan.ok());
    EXPECT_TRUE(hasDiag(plan, "provider-conflict"));
}

TEST(Resolver, HostDependency) {
    PackageRegistry reg;
    reg.add(makePkg("ninja", Scope::Host));
    reg.add(makePkg("app", Scope::Target, {}, {"host:ninja"}));
    Resolver r(reg, {});
    auto plan = r.resolve({parseDependency("app").value()});
    ASSERT_TRUE(plan.ok());
    // host tool must come before the target package that uses it.
    auto ns = names(plan);
    EXPECT_EQ(ns, (std::vector<std::string>{"ninja", "app"}));
    for (const auto& s : plan.steps) {
        if (s.name == "ninja") {
            EXPECT_EQ(s.scope, Scope::Host);
        }
    }
}

TEST(Resolver, VersionMismatch) {
    PackageRegistry reg;
    Package openssl = makePkg("openssl", Scope::Target, {"virtual:ssl"});
    openssl.version = *parseVersion("3.2.1");
    reg.add(std::move(openssl));
    reg.add(makePkg("app", Scope::Target, {}, {"target:openssl >= 5.0"}));
    Resolver r(reg, {});
    auto plan = r.resolve({parseDependency("app").value()});
    EXPECT_FALSE(plan.ok());
    EXPECT_TRUE(hasDiag(plan, "version-mismatch"));
}

TEST(Resolver, VersionSatisfied) {
    PackageRegistry reg;
    Package openssl = makePkg("openssl", Scope::Target, {"virtual:ssl"});
    openssl.version = *parseVersion("3.2.1");
    reg.add(std::move(openssl));
    reg.add(makePkg("app", Scope::Target, {}, {"target:openssl >= 3.0"}));
    Resolver r(reg, {});
    auto plan = r.resolve({parseDependency("app").value()});
    EXPECT_TRUE(plan.ok());
}

TEST(Resolver, EmptyPlanIsOk) {
    PackageRegistry reg;
    Resolver r(reg, {});
    auto plan = r.resolve({});
    EXPECT_TRUE(plan.ok());
    EXPECT_TRUE(plan.steps.empty());
}

TEST(Resolver, JsonShape) {
    PackageRegistry reg;
    reg.add(makePkg("openssl", Scope::Target, {"virtual:ssl"}, {}, "make"));
    reg.add(makePkg("app", Scope::Target, {}, {"virtual:ssl"}, "cmake"));
    Resolver r(reg, {{"ssl", "openssl"}});
    auto plan = r.resolve({parseDependency("app").value()});
    ASSERT_TRUE(plan.ok());

    const auto txt = to_json(plan);
    ASSERT_NO_THROW({ [[maybe_unused]] auto j = json::parse(txt); });
    auto j = json::parse(txt);
    EXPECT_EQ(j["ok"], true);
    ASSERT_EQ(j["steps"].size(), 2u);
    EXPECT_EQ(j["steps"][0]["name"], "openssl");
    EXPECT_EQ(j["steps"][0]["scope"], "target");
    EXPECT_EQ(j["steps"][1]["name"], "app");
    EXPECT_EQ(j["steps"][1]["backend"], "cmake");
    EXPECT_EQ(j["steps"][1]["depends"][0], "target:virtual:ssl");
}
