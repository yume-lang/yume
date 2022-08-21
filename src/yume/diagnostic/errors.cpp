#include "errors.hpp"

#include "ast/ast.hpp"
#include "token.hpp"
#include "util.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/raw_ostream.h>
#include <string_view>
#include <utility>

namespace yume {
ASTStackTrace::ASTStackTrace(string message) : message(move(message)) {}

ASTStackTrace::ASTStackTrace(string message, const ast::AST& ast) : message(move(message)) {
  this->message += " (" + ast.location().to_string() + ")";
}

ParserStackTrace::ParserStackTrace(string message) : message(move(message)) {}

ParserStackTrace::ParserStackTrace(string message, const Token& token) : message(move(message)) {
  this->message += " (" + token.loc.to_string() + ")";
}

namespace {
enum Phase : uint8_t { Index, Address, Function, Source, Offset };

struct ContainerLikeSimplify {
  string_view front;
  string_view middle;
};

struct SpanLikeSimplify {
  string_view front;
  string_view end;
};

struct DirectReplaceSimplify {
  string_view from;
  string_view to;
};

class stacktrace_ostream : public llvm::raw_ostream { // NOLINT(readability-identifier-naming): STL-like class
  using enum llvm::raw_ostream::Colors;

  Phase m_current_phase{};
  unsigned char m_template_depth{};
  bool m_unknown{};
  bool m_skip{};
  uint64_t m_total_size{};
  string m_direct_buffer{};
  llvm::raw_string_ostream m_buffer;

  void write_impl(const char* ptr, size_t size) override;

  void simplify(string_view msg);
  auto simplify(string_view msg, ContainerLikeSimplify s) -> bool;
  auto simplify(string_view msg, SpanLikeSimplify s) -> bool;
  auto simplify(string_view msg, DirectReplaceSimplify s) -> bool;

  void format_phase(string_view msg);
  void set_color(llvm::raw_ostream::Colors color);

  [[nodiscard]] auto current_pos() const -> uint64_t override { return m_total_size; }

  static const std::size_t ALLOC_FOR_BUFFER = 128;
  static const bool use_color;

public:
  explicit stacktrace_ostream() : m_buffer(m_direct_buffer) {
    SetUnbuffered();
    m_direct_buffer.reserve(ALLOC_FOR_BUFFER);
  }

