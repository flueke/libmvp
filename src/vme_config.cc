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
#include "vme_config.h"

#include <cmath>
#include <memory>
#include <QDebug>
#include <QEvent>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <spdlog/spdlog.h>

#include "CVMUSBReadoutList.h"
#include "mvlc/mvlc_trigger_io_script.h"
#include "qt_util.h"
#include "util/qt_metaobject.h"
#include "vme_config_util.h"
#include "vme_controller.h"
#include "vme_config_version.h"
#include "vme_config_json_schema_updates.h"

using namespace vats;

namespace
{

class VMEConfigReadResultErrorCategory: public std::error_category
{
    const char *name() const noexcept override
    {
        return "vme_config_read_error";
    }

    std::string message(int ev) const override
    {
        switch (static_cast<VMEConfigReadResult>(ev))
        {
            case VMEConfigReadResult::NoError:
                return "No Error";

            case VMEConfigReadResult::VersionTooOld:
                return "JSON schema version too old, schema upgrade required.";

            case VMEConfigReadResult::VersionTooNew:
                return "The file was generated by a newer version of mvme. Please upgrade.";
        }

        return "unrecognized error";
    }
};

const VMEConfigReadResultErrorCategory theVMEConfigReadResultErrorCategory {};

} // end anon namespace

std::error_code make_error_code(VMEConfigReadResult r)
{
    return { static_cast<int>(r), theVMEConfigReadResultErrorCategory };
}

//
// ConfigObject
//
ConfigObject::ConfigObject(QObject *parent, bool watchDynamicProperties)
    : ConfigObject(parent)
{
    if (watchDynamicProperties)
        setWatchDynamicProperties(true);
}

ConfigObject::ConfigObject(QObject *parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
{
    connect(this, &QObject::objectNameChanged, this, [this] {
        setModified(true);
    });

    connect(this, &ConfigObject::enabledChanged, this, [this] {
        setModified(true);
    });
}

void ConfigObject::generateNewId()
{
    m_id = QUuid::createUuid();
}

void ConfigObject::setModified(bool b)
{
    emit modified(b);

    if (m_modified != b)
    {
        m_modified = b;
        emit modifiedChanged(b);
    }

    if (b)
    {
        if (auto parentConfig = qobject_cast<ConfigObject *>(parent()))
            parentConfig->setModified(true);
    }
}

void ConfigObject::setEnabled(bool b)
{
    if (m_enabled != b)
    {
        m_enabled = b;
        emit enabledChanged(b);
    }
}

QString ConfigObject::getObjectPath() const
{
    if (objectName().isEmpty())
        return QString();

    auto parentConfig = qobject_cast<ConfigObject *>(parent());

    if (!parentConfig)
        return objectName();

    auto result = parentConfig->getObjectPath();

    if (!result.isEmpty())
        result += QChar('/');

    result += objectName();

    return result;
}

std::error_code ConfigObject::read(const QJsonObject &json)
{
    m_id = QUuid(json["id"].toString());

    if (m_id.isNull())
        m_id = QUuid::createUuid();

    setObjectName(json["name"].toString());
    setEnabled(json["enabled"].toBool(true));
    loadDynamicProperties(json["properties"].toObject(), this);

    setVariables(vme_script::symboltable_from_json(json["variable_table"].toObject()));

    auto ec = read_impl(json);

    setModified(false);

    return ec;
}

std::error_code ConfigObject::write(QJsonObject &json) const
{
    json["id"]   = m_id.toString();
    json["name"] = objectName();
    json["enabled"] = m_enabled;

    auto props = storeDynamicProperties(this);
    if (!props.isEmpty())
        json["properties"] = props;

    json["variable_table"] = vme_script::to_json(m_variables);

    return write_impl(json);
}

void ConfigObject::setVariables(const vme_script::SymbolTable &variables)
{
    if (m_variables != variables)
    {
        m_variables = variables;
        setModified();
    }
}

void ConfigObject::setVariable(const QString &name, const vme_script::Variable &var)
{
    if (m_variables.value(name) != var)
    {
        m_variables[name] = var;
        setModified();
    }
}

void ConfigObject::setVariableValue(const QString &name, const QString &value)
{
    if (m_variables.value(name).value != value)
    {
        m_variables[name].value = value;
        setModified();
    }
}

bool ConfigObject::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == this && event->type() == QEvent::DynamicPropertyChange)
        setModified();
    return QObject::eventFilter(obj, event);
}

