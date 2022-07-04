#include "errors.hpp"

#include "ast/ast.hpp"
#include "token.hpp"
#include "util.hpp"
#include <filesystem>
#include <limits>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/raw_ostream.h>
#include <string_view>
#include <utility>

namespace yume {
ASTStackTrace::ASTStackTrace(std::string message) : m_message(std::move(message)) {}

ASTStackTrace::ASTStackTrace(std::string message, const ast::AST& ast) : m_message(std::move(message)) {
  m_message += " (" + ast.location().to_string() + ")";
}

namespace {
enum Phase : uint8_t { Index, Address, Function, Source, Offset };

struct ContainerLikeSimplify {
  std::string_view front;
  std::string_view middle;
};

struct DirectReplaceSimplify {
  std::string_view from;
  std::string_view to;
};

class stacktrace_ostream : public llvm::raw_ostream {
  using enum llvm::raw_ostream::Colors;

  Phase current_phase{};
  unsigned char template_depth{};
  bool unknown{};
  bool skip{};
  uint64_t total_size{};
  std::string direct_buffer{};
  llvm::raw_string_ostream buffer;

  void write_impl(const char* Ptr, size_t Size) override;

  void simplify(std::string_view msg);
  auto simplify(std::string_view msg, ContainerLikeSimplify s) -> bool;
  auto simplify(std::string_view msg, DirectReplaceSimplify s) -> bool;

  void format_phase(std::string_view msg);
  void set_color(llvm::raw_ostream::Colors color);

  [[nodiscard]] auto current_pos() const -> uint64_t override { return total_size; }

  static const std::size_t ALLOC_FOR_BUFFER = 128;
  static const bool use_color;

public:
  explicit stacktrace_ostream() : buffer(direct_buffer) {
    SetUnbuffered();
    direct_buffer.reserve(ALLOC_FOR_BUFFER);
  }

