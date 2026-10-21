#pragma once

#include "plugins/PluginInstance.h"

#include <QList>

namespace guiro {

QList<PluginDescriptor> vst3Scan(const QString& path);
PluginPtr vst3Create(const PluginDescriptor& d, QString* error);

} // namespace guiro
