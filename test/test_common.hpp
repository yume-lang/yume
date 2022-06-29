#pragma once

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

  auto describe() const -> std::string override { return "== " + Catch::rangeToString(range); }

private:
  Range range;
};

static constexpr auto token_comparison = [](const yume::Token& a, const yume::Token& b) -> bool {
  return a.m_type == b.m_type && a.m_payload == b.m_payload;
};

template <typename... Ts> auto EqualsTokens(Ts... ts) {
  return EqualsRangeMatcher<std::vector<yume::Token>, decltype(token_comparison)>{{ts...}};
}

template <typename T>
concept os_adaptable = requires(llvm::raw_ostream& t_os, const T& t_obj) {
  { operator<<(t_os, t_obj) } -> std::same_as<llvm::raw_ostream&>;
};

namespace Catch {
  template <typename T>
  requires os_adaptable<T>
  struct StringMaker<T> {
    static auto convert(T const& value) -> std::string {
      std::string str;
      llvm::raw_string_ostream ss(str);
      ss << value;
      return str;
    }
  };
} // namespace Catch
