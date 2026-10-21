#include "ui/PianoRoll.h"
#include "engine/AudioEngine.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/UiHub.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace guiro {

QString noteName(int key)
{
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return QString::fromLatin1(names[key % 12]) + QString::number(key / 12);
}

static bool isBlack(int key)
{
    const int k = key % 12;
    return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
}

PianoRollView::PianoRollView(Project* project, AudioEngine* engine, QWidget* parent)
    : QAbstractScrollArea(parent), m_project(project), m_engine(engine)
{
    setFrameShape(QFrame::NoFrame);
    viewport()->setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    connect(project, &Project::patternEdited, this, [this] {
        syncSelection();
        updateScrollbars();
        viewport()->update();
    });
    connect(project, &Project::currentPatternChanged, this, [this] {
        m_sel.clear();
        syncSelection();
        updateScrollbars();
        viewport()->update();
    });
    connect(project, &Project::channelsChanged, this, [this] {
        if (!m_project->channel(m_channel))
            m_channel = 0;
        viewport()->update();
    });
    connect(UiHub::instance(), &UiHub::frame, this, [this] {
        if (isVisible() && m_engine->isPlaying())
            viewport()->update();
    });

}

void PianoRollView::setChannel(ObjId id)
{
    m_channel = id;
    m_sel.clear();
    syncSelection();
    viewport()->update();
}

std::vector<Note>* PianoRollView::notes() const
{
    Pattern* p = pattern();
    if (!p || !m_channel || !m_project->channel(m_channel))
        return nullptr;
    return &p->notes[m_channel];
}

void PianoRollView::syncSelection()
{
    auto* n = notes();
    m_sel.resize(n ? n->size() : 0, 0);
}

QRectF PianoRollView::gridRect() const
{
    const QRect v = viewport()->rect();
    return QRectF(kKeyW, kRulerH, v.width() - kKeyW, v.height() - kRulerH - kVelH);
}

QRectF PianoRollView::velRect() const
{
    const QRect v = viewport()->rect();
    return QRectF(kKeyW, v.height() - kVelH, v.width() - kKeyW, kVelH);
}

qreal PianoRollView::tickToX(double t) const
{
    return kKeyW + t * m_pxPerTick - horizontalScrollBar()->value();
}

double PianoRollView::xToTick(qreal x) const
{
    return (x - kKeyW + horizontalScrollBar()->value()) / m_pxPerTick;
}

qreal PianoRollView::keyToY(int key) const
{
    return kRulerH + (127 - key) * m_keyH - verticalScrollBar()->value();
}

int PianoRollView::yToKey(qreal y) const
{
    return std::clamp(127 - int(std::floor((y - kRulerH + verticalScrollBar()->value()) / m_keyH)), 0, 127);
}

QRectF PianoRollView::noteRect(const Note& n) const
{
    return QRectF(tickToX(double(n.start)), keyToY(n.key), std::max(3.0, n.length * m_pxPerTick), m_keyH);
}

Tick PianoRollView::snapTick(double t) const
{
    if (m_snap <= 1)
        return Tick(std::max(0.0, t));
    return std::max<Tick>(0, Tick(std::floor(t / double(m_snap))) * m_snap);
}

void PianoRollView::updateScrollbars()
{
    Pattern* p = pattern();
    const double len = double(std::max<Tick>(p ? p->length() : kBarTicks, kBarTicks) + kBarTicks * 4);
    const QRectF g = gridRect();
    horizontalScrollBar()->setRange(0, std::max(0, int(len * m_pxPerTick - g.width())));
    horizontalScrollBar()->setPageStep(int(g.width()));
    verticalScrollBar()->setRange(0, std::max(0, int(128 * m_keyH - g.height())));
    verticalScrollBar()->setPageStep(int(g.height()));
    verticalScrollBar()->setSingleStep(m_keyH);
}

void PianoRollView::resizeEvent(QResizeEvent* e)
{
    QAbstractScrollArea::resizeEvent(e);
    updateScrollbars();
    // Keep the view centred while the layout settles, until the user scrolls themselves.
    if (!m_initialScroll && gridRect().height() > 60)
        scrollToNotes();
}

