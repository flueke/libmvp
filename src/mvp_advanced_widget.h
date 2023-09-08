#ifndef UUID_e3b4cb4d_7f13_4aa9_b9aa_298fe9c7a134
#define UUID_e3b4cb4d_7f13_4aa9_b9aa_298fe9c7a134

#include <QWidget>

class QSpinBox;

namespace Ui
{
  class MvpAdvancedWidget;
}

namespace mesytec
{
namespace mvp
{

class Address;

class MvpAdvancedWidget: public QWidget
{
  Q_OBJECT
  signals:
    void sig_dump_to_console();
    void sig_save_to_file(const QString &);
    void sig_load_from_file(const QString &);
    void sig_boot(uchar);                   // area
    void sig_nop_recovery();
    void sig_erase_section(uchar, uchar);  // area, section
    void sig_read_hardware_id();
    void sig_keys_info();
    void sig_manage_keys();
    void sig_mdpp16_cal_dump_to_console();
    void sig_mdpp16_cal_save_to_file(const QString &);
    void sig_mdpp32_cal_dump_to_console();
    void sig_mdpp32_cal_save_to_file(const QString &);

  public:
    explicit MvpAdvancedWidget(QWidget *parent=0);

    Address get_start_address() const;
    Address get_end_address() const;
    size_t get_len_bytes() const;
    uchar get_selected_area() const;
    uchar get_selected_section() const;
    void set_start_address(const Address &a);
    void set_end_address(const Address &a);

  private slots:
    void address_spin_value_changed();
    void on_spin_len_valueChanged(int);
    void on_rb_dec_toggled(bool);
    void on_pb_boot_clicked();
    void on_pb_save_to_file_clicked();
    void on_pb_load_from_file_clicked();
    void on_pb_erase_section_clicked();
    void slt_mdpp16_cal_save_to_file();
    void slt_mdpp32_cal_save_to_file();

  private:
    void update_page_display();

    Ui::MvpAdvancedWidget *ui;

    QList<QSpinBox *> m_hex_dec_spins;
    QList<QSpinBox *> m_address_spins;
};

} // ns mvp
} // ns mesytec

#endif
