#include "perfunwind.h"
#include "perfregisterinfo.h"
#include <QDebug>
#include <endian.h>
#include <errno.h>
#include <dwarf.h>


static char *debuginfoPath = "/home/ulf/Qt/Boot2Qt-3.x/beaglebone-eLinux/qt/lib:/media/rootfs/usr/bin";

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
    qDebug() << "standard_find_debuginfo" << a << b << c << d << e << f << g << h;
    return dwfl_offline_section_address(a, b, c, d, e, f, g, h);
}

static const Dwfl_Callbacks offlineCallbacks = {
	build_id_find_elf,
    standard_find_debuginfo,
    offline_section_address,
    &debuginfoPath
};

PerfUnwind::PerfUnwind(const PerfHeader *header, const PerfFeatures *features) :
    header(header), features(features), registerArch(PerfRegisterInfo::s_numArchitectures)
{
    const QByteArray &featureArch = features->architecture();
    for (uint i = 0; i < PerfRegisterInfo::s_numArchitectures; ++i) {
        if (featureArch == PerfRegisterInfo::s_archNames[i]) {
            registerArch = i;
            break;
        }
    }
    dwfl = dwfl_begin(&offlineCallbacks);
    if (!dwfl)
        qWarning() << "failed to initialize dwfl";
}

PerfUnwind::~PerfUnwind()
{
    dwfl_end(dwfl);
}

void PerfUnwind::report(const PerfRecordMmap &mmap)
{

}

struct UnwindInfo {
    const PerfFeatures *features;
    const PerfRecordSample *sample;
    uint registerArch;
};

struct AddrLocation {};




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
    qDebug() << "memoryRead";
    Q_UNUSED(dwfl)

    /* Check overflow. */
    if (addr + sizeof(Dwarf_Word) < addr) {
        qWarning() << "Invalid memory read requested by dwfl" << addr;
		return false;
    }

	const UnwindInfo *ui = static_cast<UnwindInfo *>(arg);
    const QByteArray &stack = ui->sample->userStack();

	quint64 start = ui->sample->registerValue(PerfRegisterInfo::s_perfSp[ui->registerArch]);
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
    uint numRegs = PerfRegisterInfo::s_numRegisters[ui->registerArch][abi];
    Dwarf_Word dwarfRegs[numRegs];
    for (uint i = 0; i < numRegs; ++i) {
        uint offset = PerfRegisterInfo::s_perfToDwarf[ui->registerArch][abi][i];
        if (offset < numRegs)
            dwarfRegs[i] = userRegs[offset];
    }

    return dwfl_thread_state_registers(thread, 0, numRegs, dwarfRegs);
    return true;
}

static const Dwfl_Thread_Callbacks callbacks = {
	nextThread, NULL, memoryRead, setInitialRegisters, NULL, NULL
};


static int frameCallback(Dwfl_Frame *state, void *arg)
{
    Q_UNUSED(arg);
	Dwarf_Addr pc;

	if (!dwfl_frame_pc(state, &pc, NULL)) {
		qWarning() << dwfl_errmsg(-1);
		return DWARF_CB_ABORT;
	}

    qDebug() << "frame" << pc;

	return DWARF_CB_OK;
}

void PerfUnwind::unwind(const PerfRecordSample &sample)
{
    UnwindInfo ui = { features, &sample, registerArch };
    quint64 ip = sample.registerValue(PerfRegisterInfo::s_perfIp[registerArch]);;


    if (dwfl)
		return;

	if (!dwfl_attach_state(dwfl, 0, sample.tid(), &callbacks, &ui)) {
        qWarning() << "failed to attach state";
        return;
    }

    if (dwfl_getthread_frames(dwfl, sample.tid(), frameCallback, &ui))
        qWarning() << "failed to get frames";
}
