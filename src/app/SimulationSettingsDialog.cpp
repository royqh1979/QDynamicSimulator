#include "SimulationSettingsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

using namespace qds::core;

namespace {

QDoubleSpinBox *timeEditor(QWidget *parent, const QString &name, double value, bool positive)
{
    auto *editor = new QDoubleSpinBox(parent);
    editor->setObjectName(name);
    editor->setDecimals(12);
    editor->setRange(positive ? 1e-12 : -1e12, 1e12);
    editor->setValue(value);
    editor->setKeyboardTracking(false);
    return editor;
}

} // namespace

SimulationSettingsDialog::SimulationSettingsDialog(const SimulationSettings &settings, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Simulation settings"));
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_startTime = timeEditor(this, QStringLiteral("startTime"), settings.startTime(), false);
    m_stopTime = timeEditor(this, QStringLiteral("stopTime"), settings.stopTime(), false);
    m_dt = timeEditor(this, QStringLiteral("dt"), settings.dt(), true);
    m_saveInterval = timeEditor(this, QStringLiteral("saveInterval"), settings.saveInterval(), true);
    m_method = new QComboBox(this);
    m_method->setObjectName(QStringLiteral("integrationMethod"));
    m_method->addItem(QStringLiteral("Euler"));
    m_method->addItem(QStringLiteral("RK4"));
    m_method->setCurrentIndex(settings.integrationMethod() == IntegrationMethod::RungeKutta4 ? 1 : 0);
    m_timeUnit = new QComboBox(this);
    m_timeUnit->setObjectName(QStringLiteral("timeUnit"));
    m_timeUnit->setEditable(true);
    m_timeUnit->addItems({QString(), QStringLiteral("Second"), QStringLiteral("Minute"), QStringLiteral("Hour"),
        QStringLiteral("Day"), QStringLiteral("Month"), QStringLiteral("Year")});
    m_timeUnit->setCurrentText(settings.timeUnit());
    form->addRow(tr("Start time"), m_startTime);
    form->addRow(tr("Stop time"), m_stopTime);
    form->addRow(tr("DT"), m_dt);
    form->addRow(tr("SAVEPER"), m_saveInterval);
    form->addRow(tr("Integration method"), m_method);
    form->addRow(tr("Time unit"), m_timeUnit);
    layout->addLayout(form);
    m_error = new QLabel(this);
    m_error->setObjectName(QStringLiteral("settingsError"));
    m_error->setWordWrap(true);
    m_error->setStyleSheet(QStringLiteral("color: #b91c1c"));
    m_error->hide();
    layout->addWidget(m_error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SimulationSettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SimulationSettingsDialog::reject);

    m_previousDt = settings.dt();
    connect(m_dt, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_saveInterval->value() == m_previousDt)
            m_saveInterval->setValue(value);
        m_previousDt = value;
        m_error->hide();
    });
    connect(m_startTime, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() { m_error->hide(); });
    connect(m_stopTime, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() { m_error->hide(); });
    connect(m_saveInterval, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() { m_error->hide(); });
}

SimulationSettings SimulationSettingsDialog::settings() const
{
    return {m_startTime->value(), m_stopTime->value(), m_dt->value(), m_saveInterval->value(),
        m_method->currentIndex() == 1 ? IntegrationMethod::RungeKutta4 : IntegrationMethod::Euler,
        m_timeUnit->currentText()};
}

void SimulationSettingsDialog::accept()
{
    const SimulationSettings value = settings();
    if (!value.isValid()) {
        m_error->setText(tr("Invalid settings. Check time bounds, DT, and SAVEPER."));
        m_error->show();
        return;
    }
    QDialog::accept();
}
