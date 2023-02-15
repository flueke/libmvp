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
#ifndef __MVLC_LISTFILE_WORKER_H__
#define __MVLC_LISTFILE_WORKER_H__

#include <mesytec-mvlc/mesytec-mvlc.h>
#include "libmvme_export.h"
#include "listfile_replay_worker.h"

class LIBMVME_EXPORT MVLCListfileWorker: public ListfileReplayWorker
{
    Q_OBJECT
    public:
        using LoggerFun = std::function<void (const QString &)>;

        explicit MVLCListfileWorker(QObject *parent = nullptr);
        ~MVLCListfileWorker() override;

        void setSnoopQueues(mesytec::mvlc::ReadoutBufferQueues *snoopQueues);
        void setListfile(ListfileReplayHandle *handle) override;

        DAQStats getStats() const override;
        bool isRunning() const override;
        DAQState getState() const override;
        void setEventsToRead(u32 eventsToRead) override;

    public slots:
        // Blocking call which will perform the work
        void start() override;

        // Thread-safe calls, setting internal flags to do the state transition
        void stop() override;
        void pause() override;
        void resume() override;

    private:
        void setState(DAQState state);
        void logError(const QString &msg);

        struct Private;
        std::unique_ptr<Private> d;
};

#endif /* __MVLC_LISTFILE_WORKER_H__ */
