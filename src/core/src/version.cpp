#include <lightroot/expected.h>
#include <lightroot/version.h>

namespace lightroot {

expected<Version, std::string> parseVersion(std::string_view s) {
    Version v;
    int part = 0; // 0=major, 1=minor, 2=patch
    std::size_t i = 0;
    while (i < s.size() && part <= 2) {
        if (s[i] < '0' || s[i] > '9')
            return unexpected(std::string{"invalid version: non-digit '"} + s[i] + "'");
        std::uint32_t n = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            n = n * 10 + static_cast<std::uint32_t>(s[i] - '0');
            ++i;
        }
        if (part == 0)
            v.major = n;
        else if (part == 1)
            v.minor = n;
        else
            v.patch = n;
        ++part;
        if (i < s.size()) {
            if (s[i] != '.')
                break; // trailing junk handled below
            ++i;       // consume '.'
            if (i >= s.size())
                return unexpected(std::string{"invalid version: trailing '.'"});
        }
    }
    if (part == 0)
        return unexpected(std::string{"invalid version: empty"});
    if (i != s.size())
        return unexpected(std::string{"invalid version: unexpected suffix '"} +
                          std::string(s.substr(i)) + "'");
    return v;
}

int Version::compare(const Version& o) const {
    if (major != o.major)
        return major < o.major ? -1 : 1;
    if (minor != o.minor)
        return minor < o.minor ? -1 : 1;
    if (patch != o.patch)
        return patch < o.patch ? -1 : 1;
    return 0;
}

std::string Version::to_string() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

bool VersionConstraint::satisfies(const Version& v) const {
    if (cmp == Cmp::Any)
        return true;
    const int c = v.compare(ver);
    switch (cmp) {
        case Cmp::Eq:
            return c == 0;
        case Cmp::Ne:
            return c != 0;
        case Cmp::Ge:
            return c >= 0;
        case Cmp::Gt:
            return c > 0;
        case Cmp::Le:
            return c <= 0;
        case Cmp::Lt:
            return c < 0;
        default:
            return true;
    }
}

std::string VersionConstraint::to_string() const {
    if (cmp == Cmp::Any)
        return {};
    const char* op = "==";
    switch (cmp) {
        case Cmp::Eq:
            op = "==";
            break;
        case Cmp::Ne:
            op = "!=";
            break;
        case Cmp::Ge:
            op = ">=";
            break;
        case Cmp::Gt:
            op = ">";
            break;
        case Cmp::Le:
            op = "<=";
            break;
        case Cmp::Lt:
            op = "<";
            break;
        default:
            break;
    }
    return std::string(op) + " " + ver.to_string();
}

expected<VersionConstraint, std::string> parseVersionConstraint(std::string_view op,
                                                                std::string_view version_str) {
    Cmp cmp;
    if (op == "=" || op == "==")
        cmp = Cmp::Eq;
    else if (op == "!=")
        cmp = Cmp::Ne;
    else if (op == ">=")
        cmp = Cmp::Ge;
    else if (op == ">")
        cmp = Cmp::Gt;
    else if (op == "<=")
        cmp = Cmp::Le;
    else if (op == "<")
        cmp = Cmp::Lt;
    else
        return unexpected(std::string{"unknown operator: "} + std::string(op));

    auto v = parseVersion(version_str);
    if (!v)
        return unexpected(std::string{"invalid version '"} + std::string(version_str) +
                          "': " + v.error());
    return VersionConstraint{cmp, *v};
}

} // namespace lightroot
