#include "ui/TopBar.h"
#include "core/Project.h"
#include "engine/AudioEngine.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/UiHub.h"
#include "ui/widgets/Knob.h"
#include "ui/widgets/Scope.h"
#include <QVBoxLayout>

#include <QAction>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QToolButton>
#include <cmath>

namespace guiro {

// ---------------------------------------------------------------- pieces

// Rounded container that groups related controls.
class Capsule : public QWidget {
public:
    explicit Capsule(QWidget* parent) : QWidget(parent)
    {
        auto* l = new QHBoxLayout(this);
        l->setContentsMargins(5, 4, 5, 4);
        l->setSpacing(4);
    }
    QHBoxLayout* row() const { return static_cast<QHBoxLayout*>(layout()); }
    void addDivider()
    {
        auto* d = new QWidget(this);
        d->setFixedSize(1, 22);
        d->setStyleSheet(QStringLiteral("background:%1;").arg(theme::line.name()));
        row()->addSpacing(4);
        row()->addWidget(d);
        row()->addSpacing(4);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(theme::bg2);
        p.drawRoundedRect(QRectF(rect()), 8, 8);
    }
};

class TransportButton : public QWidget {
public:
    TransportButton(const QString& icon, const QColor& activeColor, int size, bool hero, QWidget* parent)
        : QWidget(parent), m_icon(icon), m_active(activeColor), m_hero(hero)
    {
        setFixedSize(size, size);
        setCursor(Qt::PointingHandCursor);
    }
    std::function<void()> onClick;
    void setOn(bool on)
    {
        if (on != m_on) {
            m_on = on;
            update();
        }
    }
    void setIconName(const QString& i)
    {
        m_icon = i;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        p.setPen(Qt::NoPen);
        if (m_hero) {
            p.setBrush(m_hover ? theme::accent.lighter(112) : theme::accent);
            p.drawRoundedRect(r, 6, 6);
            const qreal ins = r.width() * 0.30;
            icons::paint(p, m_icon, r.adjusted(ins, ins, -ins, -ins), Qt::white);
            return;
        }
        if (m_on)
            p.setBrush(theme::mix(theme::bg3, m_active, 0.22));
        else
            p.setBrush(m_hover ? theme::bg4 : Qt::transparent);
        p.drawRoundedRect(r, 6, 6);
        const qreal ins = r.width() * 0.28;
        icons::paint(p, m_icon, r.adjusted(ins, ins, -ins, -ins), m_on ? m_active : theme::textDim);
    }
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && onClick)
            onClick();
    }
    void enterEvent(QEnterEvent*) override
    {
        m_hover = true;
        update();
    }
    void leaveEvent(QEvent*) override
    {
        m_hover = false;
        update();
    }

private:
    QString m_icon;
    QColor m_active;
    bool m_hero;
    bool m_on = false;
    bool m_hover = false;
};

// PAT | SONG segmented switch
class ModeSwitch : public QWidget {
public:
    ModeSwitch(AudioEngine* e, QWidget* parent) : QWidget(parent), m_engine(e)
    {
        setFixedSize(100, 30);
        setCursor(Qt::PointingHandCursor);
        setToolTip(tr("Pattern / Song mode (L)"));
        connect(e, &AudioEngine::transportChanged, this, qOverload<>(&QWidget::update));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect());
        p.setPen(Qt::NoPen);
        p.setBrush(theme::bg3);
        p.drawRoundedRect(r, 6, 6);
        const bool song = m_engine->songMode();
        const QRectF seg = QRectF(song ? r.center().x() : r.left(), r.top(), r.width() / 2, r.height()).adjusted(2, 2, -2, -2);
        p.setBrush(theme::accent);
        p.drawRoundedRect(seg, 5, 5);
        p.setFont(theme::uiFont(7.8, QFont::DemiBold));
        p.setPen(song ? theme::textDim : QColor(Qt::white));
        p.drawText(QRectF(r.left(), r.top(), r.width() / 2, r.height()), Qt::AlignCenter, QStringLiteral("PAT"));
        p.setPen(song ? QColor(Qt::white) : theme::textDim);
        p.drawText(QRectF(r.center().x(), r.top(), r.width() / 2, r.height()), Qt::AlignCenter, QStringLiteral("SONG"));
    }
    void mousePressEvent(QMouseEvent* e) override { m_engine->setSongMode(e->position().x() > width() / 2.0); }

