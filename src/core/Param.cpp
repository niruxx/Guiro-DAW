#include "core/Param.h"
#include "core/Types.h"

#include <algorithm>
#include <cmath>

namespace guiro {

namespace {
thread_local bool t_audioThread = false;
}

bool isAudioThread() { return t_audioThread; }
void setAudioThread(bool b) { t_audioThread = b; }

Param::Param(QString name, float min, float max, float def, ParamCurve curve, ParamUnit unit)
    : m_name(std::move(name)), m_min(min), m_max(max), m_def(def), m_curve(curve), m_unit(unit), m_value(def)
{
}

void Param::setValue(float v)
{
    if (!std::isfinite(v))
        return;
    v = std::clamp(v, m_min, m_max);
    if (m_unit == ParamUnit::Choice)
        v = std::round(v);
    m_value.store(v, std::memory_order_relaxed);
}

float Param::toNormalized(float plain) const
{
    plain = std::clamp(plain, m_min, m_max);
    if (m_max <= m_min)
        return 0.f;
    switch (m_curve) {
    case ParamCurve::Log:
        return std::log(plain / m_min) / std::log(m_max / m_min);
    case ParamCurve::Square:
        return std::sqrt((plain - m_min) / (m_max - m_min));
    case ParamCurve::Linear:
    default:
        return (plain - m_min) / (m_max - m_min);
    }
}

float Param::fromNormalized(float n) const
{
    n = std::clamp(n, 0.f, 1.f);
    switch (m_curve) {
    case ParamCurve::Log:
        return m_min * std::pow(m_max / m_min, n);
    case ParamCurve::Square:
        return m_min + (m_max - m_min) * n * n;
    case ParamCurve::Linear:
    default:
        return m_min + (m_max - m_min) * n;
    }
}

float gainToDb(float g)
{
    return g <= 0.00001f ? -100.f : 20.f * std::log10(g);
}

QString Param::format(float v) const
{
    switch (m_unit) {
    case ParamUnit::Percent:
        return QString::number(int(std::round(v * 100.f))) + QStringLiteral("%");
    case ParamUnit::Gain: {
        float db = gainToDb(v);
        if (db <= -99.f)
            return QStringLiteral("-inf dB");
        return QString::number(db, 'f', 1) + QStringLiteral(" dB");
    }
    case ParamUnit::Hz:
        if (v >= 1000.f)
            return QString::number(v / 1000.f, 'f', v >= 10000.f ? 1 : 2) + QStringLiteral(" kHz");
        return QString::number(int(std::round(v))) + QStringLiteral(" Hz");
    case ParamUnit::Pan: {
        int p = int(std::round(v * 100.f));
        if (p == 0)
            return QStringLiteral("Centered");
        return p < 0 ? QStringLiteral("%1% left").arg(-p) : QStringLiteral("%1% right").arg(p);
    }
    case ParamUnit::Semitones: {
        float r = std::round(v * 10.f) / 10.f;
        return (r > 0 ? QStringLiteral("+") : QString()) + QString::number(r) + QStringLiteral(" st");
    }
    case ParamUnit::Ms:
        if (v >= 1000.f)
            return QString::number(v / 1000.f, 'f', 2) + QStringLiteral(" s");
        return QString::number(int(std::round(v))) + QStringLiteral(" ms");
    case ParamUnit::Choice: {
        int i = int(std::round(v - m_min));
        if (i >= 0 && i < m_choices.size())
            return m_choices[i];
        return QString::number(i);
    }
    case ParamUnit::Raw:
    default:
        return QString::number(v, 'f', 2);
    }
}

ParamPtr makeVolumeParam(const QString& name, float def)
{
    return std::make_shared<Param>(name, 0.f, 1.25f, def, ParamCurve::Square, ParamUnit::Gain);
}

ParamPtr makePanParam()
{
    return std::make_shared<Param>(QStringLiteral("Pan"), -1.f, 1.f, 0.f, ParamCurve::Linear, ParamUnit::Pan);
}

ParamPtr makeCutoffParam()
{
    return std::make_shared<Param>(QStringLiteral("Cutoff"), 20.f, 20000.f, 20000.f, ParamCurve::Log, ParamUnit::Hz);
}

ParamPtr makeResonanceParam()
{
    return std::make_shared<Param>(QStringLiteral("Resonance"), 0.f, 1.f, 0.1f, ParamCurve::Linear, ParamUnit::Percent);
}

ParamPtr makeFilterTypeParam()
{
    auto p = std::make_shared<Param>(QStringLiteral("Filter"), 0.f, 3.f, 0.f, ParamCurve::Linear, ParamUnit::Choice);
    p->setChoices({QStringLiteral("Off"), QStringLiteral("LP"), QStringLiteral("HP"), QStringLiteral("BP")});
    return p;
}

ParamPtr makePitchParam(float range)
{
    return std::make_shared<Param>(QStringLiteral("Pitch"), -range, range, 0.f, ParamCurve::Linear, ParamUnit::Semitones);
}

} // namespace guiro
