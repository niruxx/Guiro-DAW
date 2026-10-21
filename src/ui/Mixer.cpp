#include "ui/Mixer.h"
#include "core/Project.h"
#include "engine/AudioEngine.h"
#include "engine/Runtime.h"
#include "plugins/PluginManager.h"
#include "ui/Browser.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/UiHub.h"
#include "ui/widgets/Fader.h"
#include "ui/widgets/Knob.h"
#include "ui/widgets/Meter.h"

#include <QColorDialog>
#include <QDragEnterEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace guiro {

// ================================================================ strip

MixerStrip::MixerStrip(Project* project, int index, QWidget* parent) : QWidget(parent), m_project(project), m_index(index)
{
    setFixedWidth(index == 0 ? 96 : 84);
    setAcceptDrops(true);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 30, 6, 10);
    lay->setSpacing(5);

    MixerInsert* m = project->insert(index);

    // Filter section
    m_filterType = new QPushButton(this);
    m_filterType->setFixedHeight(20);
    m_filterType->setToolTip(tr("Filter type (click to cycle)"));
    m_filterType->setStyleSheet(QStringLiteral("QPushButton{font-size:7.5pt;font-weight:700;padding:0 4px;}"));
    connect(m_filterType, &QPushButton::clicked, this, [this] {
        MixerInsert* mi = m_project->insert(m_index);
        mi->filterType->setValue(float((int(mi->filterType->value()) + 1) % 4));
        refresh();
        emit UiHub::instance()->paramEdited();
    });
    lay->addWidget(m_filterType);
    auto* fk = new QHBoxLayout;
    fk->setSpacing(0);
    m_cutoff = new Knob(this, 28);
    m_cutoff->setParam(m->cutoff);
    m_cutoff->setLabel(tr("CUT"));
    m_res = new Knob(this, 28);
    m_res->setParam(m->resonance);
    m_res->setLabel(tr("RES"));
    m_res->setColor(theme::accent2);
    fk->addWidget(m_cutoff);
    fk->addWidget(m_res);
    lay->addLayout(fk);

    auto* pk = new QHBoxLayout;
    m_pan = new Knob(this, 30);
    m_pan->setParam(m->pan);
    m_pan->setLabel(tr("PAN"));
    m_pan->setColor(theme::accent2);
    pk->addStretch(1);
    pk->addWidget(m_pan);
    pk->addStretch(1);
    lay->addLayout(pk);

    auto* fm = new QHBoxLayout;
    fm->setSpacing(6);
    m_fader = new Fader(this);
    m_fader->setParam(m->volume);
    m_fader->setColor(theme::accent);
    m_meter = new Meter(this);
    m_meter->setSources(&m->rt->peakL, &m->rt->peakR);
    fm->addStretch(1);
    fm->addWidget(m_fader);
    fm->addWidget(m_meter);
    fm->addStretch(1);
    lay->addLayout(fm, 1);

    auto* ms = new QHBoxLayout;
    ms->setSpacing(3);
    m_mute = new QPushButton(tr("M"), this);
    m_solo = new QPushButton(tr("S"), this);
    for (auto* b : {m_mute, m_solo}) {
        b->setCheckable(true);
        b->setFixedHeight(20);
        b->setStyleSheet(QStringLiteral("QPushButton{font-size:7.5pt;font-weight:700;padding:0;}"));
    }
    m_mute->setToolTip(tr("Mute"));
    m_solo->setToolTip(tr("Solo"));
    connect(m_mute, &QPushButton::toggled, this, [this](bool on) {
        m_project->insert(m_index)->muted = on;
        m_project->notifyMixer();
    });
    connect(m_solo, &QPushButton::toggled, this, [this](bool on) {
        m_project->insert(m_index)->solo = on;
        m_project->notifyMixer();
    });
    ms->addWidget(m_mute);
    if (index != 0)
        ms->addWidget(m_solo);
    else
        m_solo->hide();
    lay->addLayout(ms);
    refresh();
}

