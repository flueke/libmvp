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
#include "rate_monitor_plot_widget.h"

#include <QBoxLayout>
#include <QDebug>

#include <cmath>
#include <qwt_curve_fitter.h>
#include <qwt_date_scale_draw.h>
#include <qwt_date_scale_engine.h>
#include <qwt_plot_curve.h>
#include <qwt_plot.h>
#include <qwt_plot_legenditem.h>
#include <qwt_scale_engine.h>
#include <qwt_scale_widget.h>

#include "analysis/a2/util/nan.h"
#include "histo_util.h"
#include "qt_util.h"
#include "scrollzoomer.h"
#include "util/assert.h"
#include "util/counters.h"
#include "util.h"

//
// RateMonitorPlotWidget
//

static inline QRectF make_bounding_rect(const RateSampler *sampler)
{
    QRectF result;

    if (!sampler->rateHistory.empty())
    {
        double xMin = sampler->getFirstSampleTime();
        double xMax = sampler->getLastSampleTime();
        double yMax = get_max_value(sampler->rateHistory);

        result = QRectF(xMin * 1000.0, 0.0,
                        xMax * 1000.0, yMax);
    }

    return result;
}

struct RateMonitorPlotData: public QwtSeriesData<QPointF>
{
    explicit RateMonitorPlotData(const RateSamplerPtr &sampler)
        : QwtSeriesData<QPointF>()
        , sampler(sampler)
    { }

    size_t size() const override
    {
        return sampler->historySize();
    }

    // Note: if getSample(i) returns a NaN value this method will search
    // backwards until it finds a non-NaN sample and return that value. This
    // fixes severe performance issues when plotting data which includes NaNs
    // while also being a visual improvement over replacing the NaN with 0.
    //
    // Note2: NaNs are frequently recorded when using the VMMR to read out MMR
    // monitoring data as that happens on a best-effort basis.
    //
    // Note3 (210310): Things are very slow when many NaNs are present. Trying
    // to implementing something faster by remembering the last valid sample
    // value.
    virtual QPointF sample(size_t i) const override
    {
        double x = sampler->getSampleTime(i) * 1000.0;
        double y = sampler->getSample(i);
        auto si = static_cast<ssize_t>(i);

        if (std::isnan(y))
        {
            if (si > prevSampleIndex_)
                y = prevSampleValue_;
        }

        if (std::isnan(y))
            y = 0.0;

        prevSampleIndex_ = si;
        prevSampleValue_ = y;

        return {x, y};
    }

    virtual QRectF boundingRect() const override
    {
        auto result = make_bounding_rect(sampler.get());
        return result;
    }

    RateSamplerPtr sampler;

    mutable ssize_t prevSampleIndex_;
    mutable double prevSampleValue_;

    void beginDrawing() const
    {
        prevSampleIndex_ = -1;
        prevSampleValue_ = 0.0;
    }
};

class RateMonitorPlotCurve: public QwtPlotCurve
{
    public:
        using QwtPlotCurve::QwtPlotCurve;

    protected:
        void drawLines(QPainter *painter,
                       const QwtScaleMap &xMap, const QwtScaleMap &yMap,
                       const QRectF &canvasRect, int from, int to ) const override
        {
            auto rmpd = reinterpret_cast<const RateMonitorPlotData *>(data());
            rmpd->beginDrawing();

            auto tStart = std::chrono::steady_clock::now();

            QwtPlotCurve::drawLines(painter, xMap, yMap, canvasRect, from, to);

            auto dt = std::chrono::steady_clock::now() - tStart;

            qDebug() << __PRETTY_FUNCTION__ << "dt =" <<
                std::chrono::duration_cast<std::chrono::milliseconds>(dt).count() << "ms";
        }
};


struct RateMonitorPlotWidgetPrivate
{
    QVector<RateSamplerPtr> m_samplers;

    QwtPlot *m_plot;
    ScrollZoomer *m_zoomer;
    QVector<QwtPlotCurve *> m_curves;
    QwtPlotLegendItem m_plotLegendItem;
};

struct DateScaleFormat
{
    QwtDate::IntervalType interval;
    QString format;
};

static const DateScaleFormat DateScaleFormatTable[] =
{
    { QwtDate::Millisecond,     QSL("H'h' m'm' s's' zzz'ms'") },
    { QwtDate::Second,          QSL("H'h' m'm' s's'") },
    { QwtDate::Minute,          QSL("H'h' m'm'") },
    { QwtDate::Hour,            QSL("H'h' m'm'") },
    { QwtDate::Day,             QSL("d 'd'") },
};

