#include "firmware_selection_widget.h"
#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>

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
}

FirmwareSelectionWidget::~FirmwareSelectionWidget()
{
}

QString FirmwareSelectionWidget::get_firmware_file() const
{
    return d->le_filename_->text();
}

int FirmwareSelectionWidget::get_area_index() const
{
    return d->combo_area_->currentIndex();
}

}