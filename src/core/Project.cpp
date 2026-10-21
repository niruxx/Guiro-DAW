#include "core/Project.h"
#include "core/DefaultKit.h"
#include "engine/Runtime.h"

#include <QTimer>
#include <algorithm>
#include <cmath>

namespace guiro {

// ---------------------------------------------------------------- Pattern

Tick Pattern::length() const
{
    Tick len = Tick(steps) * kStepTicks;
    for (const auto& [ch, list] : notes)
        for (const auto& n : list)
            len = std::max(len, n.start + n.length);
    len = std::max<Tick>(kBarTicks, ((len + kBarTicks - 1) / kBarTicks) * kBarTicks);
    return len;
}

bool Pattern::hasContentFor(ObjId channel) const
{
    if (auto it = notes.find(channel); it != notes.end() && !it->second.empty())
        return true;
    if (auto it = stepData.find(channel); it != stepData.end())
        for (auto v : it->second)
            if (v)
                return true;
    return false;
}

std::vector<uint8_t>& Pattern::stepsFor(ObjId channel)
{
    auto& v = stepData[channel];
    if (int(v.size()) < steps)
        v.resize(size_t(steps), 0);
    return v;
}

// ---------------------------------------------------------------- Automation

float AutomationClip::valueAt(Tick t) const
{
    if (points.empty())
        return 0.5f;
    if (t <= points.front().t)
        return points.front().v;
    if (t >= points.back().t)
        return points.back().v;
    for (size_t i = 1; i < points.size(); ++i) {
        const auto& b = points[i];
        if (t <= b.t) {
            const auto& a = points[i - 1];
            if (b.t == a.t)
                return b.v;
            const float f = float(t - a.t) / float(b.t - a.t);
            return a.v + (b.v - a.v) * f;
        }
    }
    return points.back().v;
}

void AutomationClip::sortPoints()
{
    std::stable_sort(points.begin(), points.end(), [](const AutoPoint& a, const AutoPoint& b) { return a.t < b.t; });
}

// ---------------------------------------------------------------- Project

Project::Project(QObject* parent) : QObject(parent)
{
    swing = std::make_shared<Param>(QStringLiteral("Swing"), 0.f, 1.f, 0.f, ParamCurve::Linear, ParamUnit::Percent);
    swing->setPath(QStringLiteral("global/swing"));
    masterPitch = makePitchParam(12.f);
    masterPitch->setPath(QStringLiteral("global/pitch"));
    clear();
}

Project::~Project() = default;

QColor Project::paletteColor(int i)
{
    static const QColor pal[] = {
        QColor("#ff8a4c"), QColor("#ffc145"), QColor("#9be564"), QColor("#3fd8c4"),
        QColor("#4cb5ff"), QColor("#8b7bff"), QColor("#d77bff"), QColor("#ff6b9a"),
        QColor("#ff5f5f"), QColor("#c2e05a"), QColor("#5fe3a1"), QColor("#6f9bff"),
    };
    constexpr int n = int(sizeof(pal) / sizeof(pal[0]));
    return pal[((i % n) + n) % n];
}

void Project::clear()
{
    channels.clear();
    patterns.clear();
    automations.clear();
    clips.clear();
    tracks.clear();
    inserts.clear();
    filePath.clear();
    loopStart = loopEnd = 0;
    m_nextId = 1;
    m_currentPattern = 0;
    m_selectedChannel = 0;
    m_bpm.store(130.0);
    swing->reset();
    masterPitch->reset();

    for (int i = 0; i < kNumTracks; ++i) {
        Track t;
        t.name = QStringLiteral("Track %1").arg(i + 1);
        t.color = paletteColor(i).darker(115);
        tracks.push_back(t);
    }
    for (int i = 0; i <= kNumInserts; ++i) {
        auto m = std::make_shared<MixerInsert>();
        initInsert(*m, i);
        inserts.push_back(m);
    }
}

void Project::initInsert(MixerInsert& m, int index)
{
    m.index = index;
    m.name = index == 0 ? QStringLiteral("Master") : QStringLiteral("Insert %1").arg(index);
    m.color = index == 0 ? QColor("#f5a524") : paletteColor(index - 1);
    m.volume = makeVolumeParam(QStringLiteral("Volume"), index == 0 ? 1.f : 1.f);
    m.pan = makePanParam();
    m.filterType = makeFilterTypeParam();
    m.cutoff = makeCutoffParam();
    m.resonance = makeResonanceParam();
    const QString base = QStringLiteral("mixer/%1/").arg(index);
    m.volume->setPath(base + "volume");
    m.pan->setPath(base + "pan");
    m.filterType->setPath(base + "filter");
    m.cutoff->setPath(base + "cutoff");
    m.resonance->setPath(base + "resonance");
    m.rt = std::make_shared<InsertRt>();
}

void Project::initChannelParams(Channel& c)
{
    c.volume = makeVolumeParam(QStringLiteral("Volume"), 0.78f);
    c.pan = makePanParam();
    c.pitch = makePitchParam(24.f);
    c.filterType = makeFilterTypeParam();
    c.cutoff = makeCutoffParam();
    c.resonance = makeResonanceParam();
    c.attack = std::make_shared<Param>(QStringLiteral("Attack"), 0.f, 2000.f, 0.f, ParamCurve::Square, ParamUnit::Ms);
    c.release = std::make_shared<Param>(QStringLiteral("Release"), 1.f, 4000.f, 40.f, ParamCurve::Square, ParamUnit::Ms);
    const QString base = QStringLiteral("channel/%1/").arg(c.id);
    c.volume->setPath(base + "volume");
    c.pan->setPath(base + "pan");
    c.pitch->setPath(base + "pitch");
    c.filterType->setPath(base + "filter");
    c.cutoff->setPath(base + "cutoff");
    c.resonance->setPath(base + "resonance");
    c.attack->setPath(base + "attack");
    c.release->setPath(base + "release");
    for (int i = 0; i < kNumPads; ++i) {
        auto& p = c.pads[size_t(i)];
        p.gain = makeVolumeParam(QStringLiteral("Pad %1 Volume").arg(i + 1), 1.f);
        p.pitch = makePitchParam(24.f);
        p.gain->setPath(QStringLiteral("pad/%1/%2/gain").arg(c.id).arg(i));
        p.pitch->setPath(QStringLiteral("pad/%1/%2/pitch").arg(c.id).arg(i));
        p.color = paletteColor(i / 4 * 3 + i % 4);
    }
    c.rt = std::make_shared<ChannelRt>();
}

void Project::setBpm(double bpm)
{
    bpm = std::clamp(bpm, 10.0, 522.0);
    if (std::abs(bpm - m_bpm.load()) < 1e-6)
        return;
    m_bpm.store(bpm);
    setModified(true);
    emit tempoChanged(bpm);
}

// ---------------------------------------------------------------- channels

Channel* Project::channel(ObjId id) const
{
    for (auto& c : channels)
        if (c->id == id)
            return c.get();
    return nullptr;
}

int Project::channelIndex(ObjId id) const
{
    for (size_t i = 0; i < channels.size(); ++i)
        if (channels[i]->id == id)
            return int(i);
    return -1;
}

int Project::nextFreeInsert() const
{
    for (int i = 1; i <= kNumInserts; ++i) {
        bool used = false;
        for (auto& c : channels)
            if (c->insert == i)
                used = true;
        if (!used)
            return i;
    }
    return int(channels.size() % kNumInserts) + 1;
}

std::shared_ptr<Channel> Project::createChannel(ChannelKind kind, const QString& name)
{
    auto c = std::make_shared<Channel>();
    c->id = newId();
    c->kind = kind;
    c->name = name;
    c->color = paletteColor(int(channels.size()));
    initChannelParams(*c);
    c->insert = nextFreeInsert();
    if (MixerInsert* m = insert(c->insert); m && m->name.startsWith(QStringLiteral("Insert ")))
        m->name = name;
    return c;
}

Channel* Project::addSampler(SamplePtr s, int atIndex)
{
    auto c = createChannel(ChannelKind::Sampler, s ? s->name : QStringLiteral("Sampler"));
    c->sample = std::move(s);
    Channel* raw = c.get();
    if (atIndex < 0 || atIndex > int(channels.size()))
        channels.push_back(c);
    else
        channels.insert(channels.begin() + atIndex, c);
    notifyChannels();
    return raw;
}

Channel* Project::addAudioChannel(SamplePtr s)
{
    auto c = createChannel(ChannelKind::Audio, s ? s->name : QStringLiteral("Audio"));
    c->sample = std::move(s);
    Channel* raw = c.get();
    channels.push_back(c);
    notifyChannels();
    return raw;
}

Channel* Project::addInstrument(PluginPtr p)
{
    auto c = createChannel(ChannelKind::Instrument, p ? p->name() : QStringLiteral("Instrument"));
    c->plugin = std::move(p);
    Channel* raw = c.get();
    channels.push_back(c);
    notifyChannels();
    return raw;
}

Channel* Project::addPads()
{
    int n = 1;
    for (auto& c : channels)
        if (c->kind == ChannelKind::Pads)
            ++n;
    auto c = createChannel(ChannelKind::Pads, n == 1 ? QStringLiteral("Pads") : QStringLiteral("Pads %1").arg(n));
    Channel* raw = c.get();
    channels.push_back(c);
    notifyChannels();
    return raw;
}

void Project::removeChannel(ObjId id)
{
    const int idx = channelIndex(id);
    if (idx < 0)
        return;
    channels.erase(channels.begin() + idx);
    for (auto& p : patterns) {
        p->stepData.erase(id);
        p->notes.erase(id);
    }
    clips.erase(std::remove_if(clips.begin(), clips.end(),
                               [id](const Clip& c) { return c.kind == ClipKind::Audio && c.ref == id; }),
                clips.end());
    if (m_selectedChannel == id) {
        m_selectedChannel = channels.empty() ? 0 : channels[size_t(std::min<int>(idx, int(channels.size()) - 1))]->id;
        emit selectedChannelChanged(m_selectedChannel);
    }
    notifyStructure();
}

void Project::moveChannel(ObjId id, int delta)
{
    const int idx = channelIndex(id);
    const int to = idx + delta;
    if (idx < 0 || to < 0 || to >= int(channels.size()))
        return;
    std::swap(channels[size_t(idx)], channels[size_t(to)]);
    notifyChannels();
}

ObjId Project::cloneChannel(ObjId id)
{
    Channel* src = channel(id);
    if (!src || src->kind == ChannelKind::Instrument)
        return 0;   // plugin instances cannot be trivially cloned here
    auto c = createChannel(src->kind, src->name + QStringLiteral(" (2)"));
    c->sample = src->sample;
    c->insert = src->insert;
    c->color = src->color;
    for (int i = 0; i < kNumPads; ++i) {
        c->pads[size_t(i)].sample = src->pads[size_t(i)].sample;
        c->pads[size_t(i)].gain->setValue(src->pads[size_t(i)].gain->value());
        c->pads[size_t(i)].pitch->setValue(src->pads[size_t(i)].pitch->value());
    }
    auto copy = [](const ParamPtr& a, const ParamPtr& b) { b->setValue(a->value()); };
    copy(src->volume, c->volume);
    copy(src->pan, c->pan);
    copy(src->pitch, c->pitch);
    copy(src->filterType, c->filterType);
    copy(src->cutoff, c->cutoff);
    copy(src->resonance, c->resonance);
    copy(src->attack, c->attack);
    copy(src->release, c->release);
    channels.insert(channels.begin() + channelIndex(id) + 1, c);
    notifyChannels();
    return c->id;
}

void Project::setSelectedChannel(ObjId id)
{
    if (m_selectedChannel == id)
        return;
    m_selectedChannel = id;
    emit selectedChannelChanged(id);
}

// ---------------------------------------------------------------- patterns

Pattern* Project::pattern(ObjId id) const
{
    for (auto& p : patterns)
        if (p->id == id)
            return p.get();
    return nullptr;
}

void Project::setCurrentPattern(ObjId id)
{
    if (m_currentPattern == id || !pattern(id))
        return;
    m_currentPattern = id;
    emit currentPatternChanged(id);
    requestSnapshot();
}

Pattern* Project::addPattern()
{
    auto p = std::make_shared<Pattern>();
    p->id = newId();
    p->name = QStringLiteral("Pattern %1").arg(patterns.size() + 1);
    p->color = paletteColor(int(patterns.size()) * 5 + 4);
    Pattern* raw = p.get();
    patterns.push_back(p);
    if (!m_currentPattern)
        m_currentPattern = p->id;
    notifyPatterns();
    return raw;
}

void Project::removePattern(ObjId id)
{
    auto it = std::find_if(patterns.begin(), patterns.end(), [id](auto& p) { return p->id == id; });
    if (it == patterns.end())
        return;
    patterns.erase(it);
    clips.erase(std::remove_if(clips.begin(), clips.end(),
                               [id](const Clip& c) { return c.kind == ClipKind::Pattern && c.ref == id; }),
                clips.end());
    if (patterns.empty())
        addPattern();
    if (m_currentPattern == id) {
        m_currentPattern = patterns.front()->id;
        emit currentPatternChanged(m_currentPattern);
    }
    notifyStructure();
}

ObjId Project::clonePattern(ObjId id)
{
    Pattern* src = pattern(id);
    if (!src)
        return 0;
    auto p = std::make_shared<Pattern>(*src);
    p->id = newId();
    p->name = src->name + QStringLiteral(" (2)");
    p->color = paletteColor(int(patterns.size()) * 5 + 4);
    patterns.push_back(p);
    notifyPatterns();
    return p->id;
}

// ---------------------------------------------------------------- automation

AutomationClip* Project::automation(ObjId id) const
{
    for (auto& a : automations)
        if (a->id == id)
            return a.get();
    return nullptr;
}

AutomationClip* Project::addAutomation(const QString& target)
{
    auto a = std::make_shared<AutomationClip>();
    a->id = newId();
    a->target = target;
    a->name = describeTarget(target);
    a->color = QColor("#7fd4ff");
    float v = 0.5f;
    uint32_t pid = 0;
    if (ParamPtr p = resolveParam(target))
        v = p->normalized();
    else if (PluginPtr pl = resolvePluginTarget(target, &pid)) {
        for (int i = 0; i < pl->paramCount(); ++i) {
            auto info = pl->paramInfo(i);
            if (info.id == pid && info.max > info.min)
                v = float((pl->paramValue(pid) - info.min) / (info.max - info.min));
        }
    }
    a->points = {{0, v}, {kBarTicks * 4, v}};
    AutomationClip* raw = a.get();
    automations.push_back(a);
    notifyAutomation();
    return raw;
}

void Project::removeAutomation(ObjId id)
{
    automations.erase(std::remove_if(automations.begin(), automations.end(), [id](auto& a) { return a->id == id; }),
                      automations.end());
    clips.erase(std::remove_if(clips.begin(), clips.end(),
                               [id](const Clip& c) { return c.kind == ClipKind::Automation && c.ref == id; }),
                clips.end());
    notifyStructure();
}

// ---------------------------------------------------------------- playlist

Clip* Project::clip(ObjId id)
{
    for (auto& c : clips)
        if (c.id == id)
            return &c;
    return nullptr;
}

Clip& Project::addClip(ClipKind kind, ObjId ref, int track, Tick start, Tick length)
{
    Clip c;
    c.id = newId();
    c.kind = kind;
    c.ref = ref;
    c.track = std::clamp(track, 0, kNumTracks - 1);
    c.start = std::max<Tick>(0, start);
    c.length = std::max<Tick>(kStepTicks, length);
    clips.push_back(c);
    notifyPlaylist();
    return clips.back();
}

void Project::removeClip(ObjId id)
{
    clips.erase(std::remove_if(clips.begin(), clips.end(), [id](const Clip& c) { return c.id == id; }), clips.end());
    notifyPlaylist();
}

Tick Project::sourceLength(const Clip& c) const
{
    switch (c.kind) {
    case ClipKind::Pattern:
        if (Pattern* p = pattern(c.ref))
            return p->length();
        break;
    case ClipKind::Audio:
        if (Channel* ch = channel(c.ref); ch && ch->sample) {
            const double ticksPerSec = bpm() / 60.0 * kPPQ;
            return std::max<Tick>(1, Tick(std::ceil(ch->sample->seconds() * ticksPerSec)));
        }
        break;
    case ClipKind::Automation:
        if (AutomationClip* a = automation(c.ref); a && !a->points.empty())
            return std::max<Tick>(kBarTicks, a->points.back().t);
        break;
    }
    return kBarTicks;
}

Tick Project::songLength() const
{
    Tick end = 0;
    for (const auto& c : clips)
        end = std::max(end, c.end());
    return std::max<Tick>(kBarTicks, ((end + kBarTicks - 1) / kBarTicks) * kBarTicks);
}

int Project::firstFreeTrack(Tick start, Tick end) const
{
    for (int t = 0; t < kNumTracks; ++t) {
        bool busy = false;
        for (const auto& c : clips)
            if (c.track == t && c.start < end && c.end() > start)
                busy = true;
        if (!busy)
            return t;
    }
    return 0;
}

// ---------------------------------------------------------------- targets

ParamPtr Project::resolveParam(const QString& path) const
{
    const QStringList parts = path.split(QLatin1Char('/'));
    if (parts.size() < 2)
        return nullptr;
    if (parts[0] == QLatin1String("global")) {
        if (parts[1] == QLatin1String("swing"))
            return swing;
        if (parts[1] == QLatin1String("pitch"))
            return masterPitch;
        return nullptr;
    }
    if (parts[0] == QLatin1String("mixer") && parts.size() == 3) {
        MixerInsert* m = insert(parts[1].toInt());
        if (!m)
            return nullptr;
        const QString& n = parts[2];
        if (n == QLatin1String("volume")) return m->volume;
        if (n == QLatin1String("pan")) return m->pan;
        if (n == QLatin1String("filter")) return m->filterType;
        if (n == QLatin1String("cutoff")) return m->cutoff;
        if (n == QLatin1String("resonance")) return m->resonance;
        return nullptr;
    }
    if (parts[0] == QLatin1String("channel") && parts.size() == 3) {
        Channel* c = channel(parts[1].toUInt());
        if (!c)
            return nullptr;
        const QString& n = parts[2];
        if (n == QLatin1String("volume")) return c->volume;
        if (n == QLatin1String("pan")) return c->pan;
        if (n == QLatin1String("pitch")) return c->pitch;
        if (n == QLatin1String("filter")) return c->filterType;
        if (n == QLatin1String("cutoff")) return c->cutoff;
        if (n == QLatin1String("resonance")) return c->resonance;
        if (n == QLatin1String("attack")) return c->attack;
        if (n == QLatin1String("release")) return c->release;
        return nullptr;
    }
    if (parts[0] == QLatin1String("pad") && parts.size() == 4) {
        Channel* c = channel(parts[1].toUInt());
        const int i = parts[2].toInt();
        if (!c || i < 0 || i >= kNumPads)
            return nullptr;
        return parts[3] == QLatin1String("gain") ? c->pads[size_t(i)].gain : c->pads[size_t(i)].pitch;
    }
    return nullptr;
}

PluginPtr Project::resolvePluginTarget(const QString& path, uint32_t* paramId) const
{
    const QStringList parts = path.split(QLatin1Char('/'));
    if (parts.size() != 3 || parts[0] != QLatin1String("plugin"))
        return nullptr;
    if (paramId)
        *paramId = parts[2].toUInt();
    const QString& owner = parts[1];
    if (owner.startsWith(QLatin1String("ch"))) {
        Channel* c = channel(owner.mid(2).toUInt());
        return c ? c->plugin : nullptr;
    }
    if (owner.startsWith(QLatin1String("fx"))) {
        const QStringList is = owner.mid(2).split(QLatin1Char('.'));
        if (is.size() != 2)
            return nullptr;
        MixerInsert* m = insert(is[0].toInt());
        const int slot = is[1].toInt();
        if (!m || slot < 0 || slot >= kNumFxSlots)
            return nullptr;
        return m->fx[size_t(slot)].plugin;
    }
    return nullptr;
}

QString Project::describeTarget(const QString& path) const
{
    const QStringList parts = path.split(QLatin1Char('/'));
    if (ParamPtr p = resolveParam(path)) {
        QString owner;
        if (parts[0] == QLatin1String("mixer")) {
            MixerInsert* m = insert(parts[1].toInt());
            owner = m ? m->name : QString();
        } else if (parts[0] == QLatin1String("channel") || parts[0] == QLatin1String("pad")) {
            Channel* c = channel(parts[1].toUInt());
            owner = c ? c->name : QString();
        } else {
            owner = QStringLiteral("Global");
        }
        return owner + QStringLiteral(" ▸ ") + p->name();
    }
    uint32_t pid = 0;
    if (PluginPtr pl = resolvePluginTarget(path, &pid)) {
        for (int i = 0; i < pl->paramCount(); ++i) {
            auto info = pl->paramInfo(i);
            if (info.id == pid)
                return pl->name() + QStringLiteral(" ▸ ") + info.name;
        }
        return pl->name();
    }
    return path;
}

// ---------------------------------------------------------------- notifications

void Project::setModified(bool m)
{
    if (m_modified == m)
        return;
    m_modified = m;
    emit modifiedChanged(m);
}

void Project::requestSnapshot()
{
    if (m_snapshotPending)
        return;
    m_snapshotPending = true;
    QTimer::singleShot(0, this, [this] {
        m_snapshotPending = false;
        emit snapshotNeeded();
    });
}

void Project::notifyChannels()
{
    setModified(true);
    emit channelsChanged();
    requestSnapshot();
}

void Project::notifyPatterns()
{
    setModified(true);
    emit patternsChanged();
    requestSnapshot();
}

void Project::notifyPatternEdited(ObjId pattern)
{
    setModified(true);
    emit patternEdited(pattern);
    requestSnapshot();
}

void Project::notifyPlaylist()
{
    setModified(true);
    emit playlistChanged();
    requestSnapshot();
}

void Project::notifyMixer()
{
    setModified(true);
    emit mixerChanged();
    requestSnapshot();
}

void Project::notifyAutomation()
{
    setModified(true);
    emit automationChanged();
    requestSnapshot();
}

void Project::notifyStructure()
{
    setModified(true);
    emit channelsChanged();
    emit patternsChanged();
    emit playlistChanged();
    emit mixerChanged();
    emit automationChanged();
    requestSnapshot();
}

// ---------------------------------------------------------------- new projects

void Project::createEmpty()
{
    clear();
    DefaultKit::ensure();   // keep the starter sounds available in the browser
    Pattern* p = addPattern();
    m_currentPattern = p->id;
    emit projectReset();
    notifyStructure();
    setModified(false);
}

// ---------------------------------------------------------------- default song

void Project::createDefault()
{
    clear();
    const auto kit = DefaultKit::ensure();
    auto load = [&](const QString& name) -> SamplePtr {
        auto it = kit.find(name);
        return it == kit.end() ? nullptr : SampleCache::instance().load(it->second);
    };

    Pattern* beat = addPattern();
    beat->name = QStringLiteral("Beat");
    Pattern* melody = addPattern();
    melody->name = QStringLiteral("Pluck Line");
    melody->steps = 32;

    struct Row { const char* sample; const char* steps; };
    const Row rows[] = {
        {"Kick", "x...x...x...x..."},
        {"Clap", "....x.......x..."},
        {"Hat Closed", "..x...x...x...xx"},
        {"Snare", "...........x...x"},
    };
    for (const auto& r : rows) {
        Channel* c = addSampler(load(QString::fromLatin1(r.sample)));
        if (!c)
            continue;
        auto& st = beat->stepsFor(c->id);
        for (int i = 0; i < 16; ++i)
            st[size_t(i)] = r.steps[i] == 'x' ? 100 : 0;
    }
    if (Channel* c = channel(channels.size() >= 3 ? channels[2]->id : 0))
        c->volume->setValue(0.55f);

    Channel* pluck = addSampler(load(QStringLiteral("Pluck")));
    if (pluck) {
        pluck->release->setValue(220.f);
        auto& notes = melody->notes[pluck->id];
        const int seq[] = {60, 63, 67, 70, 67, 63, 72, 70, 58, 62, 65, 70, 65, 62, 67, 65};
        for (int i = 0; i < 16; ++i)
            notes.push_back({seq[i], Tick(i) * kStepTicks * 2, kStepTicks * 2 - 6, i % 4 == 0 ? 0.9f : 0.7f});
        if (MixerInsert* m = insert(pluck->insert)) {
            m->filterType->setValue(float(FilterType::LowPass));
            m->resonance->setValue(0.45f);
        }
    }

    Channel* bass = addSampler(load(QStringLiteral("808")));
    if (bass) {
        bass->release->setValue(120.f);
        auto& notes = melody->notes[bass->id];
        notes.push_back({60, 0, kBarTicks - kStepTicks, 0.85f});
        notes.push_back({58, kBarTicks, kBarTicks - kStepTicks, 0.85f});
    }

    Channel* pads = addPads();
    const char* padNames[kNumPads] = {"Kick", "Snare", "Clap", "Rim",
                                      "Hat Closed", "Hat Open", "Shaker", "Cowbell",
                                      "Tom Low", "Tom High", "Crash", "Perc",
                                      "808", "Pluck", "Stab", "Pad"};
    for (int i = 0; i < kNumPads; ++i)
        pads->pads[size_t(i)].sample = load(QString::fromLatin1(padNames[i]));

    // Arrangement: 8 bars of beat, melody comes in at bar 5 with a filter sweep.
    for (int b = 0; b < 8; ++b)
        addClip(ClipKind::Pattern, beat->id, 0, Tick(b) * kBarTicks, kBarTicks);
    for (int b = 0; b < 2; ++b)
        addClip(ClipKind::Pattern, melody->id, 1, kBarTicks * 4 + Tick(b) * kBarTicks * 2, kBarTicks * 2);
    if (pluck) {
        AutomationClip* a = addAutomation(QStringLiteral("mixer/%1/cutoff").arg(pluck->insert));
        a->points = {{0, 0.30f}, {kBarTicks * 2, 0.85f}, {kBarTicks * 3, 0.55f}, {kBarTicks * 4, 0.95f}};
        addClip(ClipKind::Automation, a->id, 2, kBarTicks * 4, kBarTicks * 4);
    }

    tracks[0].name = QStringLiteral("Drums");
    tracks[1].name = QStringLiteral("Melody");
    tracks[2].name = QStringLiteral("Filter Sweep");

    m_currentPattern = beat->id;
    m_selectedChannel = channels.empty() ? 0 : channels.front()->id;
    setModified(false);
    emit projectReset();
    notifyStructure();
    setModified(false);
}

} // namespace guiro