void ConfigObject::setWatchDynamicProperties(bool doWatch)
{
    if (doWatch && !m_eventFilterInstalled)
    {
        installEventFilter(this);
        m_eventFilterInstalled = true;
    }
    else if (!doWatch && m_eventFilterInstalled)
    {
        removeEventFilter(this);
        m_eventFilterInstalled = false;
    }
}

std::string ConfigObject::objectNameStdString() const
{
    return objectName().toStdString();
}

//
// ContainerObject
//
ContainerObject::ContainerObject(QObject *parent)
    : ConfigObject(parent)
{
}

ContainerObject::ContainerObject(
    const QString &name, const QString &displayName,
    const QString &icon, QObject *parent)
    : ContainerObject(parent)
{
    setObjectName(name);
    if (!displayName.isNull())
        setProperty("display_name", displayName);
    setProperty("icon", icon);
}

std::error_code ContainerObject::write_impl(QJsonObject &json) const
{
    QJsonArray childArray;

    for (auto child: m_children)
    {
        QJsonObject childDataJson;
        child->write(childDataJson);

        QJsonObject childJson;
        childJson["class"] = getClassName(child);
        childJson["data"] = childDataJson;

        childArray.append(childJson);
    }

    json["children"] = childArray;

    return {};
}

std::error_code ContainerObject::read_impl(const QJsonObject &json)
{
    auto childArray = json["children"].toArray();

    for (const auto &jval: childArray)
    {
        auto jobj = jval.toObject();
        auto className = jobj["class"].toString() + "*";

        auto typeId = QMetaType::type(className.toLocal8Bit().constData());

        if (typeId == QMetaType::UnknownType)
        {
            qWarning() << "ContainerObject::read_impl: No QMetaType defined for className ="
                << className.toLocal8Bit().constData()
                << ", skipping child entry.";

            continue;
        }

        QMetaType mt(typeId);

        if (mt.flags() & QMetaType::PointerToQObject)
        {
            auto metaObject = mt.metaObject();

            if (!metaObject)
            {
                qWarning() << "No QMetaObject for class" << className;
                continue;
            }

            auto rawChild = metaObject->newInstance();
            std::unique_ptr<QObject> memGuard(rawChild);
            auto child = qobject_cast<ConfigObject *>(rawChild);

            if (!rawChild)
            {
                qWarning() << "Could not create child object of class" << className;
                continue;
            }

            if (!child)
            {
                qWarning() << "Child object is not a subclass of ConfigObject: "
                    << rawChild << ", className =" << className;
                continue;
            }

            child->read(jobj["data"].toObject());
            addChild(child);
            (void) memGuard.release();
        }
        // maybe TODO: implement the case for non-qobject metatypes using mt.create()
    }

    return {};
}

//
// VMEScriptConfig
//
VMEScriptConfig::VMEScriptConfig(QObject *parent)
    : ConfigObject(parent)
{
    setProperty("icon", ":/vme_script.png");
}

VMEScriptConfig::VMEScriptConfig(const QString &name, const QString &contents, QObject *parent)
    : ConfigObject(parent)
{
    setObjectName(name);
    setScriptContents(contents);
    setModified(false);
}

void VMEScriptConfig::setScriptContents(const QString &str)
{
    if (m_script != str)
    {
        m_script = str;
        setModified(true);
    }
}

void VMEScriptConfig::addToScript(const QString &str)
{
    m_script += str;
    setModified(true);
}

std::error_code VMEScriptConfig::read_impl(const QJsonObject &json)
{
    m_script = json["vme_script"].toString();
    spdlog::info("VMEScriptConfig::read_impl(): objectName()={}", objectName().toStdString());

    return {};
}