void PianoRollView::zoomX(double factor)
{
    const double centerTick = xToTick(gridRect().center().x());
    m_pxPerTick = std::clamp(m_pxPerTick * factor, 0.12, 6.0);
    updateScrollbars();
    horizontalScrollBar()->setValue(int(centerTick * m_pxPerTick - gridRect().width() / 2));
    viewport()->update();
}

void PianoRollView::scrollToNotes()
{
    auto* n = notes();
    if (!n || n->empty()) {
        verticalScrollBar()->setValue(int((127 - 66) * m_keyH - gridRect().height() / 2));
        return;
    }
    int lo = 127, hi = 0;
    for (const auto& note : *n) {
        lo = std::min(lo, note.key);
        hi = std::max(hi, note.key);
    }
    const int mid = (lo + hi) / 2;
    verticalScrollBar()->setValue(int((127 - mid) * m_keyH - gridRect().height() / 2));
}

int PianoRollView::noteAt(QPointF p, bool* nearEnd) const
{
    auto* n = notes();
    if (!n)
        return -1;
    for (int i = int(n->size()) - 1; i >= 0; --i) {
        const QRectF r = noteRect((*n)[size_t(i)]);
        if (r.contains(p)) {
            if (nearEnd)
                *nearEnd = p.x() > r.right() - std::min(7.0, r.width() / 3);
            return i;
        }
    }
    return -1;
}

void PianoRollView::commit()
{
    if (Pattern* p = pattern())
        m_project->notifyPatternEdited(p->id);
}

void PianoRollView::preview(int key, float vel)
{
    if (m_previewKey == key)
        return;
    stopPreview();
    if (m_channel) {
        m_engine->noteOn(m_channel, key, vel);
        m_previewKey = key;
    }
}

void PianoRollView::stopPreview()
{
    if (m_previewKey >= 0 && m_channel)
        m_engine->noteOff(m_channel, m_previewKey);
    m_previewKey = -1;
}

// ---------------------------------------------------------------- paint

