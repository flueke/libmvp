/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017  Florian Lüke <f.lueke@mesytec.com>
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
#include "template_system.h"
#include "qt_util.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>

#include <limits>

namespace
{
    using namespace vats;

    void do_log(const QString &msg, TemplateLogger logger)
    {
        if (logger)
            logger(msg);
    };

    QString read_file(const QString &fileName, TemplateLogger logger)
    {
        QFile inFile(fileName);

        if (!inFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            do_log(QString("Error opening %1 for reading: %2")
                   .arg(fileName)
                   .arg(inFile.errorString()),
                   logger);

            return QString();
        }

        QTextStream inStream(&inFile);
        return inStream.readAll();
    }

    QJsonDocument read_json_file(const QString &fileName, TemplateLogger logger)
    {
        QString data = read_file(fileName, logger);

        QJsonParseError parseError;
        QJsonDocument doc(QJsonDocument::fromJson(data.toUtf8(), &parseError));

        if (parseError.error != QJsonParseError::NoError)
        {
            do_log(QString("JSON parse error in file %1: %2 at offset %3")
                   .arg(fileName)
                   .arg(parseError.errorString())
                   .arg(parseError.offset),
                   logger);
        }

        return doc;
    }

    VMETemplate read_vme_template(const QString &path, const QString &name, TemplateLogger logger, const QDir &baseDir)
    {
        VMETemplate result;
        result.contents = read_file(path, logger);
        result.name = name;
        result.sourceFileName = baseDir.relativeFilePath(path);
        return result;
    }

    VMEModuleTemplates read_module_templates(const QString &path, TemplateLogger logger, const QDir &baseDir)
    {
        QDir dir(path);
        VMEModuleTemplates result;
        result.readout = read_vme_template(dir.filePath("readout.vmescript"), QSL("Module Readout"), logger, baseDir);
        result.reset   = read_vme_template(dir.filePath("reset.vmescript"), QSL("Module Reset"), logger, baseDir);

        auto initEntries = dir.entryList({ QSL("init-*.vmescript") }, QDir::Files | QDir::Readable, QDir::Name);

        for (const auto &fileName: initEntries)
        {
            VMETemplate vmeTemplate;
            QString entryFilePath = dir.filePath(fileName);
            vmeTemplate.contents = read_file(entryFilePath, logger);
            vmeTemplate.sourceFileName = baseDir.relativeFilePath(entryFilePath); // dir.filePath(fileName);

            QRegularExpression re;
            re.setPattern("^init-\\d\\d-(.*)\\.vmescript$");
            auto match = re.match(fileName);

            if (!match.hasMatch())
            {
                re.setPattern("^init-(.*)\\.vmescript$");
                match = re.match(fileName);
            }

            if (match.hasMatch())
            {
                vmeTemplate.name = match.captured(1);
            }
            else
            {
                vmeTemplate.name = QFileInfo(fileName).baseName();
            }

            result.init.push_back(vmeTemplate);
        }

        return result;
    }
}

namespace vats
{

QDebug operator<<(QDebug debug, const MVMETemplates &templates);

QString get_template_path()
{
    QString templatePath = QCoreApplication::applicationDirPath() + QSL("/templates");
    return templatePath;
}

MVMETemplates read_templates(TemplateLogger logger)
{
    QString templatePath = get_template_path();
    do_log(QString("Loading templates from %1").arg(templatePath), logger);
    return read_templates_from_path(templatePath, logger);
}

// TODO:
// - check for duplicate typeIds
// - check for duplicate typeNames
// - check for empty typeNames

MVMETemplates read_templates_from_path(const QString &path, TemplateLogger logger)
{
    const QDir baseDir(path);
    MVMETemplates result;

    result.eventTemplates.daqStart          = read_vme_template(baseDir.filePath(QSL("event/event_daq_start.vmescript")),
                                                                QSL("DAQ Start"), logger, baseDir);

    result.eventTemplates.daqStop           = read_vme_template(baseDir.filePath(QSL("event/event_daq_stop.vmescript")),
                                                                QSL("DAQ Stop"), logger, baseDir);

    result.eventTemplates.readoutCycleStart = read_vme_template(baseDir.filePath(QSL("event/readout_cycle_start.vmescript")),
                                                                QSL("Cycle Start"), logger, baseDir);

    result.eventTemplates.readoutCycleEnd   = read_vme_template(baseDir.filePath(QSL("event/readout_cycle_end.vmescript")),
                                                                QSL("Cycle End"), logger, baseDir);

    auto dirEntries = baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);