std::error_code VMEScriptConfig::write_impl(QJsonObject &json) const
{
    json["vme_script"] = m_script;

    return {};
}

QString VMEScriptConfig::getVerboseTitle() const
{
    auto module     = qobject_cast<ModuleConfig *>(parent());
    auto event      = qobject_cast<EventConfig *>(parent());
    auto daqConfig  = qobject_cast<VMEConfig *>(parent());

    QString title;

    if (module)
    {
        title = QString("%1 for module %2")
            .arg(objectName())
            .arg(module->objectName());
    }
    else if (event)
    {
        title = QString("%1 for event %2")
            .arg(objectName())
            .arg(event->objectName());
    }
    else if (daqConfig)
    {
        title = QString("Global Script %2")
            .arg(objectName());
    }
    else
    {
        title = objectName();
    }

    return title;
}

//
// ModuleConfig
//
ModuleConfig::ModuleConfig(QObject *parent)
    : ConfigObject(parent)
    , m_resetScript(new VMEScriptConfig(this))
    , m_readoutScript(new VMEScriptConfig(this))
{
}

void ModuleConfig::setBaseAddress(uint32_t address)
{
    if (address != m_baseAddress)
    {
        m_baseAddress = address;
        setModified();
    }
}

void ModuleConfig::setModuleMeta(const vats::VMEModuleMeta &meta)
{
    if (m_meta != meta)
    {
        m_meta = meta;
        setModified();
    }
}

void ModuleConfig::addInitScript(VMEScriptConfig *script)
{
    Q_ASSERT(script);

    script->setParent(this);
    m_initScripts.push_back(script);
    setModified(true);
}

VMEScriptConfig *ModuleConfig::getInitScript(const QString &scriptName) const
{
    auto it = std::find_if(m_initScripts.begin(), m_initScripts.end(),
                           [scriptName] (const VMEScriptConfig *config) {
                               return config->objectName() == scriptName;
                           });

    return (it != m_initScripts.end() ? *it : nullptr);
}

VMEScriptConfig *ModuleConfig::getInitScript(s32 scriptIndex) const
{
    return m_initScripts.value(scriptIndex, nullptr);
}

std::error_code ModuleConfig::read_impl(const QJsonObject &json)
{
    qDeleteAll(m_initScripts);
    m_initScripts.clear();

    QString typeName = json["type"].toString();

    // Use the typeName to load module meta info from the template system.
    const auto moduleMetas = read_templates().moduleMetas;
    auto it = std::find_if(moduleMetas.begin(), moduleMetas.end(), [typeName](const VMEModuleMeta &mm) {
        return mm.typeName == typeName;
    });

    m_meta = (it != moduleMetas.end() ? *it : VMEModuleMeta());

    // IMPORTANT: using json["baseAddress"].toInt() directly does not support
    // the full range of 32-bit unsigned integers!
    m_baseAddress = static_cast<u32>(json["baseAddress"].toDouble());

    m_resetScript->read(json["vmeReset"].toObject());
    m_readoutScript->read(json["vmeReadout"].toObject());

    auto initScriptsArray = json["initScripts"].toArray();

    for (auto it = initScriptsArray.begin();
         it != initScriptsArray.end();
         ++it)
    {
        auto cfg = new VMEScriptConfig(this);
        cfg->read(it->toObject());
        m_initScripts.push_back(cfg);
    }

    return {};
}

std::error_code ModuleConfig::write_impl(QJsonObject &json) const
{
    json["type"] = m_meta.typeName;
    json["baseAddress"] = static_cast<qint64>(m_baseAddress);

    // readout script
    {
        QJsonObject dstObject;
        m_readoutScript->write(dstObject);
        json["vmeReadout"] = dstObject;
    }

    // reset script
    {
        QJsonObject dstObject;
        m_resetScript->write(dstObject);
        json["vmeReset"] = dstObject;
    }

    // init scripts
    {
        QJsonArray dstArray;
        for (auto scriptConfig: m_initScripts)
        {
            QJsonObject dstObject;
            scriptConfig->write(dstObject);
            dstArray.append(dstObject);
        }
        json["initScripts"] = dstArray;
    }

    return {};
}