void MixerStrip::refresh()
{
    MixerInsert* m = m_project->insert(m_index);
    if (!m)
        return;
    const int ft = int(m->filterType->value());
    m_filterType->setText(m->filterType->choices().value(ft));
    m_filterType->setChecked(false);
    m_filterType->setStyleSheet(QStringLiteral("QPushButton{font-size:7.5pt;font-weight:700;padding:0 4px;%1}")
                                    .arg(ft ? QStringLiteral("color:%1;border-color:%1;").arg(theme::accent.name()) : QString()));
    m_cutoff->setEnabled(ft != 0);
    m_res->setEnabled(ft != 0);
    QSignalBlocker b1(m_mute), b2(m_solo);
    m_mute->setChecked(m->muted);
    m_solo->setChecked(m->solo);
    update();
}

void MixerStrip::setSelected(bool s)
{
    m_selected = s;
    update();
}

void MixerStrip::paintEvent(QPaintEvent*)
{
    MixerInsert* m = m_project->insert(m_index);
    if (!m)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);
    p.setBrush(m_selected ? theme::bg3 : theme::bg2);
    p.setPen(m_selected ? QPen(theme::accent, 1) : Qt::NoPen);
    p.drawRoundedRect(r, 6, 6);

    // Header
    p.setPen(Qt::NoPen);
    p.setBrush(m->color);
    p.drawRoundedRect(QRectF(r.left() + 6, r.top() + 5, r.width() - 12, 3), 1.5, 1.5);
    p.setPen(m_index == 0 ? theme::accent : theme::text);
    p.setFont(theme::uiFont(7.8, QFont::DemiBold));
    p.drawText(QRectF(r.left() + 4, r.top() + 9, r.width() - 8, 16), Qt::AlignCenter,
               p.fontMetrics().elidedText(m->name, Qt::ElideRight, int(r.width() - 8)));

    // FX count badge
    int fx = 0;
    for (const auto& s : m->fx)
        if (s.plugin)
            ++fx;
    if (fx) {
        p.setPen(Qt::NoPen);
        p.setBrush(theme::accent2);
        p.drawEllipse(QPointF(r.right() - 9, r.top() + 17), 3, 3);
    }
    if (m_index != 0) {
        p.setPen(theme::textMute);
        p.setFont(theme::monoFont(7));
        p.drawText(QRectF(r.left() + 6, r.top() + 10, 20, 14), Qt::AlignLeft | Qt::AlignVCenter, QString::number(m_index));
    }
}

void MixerStrip::mousePressEvent(QMouseEvent*)
{
    emit selected(m_index);
}

void MixerStrip::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->position().y() > 30)
        return;
    MixerInsert* m = m_project->insert(m_index);
    bool ok = false;
    const QString n = QInputDialog::getText(this, tr("Rename insert"), tr("Name"), QLineEdit::Normal, m->name, &ok);
    if (ok && !n.isEmpty()) {
        m->name = n;
        m_project->notifyMixer();
    }
}

void MixerStrip::contextMenuEvent(QContextMenuEvent* e)
{
    MixerInsert* m = m_project->insert(m_index);
    QMenu menu(this);
    menu.addSection(m->name);
    menu.addAction(icons::icon("edit"), tr("Rename…"), this, [this, m] {
        bool ok = false;
        const QString n = QInputDialog::getText(this, tr("Rename insert"), tr("Name"), QLineEdit::Normal, m->name, &ok);
        if (ok && !n.isEmpty()) {
            m->name = n;
            m_project->notifyMixer();
        }
    });
    menu.addAction(tr("Color…"), this, [this, m] {
        const QColor c = QColorDialog::getColor(m->color, this);
        if (c.isValid()) {
            m->color = c;
            m_project->notifyMixer();
        }
    });
    menu.addSeparator();
    menu.addAction(icons::icon("automation", theme::automation), tr("Automate filter cutoff"), this,
                   [m] { emit UiHub::instance()->automationRequested(m->cutoff->path()); });
    menu.addAction(icons::icon("automation", theme::automation), tr("Automate volume"), this,
                   [m] { emit UiHub::instance()->automationRequested(m->volume->path()); });
    menu.exec(e->globalPos());
}

