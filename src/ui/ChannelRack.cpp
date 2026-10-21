#include "ui/ChannelRack.h"
#include "core/DefaultKit.h"
#include "core/Project.h"
#include "engine/AudioEngine.h"
#include "engine/Runtime.h"
#include "plugins/PluginManager.h"
#include "ui/Browser.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/UiHub.h"
#include "ui/widgets/Knob.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QFileDialog>
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
#include <cmath>

namespace guiro {

void addPluginChannel(Project* project, const PluginDescriptor& d, QWidget* parent)
{
    QString err;
    PluginPtr p = PluginManager::instance().create(d, &err);
    if (!p) {
        QMessageBox::warning(parent, QObject::tr("Plugin"), QObject::tr("Could not load %1:\n%2").arg(d.name, err));
        return;
    }
    Channel* c = project->addInstrument(p);
    project->setSelectedChannel(c->id);
    emit UiHub::instance()->pluginWindowRequested(p, c->name);
}

// ---------------------------------------------------------------- row

ChannelRow::ChannelRow(Project* project, AudioEngine* engine, ObjId channel, QWidget* parent)
    : QWidget(parent), m_project(project), m_engine(engine), m_id(channel)
{
    setFixedHeight(kHeight);
    setMouseTracking(true);
    setAcceptDrops(true);
    m_pan = new Knob(this, 22);
    m_vol = new Knob(this, 22);
    m_pan->move(28, (kHeight - m_pan->height()) / 2);
    m_vol->move(55, (kHeight - m_vol->height()) / 2);
    m_pan->setColor(theme::accent2);
    connect(UiHub::instance(), &UiHub::frame, this, &ChannelRow::tickFrame);
    refresh();
}

Channel* ChannelRow::channel() const
{
    return m_project->channel(m_id);
}

void ChannelRow::refresh()
{
    Channel* c = channel();
    if (!c)
        return;
    m_pan->setParam(c->pan);
    m_vol->setParam(c->volume);
    Pattern* pat = m_project->currentPattern();
    const int steps = pat ? pat->steps : 16;
    setMinimumWidth(stepX(steps) + 16);
    update();
}

bool ChannelRow::showsPreview() const
{
    Channel* c = channel();
    if (!c)
        return false;
    if (c->kind == ChannelKind::Pads || c->kind == ChannelKind::Audio)
        return true;
    Pattern* pat = m_project->currentPattern();
    if (!pat)
        return false;
    auto it = pat->notes.find(m_id);
    return it != pat->notes.end() && !it->second.empty();
}

void ChannelRow::tickFrame()
{
    if (!isVisible())
        return;
    Channel* c = channel();
    if (!c)
        return;
    const float act = c->rt ? c->rt->activity.exchange(0.f) : 0.f;
    const float na = std::max(act, m_activity * 0.85f);
    int ps = -1;
    if (m_engine->isPlaying() && !m_engine->songMode()) {
        if (Pattern* pat = m_project->currentPattern()) {
            const Tick len = pat->length();
            const Tick pos = Tick(m_engine->position()) % std::max<Tick>(1, len);
            ps = int(pos / kStepTicks);
        }
    }
    if (std::abs(na - m_activity) > 0.01f || ps != m_playStep) {
        m_activity = na;
        m_playStep = ps;
        update();
    }
}

int ChannelRow::stepAt(QPointF p) const
{
    Pattern* pat = m_project->currentPattern();
    if (!pat || p.x() < kStepsX)
        return -1;
    for (int i = 0; i < pat->steps; ++i) {
        const int x = stepX(i);
        if (p.x() >= x && p.x() < x + kStepW + kStepGap)
            return i;
    }
    return -1;
}

void ChannelRow::paintEvent(QPaintEvent*)
{
    Channel* c = channel();
    if (!c)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool selected = m_project->selectedChannel() == m_id;

    if (selected) {
        p.setPen(Qt::NoPen);
        p.setBrush(theme::bg2);
        p.drawRoundedRect(QRectF(rect()).adjusted(2, 1, -2, -1), 6, 6);
    }

    // Mute LED
    const QRectF led = ledRect();
    p.setPen(Qt::NoPen);
    p.setBrush(c->muted ? theme::bg4 : theme::good);
    p.drawEllipse(led.adjusted(2, 2, -2, -2));

    // Insert box
    const QRectF ins = insertRect();
    p.setPen(Qt::NoPen);
    p.setBrush(theme::bg0);
    p.drawRoundedRect(ins, 4, 4);
    p.setFont(theme::monoFont(8.5));
    p.setPen(c->insert == 0 ? theme::textMute : theme::text);
    p.drawText(ins, Qt::AlignCenter, c->insert == 0 ? QStringLiteral("--") : QString::number(c->insert));

    // Name button
    const QRectF nr = nameRect();
    p.setBrush(selected ? theme::bg4 : theme::bg3);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(nr, 6, 6);
    QColor stripe = c->color;
    if (m_activity > 0.01f)
        stripe = theme::mix(c->color, Qt::white, std::min(0.6f, m_activity));
    p.setPen(Qt::NoPen);
    p.setBrush(stripe);
    p.drawRoundedRect(QRectF(nr.left() + 5, nr.top() + 7, 3, nr.height() - 14), 1.5, 1.5);
    QString kindIcon;
    switch (c->kind) {
    case ChannelKind::Instrument: kindIcon = "instrument"; break;
    case ChannelKind::Pads: kindIcon = "pads"; break;
    case ChannelKind::Audio: kindIcon = "wave"; break;
    default: break;
    }
    qreal textLeft = nr.left() + 12;
    if (!kindIcon.isEmpty()) {
        icons::paint(p, kindIcon, QRectF(textLeft, nr.center().y() - 7, 14, 14), theme::textDim);
        textLeft += 18;
    }
    p.setPen(c->muted ? theme::textMute : theme::text);
    p.setFont(theme::uiFont(8.5, QFont::Medium));
    const QRectF textR(textLeft, nr.top(), nr.right() - textLeft - 6, nr.height());
    p.drawText(textR, Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(c->name, Qt::ElideRight, int(textR.width())));

    Pattern* pat = m_project->currentPattern();
    if (!pat)
        return;

    if (showsPreview()) {
        const QRectF area(kStepsX, 5, stepX(pat->steps - 1) + kStepW - kStepsX, kHeight - 10);
        p.setPen(Qt::NoPen);
        p.setBrush(theme::bg0);
        p.drawRoundedRect(area, 4, 4);
        if (c->kind == ChannelKind::Audio) {
            p.setPen(theme::textMute);
            p.setFont(theme::uiFont(8));
            p.drawText(area.adjusted(10, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, tr("Audio clip channel — arrange it in the playlist"));
            return;
        }
        auto it = pat->notes.find(m_id);
        if (it == pat->notes.end() || it->second.empty()) {
            p.setPen(theme::textMute);
            p.setFont(theme::uiFont(8));
            p.drawText(area.adjusted(10, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       c->kind == ChannelKind::Pads ? tr("Pad bank — play the pads or open the piano roll") : QString());
            return;
        }
        int lo = 127, hi = 0;
        for (const auto& n : it->second) {
            lo = std::min(lo, n.key);
            hi = std::max(hi, n.key);
        }
        const int range = std::max(hi - lo, 8);
        const double len = double(pat->length());
        const QRectF inner = area.adjusted(6, 4, -6, -4);
        p.setPen(Qt::NoPen);
        p.setBrush(c->color);
        for (const auto& n : it->second) {
            const qreal x = inner.left() + inner.width() * double(n.start) / len;
            const qreal w = std::max(2.0, inner.width() * double(n.length) / len - 1);
            const qreal y = inner.bottom() - inner.height() * double(n.key - lo + 1) / double(range + 2);
            p.drawRoundedRect(QRectF(x, y - 1.5, w, 3), 1.5, 1.5);
        }
        return;
    }

    const auto sit = pat->stepData.find(m_id);
    for (int i = 0; i < pat->steps; ++i) {
        const QRectF r(stepX(i), 5, kStepW, kHeight - 10);
        const uint8_t v = (sit != pat->stepData.end() && i < int(sit->second.size())) ? sit->second[size_t(i)] : 0;
        const bool group = (i / 4) % 2 == 0;
        QColor base = group ? theme::bg3 : theme::mix(theme::bg3, theme::bg4, 0.55);
        if (i == m_hoverStep)
            base = theme::bg4;
        p.setPen(Qt::NoPen);
        if (v) {
            const float vel = float(v) / 127.f;
            p.setBrush(theme::mix(theme::bg4, theme::accent, 0.45 + 0.55 * vel));
        } else {
            p.setBrush(base);
        }
        p.drawRoundedRect(r, 4, 4);
        if (i == m_playStep) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, v ? 80 : 26));
            p.drawRoundedRect(r, 4, 4);
        }
    }
}

