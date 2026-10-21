#include "ui/Playlist.h"
#include "engine/AudioEngine.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/UiHub.h"
#include "ui/widgets/Waveform.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace guiro {

// ================================================================ ClipPicker

ClipPicker::ClipPicker(Project* project, QWidget* parent) : QWidget(parent), m_project(project)
{
    setMinimumWidth(140);
    setMouseTracking(true);
    connect(project, &Project::patternsChanged, this, &ClipPicker::rebuild);
    connect(project, &Project::channelsChanged, this, &ClipPicker::rebuild);
    connect(project, &Project::automationChanged, this, &ClipPicker::rebuild);
    connect(project, &Project::projectReset, this, &ClipPicker::rebuild);
    connect(project, &Project::currentPatternChanged, this, [this](ObjId id) { setBrush({ClipKind::Pattern, id}); });
    rebuild();
}

void ClipPicker::rebuild()
{
    m_items.clear();
    m_items.push_back({ClipKind::Pattern, 0, tr("PATTERNS"), {}, true});
    for (const auto& p : m_project->patterns)
        m_items.push_back({ClipKind::Pattern, p->id, p->name, p->color});
    bool audioHeader = false;
    for (const auto& c : m_project->channels) {
        if (c->kind != ChannelKind::Audio)
            continue;
        if (!audioHeader) {
            m_items.push_back({ClipKind::Audio, 0, tr("AUDIO CLIPS"), {}, true});
            audioHeader = true;
        }
        m_items.push_back({ClipKind::Audio, c->id, c->name, c->color});
    }
    if (!m_project->automations.empty()) {
        m_items.push_back({ClipKind::Automation, 0, tr("AUTOMATION"), {}, true});
        for (const auto& a : m_project->automations)
            m_items.push_back({ClipKind::Automation, a->id, a->name, a->color});
    }
    if (!m_brush.ref && !m_project->patterns.empty())
        m_brush = {ClipKind::Pattern, m_project->currentPatternId()};
    setMinimumHeight(int(m_items.size()) * kRowH + 8);
    update();
}

void ClipPicker::setBrush(Brush b)
{
    m_brush = b;
    update();
    emit brushChanged(b);
}

int ClipPicker::itemAt(QPointF p) const
{
    const int i = int((p.y() - 4) / kRowH);
    return i >= 0 && i < int(m_items.size()) && !m_items[size_t(i)].header ? i : -1;
}

void ClipPicker::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), theme::bg1);
    p.setRenderHint(QPainter::Antialiasing);
    for (size_t i = 0; i < m_items.size(); ++i) {
        const Item& it = m_items[i];
        const QRectF r(6, 4 + qreal(i) * kRowH, width() - 12, kRowH - 3);
        if (it.header) {
            p.setPen(theme::textMute);
            p.setFont(theme::uiFont(7, QFont::Bold));
            p.drawText(r.adjusted(4, 4, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, it.name);
            continue;
        }
        const bool sel = m_brush.kind == it.kind && m_brush.ref == it.ref;
        p.setPen(Qt::NoPen);
        p.setBrush(sel ? theme::mix(theme::bg3, it.color, 0.25) : theme::bg2);
        p.drawRoundedRect(r, 5, 5);
        p.setBrush(it.color);
        p.drawRoundedRect(QRectF(r.left() + 5, r.top() + 6, 4, r.height() - 12), 2, 2);
        const QString icon = it.kind == ClipKind::Pattern ? "pattern" : it.kind == ClipKind::Audio ? "wave" : "automation";
        icons::paint(p, icon, QRectF(r.left() + 14, r.center().y() - 7, 14, 14), sel ? theme::accent : theme::textDim);
        p.setPen(sel ? theme::text : theme::textDim);
        p.setFont(theme::uiFont(8.5, sel ? QFont::DemiBold : QFont::Normal));
        const QRectF tr = r.adjusted(34, 0, -4, 0);
        p.drawText(tr, Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(it.name, Qt::ElideRight, int(tr.width())));
    }
}

void ClipPicker::mousePressEvent(QMouseEvent* e)
{
    m_pressPos = e->position();
    m_pressItem = itemAt(e->position());
    if (m_pressItem < 0)
        return;
    const Item& it = m_items[size_t(m_pressItem)];
    setBrush({it.kind, it.ref});
    if (it.kind == ClipKind::Pattern)
        m_project->setCurrentPattern(it.ref);
}

void ClipPicker::mouseMoveEvent(QMouseEvent* e)
{
    if (!(e->buttons() & Qt::LeftButton) || m_pressItem < 0 ||
        (e->position() - m_pressPos).manhattanLength() < 6)
        return;
    const Item& it = m_items[size_t(m_pressItem)];
    auto* drag = new QDrag(this);
    auto* mime = new QMimeData;
    mime->setData(kClipMime, QStringLiteral("%1:%2").arg(int(it.kind)).arg(it.ref).toUtf8());
    drag->setMimeData(mime);
    m_pressItem = -1;
    drag->exec(Qt::CopyAction);
}

void ClipPicker::mouseDoubleClickEvent(QMouseEvent* e)
{
    const int i = itemAt(e->position());
    if (i < 0)
        return;
    const Item& it = m_items[size_t(i)];
    if (it.kind == ClipKind::Pattern) {
        bool ok = false;
        Pattern* p = m_project->pattern(it.ref);
        const QString n = QInputDialog::getText(this, tr("Rename pattern"), tr("Name"), QLineEdit::Normal, p->name, &ok);
        if (ok && !n.isEmpty()) {
            p->name = n;
            m_project->notifyPatterns();
        }
    }
}

