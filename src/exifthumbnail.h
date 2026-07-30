#pragma once

#include <QImage>
#include <QString>

// Extract the reduced-size image JPEG cameras embed in a file's Exif block
// (IFD1), already rotated to match the orientation tag in IFD0.
//
// Returns a null QImage when the file is not a JPEG, carries no Exif block,
// embeds no thumbnail, or the Exif structure fails a bounds check. Callers
// must therefore treat a null result as "no shortcut available", not as an
// error, and fall back to decoding the source.
//
// Embedded thumbnails are typically 160x120, so they only satisfy small
// thumbnail sizes; the caller decides whether the returned image is large
// enough for what it needs.
//
// Reentrant: no shared state, safe to call from several threads at once.
QImage readEmbeddedThumbnail(const QString &path);
