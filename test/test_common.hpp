#pragma once

#include "ast/ast.hpp"
#include "diagnostic/visitor/print_visitor.hpp"
#include "token.hpp"
#include <catch2/matchers/catch_matchers_templated.hpp>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/raw_ostream.h>
#include <ostream>
#include <sstream>
#include <utility>

template <typename Range, typename Pred = std::ranges::equal_to>
struct EqualsRangeMatcher : Catch::Matchers::MatcherGenericBase {
  EqualsRangeMatcher(Range range) : m_range{std::move(range)} {}

  template <typename OtherRange> auto match(OtherRange const& other) const -> bool {
    using std::begin;
    using std::end;

    return std::equal(begin(m_range), end(m_range), begin(other), end(other), Pred{});
  }

  auto describe() const -> std::string override { return "== " + ::Catch::rangeToString(m_range); }

private:
  Range m_range;
};

template <typename T, typename Pred = std::ranges::equal_to, typename Str = std::identity>
struct EqualsDirectMatcher : Catch::Matchers::MatcherGenericBase {
  EqualsDirectMatcher(T base) : m_base{std::move(base)} {}

  template <typename U> auto match(const U& other) const -> bool { return Pred{}(other, m_base); }

  auto describe() const -> std::string override { return "== " + ::Catch::Detail::stringify(Str{}(m_base)); }

private:
  T m_base;
};

template <typename T>
concept os_adaptable = requires(llvm::raw_ostream& t_os, const T& t_obj) {
  { operator<<(t_os, t_obj) } -> std::same_as<llvm::raw_ostream&>;
};

namespace Catch {
template <os_adaptable T> struct StringMaker<T> {
  static auto convert(const T& value) -> std::string {
    std::string str;
    llvm::raw_string_ostream ss(str);
    ss << value;
    return str;
  }
};

template <> struct StringMaker<yume::Token> {
  static auto convert(const yume::Token& token) -> std::string {
    std::string str = "(";
    llvm::raw_string_ostream ss(str);
    ss << yume::Token::type_name(token.type);
    if (token.payload.has_value()) {
      ss << " \"";
      ss.write_escaped(std::string(*token.payload));
      ss << "\")";
    }
    return str;
  }
};

template <std::derived_from<yume::ast::AST> T> struct StringMaker<T> {
  static auto convert(const T& token) -> std::string {
    std::string str;
    llvm::raw_string_ostream ss(str);
    yume::diagnostic::PrintVisitor visitor(ss);
    visitor.visit(token, nullptr);
    return str;
  }
};
} // namespace Catch
