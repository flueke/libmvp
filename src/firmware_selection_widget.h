#ifndef SRC_FIRMWARE_SELECTION_WIDGET_H_
#define SRC_FIRMWARE_SELECTION_WIDGET_H_

#include <memory>
#include <QWidget>

namespace mesytec::mvp
{

class FirmwareSelectionWidget: public QWidget
{
    Q_OBJECT
    signals:
        void firmware_file_changed(const QString &);
        void area_index_changed(int);

    public:
        FirmwareSelectionWidget(QWidget *parent = nullptr);
        ~FirmwareSelectionWidget() override;

        QString get_firmware_file() const;
        void set_firmware_file(const QString &filename);
        int get_area_index() const;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif // SRC_FIRMWARE_SELECTION_WIDGET_H_