#pragma once

#include "token.hpp"
#include "util.hpp"
#include <llvm/Support/Format.h>
#include <llvm/Support/raw_ostream.h>
#include <sstream>

namespace yume {
struct SourceFile;
}

namespace yume::diagnostic {

enum struct Severity { Note, Warn, Error };

struct Note {
public:
  string message;
  Loc location;
  Severity severity = Severity::Note;
};

class NotesHolder {
private:
  vector<Note> m_notes{};

public:
  auto emit(Loc location, Severity severity = Severity::Note) -> llvm::raw_string_ostream {
    auto& new_note = m_notes.emplace_back(Note{"", location, severity});

    return llvm::raw_string_ostream{new_note.message};
  }

  void dump(llvm::raw_ostream& stream, const vector<SourceFile*>& context_files) const;
  void dump(llvm::raw_ostream& stream) const;
};
} // namespace yume::diagnostic
