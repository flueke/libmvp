#include "firmware_selection_widget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>

#include "file_dialog.h"

namespace mesytec::mvp
{

struct FirmwareSelectionWidget::Private
{
    QLineEdit *le_filename_;
    QComboBox *combo_area_;
    QGroupBox *gb_steps_;
    QCheckBox *cb_erase_;
    QCheckBox *cb_program_;
    QCheckBox *cb_verify_;
    QPushButton *pb_start_;
};

FirmwareSelectionWidget::FirmwareSelectionWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->le_filename_ = new QLineEdit;
    d->le_filename_->setMinimumWidth(150);
    auto pb_open_file = new QPushButton;
    pb_open_file->setIcon(QIcon(":/document-open.png"));
    auto firmwareLayout = new QHBoxLayout;
    firmwareLayout->addWidget(d->le_filename_);
    firmwareLayout->addWidget(pb_open_file);
    firmwareLayout->setStretch(0, 1);
    d->combo_area_ = new QComboBox;
    for (int i=0; i<4; ++i)
        d->combo_area_->addItem(QString::number(i), i);

    d->gb_steps_ = new QGroupBox("Steps");
    d->cb_erase_ = new QCheckBox("Erase");
    d->cb_program_ = new QCheckBox("Program");
    d->cb_verify_ = new QCheckBox("Verify");
    d->cb_erase_->setChecked(true);
    d->cb_program_->setChecked(true);
    auto stepsLayout = new QVBoxLayout(d->gb_steps_);
    stepsLayout->setContentsMargins(0, 0, 0, 0);
    stepsLayout->addWidget(d->cb_erase_);
    stepsLayout->addWidget(d->cb_program_);
    stepsLayout->addWidget(d->cb_verify_);
    stepsLayout->addStretch(1);

    d->pb_start_ = new QPushButton("Start");
    auto startLayout = new QHBoxLayout;
    startLayout->addStretch(1);
    startLayout->addWidget(d->pb_start_);
    startLayout->addStretch(1);

    auto layout = new QFormLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addRow("Firmware File/Directory", firmwareLayout);
    layout->addRow("Flash Target Area", d->combo_area_);
    layout->addRow(d->gb_steps_);
    layout->addRow(startLayout);

    connect(d->combo_area_, qOverload<int>(&QComboBox::currentIndexChanged),
        this, &FirmwareSelectionWidget::area_index_changed);

    auto on_pb_open_file_clicked = [this]
    {
        QString dir = QStandardPaths::standardLocations(
                QStandardPaths::DocumentsLocation).value(0, QString());

        QSettings settings;

        dir = settings.value("directories/firmware", dir).toString();
        FileDialog file_dialog;
        file_dialog.setDirectory(dir);

        if (file_dialog.exec() != QDialog::Accepted)
            return;

        const QString filename = file_dialog.get_selected_file_or_dir();

        if (!filename.isEmpty()) {
            QFileInfo fi(filename);
            settings.setValue("directories/firmware", fi.path());
        }

        set_firmware_file(filename);
    };

    connect(pb_open_file, &QPushButton::clicked,
        this, on_pb_open_file_clicked);
}

FirmwareSelectionWidget::~FirmwareSelectionWidget()
{
}

QString FirmwareSelectionWidget::get_firmware_file() const
{
    return d->le_filename_->text();
}

void FirmwareSelectionWidget::set_firmware_file(const QString &filename)
{
  d->le_filename_->setText(filename);
  emit firmware_file_changed(filename);
}

int FirmwareSelectionWidget::get_area_index() const
{
    return d->combo_area_->currentIndex();
}

FirmwareSteps FirmwareSelectionWidget::get_firmware_steps() const
{
    FirmwareSteps result = {};
    if (d->cb_erase_->isChecked())
        result |= FirmwareSteps::Step_Erase;
    if (d->cb_program_->isChecked())
        result |= FirmwareSteps::Step_Program;
    if (d->cb_verify_->isChecked())
        result |= FirmwareSteps::Step_Verify;
    return result;
}
}