  void clean_buffer() {
    if (!m_skip)
      errs() << m_direct_buffer;

    m_skip = false;
    m_direct_buffer.clear();
  }
};

const bool stacktrace_ostream::use_color = errs().has_colors();

// std::unique_ptr<$, std::default_delete<$> >
constexpr ContainerLikeSimplify UPTR_SIMPLIFY = {"std::unique_ptr<", ", std::default_delete<"};
// std::vector<$, std::allocator<$> >
constexpr ContainerLikeSimplify VEC_SIMPLIFY = {"std::vector<", ", std::allocator<"};

// std::span<$, 18446744073709551615ul>
constexpr SpanLikeSimplify SPAN_SIMPLIFY = {"std::span<", ", 18446744073709551615ul>"};

constexpr DirectReplaceSimplify STRING_SIMPLIFY = {
    "std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >", "std::string"};
constexpr DirectReplaceSimplify STRINGSTREAM_SIMPLIFY = {
    "std::__cxx11::basic_stringstream<char, std::char_traits<char>, std::allocator<char> >", "std::stringstream"};
constexpr DirectReplaceSimplify ANONYMOUS_NS_SIMPLIFY = {"::(anonymous namespace)::", ":::"};
constexpr DirectReplaceSimplify DECL_LIKE_SIMPLIFY = {
    "std::variant<std::monostate, yume::Fn*, yume::Struct*, yume::Ctor*>", "yume::DeclLike"};

auto stacktrace_ostream::simplify(string_view msg, ContainerLikeSimplify s) -> bool {
  constexpr static const string_view MIDDLE_END = "> >";
  constexpr static const string_view END = ">";

  auto uptr_start = msg.find(s.front);
  if (uptr_start == string::npos)
    return false;

  auto uptr_delete_start = msg.find(s.middle, uptr_start);
  if (uptr_delete_start == string::npos)
    return false;

  auto contained_start = uptr_start + s.front.length();
  auto contained_typename = msg.substr(contained_start, uptr_delete_start - contained_start);
  auto from_deleter = msg.substr(uptr_delete_start + s.middle.length());

  if (!from_deleter.starts_with(contained_typename))
    return false;

  if (!from_deleter.substr(contained_typename.length()).starts_with(MIDDLE_END))
    return false;

  auto uptr_end = uptr_delete_start + s.middle.length() + contained_typename.length() + MIDDLE_END.length();

  simplify(msg.substr(0, uptr_start));
  m_buffer << s.front << contained_typename << END;
  simplify(msg.substr(uptr_end));

  return true;
};

auto stacktrace_ostream::simplify(string_view msg, SpanLikeSimplify s) -> bool {
  constexpr static const string_view END = ">";

  auto span_start = msg.find(s.front);
  if (span_start == string::npos)
    return false;

  auto span_bound_start = msg.find(s.end, span_start);
  if (span_bound_start == string::npos)
    return false;

  auto contained_start = span_start + s.front.length();
  auto contained_typename = msg.substr(contained_start, span_bound_start - contained_start);

  auto span_end = span_bound_start + s.end.length();

  simplify(msg.substr(0, span_start));
  m_buffer << s.front << contained_typename << END;
  simplify(msg.substr(span_end));

  return true;
};

auto stacktrace_ostream::simplify(string_view msg, DirectReplaceSimplify s) -> bool {
  auto start = msg.find(s.from);
  if (start == string::npos)
    return false;

  simplify(msg.substr(0, start));
  m_buffer << s.to;
  simplify(msg.substr(start + s.from.size()));

  return true;
}

void stacktrace_ostream::simplify(string_view msg) {
  bool found_any = simplify(msg, UPTR_SIMPLIFY) || simplify(msg, VEC_SIMPLIFY) || simplify(msg, STRING_SIMPLIFY) ||
                   simplify(msg, STRINGSTREAM_SIMPLIFY) || simplify(msg, ANONYMOUS_NS_SIMPLIFY) ||
                   simplify(msg, SPAN_SIMPLIFY) || simplify(msg, DECL_LIKE_SIMPLIFY);

  if (found_any)
    return;

  size_t prev_pos = 0;
  size_t start_pos = 0;
  while (true) {
    start_pos = msg.find_first_of("<>()", prev_pos);
    auto substr = msg.substr(prev_pos, start_pos == string::npos ? start_pos : start_pos - prev_pos);

    if (m_template_depth == 0)
      set_color(CYAN);

    m_buffer << substr;

    if (m_template_depth == 0 && use_color)
      m_buffer << llvm::sys::Process::ResetColor();

    if (start_pos == string::npos)
      break;

    auto angle = msg.at(start_pos);
    m_template_depth += (angle == '<' || angle == '(') ? 1 : -1;
    m_buffer << angle;
    prev_pos = start_pos + 1;
  }
}

void stacktrace_ostream::set_color(llvm::raw_ostream::Colors color) {
  if (color != RESET && use_color)
    m_buffer << llvm::sys::Process::OutputColor(static_cast<char>(color), false, false);
};

void stacktrace_ostream::format_phase(string_view msg) {
  static array skip_lines = {"yume::CRTPWalker<"sv, "__libc_start_"sv};
  static string source_dir = YUME_SRC_DIR;

  switch (m_current_phase) {
  case Address: m_buffer << msg; break;
  case Index:
    set_color(GREEN);
    m_buffer << msg;
    break;
  case Offset:
    set_color(RED);
    m_buffer << msg;
    break;
  case Function:
    set_color(CYAN);
    if (std::ranges::any_of(skip_lines, [&](string_view s) noexcept { return msg.find(s) != string::npos; }))
      m_skip = true;
    else
      simplify(msg);
    break;
  case Source:
    set_color(YELLOW);
    if (auto normal_path = std::filesystem::path(msg).lexically_normal().native();
        normal_path.starts_with(source_dir) && use_color) {
      m_buffer << llvm::sys::Process::OutputBold(false);
      m_buffer << static_cast<string_view>(normal_path).substr(source_dir.size());
    } else {
      m_buffer << normal_path;
    }
  };

  if (use_color)
    m_buffer << llvm::sys::Process::ResetColor();
}

void stacktrace_ostream::write_impl(const char* ptr, size_t size) {
  auto msg = string_view(ptr, size);
  m_total_size += size;

  if (size == 1 && llvm::isSpace(msg.at(0))) {
    m_buffer << msg.at(0);
    return;
  }

  if (m_unknown) {
    m_unknown = msg.at(0) != ')';
  } else if (msg.at(0) == '(') {
    m_skip = m_current_phase == Function;
    m_unknown = true;
    m_skip = true;
    m_current_phase = Offset;
  } else if (msg.at(0) == '/' && m_current_phase != Source) {
    m_current_phase = Source;
  }

  format_phase(msg);

  int counter = static_cast<int>(m_current_phase);
  if (m_current_phase != Offset) {
    ++counter;
    counter %= 4;
  }

  m_current_phase = static_cast<Phase>(counter);
  if (m_current_phase == Offset && !m_unknown)
    m_current_phase = Index;

  if (m_current_phase == Index)
    clean_buffer();
}

} // namespace

void backtrace(void* /*unused*/) {
  static stacktrace_ostream stream{};
  llvm::sys::PrintStackTrace(stream);
  stream.clean_buffer();
}
} // namespace yume