private:
    AudioEngine* m_engine;
};

// Draggable tempo display.
class TempoBox : public QWidget {
public:
    TempoBox(Project* p, QWidget* parent) : QWidget(parent), m_project(p)
    {
        setFixedSize(84, 40);
        setCursor(Qt::SizeVerCursor);
        setToolTip(tr("Tempo — drag or scroll to change, double-click to type"));
        connect(p, &Project::tempoChanged, this, qOverload<>(&QWidget::update));
        connect(p, &Project::projectReset, this, qOverload<>(&QWidget::update));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setPen(theme::text);
        p.setFont(theme::monoFont(14, QFont::DemiBold));
        p.drawText(QRectF(0, 0, width(), 26), Qt::AlignCenter, QString::number(m_project->bpm(), 'f', 2));
        p.setPen(theme::textMute);
        p.setFont(theme::uiFont(6.5, QFont::Bold));
        p.drawText(QRectF(0, 25, width(), 12), Qt::AlignCenter, QStringLiteral("BPM"));
    }
    void mousePressEvent(QMouseEvent* e) override
    {
        m_pressY = e->globalPosition().y();
        m_pressBpm = m_project->bpm();
        m_fine = e->position().x() > width() * 0.62;
    }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        const double dy = m_pressY - e->globalPosition().y();
        const double step = m_fine || (e->modifiers() & Qt::ShiftModifier) ? 0.01 : 0.25;
        double v = m_pressBpm + dy * step;
        if (!m_fine)
            v = std::round(v * 4.0) / 4.0;
        m_project->setBpm(v);
    }
    void wheelEvent(QWheelEvent* e) override
    {
        const double step = (e->modifiers() & Qt::ShiftModifier) ? 0.1 : 1.0;
        m_project->setBpm(m_project->bpm() + (e->angleDelta().y() > 0 ? step : -step));
    }
    void mouseDoubleClickEvent(QMouseEvent*) override
    {
        bool ok = false;
        const double v = QInputDialog::getDouble(this, tr("Tempo"), tr("Beats per minute"), m_project->bpm(), 10, 522, 3, &ok);
        if (ok)
            m_project->setBpm(v);
    }

private:
    Project* m_project;
    double m_pressY = 0, m_pressBpm = 0;
    bool m_fine = false;
};

class TimeBox : public QWidget {
public:
    TimeBox(AudioEngine* e, QWidget* parent) : QWidget(parent), m_engine(e)
    {
        setFixedSize(112, 40);
        connect(UiHub::instance(), &UiHub::frame, this, [this] {
            const double pos = m_engine->position();
            if (std::abs(pos - m_last) > 0.5) {
                m_last = pos;
                update();
            }
        });
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const int64_t t = int64_t(m_engine->position());
        const int bar = int(t / kBarTicks) + 1;
        const int beat = int((t % kBarTicks) / kBeatTicks) + 1;
        const int tick = int(t % kBeatTicks);
        p.setFont(theme::monoFont(14, QFont::DemiBold));
        p.setPen(theme::text);
        p.drawText(QRectF(0, 0, width(), 26), Qt::AlignCenter,
                   QStringLiteral("%1:%2:%3").arg(bar, 3, 10, QLatin1Char('0')).arg(beat).arg(tick, 2, 10, QLatin1Char('0')));
        p.setPen(theme::textMute);
        p.setFont(theme::uiFont(6.5, QFont::Bold));
        p.drawText(QRectF(0, 25, width(), 12), Qt::AlignCenter, QStringLiteral("BAR · BEAT · TICK"));
    }

private:
    AudioEngine* m_engine;
    double m_last = -1;
};