void PianoRollView::paintEvent(QPaintEvent*)
{
    QPainter p(viewport());
    const QRectF g = gridRect();
    const QRectF vr = velRect();
    p.fillRect(viewport()->rect(), theme::bg1);
    Channel* ch = m_project->channel(m_channel);
    Pattern* pat = pattern();

    if (!ch || ch->kind == ChannelKind::Audio) {
        p.setPen(theme::textMute);
        p.setFont(theme::uiFont(10));
        p.drawText(viewport()->rect(), Qt::AlignCenter, tr("Select a channel in the Channel Rack to edit its notes"));
        return;
    }

    // Rows
    p.save();
    p.setClipRect(g);
    const int topKey = yToKey(g.top());
    const int botKey = yToKey(g.bottom());
    for (int k = botKey; k <= topKey; ++k) {
        const QRectF row(g.left(), keyToY(k), g.width(), m_keyH);
        p.fillRect(row, isBlack(k) ? theme::bg1 : theme::bg2);
        if (k % 12 == 0) {
            p.setPen(QPen(theme::line, 1));
            p.drawLine(QPointF(g.left(), row.bottom()), QPointF(g.right(), row.bottom()));
        }
    }
    // Columns
    const double t0 = xToTick(g.left());
    const double t1 = xToTick(g.right());
    const Tick grid = m_pxPerTick * kStepTicks < 6 ? kBeatTicks : kStepTicks;
    for (Tick t = Tick(std::floor(t0 / grid)) * grid; t <= t1; t += grid) {
        const qreal x = std::round(tickToX(double(t))) + 0.5;
        QColor c = t % kBarTicks == 0 ? theme::bg4.lighter(125) : t % kBeatTicks == 0 ? theme::line : theme::lineSoft;
        p.setPen(QPen(c, 1));
        p.drawLine(QPointF(x, g.top()), QPointF(x, g.bottom()));
    }
    // Beyond pattern end
    if (pat) {
        const qreal ex = tickToX(double(pat->length()));
        if (ex < g.right())
            p.fillRect(QRectF(ex, g.top(), g.right() - ex, g.height()), QColor(0, 0, 0, 70));
    }

    // Notes
    p.setRenderHint(QPainter::Antialiasing);
    if (auto* n = notes()) {
        for (size_t i = 0; i < n->size(); ++i) {
            const Note& note = (*n)[i];
            const QRectF r = noteRect(note).adjusted(0.5, 0.5, -0.5, -0.5);
            if (!r.intersects(g))
                continue;
            const bool sel = i < m_sel.size() && m_sel[i];
            QColor c = theme::mix(ch->color.darker(170), ch->color, 0.35 + 0.65 * note.velocity);
            p.setBrush(c);
            p.setPen(sel ? QPen(Qt::white, 1.5) : Qt::NoPen);
            p.drawRoundedRect(r, 2, 2);
            if (r.width() > 28 && m_keyH >= 12) {
                p.setPen(QColor(0, 0, 0, 170));
                p.setFont(theme::uiFont(7, QFont::DemiBold));
                p.drawText(r.adjusted(4, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft, noteName(note.key));
            }
        }
    }
    // Rubber band
    if (m_drag == Drag::Select) {
        const QRectF band = QRectF(m_pressPos, m_curPos).normalized();
        p.setPen(QPen(theme::accent, 1, Qt::DashLine));
        QColor f = theme::accent;
        f.setAlphaF(0.12f);
        p.setBrush(f);
        p.drawRect(band);
    }
    // Playhead
    if (m_engine->isPlaying() && !m_engine->songMode() && pat) {
        const double pos = std::fmod(m_engine->position(), double(std::max<Tick>(1, pat->length())));
        const qreal x = tickToX(pos);
        p.setPen(QPen(theme::accent, 1.5));
        p.drawLine(QPointF(x, g.top()), QPointF(x, g.bottom()));
    }
    p.restore();

    // Keyboard
    p.save();
    p.setClipRect(QRectF(0, g.top(), kKeyW, g.height()));
    for (int k = botKey; k <= topKey; ++k) {
        const QRectF r(0, keyToY(k), kKeyW - 1, m_keyH);
        const bool black = isBlack(k);
        QColor c = black ? QColor("#1d2027") : QColor("#d9dde6");
        if (k == m_previewKey)
            c = theme::accent;
        else if (k == m_hoverKey)
            c = black ? QColor("#343944") : QColor("#f2f4f8");
        p.fillRect(black ? r.adjusted(0, 1, -18, -1) : r, c);
        if (!black) {
            p.setPen(QPen(QColor("#9aa0ab"), 1));
            p.drawLine(r.bottomLeft(), r.bottomRight());
        } else {
            p.fillRect(QRectF(r.right() - 18, r.top(), 18, r.height()), QColor("#d9dde6"));
        }
        if (k % 12 == 0) {
            p.setPen(QColor("#3a3f4b"));
            p.setFont(theme::uiFont(7, QFont::Bold));
            p.drawText(r.adjusted(0, 0, -4, 0), Qt::AlignVCenter | Qt::AlignRight, noteName(k));
        }
    }
    p.restore();

    // Ruler
    const QRectF ruler(kKeyW, 0, g.width(), kRulerH);
    p.fillRect(QRectF(0, 0, viewport()->width(), kRulerH), theme::bg2);
    p.save();
    p.setClipRect(ruler);
    p.setFont(theme::monoFont(8));
    for (Tick t = Tick(std::floor(t0 / kBeatTicks)) * kBeatTicks; t <= t1; t += kBeatTicks) {
        const qreal x = std::round(tickToX(double(t))) + 0.5;
        if (t % kBarTicks == 0) {
            p.setPen(theme::textDim);
            p.drawText(QPointF(x + 4, 15), QString::number(t / kBarTicks + 1));
            p.setPen(QPen(theme::textMute, 1));
            p.drawLine(QPointF(x, 8), QPointF(x, kRulerH));
        } else {
            p.setPen(QPen(theme::line, 1));
            p.drawLine(QPointF(x, 16), QPointF(x, kRulerH));
        }
    }
    p.restore();
    p.setPen(theme::line);
    p.drawLine(QPointF(0, kRulerH - 0.5), QPointF(viewport()->width(), kRulerH - 0.5));

    // Velocity lane
    p.fillRect(vr, theme::bg0);
    p.fillRect(QRectF(0, vr.top(), kKeyW, vr.height()), theme::bg0);
    p.setPen(theme::line);
    p.drawLine(QPointF(0, vr.top() + 0.5), QPointF(viewport()->width(), vr.top() + 0.5));
    p.setPen(theme::textMute);
    p.setFont(theme::uiFont(7, QFont::Bold));
    p.drawText(QRectF(0, vr.top(), kKeyW - 6, 20), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("VEL"));
    p.save();
    p.setClipRect(vr);
    p.setRenderHint(QPainter::Antialiasing);
    if (auto* n = notes()) {
        const QRectF inner = vr.adjusted(0, 8, 0, -6);
        for (size_t i = 0; i < n->size(); ++i) {
            const Note& note = (*n)[i];
            const qreal x = tickToX(double(note.start)) + 1;
            const qreal y = inner.bottom() - inner.height() * note.velocity;
            const bool sel = i < m_sel.size() && m_sel[i];
            const QColor c = sel ? theme::accent : ch->color;
            p.setPen(QPen(c, 2));
            p.drawLine(QPointF(x, inner.bottom()), QPointF(x, y));
            p.setBrush(theme::bg0);
            p.drawEllipse(QPointF(x, y), 3, 3);
        }
    }
    p.restore();
}

// ---------------------------------------------------------------- input

void PianoRollView::setVelocityAt(QPointF pos)
{
    auto* n = notes();
    if (!n)
        return;
    const QRectF inner = velRect().adjusted(0, 8, 0, -6);
    const float v = float(std::clamp((inner.bottom() - pos.y()) / inner.height(), 0.02, 1.0));
    const bool anySel = std::any_of(m_sel.begin(), m_sel.end(), [](char c) { return c; });
    bool changed = false;
    for (size_t i = 0; i < n->size(); ++i) {
        const qreal x = tickToX(double((*n)[i].start));
        if (std::abs(x - pos.x()) <= 4 && (!anySel || m_sel[i])) {
            (*n)[i].velocity = v;
            changed = true;
        }
    }
    if (changed) {
        commit();
        emit UiHub::instance()->statusMessage(tr("Velocity %1%").arg(int(v * 100)), 1000);
    }
}

void PianoRollView::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    m_initialScroll = true;
    const QPointF pos = e->position();
    m_pressPos = m_curPos = pos;
    auto* n = notes();
    if (!n)
        return;
    syncSelection();

    if (pos.x() < kKeyW && pos.y() > kRulerH && pos.y() < velRect().top()) {
        m_drag = Drag::Keyboard;
        preview(yToKey(pos.y()), 0.8f);
        viewport()->update();
        return;
    }
    if (pos.y() < kRulerH) {
        if (!m_engine->songMode())
            m_engine->setPosition(std::max(0.0, xToTick(pos.x())));
        return;
    }
    if (velRect().contains(pos)) {
        m_drag = Drag::Velocity;
        setVelocityAt(pos);
        return;
    }
    if (!gridRect().contains(pos))
        return;

    bool nearEnd = false;
    const int hit = noteAt(pos, &nearEnd);
    if (e->button() == Qt::RightButton) {
        m_drag = Drag::Erase;
        if (hit >= 0) {
            n->erase(n->begin() + hit);
            m_sel.erase(m_sel.begin() + hit);
            commit();
        }
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;

    if (hit < 0 && (e->modifiers() & Qt::ControlModifier)) {
        m_drag = Drag::Select;
        if (!(e->modifiers() & Qt::ShiftModifier))
            std::fill(m_sel.begin(), m_sel.end(), 0);
        return;
    }

    int idx = hit;
    if (idx < 0) {
        Note note;
        note.key = yToKey(pos.y());
        note.start = snapTick(xToTick(pos.x()));
        note.length = std::max<Tick>(1, UiHub::instance()->lastNoteLength);
        note.velocity = 0.78f;
        n->push_back(note);
        m_sel.assign(n->size(), 0);
        idx = int(n->size()) - 1;
        m_sel[size_t(idx)] = 1;
        commit();
    } else if (!m_sel[size_t(idx)]) {
        if (!(e->modifiers() & Qt::ShiftModifier))
            std::fill(m_sel.begin(), m_sel.end(), 0);
        m_sel[size_t(idx)] = 1;
    }
    m_pressNote = idx;
    m_dragOrigin = *n;
    m_drag = nearEnd && hit >= 0 ? Drag::Resize : Drag::Move;
    if (m_drag == Drag::Resize)
        setCursor(Qt::SizeHorCursor);
    preview((*n)[size_t(idx)].key, (*n)[size_t(idx)].velocity);
    viewport()->update();
}

void PianoRollView::mouseMoveEvent(QMouseEvent* e)
{
    const QPointF pos = e->position();
    m_curPos = pos;
    auto* n = notes();

    if (m_drag == Drag::None) {
        const int hk = (pos.x() < kKeyW && pos.y() > kRulerH && pos.y() < velRect().top()) ? yToKey(pos.y()) : -1;
        if (hk != m_hoverKey) {
            m_hoverKey = hk;
            viewport()->update();
        }
        bool nearEnd = false;
        const int hit = gridRect().contains(pos) ? noteAt(pos, &nearEnd) : -1;
        viewport()->setCursor(hit >= 0 ? (nearEnd ? Qt::SizeHorCursor : Qt::SizeAllCursor) : Qt::ArrowCursor);
        return;
    }
    if (!n)
        return;

    switch (m_drag) {
    case Drag::Keyboard:
        preview(yToKey(pos.y()), 0.8f);
        viewport()->update();
        break;
    case Drag::Velocity:
        setVelocityAt(pos);
        break;
    case Drag::Erase: {
        const int hit = noteAt(pos);
        if (hit >= 0) {
            n->erase(n->begin() + hit);
            m_sel.erase(m_sel.begin() + hit);
            commit();
        }
        break;
    }
    case Drag::Select: {
        const QRectF band = QRectF(m_pressPos, pos).normalized();
        for (size_t i = 0; i < n->size(); ++i)
            if (noteRect((*n)[i]).intersects(band))
                m_sel[i] = 1;
            else if (!(e->modifiers() & Qt::ShiftModifier))
                m_sel[i] = 0;
        viewport()->update();
        break;
    }
    case Drag::Move: {
        if (m_pressNote < 0 || m_pressNote >= int(m_dragOrigin.size()) || m_dragOrigin.size() != n->size())
            break;
        const Note& anchor = m_dragOrigin[size_t(m_pressNote)];
        const double dt = xToTick(pos.x()) - xToTick(m_pressPos.x());
        const Tick newStart = snapTick(double(anchor.start) + dt + (m_snap > 1 ? m_snap / 2.0 : 0.0));
        Tick delta = newStart - anchor.start;
        int dk = yToKey(pos.y()) - yToKey(m_pressPos.y());
        for (size_t i = 0; i < n->size(); ++i)
            if (m_sel[i]) {
                delta = std::max(delta, -m_dragOrigin[i].start);
                dk = std::clamp(dk, -m_dragOrigin[i].key, 127 - m_dragOrigin[i].key);
            }
        for (size_t i = 0; i < n->size(); ++i)
            if (m_sel[i]) {
                (*n)[i].start = m_dragOrigin[i].start + delta;
                (*n)[i].key = m_dragOrigin[i].key + dk;
            }
        preview((*n)[size_t(m_pressNote)].key, (*n)[size_t(m_pressNote)].velocity);
        commit();
        break;
    }
    case Drag::Resize: {
        if (m_pressNote < 0 || m_dragOrigin.size() != n->size())
            break;
        const double dt = xToTick(pos.x()) - xToTick(m_pressPos.x());
        const Tick step = m_snap > 1 ? m_snap : 1;
        for (size_t i = 0; i < n->size(); ++i)
            if (m_sel[i]) {
                const double raw = double(m_dragOrigin[i].length) + dt;
                Tick len = m_snap > 1 ? Tick(std::round(raw / double(step))) * step : Tick(raw);
                (*n)[i].length = std::max<Tick>(m_snap > 1 ? step : 1, len);
            }
        UiHub::instance()->lastNoteLength = (*n)[size_t(m_pressNote)].length;
        commit();
        break;
    }
    default:
        break;
    }
}

void PianoRollView::mouseReleaseEvent(QMouseEvent*)
{
    if (m_drag == Drag::Move && m_pressNote >= 0) {
        if (auto* n = notes(); n && m_pressNote < int(n->size()))
            UiHub::instance()->lastNoteLength = (*n)[size_t(m_pressNote)].length;
    }
    m_drag = Drag::None;
    m_pressNote = -1;
    m_dragOrigin.clear();
    stopPreview();
    unsetCursor();
    viewport()->update();
}

void PianoRollView::wheelEvent(QWheelEvent* e)
{
    const int dy = e->angleDelta().y();
    if (e->modifiers() & Qt::ControlModifier) {
        zoomX(dy > 0 ? 1.2 : 1 / 1.2);
    } else if (e->modifiers() & Qt::AltModifier) {
        m_keyH = std::clamp(m_keyH + (e->angleDelta().x() + dy > 0 ? 1 : -1), 8, 28);
        updateScrollbars();
        viewport()->update();
    } else if (e->modifiers() & Qt::ShiftModifier) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - (dy ? dy : e->angleDelta().x()));
    } else {
        m_initialScroll = true;
        QAbstractScrollArea::wheelEvent(e);
    }
}