const EventConfig *ModuleConfig::getEventConfig() const
{
    return qobject_cast<const EventConfig *>(parent());
}

EventConfig *ModuleConfig::getEventConfig()
{
    return qobject_cast<EventConfig *>(parent());
}

QUuid ModuleConfig::getEventId() const
{
    if (auto eventConfig = getEventConfig())
    {
        return eventConfig->getId();
    }

    return {};
}

const VMEConfig *ModuleConfig::getVMEConfig() const
{
    if (auto ev = getEventConfig())
        return ev->getVMEConfig();
    return nullptr;
}

VMEConfig *ModuleConfig::getVMEConfig()
{
    if (auto ev = getEventConfig())
        return ev->getVMEConfig();
    return nullptr;
}

//
// EventConfig
//

EventConfig::EventConfig(QObject *parent)
    : ConfigObject(parent)
{
    vmeScripts[QSL("daq_start")] = new VMEScriptConfig(this);
    vmeScripts[QSL("daq_start")]->setObjectName(QSL("DAQ Start"));

    vmeScripts[QSL("daq_stop")] = new VMEScriptConfig(this);
    vmeScripts[QSL("daq_stop")]->setObjectName(QSL("DAQ Stop"));

    vmeScripts[QSL("readout_start")] = new VMEScriptConfig(this);
    vmeScripts[QSL("readout_start")]->setObjectName(QSL("Cycle Start"));

    vmeScripts[QSL("readout_end")] = new VMEScriptConfig(this);
    vmeScripts[QSL("readout_end")]->setObjectName(QSL("Cycle End"));

    triggerOptions[QSL("sis3153.timer_period")] = 1.0;
}

const VMEConfig *EventConfig::getVMEConfig() const
{
    return qobject_cast<const VMEConfig *>(parent());
}

VMEConfig *EventConfig::getVMEConfig()
{
    return qobject_cast<VMEConfig *>(parent());
}

std::error_code EventConfig::read_impl(const QJsonObject &json)
{
    qDeleteAll(modules);
    modules.clear();

    // triggerCondition and options
    {
        auto tcName = json["triggerCondition"].toString();
        auto it = std::find_if(TriggerConditionNames.begin(), TriggerConditionNames.end(),
                               [tcName](const auto &testName) {
            return tcName == testName;
        });

        // FIXME: report error on unknown trigger condition
        triggerCondition = (it != TriggerConditionNames.end()) ? it.key() : TriggerCondition::Interrupt;
        triggerOptions = json["triggerOptions"].toObject().toVariantMap();
    }
    irqLevel = json["irqLevel"].toInt();
    irqVector = json["irqVector"].toInt();
    scalerReadoutPeriod = json["scalerReadoutPeriod"].toInt();
    scalerReadoutFrequency = json["scalerReadoutFrequency"].toInt();

    QJsonArray moduleArray = json["modules"].toArray();
    for (int i=0; i<moduleArray.size(); ++i)
    {
        QJsonObject moduleObject = moduleArray[i].toObject();
        ModuleConfig *moduleConfig = new ModuleConfig(this);
        moduleConfig->read(moduleObject);
        modules.append(moduleConfig);
    }

    for (auto scriptConfig: vmeScripts.values())
    {
        scriptConfig->setScriptContents(QString());
    }

    QJsonObject scriptsObject = json["vme_scripts"].toObject();

    for (auto it = scriptsObject.begin();
         it != scriptsObject.end();
         ++it)
    {
        if (vmeScripts.contains(it.key()))
        {
            vmeScripts[it.key()]->read(it.value().toObject());
        }
    }

    return {};
}

