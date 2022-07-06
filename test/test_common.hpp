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
  EqualsRangeMatcher(Range range) : range{std::move(range)} {}

  template <typename OtherRange> auto match(OtherRange const& other) const -> bool {
    using std::begin;
    using std::end;

    return std::equal(begin(range), end(range), begin(other), end(other), Pred{});
  }

  auto describe() const -> std::string override { return "== " + ::Catch::rangeToString(range); }

private:
  Range range;
};

template <typename T, typename Pred = std::ranges::equal_to, typename Str = std::identity>
struct EqualsDirectMatcher : Catch::Matchers::MatcherGenericBase {
  EqualsDirectMatcher(T base) : base{std::move(base)} {}

  template <typename U> auto match(const U& other) const -> bool { return Pred{}(other, base); }

  auto describe() const -> std::string override { return "== " + ::Catch::Detail::stringify(Str{}(base)); }

private:
  T base;
};

template <typename T>
concept os_adaptable = requires(llvm::raw_ostream& t_os, const T& t_obj) {
  { operator<<(t_os, t_obj) } -> std::same_as<llvm::raw_ostream&>;
};

namespace Catch {
  template <typename T>
  requires os_adaptable<T>
  struct StringMaker<T> {
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
      ss << yume::Token::type_name(token.m_type);
      if (token.m_payload.has_value()) {
        ss << " \"";
        ss.write_escaped(std::string(*token.m_payload));
        ss << "\")";
      }
      return str;
    }
  };

  template <typename T>
  requires std::derived_from<T, yume::ast::AST>
  struct StringMaker<T> {
    static auto convert(const T& token) -> std::string {
      std::string str;
      llvm::raw_string_ostream ss(str);
      yume::diagnostic::PrintVisitor visitor(ss);
      visitor.visit(token, nullptr);
      return str;
    }
  };
} // namespace Catch
