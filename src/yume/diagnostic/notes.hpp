#pragma once

#include "token.hpp"
#include "util.hpp"
#include <llvm/Support/Format.h>
#include <llvm/Support/raw_ostream.h>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace yume {
struct SourceFile;
}

namespace yume::diagnostic {

enum struct Severity { Note, Warn, Error, Fatal };

struct NotesHolder;

struct Note {
public:
  NotesHolder* holder;
  string message;
  Loc location;
  Severity severity;

  Note(NotesHolder* holder, string message, Loc location, Severity severity = Severity::Note)
      : holder{holder}, message{std::move(message)}, location{location}, severity{severity} {}

  Note(const Note&) = delete;
  Note(Note&&) = default;
  auto operator=(const Note&) -> Note& = delete;
  auto operator=(Note&&) -> Note& = default;

  ~Note() noexcept(false) {
    emit();

    if (this->severity == Severity::Fatal)
      throw std::runtime_error(message);
  }

  void emit() const;

  auto operator<<(auto&& other) -> Note& {
    llvm::raw_string_ostream{this->message} << std::forward<decltype(other)>(other);
    return *this;
  }
};

struct NotesHolder {
public:
  vector<SourceFile*> context_files;
  llvm::raw_ostream* stream{&llvm::errs()};
  Loc prev_loc{};

  auto emit(Loc location, Severity severity = Severity::Note) -> Note { return {this, "", location, severity}; }
};

struct StringNotesHolder : public NotesHolder {
public:
  string buffer{};
  unique_ptr<llvm::raw_string_ostream> buffer_stream = std::make_unique<llvm::raw_string_ostream>(buffer);

  StringNotesHolder(vector<SourceFile*> context_files = {}) : NotesHolder{std::move(context_files), nullptr} {
    this->stream = buffer_stream.get();
  }
};
} // namespace yume::diagnostic
