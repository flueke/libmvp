#ifndef MVME_H
#define MVME_H

#include <QMainWindow>
#include <QMap>

class Hist2D;
class DataThread;
class Diagnostics;
class HistogramCollection;
class MVMEContext;
class RealtimeData;
class VirtualMod;
class vmedevice;
class EventConfig;
class ModuleConfig;
class MVMEContextWidget;
class DAQConfig;
class DAQConfigTreeWidget;
class HistogramTreeWidget;
class VMEScriptConfig;
class ConfigObject;

class QMdiSubWindow;
class QThread;
class QTimer;
class QwtPlotCurve;
class QTextBrowser;


namespace Ui {
class mvme;
}

class mvme : public QMainWindow
{
    Q_OBJECT

public:
    explicit mvme(QWidget *parent = 0);
    ~mvme();

    virtual void closeEvent(QCloseEvent *event);
    void restoreSettings();


    MVMEContext *getContext() { return m_context; }

public slots:
    void replot();
    void drawTimerSlot();
    void displayAbout();
    void displayAboutQt();
    void clearLog();

    void openInNewWindow(QObject *object);
    void addWidgetWindow(QWidget *widget, QSize windowSize = QSize(600, 400));

protected:
    void resizeEvent(QResizeEvent *event) override;

public slots:
    void on_actionNewConfig_triggered();
    void on_actionLoadConfig_triggered();
    bool on_actionSaveConfig_triggered();
    bool on_actionSaveConfigAs_triggered();

    bool loadConfig(const QString &fileName);

private slots:
    void on_actionOpen_Listfile_triggered();
    void on_actionClose_Listfile_triggered();

    void on_actionVME_Debug_triggered();

    void onObjectClicked(QObject *obj);
    void onObjectDoubleClicked(QObject *obj);
    void onObjectAboutToBeRemoved(QObject *obj);


    void appendToLog(const QString &);
    void updateWindowTitle();
    void onConfigChanged(DAQConfig *config);

    void onDAQAboutToStart(quint32 nCycles);

    void onShowDiagnostics(ModuleConfig *config);
    void on_actionImport_Histogram_triggered();

    void on_actionVMEScriptRef_triggered();


private:
    Ui::mvme *ui;
    QTimer* drawTimer;

    // list of possibly connected VME devices

    MVMEContext *m_context;
    QTextBrowser *m_logView;
    QMap<QObject *, QList<QMdiSubWindow *>> m_objectWindows;
    DAQConfigTreeWidget *m_daqConfigTreeWidget;
    HistogramTreeWidget *m_histogramTreeWidget;

    QDockWidget *dock_daqControl,
                *dock_daqStats,
                *dock_configTree,
                *dock_histoTree,
                *dock_logView;
};

#endif // MVME_H