void ClipPicker::contextMenuEvent(QContextMenuEvent* e)
{
    const int i = itemAt(e->pos());
    if (i < 0)
        return;
    const Item it = m_items[size_t(i)];
    QMenu menu(this);
    if (it.kind == ClipKind::Pattern) {
        menu.addAction(tr("Rename…"), this, [this, it] {
            Pattern* p = m_project->pattern(it.ref);
            bool ok = false;
            const QString n = QInputDialog::getText(this, tr("Rename pattern"), tr("Name"), QLineEdit::Normal, p->name, &ok);
            if (ok && !n.isEmpty()) {
                p->name = n;
                m_project->notifyPatterns();
            }
        });
        menu.addAction(tr("Color…"), this, [this, it] {
            Pattern* p = m_project->pattern(it.ref);
            const QColor c = QColorDialog::getColor(p->color, this);
            if (c.isValid()) {
                p->color = c;
                m_project->notifyPatterns();
            }
        });
        menu.addAction(tr("Clone"), this, [this, it] { m_project->clonePattern(it.ref); });
        menu.addAction(icons::icon("trash", theme::danger), tr("Delete"), this, [this, it] { m_project->removePattern(it.ref); });
    } else if (it.kind == ClipKind::Automation) {
        menu.addAction(icons::icon("trash", theme::danger), tr("Delete automation"), this,
                       [this, it] { m_project->removeAutomation(it.ref); });
    } else {
        menu.addAction(icons::icon("trash", theme::danger), tr("Delete audio channel"), this,
                       [this, it] { m_project->removeChannel(it.ref); });
    }
    menu.exec(e->globalPos());
}

// ================================================================ PlaylistView

PlaylistView::PlaylistView(Project* project, AudioEngine* engine, ClipPicker* picker, QWidget* parent)
    : QAbstractScrollArea(parent), m_project(project), m_engine(engine), m_picker(picker)
{
    setFrameShape(QFrame::NoFrame);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    viewport()->setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    auto refresh = [this] {
        updateScrollbars();
        viewport()->update();
    };
    connect(project, &Project::playlistChanged, this, refresh);
    connect(project, &Project::patternEdited, this, refresh);
    connect(project, &Project::patternsChanged, this, refresh);
    connect(project, &Project::channelsChanged, this, refresh);
    connect(project, &Project::automationChanged, this, refresh);
    connect(project, &Project::projectReset, this, [this, refresh] {
        m_selected.clear();
        horizontalScrollBar()->setValue(0);
        verticalScrollBar()->setValue(0);
        refresh();
    });
    connect(UiHub::instance(), &UiHub::frame, this, [this] {
        if (isVisible() && m_engine->isPlaying()) {
            followPlayhead();
            viewport()->update();
        }
    });
}

QRectF PlaylistView::gridRect() const
{
    return QRectF(kHeaderW, kRulerH, viewport()->width() - kHeaderW, viewport()->height() - kRulerH);
}

qreal PlaylistView::tickToX(double t) const
{
    return kHeaderW + t * m_pxPerTick - horizontalScrollBar()->value();
}

double PlaylistView::xToTick(qreal x) const
{
    return (x - kHeaderW + horizontalScrollBar()->value()) / m_pxPerTick;
}

qreal PlaylistView::trackToY(int track) const
{
    return kRulerH + track * m_trackH - verticalScrollBar()->value();
}

int PlaylistView::yToTrack(qreal y) const
{
    return std::clamp(int(std::floor((y - kRulerH + verticalScrollBar()->value()) / m_trackH)), 0, kNumTracks - 1);
}

Tick PlaylistView::snapTick(double t) const
{
    if (m_snap <= 1)
        return Tick(std::max(0.0, t));
    return std::max<Tick>(0, Tick(std::floor(t / double(m_snap))) * m_snap);
}

QRectF PlaylistView::clipRect(const Clip& c) const
{
    return QRectF(tickToX(double(c.start)), trackToY(c.track) + 1, c.length * m_pxPerTick, m_trackH - 2);
}

void PlaylistView::updateScrollbars()
{
    const double len = double(std::max<Tick>(m_project->songLength() + kBarTicks * 16, kBarTicks * 64));
    const QRectF g = gridRect();
    horizontalScrollBar()->setRange(0, std::max(0, int(len * m_pxPerTick - g.width())));
    horizontalScrollBar()->setPageStep(int(g.width()));
    horizontalScrollBar()->setSingleStep(int(kBeatTicks * m_pxPerTick));
    verticalScrollBar()->setRange(0, std::max(0, kNumTracks * m_trackH - int(g.height())));
    verticalScrollBar()->setPageStep(int(g.height()));
    verticalScrollBar()->setSingleStep(m_trackH / 2);
}

void PlaylistView::resizeEvent(QResizeEvent* e)
{
    QAbstractScrollArea::resizeEvent(e);
    updateScrollbars();
}

void PlaylistView::zoomX(double f)
{
    const double centerTick = xToTick(gridRect().center().x());
    m_pxPerTick = std::clamp(m_pxPerTick * f, 0.03, 3.0);
    updateScrollbars();
    horizontalScrollBar()->setValue(int(centerTick * m_pxPerTick - gridRect().width() / 2));
    viewport()->update();
}

void PlaylistView::followPlayhead()
{
    if (!m_engine->songMode() || m_drag != Drag::None)
        return;
    const qreal x = tickToX(m_engine->position());
    const QRectF g = gridRect();
    if (x > g.right() - 40 || x < g.left())
        horizontalScrollBar()->setValue(int(m_engine->position() * m_pxPerTick - g.width() * 0.1));
}