std::error_code EventConfig::write_impl(QJsonObject &json) const
{
    json["triggerCondition"] = TriggerConditionNames.value(triggerCondition);
    json["triggerOptions"]   = QJsonObject::fromVariantMap(triggerOptions);
    json["irqLevel"] = irqLevel;
    json["irqVector"] = irqVector;
    json["scalerReadoutPeriod"] = scalerReadoutPeriod;
    json["scalerReadoutFrequency"] = scalerReadoutFrequency;

    // modules
    QJsonArray moduleArray;

    for (auto module: modules)
    {
        QJsonObject moduleObject;
        module->write(moduleObject);
        moduleArray.append(moduleObject);
    }
    json["modules"] = moduleArray;

    // scripts
    QJsonObject scriptsObject;

    for (auto it = vmeScripts.begin();
         it != vmeScripts.end();
         ++it)
    {
        QJsonObject scriptJson;
        if (it.value())
        {
            it.value()->write(scriptJson);
            scriptsObject[it.key()] = scriptJson;
        }
    }

    json["vme_scripts"] = scriptsObject;

    return {};
}

//
// VMEConfig
//

VMEConfig::VMEConfig(QObject *parent)
    : ConfigObject(parent)
{
    m_globalObjects.setObjectName("global_objects");
    m_globalObjects.setProperty("display_name", "Global Objects");
    m_globalObjects.setProperty("icon", ":/vme_global_scripts.png");

    connect(&m_globalObjects, &ContainerObject::childAdded,
            this, &VMEConfig::onChildObjectAdded);

    connect(&m_globalObjects, &ContainerObject::childAboutToBeRemoved,
            this, &VMEConfig::onChildObjectAboutToBeRemoved);

    createMissingGlobals();

    setVMEController(m_controllerType);
}

void VMEConfig::onChildObjectAdded(ConfigObject *child, int index)
{
    //qDebug() << __PRETTY_FUNCTION__ << "child=" << child << "index=" << index;
    assert(child);

    emit globalChildAdded(child, index);

    // React to the childs modified signal
    connect(child, &ConfigObject::modified, this, [this] () { setModified(); });

    // Handle a container object by subscribing to its childAdded() signal and
    // then recursing to its children.
    if (auto co = qobject_cast<ContainerObject *>(child))
    {
        connect(co, &ContainerObject::childAdded,
                this, &VMEConfig::onChildObjectAdded);

        connect(co, &ContainerObject::childAboutToBeRemoved,
                this, &VMEConfig::onChildObjectAboutToBeRemoved);

        // Handle existing children of the newly added ContainerObject.
        auto children = co->getChildren();

        for (int i=0; i<children.size(); i++)
            onChildObjectAdded(children[i], i);
    }

    setModified();
}

void VMEConfig::onChildObjectAboutToBeRemoved(ConfigObject *child)
{
    assert(child);
    //qDebug() << __PRETTY_FUNCTION__ << "emit globalChildAboutToBeRemoved() child=" << child;
    emit globalChildAboutToBeRemoved(child);
    setModified();
}

void VMEConfig::createMissingGlobals()
{
    if (!m_globalObjects.findChildByName("daq_start"))
    {
        auto daqStartScripts = new ContainerObject(
            "daq_start", "DAQ Start", ":/folder_orange.png");
        m_globalObjects.addChild(daqStartScripts);
    }

    if (!m_globalObjects.findChildByName("daq_stop"))
    {
        auto daqStopScripts = new ContainerObject(
            "daq_stop", "DAQ Stop", ":/folder_orange.png");
        m_globalObjects.addChild(daqStopScripts);
    }

    if (!m_globalObjects.findChildByName("manual"))
    {
        auto manualScripts = new ContainerObject(
            "manual", "Manual", ":/folder_orange.png");
        m_globalObjects.addChild(manualScripts);
    }
}

void VMEConfig::addEventConfig(EventConfig *config)
{
    config->setParent(this);
    eventConfigs.push_back(config);
    emit eventAdded(config);
    setModified();
}

bool VMEConfig::removeEventConfig(EventConfig *config)
{
    bool ret = eventConfigs.removeOne(config);
    if (ret)
    {
        emit eventAboutToBeRemoved(config);
        config->setParent(nullptr);
        config->deleteLater();
        setModified();
    }

    return ret;
}

