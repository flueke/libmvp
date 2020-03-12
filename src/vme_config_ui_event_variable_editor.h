/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __MVME_VME_CONFIG_UI_EVENT_VARIABLE_EDITOR_H__
#define __MVME_VME_CONFIG_UI_EVENT_VARIABLE_EDITOR_H__

#include <memory>
#include <QWidget>

#include "libmvme_export.h"
#include "vme_config.h"
#include "vme_script.h"

class LIBMVME_EXPORT EventVariableEditor: public QWidget
{
    Q_OBJECT
    signals:
        void logMessage(const QString &str);
        void logError(const QString &str);

    public:
        using RunScriptCallback = std::function<
            vme_script::ResultList (
                const vme_script::VMEScript &,
                vme_script::LoggerFun)>;

        explicit EventVariableEditor(
            EventConfig *eventConfig,
            RunScriptCallback runScriptCallback,
            QWidget *parent = nullptr);

        ~EventVariableEditor();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

#endif /* __MVME_VME_CONFIG_UI_EVENT_VARIABLE_EDITOR_H__ */