int PlaylistView::clipAt(QPointF p) const
{
    for (int i = int(m_project->clips.size()) - 1; i >= 0; --i)
        if (clipRect(m_project->clips[size_t(i)]).contains(p))
            return i;
    return -1;
}

QString PlaylistView::clipName(const Clip& c) const
{
    switch (c.kind) {
    case ClipKind::Pattern:
        if (Pattern* p = m_project->pattern(c.ref))
            return p->name;
        break;
    case ClipKind::Audio:
        if (Channel* ch = m_project->channel(c.ref))
            return ch->name;
        break;
    case ClipKind::Automation:
        if (AutomationClip* a = m_project->automation(c.ref))
            return a->name;
        break;
    }
    return QStringLiteral("?");
}

QColor PlaylistView::clipColor(const Clip& c) const
{
    switch (c.kind) {
    case ClipKind::Pattern:
        if (Pattern* p = m_project->pattern(c.ref))
            return p->color;
        break;
    case ClipKind::Audio:
        if (Channel* ch = m_project->channel(c.ref))
            return ch->color;
        break;
    case ClipKind::Automation:
        if (AutomationClip* a = m_project->automation(c.ref))
            return a->color;
        break;
    }
    return theme::textMute;
}

void PlaylistView::drawClip(QPainter& p, const Clip& c, const QRectF& r, bool selected)
{
    const QColor col = clipColor(c);
    const bool muted = m_project->tracks[size_t(c.track)].muted;
    QColor body = theme::mix(theme::bg2, col, muted ? 0.08 : 0.22);
    QColor head = theme::mix(theme::bg2, col, muted ? 0.2 : 0.75);

    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath shape;
    shape.addRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    p.setClipPath(shape);
    p.fillRect(r, body);
    const qreal headH = std::min<qreal>(16, r.height() / 3);
    p.fillRect(QRectF(r.left(), r.top(), r.width(), headH), head);
    p.setPen(muted ? theme::textMute : QColor(10, 12, 16));
    p.setFont(theme::uiFont(7.5, QFont::DemiBold));
    p.drawText(QRectF(r.left() + 5, r.top(), r.width() - 8, headH), Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(clipName(c), Qt::ElideRight, int(r.width() - 8)));

    const QRectF content(r.left() + 2, r.top() + headH + 2, r.width() - 4, r.height() - headH - 4);
    const QColor ink = muted ? theme::textMute : col.lighter(115);

    if (c.kind == ClipKind::Pattern) {
        if (Pattern* pat = m_project->pattern(c.ref)) {
            const double L = double(std::max<Tick>(1, pat->length()));
            // Gather all events of all channels and draw them as a mini piano roll.
            int lo = 127, hi = 0;
            std::vector<std::tuple<double, double, int>> evs;
            int row = 0;
            for (const auto& ch : m_project->channels) {
                if (auto it = pat->stepData.find(ch->id); it != pat->stepData.end()) {
                    bool any = false;
                    for (int s = 0; s < std::min<int>(pat->steps, int(it->second.size())); ++s)
                        if (it->second[size_t(s)]) {
                            evs.emplace_back(double(s * kStepTicks), double(kStepTicks) * 0.6, 200 + row);
                            any = true;
                        }
                    if (any)
                        ++row;
                }
                if (auto it = pat->notes.find(ch->id); it != pat->notes.end())
                    for (const Note& n : it->second) {
                        evs.emplace_back(double(n.start), double(n.length), n.key);
                        lo = std::min(lo, n.key);
                        hi = std::max(hi, n.key);
                    }
            }
            const int stepRows = std::max(row, 1);
            const int noteRange = hi >= lo ? std::max(hi - lo + 1, 6) : 0;
            const int totalRows = stepRows + noteRange;
            p.setPen(Qt::NoPen);
            p.setBrush(ink);
            for (double rep = -double(c.offset); rep < double(c.length); rep += L) {
                for (const auto& [start, len, key] : evs) {
                    const double t = rep + start;
                    if (t + len < 0 || t >= double(c.length))
                        continue;
                    const qreal x = content.left() + t * m_pxPerTick;
                    const qreal w = std::max(1.5, len * m_pxPerTick - 0.5);
                    const int rowIdx = key >= 200 ? key - 200 : stepRows + (hi - key);
                    const qreal rh = content.height() / std::max(totalRows, 1);
                    p.drawRect(QRectF(x, content.top() + rowIdx * rh + rh * 0.15, w, std::max(1.2, rh * 0.7)));
                }
            }
        }
    } else if (c.kind == ClipKind::Audio) {
        if (Channel* ch = m_project->channel(c.ref); ch && ch->sample) {
            const double framesPerTick = ch->sample->sampleRate * 60.0 / (m_project->bpm() * kPPQ);
            const double f0 = double(c.offset) * framesPerTick;
            const double f1 = f0 + double(c.length) * framesPerTick;
            const QRectF visible = content.intersected(QRectF(gridRect().left(), content.top(), gridRect().width(), content.height()));
            if (visible.width() > 1) {
                const double vf0 = f0 + (visible.left() - content.left()) / content.width() * (f1 - f0);
                const double vf1 = f0 + (visible.right() - content.left()) / content.width() * (f1 - f0);
                drawWaveform(p, *ch->sample, visible, ink, vf0, std::min(vf1, double(ch->sample->frames)));
            }
        }
    } else if (c.kind == ClipKind::Automation) {
        if (AutomationClip* a = m_project->automation(c.ref); a && !a->points.empty()) {
            QPainterPath path;
            QPainterPath fill;
            const auto pt = [&](const AutoPoint& ap) {
                return QPointF(content.left() + double(ap.t - c.offset) * m_pxPerTick,
                               content.bottom() - ap.v * content.height());
            };
            path.moveTo(QPointF(content.left(), content.bottom() - a->valueAt(c.offset) * content.height()));
            for (const auto& ap : a->points)
                if (ap.t > c.offset && ap.t < c.offset + c.length)
                    path.lineTo(pt(ap));
            path.lineTo(QPointF(content.right(), content.bottom() - a->valueAt(c.offset + c.length) * content.height()));
            fill = path;
            fill.lineTo(content.bottomRight());
            fill.lineTo(content.bottomLeft());
            fill.closeSubpath();
            QColor f = ink;
            f.setAlphaF(0.18f);
            p.setPen(Qt::NoPen);
            p.setBrush(f);
            p.drawPath(fill);
            p.setPen(QPen(ink, 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
            p.setBrush(theme::bg0);
            for (const auto& ap : a->points)
                if (ap.t >= c.offset && ap.t <= c.offset + c.length)
                    p.drawEllipse(pt(ap), 3.2, 3.2);
        }
    }
    p.setClipping(false);
    if (selected) {
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(0.75, 0.75, -0.75, -0.75), 4, 4);
    }
    p.restore();
}

void PlaylistView::paintEvent(QPaintEvent*)
{
    QPainter p(viewport());
    const QRectF g = gridRect();
    p.fillRect(viewport()->rect(), theme::bg1);

    const int firstTrack = yToTrack(g.top());
    const int lastTrack = yToTrack(g.bottom());
    const double t0 = xToTick(g.left());
    const double t1 = xToTick(g.right());

    // Grid
    p.save();
    p.setClipRect(g);
    for (int t = firstTrack; t <= lastTrack; ++t) {
        const QRectF row(g.left(), trackToY(t), g.width(), m_trackH);
        p.fillRect(row, t % 2 ? theme::bg1 : theme::mix(theme::bg1, theme::bg2, 0.6));
        p.setPen(QPen(theme::lineSoft, 1));
        p.drawLine(QPointF(g.left(), row.bottom() - 0.5), QPointF(g.right(), row.bottom() - 0.5));
    }
    const Tick grid = m_pxPerTick * kBeatTicks < 10 ? kBarTicks : kBeatTicks;
    for (Tick t = Tick(std::floor(t0 / grid)) * grid; t <= t1; t += grid) {
        const qreal x = std::round(tickToX(double(t))) + 0.5;
        p.setPen(QPen(t % (kBarTicks * 4) == 0 ? theme::bg4 : t % kBarTicks == 0 ? theme::line : theme::lineSoft, 1));
        p.drawLine(QPointF(x, g.top()), QPointF(x, g.bottom()));
    }
    // Loop region
    if (m_project->loopEnd > m_project->loopStart) {
        const qreal x0 = tickToX(double(m_project->loopStart));
        const qreal x1 = tickToX(double(m_project->loopEnd));
        QColor f = theme::accent2;
        f.setAlphaF(0.05f);
        p.fillRect(QRectF(x0, g.top(), x1 - x0, g.height()), f);
    }
    // Clips
    for (const Clip& c : m_project->clips) {
        if (c.track < firstTrack || c.track > lastTrack)
            continue;
        const QRectF r = clipRect(c);
        if (r.right() < g.left() || r.left() > g.right())
            continue;
        const bool sel = std::find(m_selected.begin(), m_selected.end(), c.id) != m_selected.end();
        drawClip(p, c, r, sel);
    }
    if (m_project->clips.empty()) {
        const QRectF card(g.center().x() - 210, g.top() + 60, 420, 110);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(theme::bg2);
        p.drawRoundedRect(card, 8, 8);
        icons::paint(p, QStringLiteral("song"), QRectF(card.center().x() - 14, card.top() + 16, 28, 28), theme::accent);
        p.setPen(theme::text);
        p.setFont(theme::uiFont(10.5, QFont::DemiBold));
        p.drawText(QRectF(card.left(), card.top() + 50, card.width(), 22), Qt::AlignCenter, tr("Arrange your song here"));
        p.setPen(theme::textDim);
        p.setFont(theme::uiFont(8.5));
        p.drawText(QRectF(card.left(), card.top() + 72, card.width(), 20), Qt::AlignCenter,
                   tr("Click to paint the selected pattern · drop audio files to make clips"));
    }
    // Playhead
    if (m_engine->songMode()) {
        const qreal x = tickToX(m_engine->position());
        if (x >= g.left()) {
            p.setPen(QPen(theme::accent, 1.5));
            p.drawLine(QPointF(x, g.top()), QPointF(x, g.bottom()));
        }
    }
    p.restore();

    // Track headers
    p.save();
    p.setClipRect(QRectF(0, g.top(), kHeaderW, g.height()));
    p.setRenderHint(QPainter::Antialiasing);
    for (int t = firstTrack; t <= lastTrack; ++t) {
        const Track& tr = m_project->tracks[size_t(t)];
        const QRectF r(0, trackToY(t), kHeaderW, m_trackH);
        p.fillRect(r, theme::bg1);
        p.setPen(theme::lineSoft);
        p.drawLine(QPointF(r.left(), r.bottom() - 0.5), QPointF(r.right(), r.bottom() - 0.5));
        p.setPen(Qt::NoPen);
        p.setBrush(tr.color);
        p.drawRoundedRect(QRectF(r.left() + 10, r.center().y() - 4, 8, 8), 2, 2);
        p.setPen(tr.muted ? theme::textMute : theme::text);
        p.setFont(theme::uiFont(8.5));
        p.drawText(r.adjusted(26, 0, -30, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   p.fontMetrics().elidedText(tr.name, Qt::ElideRight, int(r.width() - 58)));
        const QRectF mute(r.right() - 26, r.center().y() - 8, 16, 16);
        p.setPen(Qt::NoPen);
        p.setBrush(tr.muted ? theme::bg3 : theme::mix(theme::bg3, theme::good, 0.25));
        p.drawRoundedRect(mute, 3, 3);
        p.setPen(tr.muted ? theme::textMute : theme::good);
        p.setFont(theme::uiFont(7, QFont::Bold));
        p.drawText(mute, Qt::AlignCenter, QStringLiteral("M"));
    }
    p.restore();

    // Ruler
    p.fillRect(QRectF(0, 0, viewport()->width(), kRulerH), theme::bg2);
    p.save();
    p.setClipRect(QRectF(kHeaderW, 0, g.width(), kRulerH));
    if (m_project->loopEnd > m_project->loopStart) {
        const qreal x0 = tickToX(double(m_project->loopStart));
        const qreal x1 = tickToX(double(m_project->loopEnd));
        QColor f = theme::accent2;
        f.setAlphaF(0.35f);
        p.fillRect(QRectF(x0, 0, x1 - x0, 7), f);
        p.setPen(QPen(theme::accent2, 1));
        p.drawLine(QPointF(x0, 0), QPointF(x0, kRulerH));
        p.drawLine(QPointF(x1, 0), QPointF(x1, kRulerH));
    }
    const int labelEvery = m_pxPerTick * kBarTicks < 30 ? 4 : 1;
    p.setFont(theme::monoFont(8));
    for (Tick t = Tick(std::floor(t0 / kBarTicks)) * kBarTicks; t <= t1; t += kBarTicks) {
        const qreal x = std::round(tickToX(double(t))) + 0.5;
        const int bar = int(t / kBarTicks);
        p.setPen(QPen(theme::textMute, 1));
        p.drawLine(QPointF(x, bar % labelEvery == 0 ? 12 : 20), QPointF(x, kRulerH));
        if (bar % labelEvery == 0) {
            p.setPen(theme::textDim);
            p.drawText(QPointF(x + 4, 20), QString::number(bar + 1));
        }
    }
    if (m_engine->songMode()) {
        const qreal x = tickToX(m_engine->position());
        QPainterPath tri;
        tri.moveTo(x - 6, kRulerH - 9);
        tri.lineTo(x + 6, kRulerH - 9);
        tri.lineTo(x, kRulerH - 1);
        tri.closeSubpath();
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(theme::accent);
        p.drawPath(tri);
    }
    p.restore();
    p.setPen(theme::line);
    p.drawLine(QPointF(0, kRulerH - 0.5), QPointF(viewport()->width(), kRulerH - 0.5));
    p.drawLine(QPointF(kHeaderW - 0.5, 0), QPointF(kHeaderW - 0.5, viewport()->height()));
    p.setPen(theme::textMute);
    p.setFont(theme::uiFont(7, QFont::Bold));
    p.drawText(QRectF(10, 0, kHeaderW - 10, kRulerH), Qt::AlignVCenter | Qt::AlignLeft,
               m_engine->songMode() ? tr("SONG MODE") : tr("PATTERN MODE"));
}

int PlaylistView::automationPointAt(const Clip& c, QPointF pos) const
{
    AutomationClip* a = m_project->automation(c.ref);
    if (!a)
        return -1;
    const QRectF r = clipRect(c);
    const qreal headH = std::min<qreal>(16, r.height() / 3);
    const QRectF content(r.left() + 2, r.top() + headH + 2, r.width() - 4, r.height() - headH - 4);
    for (size_t i = 0; i < a->points.size(); ++i) {
        const auto& ap = a->points[i];
        const QPointF pt(content.left() + double(ap.t - c.offset) * m_pxPerTick, content.bottom() - ap.v * content.height());
        if (QLineF(pt, pos).length() <= 7)
            return int(i);
    }
    return -1;
}

void PlaylistView::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    const QPointF pos = e->position();
    m_pressPos = pos;
    const QRectF g = gridRect();

    // Ruler: click = locate, drag = loop region, right click = clear loop.
    if (pos.y() < kRulerH && pos.x() > kHeaderW) {
        if (e->button() == Qt::RightButton) {
            m_project->loopStart = m_project->loopEnd = 0;
            m_project->notifyPlaylist();
            return;
        }
        m_drag = Drag::Ruler;
        m_rulerStart = xToTick(pos.x());
        m_rulerMoved = false;
        return;
    }
    // Track headers
    if (pos.x() < kHeaderW && pos.y() > kRulerH) {
        const int t = yToTrack(pos.y());
        if (pos.x() > kHeaderW - 30 || e->button() == Qt::LeftButton) {
            if (pos.x() > kHeaderW - 30) {
                m_project->tracks[size_t(t)].muted = !m_project->tracks[size_t(t)].muted;
                m_project->notifyPlaylist();
            }
        }
        return;
    }
    if (!g.contains(pos))
        return;

    const int idx = clipAt(pos);
    if (e->button() == Qt::RightButton) {
        if (idx >= 0) {
            Clip& c = m_project->clips[size_t(idx)];
            if (c.kind == ClipKind::Automation) {
                const int pt = automationPointAt(c, pos);
                AutomationClip* a = m_project->automation(c.ref);
                if (pt >= 0 && a && a->points.size() > 1) {
                    a->points.erase(a->points.begin() + pt);
                    m_project->notifyAutomation();
                    return;
                }
            }
            m_project->removeClip(c.id);
        }
        m_drag = Drag::Erase;
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;

    if (idx >= 0) {
        Clip& c = m_project->clips[size_t(idx)];
        if (c.kind == ClipKind::Automation) {
            const int pt = automationPointAt(c, pos);
            if (pt >= 0) {
                m_drag = Drag::Point;
                m_dragClip = c.id;
                m_dragPoint = pt;
                return;
            }
        }
        if (!(e->modifiers() & Qt::ShiftModifier))
            m_selected.clear();
        if (std::find(m_selected.begin(), m_selected.end(), c.id) == m_selected.end())
            m_selected.push_back(c.id);
        if (e->modifiers() & Qt::ControlModifier) {
            // Ctrl-drag duplicates.
            Clip copy = c;
            Clip& dup = m_project->addClip(copy.kind, copy.ref, copy.track, copy.start, copy.length);
            dup.offset = copy.offset;
            m_dragClip = dup.id;
            m_origin = dup;
        } else {
            m_dragClip = c.id;
            m_origin = c;
        }
        const QRectF r = clipRect(m_origin);
        if (pos.x() > r.right() - 8)
            m_drag = Drag::ResizeRight;
        else if (pos.x() < r.left() + 6)
            m_drag = Drag::ResizeLeft;
        else
            m_drag = Drag::Move;
        if (m_origin.kind == ClipKind::Pattern)
            m_project->setCurrentPattern(m_origin.ref);
        m_picker->setBrush({m_origin.kind, m_origin.ref});
        viewport()->update();
        return;
    }

    // Empty: paint the brush.
    const Brush b = m_picker->brush();
    if (!b.ref)
        return;
    Clip probe;
    probe.kind = b.kind;
    probe.ref = b.ref;
    const Tick len = m_project->sourceLength(probe);
    Clip& c = m_project->addClip(b.kind, b.ref, yToTrack(pos.y()), snapTick(xToTick(pos.x())), len);
    m_selected = {c.id};
    m_dragClip = c.id;
    m_origin = c;
    m_drag = Drag::Move;
}

void PlaylistView::mouseMoveEvent(QMouseEvent* e)
{
    const QPointF pos = e->position();
    if (m_drag == Drag::None) {
        const int idx = clipAt(pos);
        Qt::CursorShape cur = Qt::ArrowCursor;
        if (idx >= 0) {
            const Clip& c = m_project->clips[size_t(idx)];
            const QRectF r = clipRect(c);
            if (c.kind == ClipKind::Automation && automationPointAt(c, pos) >= 0)
                cur = Qt::PointingHandCursor;
            else if (pos.x() > r.right() - 8 || pos.x() < r.left() + 6)
                cur = Qt::SizeHorCursor;
            else
                cur = Qt::SizeAllCursor;
        } else if (pos.y() < kRulerH && pos.x() > kHeaderW) {
            cur = Qt::IBeamCursor;
        }
        viewport()->setCursor(cur);
        return;
    }

    switch (m_drag) {
    case Drag::Ruler: {
        const double t = xToTick(pos.x());
        if (std::abs(pos.x() - m_pressPos.x()) > 4) {
            m_rulerMoved = true;
            const Tick a = snapTick(std::min(t, m_rulerStart));
            const Tick b = snapTick(std::max(t, m_rulerStart)) + std::max<Tick>(m_snap, 1);
            m_project->loopStart = a;
            m_project->loopEnd = b;
            m_project->notifyPlaylist();
        }
        break;
    }
    case Drag::Erase: {
        const int idx = clipAt(pos);
        if (idx >= 0)
            m_project->removeClip(m_project->clips[size_t(idx)].id);
        break;
    }
    case Drag::Point: {
        Clip* c = m_project->clip(m_dragClip);
        AutomationClip* a = c ? m_project->automation(c->ref) : nullptr;
        if (!a || m_dragPoint < 0 || m_dragPoint >= int(a->points.size()))
            break;
        const QRectF r = clipRect(*c);
        const qreal headH = std::min<qreal>(16, r.height() / 3);
        const QRectF content(r.left() + 2, r.top() + headH + 2, r.width() - 4, r.height() - headH - 4);
        AutoPoint& pt = a->points[size_t(m_dragPoint)];
        const Tick lo = m_dragPoint > 0 ? a->points[size_t(m_dragPoint - 1)].t : 0;
        const Tick hi = m_dragPoint + 1 < int(a->points.size()) ? a->points[size_t(m_dragPoint + 1)].t : kNever;
        Tick t = Tick(double(c->offset) + (pos.x() - content.left()) / m_pxPerTick);
        if (!(e->modifiers() & Qt::AltModifier) && m_snap > 1)
            t = Tick(std::round(double(t) / double(m_snap / 2 > 0 ? m_snap / 2 : 1))) * (m_snap / 2 > 0 ? m_snap / 2 : 1);
        pt.t = std::clamp<Tick>(t, lo, hi == kNever ? std::max<Tick>(lo, t) : hi);
        if (m_dragPoint == 0)
            pt.t = std::max<Tick>(0, pt.t);
        pt.v = float(std::clamp((content.bottom() - pos.y()) / content.height(), 0.0, 1.0));
        m_project->notifyAutomation();
        if (ParamPtr param = m_project->resolveParam(a->target))
            emit UiHub::instance()->statusMessage(param->format(param->fromNormalized(pt.v)), 1000);
        break;
    }
    case Drag::Move: {
        Clip* c = m_project->clip(m_dragClip);
        if (!c)
            break;
        const double dt = xToTick(pos.x()) - xToTick(m_pressPos.x());
        c->start = snapTick(double(m_origin.start) + dt + (m_snap > 1 ? m_snap / 2.0 : 0));
        c->track = std::clamp(m_origin.track + (yToTrack(pos.y()) - yToTrack(m_pressPos.y())), 0, kNumTracks - 1);
        m_project->notifyPlaylist();
        break;
    }
    case Drag::ResizeRight: {
        Clip* c = m_project->clip(m_dragClip);
        if (!c)
            break;
        const Tick end = snapTick(xToTick(pos.x()) + (m_snap > 1 ? m_snap / 2.0 : 0));
        Tick len = std::max<Tick>(std::max<Tick>(m_snap, kStepTicks), end - c->start);
        if (c->kind == ClipKind::Audio)
            len = std::min(len, m_project->sourceLength(*c) - c->offset);
        c->length = std::max<Tick>(kStepTicks, len);
        m_project->notifyPlaylist();
        break;
    }
    case Drag::ResizeLeft: {
        Clip* c = m_project->clip(m_dragClip);
        if (!c)
            break;
        Tick ns = snapTick(xToTick(pos.x()) + (m_snap > 1 ? m_snap / 2.0 : 0));
        const Tick end = m_origin.end();
        ns = std::clamp<Tick>(ns, m_origin.start - m_origin.offset, end - kStepTicks);
        c->start = ns;
        c->offset = m_origin.offset + (ns - m_origin.start);
        c->length = end - ns;
        m_project->notifyPlaylist();
        break;
    }
    default:
        break;
    }
}

void PlaylistView::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_drag == Drag::Ruler && !m_rulerMoved) {
        if (!m_engine->songMode())
            m_engine->setSongMode(true);
        m_engine->setPosition(std::max(0.0, xToTick(e->position().x())));
    }
    m_drag = Drag::None;
    m_dragClip = 0;
    m_dragPoint = -1;
    viewport()->update();
}