void ChannelRow::setStep(int i, bool on)
{
    Pattern* pat = m_project->currentPattern();
    if (!pat || i < 0 || i >= pat->steps)
        return;
    auto& st = pat->stepsFor(m_id);
    const uint8_t want = on ? (st[size_t(i)] ? st[size_t(i)] : 100) : 0;
    if (st[size_t(i)] == want)
        return;
    st[size_t(i)] = want;
    m_project->notifyPatternEdited(pat->id);
    update();
}

void ChannelRow::mousePressEvent(QMouseEvent* e)
{
    Channel* c = channel();
    if (!c)
        return;
    const QPointF pos = e->position();
    if (ledRect().adjusted(-4, -4, 4, 4).contains(pos)) {
        if (e->button() == Qt::LeftButton) {
            c->muted = !c->muted;
        } else if (e->button() == Qt::RightButton) {
            // Solo: mute everything else (toggle).
            bool othersMuted = true;
            for (auto& o : m_project->channels)
                if (o->id != m_id && !o->muted)
                    othersMuted = false;
            for (auto& o : m_project->channels)
                o->muted = othersMuted ? false : (o->id != m_id);
        }
        m_project->notifyChannels();
        return;
    }
    if (insertRect().contains(pos) && e->button() == Qt::LeftButton) {
        m_insertDrag = true;
        m_insertPressY = pos.y();
        m_insertPressVal = c->insert;
        return;
    }
    if (nameRect().contains(pos)) {
        m_project->setSelectedChannel(m_id);
        if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier))
            m_engine->noteOn(m_id, c->kind == ChannelKind::Pads ? kPadBaseKey : kRootKey, 0.8f);
        return;
    }
    if (showsPreview()) {
        if (e->button() == Qt::LeftButton && pos.x() >= kStepsX) {
            m_project->setSelectedChannel(m_id);
            if (c->kind == ChannelKind::Pads)
                emit UiHub::instance()->padsRequested(m_id);
            if (c->kind != ChannelKind::Audio)
                emit UiHub::instance()->pianoRollRequested(m_id);
        }
        return;
    }
    const int s = stepAt(pos);
    if (s < 0)
        return;
    Pattern* pat = m_project->currentPattern();
    const auto& st = pat->stepsFor(m_id);
    if (e->button() == Qt::LeftButton) {
        m_paintMode = st[size_t(s)] ? 0 : 1;
        setStep(s, m_paintMode == 1);
    } else if (e->button() == Qt::RightButton) {
        m_paintMode = 0;
        setStep(s, false);
    }
}

