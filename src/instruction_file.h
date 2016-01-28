#ifndef UUID_baaf48de_914a_42f0_bf60_ff09aecd543a
#define UUID_baaf48de_914a_42f0_bf60_ff09aecd543a

#include "flash.h"

namespace mesytec
{
namespace mvp
{

struct Instruction
{
  enum class Type { text, binary };
  typedef QVector<uchar> data_type;
  Type type;
  Address address;
  data_type data;
};

class InstructionFileParseError: public std::runtime_error
{
  public:
    InstructionFileParseError(int line_number, const QString &line,
        const QString &message=QString())
      : std::runtime_error(message.toStdString())
      , m_line_number(line_number)
      , m_line(line)
      , m_message(message)
  {}

  int line_number() const { return m_line_number; }
  QString line() const { return m_line; }
  QString message() const { return m_message; }

  QString to_string() const
  {
    return QString("%1: line_number=%2, line=%3")
      .arg(what())
      .arg(line_number())
      .arg(line());
  }

  private:
    size_t m_line_number;
    QString m_line;
    QString m_message;
};

QVector<Instruction> parse_instruction_file(QTextStream &stream);

} // ns mvp
} // ns mesytec

#endif