void PlaylistView::mouseDoubleClickEvent(QMouseEvent* e)
{
    const QPointF pos = e->position();
    if (pos.x() < kHeaderW && pos.y() > kRulerH) {
        const int t = yToTrack(pos.y());
        bool ok = false;
        const QString n = QInputDialog::getText(this, tr("Rename track"), tr("Name"), QLineEdit::Normal,
                                                m_project->tracks[size_t(t)].name, &ok);
        if (ok && !n.isEmpty()) {
            m_project->tracks[size_t(t)].name = n;
            m_project->notifyPlaylist();
        }
        return;
    }
    const int idx = clipAt(pos);
    if (idx < 0)
        return;
    const Clip c = m_project->clips[size_t(idx)];
    if (c.kind == ClipKind::Automation) {
        // Add a point at the clicked position.
        AutomationClip* a = m_project->automation(c.ref);
        if (!a)
            return;
        const QRectF r = clipRect(c);
        const qreal headH = std::min<qreal>(16, r.height() / 3);
        const QRectF content(r.left() + 2, r.top() + headH + 2, r.width() - 4, r.height() - headH - 4);
        AutoPoint pt;
        pt.t = Tick(double(c.offset) + (pos.x() - content.left()) / m_pxPerTick);
        pt.v = float(std::clamp((content.bottom() - pos.y()) / content.height(), 0.0, 1.0));
        a->points.push_back(pt);
        a->sortPoints();
        m_project->notifyAutomation();
    } else if (c.kind == ClipKind::Pattern) {
        m_project->setCurrentPattern(c.ref);
        if (!m_project->channels.empty())
            emit UiHub::instance()->pianoRollRequested(m_project->selectedChannel());
    }
}