bool VMEConfig::contains(EventConfig *config)
{
    return eventConfigs.indexOf(config) >= 0;
}

bool VMEConfig::addGlobalScript(VMEScriptConfig *config, const QString &category)
{
    auto parent = qobject_cast<ContainerObject *>(m_globalObjects.findChildByName(category));

    if (!parent)
    {
        assert(false);
        return false;
    }

    parent->addChild(config);
    config->setParent(parent);
    setModified();
    return true;
}

bool VMEConfig::removeGlobalScript(VMEScriptConfig *config)
{
    auto parent = qobject_cast<ContainerObject *>(config->parent());

    if (!parent)
        return false;

    parent->removeChild(config);
    config->setParent(nullptr);
    config->deleteLater();
    setModified();
    return true;
}

QStringList VMEConfig::getGlobalScriptCategories() const
{
    QStringList result;

    for (auto child: m_globalObjects.getChildren())
        result.push_back(child->objectName());

    return result;
}

void VMEConfig::setVMEController(VMEControllerType type, const QVariantMap &settings)
{
    m_controllerType = type;

    // Merge the controller settings, overwriting existing values.
    // Note: unite() doesn't work because it uses insertMulti() instead of
    // overwriting the values.
    for (const auto &key: settings.keys())
        m_controllerSettings[key] = settings.value(key);

    if (is_mvlc_controller(type)
        && !qobject_cast<VMEScriptConfig *>(m_globalObjects.findChildByName("mvlc_trigger_io")))
    {
        // At some point during development the mvlc_trigger_io object was
        // created as a ContainerObject instead of as a VMEScriptConfig. This
        // code removes the ContainerObject and replaces it with a
        // VMEScriptConfig.
        if (auto child = m_globalObjects.findChildByName("mvlc_trigger_io"))
        {
            m_globalObjects.removeChild(child);
            child->setParent(nullptr);
            child->deleteLater();
            assert(!m_globalObjects.findChildByName("mvlc_trigger_io"));
        }

        if (!m_globalObjects.findChildByName("mvlc_trigger_io"))
        {
            auto triggerIOScript = new VMEScriptConfig;
            triggerIOScript->setObjectName("mvlc_trigger_io");
            triggerIOScript->setProperty("display_name", "MVLC Trigger/IO");
            triggerIOScript->setProperty("icon", ":/vme_module.png");
            triggerIOScript->setScriptContents(
                mesytec::mvme_mvlc::trigger_io::generate_trigger_io_script_text({}));
            m_globalObjects.addChild(triggerIOScript);

            assert(m_globalObjects.findChildByName("mvlc_trigger_io"));
        }
    }

    if (is_mvlc_controller(type))
    {
        assert(qobject_cast<VMEScriptConfig *>(m_globalObjects.findChildByName("mvlc_trigger_io")));
    }

    setModified();
    emit vmeControllerTypeSet(type);
}

const ContainerObject &VMEConfig::getGlobalObjectRoot() const
{
    return m_globalObjects;
}

ContainerObject &VMEConfig::getGlobalObjectRoot()
{
    return m_globalObjects;
}

std::error_code VMEConfig::write_impl(QJsonObject &json) const
{
    QJsonArray eventArray;
    for (auto event: eventConfigs)
    {
        QJsonObject eventObject;
        event->write(eventObject);
        eventArray.append(eventObject);
    }
    json["events"] = eventArray;

    // global objects
    {
        QJsonObject globalsJson;
        m_globalObjects.write(globalsJson);
        json["global_objects"] = globalsJson;
    }

    // vme controller
    QJsonObject controllerJson;
    controllerJson["type"] = to_string(m_controllerType);
    controllerJson["settings"] = QJsonObject::fromVariantMap(m_controllerSettings);
    json["vme_controller"] = controllerJson;
    mvme::vme_config::json_schema::set_vmeconfig_version(json, GetCurrentVMEConfigVersion());

    return {};
}

