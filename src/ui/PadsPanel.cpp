#include "ui/PadsPanel.h"
#include "core/Project.h"
#include "engine/AudioEngine.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/UiHub.h"
#include "ui/widgets/Knob.h"
#include "ui/widgets/Waveform.h"

#include <QComboBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>

namespace guiro {

namespace {
// MPC-style layout: the bottom row holds pads 1-4.
const Qt::Key kPadKeys[kNumPads] = {Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_V, Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_F,
                                    Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R, Qt::Key_1, Qt::Key_2, Qt::Key_3, Qt::Key_4};
const char* kPadKeyNames[kNumPads] = {"Z", "X", "C", "V", "A", "S", "D", "F", "Q", "W", "E", "R", "1", "2", "3", "4"};
} // namespace

// ================================================================ pad

PadWidget::PadWidget(int index, QWidget* parent) : QWidget(parent), m_index(index)
{
    setMinimumSize(72, 64);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAcceptDrops(true);
    setCursor(Qt::PointingHandCursor);
    connect(UiHub::instance(), &UiHub::frame, this, [this] {
        if (m_glow > 0.001f) {
            m_glow *= 0.88f;
            if (m_glow < 0.01f)
                m_glow = 0.f;
            update();
        }
    });
}

void PadWidget::setInfo(const QString& name, const QColor& color, bool loaded, const QString& key)
{
    m_name = name;
    m_color = color;
    m_loaded = loaded;
    m_key = key;
    update();
}

void PadWidget::setSelected(bool s)
{
    m_selected = s;
    update();
}

void PadWidget::flash(float v)
{
    m_glow = std::max(m_glow, 0.35f + 0.65f * v);
    update();
}

void PadWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(3, 3, -3, -3);

    // Flat tile; a hit fills it with the pad colour and fades back.
    QColor base = m_loaded ? theme::bg3 : theme::bg2;
    if (m_glow > 0.f)
        base = theme::mix(base, m_color, 0.75 * m_glow);
    p.setBrush(base);
    p.setPen(m_dropHover || m_selected ? QPen(m_dropHover ? theme::accent : theme::textMute, 1.5) : Qt::NoPen);
    p.drawRoundedRect(r, 6, 6);

    // Colour tag
    p.setPen(Qt::NoPen);
    p.setBrush(m_loaded ? m_color : theme::bg4);
    p.drawRoundedRect(QRectF(r.left() + 10, r.top() + 10, 18, 3), 1.5, 1.5);

    p.setPen(m_loaded ? theme::text : theme::textMute);
    p.setFont(theme::uiFont(8.5, QFont::DemiBold));
    const QRectF textR = r.adjusted(10, 14, -10, -18);
    p.drawText(textR, Qt::AlignCenter | Qt::TextWordWrap, m_loaded ? m_name : tr("Drop sample"));
    p.setFont(theme::monoFont(7.5));
    p.setPen(theme::textMute);
    p.drawText(r.adjusted(10, 0, -10, -6), Qt::AlignBottom | Qt::AlignLeft, QString::number(m_index + 1));
    p.drawText(r.adjusted(10, 0, -10, -6), Qt::AlignBottom | Qt::AlignRight, m_key);
}

void PadWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_down = true;
    m_pressPos = e->position();
    // Strike harder towards the top of the pad.
    const float v = float(std::clamp(1.0 - e->position().y() / std::max(1, height()) * 0.7, 0.25, 1.0));
    emit pressed(m_index, v);
}

void PadWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (m_down && m_loaded && (e->position() - m_pressPos).manhattanLength() > 14) {
        m_down = false;
        emit released(m_index);
        emit dragOut(m_index);
    }
}

void PadWidget::mouseReleaseEvent(QMouseEvent*)
{
    if (m_down)
        emit released(m_index);
    m_down = false;
}

void PadWidget::contextMenuEvent(QContextMenuEvent* e)
{
    emit menuRequested(m_index, e->globalPos());
}

void PadWidget::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls()) {
        m_dropHover = true;
        update();
        e->acceptProposedAction();
    }
}

void PadWidget::dragLeaveEvent(QDragLeaveEvent*)
{
    m_dropHover = false;
    update();
}

void PadWidget::dropEvent(QDropEvent* e)
{
    m_dropHover = false;
    QStringList files;
    for (const QUrl& u : e->mimeData()->urls())
        if (SampleCache::isAudioFile(u.toLocalFile()))
            files << u.toLocalFile();
    if (!files.isEmpty())
        emit filesDropped(m_index, files);
    update();
    e->acceptProposedAction();
}

// ================================================================ panel

