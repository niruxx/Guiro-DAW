#pragma once

#include "plugins/PluginInstance.h"

#include <QList>

namespace guiro {

QList<PluginDescriptor> lv2ScanAll();
PluginPtr lv2Create(const PluginDescriptor& d, QString* error);

} // namespace guiro
