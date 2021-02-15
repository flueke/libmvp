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
#ifndef __MVME_PLOT_UTIL_H__
#define __MVME_PLOT_UTIL_H__

#include <memory>
#include <qwt_painter.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_grid.h>
#include <qwt_plot.h>
#include <qwt_plot_histogram.h>
#include <qwt_plot_item.h>
#include <qwt_plot_magnifier.h>
#include <qwt_plot_marker.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_renderer.h>
#include <qwt_plot_textlabel.h>
#include <qwt_plot_zoneitem.h>
#include <qwt_point_data.h>
#include <qwt_scale_engine.h>
#include <qwt_scale_widget.h>
#include <qwt_text.h>
#include <qwt_widget_overlay.h>
#include "scrollzoomer.h"
#include "libmvme_export.h"
#include "util/qwt_scalepicker.h"

namespace mvme_qwt
{

class TextLabelRowLayout;

class LIBMVME_EXPORT TextLabelItem: public QwtPlotItem
{
    public:
        explicit TextLabelItem(const QwtText &title = QwtText());
        virtual ~TextLabelItem();

        void setText(const QwtText &text);
        QwtText text() const;

        void setParentLayout(TextLabelRowLayout *layout);
        TextLabelRowLayout *getParentLayout() const;

        virtual int rtti() const override { return QwtPlotItem::Rtti_PlotTextLabel; }

        virtual void draw(
            QPainter *painter,
            const QwtScaleMap &xMap, const QwtScaleMap &yMap,
            const QRectF &canvasRect) const override;

    protected:
        void invalidateCache();

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

class LIBMVME_EXPORT TextLabelRowLayout
{
    public:
        TextLabelRowLayout();
        ~TextLabelRowLayout();

        void addTextLabel(TextLabelItem *label);
        QVector<TextLabelItem *> getTextLabels() const;
        int size() const;
        void removeTextLabel(TextLabelItem *label);
        void removeTextLabel(int index);

        QRectF getPaintArea(const TextLabelItem *label, QPainter *painter,
                            const QRectF &canvasRect) const;

        void attachAll(QwtPlot *plot);

        void setMarginTop(int margin);
        int getMarginTop() const;
        void setMarginRight(int margin);
        int getMarginRight() const;
        void setSpacing(int spacing);
        int getSpacing() const;

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};


} // end namespace mvme_qwt

#endif /* __MVME_PLOT_UTIL_H__ */
