#pragma once

#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>

namespace guiro {

// How the normalized 0..1 range maps to the plain value.
enum class ParamCurve { Linear, Log, Square };

// How a value is presented to the user.
enum class ParamUnit { Raw, Percent, Gain, Hz, Pan, Semitones, Ms, Choice };

// An automatable, lock-free parameter. The audio thread reads value(),
// the UI and automation write it. All values are plain (not normalized).
class Param {
public:
    Param(QString name, float min, float max, float def,
          ParamCurve curve = ParamCurve::Linear, ParamUnit unit = ParamUnit::Raw);

    const QString& name() const { return m_name; }
    float min() const { return m_min; }
    float max() const { return m_max; }
    float defaultValue() const { return m_def; }
    ParamUnit unit() const { return m_unit; }

    float value() const { return m_value.load(std::memory_order_relaxed); }
    void setValue(float v);
    void reset() { setValue(m_def); }

    float normalized() const { return toNormalized(value()); }
    void setNormalized(float n) { setValue(fromNormalized(n)); }
    float toNormalized(float plain) const;
    float fromNormalized(float n) const;

    QString displayText() const { return format(value()); }
    QString format(float plain) const;

    // Stable path used for automation targets and serialization, e.g. "mixer/3/cutoff".
    const QString& path() const { return m_path; }
    void setPath(const QString& p) { m_path = p; }

    void setChoices(QStringList c) { m_choices = std::move(c); }
    const QStringList& choices() const { return m_choices; }

    // Set while an automation clip drives this param, so the UI can show it.
    std::atomic<bool> automated{false};

private:
    QString m_name;
    QString m_path;
    float m_min, m_max, m_def;
    ParamCurve m_curve;
    ParamUnit m_unit;
    QStringList m_choices;
    std::atomic<float> m_value;
};

using ParamPtr = std::shared_ptr<Param>;

// Factories for the parameter flavours used throughout the app.
ParamPtr makeVolumeParam(const QString& name = QStringLiteral("Volume"), float def = 0.8f);
ParamPtr makePanParam();
ParamPtr makeCutoffParam();
ParamPtr makeResonanceParam();
ParamPtr makeFilterTypeParam();
ParamPtr makePitchParam(float range = 24.f);

float gainToDb(float g);

} // namespace guiro
