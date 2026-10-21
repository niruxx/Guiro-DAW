#pragma once

#include <QString>
#include <map>

namespace guiro {

// Procedurally synthesized starter kit ("Guiro Basics"), written to
// ~/.local/share/Guiro/Packs on first run so the app makes sound out of the box.
class DefaultKit {
public:
    static QString packsDir();
    // Generates missing sounds and returns name -> file path.
    static std::map<QString, QString> ensure();
};

} // namespace guiro