void MixerStrip::dragEnterEvent(QDragEnterEvent* e)
{
    if (const PluginDescriptor* d = pluginFromMime(e->mimeData()); d && !d->instrument)
        e->acceptProposedAction();
}

void MixerStrip::dropEvent(QDropEvent* e)
{
    const PluginDescriptor* d = pluginFromMime(e->mimeData());
    MixerInsert* m = m_project->insert(m_index);
    if (!d || !m)
        return;
    for (auto& slot : m->fx) {
        if (slot.plugin)
            continue;
        QString err;
        slot.plugin = PluginManager::instance().create(*d, &err);
        if (!slot.plugin)
            QMessageBox::warning(this, tr("Plugin"), err);
        else
            emit UiHub::instance()->pluginWindowRequested(slot.plugin, d->name);
        break;
    }
    emit selected(m_index);
    m_project->notifyMixer();
    e->acceptProposedAction();
}

// ================================================================ FX rack

FxRack::FxRack(Project* project, QWidget* parent) : QWidget(parent), m_project(project)
{
    setFixedWidth(240);
    setAcceptDrops(true);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(6);
    m_title = new QLabel(this);
    m_title->setFont(theme::uiFont(9.5, QFont::DemiBold));
    lay->addWidget(m_title);
    auto* hint = new QLabel(tr("Effect slots — drop effect plugins here"), this);
    hint->setStyleSheet(QStringLiteral("color:%1;font-size:8pt;").arg(theme::textMute.name()));
    lay->addWidget(hint);
    m_slots = new QVBoxLayout;
    m_slots->setSpacing(4);
    lay->addLayout(m_slots);
    lay->addStretch(1);
    connect(project, &Project::mixerChanged, this, &FxRack::rebuild);
    connect(project, &Project::projectReset, this, &FxRack::rebuild);
    rebuild();
}

void FxRack::setInsert(int index)
{
    m_index = index;
    rebuild();
}

void FxRack::loadInto(int slot)
{
    MixerInsert* m = m_project->insert(m_index);
    if (!m)
        return;
    QMenu menu(this);
    int count = 0;
    for (const auto& d : PluginManager::instance().plugins()) {
        if (d.instrument)
            continue;
        ++count;
        menu.addAction(icons::icon("effect", theme::accent2), QStringLiteral("%1  ·  %2").arg(d.name, formatName(d.format)), this,
                       [this, d, slot, m] {
                           QString err;
                           PluginPtr p = PluginManager::instance().create(d, &err);
                           if (!p) {
                               QMessageBox::warning(this, tr("Plugin"), tr("Could not load %1:\n%2").arg(d.name, err));
                               return;
                           }
                           m->fx[size_t(slot)].plugin = p;
                           m->fx[size_t(slot)].bypass = false;
                           m_project->notifyMixer();
                           emit UiHub::instance()->pluginWindowRequested(p, d.name);
                       });
    }
    if (!count)
        menu.addAction(tr("No effect plugins found — install some and rescan in the browser"))->setEnabled(false);
    menu.exec(QCursor::pos());
}

