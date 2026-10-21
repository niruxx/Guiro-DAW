#pragma once

#include <QString>
#include <QStringList>

namespace guiro {

class Project;

// .guiro files are human-readable JSON. Samples are referenced by path
// (absolute plus project-relative), plugin state is embedded as base64.
namespace ProjectIO {
bool save(const Project& p, const QString& path, QString* error);
bool load(Project& p, const QString& path, QString* error, QStringList* warnings);
} // namespace ProjectIO

} // namespace guiro