void PlaylistView::contextMenuEvent(QContextMenuEvent* e)
{
    // Right-click edits are handled on press; offer a menu on track headers only.
    if (e->pos().x() >= kHeaderW || e->pos().y() < kRulerH)
        return;
    const int t = yToTrack(e->pos().y());
    Track& tr = m_project->tracks[size_t(t)];
    QMenu menu(this);
    menu.addSection(tr.name);
    menu.addAction(tr.muted ? QObject::tr("Unmute") : QObject::tr("Mute"), this, [this, t] {
        m_project->tracks[size_t(t)].muted = !m_project->tracks[size_t(t)].muted;
        m_project->notifyPlaylist();
    });
    menu.addAction(QObject::tr("Rename…"), this, [this, t] {
        bool ok = false;
        const QString n = QInputDialog::getText(this, QObject::tr("Rename track"), QObject::tr("Name"), QLineEdit::Normal,
                                                m_project->tracks[size_t(t)].name, &ok);
        if (ok && !n.isEmpty()) {
            m_project->tracks[size_t(t)].name = n;
            m_project->notifyPlaylist();
        }
    });
    menu.addAction(QObject::tr("Color…"), this, [this, t] {
        const QColor c = QColorDialog::getColor(m_project->tracks[size_t(t)].color, this);
        if (c.isValid()) {
            m_project->tracks[size_t(t)].color = c;
            m_project->notifyPlaylist();
        }
    });
    menu.addAction(icons::icon("trash", theme::danger), QObject::tr("Clear track"), this, [this, t] {
        auto& clips = m_project->clips;
        clips.erase(std::remove_if(clips.begin(), clips.end(), [t](const Clip& c) { return c.track == t; }), clips.end());
        m_project->notifyPlaylist();
    });
    menu.exec(e->globalPos());
}