void FxRack::rebuild()
{
    for (auto* w : m_slotWidgets)
        w->deleteLater();
    m_slotWidgets.clear();
    MixerInsert* m = m_project->insert(m_index);
    if (!m)
        return;
    m_title->setText(QStringLiteral("<span style='color:%1'>●</span>&nbsp; %2").arg(m->color.name(), m->name.toHtmlEscaped()));
    for (int i = 0; i < kNumFxSlots; ++i) {
        auto& slot = m->fx[size_t(i)];
        auto* row = new QWidget(this);
        row->setFixedHeight(30);
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(4);
        if (slot.plugin) {
            auto* power = new QToolButton(row);
            power->setCheckable(true);
            power->setChecked(!slot.bypass);
            power->setIcon(icons::icon("power", slot.bypass ? theme::textMute : theme::good, 16));
            power->setToolTip(tr("Enable / bypass"));
            connect(power, &QToolButton::toggled, this, [this, m, i](bool on) {
                m->fx[size_t(i)].bypass = !on;
                m_project->notifyMixer();
            });
            auto* name = new QPushButton(slot.plugin->name(), row);
            name->setToolTip(tr("Open editor"));
            name->setStyleSheet(QStringLiteral("text-align:left;"));
            PluginPtr pl = slot.plugin;
            connect(name, &QPushButton::clicked, this, [pl] { emit UiHub::instance()->pluginWindowRequested(pl, pl->name()); });
            auto* del = new QToolButton(row);
            del->setIcon(icons::icon("close", theme::textMute, 14));
            del->setToolTip(tr("Remove"));
            connect(del, &QToolButton::clicked, this, [this, m, i] {
                m->fx[size_t(i)].plugin.reset();
                m_project->notifyMixer();
            });
            rl->addWidget(power);
            rl->addWidget(name, 1);
            rl->addWidget(del);
        } else {
            auto* empty = new QPushButton(QStringLiteral("%1   ·   %2").arg(i + 1).arg(tr("Empty slot")), row);
            empty->setStyleSheet(QStringLiteral("text-align:left;color:%1;background:%2;border-style:dashed;")
                                     .arg(theme::textMute.name(), theme::bg1.name()));
            connect(empty, &QPushButton::clicked, this, [this, i] { loadInto(i); });
            rl->addWidget(empty, 1);
        }
        m_slots->addWidget(row);
        m_slotWidgets.push_back(row);
    }
}

void FxRack::dragEnterEvent(QDragEnterEvent* e)
{
    if (const PluginDescriptor* d = pluginFromMime(e->mimeData()); d && !d->instrument)
        e->acceptProposedAction();
}

void FxRack::dropEvent(QDropEvent* e)
{
    const PluginDescriptor* d = pluginFromMime(e->mimeData());
    MixerInsert* m = m_project->insert(m_index);
    if (!d || !m)
        return;
    for (auto& slot : m->fx) {
        if (slot.plugin)
            continue;
        QString err;
        slot.plugin = PluginManager::instance().create(*d, &err);
        if (slot.plugin)
            emit UiHub::instance()->pluginWindowRequested(slot.plugin, d->name);
        else
            QMessageBox::warning(this, tr("Plugin"), err);
        break;
    }
    m_project->notifyMixer();
    e->acceptProposedAction();
}

// ================================================================ mixer

Mixer::Mixer(Project* project, AudioEngine*, QWidget* parent) : QWidget(parent), m_project(project)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget(scroll);
    m_strips = new QHBoxLayout(body);
    m_strips->setContentsMargins(6, 6, 6, 6);
    m_strips->setSpacing(4);
    m_strips->addStretch(1);
    scroll->setWidget(body);
    lay->addWidget(scroll, 1);
    auto* sep = new QWidget(this);
    sep->setFixedWidth(1);
    sep->setStyleSheet(QStringLiteral("background:%1;").arg(theme::line.name()));
    lay->addWidget(sep);
    m_fx = new FxRack(project, this);
    lay->addWidget(m_fx);

    connect(project, &Project::mixerChanged, this, [this] {
        for (auto* s : m_stripWidgets)
            s->refresh();
    });
    connect(project, &Project::channelsChanged, this, [this] {
        for (auto* s : m_stripWidgets)
            s->update();
    });
    connect(project, &Project::projectReset, this, &Mixer::rebuild);
    rebuild();
}

void Mixer::rebuild()
{
    for (auto* s : m_stripWidgets)
        s->deleteLater();
    m_stripWidgets.clear();
    for (int i = 0; i <= kNumInserts; ++i) {
        auto* s = new MixerStrip(m_project, i, this);
        connect(s, &MixerStrip::selected, this, &Mixer::selectInsert);
        m_strips->insertWidget(m_strips->count() - 1, s);
        m_stripWidgets.push_back(s);
    }
    selectInsert(m_selected);
}

void Mixer::selectInsert(int index)
{
    m_selected = index;
    for (auto* s : m_stripWidgets)
        s->setSelected(s->index() == index);
    m_fx->setInsert(index);
}

} // namespace guiro
