#ifndef UUID_38aac333_3951_4b01_abe7_0c90c425b0ec
#define UUID_38aac333_3951_4b01_abe7_0c90c425b0ec

#include <QEventLoop>
#include <QFutureWatcher>
#include <QListWidget>
#include <QMainWindow>
#include <QProgressDialog>
#include <QSerialPortInfo>
#include <QTabWidget>

#include <firmware_selection_widget.h>
#include <mvlc_mvp_connector.h>
#include <mvlc_mvp_flash.h>
#include <serial_port_flash.h>
#include <serial_port_mvp_connector.h>

#include "flash.h"
#include "port_helper.h"
#include "firmware.h"
#include "firmware_ops.h"

class QCheckBox;
class QCloseEvent;
class QGroupBox;
class QSerialPort;
class QTextBrowser;

namespace Ui
{
  class MVPLabGui;
}

namespace mesytec
{
namespace mvp
{

static const int port_refresh_interval_ms = 1000;

class FlashWidget;
class MvpAdvancedWidget;

class MVPLabGui: public QMainWindow
{
  Q_OBJECT
  public:
    explicit MVPLabGui(QWidget *parent=0);
    virtual ~MVPLabGui();

    QTextBrowser *getLogview();

  protected:
    virtual void closeEvent(QCloseEvent *event) override;

  private slots:
    // firmware
    void _on_start_button_clicked();
    void _on_firmware_file_changed(const QString &);

    void write_firmware();
    void handle_keys();
    void mvlc_connect();
    void mvlc_scanbus();
    void onActiveConnectorChanged();

    // execution
    void handle_future_started();
    void handle_future_finished();

    // misc
    void append_to_log(const QString &s);
    void on_actionAbout_triggered();
    void on_actionAbout_Qt_triggered();
    void on_actionShowAdvanced_toggled(bool checked);

    // advanced
    void adv_dump_to_console();
    void adv_save_to_file(const QString &filename);
    void adv_load_from_file(const QString &filename);
    void adv_boot(uchar area);
    void adv_nop_recovery();
    void adv_erase_section(uchar area, uchar section);
    void adv_read_hardware_id();
    KeysInfo read_device_keys();
    void adv_keys_info();
    void adv_manage_keys();
    void adv_mdpp16_cal_dump_to_console();
    void adv_mdpp16_cal_save_to_file(const QString &filename);
    void adv_mdpp32_cal_dump_to_console();
    void adv_mdpp32_cal_save_to_file(const QString &filename);

  private:
    void append_to_log_queued(const QString &s);
    void _show_logview_context_menu(const QPoint &);

    QVector<uchar> perform_memory_dump();
    QVector<uchar> read_mdpp16_calibration_data();
    QVector<uchar> read_mdpp32_calibration_data();

    Ui::MVPLabGui *ui;

    QObject *m_object_holder;

    // FlashInterface instance: SerialPortFlash(serialPort) | MvlcMvpFlash(mvlc, vmeAddress)
    // Firmware Input + Target Area + steps to perform
    // MvpAdvancedWidget

    // ConnectWidget up top
    // FirmwareWidget with steps and start button (normal firmeare update operation)
    // advanced widget
    // optional connector special widget
    // column 2: logview
    SerialPortMvpConnector *serialPortConnector_;
    MvlcMvpConnector *mvlcConnector_;
    MvpConnectorInterface *getActiveConnector();
    std::vector<MvpConnectorInterface *> getConnectors() { return { serialPortConnector_, mvlcConnector_ }; }

    QFutureWatcher<void> m_fw;
    QEventLoop m_loop;
    bool m_quit = false;

    QTabWidget *tabs_connectors_;
    FirmwareSelectionWidget *firmwareSelectWidget_;
    MvpAdvancedWidget *m_advancedwidget;
    QGroupBox *advanced_widget_gb_;
    QProgressBar *m_progressbar;
    FirmwareArchive m_firmware;
};

class KeySelectionDialog: public QDialog
{
    Q_OBJECT
    public:
        KeySelectionDialog(const KeyList &keys, QWidget *parent = nullptr);

        KeyList get_selected_keys() const;

    private:
        void populate();

        KeyList m_keys;
        QListWidget *m_listWidget;
};

} // ns mvp
} // ns mesytec

#endif