void PlaylistView::wheelEvent(QWheelEvent* e)
{
    const int dy = e->angleDelta().y();
    if (e->modifiers() & Qt::ControlModifier) {
        zoomX(dy > 0 ? 1.2 : 1 / 1.2);
    } else if (e->modifiers() & Qt::AltModifier) {
        m_trackH = std::clamp(m_trackH + ((dy ? dy : e->angleDelta().x()) > 0 ? 6 : -6), 28, 140);
        updateScrollbars();
        viewport()->update();
    } else if (e->modifiers() & Qt::ShiftModifier) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - (dy ? dy : e->angleDelta().x()));
    } else {
        QAbstractScrollArea::wheelEvent(e);
    }
}

void PlaylistView::keyPressEvent(QKeyEvent* e)
{
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && !m_selected.empty()) {
        auto& clips = m_project->clips;
        clips.erase(std::remove_if(clips.begin(), clips.end(),
                                   [this](const Clip& c) {
                                       return std::find(m_selected.begin(), m_selected.end(), c.id) != m_selected.end();
                                   }),
                    clips.end());
        m_selected.clear();
        m_project->notifyPlaylist();
        return;
    }
    if (e->key() == Qt::Key_A && (e->modifiers() & Qt::ControlModifier)) {
        m_selected.clear();
        for (const auto& c : m_project->clips)
            m_selected.push_back(c.id);
        viewport()->update();
        return;
    }
    QAbstractScrollArea::keyPressEvent(e);
}

