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
#include "listfile_browser.h"

#include <QHeaderView>
#include <QBoxLayout>
#include <QMessageBox>
#include <QTimer>
#include <qnamespace.h>

#include "analysis/analysis.h"
#include "mvme_context.h"
#include "mvme_context_lib.h"
#include "mvme.h"

static const int PeriodicRefreshInterval_ms = 1000.0;

static const QStringList NameFilters = { QSL("*.mvmelst"), QSL("*.zip") };

ListfileBrowser::ListfileBrowser(MVMEContext *context, MVMEMainWindow *mainWindow, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
    , m_mainWindow(mainWindow)
    , m_fsModel(new QFileSystemModel(this))
    , m_fsView(new QTableView(this))
    , m_analysisLoadActionCombo(new QComboBox(this))
{
    setWindowTitle(QSL("Listfile Browser"));

    set_widget_font_pointsize(this, 8);

    m_fsModel->setReadOnly(true);
    m_fsModel->setFilter(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs);
    m_fsModel->setNameFilters(NameFilters);
    m_fsModel->setNameFilterDisables(false);

    m_fsView->setModel(m_fsModel);
    m_fsView->verticalHeader()->hide();
    m_fsView->hideColumn(2); // Hides the file type column
    m_fsView->setSortingEnabled(true);

    auto widgetLayout = new QVBoxLayout(this);

    // On listfile load
    {
        auto label = new QLabel(QSL("On listfile load"));
        auto combo = m_analysisLoadActionCombo;
        combo->addItem(QSL("keep current analysis"),        0u);
        combo->addItem(QSL("load analysis from listfile"),  OpenListfileFlags::LoadAnalysis);

        auto layout = new QHBoxLayout;
        layout->addWidget(label);
        layout->addWidget(combo);
        layout->addStretch();

        widgetLayout->addLayout(layout);
    }

    widgetLayout->addWidget(m_fsView);

    connect(m_context, &MVMEContext::workspaceDirectoryChanged,
            this, [this](const QString &) { onWorkspacePathChanged(); });

    connect(m_context, &MVMEContext::daqStateChanged,
            this, &ListfileBrowser::onGlobalStateChanged);

    connect(m_context, &MVMEContext::modeChanged,
            this, &ListfileBrowser::onGlobalStateChanged);

    connect(m_fsModel, &QFileSystemModel::directoryLoaded, this, [this](const QString &) {
        m_fsView->resizeColumnsToContents();
        m_fsView->resizeRowsToContents();
    });

    connect(m_fsView, &QAbstractItemView::doubleClicked,
            this, &ListfileBrowser::onItemDoubleClicked);

    onWorkspacePathChanged();
    onGlobalStateChanged();
    m_fsView->horizontalHeader()->restoreState(QSettings().value("ListfileBrowser/HorizontalHeaderState").toByteArray());

    auto refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &ListfileBrowser::periodicUpdate);
    refreshTimer->setInterval(PeriodicRefreshInterval_ms);
    refreshTimer->start();
}

ListfileBrowser::~ListfileBrowser()
{
    QSettings().setValue("ListfileBrowser/HorizontalHeaderState", m_fsView->horizontalHeader()->saveState());
}

void ListfileBrowser::onWorkspacePathChanged()
{
    auto workspaceDirectory = m_context->getWorkspaceDirectory();
    auto workspaceSettings  = m_context->makeWorkspaceSettings();

    QDir dir(workspaceDirectory);
    QString listfileDirectory = dir.filePath(
        workspaceSettings->value(QSL("ListFileDirectory")).toString());

    m_fsModel->setRootPath(listfileDirectory);
    m_fsView->setRootIndex(m_fsModel->index(listfileDirectory));
}

void ListfileBrowser::onGlobalStateChanged()
{
    qDebug() << __PRETTY_FUNCTION__;
    auto globalMode = m_context->getMode();
    auto daqState   = m_context->getDAQState();

    bool disableBrowser = (globalMode == GlobalMode::DAQ && daqState != DAQState::Idle);

    m_fsView->setEnabled(!disableBrowser);
}

void ListfileBrowser::periodicUpdate()
{
    // FIXME: does not update file sizes reliably. calling reset() on the view
    // doesn't fix the size problem and also makes selections and stuff go
    // away, which is not desired at all.
    // Why is there no easy way to force a refresh for the current root index
    // of the view? Why is this stuff always difficult and never easy?
    auto rootPath = m_fsModel->rootPath();
    //qDebug() << __PRETTY_FUNCTION__ << "epicly failing to fail!!1111 rootPath=" << rootPath;
    m_fsModel->setRootPath(QSL(""));
    m_fsModel->setRootPath(rootPath);
    m_fsView->setRootIndex(m_fsModel->index(rootPath));
}

static const QString AnalysisFileFilter = QSL("MVME Analysis Files (*.analysis);; All Files (*.*)");

void ListfileBrowser::onItemDoubleClicked(const QModelIndex &mi)
{
    if (m_context->getMode() == GlobalMode::DAQ
        && m_context->getDAQState() != DAQState::Idle)
    {
        return;
    }

    if (m_context->getVMEConfig()->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, "Save configuration?",
                           "The current VME configuration has modifications. Do you want to save it?",
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            if (!m_mainWindow->onActionSaveVMEConfig_triggered())
            {
                return;
            }
        }
        else if (result == QMessageBox::Cancel)
        {
            return;
        }
    }


    u16 flags = m_analysisLoadActionCombo->currentData().toUInt(0);

    if ((flags & OpenListfileFlags::LoadAnalysis) && m_context->getAnalysis()->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, QSL("Save analysis configuration?"),
                           QSL("The current analysis configuration has modifications. Do you want to save it?"),
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);

        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            auto result = saveAnalysisConfig(
                m_context->getAnalysis(),
                m_context->getAnalysisConfigFileName(),
                m_context->getWorkspaceDirectory(),
                AnalysisFileFilter,
                m_context);

            if (!result.first)
            {
                m_context->logMessage(QSL("Error: ") + result.second);
                return;
            }
        }
        else if (result == QMessageBox::Cancel)
        {
            return;
        }
    }

    auto filename = m_fsModel->filePath(mi);

    try
    {
        const auto &replayHandle = context_open_listfile(m_context, filename, flags);

        if (!replayHandle.messages.isEmpty())
        {
            m_context->logMessageRaw(QSL(">>>>> Begin listfile log"));
            m_context->logMessageRaw(replayHandle.messages);
            m_context->logMessageRaw(QSL("<<<<< End listfile log"));
        }
        m_mainWindow->updateWindowTitle();
    }
    catch (const QString &e)
    {
        QMessageBox::critical(this, QSL("Error opening listfile"),
                              QString("Error opening listfile %1: %2")
                              .arg(filename)
                              .arg(e));
    }
}
