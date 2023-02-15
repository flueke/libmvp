/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "stream_worker_base.h"

#include "util/perf.h"
#include "util/qt_str.h"

StreamWorkerBase::StreamWorkerBase(QObject *parent)
    : QObject(parent)
    , m_logThrottle(MaxLogMessagesPerSecond, std::chrono::seconds(1))
{}

StreamWorkerBase::~StreamWorkerBase()
{
    qDebug() << __PRETTY_FUNCTION__ << this << this->objectName() << this->parent();
}

bool StreamWorkerBase::logMessage(const MessageSeverity &/*sev*/,
                                  const QString &msg,
                                  bool useThrottle)
{
    if (!useThrottle)
    {
        qDebug().noquote() << msg;
        emit sigLogMessage(msg);
        return true;
    }

    // have to store this before the call to eventOverflows()
    size_t suppressedMessages = m_logThrottle.overflow();

    if (!m_logThrottle.eventOverflows())
    {
        if (unlikely(suppressedMessages))
        {
            auto finalMsg(QString("%1 (suppressed %2 earlier messages)")
                          .arg(msg)
                          .arg(suppressedMessages)
                         );
            qDebug().noquote() << finalMsg;
            emit sigLogMessage(msg);
        }
        else
        {
            qDebug().noquote() << msg;
            emit sigLogMessage(msg);
        }
        return true;
    }

    return false;
}