RateMonitorPlotWidget::RateMonitorPlotWidget(QWidget *parent)
    : QWidget(parent)
    , m_d(std::make_unique<RateMonitorPlotWidgetPrivate>())
{
    // plot and curve
    m_d->m_plot = new QwtPlot(this);
    m_d->m_plot->canvas()->setMouseTracking(true);
    m_d->m_plot->axisWidget(QwtPlot::yLeft)->setTitle("Rate");

    {
        auto engine = new QwtDateScaleEngine(Qt::UTC);
        m_d->m_plot->setAxisScaleEngine(QwtPlot::xBottom, engine);

        auto draw = new QwtDateScaleDraw(Qt::UTC);

        for (size_t i = 0; i < ArrayCount(DateScaleFormatTable); i++)
        {
            draw->setDateFormat(
                DateScaleFormatTable[i].interval,
                DateScaleFormatTable[i].format);
        }

        m_d->m_plot->setAxisScaleDraw(QwtPlot::xBottom, draw);
    }

    m_d->m_plotLegendItem.attach(m_d->m_plot);

    // zoomer
    m_d->m_zoomer = new ScrollZoomer(m_d->m_plot->canvas());

    /* NOTE: using connect with the c++ pointer-to-member syntax with these qwt signals does
     * not work for some reason. */
    // TODO: use casts to select specific overloads of these signals
    TRY_ASSERT(connect(m_d->m_zoomer, SIGNAL(zoomed(const QRectF &)),
                       this, SLOT(zoomerZoomed(const QRectF &))));
    TRY_ASSERT(connect(m_d->m_zoomer, &ScrollZoomer::mouseCursorMovedTo,
                       this, &RateMonitorPlotWidget::mouseCursorMovedToPlotCoord));
    TRY_ASSERT(connect(m_d->m_zoomer, &ScrollZoomer::mouseCursorLeftPlot,
                       this, &RateMonitorPlotWidget::mouseCursorLeftPlot));

    // layout
    auto widgetLayout = new QHBoxLayout(this);
    widgetLayout->setContentsMargins(0, 0, 0, 0);
    widgetLayout->setSpacing(0);
    widgetLayout->addWidget(m_d->m_plot);

    setYAxisScale(AxisScale::Linear);
}

RateMonitorPlotWidget::~RateMonitorPlotWidget()
{
    qDeleteAll(m_d->m_curves);
}

void RateMonitorPlotWidget::addRateSampler(const RateSamplerPtr &sampler,
                                           const QString &title,
                                           const QColor &color)
{
    assert(sampler);
    assert(m_d->m_samplers.size() == m_d->m_curves.size());

    auto curve = std::make_unique<RateMonitorPlotCurve>(title);

    curve->setData(new RateMonitorPlotData(sampler));
    curve->setPen(color);
    curve->setStyle(QwtPlotCurve::Lines);
    //curve->setPaintAttribute(QwtPlotCurve::FilterPoints);
    curve->setRenderHint(QwtPlotItem::RenderAntialiased);

    curve->attach(m_d->m_plot);

    m_d->m_curves.push_back(curve.release());
    m_d->m_samplers.push_back(sampler);

#if 0
    qDebug() << __PRETTY_FUNCTION__ << "added rate. title =" << title << ", color =" << color
        << ", capacity =" << sampler->historyCapacity()
        << ", new plot count =" << m_d->m_curves.size();
#endif

    assert(m_d->m_samplers.size() == m_d->m_curves.size());
}

void RateMonitorPlotWidget::removeRateSampler(const RateSamplerPtr &sampler)
{
    removeRateSampler(m_d->m_samplers.indexOf(sampler));
}

void RateMonitorPlotWidget::removeRateSampler(int index)
{
    assert(m_d->m_samplers.size() == m_d->m_curves.size());

    if (0 <= index && index < m_d->m_samplers.size())
    {
        assert(index < m_d->m_curves.size());

#if 0
        qDebug() << __PRETTY_FUNCTION__ << "removing plot with index" << index;
#endif
        auto curve = std::unique_ptr<QwtPlotCurve>(m_d->m_curves.at(index));
        curve->detach();
        m_d->m_curves.remove(index);
        m_d->m_samplers.remove(index);
    }

    assert(m_d->m_samplers.size() == m_d->m_curves.size());
}

void RateMonitorPlotWidget::removeAllRateSamplers()
{
    while (rateCount())
    {
        removeRateSampler(0);
    }
}

int RateMonitorPlotWidget::rateCount() const
{
    assert(m_d->m_samplers.size() == m_d->m_curves.size());
    return m_d->m_samplers.size();
}

QVector<RateSamplerPtr> RateMonitorPlotWidget::getRateSamplers() const
{
    assert(m_d->m_samplers.size() == m_d->m_curves.size());
    return m_d->m_samplers;
}

RateSamplerPtr RateMonitorPlotWidget::getRateSampler(int index) const
{
    return m_d->m_samplers.value(index);
}

bool RateMonitorPlotWidget::isInternalLegendVisible() const
{
    return m_d->m_plotLegendItem.plot();
}

void RateMonitorPlotWidget::setInternalLegendVisible(bool visible)
{
    if (visible && !m_d->m_plotLegendItem.plot())
        m_d->m_plotLegendItem.attach(m_d->m_plot);
    else if (!visible && m_d->m_plotLegendItem.plot())
        m_d->m_plotLegendItem.detach();
}

static bool axis_is_lin(QwtPlot *plot, QwtPlot::Axis axis)
{
    return dynamic_cast<QwtLinearScaleEngine *>(plot->axisScaleEngine(axis));
}

