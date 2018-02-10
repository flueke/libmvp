#include "rate_monitoring.h"

#include <QApplication>
#include <QDebug>
#include <QTimer>
#include <QBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <random>
#include "util/typedefs.h"
#include <qwt_legend.h>
#include <qwt_plot.h>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const size_t BufferCapacity = 100;
    const s32 ReplotPeriod_ms = 500;
    const s32 NewDataPeriod_ms = 250;

    auto rateHistory = std::make_shared<RateHistoryBuffer>(BufferCapacity);

    rateHistory->push_back(1);
    rateHistory->push_back(2);
    rateHistory->push_back(3);
    rateHistory->push_back(4);
    rateHistory->push_back(5);

    // Plot and external legend
    auto plotWidget = new RateMonitorPlotWidget;
    QwtLegend legend;

    QObject::connect(plotWidget->getPlot(), &QwtPlot::legendDataChanged,
                     &legend, &QwtLegend::updateLegend);

    legend.setDefaultItemMode(QwtLegendData::Checkable);


    // set plot data and show widgets
    plotWidget->setRateHistoryBuffer(rateHistory);

    auto leftWidget = new QWidget;
    auto leftLayout = new QVBoxLayout(leftWidget);

    auto gb_scale = new QGroupBox("Y Scale");
    {
        auto rb_scaleLin = new QRadioButton;
        auto rb_scaleLog = new QRadioButton;

        QObject::connect(rb_scaleLin, &QRadioButton::toggled, gb_scale, [=](bool checked) {
            if (checked) plotWidget->setYAxisScale(AxisScale::Linear);
        });

        QObject::connect(rb_scaleLog, &QRadioButton::toggled, gb_scale, [=](bool checked) {
            if (checked) plotWidget->setYAxisScale(AxisScale::Logarithmic);
        });

        rb_scaleLin->setChecked(true);

        auto l = new QFormLayout(gb_scale);
        l->addRow("Linear", rb_scaleLin);
        l->addRow("Logarithmic", rb_scaleLog);

        leftLayout->addWidget(gb_scale);
    }
    

    QWidget mainWidget;
    auto mainLayout = new QHBoxLayout(&mainWidget);
    mainLayout->addWidget(leftWidget);
    mainLayout->addWidget(plotWidget);

    mainWidget.show();

    //
    // Replot timer
    //
    QTimer replotTimer;
    QObject::connect(&replotTimer, &QTimer::timeout, plotWidget, [&] () {
        plotWidget->replot();
    });

    replotTimer.setInterval(ReplotPeriod_ms);
    replotTimer.start();

    //
    // Fill timer
    //
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<double> dist(0.0, 15.0);

    static const double SinOffset = 2.0;
    static const double SinScale = 15.0;
    static const double SinInc = 0.25;
    double x = 0.0;

    QTimer fillTimer;
    QObject::connect(&fillTimer, &QTimer::timeout, plotWidget, [&] () {
        //double value = dist(gen);
        double value = (std::sin(x) + SinOffset) * SinScale;
        x += SinInc;
        rateHistory->push_back(value);
    });

    fillTimer.setInterval(NewDataPeriod_ms);
    fillTimer.start();

    return app.exec();
}
