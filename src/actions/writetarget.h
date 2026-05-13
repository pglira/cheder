#pragma once

#include <QString>

#include <functional>

class ActionLogger;

// File-write protocol shared by actions. Two concerns rolled into one module
// because they're always used together:
//
//   1. Resolve where to write under an output directory, applying an overwrite
//      policy (Overwrite / Skip / Rename).
//   2. Write atomically via a sibling ".part" file, then rename into place —
//      so a crashed or failed write can't replace a previously-good file.
//
// Pure free functions; state lives in the caller. Two existing adapters:
// BatchAction (1→1, with a same-file-as-input guard) and ConcatenateAction
// (N→1).
namespace WriteTarget {

enum class Overwrite { Overwrite, Skip, Rename };

enum class ResolveStatus { Ok, Skip, Failed };

struct Resolved {
    QString       path;    // empty unless status == Ok
    ResolveStatus status;
};

// Renders an output filename template against an input path. Substitutes
// `{stem}` (input's completeBaseName) and `{ext}` (input's suffix without
// dot). Literal extensions in the template are honored as-is — typing
// `{stem}.png` against a JPEG input produces a PNG name, which is how
// callers opt in to format conversion. Trim the template before passing;
// this function does not.
QString renderFilename(const QString &templateStr, const QString &inputPath);

// Resolve `outDir/filename` under `policy`. Logs the reason on skip/fail.
// `avoidIfSame` (when non-empty) makes Rename mode skip candidates that
// resolve to that path — used by in-place 1→1 edits so the rename loop
// can't pick the source file itself.
Resolved resolve(const QString &outDir,
                 const QString &filename,
                 Overwrite policy,
                 ActionLogger *logger,
                 const QString &avoidIfSame = {});

// Write to a sibling ".part" file, then rename into place. Returns
// `finalPath` on success and logs "wrote …"; on failure returns {} and
// logs the reason. Callers are responsible for ensuring the parent
// directory exists (e.g. QDir().mkpath(outDir)).
QString write(const QString &finalPath,
              ActionLogger *logger,
              std::function<bool(const QString &tempPath)> writer);

// Move `input` to `finalPath`: try a same-fs atomic QFile::rename first;
// on failure (e.g. cross-fs), fall back to copy-via-.part-then-remove so a
// partial write can't clobber the source. Returns `finalPath` on success
// (logged as "<verb> <src> -> <dst>"), {} on failure. The parent of
// `finalPath` is created if needed.
QString move(const QString &input,
             const QString &finalPath,
             ActionLogger *logger,
             const char *verb = "moved");

}  // namespace WriteTarget
