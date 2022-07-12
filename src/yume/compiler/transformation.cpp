#include "transformation.hpp"

#include "ast/ast.hpp"
#include <memory>
#include <span>
#include <utility>

namespace yume {
template <typename T> auto dup(const vector<T>& items) {
  auto dup = vector<T>();
  dup.reserve(items.size());
  for (auto& i : items) {
    auto* cloned = i.clone();
    dup.push_back(move(*cloned));
    delete cloned; // Need to free the cloned object even though it was moved from
  }

  return dup;
}

auto transform_enum(ast::EnumDecl& decl) -> unique_ptr<ast::StructDecl> {
  // TODO: preserve more location
  auto body = ast::Compound{{}, {}};
  return std::make_unique<ast::StructDecl>(decl.token_range(), decl.name(), dup(decl.fields()), vector<string>{},
                                           move(body));
};
} // namespace yume
