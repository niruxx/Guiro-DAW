#include "core/ProjectIO.h"
#include "core/Project.h"
#include "plugins/PluginManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace guiro {

namespace {

constexpr int kFormatVersion = 1;

QJsonObject saveParams(std::initializer_list<std::pair<const char*, const ParamPtr*>> list)
{
    QJsonObject o;
    for (const auto& [k, p] : list)
        if (*p)
            o.insert(QLatin1String(k), double((*p)->value()));
    return o;
}

void loadParams(const QJsonObject& o, std::initializer_list<std::pair<const char*, const ParamPtr*>> list)
{
    for (const auto& [k, p] : list)
        if (*p && o.contains(QLatin1String(k)))
            (*p)->setValue(float(o.value(QLatin1String(k)).toDouble()));
}

QJsonObject samplePath(const SamplePtr& s, const QDir& base)
{
    if (!s)
        return {};
    return QJsonObject{{"path", s->path}, {"rel", base.relativeFilePath(s->path)}};
}

SamplePtr loadSample(const QJsonValue& v, const QDir& base, QStringList* warnings)
{
    const QJsonObject o = v.toObject();
    if (o.isEmpty())
        return nullptr;
    QString path = o.value("path").toString();
    if (!QFileInfo::exists(path)) {
        const QString rel = base.absoluteFilePath(o.value("rel").toString());
        if (QFileInfo::exists(rel))
            path = rel;
    }
    QString err;
    SamplePtr s = SampleCache::instance().load(path, &err);
    if (!s && warnings)
        *warnings << QObject::tr("Missing sample: %1").arg(path);
    return s;
}

QJsonObject savePlugin(const PluginPtr& p)
{
    if (!p)
        return {};
    const auto& d = p->descriptor();
    return QJsonObject{{"format", formatName(d.format)}, {"uid", d.uid},   {"path", d.path},
                       {"name", d.name},                 {"vendor", d.vendor}, {"instrument", d.instrument},
                       {"state", QString::fromLatin1(p->saveState().toBase64())}};
}

PluginPtr loadPlugin(const QJsonObject& o, QStringList* warnings)
{
    if (o.isEmpty())
        return nullptr;
    PluginDescriptor d;
    if (!formatFromName(o.value("format").toString(), &d.format))
        return nullptr;
    d.uid = o.value("uid").toString();
    if (const PluginDescriptor* known = PluginManager::instance().find(d.format, d.uid)) {
        d = *known;
    } else {
        d.path = o.value("path").toString();
        d.name = o.value("name").toString();
        d.vendor = o.value("vendor").toString();
        d.instrument = o.value("instrument").toBool();
    }
    QString err;
    PluginPtr p = PluginManager::instance().create(d, &err);
    if (!p) {
        if (warnings)
            *warnings << QObject::tr("Could not load plugin %1: %2").arg(o.value("name").toString(), err);
        return nullptr;
    }
    p->loadState(QByteArray::fromBase64(o.value("state").toString().toLatin1()));
    return p;
}

QString kindName(ChannelKind k)
{
    switch (k) {
    case ChannelKind::Sampler: return "sampler";
    case ChannelKind::Instrument: return "instrument";
    case ChannelKind::Pads: return "pads";
    case ChannelKind::Audio: return "audio";
    }
    return "sampler";
}

ChannelKind kindFrom(const QString& s)
{
    if (s == "instrument") return ChannelKind::Instrument;
    if (s == "pads") return ChannelKind::Pads;
    if (s == "audio") return ChannelKind::Audio;
    return ChannelKind::Sampler;
}

QString clipKindName(ClipKind k)
{
    switch (k) {
    case ClipKind::Pattern: return "pattern";
    case ClipKind::Audio: return "audio";
    case ClipKind::Automation: return "automation";
    }
    return "pattern";
}

ClipKind clipKindFrom(const QString& s)
{
    if (s == "audio") return ClipKind::Audio;
    if (s == "automation") return ClipKind::Automation;
    return ClipKind::Pattern;
}

} // namespace

