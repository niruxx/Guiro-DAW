#pragma once

#include "plugins/PluginInstance.h"

#include <QList>

namespace guiro {

QList<PluginDescriptor> clapScan(const QString& path);
PluginPtr clapCreate(const PluginDescriptor& d, QString* error);

} // namespace guiro
