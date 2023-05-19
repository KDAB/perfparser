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

#include "demangler.h"

#include <QLibrary>
#include <QDebug>

/* Demangler specification
 * int demangler(const char* mangledSymbol, char* demangledBuffer, size_t bufferSize)
 *
 * size_t is platform dependent (4 bytes on 32 bit, 8 bytes on 64 bit)
 * */

namespace {
bool startsWith(const char* string, const QByteArray& prefix) {
    return strcmp(string, prefix.constData()) == 0;
}
}

Demangler::Demangler()
{
    loadDemangleLib(QStringLiteral("rustc_demangle"), "rustc_demangle", QByteArrayLiteral("_R"));
    loadDemangleLib(QStringLiteral("d_demangle"), "demangle_symbol", QByteArrayLiteral("_D"));
}

bool Demangler::demangle(const char *mangledSymbol, char *demangleBuffer, size_t demangleBufferLength)
{
    // fast path, some languages (like rust since 1.37 or d) share a common prefix
    // try these first
    for (const auto& demangler : std::as_const(m_demanglers)) {
        if (startsWith(mangledSymbol, demangler.prefix)) {
            if (demangler.demangler(mangledSymbol, demangleBuffer, demangleBufferLength)) {
                return true;
            }
        }
    }

    for (const auto& demangler : std::as_const(m_demanglers)) {
        if (demangler.demangler(mangledSymbol, demangleBuffer, demangleBufferLength)) {
            return true;
        }
    }
    return false;
}

void Demangler::loadDemangleLib(const QString &name, const char* function, const QByteArray& prefix)
{
    QLibrary lib(name);
    if (!lib.load()) {
        qDebug("failed to load library %ls: %ls", qUtf16Printable(name), qUtf16Printable(lib.errorString()));
        return;
    }
    const auto rawSymbol = lib.resolve(function);
    if (!rawSymbol) {
        qDebug("failed to resolve %s function in library %ls: %ls", function, qUtf16Printable(lib.fileName()),
               qUtf16Printable(lib.errorString()));
        return;
    }

    m_demanglers.push_back({prefix, reinterpret_cast<Demangler::demangler_t>(rawSymbol)});
}