PadsPanel::PadsPanel(Project* project, AudioEngine* engine, QWidget* parent)
    : QWidget(parent), m_project(project), m_engine(engine)
{
    setFocusPolicy(Qt::StrongFocus);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 8, 10, 10);
    lay->setSpacing(8);

    auto* top = new QHBoxLayout;
    m_banks = new QComboBox(this);
    m_banks->setMinimumWidth(140);
    auto* add = new QPushButton(icons::icon("plus"), tr("New bank"), this);
    connect(add, &QPushButton::clicked, this, [this] {
        Channel* c = m_project->addPads();
        showChannel(c->id);
    });
    auto* hint = new QLabel(tr("Keys: 1-4 · Q-R · A-F · Z-V   ·   Record arms note capture"), this);
    hint->setStyleSheet(QStringLiteral("color:%1;font-size:8pt;").arg(theme::textMute.name()));
    top->addWidget(m_banks);
    top->addWidget(add);
    top->addSpacing(10);
    top->addWidget(hint, 1);
    lay->addLayout(top);

    auto* grid = new QGridLayout;
    grid->setSpacing(2);
    for (int i = 0; i < kNumPads; ++i) {
        auto* pad = new PadWidget(i, this);
        m_pads[size_t(i)] = pad;
        const int row = 3 - i / 4;
        const int col = i % 4;
        grid->addWidget(pad, row, col);
        connect(pad, &PadWidget::pressed, this, [this](int idx, float v) {
            setFocus();
            selectPad(idx);
            hit(idx, v);
        });
        connect(pad, &PadWidget::released, this, &PadsPanel::release);
        connect(pad, &PadWidget::filesDropped, this, &PadsPanel::loadFiles);
        connect(pad, &PadWidget::menuRequested, this, &PadsPanel::padMenu);
        connect(pad, &PadWidget::dragOut, this, [this](int idx) {
            Channel* c = channel();
            if (!c || !c->pads[size_t(idx)].sample)
                return;
            auto* drag = new QDrag(this);
            auto* mime = new QMimeData;
            mime->setUrls({QUrl::fromLocalFile(c->pads[size_t(idx)].sample->path)});
            drag->setMimeData(mime);
            drag->exec(Qt::CopyAction);
        });
    }
    lay->addLayout(grid, 1);

    // Selected pad editor
    auto* editor = new QWidget(this);
    editor->setFixedHeight(96);
    auto* el = new QHBoxLayout(editor);
    el->setContentsMargins(0, 0, 0, 0);
    el->setSpacing(10);
    auto* left = new QVBoxLayout;
    m_padTitle = new QLabel(editor);
    m_padTitle->setFont(theme::uiFont(9, QFont::DemiBold));
    left->addWidget(m_padTitle);
    m_wave = new Waveform(editor);
    left->addWidget(m_wave, 1);
    el->addLayout(left, 1);
    m_gain = new Knob(editor, 34);
    m_gain->setLabel(tr("LEVEL"));
    m_pitch = new Knob(editor, 34);
    m_pitch->setLabel(tr("PITCH"));
    m_pitch->setColor(theme::accent2);
    el->addWidget(m_gain);
    el->addWidget(m_pitch);
    lay->addWidget(editor);
    connect(m_wave, &Waveform::fileDropped, this, [this](const QString& f) { loadFiles(m_selectedPad, {f}); });

    connect(m_banks, &QComboBox::activated, this, [this](int i) { showChannel(m_banks->itemData(i).toUInt()); });
    connect(project, &Project::channelsChanged, this, [this] {
        rebuildBanks();
        refresh();
    });
    connect(project, &Project::projectReset, this, [this] {
        m_channel = 0;
        rebuildBanks();
        refresh();
    });
    connect(UiHub::instance(), &UiHub::padsRequested, this, &PadsPanel::showChannel);
    rebuildBanks();
    refresh();
}

Channel* PadsPanel::channel() const
{
    Channel* c = m_project->channel(m_channel);
    return c && c->kind == ChannelKind::Pads ? c : nullptr;
}

void PadsPanel::rebuildBanks()
{
    QSignalBlocker b(m_banks);
    m_banks->clear();
    for (const auto& c : m_project->channels)
        if (c->kind == ChannelKind::Pads)
            m_banks->addItem(icons::icon("pads", c->color), c->name, c->id);
    if (!channel() && m_banks->count() > 0)
        m_channel = m_banks->itemData(0).toUInt();
    const int idx = m_banks->findData(m_channel);
    if (idx >= 0)
        m_banks->setCurrentIndex(idx);
}

void PadsPanel::showChannel(ObjId id)
{
    m_channel = id;
    rebuildBanks();
    refresh();
}

void PadsPanel::refresh()
{
    Channel* c = channel();
    for (int i = 0; i < kNumPads; ++i) {
        const bool loaded = c && c->pads[size_t(i)].sample;
        m_pads[size_t(i)]->setInfo(loaded ? c->pads[size_t(i)].sample->name : QString(),
                                   c ? c->pads[size_t(i)].color : theme::textMute, loaded,
                                   QString::fromLatin1(kPadKeyNames[i]));
        m_pads[size_t(i)]->setEnabled(c != nullptr);
    }
    selectPad(m_selectedPad);
}

