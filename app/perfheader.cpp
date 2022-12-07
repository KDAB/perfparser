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

#include <cstddef>

PerfHeader::PerfHeader(QIODevice *source)  :
    m_source(source)
{
    connect(source, &QIODevice::readyRead, this, &PerfHeader::read);
    connect(source, &QIODevice::aboutToClose, this, &PerfHeader::error);
}

void PerfHeader::read()
{
    QDataStream stream(m_source);
    if (m_size == 0) {
        if (!m_source->isSequential() && m_source->size() < pipeHeaderFixedLength()) {
            qWarning() << "File is too small for perf header.";
            emit error();
            return;
        }

        if (m_source->bytesAvailable() < pipeHeaderFixedLength())
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

    if (m_size < pipeHeaderFixedLength()) {
        qWarning() << "Header claims to be smaller than magic + size:" << m_size;
        emit error();
        return;
    } else if (m_size > pipeHeaderFixedLength()) {
        // read extended header information only available in perf.data files,
        // not in piped perf streams

        if (!m_source->isSequential() && m_source->size() < fileHeaderFixedLength()) {
            qWarning() << "File is too small for perf header.";
            emit error();
            return;
        }

        if (m_source->bytesAvailable() < fileHeaderFixedLength() - pipeHeaderFixedLength())
            return;

        // file header
        stream >> m_attrSize >> m_attrs >> m_data >> m_eventTypes;
        for (auto &feature : m_features)
            stream >> feature;

        if (m_magic == s_magicSwitched && !hasFeature(HOSTNAME) && !hasFeature(CMDLINE)) {

            for (auto &feature : m_features)
            {
                static_assert(std::is_same<std::decay<decltype(feature)>::type, quint64>::value, "");
                auto feature32 = reinterpret_cast<quint32*>(&feature);
                qSwap(feature32[0], feature32[1]);
            }

            if (!hasFeature(HOSTNAME) && !hasFeature(CMDLINE)) {
                // It borked: blank it all
                qWarning() << "bad feature data:" << m_features;
                std::fill(std::begin(m_features), std::end(m_features), 0);
                setFeature(BUILD_ID);
            }
        }

        const auto minimumFileSize = static_cast<qint64>(dataOffset() + dataSize());
        if (!m_source->isSequential() && m_source->size() < minimumFileSize) {
            qWarning() << "File is too small to hold perf data.";
            emit error();
            return;
        }

        if (m_size > fileHeaderFixedLength()) {
            if (m_size > std::numeric_limits<int>::max()) {
                qWarning() << "Excessively large perf file header:" << m_size;
                emit error();
                return;
            }
            qWarning() << "Header not completely read.";
            stream.skipRawData(static_cast<int>(m_size) - fileHeaderFixedLength());
        }
    } else {
        // pipe header, anything to do here?
    }

    disconnect(m_source, &QIODevice::readyRead, this, &PerfHeader::read);
    disconnect(m_source, &QIODevice::aboutToClose, this, &PerfHeader::error);
    m_source = nullptr;
    emit finished();
}

quint16 PerfHeader::pipeHeaderFixedLength()
{
    return sizeof(m_magic) + sizeof(m_size);
}

quint16 PerfHeader::fileHeaderFixedLength()
{
    return pipeHeaderFixedLength() + sizeof(m_attrSize)
        + static_cast<unsigned long>(3 * PerfFileSection::fixedLength()) // m_attrs, m_data, m_eventTypes
        + sizeof(m_features);
}

QDataStream::ByteOrder PerfHeader::byteOrder() const
{
    // magic is read in QDataStream's default big endian mode
    return m_magic == s_magicSame ? QDataStream::BigEndian : QDataStream::LittleEndian;
}

void PerfHeader::setFeature(PerfHeader::Feature feature)
{
    Q_ASSERT(feature >= 0 && feature < FEAT_BITS);
    m_features[feature / 64] |= (1ULL << (feature % 64));
}

void PerfHeader::clearFeature(PerfHeader::Feature feature)
{
    Q_ASSERT(feature >= 0 && feature < FEAT_BITS);
    m_features[feature / 64] &= ~(1ULL << (feature % 64));
}

bool PerfHeader::hasFeature(PerfHeader::Feature feature) const
{
    Q_ASSERT(feature >= 0 && feature < FEAT_BITS);
    return (m_features[feature / 64] & (1ULL << (feature % 64))) != 0;
}