static bool axis_is_log(QwtPlot *plot, QwtPlot::Axis axis)
{
    return dynamic_cast<QwtLogScaleEngine *>(plot->axisScaleEngine(axis));
}

void RateMonitorPlotWidget::replot()
{
    // Determine x-axis range by looking at the first and last sample time of each rate sampler.
    double xMin = std::numeric_limits<double>::max();
    double xMax = std::numeric_limits<double>::lowest();

    if (m_d->m_zoomer->zoomRectIndex() == 0) // fully zoomed out
    {
        bool hasSamples = false;

        for (auto &sampler: m_d->m_samplers)
        {
            if (!sampler->rateHistory.empty())
            {
                xMin = std::min(xMin, sampler->getFirstSampleTime());
                xMax = std::max(xMax, sampler->getLastSampleTime());
                hasSamples = true;
            }
        }

        if (!hasSamples)
        {
            xMin = 0.0;
            xMax = 60.0;
        }
    }
    else // zoomed in
    {
        auto scaleDiv = m_d->m_plot->axisScaleDiv(QwtPlot::xBottom);
        xMin = scaleDiv.lowerBound() / 1000.0;
        xMax = scaleDiv.upperBound() / 1000.0;
    }

    // scale the x-values to milliseconds
    double xMin_ms = xMin * 1000.0;
    double xMax_ms = xMax * 1000.0;

    // if fully zoomed out set the x-interval to the min and max x-value over
    // all the samplers
    if (m_d->m_zoomer->zoomRectIndex() == 0)
        m_d->m_plot->setAxisScale(QwtPlot::xBottom, xMin_ms, xMax_ms);

    AxisInterval visibleXInterval_s = { xMin, xMax };

    // y-axis range

    double yMin = 0.0;
    double yMax = 1.0;

    for (auto &sampler: m_d->m_samplers)
    {
        if (!sampler->rateHistory.empty())
        {
            auto stats = calc_rate_sampler_stats(*sampler, visibleXInterval_s);
            auto yInterval = stats.intervals[Qt::YAxis];

            if (!std::isnan(yInterval.minValue))
                yMin = std::min(yMin, yInterval.minValue);

            if (!std::isnan(yInterval.maxValue))
                yMax = std::max(yMax, yInterval.maxValue);
        }
    }

    if (axis_is_log(m_d->m_plot, QwtPlot::yLeft))
    {
        yMin = 1.0;
        yMax = std::pow(yMax, 1.2);
    }
    else
    {
        yMax = yMax * 1.2;
    }

    m_d->m_plot->setAxisScale(QwtPlot::yLeft, yMin, yMax);

    // Tell Qwt to update and render
    m_d->m_plot->updateAxes();
    m_d->m_plot->replot();
}

void RateMonitorPlotWidget::setYAxisScale(AxisScale scaleType)
{

    if (scaleType == AxisScale::Linear && !axis_is_lin(m_d->m_plot, QwtPlot::yLeft))
    {
        m_d->m_plot->setAxisScaleEngine(QwtPlot::yLeft, new QwtLinearScaleEngine);
        m_d->m_plot->setAxisAutoScale(QwtPlot::yLeft, true);
    }
    else if (scaleType == AxisScale::Logarithmic && !axis_is_log(m_d->m_plot, QwtPlot::yLeft))
    {
        auto scaleEngine = new QwtLogScaleEngine;
        scaleEngine->setTransformation(new MinBoundLogTransform);
        m_d->m_plot->setAxisScaleEngine(QwtPlot::yLeft, scaleEngine);
    }

    replot();
}

AxisScale RateMonitorPlotWidget::getYAxisScale() const
{
    if (axis_is_lin(m_d->m_plot, QwtPlot::yLeft))
        return AxisScale::Linear;

    assert(axis_is_log(m_d->m_plot, QwtPlot::yLeft));

    return AxisScale::Logarithmic;
}

void RateMonitorPlotWidget::zoomerZoomed(const QRectF &)
{
    replot();
}

void RateMonitorPlotWidget::mouseCursorMovedToPlotCoord(QPointF)
{
}

void RateMonitorPlotWidget::mouseCursorLeftPlot()
{
}

QwtPlot *RateMonitorPlotWidget::getPlot()
{
    return m_d->m_plot;
}

QwtPlotCurve *RateMonitorPlotWidget::getPlotCurve(const RateSamplerPtr &sampler) const
{
    int index = m_d->m_samplers.indexOf(sampler);

    if (0 < index && index < m_d->m_samplers.size())
    {
        assert(index < m_d->m_curves.size());
        return m_d->m_curves.at(index);
    }

    return nullptr;
}

QwtPlotCurve *RateMonitorPlotWidget::getPlotCurve(int index) const
{
    if (0 < index && index < m_d->m_samplers.size())
    {
        assert(index < m_d->m_curves.size());
        return m_d->m_curves.at(index);
    }

    return nullptr;
}

QVector<QwtPlotCurve *> RateMonitorPlotWidget::getPlotCurves() const
{
    return m_d->m_curves;
}

ScrollZoomer *RateMonitorPlotWidget::getZoomer() const
{
    return m_d->m_zoomer;
}