  void clean_buffer() {
    if (!skip)
      llvm::errs() << direct_buffer;

    skip = false;
    direct_buffer.clear();
  }
};

const bool stacktrace_ostream::use_color = llvm::errs().has_colors();

// std::unique_ptr<$, std::default_delete<$> >
constexpr ContainerLikeSimplify UPTR_SIMPLIFY = {"std::unique_ptr<", ", std::default_delete<"};
// std::vector<$, std::allocator<$> >
constexpr ContainerLikeSimplify VEC_SIMPLIFY = {"std::vector<", ", std::allocator<"};

constexpr DirectReplaceSimplify STRING_SIMPLIFY = {
    "std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >", "std::string"};
constexpr DirectReplaceSimplify STRINGSTREAM_SIMPLIFY = {
    "std::__cxx11::basic_stringstream<char, std::char_traits<char>, std::allocator<char> >", "std::stringstream"};
constexpr DirectReplaceSimplify ANONYMOUS_NS_SIMPLIFY = {"::(anonymous namespace)::", ":::"};

auto stacktrace_ostream::simplify(std::string_view msg, ContainerLikeSimplify s) -> bool {
  constexpr static const std::string_view middle_end = "> >";
  constexpr static const std::string_view end = ">";

  auto uptr_start = msg.find(s.front);
  if (uptr_start == std::string::npos)
    return false;

  auto uptr_delete_start = msg.find(s.middle, uptr_start);
  if (uptr_delete_start == std::string::npos)
    return false;

  auto contained_start = uptr_start + s.front.length();
  auto contained_typename = msg.substr(contained_start, uptr_delete_start - contained_start);
  auto from_deleter = msg.substr(uptr_delete_start + s.middle.length());

  if (!from_deleter.starts_with(contained_typename))
    return false;

  if (!from_deleter.substr(contained_typename.length()).starts_with(middle_end))
    return false;

  auto uptr_end = uptr_delete_start + s.middle.length() + contained_typename.length() + middle_end.length();

  simplify(msg.substr(0, uptr_start));
  buffer << s.front << contained_typename << end;
  simplify(msg.substr(uptr_end));

  return true;
};

auto stacktrace_ostream::simplify(std::string_view msg, DirectReplaceSimplify s) -> bool {
  auto start = msg.find(s.from);
  if (start == std::string::npos)
    return false;

  simplify(msg.substr(0, start));
  buffer << s.to;
  simplify(msg.substr(start + s.from.size()));

  return true;
}

void stacktrace_ostream::simplify(std::string_view msg) {
  bool found_any = simplify(msg, UPTR_SIMPLIFY) || simplify(msg, VEC_SIMPLIFY) || simplify(msg, STRING_SIMPLIFY) ||
                   simplify(msg, STRINGSTREAM_SIMPLIFY) || simplify(msg, ANONYMOUS_NS_SIMPLIFY);

  if (found_any)
    return;

  size_t prev_pos = 0;
  size_t start_pos = 0;
  while (true) {
    start_pos = msg.find_first_of("<>()", prev_pos);
    auto substr = msg.substr(prev_pos, start_pos == std::string::npos ? start_pos : start_pos - prev_pos);

    if (template_depth == 0)
      set_color(CYAN);

    buffer << substr;

    if (template_depth == 0 && use_color)
      buffer << llvm::sys::Process::ResetColor();

    if (start_pos == std::string::npos)
      break;

    auto angle = msg.at(start_pos);
    template_depth += (angle == '<' || angle == '(') ? 1 : -1;
    buffer << angle;
    prev_pos = start_pos + 1;
  }
}

void stacktrace_ostream::set_color(llvm::raw_ostream::Colors color) {
  if (color != RESET && use_color)
    buffer << llvm::sys::Process::OutputColor(static_cast<char>(color), false, false);
};

void stacktrace_ostream::format_phase(std::string_view msg) {
  using namespace std::literals;
  static std::array skip_lines = {"yume::CRTPWalker<"sv, "__libc_start_"sv};
  static std::string source_dir = YUME_SRC_DIR;

  switch (current_phase) {
  case Address: buffer << msg; break;
  case Index:
    set_color(GREEN);
    buffer << msg;
    break;
  case Offset:
    set_color(RED);
    buffer << msg;
    break;
  case Function:
    set_color(CYAN);
    if (std::ranges::any_of(skip_lines, [&](std::string_view s) { return msg.find(s) != std::string::npos; }))
      skip = true;
    else
      simplify(msg);
    break;
  case Source:
    set_color(YELLOW);
    if (auto normal_path = std::filesystem::path(msg).lexically_normal().native();
        normal_path.starts_with(source_dir) && use_color) {
      buffer << llvm::sys::Process::OutputBold(false);
      buffer << static_cast<std::string_view>(normal_path).substr(source_dir.size());
    } else {
      buffer << normal_path;
    }
  };

  if (use_color)
    buffer << llvm::sys::Process::ResetColor();
}

void stacktrace_ostream::write_impl(const char* ptr, size_t size) {

  auto msg = std::string_view(ptr, size);
  total_size += size;

  if (size == 1 && llvm::isSpace(msg.at(0))) {
    buffer << msg.at(0);
    return;
  }

  if (unknown) {
    unknown = msg.at(0) != ')';
  } else if (msg.at(0) == '(') {
    skip = current_phase == Function;
    unknown = true;
    current_phase = Offset;
  } else if (msg.at(0) == '/' && current_phase != Source) {
    current_phase = Source;
  }

  format_phase(msg);

  int counter = static_cast<int>(current_phase);
  if (current_phase != Offset)
    counter = ++counter % 4;

  if (counter == 0)
    clean_buffer();

  current_phase = static_cast<Phase>(counter);
  if (current_phase == Offset && !unknown)
    current_phase = Index;
}

} // namespace

void backtrace(void* /*unused*/) {
  static stacktrace_ostream stream{};
  llvm::sys::PrintStackTrace(stream);
  stream.clean_buffer();
}
} // namespace yume