std::error_code VMEConfig::read_impl(const QJsonObject &json)
{
    // Version check before trying to load. The json schema should have been
    // updated on the outside.
    {
        int version = mvme::vme_config::json_schema::get_vmeconfig_version(json);

        if (version < GetCurrentVMEConfigVersion())
            return make_error_code(VMEConfigReadResult::VersionTooOld);

        if (version > GetCurrentVMEConfigVersion())
            return make_error_code(VMEConfigReadResult::VersionTooNew);

        assert(version == GetCurrentVMEConfigVersion());
    }

    // Delete existing events
    qDeleteAll(eventConfigs);
    eventConfigs.clear();

    // Delete global objects, this includes the daq_start, daq_stop and manual
    // containers.
    for (auto child: m_globalObjects.getChildren())
    {
        m_globalObjects.removeChild(child);
        child->setParent(nullptr);
        child->deleteLater();
    }

    // Create the EventConfig instances
    QJsonArray eventArray = json["events"].toArray();

    for (int eventIndex=0; eventIndex<eventArray.size(); ++eventIndex)
    {
        QJsonObject eventObject = eventArray[eventIndex].toObject();
        EventConfig *eventConfig = new EventConfig(this);
        eventConfig->read(eventObject);
        eventConfigs.append(eventConfig);
    }
    //qDebug() << __PRETTY_FUNCTION__ << "read" << eventConfigs.size() << "event configs";

    // read global objects, create missing objects afterwards
    assert(m_globalObjects.objectName() == "global_objects");
    m_globalObjects.read(json["global_objects"].toObject());
    m_globalObjects.setObjectName("global_objects");
    m_globalObjects.setParent(this);
    createMissingGlobals();

    // old script objects. These are now stored as children of m_globalObjects instead
    QJsonObject scriptsObject = json["vme_script_lists"].toObject();

    for (auto it = scriptsObject.begin();
         it != scriptsObject.end();
         ++it)
    {
        auto category = it.key();
        auto parent = m_globalObjects.findChild<ContainerObject *>(category);

        assert(parent);
        if (!parent) continue;

        QJsonArray scriptsArray = it.value().toArray();

        for (auto arrayIter = scriptsArray.begin();
             arrayIter != scriptsArray.end();
             ++arrayIter)
        {
            VMEScriptConfig *cfg(new VMEScriptConfig(this));
            cfg->read((*arrayIter).toObject());
            parent->addChild(cfg);
        }
    }

    // vme controller
    auto controllerJson = json["vme_controller"].toObject();
    m_controllerType = from_string(controllerJson["type"].toString());
    m_controllerSettings = controllerJson["settings"].toObject().toVariantMap();

    setVMEController(m_controllerType, m_controllerSettings);

    return {};
}

ModuleConfig *VMEConfig::getModuleConfig(int eventIndex, int moduleIndex) const
{
    ModuleConfig *result = nullptr;
    auto eventConfig = eventConfigs.value(eventIndex);

    if (eventConfig)
    {
        result = eventConfig->getModuleConfigs().value(moduleIndex);
    }

    return result;
}

ModuleConfig *VMEConfig::getModuleConfig(const QUuid &moduleId) const
{
    for (auto eventConfig: eventConfigs)
        for (auto moduleConfig: eventConfig->getModuleConfigs())
            if (moduleConfig->getId() == moduleId)
                return moduleConfig;

    return nullptr;
}

EventConfig *VMEConfig::getEventConfig(const QString &name) const
{
    for (auto cfg: eventConfigs)
    {
        if (cfg->objectName() == name)
            return cfg;
    }
    return nullptr;
}

EventConfig *VMEConfig::getEventConfig(const QUuid &id) const
{
    for (auto cfg: eventConfigs)
    {
        if (cfg->getId() == id)
            return cfg;
    }
    return nullptr;
}

QList<ModuleConfig *> VMEConfig::getAllModuleConfigs() const
{
    QList<ModuleConfig *> result;

    for (auto eventConfig: eventConfigs)
    {
        for (auto moduleConfig: eventConfig->getModuleConfigs())
        {
            result.push_back(moduleConfig);
        }
    }

    return result;
}

