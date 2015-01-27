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

#include "perfunwind.h"
#include "perfregisterinfo.h"

#include <dwarf.h>

#include <QDir>
#include <QDebug>

#include <limits>


int build_id_find_elf (Dwfl_Module *a, void **b, const char *c, Dwarf_Addr d, char **e, Elf **f)
{
    qDebug() << "build_id_find_elf" << a << b << c << d << e << f;
    return dwfl_build_id_find_elf(a, b, c, d, e, f);
}

int standard_find_debuginfo (Dwfl_Module *a, void **b, const char *c, Dwarf_Addr d, const char *e,
                             const char *f, GElf_Word g, char **h)
{
    qDebug() << "standard_find_debuginfo" << a << b << c << d << e << f << g << h;
    return dwfl_standard_find_debuginfo(a, b, c, d, e, f, g, h);
}

int offline_section_address (Dwfl_Module *a, void **b, const char *c, Dwarf_Addr d, const char *e,
                             GElf_Word f, const GElf_Shdr *g, Dwarf_Addr *h)
{
    qDebug() << "offline_section_address" << a << b << c << d << e << f << g << h;
    return dwfl_offline_section_address(a, b, c, d, e, f, g, h);
}

PerfUnwind::PerfUnwind(quint32 pid, const PerfHeader *header, const PerfFeatures *features,
                       const QByteArray &systemRoot, const QByteArray &extraLibsPath,
                       const QByteArray &appPath) :
    pid(pid), header(header), features(features),
    registerArch(PerfRegisterInfo::s_numArchitectures), systemRoot(systemRoot),
    extraLibsPath(extraLibsPath), appPath(appPath)
{
    offlineCallbacks.find_elf = build_id_find_elf;
    offlineCallbacks.find_debuginfo =  standard_find_debuginfo;
    offlineCallbacks.section_address = offline_section_address;
    QByteArray newDebugInfo = ":" + appPath + ":" + extraLibsPath + ":" + systemRoot;
    debugInfoPath = new char[newDebugInfo.length() + 1];
    debugInfoPath[newDebugInfo.length()] = 0;
    memcpy(debugInfoPath, newDebugInfo.data(), newDebugInfo.length());
    offlineCallbacks.debuginfo_path = &debugInfoPath;

    const QByteArray &featureArch = features->architecture();
    for (uint i = 0; i < PerfRegisterInfo::s_numArchitectures; ++i) {
        if (featureArch.startsWith(PerfRegisterInfo::s_archNames[i])) {
            registerArch = i;
            break;
        }
    }
}

PerfUnwind::~PerfUnwind()
{
    delete[] debugInfoPath;
}

bool findInExtraPath(QFileInfo &path, const QString &fileName)
{
    path.setFile(path.absoluteFilePath() + "/" + fileName);
    if (path.exists())
        return true;

    QDir absDir = path.absoluteDir();
    foreach (const QString &entry, absDir.entryList(QStringList(),
                                                    QDir::Dirs | QDir::NoDotAndDotDot)) {
        path.setFile(absDir, entry);
        if (findInExtraPath(path, fileName))
            return true;
    }
    return false;
}

void PerfUnwind::registerElf(const PerfRecordMmap &mmap)
{
    if (mmap.pid() != std::numeric_limits<quint32>::max() && mmap.pid() != pid)
        return;

    // No point in mapping the same file twice
    if (elfs.find(mmap.addr()) != elfs.end())
        return;

    QString fileName = QFileInfo(mmap.filename()).fileName();
    QFileInfo path;
    if (pid == mmap.pid()) {
        path.setFile(appPath + "/" + fileName);
        if (!path.isFile()) {
            path.setFile(extraLibsPath);
            if (!findInExtraPath(path, fileName))
                path.setFile(systemRoot + mmap.filename());
        }
    } else { // kernel
        path.setFile(systemRoot + mmap.filename());
    }

    if (path.isFile())
        elfs[mmap.addr()] = path;
    else
        qWarning() << "cannot find file to report for" << QString::fromLocal8Bit(mmap.filename());

}

void PerfUnwind::reportElf(quint64 ip) const
{
    QMap<quint64, QFileInfo>::ConstIterator i = elfs.upperBound(ip);
    if (i == elfs.end() || i.key() != ip) {
        if (i != elfs.begin())
            --i;
        else
            i = elfs.end();
    }

    if (i != elfs.end()) {
        if (!dwfl_report_elf(dwfl, i.value().fileName().toLocal8Bit().data(),
                             i.value().absoluteFilePath().toLocal8Bit().data(), -1, i.key(),
                             false)) {
            qWarning() << "failed to report" << i.value().absoluteFilePath() << "for"
                       << i.key() << ":" << dwfl_errmsg(dwfl_errno());
        } else {
            qDebug() << "reported" << ip << i.key() << i.value().absoluteFilePath();
        }
    } else {
        qWarning() << "no elf found for" << ip;
    }
}

