#pragma once

#include "token.hpp"
#include "util.hpp"
#include <llvm/Support/raw_ostream.h>
#include <sstream>

namespace yume::diagnostic {
struct Note {
public:
  string message;
  Loc location;
};

class NotesHolder {
private:
  vector<Note> m_notes{};

public:
  auto emit(Loc location) -> llvm::raw_string_ostream {
    auto& new_note = m_notes.emplace_back(Note{"", location});

    return llvm::raw_string_ostream{new_note.message};
  }

  void dump(llvm::raw_ostream& stream) const {
    for (const auto& i : m_notes)
      stream << "note: " << i.location.single().to_string() << ": " << i.message << "\n";
  }
};
} // namespace yume::diagnostic