bool ProjectIO::save(const Project& p, const QString& path, QString* error)
{
    const QDir base = QFileInfo(path).absoluteDir();
    QJsonObject root;
    root.insert("guiro", kFormatVersion);
    root.insert("bpm", p.bpm());
    root.insert("swing", double(p.swing->value()));
    root.insert("currentPattern", qint64(p.currentPatternId()));
    root.insert("loop", QJsonArray{qint64(p.loopStart), qint64(p.loopEnd)});

    QJsonArray channels;
    for (const auto& c : p.channels) {
        QJsonObject o{{"id", qint64(c->id)},
                      {"kind", kindName(c->kind)},
                      {"name", c->name},
                      {"color", c->color.name()},
                      {"muted", c->muted},
                      {"insert", c->insert}};
        o.insert("params", saveParams({{"volume", &c->volume}, {"pan", &c->pan}, {"pitch", &c->pitch},
                                       {"filter", &c->filterType}, {"cutoff", &c->cutoff}, {"resonance", &c->resonance},
                                       {"attack", &c->attack}, {"release", &c->release}}));
        if (c->sample)
            o.insert("sample", samplePath(c->sample, base));
        if (c->kind == ChannelKind::Pads) {
            QJsonArray pads;
            for (const auto& pad : c->pads) {
                QJsonObject po = samplePath(pad.sample, base);
                po.insert("gain", double(pad.gain->value()));
                po.insert("pitch", double(pad.pitch->value()));
                pads.append(po);
            }
            o.insert("pads", pads);
        }
        if (c->plugin)
            o.insert("plugin", savePlugin(c->plugin));
        channels.append(o);
    }
    root.insert("channels", channels);

    QJsonArray patterns;
    for (const auto& pat : p.patterns) {
        QJsonObject o{{"id", qint64(pat->id)}, {"name", pat->name}, {"color", pat->color.name()}, {"steps", pat->steps}};
        QJsonObject steps;
        for (const auto& [ch, data] : pat->stepData) {
            QJsonArray a;
            for (uint8_t v : data)
                a.append(int(v));
            steps.insert(QString::number(ch), a);
        }
        o.insert("stepData", steps);
        QJsonObject notes;
        for (const auto& [ch, list] : pat->notes) {
            QJsonArray a;
            for (const Note& n : list)
                a.append(QJsonArray{n.key, qint64(n.start), qint64(n.length), double(n.velocity)});
            notes.insert(QString::number(ch), a);
        }
        o.insert("notes", notes);
        patterns.append(o);
    }
    root.insert("patterns", patterns);

    QJsonArray autos;
    for (const auto& a : p.automations) {
        QJsonArray pts;
        for (const auto& pt : a->points)
            pts.append(QJsonArray{qint64(pt.t), double(pt.v)});
        autos.append(QJsonObject{{"id", qint64(a->id)}, {"name", a->name}, {"color", a->color.name()},
                                 {"target", a->target}, {"points", pts}});
    }
    root.insert("automations", autos);

    QJsonArray tracks;
    for (const auto& t : p.tracks)
        tracks.append(QJsonObject{{"name", t.name}, {"color", t.color.name()}, {"muted", t.muted}});
    root.insert("tracks", tracks);

    QJsonArray clips;
    for (const auto& c : p.clips)
        clips.append(QJsonObject{{"kind", clipKindName(c.kind)}, {"ref", qint64(c.ref)},       {"track", c.track},
                                 {"start", qint64(c.start)},     {"length", qint64(c.length)}, {"offset", qint64(c.offset)}});
    root.insert("clips", clips);

    QJsonArray mixer;
    for (const auto& m : p.inserts) {
        QJsonObject o{{"name", m->name}, {"color", m->color.name()}, {"muted", m->muted}, {"solo", m->solo}};
        o.insert("params", saveParams({{"volume", &m->volume}, {"pan", &m->pan}, {"filter", &m->filterType},
                                       {"cutoff", &m->cutoff}, {"resonance", &m->resonance}}));
        QJsonArray fx;
        for (const auto& slot : m->fx) {
            QJsonObject so = savePlugin(slot.plugin);
            if (!so.isEmpty())
                so.insert("bypass", slot.bypass);
            fx.append(so);
        }
        o.insert("fx", fx);
        mixer.append(o);
    }
    root.insert("mixer", mixer);

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

bool ProjectIO::load(Project& p, const QString& path, QString* error, QStringList* warnings)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (doc.isNull() || !doc.object().contains("guiro")) {
        if (error)
            *error = perr.error != QJsonParseError::NoError ? perr.errorString() : QObject::tr("Not a Guiro project");
        return false;
    }
    const QJsonObject root = doc.object();
    const QDir base = QFileInfo(path).absoluteDir();

    p.clear();
    p.setBpm(root.value("bpm").toDouble(130.0));
    p.swing->setValue(float(root.value("swing").toDouble()));
    const QJsonArray loop = root.value("loop").toArray();
    if (loop.size() == 2) {
        p.loopStart = loop[0].toInteger();
        p.loopEnd = loop[1].toInteger();
    }

    for (const auto& v : root.value("channels").toArray()) {
        const QJsonObject o = v.toObject();
        auto c = p.createChannel(kindFrom(o.value("kind").toString()), o.value("name").toString());
        c->id = ObjId(o.value("id").toInteger());
        p.bumpId(c->id);
        // Parameter paths embed the id, so rebuild them for the restored id.
        const QString b = QStringLiteral("channel/%1/").arg(c->id);
        c->volume->setPath(b + "volume");
        c->pan->setPath(b + "pan");
        c->pitch->setPath(b + "pitch");
        c->filterType->setPath(b + "filter");
        c->cutoff->setPath(b + "cutoff");
        c->resonance->setPath(b + "resonance");
        c->attack->setPath(b + "attack");
        c->release->setPath(b + "release");
        c->color = QColor(o.value("color").toString());
        c->muted = o.value("muted").toBool();
        c->insert = o.value("insert").toInt();
        loadParams(o.value("params").toObject(),
                   {{"volume", &c->volume}, {"pan", &c->pan}, {"pitch", &c->pitch}, {"filter", &c->filterType},
                    {"cutoff", &c->cutoff}, {"resonance", &c->resonance}, {"attack", &c->attack}, {"release", &c->release}});
        if (o.contains("sample"))
            c->sample = loadSample(o.value("sample"), base, warnings);
        const QJsonArray pads = o.value("pads").toArray();
        for (int i = 0; i < std::min<int>(kNumPads, int(pads.size())); ++i) {
            auto& pad = c->pads[size_t(i)];
            const QJsonObject po = pads[i].toObject();
            pad.sample = loadSample(po, base, warnings);
            pad.gain->setValue(float(po.value("gain").toDouble(1.0)));
            pad.pitch->setValue(float(po.value("pitch").toDouble(0.0)));
            pad.gain->setPath(QStringLiteral("pad/%1/%2/gain").arg(c->id).arg(i));
            pad.pitch->setPath(QStringLiteral("pad/%1/%2/pitch").arg(c->id).arg(i));
        }
        if (o.contains("plugin"))
            c->plugin = loadPlugin(o.value("plugin").toObject(), warnings);
        p.channels.push_back(c);
    }

    for (const auto& v : root.value("patterns").toArray()) {
        const QJsonObject o = v.toObject();
        auto pat = std::make_shared<Pattern>();
        pat->id = ObjId(o.value("id").toInteger());
        p.bumpId(pat->id);
        pat->name = o.value("name").toString();
        pat->color = QColor(o.value("color").toString());
        pat->steps = std::clamp(o.value("steps").toInt(16), 1, 256);
        const QJsonObject steps = o.value("stepData").toObject();
        for (auto it = steps.begin(); it != steps.end(); ++it) {
            auto& vec = pat->stepData[it.key().toUInt()];
            for (const auto& s : it.value().toArray())
                vec.push_back(uint8_t(std::clamp(s.toInt(), 0, 127)));
        }
        const QJsonObject notes = o.value("notes").toObject();
        for (auto it = notes.begin(); it != notes.end(); ++it) {
            auto& vec = pat->notes[it.key().toUInt()];
            for (const auto& n : it.value().toArray()) {
                const QJsonArray a = n.toArray();
                if (a.size() >= 4)
                    vec.push_back({a[0].toInt(), a[1].toInteger(), a[2].toInteger(), float(a[3].toDouble())});
            }
        }
        p.patterns.push_back(pat);
    }
    if (p.patterns.empty())
        p.addPattern();

    for (const auto& v : root.value("automations").toArray()) {
        const QJsonObject o = v.toObject();
        auto a = std::make_shared<AutomationClip>();
        a->id = ObjId(o.value("id").toInteger());
        p.bumpId(a->id);
        a->name = o.value("name").toString();
        a->color = QColor(o.value("color").toString());
        a->target = o.value("target").toString();
        for (const auto& pt : o.value("points").toArray()) {
            const QJsonArray arr = pt.toArray();
            if (arr.size() == 2)
                a->points.push_back({arr[0].toInteger(), float(arr[1].toDouble())});
        }
        a->sortPoints();
        p.automations.push_back(a);
    }

    const QJsonArray tracks = root.value("tracks").toArray();
    for (int i = 0; i < std::min<int>(int(tracks.size()), int(p.tracks.size())); ++i) {
        const QJsonObject o = tracks[i].toObject();
        p.tracks[size_t(i)].name = o.value("name").toString();
        p.tracks[size_t(i)].color = QColor(o.value("color").toString());
        p.tracks[size_t(i)].muted = o.value("muted").toBool();
    }

    for (const auto& v : root.value("clips").toArray()) {
        const QJsonObject o = v.toObject();
        Clip c;
        c.id = p.newId();
        c.kind = clipKindFrom(o.value("kind").toString());
        c.ref = ObjId(o.value("ref").toInteger());
        c.track = o.value("track").toInt();
        c.start = o.value("start").toInteger();
        c.length = std::max<Tick>(1, o.value("length").toInteger());
        c.offset = o.value("offset").toInteger();
        p.clips.push_back(c);
    }

    const QJsonArray mixer = root.value("mixer").toArray();
    for (int i = 0; i < std::min<int>(int(mixer.size()), int(p.inserts.size())); ++i) {
        const QJsonObject o = mixer[i].toObject();
        MixerInsert& m = *p.inserts[size_t(i)];
        m.name = o.value("name").toString(m.name);
        m.color = QColor(o.value("color").toString(m.color.name()));
        m.muted = o.value("muted").toBool();
        m.solo = o.value("solo").toBool();
        loadParams(o.value("params").toObject(), {{"volume", &m.volume}, {"pan", &m.pan}, {"filter", &m.filterType},
                                                  {"cutoff", &m.cutoff}, {"resonance", &m.resonance}});
        const QJsonArray fx = o.value("fx").toArray();
        for (int k = 0; k < std::min<int>(kNumFxSlots, int(fx.size())); ++k) {
            const QJsonObject so = fx[k].toObject();
            if (so.isEmpty())
                continue;
            m.fx[size_t(k)].plugin = loadPlugin(so, warnings);
            m.fx[size_t(k)].bypass = so.value("bypass").toBool();
        }
    }

    const ObjId cur = ObjId(root.value("currentPattern").toInteger());
    p.setCurrentPattern(p.pattern(cur) ? cur : p.patterns.front()->id);
    if (!p.channels.empty())
        p.setSelectedChannel(p.channels.front()->id);
    p.filePath = path;
    emit p.projectReset();
    p.notifyStructure();
    p.setModified(false);
    return true;
}

} // namespace guiro
