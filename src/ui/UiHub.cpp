#include "ui/UiHub.h"
#include "core/Project.h"

namespace guiro {

UiHub* UiHub::instance()
{
    static UiHub* hub = new UiHub;
    return hub;
}

void UiHub::init(Project* project, AudioEngine* engine)
{
    m_project = project;
    m_engine = engine;
    m_timer.setInterval(16);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &UiHub::frame);
    m_timer.start();
    connect(this, &UiHub::paramEdited, project, [project] { project->setModified(true); });
}

} // namespace guiro
