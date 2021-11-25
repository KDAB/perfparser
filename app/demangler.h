/****************************************************************************
**
** Copyright (C) 2021 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com, author Lieven Hey <lieven.hey@kdab.com>
** Contact: http://www.qt.io/licensing/
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

#ifndef DEMANGLER_H
#define DEMANGLER_H

#include <QVector>

class Demangler
{
public:
    Demangler();

    bool demangle(const char* mangledSymbol, char* demangleBuffer, size_t demangleBufferLength);

private:
    void loadDemangleLib(const QString& name, const char* function, const QByteArray& prefix);

    using demangler_t = int (*) (const char*, char *, size_t);
    struct DemangleInfo {
        QByteArray prefix;
        demangler_t demangler;
    };

    QVector<DemangleInfo> m_demanglers;
};

#endif // DEMANGLER_H
