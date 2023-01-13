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
#include "listfile_replay_worker.h"

ListfileReplayWorker::ListfileReplayWorker(
    ThreadSafeDataBufferQueue *emptyBufferQueue,
    ThreadSafeDataBufferQueue *filledBufferQueue,
    QObject *parent)
: QObject(parent)
, m_emptyBufferQueue(emptyBufferQueue)
, m_filledBufferQueue(filledBufferQueue)
{ }

void ListfileReplayWorker::setLogger(LoggerFun logger)
{
    m_logger = logger;
}
