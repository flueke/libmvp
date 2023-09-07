#include "firmware_selection_widget.h"

#include <QComboBox>
#include <QFormLayout>
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
};

FirmwareSelectionWidget::FirmwareSelectionWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->le_filename_ = new QLineEdit;
    auto pb_open_file = new QPushButton;
    pb_open_file->setIcon(QIcon(":/document-open.png"));
    auto firmwareLayout = new QHBoxLayout;
    firmwareLayout->addWidget(d->le_filename_);
    firmwareLayout->addWidget(pb_open_file);
    firmwareLayout->setStretch(0, 1);
    d->combo_area_ = new QComboBox;
    for (int i=0; i<4; ++i)
        d->combo_area_->addItem(QString::number(i), i);
    auto layout = new QFormLayout(this);
    layout->addRow("Firmware File/Directory", firmwareLayout);
    layout->addRow("Flash Target Area", d->combo_area_);

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

}