class CpuBox : public QWidget {
public:
    CpuBox(AudioEngine* e, QWidget* parent) : QWidget(parent), m_engine(e)
    {
        setFixedSize(58, 32);
        setToolTip(tr("Audio engine load"));
        connect(UiHub::instance(), &UiHub::frame, this, [this] {
            if (++m_div % 8 == 0)
                update();
        });
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const float load = std::clamp(m_engine->cpuLoad(), 0.f, 1.f);
        p.setPen(theme::textMute);
        p.setFont(theme::uiFont(6.5, QFont::Bold));
        p.drawText(QRectF(4, 2, width() - 8, 12), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("CPU"));
        p.setPen(load > 0.8f ? theme::danger : theme::textDim);
        p.setFont(theme::monoFont(8));
        p.drawText(QRectF(4, 2, width() - 8, 12), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("%1%").arg(int(load * 100)));
        const QRectF bar(4, 19, width() - 8, 5);
        p.setPen(Qt::NoPen);
        p.setBrush(theme::bg3);
        p.drawRoundedRect(bar, 2.5, 2.5);
        p.setBrush(load > 0.8f ? QBrush(theme::danger) : QBrush(theme::accentGradient(bar)));
        p.drawRoundedRect(QRectF(bar.left(), bar.top(), std::max(5.0, bar.width() * load), bar.height()), 2.5, 2.5);
    }

private:
    AudioEngine* m_engine;
    int m_div = 0;
};

class Logo : public QWidget {
public:
    explicit Logo(QWidget* parent) : QWidget(parent) { setFixedSize(100, 40); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF mark(2, 6, 28, 28);
        p.setPen(Qt::NoPen);
        p.setBrush(theme::accent);
        p.drawRoundedRect(mark, 7, 7);
        p.setPen(QPen(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap));
        for (int i = 0; i < 4; ++i) {
            const qreal x = mark.left() + 7.5 + i * 4.3;
            const qreal inset = (i == 0 || i == 3) ? 10 : 7.5;
            p.drawLine(QPointF(x, mark.top() + inset), QPointF(x, mark.bottom() - inset));
        }
        p.setPen(theme::text);
        p.setFont(theme::uiFont(13, QFont::DemiBold));
        p.drawText(QRectF(38, 0, width() - 40, height()), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("guiro"));
    }
};

// ---------------------------------------------------------------- TopBar

