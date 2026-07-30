#include "exifthumbnail.h"

#include <QFile>
#include <QTransform>

namespace {

// Exif stores scalars in the byte order named by the TIFF header, so every
// read is parameterised on it rather than on the host's.
quint16 readU16(const uchar *p, bool littleEndian) {
    return littleEndian ? quint16(p[0] | (p[1] << 8))
                        : quint16((p[0] << 8) | p[1]);
}

quint32 readU32(const uchar *p, bool littleEndian) {
    return littleEndian ? (quint32(p[0]) | (quint32(p[1]) << 8)
                           | (quint32(p[2]) << 16) | (quint32(p[3]) << 24))
                        : (quint32(p[3]) | (quint32(p[2]) << 8)
                           | (quint32(p[1]) << 16) | (quint32(p[0]) << 24));
}

// Walk a JPEG's marker chain and return the payload of the Exif APP1 segment
// with its "Exif\0\0" introducer stripped, leaving a buffer whose offset 0 is
// the TIFF header all Exif offsets are relative to. Returns an empty array
// when there is no such segment.
//
// Only the marker headers are read; other segments are seeked past, so the
// cost is a handful of small reads regardless of file size.
QByteArray readExifSegment(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};

    uchar soi[2];
    if (file.read(reinterpret_cast<char *>(soi), 2) != 2) return {};
    if (soi[0] != 0xFF || soi[1] != 0xD8) return {};

    while (true) {
        // Markers are introduced by one or more 0xFF fill bytes followed by a
        // type byte that is neither 0xFF nor 0x00.
        uchar byte = 0;
        do {
            if (file.read(reinterpret_cast<char *>(&byte), 1) != 1) return {};
        } while (byte != 0xFF);
        do {
            if (file.read(reinterpret_cast<char *>(&byte), 1) != 1) return {};
        } while (byte == 0xFF);

        const uchar type = byte;
        // End of image, and start of scan data: either way no metadata
        // segment can follow, so there is nothing left to find.
        if (type == 0xD9 || type == 0xDA) return {};
        // Standalone markers carry no length field.
        if (type == 0x01 || (type >= 0xD0 && type <= 0xD8)) continue;

        uchar lengthBytes[2];
        if (file.read(reinterpret_cast<char *>(lengthBytes), 2) != 2) return {};
        const int length = (lengthBytes[0] << 8) | lengthBytes[1];
        if (length < 2) return {};
        const int payloadSize = length - 2;

        if (type == 0xE1) {
            const QByteArray payload = file.read(payloadSize);
            if (payload.size() != payloadSize) return {};
            // APP1 also carries XMP; keep scanning when this one is not Exif.
            if (payload.startsWith(QByteArrayLiteral("Exif\0\0")))
                return payload.mid(6);
            continue;
        }

        if (!file.seek(file.pos() + payloadSize)) return {};
    }
}

// Rotate and flip `image` so it appears the way the Exif orientation value
// prescribes. Values outside 1-8, and the identity value 1, leave it alone.
QImage applyOrientation(const QImage &image, int orientation) {
    switch (orientation) {
    case 2: return image.mirrored(true, false);
    case 3: return image.transformed(QTransform().rotate(180));
    case 4: return image.mirrored(false, true);
    case 5: return image.mirrored(true, false).transformed(QTransform().rotate(90));
    case 6: return image.transformed(QTransform().rotate(90));
    case 7: return image.mirrored(true, false).transformed(QTransform().rotate(270));
    case 8: return image.transformed(QTransform().rotate(270));
    default: return image;
    }
}

}  // namespace

QImage readEmbeddedThumbnail(const QString &path) {
    const QByteArray tiff = readExifSegment(path);
    const qint64 size = tiff.size();
    if (size < 8) return {};

    const uchar *base = reinterpret_cast<const uchar *>(tiff.constData());

    bool littleEndian;
    if (base[0] == 'I' && base[1] == 'I')      littleEndian = true;
    else if (base[0] == 'M' && base[1] == 'M') littleEndian = false;
    else return {};
    if (readU16(base + 2, littleEndian) != 42) return {};

    // Every offset below is untrusted file content, so each is range-checked
    // against the segment before it is dereferenced.
    const auto visitIfd = [&](qint64 offset, auto &&visit) -> qint64 {
        if (offset < 8 || offset + 2 > size) return 0;
        const qint64 count = readU16(base + offset, littleEndian);
        const qint64 end = offset + 2 + count * 12;
        if (end + 4 > size) return 0;
        for (qint64 i = 0; i < count; ++i) {
            const uchar *entry = base + offset + 2 + i * 12;
            visit(readU16(entry, littleEndian), entry + 8);
        }
        return readU32(base + end, littleEndian);
    };

    int orientation = 1;
    const qint64 ifd1Offset = visitIfd(readU32(base + 4, littleEndian),
                                       [&](quint16 tag, const uchar *value) {
        if (tag == 0x0112) orientation = readU16(value, littleEndian);
    });

    qint64 thumbOffset = 0;
    qint64 thumbLength = 0;
    visitIfd(ifd1Offset, [&](quint16 tag, const uchar *value) {
        if (tag == 0x0201) thumbOffset = readU32(value, littleEndian);
        if (tag == 0x0202) thumbLength = readU32(value, littleEndian);
    });

    if (thumbOffset <= 0 || thumbLength <= 0) return {};
    if (thumbOffset + thumbLength > size) return {};

    const QImage thumbnail =
        QImage::fromData(base + thumbOffset, int(thumbLength), "JPG");
    if (thumbnail.isNull()) return {};
    return applyOrientation(thumbnail, orientation);
}
