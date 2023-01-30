#include "diagnostic/notes.hpp"
#include "compiler/vals.hpp"
#include <filesystem>
#include <fstream>
#include <limits>
#include <llvm/Support/Format.h>
#include <ranges>
#include <string>

namespace yume::diagnostic {
void Note::emit() const {
  if (!holder->context_files.empty() && (holder->prev_loc != this->location || !holder->prev_loc.valid())) {
    holder->prev_loc = this->location;

    auto matching_file = std::ranges::find_if(holder->context_files,
                                              [&](const SourceFile* src) { return src->name == this->location.file; });
    if (matching_file != holder->context_files.end()) {
      *holder->stream << "\n    |\n" << llvm::right_justify(std::to_string(this->location.begin_line), 4) << "| ";
      std::string line;
      auto file = std::fstream{(*matching_file)->path};
      for (int i = 0; i < this->location.begin_line - 1; ++i)
        file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      std::getline(file, line);
      *holder->stream << line << "\n    |" << std::string(this->location.begin_col, ' ') << "^\n";
      // TODO(rymiel): Handle diagnostic locations spanning multiple columns.
      // TODO(rymiel): Handle diagnostic locations spanning multiple lines???
      // TODO(rymiel): Add a splash of color.
    }
  }

  switch (this->severity) {
  case Severity::Note: *holder->stream << "note: "; break;
  case Severity::Warn: *holder->stream << "warn: "; break;
  case Severity::Error: *holder->stream << "error: "; break;
  case Severity::Fatal: *holder->stream << "FATAL: "; break;
  }
  *holder->stream << this->location.single().to_string() << ": " << this->message << "\n";
}

} // namespace yume::diagnostic