TopBar::TopBar(Project* project, AudioEngine* engine, QWidget* parent)
    : QWidget(parent), m_project(project), m_engine(engine)
{
    setFixedHeight(58);
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(8, 4, 10, 4);
    lay->setSpacing(8);

    // ---- menus + hint bar (top-left, like FL)
    auto* left = new QVBoxLayout;
    left->setSpacing(0);
    left->setContentsMargins(0, 0, 0, 0);
    m_menuBar = new QMenuBar(this);
    m_menuBar->setNativeMenuBar(false);
    m_menuBar->setStyleSheet(QStringLiteral(
        "QMenuBar{background:transparent;font-size:8pt;font-weight:600;}"
        "QMenuBar::item{padding:3px 5px;border-radius:4px;color:%1;}"
        "QMenuBar::item:selected{background:%2;color:%3;}").arg(theme::textDim.name(), theme::bg3.name(), theme::text.name()));
    m_hint = new QLabel(this);
    m_hint->setFixedHeight(20);
    m_hint->setMinimumWidth(240);
    m_hint->setMaximumWidth(420);
    m_hint->setStyleSheet(QStringLiteral("QLabel{background:%1;color:%2;border-radius:4px;padding:0 8px;font-size:8pt;}")
                              .arg(theme::bg0.name(), theme::textDim.name()));
    left->addWidget(m_menuBar);
    left->addWidget(m_hint);
    lay->addLayout(left);

    // ---- master volume
    m_master = new Knob(this, 26);
    m_master->setLabel(tr("MASTER"));
    lay->addWidget(m_master);

    // ---- transport
    auto* transport = new Capsule(this);
    transport->row()->addWidget(new ModeSwitch(engine, transport));
    transport->row()->addSpacing(4);
    m_play = new TransportButton(QStringLiteral("play"), theme::good, 34, true, transport);
    m_play->setToolTip(tr("Play / stop (Space)"));
    m_play->onClick = [engine] { engine->togglePlay(); };
    auto* stop = new TransportButton(QStringLiteral("stop"), theme::text, 32, false, transport);
    stop->setToolTip(tr("Stop and return to the start"));
    stop->onClick = [engine] { engine->stop(); };
    m_rec = new TransportButton(QStringLiteral("record"), theme::danger, 32, false, transport);
    m_rec->setToolTip(tr("Record — capture pads, MIDI and keyboard notes into the current pattern (R)"));
    m_rec->onClick = [engine] { engine->setRecording(!engine->recording()); };
    transport->row()->addWidget(m_play);
    transport->row()->addWidget(stop);
    transport->row()->addWidget(m_rec);
    transport->addDivider();
    auto* tempo = new TempoBox(project, transport);
    transport->row()->addWidget(tempo);
    transport->addDivider();
    m_metro = new TransportButton(QStringLiteral("metronome"), theme::accent, 32, false, transport);
    m_metro->setToolTip(tr("Metronome (Ctrl+M)"));
    m_metro->onClick = [engine] { engine->setMetronome(!engine->metronome()); };
    transport->row()->addWidget(m_metro);
    transport->addDivider();
    transport->row()->addWidget(new TimeBox(engine, transport));
    transport->row()->addSpacing(6);
    lay->addWidget(transport);

    // ---- pattern selector
    auto* pat = new Capsule(this);
    auto mkRound = [pat](const QString& icon, const QString& tip) {
        auto* b = new QToolButton(pat);
        b->setIcon(icons::icon(icon, theme::textDim, 16));
        b->setToolTip(tip);
        b->setFixedSize(28, 30);
        b->setStyleSheet(QStringLiteral("QToolButton{padding:0;border:none;background:transparent;border-radius:6px;min-height:0;}"
                                        "QToolButton:hover{background:%1;}").arg(theme::bg4.name()));
        return b;
    };
    auto* prev = mkRound("chevron-left", tr("Previous pattern"));
    connect(prev, &QToolButton::clicked, this, [this] { stepPattern(-1); });
    m_patternButton = new QPushButton(pat);
    m_patternButton->setMinimumWidth(110);
    m_patternButton->setFixedHeight(30);
    m_patternButton->setToolTip(tr("Pattern selector — click to pick, rename or create patterns"));
    m_patternButton->setStyleSheet(QStringLiteral("QPushButton{border:none;background:%1;border-radius:6px;font-weight:600;text-align:left;padding-left:10px;}"
                                                  "QPushButton:hover{background:%2;}").arg(theme::bg3.name(), theme::bg4.name()));
    connect(m_patternButton, &QPushButton::clicked, this, &TopBar::showPatternMenu);
    auto* next = mkRound("chevron-right", tr("Next pattern"));
    connect(next, &QToolButton::clicked, this, [this] { stepPattern(1); });
    auto* add = mkRound("plus", tr("New pattern (F4)"));
    connect(add, &QToolButton::clicked, this, [this] { m_project->setCurrentPattern(m_project->addPattern()->id); });
    pat->row()->addWidget(prev);
    pat->row()->addWidget(m_patternButton);
    pat->row()->addWidget(next);
    pat->row()->addWidget(add);
    lay->addWidget(pat);

    lay->addStretch(1);
    auto* scope = new Scope(this);
    scope->setToolTip(tr("Master output"));
    lay->addWidget(scope);
    auto* cpu = new CpuBox(engine, this);
    cpu->setToolTip(tr("Audio engine load — %1 at %2 Hz").arg(engine->backendName()).arg(int(engine->sampleRate())));
    lay->addWidget(cpu);

    // ---- window buttons
    auto* toggles = new Capsule(this);
    m_toggles = toggles->row();
    lay->addWidget(toggles);

    connect(engine, &AudioEngine::transportChanged, this, [this] {
        m_play->setOn(m_engine->isPlaying());
        m_play->setIconName(m_engine->isPlaying() ? QStringLiteral("stop") : QStringLiteral("play"));
        m_rec->setOn(m_engine->recording());
        m_metro->setOn(m_engine->metronome());
    });
    connect(project, &Project::currentPatternChanged, this, &TopBar::refreshPattern);
    connect(project, &Project::patternsChanged, this, &TopBar::refreshPattern);
    connect(project, &Project::projectReset, this, &TopBar::refreshPattern);
    connect(project, &Project::projectReset, this, &TopBar::bindMaster);
    refreshPattern();
    bindMaster();
    setHint(tr("Welcome to Guiro — hover over anything to see what it does here."));
}