QPair<int, int> VMEConfig::getEventAndModuleIndices(ModuleConfig *cfg) const
{
    for (int eventIndex = 0;
         eventIndex < eventConfigs.size();
         ++eventIndex)
    {
        auto moduleConfigs = eventConfigs[eventIndex]->getModuleConfigs();
        int moduleIndex = moduleConfigs.indexOf(cfg);
        if (moduleIndex >= 0)
            return qMakePair(eventIndex, moduleIndex);
    }

    return qMakePair(-1, -1);
}

std::pair<std::unique_ptr<VMEConfig>, QString>
    read_vme_config_from_file(
        const QString &filename,
        std::function<void (const QString &msg)> logger)
{
    std::pair<std::unique_ptr<VMEConfig>, QString> result;

    QFile inFile(filename);
    if (!inFile.open(QIODevice::ReadOnly))
    {
        result.second = inFile.errorString();
        return result;
    }

    auto data = inFile.readAll();

    if (data.isEmpty())
        return std::make_pair(std::make_unique<VMEConfig>(), QString{});

    QJsonParseError parseError;
    QJsonDocument doc(QJsonDocument::fromJson(data, &parseError));

    if (parseError.error != QJsonParseError::NoError)
    {
        result.second = QSL("%1 at offset %2")
            .arg(parseError.errorString())
            .arg(parseError.offset);
        return result;
    }

    if (!doc.isNull() && !doc.object().contains("DAQConfig"))
    {
        result.second = QSL("The file does not contain an mvme VMEConfig object.");
        return result;
    }

    QJsonObject json = mvme::vme_config::json_schema::convert_vmeconfig_to_current_version(
        doc.object()["DAQConfig"].toObject(), logger, {});

    auto vmeConfig = std::make_unique<VMEConfig>();

    if (auto ec = vmeConfig->read(json))
    {
        result.second = ec.message().c_str();
    }

    result.first  = std::move(vmeConfig);
    return result;
}

QString make_unique_event_name(const QString &prefix, const VMEConfig *vmeConfig)
{
    auto eventConfigs = vmeConfig->getEventConfigs();
    QSet<QString> eventNames;

    for (auto cfg: eventConfigs)
    {
        if (cfg->objectName().startsWith(prefix))
        {
            eventNames.insert(cfg->objectName());
        }
    }

    u32 suffix = 0;
    QString result = QString("%1%2").arg(prefix).arg(suffix++);

    while (eventNames.contains(result))
    {
        result = QString("%1%2").arg(prefix).arg(suffix++);
    }
    return result;
}

QString make_unique_module_name(const QString &prefix, const VMEConfig *vmeConfig)
{
    auto moduleConfigs = vmeConfig->getAllModuleConfigs();
    QSet<QString> moduleNames;

    for (auto cfg: moduleConfigs)
    {
        if (cfg->objectName().startsWith(prefix))
        {
            moduleNames.insert(cfg->objectName());
        }
    }

    QString result = prefix;
    u32 suffix = 1;
    while (moduleNames.contains(result))
    {
        result = QString("%1_%2").arg(prefix).arg(suffix++);
    }
    return result;
}

QString make_unique_name(const ConfigObject *co, const ContainerObject *destContainer)
{
    QSet<QString> destNames;
    for (auto child: destContainer->getChildren())
        destNames.insert(child->objectName());

    auto prefix = co->objectName();
    prefix.remove(QRegularExpression("\\d+$", QRegularExpression::MultilineOption));

    QString result = prefix;
    u32 suffix = 1;
    while (destNames.contains(result))
    {
        result = QString("%1%2").arg(prefix).arg(suffix++);
    }

    return result;
}

void move_module(ModuleConfig *module, EventConfig *destEvent, int destIndex)
{
    auto sourceEvent = module->getEventConfig();

    qDebug() << __PRETTY_FUNCTION__
        << "module=" << module
        << ", sourceEvent=" << sourceEvent
        << ", destEvent=" << destEvent
        << ", destIndex=" << destIndex;

    if (sourceEvent)
        sourceEvent->removeModuleConfig(module);

    destEvent->addModuleConfig(module, destIndex);
}
