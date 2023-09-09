#include "serial_port_flash.h"

namespace mesytec::mvp
{

void SerialPortFlash::recover(size_t tries)
{
  std::exception_ptr last_nop_exception;

  qDebug() << "begin recover(): tries =" << tries;

  for (size_t n=0; n<tries; ++n) {
    try {
      auto data = read_available(constants::recover_timeout_ms);
      qDebug() << "recover(): read_available():" << format_bytes(data);
    } catch (const ComError &e) {
      qDebug() << "ignoring ComError from read_available()";
    }

    try {
      nop();
      return;
    } catch (const std::exception &e) {
      last_nop_exception = std::current_exception();
      qDebug() << "Flash::recover(): exception from NOP" << e.what();
    }
  }

  if (last_nop_exception)
    std::rethrow_exception(last_nop_exception);
  else
    throw std::runtime_error("NOP recovery failed for an unknown reason");
}

void SerialPortFlash::write_page(const Address &addr, uchar section,
  const gsl::span<uchar> data, int timeout_ms)
{
  //if (addr[0] != 0)
  //  throw std::invalid_argument("write_page: address is not page aligned (a0!=0)");

  const auto sz = data.size();

  if (sz == 0)
    throw std::invalid_argument("write_page: empty data given");

  if (sz > constants::page_size)
    throw std::invalid_argument("write_page: data size > page size");

  const bool use_verbose = false;

  maybe_set_verbose(use_verbose);
  maybe_enable_write();

  uchar len_byte(sz == constants::page_size ? 0 : sz); // 256 encoded as 0
  m_wbuf = { opcodes::WRF, addr[0], addr[1], addr[2], section, len_byte };

  //qDebug() << "WRF: addr=" << addr << ", si =" << section << ", len =" << len_byte;

  write_instruction(m_wbuf);
  write(data, timeout_ms);

  emit data_written(span_to_qvector(data));

  //qDebug() << "WRF data written:" << span_to_qvector(data);

  if (use_verbose) {
    try {
      //QVector<uchar> rbuf(use_verbose ? 4 + data.size() : 4);
      //read(gsl::span(rbuf));
      auto rbuf = read_available();
      qDebug() << "write_page: try read yielded" << rbuf;
    } catch (const std::exception &e) {
      qDebug() << "write_page: try read raised" << e.what();
    }
  }
}

void SerialPortFlash::read_page(const Address &addr, uchar section,
  gsl::span<uchar> dest, int timeout_ms)
{
  //if (addr[0] != 0)
  //  throw std::invalid_argument("read_page: address is not page aligned (a0!=0)");

  qDebug() << "read_page: addr =" << addr << ", section =" << static_cast<uint32_t>(section)
    << ", dest.size() =" << dest.size() << ", timeout_ms =" << timeout_ms;

  auto len = dest.size();

  if (len == 0)
    throw std::invalid_argument("read_page: len == 0");

  if (len > constants::page_size)
    throw std::invalid_argument("read_page: len > page size");

  maybe_set_verbose(false);

  uchar len_byte(len == constants::page_size ? 0 : len); // 256 encoded as 0

  m_wbuf = { opcodes::REF, addr[0], addr[1], addr[2], section, len_byte };
  write_instruction(m_wbuf);
  read(dest, timeout_ms);
}

void SerialPortFlash::write_instruction(const gsl::span<uchar> data, int timeout_ms)
{
  write(data, timeout_ms);

  uchar opcode(*std::begin(data));

  if (opcode != opcodes::WRF && m_write_enabled) {
    // any instruction except WRF (and EFW) unsets write enable
    qDebug() << "clearing cached write_enable flag (instruction ="
      << op_to_string(opcode) << "!= WRF)";
    m_write_enabled = false;
  }

  //qDebug() << "instruction written:" << format_bytes(span_to_qvector(data));

  emit instruction_written(span_to_qvector(data));
}

void SerialPortFlash::read_response(gsl::span<uchar> dest, int timeout_ms)
{
  read(dest, timeout_ms);
  emit response_read(span_to_qvector(dest));
}

void SerialPortFlash::write(const gsl::span<uchar> data, int timeout_ms)
{
  qint64 res = m_port->write(reinterpret_cast<const char *>(data.data()), data.size());

  if (res != static_cast<qint64>(data.size())
      || !m_port->waitForBytesWritten(timeout_ms)) {
    throw make_com_error(m_port);
  }
}

void SerialPortFlash::read(gsl::span<uchar> dest, int timeout_ms)
{
  const auto len = dest.size();

  if (len == 0)
    throw std::invalid_argument("read: destination size == 0");

  size_t bytes_read = 0;

  while (bytes_read < len) {
    if (!m_port->bytesAvailable() && !m_port->waitForReadyRead(timeout_ms))
      throw make_com_error(m_port);

    qint64 to_read = std::min(m_port->bytesAvailable(), static_cast<qint64>(len - bytes_read));
    qint64 res     = m_port->read(reinterpret_cast<char *>(dest.begin()) + bytes_read, to_read);

    if (res != to_read)
      throw make_com_error(m_port);

    bytes_read += res;
  }
}

QVector<uchar> SerialPortFlash::read_available(int timeout_ms)
{
  if (!m_port->bytesAvailable() && !m_port->waitForReadyRead(timeout_ms))
    throw make_com_error(m_port);

  QVector<uchar> ret(m_port->bytesAvailable());

  qint64 res = m_port->read(reinterpret_cast<char *>(ret.data()), ret.size());

  if (res != ret.size())
    throw make_com_error(m_port);

  if (m_port->bytesAvailable()) {
    qDebug() << "read_available: there are still" << m_port->bytesAvailable() << " bytes available";
  }

  return ret;
}
}