#pragma once

#include <QWidget>

namespace guiro {

// Tiny master-output oscilloscope for the top bar.
class Scope : public QWidget {
    Q_OBJECT
public:
    explicit Scope(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    float m_buf[512] = {};
};

} // namespace guiro
