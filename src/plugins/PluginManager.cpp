#include "plugins/PluginManager.h"
#include "engine/AudioEngine.h"
#include "plugins/ClapHost.h"
#include "plugins/Lv2Host.h"
#include "plugins/Vst2Host.h"
#include "plugins/Vst3Host.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <cstdio>

namespace guiro {

QString formatName(PluginFormat f)
{
    switch (f) {
    case PluginFormat::VST3: return QStringLiteral("VST3");
    case PluginFormat::CLAP: return QStringLiteral("CLAP");
    case PluginFormat::LV2: return QStringLiteral("LV2");
    case PluginFormat::VST2: return QStringLiteral("VST2");
    }
    return {};
}

bool formatFromName(const QString& s, PluginFormat* out)
{
    const QString u = s.toUpper();
    if (u == QLatin1String("VST3")) *out = PluginFormat::VST3;
    else if (u == QLatin1String("CLAP")) *out = PluginFormat::CLAP;
    else if (u == QLatin1String("LV2")) *out = PluginFormat::LV2;
    else if (u == QLatin1String("VST2") || u == QLatin1String("VST")) *out = PluginFormat::VST2;
    else return false;
    return true;
}

namespace {

QJsonObject toJson(const PluginDescriptor& d)
{
    return QJsonObject{
        {"format", formatName(d.format)}, {"uid", d.uid},           {"path", d.path},
        {"name", d.name},                 {"vendor", d.vendor},     {"category", d.category},
        {"instrument", d.instrument},     {"bridged", d.bridged},
    };
}

bool fromJson(const QJsonObject& o, PluginDescriptor* d)
{
    if (!formatFromName(o.value("format").toString(), &d->format))
        return false;
    d->uid = o.value("uid").toString();
    d->path = o.value("path").toString();
    d->name = o.value("name").toString();
    d->vendor = o.value("vendor").toString();
    d->category = o.value("category").toString();
    d->instrument = o.value("instrument").toBool();
    d->bridged = o.value("bridged").toBool();
    return !d->uid.isEmpty() && !d->name.isEmpty();
}

// Recursively finds plugin files/bundles without descending into bundles.
void collectPlugins(const QString& dir, const QString& ext, QStringList& out, int depth)
{
    if (depth > 6)
        return;
    const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : entries) {
        if (fi.fileName().endsWith(ext, Qt::CaseInsensitive))
            out << fi.absoluteFilePath();
        else if (fi.isDir())
            collectPlugins(fi.absoluteFilePath(), ext, out, depth + 1);
    }
}

} // namespace

bool PluginManager::isWineBridged(const QString& path)
{
    if (path.contains(QLatin1String("/yabridge/")))
        return true;
    const QFileInfo fi(path);
    if (fi.suffix() == QLatin1String("so") && QFileInfo::exists(fi.absolutePath() + "/" + fi.completeBaseName() + ".dll"))
        return true;
    if (fi.suffix() == QLatin1String("vst3") && fi.isDir())
        return QFileInfo::exists(path + "/Contents/x86_64-win") || QFileInfo::exists(path + "/Contents/x86-win");
    return false;
}

PluginManager& PluginManager::instance()
{
    static PluginManager m;
    return m;
}

QStringList PluginManager::supportedFormats()
{
    QStringList f{QStringLiteral("VST2")};
#ifdef GUIRO_HAVE_VST3
    f << QStringLiteral("VST3");
#endif
#ifdef GUIRO_HAVE_CLAP
    f << QStringLiteral("CLAP");
#endif
#ifdef GUIRO_HAVE_LV2
    f << QStringLiteral("LV2");
#endif
    return f;
}

