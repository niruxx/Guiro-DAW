#pragma once

#include "plugins/PluginInstance.h"

#include <QList>

namespace guiro {

// Native Linux VST2 (.so). Windows .dll plugins reach us through yabridge,
// which exposes them as Linux .so files.
QList<PluginDescriptor> vst2Scan(const QString& path);
PluginPtr vst2Create(const PluginDescriptor& d, QString* error);

} // namespace guiro
