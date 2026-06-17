#include <lightroot/registry.h>

#include <utility>

namespace lightroot {

void PackageRegistry::add(Package pkg) {
    const Key key{pkg.scope, pkg.name};
    if (const auto it = index_.find(key); it != index_.end()) {
        Duplicate d{pkg.scope, pkg.name, order_[it->second].origin, pkg.origin};
        dups_.push_back(std::move(d));
        order_[it->second] = std::move(pkg); // later definition overrides
    } else {
        index_[key] = order_.size();
        order_.push_back(std::move(pkg));
    }
}

const Package* PackageRegistry::find(Scope scope, std::string_view name) const {
    const auto it = index_.find(Key{scope, std::string(name)});
    if (it == index_.end())
        return nullptr;
    return &order_[it->second];
}

const Package* PackageRegistry::findTarget(std::string_view name) const {
    return find(Scope::Target, name);
}

const Package* PackageRegistry::findHost(std::string_view name) const {
    return find(Scope::Host, name);
}

} // namespace lightroot
