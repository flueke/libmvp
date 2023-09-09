#ifndef EXTERNAL_LIBMVP_SRC_SERIAL_PORT_FLASH_H_
#define EXTERNAL_LIBMVP_SRC_SERIAL_PORT_FLASH_H_

#include "flash.h"

namespace mesytec::mvp
{

  class SerialPortFlash: public FlashInterface
  {
    Q_OBJECT
    public:
      SerialPortFlash(QObject *parent=nullptr)
        : FlashInterface(parent)
      {}

      SerialPortFlash(gsl::not_null<QIODevice *> device, QObject *parent=nullptr)
        : FlashInterface(parent)
        , m_port(device)
      {}

      void set_port(gsl::not_null<QIODevice *> device) { m_port = device; }
      QIODevice *get_port() const { return m_port; }

      void write_instruction(const gsl::span<uchar> data,
        int timeout_ms = constants::default_timeout_ms) override;

      void read_response(gsl::span<uchar> dest,
        int timeout_ms = constants::default_timeout_ms) override;

      void write_page(const Address &address, uchar section,
        const gsl::span<uchar> data, int timeout_ms = constants::data_timeout_ms) override;

      void read_page(const Address &address, uchar section, gsl::span<uchar> dest,
        int timeout_ms = constants::data_timeout_ms) override;

      void recover(size_t tries=default_recover_tries) override;

    protected:
      QVector<uchar> read_available(
        int timeout_ms = constants::default_timeout_ms);

      void write(const gsl::span<uchar> data,
        int timeout_ms = constants::default_timeout_ms);

      void read(gsl::span<uchar> dest,
        int timeout_ms = constants::default_timeout_ms);

    private:
      QIODevice *m_port = nullptr;
  };

}

#endif // EXTERNAL_LIBMVP_SRC_SERIAL_PORT_FLASH_H_