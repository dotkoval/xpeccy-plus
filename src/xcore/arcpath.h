#pragma once

// An image inside an archive is named by an archive path: the archive file's own path,
// '/', then the entry - "C:/Games/game.tzx.zip/Side A.tzx". That one string is what the
// drives, Recent, Favorites, Reload, the config and the title hold. Only what reads the
// image's bytes asks arc_local() for a copy, unpacked into the program's own temporary
// folder, which goes when the program exits (and, after a crash, at the next start).

#include <QString>
#include <QVector>

#include "archive.h"

// what an open dialog's filter adds for them
#define ARC_FILTER "*.zip *.gz *.7z"

// true for an archive path, with its two halves; false for anything else
bool arc_split(const QString& path, QString* archive = nullptr, QString* entry = nullptr);
QString arc_join(const QString& archive, const QString& entry);
// a file that is an archive itself: what a picker or the first image is taken from
bool arc_bare(const QString& path);
// the file to read: the unpacked copy for an archive path, the path itself otherwise;
// empty, with err set, when the entry cannot be unpacked
QString arc_local(const QString& path, QString* err);
// the archive's entries, read once while it stays the same file; nullptr, with err set,
// when it cannot be read
const QVector<arcEntry>* arc_entries(const QString& archive, QString* err);
// the name shown: "game.tzx.zip > Side A.tzx", or a plain file's name
QString arc_name(const QString& path);
// the file is there: for an archive path, its archive
bool arc_exists(const QString& path);
// the canonical form, the archive half resolved as any file; empty when it is not there
QString arc_canonical(const QString& path);
// a file named after the image, beside it - for an archive path, beside the archive:
// ext empty keeps the entry's own file name
QString arc_beside(const QString& path, const QString& ext);
// the temporary folders: a crashed run's swept at start, ours removed at exit
void arc_start();
void arc_finish();