struct UnwindInfo {
    const PerfUnwind *unwind;
    const PerfRecordSample *sample;
};

static pid_t nextThread(Dwfl *dwfl, void *arg, void **threadArg)
{
    /* Stop after first thread. */
    if (*threadArg != 0)
        return 0;

    *threadArg = arg;
    return dwfl_pid(dwfl);
}


static bool memoryRead(Dwfl *dwfl, Dwarf_Addr addr, Dwarf_Word *result, void *arg)
{
    //qDebug() << "memoryRead";
    Q_UNUSED(dwfl)

    /* Check overflow. */
    if (addr + sizeof(Dwarf_Word) < addr) {
        qWarning() << "Invalid memory read requested by dwfl" << addr;
        return false;
    }

    const UnwindInfo *ui = static_cast<UnwindInfo *>(arg);
    const QByteArray &stack = ui->sample->userStack();

    quint64 start = ui->sample->registerValue(PerfRegisterInfo::s_perfSp[ui->unwind->architecture()]);
    quint64 end = start + stack.size();

    if (addr < start || addr + sizeof(Dwarf_Word) > end) {
        qWarning() << "Cannot read memory at" << addr;
        qWarning() << "dwfl should only read stack state (" << start << "to" << end
                   << ") with memoryRead().";
        return false;
    }

    *result = *(Dwarf_Word *)(&stack.data()[addr - start]);
    return true;
}

bool setInitialRegisters(Dwfl_Thread *thread, void *arg)
{
    qDebug() << "initial registers";
    const UnwindInfo *ui = static_cast<UnwindInfo *>(arg);
    const QList<quint64> &userRegs = ui->sample->registers();
    quint64 abi = ui->sample->registerAbi();
    uint architecture = ui->unwind->architecture();
    uint numRegs = PerfRegisterInfo::s_numRegisters[architecture][abi];
    Dwarf_Word dwarfRegs[numRegs];
    for (uint i = 0; i < numRegs; ++i) {
        uint offset = PerfRegisterInfo::s_perfToDwarf[architecture][abi][i];
        if (offset < numRegs)
            dwarfRegs[i] = userRegs[offset];
    }

    return dwfl_thread_state_registers(thread, 0, numRegs, dwarfRegs);
}

static const Dwfl_Thread_Callbacks callbacks = {
    nextThread, NULL, memoryRead, setInitialRegisters, NULL, NULL
};

static int frameCallback(Dwfl_Frame *state, void *arg)
{
    Dwarf_Addr pc;

    bool isactivation;
    if (!dwfl_frame_pc(state, &pc, &isactivation)) {
        qWarning() << dwfl_errmsg(dwfl_errno());
        return DWARF_CB_ABORT;
    }

    Dwarf_Addr pc_adjusted = pc - (isactivation ? 0 : 1);

    /* Get PC->SYMNAME.  */
    Dwfl_Thread *thread = dwfl_frame_thread (state);
    Dwfl *dwfl = dwfl_thread_dwfl (thread);
    Dwfl_Module *mod = dwfl_addrmodule (dwfl, pc_adjusted);
    const char *symname = NULL;
    if (!mod) {
        qDebug() << "reporting from callback";
        UnwindInfo *ui = static_cast<UnwindInfo *>(arg);
        ui->unwind->reportElf(pc_adjusted);
        mod = dwfl_addrmodule (dwfl, pc_adjusted);
    }
    if (mod)
        symname = dwfl_module_addrname (mod, pc_adjusted);

    qDebug() << "frame" << pc << symname;
    return DWARF_CB_OK;
}

void PerfUnwind::unwind(const PerfRecordSample &sample)
{
    if (sample.pid() != pid) {
        qDebug() << "wrong pid" << sample.pid() << pid;
        return;
    }
    UnwindInfo ui = { this, &sample };


    dwfl = dwfl_begin(&offlineCallbacks);

    if (!dwfl) {
        qWarning() << "failed to initialize dwfl" << dwfl_errmsg(dwfl_errno());
        return;
    }

    quint64 ip = sample.registerValue(PerfRegisterInfo::s_perfIp[registerArch]);;
    reportElf(ip);

	if (!dwfl_attach_state(dwfl, 0, sample.tid(), &callbacks, &ui)) {
        qWarning() << "failed to attach state:" << dwfl_errmsg(dwfl_errno());
        return;
    }

    if (dwfl_getthread_frames(dwfl, sample.tid(), frameCallback, &ui))
        qWarning() << "failed to get frames:" << dwfl_errmsg(dwfl_errno());

    dwfl_end(dwfl);
}