void PlaylistView::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasFormat(kClipMime) || e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void PlaylistView::dragMoveEvent(QDragMoveEvent* e)
{
    if (gridRect().contains(e->position()))
        e->acceptProposedAction();
    else
        e->ignore();
}

void PlaylistView::dropEvent(QDropEvent* e)
{
    const QPointF pos = e->position();
    const int track = yToTrack(pos.y());
    const Tick start = snapTick(xToTick(pos.x()));
    if (e->mimeData()->hasFormat(kClipMime)) {
        const QStringList parts = QString::fromUtf8(e->mimeData()->data(kClipMime)).split(QLatin1Char(':'));
        if (parts.size() == 2) {
            Clip probe;
            probe.kind = ClipKind(parts[0].toInt());
            probe.ref = parts[1].toUInt();
            Clip& c = m_project->addClip(probe.kind, probe.ref, track, start, m_project->sourceLength(probe));
            m_selected = {c.id};
        }
        e->acceptProposedAction();
        return;
    }
    int t = track;
    Tick s = start;
    for (const QUrl& u : e->mimeData()->urls()) {
        const QString path = u.toLocalFile();
        if (!SampleCache::isAudioFile(path))
            continue;
        SamplePtr smp = SampleCache::instance().load(path);
        if (!smp)
            continue;
        Channel* ch = m_project->addAudioChannel(smp);
        Clip probe;
        probe.kind = ClipKind::Audio;
        probe.ref = ch->id;
        Clip& c = m_project->addClip(ClipKind::Audio, ch->id, t, s, m_project->sourceLength(probe));
        m_selected = {c.id};
        m_picker->setBrush({ClipKind::Audio, ch->id});
        t = std::min(t + 1, kNumTracks - 1);
    }
    if (!m_engine->songMode())
        m_engine->setSongMode(true);
    e->acceptProposedAction();
}

