#ifndef SRC_FIRMWARE_SELECTION_WIDGET_H_
#define SRC_FIRMWARE_SELECTION_WIDGET_H_

#include <memory>
#include <QWidget>

namespace mesytec::mvp
{

enum FirmwareSteps
{
    Step_Erase   = 1u << 0,
    Step_Program = 1u << 1,
    Step_Verify  = 1u << 2,
};

inline FirmwareSteps operator|(FirmwareSteps a, FirmwareSteps b)
{
    return static_cast<FirmwareSteps>(
        static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

inline FirmwareSteps &operator|=(FirmwareSteps &a, FirmwareSteps b)
{
    a = a | b;
    return a;
}

class FirmwareSelectionWidget: public QWidget
{
    Q_OBJECT
    signals:
        void firmware_file_changed(const QString &);
        void area_index_changed(int);
        void start_button_clicked();

    public:
        FirmwareSelectionWidget(QWidget *parent = nullptr);
        ~FirmwareSelectionWidget() override;

        QString get_firmware_file() const;
        void set_firmware_file(const QString &filename);
        int get_area_index() const;

        FirmwareSteps get_firmware_steps() const;

    public slots:
        void set_area_select_enabled(bool enabled);
        void set_start_button_enabled(bool enabled);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif // SRC_FIRMWARE_SELECTION_WIDGET_H_