QStringList PluginManager::searchPaths(PluginFormat f)
{
    const QString home = QDir::homePath();
    QStringList paths;
    auto env = [&](const char* var) {
        const QString v = qEnvironmentVariable(var);
        if (!v.isEmpty())
            paths << v.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    };
    switch (f) {
    case PluginFormat::VST3:
        env("VST3_PATH");
        paths << home + "/.vst3" << "/usr/lib/vst3" << "/usr/local/lib/vst3" << "/usr/lib64/vst3"
              << "/var/lib/flatpak/runtime/org.freedesktop.LinuxAudio.Plugins/current/active/files/lib/vst3";
        break;
    case PluginFormat::CLAP:
        env("CLAP_PATH");
        paths << home + "/.clap" << "/usr/lib/clap" << "/usr/local/lib/clap" << "/usr/lib64/clap";
        break;
    case PluginFormat::VST2:
        env("VST_PATH");
        env("LXVST_PATH");
        paths << home + "/.vst" << home + "/.lxvst" << "/usr/lib/vst" << "/usr/local/lib/vst" << "/usr/lib64/vst"
              << "/usr/lib/lxvst" << "/usr/local/lib/lxvst";
        break;
    case PluginFormat::LV2:
        env("LV2_PATH");
        paths << home + "/.lv2" << "/usr/lib/lv2" << "/usr/local/lib/lv2";
        break;
    }
    paths.removeDuplicates();
    return paths;
}

QString PluginManager::cachePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + "/.config";
    return dir + "/Guiro/plugins.json";
}

const PluginDescriptor* PluginManager::find(PluginFormat f, const QString& uid) const
{
    for (const auto& d : m_plugins)
        if (d.format == f && d.uid == uid)
            return &d;
    return nullptr;
}

void PluginManager::loadCache()
{
    QFile f(cachePath());
    if (!f.open(QIODevice::ReadOnly)) {
        QTimer::singleShot(1500, this, &PluginManager::rescan);
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    m_plugins.clear();
    for (const auto& v : root.value("plugins").toArray()) {
        PluginDescriptor d;
        if (fromJson(v.toObject(), &d))
            m_plugins.push_back(d);
    }
    m_failed.clear();
    for (const auto& v : root.value("failed").toArray())
        m_failed << v.toString();
    emit pluginsChanged();
}

void PluginManager::saveCache()
{
    QJsonArray arr;
    for (const auto& d : m_plugins)
        arr.append(toJson(d));
    QJsonObject root{{"version", 1}, {"plugins", arr}, {"failed", QJsonArray::fromStringList(m_failed)}};
    QDir().mkpath(QFileInfo(cachePath()).absolutePath());
    QFile f(cachePath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson());
}

void PluginManager::rescan()
{
    if (m_scanning)
        return;
    m_scanning = true;
    m_queue.clear();
    m_found.clear();
    m_failed.clear();

    for (const QString& dir : searchPaths(PluginFormat::VST2)) {
        QStringList found;
        collectPlugins(dir, QStringLiteral(".so"), found, 0);
        for (const QString& p : found)
            m_queue.push_back({PluginFormat::VST2, p});
    }
#ifdef GUIRO_HAVE_VST3
    for (const QString& dir : searchPaths(PluginFormat::VST3)) {
        QStringList found;
        collectPlugins(dir, QStringLiteral(".vst3"), found, 0);
        for (const QString& p : found)
            m_queue.push_back({PluginFormat::VST3, p});
    }
#endif
#ifdef GUIRO_HAVE_CLAP
    for (const QString& dir : searchPaths(PluginFormat::CLAP)) {
        QStringList found;
        collectPlugins(dir, QStringLiteral(".clap"), found, 0);
        for (const QString& p : found)
            m_queue.push_back({PluginFormat::CLAP, p});
    }
#endif
#ifdef GUIRO_HAVE_LV2
    // LV2 metadata is plain Turtle - safe to read in-process.
    m_found += lv2ScanAll();
#endif

    m_total = int(m_queue.size());
    m_done = 0;
    m_running = 0;
    emit scanProgress(0, m_total, QString());
    if (m_queue.isEmpty()) {
        m_plugins = m_found;
        m_scanning = false;
        saveCache();
        emit pluginsChanged();
        emit scanFinished();
        return;
    }
    for (int i = 0; i < 4; ++i)
        startNext();
}

void PluginManager::startNext()
{
    if (m_queue.isEmpty()) {
        if (m_running == 0 && m_scanning) {
            std::sort(m_found.begin(), m_found.end(), [](const PluginDescriptor& a, const PluginDescriptor& b) {
                return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
            });
            m_plugins = m_found;
            m_scanning = false;
            saveCache();
            emit pluginsChanged();
            emit scanFinished();
        }
        return;
    }
    const Job job = m_queue.takeFirst();
    ++m_running;
    auto* proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    auto* timeout = new QTimer(proc);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, proc, [proc] { proc->kill(); });
    connect(proc, &QProcess::finished, this, [this, proc, job](int code, QProcess::ExitStatus st) {
        const QList<QByteArray> lines = proc->readAllStandardOutput().split('\n');
        bool any = false;
        for (const QByteArray& line : lines) {
            if (!line.startsWith('{'))
                continue;
            PluginDescriptor d;
            if (fromJson(QJsonDocument::fromJson(line).object(), &d)) {
                d.bridged = isWineBridged(d.path);
                m_found.push_back(d);
                any = true;
            }
        }
        if (st != QProcess::NormalExit || (code != 0 && !any))
            m_failed << job.path;
        proc->deleteLater();
        --m_running;
        ++m_done;
        emit scanProgress(m_done, m_total, QFileInfo(job.path).fileName());
        startNext();
    });
    proc->start(QCoreApplication::applicationFilePath(), {"--scan-plugin", formatName(job.format), job.path});
    // Bridged plugins may have to boot a Wine prefix on first load.
    timeout->start(isWineBridged(job.path) ? 180000 : 30000);
}

