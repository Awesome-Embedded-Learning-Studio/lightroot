#include <lightroot/dependency.h>
#include <lightroot/expected.h>

#include <cctype>
#include <utility>

namespace lightroot {
namespace {

/// Minimal cursor over a dependency spec string, with 1-based column tracking.
struct Cursor {
    std::string_view s;
    std::size_t pos = 0;

    bool eof() const { return pos >= s.size(); }
    char peek() const { return eof() ? '\0' : s[pos]; }
    char get() { return eof() ? '\0' : s[pos++]; }
    std::size_t column() const { return pos + 1; } // 1-based

    void skipWs() {
        while (!eof() && std::isspace(static_cast<unsigned char>(peek())))
            ++pos;
    }

    /// Identifier chars for names/versions/features: alnum and . _ - +.
    std::string readIdent() {
        const std::size_t start = pos;
        while (!eof()) {
            const char c = peek();
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '+' ||
                c == '.') {
                ++pos;
            } else {
                break;
            }
        }
        return std::string(s.substr(start, pos - start));
    }
};

bool tryScope(Cursor& c, Scope& out) {
    // Match "host:" or "target:" at the current position.
    static constexpr std::string_view HOST_PREFIX = "host:";
    static constexpr std::string_view TARGET_PREFIX = "target:";
    for (auto [prefix, sc] :
         {std::pair{HOST_PREFIX, Scope::Host}, std::pair{TARGET_PREFIX, Scope::Target}}) {
        if (c.pos + prefix.size() <= c.s.size() && c.s.substr(c.pos, prefix.size()) == prefix) {
            out = sc;
            c.pos += prefix.size();
            c.skipWs();
            return true;
        }
    }
    return false;
}

} // namespace

expected<DependencyRef, ParseError> parseDependency(std::string_view spec) {
    Cursor c{spec};
    c.skipWs();

    DependencyRef dep;

    Scope sc = dep.scope;
    if (tryScope(c, sc))
        dep.scope = sc;

    if (c.peek() == '\0') {
        return unexpected(ParseError{"empty dependency spec", c.column()});
    }

    // virtual: prefix?
    constexpr std::string_view VK = "virtual:";
    if (c.pos + VK.size() <= c.s.size() && c.s.substr(c.pos, VK.size()) == VK) {
        c.pos += VK.size();
        dep.kind = DepKind::Virtual;
    }

    dep.name = c.readIdent();
    if (dep.name.empty()) {
        return unexpected(ParseError{"expected package or virtual capability name", c.column()});
    }

    c.skipWs();

    // optional version constraint: comparator then version.
    if (!c.eof()) {
        const char ch = c.peek();
        if (ch == '>' || ch == '<' || ch == '=' || ch == '!') {
            std::string op;
            op.push_back(c.get());
            if (!c.eof()) {
                const char n = c.peek();
                if (n == '=' || n == '>' || n == '<')
                    op.push_back(c.get());
            }
            c.skipWs();
            const std::string vstr = c.readIdent();
            if (vstr.empty()) {
                return unexpected(ParseError{"expected version after '" + op + "'", c.column()});
            }
            auto vc = parseVersionConstraint(op, vstr);
            if (!vc) {
                return unexpected(ParseError{"invalid version constraint '" + op + " " + vstr +
                                                 "': " + vc.error(),
                                             c.column()});
            }
            dep.version = *vc;
            c.skipWs();
        }
    }

    // optional features: [a, b]
    if (!c.eof() && c.peek() == '[') {
        c.get(); // consume '['
        c.skipWs();
        while (!c.eof() && c.peek() != ']') {
            std::string f = c.readIdent();
            if (f.empty()) {
                return unexpected(ParseError{"expected feature name inside []", c.column()});
            }
            dep.features.push_back(std::move(f));
            c.skipWs();
            if (!c.eof() && c.peek() == ',') {
                c.get();
                c.skipWs();
            }
        }
        if (c.eof()) {
            return unexpected(ParseError{"unterminated feature list, missing ']'", c.column()});
        }
        c.get(); // consume ']'
        c.skipWs();
    }

    if (!c.eof()) {
        return unexpected(ParseError{
            std::string("unexpected trailing character '") + c.peek() + "'", c.column()});
    }

    return dep;
}

std::string DependencyRef::to_string() const {
    std::string out;
    out += lightroot::to_string(scope);
    out += ':';
    if (kind == DepKind::Virtual)
        out += "virtual:";
    out += name;
    if (version.cmp != Cmp::Any) {
        out += ' ';
        out += version.to_string();
    }
    if (!features.empty()) {
        out += '[';
        for (std::size_t i = 0; i < features.size(); ++i) {
            if (i)
                out += ',';
            out += features[i];
        }
        out += ']';
    }
    return out;
}

} // namespace lightroot