// ================================================================ Playlist

Playlist::Playlist(Project* project, AudioEngine* engine, QWidget* parent) : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto* bar = new QWidget(this);
    bar->setFixedHeight(40);
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(10, 4, 10, 4);
    bl->setSpacing(8);
    auto* help = new QLabel(tr("Click: paint selected clip · Drag: move · Ctrl+drag: copy · Edges: resize · Right: delete · "
                               "Ruler: click to locate, drag to loop"),
                            bar);
    help->setStyleSheet(QStringLiteral("color:%1; font-size:8pt;").arg(theme::textMute.name()));
    bl->addWidget(help, 1);
    auto* snapIcon = new QLabel(bar);
    snapIcon->setPixmap(icons::icon("magnet").pixmap(16, 16));
    bl->addWidget(snapIcon);
    m_snap = new QComboBox(bar);
    m_snap->addItem(tr("Bar"), int(kBarTicks));
    m_snap->addItem(tr("Beat"), int(kBeatTicks));
    m_snap->addItem(tr("1/2 beat"), int(kBeatTicks / 2));
    m_snap->addItem(tr("Step"), int(kStepTicks));
    m_snap->addItem(tr("None"), 1);
    m_snap->setCurrentIndex(1);
    bl->addWidget(m_snap);
    auto* zout = new QToolButton(bar);
    zout->setText(QStringLiteral("−"));
    auto* zin = new QToolButton(bar);
    zin->setText(QStringLiteral("+"));
    bl->addWidget(zout);
    bl->addWidget(zin);
    lay->addWidget(bar);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setHandleWidth(2);
    auto* pickScroll = new QWidget(split);
    auto* pl = new QVBoxLayout(pickScroll);
    pl->setContentsMargins(0, 0, 0, 0);
    m_picker = new ClipPicker(project, pickScroll);
    pl->addWidget(m_picker);
    pl->addStretch(1);
    pickScroll->setAutoFillBackground(true);
    QPalette pal = pickScroll->palette();
    pal.setColor(QPalette::Window, theme::bg1);
    pickScroll->setPalette(pal);
    m_view = new PlaylistView(project, engine, m_picker, split);
    split->addWidget(pickScroll);
    split->addWidget(m_view);
    split->setStretchFactor(1, 1);
    split->setSizes({160, 900});
    lay->addWidget(split, 1);

    connect(m_snap, &QComboBox::currentIndexChanged, this, [this](int i) { m_view->setSnap(m_snap->itemData(i).toInt()); });
    connect(zin, &QToolButton::clicked, this, [this] { m_view->zoomX(1.25); });
    connect(zout, &QToolButton::clicked, this, [this] { m_view->zoomX(0.8); });
}

} // namespace guiro