void ChannelRow::mouseMoveEvent(QMouseEvent* e)
{
    if (m_insertDrag) {
        Channel* c = channel();
        const int v = std::clamp(m_insertPressVal + int((m_insertPressY - e->position().y()) / 6.0), 0, kNumInserts);
        if (c && v != c->insert) {
            c->insert = v;
            m_project->notifyChannels();
        }
        return;
    }
    const int s = stepAt(e->position());
    if (m_paintMode >= 0 && s >= 0)
        setStep(s, m_paintMode == 1);
    if (s != m_hoverStep && !showsPreview()) {
        m_hoverStep = s;
        update();
    }
}

void ChannelRow::mouseReleaseEvent(QMouseEvent*)
{
    m_paintMode = -1;
    m_insertDrag = false;
    if (Channel* c = channel())
        m_engine->noteOff(m_id, c->kind == ChannelKind::Pads ? kPadBaseKey : kRootKey);
}

void ChannelRow::leaveEvent(QEvent*)
{
    m_hoverStep = -1;
    update();
}

void ChannelRow::mouseDoubleClickEvent(QMouseEvent* e)
{
    Channel* c = channel();
    if (!c || !nameRect().contains(e->position()))
        return;
    switch (c->kind) {
    case ChannelKind::Instrument:
        if (c->plugin)
            emit UiHub::instance()->pluginWindowRequested(c->plugin, c->name);
        break;
    case ChannelKind::Pads:
        emit UiHub::instance()->padsRequested(m_id);
        break;
    default:
        emit UiHub::instance()->channelSettingsRequested(m_id);
        break;
    }
}

