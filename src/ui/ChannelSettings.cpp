#include "ui/ChannelSettings.h"
#include "core/Project.h"
#include "engine/AudioEngine.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/UiHub.h"
#include "ui/widgets/Knob.h"
#include "ui/widgets/Waveform.h"

#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace guiro {

namespace {
QHash<ObjId, ChannelSettings*>& windows()
{
    static QHash<ObjId, ChannelSettings*> w;
    return w;
}

QLabel* sectionLabel(const QString& t, QWidget* parent)
{
    auto* l = new QLabel(t, parent);
    l->setStyleSheet(QStringLiteral("color:%1;font-size:7.5pt;font-weight:700;letter-spacing:1px;").arg(theme::textMute.name()));
    return l;
}
} // namespace

void ChannelSettings::open(Project* project, AudioEngine* engine, ObjId channel, QWidget* parent)
{
    if (!project->channel(channel))
        return;
    if (auto* w = windows().value(channel)) {
        w->show();
        w->raise();
        w->activateWindow();
        return;
    }
    auto* w = new ChannelSettings(project, engine, channel, parent);
    windows().insert(channel, w);
    w->show();
}

ChannelSettings::~ChannelSettings()
{
    windows().remove(m_id);
}

ChannelSettings::ChannelSettings(Project* project, AudioEngine* engine, ObjId channel, QWidget* parent)
    : QWidget(parent, Qt::Tool), m_project(project), m_engine(engine), m_id(channel)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumWidth(460);
    Channel* c = project->channel(channel);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(14, 12, 14, 14);
    lay->setSpacing(10);

    auto* head = new QHBoxLayout;
    m_title = new QLabel(this);
    m_title->setFont(theme::uiFont(11, QFont::DemiBold));
    head->addWidget(m_title, 1);
    if (c->kind == ChannelKind::Sampler || c->kind == ChannelKind::Audio) {
        auto* load = new QPushButton(icons::icon("open"), tr("Load sample…"), this);
        connect(load, &QPushButton::clicked, this, [this] {
            const QString f = QFileDialog::getOpenFileName(this, tr("Load sample"), QString(),
                                                           tr("Audio files (%1)").arg(SampleCache::nameFilters().join(' ')));
            if (!f.isEmpty())
                loadSample(f);
        });
        head->addWidget(load);
    }
    auto* preview = new QPushButton(icons::icon("play"), tr("Preview"), this);
    connect(preview, &QPushButton::pressed, this, [this] { m_engine->noteOn(m_id, kRootKey, 0.85f); });
    connect(preview, &QPushButton::released, this, [this] { m_engine->noteOff(m_id, kRootKey); });
    head->addWidget(preview);
    lay->addLayout(head);

    m_wave = new Waveform(this);
    m_wave->setMinimumHeight(90);
    connect(m_wave, &Waveform::fileDropped, this, &ChannelSettings::loadSample);
    if (c->kind == ChannelKind::Sampler || c->kind == ChannelKind::Audio)
        lay->addWidget(m_wave);
    else
        m_wave->hide();

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(4);
    grid->addWidget(sectionLabel(tr("CHANNEL"), this), 0, 0, 1, 3);
    grid->addWidget(sectionLabel(tr("ENVELOPE"), this), 0, 3, 1, 2);
    auto addKnob = [&](const ParamPtr& p, const QString& label, int col, const QColor& color) {
        auto* k = new Knob(this, 38);
        k->setParam(p);
        k->setLabel(label);
        k->setColor(color);
        grid->addWidget(k, 1, col, Qt::AlignHCenter);
    };
    addKnob(c->volume, tr("VOLUME"), 0, theme::accent);
    addKnob(c->pan, tr("PAN"), 1, theme::accent2);
    addKnob(c->pitch, tr("PITCH"), 2, theme::accent2);
    addKnob(c->attack, tr("ATTACK"), 3, theme::accent);
    addKnob(c->release, tr("RELEASE"), 4, theme::accent);
    lay->addLayout(grid);

    auto* fgrid = new QGridLayout;
    fgrid->setHorizontalSpacing(14);
    fgrid->addWidget(sectionLabel(tr("CHANNEL FILTER"), this), 0, 0, 1, 3);
    m_filterType = new QPushButton(this);
    m_filterType->setFixedWidth(64);
    connect(m_filterType, &QPushButton::clicked, this, [this] {
        Channel* ch = m_project->channel(m_id);
        if (!ch)
            return;
        ch->filterType->setValue(float((int(ch->filterType->value()) + 1) % 4));
        refresh();
        emit UiHub::instance()->paramEdited();
    });
    fgrid->addWidget(m_filterType, 1, 0, Qt::AlignVCenter);
    auto* cut = new Knob(this, 38);
    cut->setParam(c->cutoff);
    cut->setLabel(tr("CUTOFF"));
    auto* res = new Knob(this, 38);
    res->setParam(c->resonance);
    res->setLabel(tr("RES"));
    res->setColor(theme::accent2);
    fgrid->addWidget(cut, 1, 1, Qt::AlignHCenter);
    fgrid->addWidget(res, 1, 2, Qt::AlignHCenter);
    fgrid->setColumnStretch(3, 1);
    auto* tip = new QLabel(tr("Right-click any knob → Create automation clip"), this);
    tip->setStyleSheet(QStringLiteral("color:%1;font-size:8pt;").arg(theme::textMute.name()));
    fgrid->addWidget(tip, 1, 3, Qt::AlignRight | Qt::AlignBottom);
    lay->addLayout(fgrid);

    connect(project, &Project::channelsChanged, this, [this] {
        if (!m_project->channel(m_id))
            close();
        else
            refresh();
    });
    connect(project, &Project::projectReset, this, &QWidget::close);
    refresh();
}

void ChannelSettings::loadSample(const QString& path)
{
    Channel* c = m_project->channel(m_id);
    if (!c)
        return;
    if (SamplePtr s = SampleCache::instance().load(path)) {
        c->sample = s;
        c->name = s->name;
        m_project->notifyChannels();
    }
}

void ChannelSettings::refresh()
{
    Channel* c = m_project->channel(m_id);
    if (!c)
        return;
    setWindowTitle(tr("%1 — Channel settings").arg(c->name));
    m_title->setText(QStringLiteral("<span style='color:%1'>●</span>&nbsp; %2").arg(c->color.name(), c->name.toHtmlEscaped()));
    m_wave->setSample(c->sample);
    m_wave->setColor(c->color);
    const int ft = int(c->filterType->value());
    m_filterType->setText(c->filterType->choices().value(ft));
    m_filterType->setStyleSheet(ft ? QStringLiteral("color:%1;border-color:%1;").arg(theme::accent.name()) : QString());
}

} // namespace guiro
