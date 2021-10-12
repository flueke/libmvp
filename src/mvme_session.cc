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
#include "mvme_session.h"

#include <QCoreApplication>
#include <QDebug>
#include <QLibraryInfo>
#include <QLocale>

#include "analysis/analysis_session.h"
#include "build_info.h"
#include "git_sha1.h"
#include "mvlc/mvlc_qt_object.h"
#include "mvme_stream_worker.h"
#include "mvlc_stream_worker.h"
#include "vme_config.h"
#include "vme_controller.h"

void mvme_init(const QString &appName)
{
    Q_INIT_RESOURCE(resources);

    qRegisterMetaType<DAQState>("DAQState");
    qRegisterMetaType<GlobalMode>("GlobalMode");
    qRegisterMetaType<AnalysisWorkerState>("AnalysisWorkerState");
    qRegisterMetaType<ControllerState>("ControllerState");
    qRegisterMetaType<Qt::Axis>("Qt::Axis");
    qRegisterMetaType<mesytec::mvme_mvlc::MVLCObject::State>("mesytec::mvme_mvlc::MVLCObject::State");
    qRegisterMetaType<DataBuffer>("DataBuffer");
    qRegisterMetaType<EventRecord>("EventRecord");

    qRegisterMetaType<ContainerObject *>();
    qRegisterMetaType<VMEScriptConfig *>();
    qRegisterMetaType<ModuleConfig *>();
    qRegisterMetaType<EventConfig *>();
    qRegisterMetaType<VMEConfig *>();
    qRegisterMetaType<VMEConfig *>();

#define REG_META_VEC(T) \
    qRegisterMetaType<QVector<T>>("QVector<"#T">")

    REG_META_VEC(u8);
    REG_META_VEC(u16);
    REG_META_VEC(u32);

    REG_META_VEC(s8);
    REG_META_VEC(s16);
    REG_META_VEC(s32);

#undef REG_META_VEC

    QCoreApplication::setOrganizationDomain("www.mesytec.com");
    QCoreApplication::setOrganizationName("mesytec");
    QCoreApplication::setApplicationName(appName);
    QCoreApplication::setApplicationVersion(GIT_VERSION);

    QLocale::setDefault(QLocale::c());

    qDebug() << "prefixPath = " << QLibraryInfo::location(QLibraryInfo::PrefixPath);
    qDebug() << "librariesPaths = " << QLibraryInfo::location(QLibraryInfo::LibrariesPath);
    qDebug() << "pluginsPaths = " << QLibraryInfo::location(QLibraryInfo::PluginsPath);
    qDebug() << "GIT_VERSION =" << GIT_VERSION;
    qDebug() << "BUILD_TYPE =" << BUILD_TYPE;
    qDebug() << "BUILD_CXX_FLAGS =" << BUILD_CXX_FLAGS;

    spdlog::set_level(spdlog::level::trace);
}

void mvme_shutdown()
{
}