void ChannelRow::wheelEvent(QWheelEvent* e)
{
    Channel* c = channel();
    if (!c)
        return;
    const int dir = e->angleDelta().y() > 0 ? 1 : -1;
    if (insertRect().contains(e->position())) {
        c->insert = std::clamp(c->insert + dir, 0, kNumInserts);
        m_project->notifyChannels();
        e->accept();
        return;
    }
    const int s = stepAt(e->position());
    Pattern* pat = m_project->currentPattern();
    if (s >= 0 && pat && !showsPreview()) {
        auto& st = pat->stepsFor(m_id);
        if (st[size_t(s)]) {
            st[size_t(s)] = uint8_t(std::clamp(int(st[size_t(s)]) + dir * 8, 1, 127));
            m_project->notifyPatternEdited(pat->id);
            update();
            emit UiHub::instance()->statusMessage(tr("Step velocity %1").arg(st[size_t(s)]), 1200);
            e->accept();
            return;
        }
    }
    e->ignore();
}

void ChannelRow::contextMenuEvent(QContextMenuEvent* e)
{
    if (nameRect().contains(e->pos()) || insertRect().contains(e->pos()))
        showMenu(e->globalPos());
}

void ChannelRow::showMenu(const QPoint& global)
{
    Channel* c = channel();
    if (!c)
        return;
    QMenu menu(this);
    menu.addSection(c->name);
    if (c->kind != ChannelKind::Audio)
        menu.addAction(icons::icon("piano"), tr("Piano roll"), this, [this] { emit UiHub::instance()->pianoRollRequested(m_id); });
    if (c->kind == ChannelKind::Instrument && c->plugin)
        menu.addAction(icons::icon("plugin"), tr("Show plugin editor"), this,
                       [this, c] { emit UiHub::instance()->pluginWindowRequested(c->plugin, c->name); });
    if (c->kind == ChannelKind::Pads)
        menu.addAction(icons::icon("pads"), tr("Open pads"), this, [this] { emit UiHub::instance()->padsRequested(m_id); });
    menu.addAction(icons::icon("gear"), tr("Channel settings"), this,
                   [this] { emit UiHub::instance()->channelSettingsRequested(m_id); });
    if (c->kind == ChannelKind::Sampler || c->kind == ChannelKind::Audio) {
        menu.addAction(icons::icon("open"), tr("Replace sample…"), this, [this, c] {
            const QString f = QFileDialog::getOpenFileName(this, tr("Load sample"), QString(),
                                                           tr("Audio files (%1)").arg(SampleCache::nameFilters().join(' ')));
            if (f.isEmpty())
                return;
            if (SamplePtr s = SampleCache::instance().load(f)) {
                c->sample = s;
                c->name = s->name;
                m_project->notifyChannels();
            }
        });
    }
    menu.addSeparator();
    menu.addAction(icons::icon("edit"), tr("Rename…"), this, [this, c] {
        bool ok = false;
        const QString n = QInputDialog::getText(this, tr("Rename channel"), tr("Name"), QLineEdit::Normal, c->name, &ok);
        if (ok && !n.isEmpty()) {
            c->name = n;
            m_project->notifyChannels();
        }
    });
    menu.addAction(tr("Color…"), this, [this, c] {
        const QColor col = QColorDialog::getColor(c->color, this, tr("Channel color"));
        if (col.isValid()) {
            c->color = col;
            m_project->notifyChannels();
        }
    });
    QMenu* route = menu.addMenu(icons::icon("mixer"), tr("Route to mixer insert"));
    for (int i = 0; i <= kNumInserts; ++i) {
        MixerInsert* m = m_project->insert(i);
        QAction* a = route->addAction(i == 0 ? tr("Master") : QStringLiteral("%1 · %2").arg(i).arg(m ? m->name : QString()));
        a->setCheckable(true);
        a->setChecked(c->insert == i);
        connect(a, &QAction::triggered, this, [this, c, i] {
            c->insert = i;
            m_project->notifyChannels();
        });
    }
    menu.addSeparator();
    menu.addAction(tr("Move up"), this, [this] { m_project->moveChannel(m_id, -1); });
    menu.addAction(tr("Move down"), this, [this] { m_project->moveChannel(m_id, 1); });
    if (c->kind != ChannelKind::Instrument)
        menu.addAction(tr("Clone"), this, [this] { m_project->cloneChannel(m_id); });
    menu.addAction(tr("Clear steps"), this, [this] {
        if (Pattern* pat = m_project->currentPattern()) {
            pat->stepData.erase(m_id);
            pat->notes.erase(m_id);
            m_project->notifyPatternEdited(pat->id);
        }
    });
    menu.addSeparator();
    menu.addAction(icons::icon("trash", theme::danger), tr("Delete channel"), this, [this] {
        const ObjId id = m_id;
        Project* p = m_project;
        QMetaObject::invokeMethod(p, [p, id] { p->removeChannel(id); }, Qt::QueuedConnection);
    });
    menu.exec(global);
}