void PianoRollView::keyPressEvent(QKeyEvent* e)
{
    auto* n = notes();
    if (!n) {
        QAbstractScrollArea::keyPressEvent(e);
        return;
    }
    syncSelection();
    const bool anySel = std::any_of(m_sel.begin(), m_sel.end(), [](char c) { return c; });
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        std::vector<Note> keep;
        for (size_t i = 0; i < n->size(); ++i)
            if (!m_sel[i])
                keep.push_back((*n)[i]);
        *n = keep;
        m_sel.assign(n->size(), 0);
        commit();
    } else if (e->key() == Qt::Key_A && (e->modifiers() & Qt::ControlModifier)) {
        std::fill(m_sel.begin(), m_sel.end(), 1);
        viewport()->update();
    } else if (e->key() == Qt::Key_D && (e->modifiers() & Qt::ControlModifier) && anySel) {
        Tick lo = kNever, hi = 0;
        for (size_t i = 0; i < n->size(); ++i)
            if (m_sel[i]) {
                lo = std::min(lo, (*n)[i].start);
                hi = std::max(hi, (*n)[i].start + (*n)[i].length);
            }
        const Tick shift = std::max<Tick>(kBeatTicks, ((hi - lo + kBeatTicks - 1) / kBeatTicks) * kBeatTicks);
        const size_t count = n->size();
        for (size_t i = 0; i < count; ++i)
            if (m_sel[i]) {
                Note c = (*n)[i];
                c.start += shift;
                n->push_back(c);
            }
        m_sel.assign(n->size(), 0);
        for (size_t i = count; i < n->size(); ++i)
            m_sel[i] = 1;
        commit();
    } else if ((e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) && anySel) {
        const int d = (e->key() == Qt::Key_Up ? 1 : -1) * ((e->modifiers() & Qt::ShiftModifier) ? 12 : 1);
        for (size_t i = 0; i < n->size(); ++i)
            if (m_sel[i])
                (*n)[i].key = std::clamp((*n)[i].key + d, 0, 127);
        commit();
    } else if ((e->key() == Qt::Key_Left || e->key() == Qt::Key_Right) && anySel) {
        const Tick d = (e->key() == Qt::Key_Right ? 1 : -1) * std::max<Tick>(m_snap, 1);
        for (size_t i = 0; i < n->size(); ++i)
            if (m_sel[i])
                (*n)[i].start = std::max<Tick>(0, (*n)[i].start + d);
        commit();
    } else if (e->key() == Qt::Key_Escape) {
        std::fill(m_sel.begin(), m_sel.end(), 0);
        viewport()->update();
    } else {
        QAbstractScrollArea::keyPressEvent(e);
    }
}

