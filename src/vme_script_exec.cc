#include "vme_script_exec.h"

#include <QDebug>
#include <QThread>

#include "mvlc/mvlc_vme_controller.h"
#include "vmusb.h"

namespace vme_script
{

ResultList run_script(
    VMEController *controller, const VMEScript &script,
    LoggerFun logger, const run_script_options::Flag &options)
{
    return run_script(controller, script, logger, logger, options);
}

ResultList run_script(
    VMEController *controller, const VMEScript &script,
    LoggerFun logger, LoggerFun error_logger,
    const run_script_options::Flag &options)
{
    int cmdNumber = 1;
    ResultList results;
    std::unique_lock<mesytec::mvlc::Mutex> mvlcErrorPollerSuspendMutex;

    if (auto mvlcCtrl = qobject_cast<mesytec::mvme_mvlc::MVLC_VMEController *>(controller))
    {
        auto mvlc = mvlcCtrl->getMVLC();
        mvlcErrorPollerSuspendMutex = mvlc.suspendStackErrorPolling();
    }

    for (auto cmd: script)
    {
        if (cmd.type != CommandType::Invalid)
        {
            if (!cmd.warning.isEmpty())
            {
                logger(QString("Warning: %1 on line %2 (cmd=%3)")
                       .arg(cmd.warning)
                       .arg(cmd.lineNumber)
                       .arg(to_string(cmd.type))
                      );
            }

            auto tStart = QDateTime::currentDateTime();

            qDebug() << __FUNCTION__
                << tStart << "begin run_command" << cmdNumber << "of" << script.size();

            auto result = run_command(controller, cmd, logger);

            auto tEnd = QDateTime::currentDateTime();
            results.push_back(result);

            qDebug() << __FUNCTION__
                << tEnd
                << "  " << cmdNumber << "of" << script.size() << ":"
                << format_result(result)
                << "duration:" << tStart.msecsTo(tEnd) << "ms";

            if (options & run_script_options::LogEachResult)
            {
                if (result.error.isError())
                    error_logger(format_result(result));
                else
                    logger(format_result(result));
            }

            if ((options & run_script_options::AbortOnError)
                && result.error.isError())
            {
                break;
            }
        }

        ++cmdNumber;
    }

    return results;
}

Result run_command(VMEController *controller, const Command &cmd, LoggerFun logger)
{
    /*
    if (logger)
        logger(to_string(cmd));
    */

    Result result;

    result.command = cmd;

    switch (cmd.type)
    {
        case CommandType::Invalid:
            /* Note: SetBase and ResetBase have already been handled at parse time. */
        case CommandType::SetBase:
        case CommandType::ResetBase:
        case CommandType::SetVariable:
            break;

        case CommandType::Read:
        case CommandType::ReadAbs:
            {
                switch (cmd.dataWidth)
                {
                    case DataWidth::D16:
                        {
                            uint16_t value = 0;
                            result.error = controller->read16(cmd.address, &value, cmd.addressMode);
                            result.value = value;
                        } break;
                    case DataWidth::D32:
                        {
                            uint32_t value = 0;
                            result.error = controller->read32(cmd.address, &value, cmd.addressMode);
                            result.value = value;
                        } break;
                }
            } break;

        case CommandType::Write:
        case CommandType::WriteAbs:
            {
                switch (cmd.dataWidth)
                {
                    case DataWidth::D16:
                        result.error = controller->write16(cmd.address, cmd.value, cmd.addressMode);
                        break;
                    case DataWidth::D32:
                        result.error = controller->write32(cmd.address, cmd.value, cmd.addressMode);
                        break;
                }
            } break;

        case CommandType::Wait:
            {
                QThread::msleep(cmd.delay_ms);
            } break;

        case CommandType::Marker:
            {
                result.value = cmd.value;
            } break;

        case CommandType::BLT:
            {
                result.error = controller->blockRead(
                    cmd.address, cmd.transfers, &result.valueVector,
                    vme_address_modes::BLT32, false);
            } break;

        case CommandType::BLTFifo:
            {
                result.error = controller->blockRead(
                    cmd.address, cmd.transfers, &result.valueVector,
                    vme_address_modes::BLT32, true);
            } break;

        case CommandType::MBLT:
            {
                result.error = controller->blockRead(
                    cmd.address, cmd.transfers, &result.valueVector,
                    vme_address_modes::MBLT64, false);
            } break;

        case CommandType::MBLTSwapped:
            if (auto mvlc = qobject_cast<mesytec::mvme_mvlc::MVLC_VMEController *>(controller))
            {
                result.error = mvlc->vmeMBLTSwapped(
                    cmd.address, cmd.transfers, &result.valueVector);
            }
            else
            {
                result.error = VMEError(VMEError::WrongControllerType,
                                        QSL("MVLC controller required"));
            } break;

        case CommandType::MBLTFifo:
            {
                result.error = controller->blockRead(
                    cmd.address, cmd.transfers, &result.valueVector,
                    vme_address_modes::MBLT64, true);
            } break;

        case CommandType::VMUSB_WriteRegister:
            if (auto vmusb = qobject_cast<VMUSB *>(controller))
            {
                result.error = vmusb->writeRegister(cmd.address, cmd.value);
            }
            else
            {
                result.error = VMEError(VMEError::WrongControllerType,
                                        QSL("VMUSB controller required"));
            } break;

        case CommandType::VMUSB_ReadRegister:
            if (auto vmusb = qobject_cast<VMUSB *>(controller))
            {
                result.value = 0u;
                result.error = vmusb->readRegister(cmd.address, &result.value);
            }
            else
            {
                result.error = VMEError(VMEError::WrongControllerType,
                                        QSL("VMUSB controller required"));
            } break;

        case CommandType::MVLC_WriteSpecial:
            {
                auto msg = QSL("mvlc_writespecial is not supported by vme_script::run_command().");
                result.error = VMEError(VMEError::UnsupportedCommand, msg);
                if (logger) logger(msg);
            }
            break;

        case CommandType::MetaBlock:
        case CommandType::Blk2eSST64:
        case CommandType::Print:
            break;
    }

    return result;
}

QString format_result(const Result &result)
{
    if (result.error.isError())
    {
        QString ret = QString("Error from \"%1\": %2")
            .arg(to_string(result.command))
            .arg(result.error.toString());

#if 0 // too verbose
        if (auto ec = result.error.getStdErrorCode())
        {
            ret += QString(" (std::error_code: msg=%1, value=%2, cat=%3)")
                .arg(ec.message().c_str())
                .arg(ec.value())
                .arg(ec.category().name());
        }
#endif

        return ret;
    }

    QString ret(to_string(result.command));

    switch (result.command.type)
    {
        case CommandType::Invalid:
        case CommandType::Wait:
        case CommandType::Marker:
        case CommandType::SetBase:
        case CommandType::ResetBase:
        case CommandType::MVLC_WriteSpecial:
        case CommandType::MetaBlock:
        case CommandType::SetVariable:
            break;

        case CommandType::Write:
        case CommandType::WriteAbs:
        case CommandType::VMUSB_WriteRegister:
            // Append the decimal form of the written value and a message that
            // the write was ok.
            ret += QSL(" (%1 dec), write ok").arg(result.command.value);
            break;

        case CommandType::Read:
        case CommandType::ReadAbs:
            ret += QString(" -> 0x%1 (%2 dec)")
                .arg(result.value, 8, 16, QChar('0'))
                .arg(result.value)
                ;
            break;

        case CommandType::BLT:
        case CommandType::BLTFifo:
        case CommandType::MBLT:
        case CommandType::MBLTFifo:
        case CommandType::MBLTSwapped:
        case CommandType::Blk2eSST64:
            {
                ret += "\n";
                for (int i=0; i<result.valueVector.size(); ++i)
                {
                    ret += QString(QSL("%1: 0x%2\n"))
                        .arg(i, 2, 10, QChar(' '))
                        .arg(result.valueVector[i], 8, 16, QChar('0'));
                }
            } break;

        case CommandType::VMUSB_ReadRegister:
            ret += QSL(" -> 0x%1, %2")
                .arg(result.value, 8, 16, QChar('0'))
                .arg(result.value)
                ;
            break;

        case CommandType::Print:
            {
                ret = result.command.printArgs.join(' ');
            }
            break;
    }

    return ret;
}

} // end namespace vme_script