void ChannelRow::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls() && nameRect().adjusted(-200, 0, 0, 0).contains(e->position()))
        e->acceptProposedAction();
    else
        e->ignore();
}

void ChannelRow::dropEvent(QDropEvent* e)
{
    Channel* c = channel();
    const auto urls = e->mimeData()->urls();
    if (!c || urls.isEmpty())
        return;
    const QString path = urls.first().toLocalFile();
    if (!SampleCache::isAudioFile(path))
        return;
    if (SamplePtr s = SampleCache::instance().load(path)) {
        if (c->kind == ChannelKind::Sampler || c->kind == ChannelKind::Audio) {
            c->sample = s;
            c->name = s->name;
            m_project->notifyChannels();
        }
        e->acceptProposedAction();
    }
}

// ---------------------------------------------------------------- rack

ChannelRack::ChannelRack(Project* project, AudioEngine* engine, QWidget* parent)
    : QWidget(parent), m_project(project), m_engine(engine)
{
    setAcceptDrops(true);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto* header = new QWidget(this);
    header->setFixedHeight(46);
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(10, 4, 10, 4);
    hl->setSpacing(10);
    m_swing = new Knob(header, 26);
    m_swing->setParam(project->swing);
    m_swing->setColor(theme::accent2);
    m_swing->setToolTip(tr("Swing"));
    connect(m_swing, &Knob::edited, project, [project] { project->notifyPatternEdited(project->currentPatternId()); });
    auto* swingLabel = new QLabel(tr("SWING"), header);
    swingLabel->setStyleSheet(QStringLiteral("color:%1; font-size:7.5pt; font-weight:700;").arg(theme::textMute.name()));
    hl->addWidget(m_swing);
    hl->addWidget(swingLabel);
    hl->addSpacing(8);
    auto* stepsLabel = new QLabel(tr("STEPS"), header);
    stepsLabel->setStyleSheet(swingLabel->styleSheet());
    m_steps = new QComboBox(header);
    for (int s : {8, 12, 16, 24, 32, 48, 64})
        m_steps->addItem(QString::number(s), s);
    connect(m_steps, &QComboBox::activated, this, [this](int idx) {
        Pattern* p = m_project->currentPattern();
        if (!p)
            return;
        p->steps = m_steps->itemData(idx).toInt();
        for (auto& [ch, v] : p->stepData)
            v.resize(size_t(p->steps), 0);
        m_project->notifyPatternEdited(p->id);
    });
    hl->addWidget(stepsLabel);
    hl->addWidget(m_steps);
    hl->addStretch(1);
    auto* add = new QToolButton(header);
    add->setIcon(icons::icon("plus", theme::accent));
    add->setText(tr("Add"));
    add->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    add->setToolTip(tr("Add a channel"));
    connect(add, &QToolButton::clicked, this, &ChannelRack::showAddMenu);
    hl->addWidget(add);
    lay->addWidget(header);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_body = new QWidget(m_scroll);
    m_body->setAutoFillBackground(true);
    QPalette pal = m_body->palette();
    pal.setColor(QPalette::Window, theme::bg1);
    m_body->setPalette(pal);
    m_rows = new QVBoxLayout(m_body);
    m_rows->setContentsMargins(4, 4, 4, 4);
    m_rows->setSpacing(1);
    m_empty = new QWidget(m_body);
    {
        auto* el = new QVBoxLayout(m_empty);
        el->setContentsMargins(24, 40, 24, 24);
        el->setSpacing(10);
        auto* icon = new QLabel(m_empty);
        icon->setPixmap(icons::icon("rack", theme::accent, 40).pixmap(40, 40));
        icon->setAlignment(Qt::AlignCenter);
        auto* title = new QLabel(tr("Your rack is empty"), m_empty);
        title->setAlignment(Qt::AlignCenter);
        title->setFont(theme::uiFont(12, QFont::DemiBold));
        auto* sub = new QLabel(tr("Add a channel to start making a beat — or drag sounds in from the browser."), m_empty);
        sub->setAlignment(Qt::AlignCenter);
        sub->setWordWrap(true);
        sub->setStyleSheet(QStringLiteral("color:%1;").arg(theme::textDim.name()));
        auto* btns = new QHBoxLayout;
        btns->addStretch(1);
        auto* sampler = new QPushButton(icons::icon("wave", theme::text), tr("Sampler"), m_empty);
        sampler->setDefault(true);
        auto* pads = new QPushButton(icons::icon("pads", theme::text), tr("Pad bank"), m_empty);
        auto* inst = new QPushButton(icons::icon("instrument", theme::text), tr("Instrument"), m_empty);
        btns->addWidget(sampler);
        btns->addWidget(pads);
        btns->addWidget(inst);
        btns->addStretch(1);
        el->addWidget(icon);
        el->addWidget(title);
        el->addWidget(sub);
        el->addSpacing(6);
        el->addLayout(btns);
        connect(sampler, &QPushButton::clicked, this, [this] {
            const QStringList files = QFileDialog::getOpenFileNames(this, tr("Load samples"), DefaultKit::packsDir(),
                                                                    tr("Audio files (%1)").arg(SampleCache::nameFilters().join(' ')));
            for (const QString& f : files)
                if (SamplePtr smp = SampleCache::instance().load(f))
                    m_project->setSelectedChannel(m_project->addSampler(smp)->id);
        });
        connect(pads, &QPushButton::clicked, this, [this] {
            Channel* c = m_project->addPads();
            m_project->setSelectedChannel(c->id);
            emit UiHub::instance()->padsRequested(c->id);
        });
        connect(inst, &QPushButton::clicked, this, &ChannelRack::showAddMenu);
    }
    m_rows->addWidget(m_empty);
    m_rows->addStretch(1);
    m_scroll->setWidget(m_body);
    lay->addWidget(m_scroll, 1);

    connect(project, &Project::channelsChanged, this, &ChannelRack::rebuild);
    connect(project, &Project::projectReset, this, &ChannelRack::rebuild);
    connect(project, &Project::patternEdited, this, &ChannelRack::refreshRows);
    connect(project, &Project::currentPatternChanged, this, &ChannelRack::refreshRows);
    connect(project, &Project::selectedChannelChanged, this, &ChannelRack::refreshRows);
    rebuild();
}

