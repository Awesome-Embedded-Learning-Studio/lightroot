#include <lightroot/types.h>

namespace lightroot {

const char* to_string(Scope s) {
    return s == Scope::Host ? "host" : "target";
}
const char* to_string(DepKind k) {
    return k == DepKind::Virtual ? "virtual" : "package";
}
const char* to_string(ProviderCardinality c) {
    return c == ProviderCardinality::Additive ? "additive" : "exclusive";
}

} // namespace lightroot