void PadsPanel::selectPad(int pad)
{
    m_selectedPad = pad;
    for (int i = 0; i < kNumPads; ++i)
        m_pads[size_t(i)]->setSelected(i == pad);
    Channel* c = channel();
    if (!c) {
        m_padTitle->setText(tr("No pad bank — create one with “New bank”"));
        m_wave->setSample(nullptr);
        m_gain->setParam(nullptr);
        m_pitch->setParam(nullptr);
        return;
    }
    auto& slot = c->pads[size_t(pad)];
    m_padTitle->setText(tr("Pad %1  ·  %2").arg(pad + 1).arg(slot.sample ? slot.sample->name : tr("empty")));
    m_wave->setSample(slot.sample);
    m_wave->setColor(slot.color);
    m_gain->setParam(slot.gain);
    m_pitch->setParam(slot.pitch);
}

void PadsPanel::hit(int pad, float velocity)
{
    Channel* c = channel();
    if (!c)
        return;
    m_engine->noteOn(c->id, kPadBaseKey + pad, velocity);
    m_pads[size_t(pad)]->flash(velocity);
    recordHit(pad, velocity);
}

void PadsPanel::release(int pad)
{
    if (Channel* c = channel())
        m_engine->noteOff(c->id, kPadBaseKey + pad);
}

void PadsPanel::recordHit(int pad, float velocity)
{
    Channel* c = channel();
    Pattern* pat = m_project->currentPattern();
    if (!c || !pat || !m_engine->recording() || !m_engine->isPlaying())
        return;
    double pos = m_engine->position();
    if (m_engine->songMode()) {
        // Record relative to the clip of the current pattern under the playhead.
        bool found = false;
        for (const Clip& clip : m_project->clips) {
            if (clip.kind == ClipKind::Pattern && clip.ref == pat->id && pos >= double(clip.start) && pos < double(clip.end())) {
                pos = pos - double(clip.start) + double(clip.offset);
                found = true;
                break;
            }
        }
        if (!found)
            return;
    }
    const Tick len = pat->length();
    Tick t = Tick(std::llround(pos / double(kStepTicks))) * kStepTicks;
    t %= std::max<Tick>(len, 1);
    auto& notes = pat->notes[c->id];
    for (const Note& n : notes)
        if (n.key == kPadBaseKey + pad && n.start == t)
            return;
    notes.push_back({kPadBaseKey + pad, t, kStepTicks, velocity});
    m_project->notifyPatternEdited(pat->id);
}

void PadsPanel::loadFiles(int pad, const QStringList& files)
{
    Channel* c = channel();
    if (!c) {
        c = m_project->addPads();
        m_channel = c->id;
    }
    int i = pad;
    for (const QString& f : files) {
        if (i >= kNumPads)
            break;
        if (SamplePtr s = SampleCache::instance().load(f))
            c->pads[size_t(i++)].sample = s;
    }
    m_project->notifyChannels();
    selectPad(pad);
}

void PadsPanel::padMenu(int pad, const QPoint& global)
{
    Channel* c = channel();
    if (!c)
        return;
    selectPad(pad);
    QMenu menu(this);
    menu.addSection(tr("Pad %1").arg(pad + 1));
    menu.addAction(icons::icon("open"), tr("Load sample…"), this, [this, pad] {
        const QStringList f = QFileDialog::getOpenFileNames(this, tr("Load samples"), QString(),
                                                            tr("Audio files (%1)").arg(SampleCache::nameFilters().join(' ')));
        if (!f.isEmpty())
            loadFiles(pad, f);
    });
    if (c->pads[size_t(pad)].sample) {
        menu.addAction(icons::icon("rack"), tr("Send to channel rack"), this, [this, c, pad] {
            m_project->addSampler(c->pads[size_t(pad)].sample);
        });
        menu.addAction(icons::icon("trash", theme::danger), tr("Clear pad"), this, [this, c, pad] {
            c->pads[size_t(pad)].sample.reset();
            m_project->notifyChannels();
        });
    }
    menu.exec(global);
}

void PadsPanel::keyPressEvent(QKeyEvent* e)
{
    if (e->isAutoRepeat() || e->modifiers() & (Qt::ControlModifier | Qt::AltModifier))
        return QWidget::keyPressEvent(e);
    for (int i = 0; i < kNumPads; ++i)
        if (e->key() == kPadKeys[i]) {
            selectPad(i);
            hit(i, 0.85f);
            return;
        }
    QWidget::keyPressEvent(e);
}

void PadsPanel::keyReleaseEvent(QKeyEvent* e)
{
    if (e->isAutoRepeat())
        return;
    for (int i = 0; i < kNumPads; ++i)
        if (e->key() == kPadKeys[i]) {
            release(i);
            return;
        }
    QWidget::keyReleaseEvent(e);
}

} // namespace guiro