void ChannelRack::syncSteps()
{
    if (Pattern* p = m_project->currentPattern()) {
        const int idx = m_steps->findData(p->steps);
        if (idx >= 0)
            m_steps->setCurrentIndex(idx);
        else {
            m_steps->addItem(QString::number(p->steps), p->steps);
            m_steps->setCurrentIndex(m_steps->count() - 1);
        }
    }
}

void ChannelRack::rebuild()
{
    for (auto* r : m_rowWidgets)
        r->deleteLater();
    m_rowWidgets.clear();
    int i = 0;
    for (const auto& c : m_project->channels) {
        auto* row = new ChannelRow(m_project, m_engine, c->id, m_body);
        m_rows->insertWidget(i++, row);
        m_rowWidgets.push_back(row);
    }
    m_empty->setVisible(m_project->channels.empty());
    syncSteps();
}

void ChannelRack::refreshRows()
{
    for (auto* r : m_rowWidgets)
        r->refresh();
    syncSteps();
}

void ChannelRack::showAddMenu()
{
    QMenu menu(this);
    menu.addAction(icons::icon("wave"), tr("Sampler (load sample…)"), this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Load samples"), QString(),
                                                                tr("Audio files (%1)").arg(SampleCache::nameFilters().join(' ')));
        for (const QString& f : files)
            if (SamplePtr s = SampleCache::instance().load(f))
                m_project->addSampler(s);
    });
    menu.addAction(icons::icon("pads"), tr("Pad bank"), this, [this] {
        Channel* c = m_project->addPads();
        m_project->setSelectedChannel(c->id);
        emit UiHub::instance()->padsRequested(c->id);
    });
    const auto& plugins = PluginManager::instance().plugins();
    QMenu* inst = menu.addMenu(icons::icon("instrument"), tr("Instrument plugin"));
    int n = 0;
    for (const auto& d : plugins) {
        if (!d.instrument)
            continue;
        ++n;
        inst->addAction(QStringLiteral("%1  ·  %2").arg(d.name, formatName(d.format)), this,
                        [this, d] { addPluginChannel(m_project, d, this); });
    }
    if (n == 0)
        inst->addAction(tr("No instrument plugins found"))->setEnabled(false);
    menu.exec(QCursor::pos());
}

void ChannelRack::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls() || e->mimeData()->hasFormat(kPluginMime))
        e->acceptProposedAction();
}

void ChannelRack::dropEvent(QDropEvent* e)
{
    if (const PluginDescriptor* d = pluginFromMime(e->mimeData())) {
        if (d->instrument)
            addPluginChannel(m_project, *d, this);
        e->acceptProposedAction();
        return;
    }
    for (const QUrl& u : e->mimeData()->urls()) {
        const QString path = u.toLocalFile();
        if (!SampleCache::isAudioFile(path))
            continue;
        if (SamplePtr s = SampleCache::instance().load(path))
            m_project->setSelectedChannel(m_project->addSampler(s)->id);
    }
    e->acceptProposedAction();
}

} // namespace guiro
