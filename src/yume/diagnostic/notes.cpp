#include "diagnostic/notes.hpp"
#include "compiler/vals.hpp"
#include <fstream>
#include <string>

namespace yume::diagnostic {
void NotesHolder::dump(llvm::raw_ostream& stream) const { dump(stream, {}); }

void NotesHolder::dump(llvm::raw_ostream& stream, const vector<SourceFile*>& context_files) const {
  Loc prev_loc = {};

  for (const auto& note : m_notes) {
    if (!context_files.empty() && (prev_loc != note.location || !prev_loc.valid())) {
      prev_loc = note.location;

      auto matching_file =
          std::ranges::find_if(context_files, [&](const SourceFile* src) { return src->name == note.location.file; });
      if (matching_file != context_files.end()) {
        stream << "\n    |\n" << llvm::right_justify(std::to_string(note.location.begin_line), 4) << "| ";
        std::string line;
        std::fstream file = (*matching_file)->path;
        for (int i = 0; i < note.location.begin_line - 1; ++i)
          file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::getline(file, line);
        stream << line << "\n    |" << std::string(note.location.begin_col, ' ') << "^\n";
        // TODO(rymiel): Handle diagnostic locations spanning multiple columns.
        // TODO(rymiel): Handle diagnostic locations spanning multiple lines???
        // TODO(rymiel): Add a splash of color.
      }
    }

    switch (note.severity) {
    case Severity::Note: stream << "note: "; break;
    case Severity::Warn: stream << "warn: "; break;
    case Severity::Error: stream << "error: "; break;
    }
    stream << note.location.single().to_string() << ": " << note.message << "\n";
  }
}

} // namespace yume::diagnostic
