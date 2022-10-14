#ifndef __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_H_
#define __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_H_

#include <QWidget>
#include "analysis_service_provider.h"

class MultiPlotWidget: public QWidget
{
    Q_OBJECT
    public:
        explicit MultiPlotWidget(AnalysisServiceProvider *asp, QWidget *parent = nullptr);
        ~MultiPlotWidget() override;

    public slots:
        void addSink(const analysis::SinkPtr &sink);

    protected:
        void dragEnterEvent(QDragEnterEvent *ev) override;
        void dragLeaveEvent(QDragLeaveEvent *ev) override;
        void dragMoveEvent(QDragMoveEvent *ev) override;
        void dropEvent(QDropEvent *ev) override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

#endif // __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_H_