void TopBar::bindMaster()
{
    if (MixerInsert* m = m_project->insert(0))
        m_master->setParam(m->volume);
    m_master->setToolTip(tr("Master volume"));
}

void TopBar::setHint(const QString& text)
{
    m_hint->setText(m_hint->fontMetrics().elidedText(text, Qt::ElideRight, m_hint->width() - 16));
}

void TopBar::addPanelToggle(QAction* action, const QString& icon)
{
    auto* b = new QToolButton(this);
    QIcon ic;
    ic.addPixmap(icons::icon(icon, theme::textMute, 20).pixmap(20, 20, QIcon::Normal, QIcon::Off), QIcon::Normal, QIcon::Off);
    ic.addPixmap(icons::icon(icon, theme::accent, 20).pixmap(20, 20), QIcon::Normal, QIcon::On);
    action->setIcon(ic);
    b->setDefaultAction(action);
    b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    b->setFixedSize(32, 32);
    b->setIconSize(QSize(18, 18));
    b->setStyleSheet(QStringLiteral("QToolButton{padding:0;border:none;background:transparent;border-radius:6px;}"
                                    "QToolButton:hover{background:%1;}"
                                    "QToolButton:checked{background:%2;}")
                         .arg(theme::bg4.name(), theme::mix(theme::bg3, theme::accent, 0.22).name()));
    m_toggles->addWidget(b);
}

void TopBar::refreshPattern()
{
    Pattern* p = m_project->currentPattern();
    const QString name = p ? p->name : tr("No pattern");
    const QColor c = p ? p->color : theme::textMute;
    QPixmap dot(24, 24);
    dot.setDevicePixelRatio(2);
    dot.fill(Qt::transparent);
    QPainter dp(&dot);
    dp.setRenderHint(QPainter::Antialiasing);
    dp.setPen(Qt::NoPen);
    dp.setBrush(c);
    dp.drawEllipse(QRectF(2, 2, 8, 8));
    dp.end();
    m_patternButton->setIcon(QIcon(dot));
    m_patternButton->setText(name);
}

void TopBar::stepPattern(int delta)
{
    auto& pats = m_project->patterns;
    if (pats.empty())
        return;
    int idx = 0;
    for (size_t i = 0; i < pats.size(); ++i)
        if (pats[i]->id == m_project->currentPatternId())
            idx = int(i);
    idx = (idx + delta + int(pats.size())) % int(pats.size());
    m_project->setCurrentPattern(pats[size_t(idx)]->id);
}

void TopBar::showPatternMenu()
{
    QMenu menu(this);
    menu.addSection(tr("Patterns"));
    for (const auto& p : m_project->patterns) {
        QAction* a = menu.addAction(p->name);
        a->setCheckable(true);
        a->setChecked(p->id == m_project->currentPatternId());
        const ObjId id = p->id;
        connect(a, &QAction::triggered, this, [this, id] { m_project->setCurrentPattern(id); });
    }
    menu.addSeparator();
    menu.addAction(icons::icon("plus"), tr("New pattern"), this, [this] { m_project->setCurrentPattern(m_project->addPattern()->id); });
    menu.addAction(tr("Clone pattern"), this, [this] {
        if (ObjId id = m_project->clonePattern(m_project->currentPatternId()))
            m_project->setCurrentPattern(id);
    });
    menu.addAction(icons::icon("edit"), tr("Rename pattern…"), this, [this] {
        Pattern* p = m_project->currentPattern();
        if (!p)
            return;
        bool ok = false;
        const QString n = QInputDialog::getText(this, tr("Rename pattern"), tr("Name"), QLineEdit::Normal, p->name, &ok);
        if (ok && !n.isEmpty()) {
            p->name = n;
            m_project->notifyPatterns();
        }
    });
    menu.addAction(icons::icon("trash", theme::danger), tr("Delete pattern"), this,
                   [this] { m_project->removePattern(m_project->currentPatternId()); });
    menu.exec(m_patternButton->mapToGlobal(QPoint(0, m_patternButton->height() + 8)));
}

void TopBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), theme::bg1);
    p.setPen(theme::lineSoft);
    p.drawLine(0, height() - 1, width(), height() - 1);
}

} // namespace guiro
