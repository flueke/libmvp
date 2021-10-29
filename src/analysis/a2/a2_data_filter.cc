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
#include "a2_data_filter.h"

#include <cctype>
#include <stdexcept>

namespace
{
    inline std::string remove_spaces(const std::string &input)
    {
        std::string result;

        for (auto c: input)
        {
            if (c != ' ')
                result.push_back(c);
        }

        return result;
    }
}

namespace a2
{
namespace data_filter
{
    DataFilter make_filter(const std::string &filterRaw, s32 wordIndex)
    {
        auto filter = remove_spaces(filterRaw);

        if (filter.size() > FilterSize)
            throw std::length_error("maximum filter size of 32 exceeded");

        DataFilter result;
        result.filter.fill('X');
        result.matchWordIndex = wordIndex;

        for (s32 isrc=filter.size()-1, idst=0;
             isrc >= 0;
             --isrc, ++idst)
        {
            result.filter[idst] = filter[isrc];
        }

        for (s32 i=0; i<FilterSize; ++i)
        {
            char c = result.filter[i];

            if (c == '0' || c == '1' || c == 0 || c == 1)
                result.matchMask |= 1 << i;

            if (c == '1' || c == 1)
                result.matchValue |= 1 << i;
        }

        return result;
    }

    CacheEntry make_cache_entry(const DataFilter &filter, char marker)
    {
        marker = std::tolower(marker);

        CacheEntry result;

#ifndef A2_DATA_FILTER_ALWAYS_GATHER
        bool markerSeen = false;
        bool gapSeen = false;
#endif

        for (s32 i=0; i<FilterSize; ++i)
        {
            char c = std::tolower(filter.filter[i]);

            if (c == marker)
            {
#ifndef A2_DATA_FILTER_ALWAYS_GATHER
                if (markerSeen && gapSeen)
                {
                    // Had marker and a gap, now on marker again -> need gather step
                    result.needGather = true;
                }
                markerSeen = true;
#endif

                result.extractMask |= 1 << i;
            }
#ifndef A2_DATA_FILTER_ALWAYS_GATHER
            else if (markerSeen)
            {
                gapSeen = true;
            }
#endif
        }

#ifndef A2_DATA_FILTER_ALWAYS_GATHER
        result.extractShift = trailing_zeroes(result.extractMask);
#endif
        result.extractBits  = number_of_set_bits(result.extractMask);

        return result;
    }

    std::string to_string(const DataFilter &filter)
    {
        std::string result(filter.filter.size(), 'X');

        for (s32 isrc = filter.filter.size() - 1, idst = 0;
             isrc >= 0;
             --isrc, ++idst)
        {
            result[idst] = filter.filter[isrc];
        }

        return result;
    }

} // namespace data_filter
} // namespace a2