// ---------------------------------------------------------------- container

PianoRoll::PianoRoll(Project* project, AudioEngine* engine, QWidget* parent) : QWidget(parent), m_project(project)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto* bar = new QWidget(this);
    bar->setFixedHeight(40);
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(10, 4, 10, 4);
    bl->setSpacing(8);
    m_title = new QLabel(bar);
    m_title->setFont(theme::uiFont(9.5, QFont::DemiBold));
    bl->addWidget(m_title);
    bl->addStretch(1);
    auto* snapIcon = new QLabel(bar);
    snapIcon->setPixmap(icons::icon("magnet").pixmap(16, 16));
    bl->addWidget(snapIcon);
    m_snap = new QComboBox(bar);
    m_snap->addItem(tr("Bar"), int(kBarTicks));
    m_snap->addItem(tr("1/4 beat"), int(kBeatTicks));
    m_snap->addItem(tr("1/8"), int(kBeatTicks / 2));
    m_snap->addItem(tr("1/16 step"), int(kStepTicks));
    m_snap->addItem(tr("1/32"), int(kStepTicks / 2));
    m_snap->addItem(tr("1/16 triplet"), int(kBeatTicks / 6));
    m_snap->addItem(tr("None"), 1);
    m_snap->setCurrentIndex(3);
    bl->addWidget(m_snap);
    auto* zin = new QToolButton(bar);
    zin->setText(QStringLiteral("+"));
    auto* zout = new QToolButton(bar);
    zout->setText(QStringLiteral("−"));
    bl->addWidget(zout);
    bl->addWidget(zin);
    auto* help = new QLabel(tr("Click: draw · Drag: move · Edge: resize · Right: delete · Ctrl+drag: select"), bar);
    help->setStyleSheet(QStringLiteral("color:%1; font-size:8pt;").arg(theme::textMute.name()));
    bl->insertWidget(1, help);
    bl->insertSpacing(1, 14);
    lay->addWidget(bar);

    m_view = new PianoRollView(project, engine, this);
    lay->addWidget(m_view, 1);

    connect(m_snap, &QComboBox::currentIndexChanged, this, [this](int i) { m_view->setSnap(m_snap->itemData(i).toInt()); });
    connect(zin, &QToolButton::clicked, this, [this] { m_view->zoomX(1.25); });
    connect(zout, &QToolButton::clicked, this, [this] { m_view->zoomX(0.8); });
    connect(project, &Project::selectedChannelChanged, this, &PianoRoll::showChannel);
    connect(project, &Project::channelsChanged, this, &PianoRoll::refreshHeader);
    connect(project, &Project::currentPatternChanged, this, &PianoRoll::refreshHeader);
    connect(project, &Project::projectReset, this, [this] { showChannel(m_project->selectedChannel()); });
    showChannel(project->selectedChannel());
}

void PianoRoll::showChannel(ObjId id)
{
    m_view->setChannel(id);
    m_view->scrollToNotes();
    refreshHeader();
}

void PianoRoll::refreshHeader()
{
    Channel* c = m_project->channel(m_view->channelId());
    Pattern* p = m_project->currentPattern();
    if (!c) {
        m_title->setText(tr("Piano roll"));
        return;
    }
    m_title->setText(QStringLiteral("<span style='color:%1'>●</span>&nbsp; %2 <span style='color:%3'>· %4</span>")
                         .arg(c->color.name(), c->name.toHtmlEscaped(), theme::textMute.name(), p ? p->name.toHtmlEscaped() : QString()));
}

} // namespace guiro
