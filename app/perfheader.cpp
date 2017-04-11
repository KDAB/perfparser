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

PerfHeader::PerfHeader(QIODevice *source)  :
    m_source(source), m_magic(0), m_size(0), m_attrSize(0)
{
    connect(source, &QIODevice::readyRead, this, &PerfHeader::read);
    for (uint i = 0; i < sizeof(m_features) / sizeof(quint64); ++i)
        m_features[i] = 0;
}

void PerfHeader::read()
{
    const uint featureParts = sizeof(m_features) / sizeof(quint64);

    QDataStream stream(m_source);
    if (m_size == 0) {
        const auto expectedSize = static_cast<qint64>(sizeof(m_magic) + sizeof(m_size));
        if (!m_source->isSequential() && m_source->size() < expectedSize) {
            qWarning() << "File is too small for perf header.";
            emit error();
            return;
        }

        if (m_source->bytesAvailable() < expectedSize)
            return;

        stream >> m_magic;
        if (m_magic != s_magicSame && m_magic != s_magicSwitched) {
            qWarning() << "invalid magic:" << m_magic;
            qWarning() << "we don't support V1 perf data";
            emit error();
            return;
        } else {
            stream.setByteOrder(byteOrder());
        }

        stream >> m_size;
    }

    if (m_size == s_perfHeaderSize) {
        if (!m_source->isSequential() && m_source->size() < static_cast<qint64>(m_size)) {
            qWarning() << "File is too small for perf header.";
            emit error();
            return;
        }

        if (m_source->bytesAvailable() < static_cast<qint64>(m_size - sizeof(m_magic) -
                                                             sizeof(m_size)))
            return;

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

        const auto minimumFileSize = static_cast<qint64>(dataOffset() + dataSize());
        if (!m_source->isSequential() && m_source->size() < minimumFileSize) {
            qWarning() << "File is too small to hold perf data.";
            emit error();
            return;
        }
    } else {
        // pipe header, anything to do here?
    }

    disconnect(m_source, &QIODevice::readyRead, this, &PerfHeader::read);
    emit finished();
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