int PluginManager::runScanner(const QString& format, const QString& path)
{
    PluginFormat f;
    if (!formatFromName(format, &f))
        return 2;
    QList<PluginDescriptor> found;
    switch (f) {
    case PluginFormat::VST2:
        found = vst2Scan(path);
        break;
    case PluginFormat::VST3:
#ifdef GUIRO_HAVE_VST3
        found = vst3Scan(path);
#endif
        break;
    case PluginFormat::CLAP:
#ifdef GUIRO_HAVE_CLAP
        found = clapScan(path);
#endif
        break;
    case PluginFormat::LV2:
        break;
    }
    for (const auto& d : found) {
        const QByteArray line = QJsonDocument(toJson(d)).toJson(QJsonDocument::Compact);
        std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
        std::fputc('\n', stdout);
    }
    std::fflush(stdout);
    return found.isEmpty() ? 1 : 0;
}

PluginPtr PluginManager::create(const PluginDescriptor& d, QString* error)
{
    PluginPtr p;
    switch (d.format) {
    case PluginFormat::VST2:
        p = vst2Create(d, error);
        break;
    case PluginFormat::VST3:
#ifdef GUIRO_HAVE_VST3
        p = vst3Create(d, error);
#endif
        break;
    case PluginFormat::CLAP:
#ifdef GUIRO_HAVE_CLAP
        p = clapCreate(d, error);
#endif
        break;
    case PluginFormat::LV2:
#ifdef GUIRO_HAVE_LV2
        p = lv2Create(d, error);
#endif
        break;
    }
    if (!p) {
        if (error && error->isEmpty())
            *error = tr("%1 support is not available in this build").arg(formatName(d.format));
        return nullptr;
    }
    const double sr = AudioEngine::instance() ? AudioEngine::instance()->sampleRate() : 48000.0;
    if (!p->activate(sr, kMaxBlock)) {
        if (error)
            *error = tr("The plugin failed to activate");
        return nullptr;
    }
    return p;
}

} // namespace guiro
