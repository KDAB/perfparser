/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd
** All rights reserved.
** For any questions to The Qt Company, please use contact form at http://www.qt.io/contact-us
**
** This file is part of the Qt Enterprise Perf Profiler Add-on.
**
** GNU General Public License Usage
** This file may be used under the terms of the GNU General Public License
** version 3 as published by the Free Software Foundation and appearing in
** the file LICENSE.GPLv3 included in the packaging of this file. Please
** review the following information to ensure the GNU General Public License
** requirements will be met: https://www.gnu.org/licenses/gpl.html.
**
** If you have questions regarding the use of this file, please use
** contact form at http://www.qt.io/contact-us
**
****************************************************************************/

#include "perfheader.h"
#include <QDebug>

PerfHeader::PerfHeader()  :
    m_magic(0), m_size(0), m_attrSize(0)
{
    for (uint i = 0; i < sizeof(m_features) / sizeof(quint64); ++i)
        m_features[i] = 0;
}

bool PerfHeader::read(QIODevice *source)
{
    const uint featureParts = sizeof(m_features) / sizeof(quint64);
    // TODO: when reading from a pipe we get a truncated header; add some sane defaults then

    QDataStream stream(source);
    stream >> m_magic;
    if (m_magic != s_magicSame && m_magic != s_magicSwitched) {
        qWarning() << "invalid magic:" << m_magic;
        qWarning() << "we don't support V1 perf data";
        return false;
    } else {
        stream.setByteOrder(byteOrder());
    }

    stream >> m_size;

    if ((m_size == sizeof(PerfHeader))) {
        // file header
        stream >> m_attrSize >> m_attrs >> m_data >> m_eventTypes;
        for (uint i = 0; i < featureParts; ++i)
            stream >> m_features[i];

        if (m_magic == s_magicSwitched && !hasFeature(HOSTNAME)) {

            quint32 *features32 = reinterpret_cast<quint32 *>(&m_features[0]);
            for (uint i = 0; i < featureParts; ++i)
                qSwap(features32[i * 2], features32[i * 2 + 1]);

            if (!hasFeature(HOSTNAME)) {
                // It borked: blank it all
                qWarning() << "bad feature data:" << m_features;
                for (uint i = 0; i < featureParts; ++i)
                    m_features[i] = 0;
                setFeature(BUILD_ID);
            }
        }
    } else {
        // pipe header, anything to do here?
    }

    return true;
}

QDataStream::ByteOrder PerfHeader::byteOrder() const
{
    // magic is read in QDataStream's default big endian mode
    return m_magic == s_magicSame ? QDataStream::BigEndian : QDataStream::LittleEndian;
}

void PerfHeader::setFeature(PerfHeader::Feature feature)
{
    Q_ASSERT(feature >= 0 && feature < sizeof(m_features) * sizeof(quint64));
    m_features[feature / 64] |= (1ULL << (feature % 64));
}

void PerfHeader::clearFeature(PerfHeader::Feature feature)
{
    Q_ASSERT(feature >= 0 && feature < sizeof(m_features) * sizeof(quint64));
    m_features[feature / 64] &= ~(1ULL << (feature % 64));
}

bool PerfHeader::hasFeature(PerfHeader::Feature feature) const
{
    Q_ASSERT(feature >= 0 && feature < sizeof(m_features) * sizeof(quint64));
    return (m_features[feature / 64] & (1ULL << (feature % 64))) != 0;
}