    for (const auto &dirName: dirEntries)
    {
        QDir moduleDir(baseDir.filePath(dirName));

        if (!moduleDir.exists(QSL("module_info.json")))
            continue;

        auto moduleInfo = read_json_file(moduleDir.filePath(QSL("module_info.json")), logger);

        if (moduleInfo.isEmpty())
        {
            do_log(QString("Skipping %1: invalid module_info.json").arg(moduleDir.path()), logger);
            continue;
        }

        auto json = moduleInfo.object();

        s32 moduleType = json["typeId"].toInt();

        if (moduleType <= 0 || moduleType > std::numeric_limits<u8>::max())
        {
            do_log(QString("%1: module typeId out of range (valid range is [1, 255])")
                   .arg(moduleDir.filePath(QSL("moduleInfo.json"))),
                   logger);
            continue;
        }

        VMEModuleMeta mm;
        mm.typeId = static_cast<u8>(moduleType);
        mm.typeName = json["typeName"].toString();
        mm.displayName = json["displayName"].toString();
        mm.templates = read_module_templates(moduleDir.filePath(QSL("vme")), logger, baseDir);
        mm.templatePath = moduleDir.path();

        result.moduleMetas.push_back(mm);
    }

    return result;
}

QString get_module_path(const QString &moduleTypeName)
{
    auto templates = read_templates();

    for (const auto &mm: templates.moduleMetas)
    {
        if (mm.typeName == moduleTypeName)
            return mm.templatePath;
    }

    return QString();
}

static QTextStream &do_indent(QTextStream &out, int indent)
{
    for (int i=0; i<indent; ++i)
        out << " ";

    return out;
}

static QTextStream &print(QTextStream &out, const VMETemplate &vmeTemplate, int indent = 0)
{
    do_indent(out, indent) << "name=" << vmeTemplate.name << endl;
    do_indent(out, indent) << "source=" << vmeTemplate.sourceFileName << endl;
    do_indent(out, indent) << "size=" << vmeTemplate.contents.size() << endl;

    return out;
}

static QTextStream &print(QTextStream &out, const VMEModuleMeta &module, int indent = 0)
{
    do_indent(out, indent) << "typeId=" << static_cast<u32>(module.typeId) << endl;
    do_indent(out, indent) << "typeName=" << module.typeName << endl;
    do_indent(out, indent) << "displayName=" << module.displayName << endl;

    do_indent(out, indent) << "templates:" << endl;

    do_indent(out, indent+2) << "reset:" << endl;
    print(out, module.templates.reset, indent+4);

    do_indent(out, indent+2) << "readout:" << endl;
    print(out, module.templates.readout, indent+4);

    do_indent(out, indent+2) << "init (" << module.templates.init.size() << " templates):" << endl;

    int idx = 0;
    for (const auto &vmeTemplate: module.templates.init)
    {
        do_indent(out, indent+4) << idx << endl;
        print(out, vmeTemplate, indent+6);

        ++idx;
    }

    return out;
}

QTextStream &operator<<(QTextStream &out, const MVMETemplates &templates)
{
    // Module table
    {
        QVector<VMEModuleMeta> modules(templates.moduleMetas);
        qSort(modules.begin(), modules.end(), [](const VMEModuleMeta &a, const VMEModuleMeta &b) {
            return a.typeId < b.typeId;
        });

        out << ">>>>> Known Modules <<<<<" << endl;
        const int fw = 20;
        left(out) << qSetFieldWidth(fw) << "typeId" << "typeName" << "displayName" << qSetFieldWidth(0) << endl;

        for (const auto &mm: modules)
        {
            out << qSetFieldWidth(fw)
                << static_cast<u32>(mm.typeId)
                << mm.typeName
                << mm.displayName
                << qSetFieldWidth(0)
                << endl;
        }

        out << "<<<<< Known Modules >>>>>" << endl;
        out.reset();
    }

    out << endl;

    // Detailed information about loaded templates
    {
        out << ">>>>> VME Templates <<<<<" << endl;
        out << "Event:" << endl;

        do_indent(out, 2) << "daqStart" << endl;
        print(out, templates.eventTemplates.daqStart, 4);

        do_indent(out, 2) << "daqStop" << endl;
        print(out, templates.eventTemplates.daqStop, 4);

        do_indent(out, 2) << "readoutCycleStart" << endl;
        print(out, templates.eventTemplates.readoutCycleStart, 4);

        do_indent(out, 2) << "readoutCycleEnd" << endl;
        print(out, templates.eventTemplates.readoutCycleEnd, 4);

        out << endl << "Modules:" << endl;

        for (const auto &module: templates.moduleMetas)
        {
            do_indent(out, 2) << "Begin Module" << endl;
            print(out, module, 4);
            do_indent(out, 2) << "End Module" << endl << endl;
        }
        out << "<<<<< VME Templates >>>>>" << endl;
    }

    return out;
}

} // namespace vats