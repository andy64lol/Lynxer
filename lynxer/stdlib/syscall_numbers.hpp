#pragma once

#include <string>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

long syscallNumberFor(const std::string& name, bool& known) {
    known = true;
#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
    if (name == "syscallGetCurrentDirectory") return SYS_getcwd;
    if (name == "syscallChangeDirectory") return SYS_chdir;
    if (name == "syscallControlInputOutput") return SYS_ioctl;
    if (name == "syscallRead") return SYS_read;
    if (name == "syscallWrite") return SYS_write;
#ifdef SYS_pread64
    if (name == "syscallPositionedRead64") return SYS_pread64;
    if (name == "syscallPositionedWrite64") return SYS_pwrite64;
#endif
    if (name == "syscallOpenAt") return SYS_openat;
    if (name == "syscallClose") return SYS_close;
#ifdef SYS_readv
    if (name == "syscallReadVector") return SYS_readv;
    if (name == "syscallWriteVector") return SYS_writev;
#endif
    if (name == "syscallSeekFile") return SYS_lseek;
    if (name == "syscallGetFileStatus") return SYS_fstat;
    if (name == "syscallGetFileStatusAt") return SYS_newfstatat;
    if (name == "syscallTruncateFile") return SYS_ftruncate;
    if (name == "syscallCheckFileAccessAt") return SYS_faccessat;
    if (name == "syscallSynchronizeFile") return SYS_fsync;
    if (name == "syscallSynchronizeFileData") return SYS_fdatasync;
    if (name == "syscallDuplicateFileDescriptor") return SYS_dup;
    if (name == "syscallDuplicateFileDescriptorAt") return SYS_dup3;
    if (name == "syscallCreatePipe") return SYS_pipe2;
    if (name == "syscallControlFileDescriptor") return SYS_fcntl;
#ifdef SYS_getdents64
    if (name == "syscallGetDirectoryEntries") return SYS_getdents64;
#endif
    if (name == "syscallReadSymbolicLink") return SYS_readlinkat;
    if (name == "syscallCreateDirectoryAt") return SYS_mkdirat;
    if (name == "syscallRemoveFileAt") return SYS_unlinkat;
    if (name == "syscallRenameFileAt") return SYS_renameat;
    if (name == "syscallCreateHardLinkAt") return SYS_linkat;
    if (name == "syscallCreateSymbolicLinkAt") return SYS_symlinkat;
    if (name == "syscallChangeFilePermissions") return SYS_fchmodat;
    if (name == "syscallChangeFileDescriptorPermissions") return SYS_fchmod;
    if (name == "syscallChangeFileOwner") return SYS_fchownat;
    if (name == "syscallChangeFileDescriptorOwner") return SYS_fchown;
    if (name == "syscallMemoryMap") return SYS_mmap;
    if (name == "syscallMemoryUnmap") return SYS_munmap;
    if (name == "syscallMemoryProtect") return SYS_mprotect;
    if (name == "syscallMemoryAdvise") return SYS_madvise;
#ifdef SYS_mremap
    if (name == "syscallMemoryRemap") return SYS_mremap;
#endif
    if (name == "syscallAdjustProgramBreak") return SYS_brk;
    if (name == "syscallExecuteProgram") return SYS_execve;
    if (name == "syscallExecuteProgramAt") return SYS_execveat;
    if (name == "syscallExitProcess") return SYS_exit;
    if (name == "syscallExitAllThreads") return SYS_exit_group;
    if (name == "syscallWaitForProcess") return SYS_wait4;
    if (name == "syscallGetProcessId") return SYS_getpid;
    if (name == "syscallGetParentProcessId") return SYS_getppid;
    if (name == "syscallSendSignal") return SYS_kill;
    if (name == "syscallCreateThread") return SYS_clone;
    if (name == "syscallGetThreadId") return SYS_gettid;
    if (name == "syscallWaitOnMemory") return SYS_futex;
    if (name == "syscallSetThreadIdAddress") return SYS_set_tid_address;
    if (name == "syscallSetRobustThreadList") return SYS_set_robust_list;
    if (name == "syscallGetRobustThreadList") return SYS_get_robust_list;
    if (name == "syscallYieldProcessor") return SYS_sched_yield;
    if (name == "syscallGetClockTime") return SYS_clock_gettime;
    if (name == "syscallGetClockResolution") return SYS_clock_getres;
    if (name == "syscallSleep") return SYS_nanosleep;
    if (name == "syscallGetRandomBytes") return SYS_getrandom;
    if (name == "syscallCreateSocket") return SYS_socket;
    if (name == "syscallCreateSocketPair") return SYS_socketpair;
    if (name == "syscallBindSocket") return SYS_bind;
    if (name == "syscallListenSocket") return SYS_listen;
    if (name == "syscallAcceptConnection") return SYS_accept;
    if (name == "syscallConnectSocket") return SYS_connect;
    if (name == "syscallSendData") return SYS_sendto;
    if (name == "syscallReceiveData") return SYS_recvfrom;
    if (name == "syscallSendMessage") return SYS_sendmsg;
    if (name == "syscallReceiveMessage") return SYS_recvmsg;
    if (name == "syscallShutdownSocket") return SYS_shutdown;
    if (name == "syscallGetSocketAddress") return SYS_getsockname;
    if (name == "syscallGetPeerAddress") return SYS_getpeername;
    if (name == "syscallSetSocketOption") return SYS_setsockopt;
    if (name == "syscallGetSocketOption") return SYS_getsockopt;
    if (name == "syscallCreateEventPoll") return SYS_epoll_create1;
    if (name == "syscallControlEventPoll") return SYS_epoll_ctl;
    if (name == "syscallInitializeInodeNotifications") return SYS_inotify_init1;
    if (name == "syscallAddInodeNotificationWatch") return SYS_inotify_add_watch;
    if (name == "syscallRemoveInodeNotificationWatch") return SYS_inotify_rm_watch;
    if (name == "syscallGetSystemInformation") return SYS_sysinfo;
    if (name == "syscallGetUnixSystemName") return SYS_uname;
    if (name == "syscallGetExtendedFileStatus") return SYS_statx;
    if (name == "syscallGetResourceUsage") return SYS_getrusage;
    if (name == "syscallGetResourceLimit") return SYS_getrlimit;
    if (name == "syscallSetResourceLimit") return SYS_setrlimit;
    if (name == "syscallControlProcess") return SYS_prctl;
    // Extended filesystem surface.
#ifdef SYS_openat2
    if (name == "syscallOpenAt2") return SYS_openat2;
#endif
#ifdef SYS_faccessat2
    if (name == "syscallCheckFileAccessAt2") return SYS_faccessat2;
#endif
#ifdef SYS_copy_file_range
    if (name == "syscallCopyFileRange") return SYS_copy_file_range;
#endif
#ifdef SYS_fallocate
    if (name == "syscallFallocateFile") return SYS_fallocate;
#endif
#ifdef SYS_syncfs
    if (name == "syscallSynchronizeFilesystem") return SYS_syncfs;
#endif
    // Processes and threads.
#ifdef SYS_clone3
    if (name == "syscallCreateThread3") return SYS_clone3;
#endif
#ifdef SYS_pidfd_open
    if (name == "syscallOpenProcessFileDescriptor") return SYS_pidfd_open;
#endif
#ifdef SYS_pidfd_send_signal
    if (name == "syscallSendSignalToProcessFileDescriptor")
        return SYS_pidfd_send_signal;
#endif
#ifdef SYS_sched_getaffinity
    if (name == "syscallGetThreadAffinity") return SYS_sched_getaffinity;
    if (name == "syscallSetThreadAffinity") return SYS_sched_setaffinity;
#endif
#ifdef SYS_getpriority
    if (name == "syscallGetThreadPriority") return SYS_getpriority;
    if (name == "syscallSetThreadPriority") return SYS_setpriority;
#endif
#ifdef SYS_waitid
    if (name == "syscallWaitForProcessId") return SYS_waitid;
#endif
    // Memory.
#ifdef SYS_mlock
    if (name == "syscallLockMemory") return SYS_mlock;
    if (name == "syscallUnlockMemory") return SYS_munlock;
#endif
#ifdef SYS_msync
    if (name == "syscallSynchronizeMemory") return SYS_msync;
#endif
#ifdef SYS_memfd_create
    if (name == "syscallCreateMemoryFileDescriptor") return SYS_memfd_create;
#endif
#ifdef SYS_mbind
    if (name == "syscallSetMemoryPolicy") return SYS_mbind;
#endif
    // Time.
#ifdef SYS_gettimeofday
    if (name == "syscallGetTimeOfDay") return SYS_gettimeofday;
#endif
#ifdef SYS_clock_nanosleep
    if (name == "syscallSleepClock") return SYS_clock_nanosleep;
#endif
#ifdef SYS_timerfd_create
    if (name == "syscallCreateTimerFileDescriptor") return SYS_timerfd_create;
    if (name == "syscallControlTimerFileDescriptor")
        return SYS_timerfd_settime;
#endif
    // Signals.
#ifdef SYS_rt_sigaction
    if (name == "syscallControlSignal") return SYS_rt_sigaction;
#endif
#ifdef SYS_rt_sigprocmask
    if (name == "syscallControlSignalMask") return SYS_rt_sigprocmask;
#endif
#ifdef SYS_signalfd4
    if (name == "syscallCreateSignalFileDescriptor") return SYS_signalfd4;
#endif
    // Sockets and event loops.
#ifdef SYS_sendmmsg
    if (name == "syscallSendMessages") return SYS_sendmmsg;
#endif
#ifdef SYS_recvmmsg
    if (name == "syscallReceiveMessages") return SYS_recvmmsg;
#endif
#ifdef SYS_accept4
    if (name == "syscallAcceptConnection4") return SYS_accept4;
#endif
#ifdef SYS_epoll_pwait2
    if (name == "syscallWaitForEvents2") return SYS_epoll_pwait2;
#endif
#ifdef SYS_eventfd2
    if (name == "syscallCreateEventFileDescriptor") return SYS_eventfd2;
#endif
    // System information.
#ifdef SYS_capget
    if (name == "syscallGetCapabilities") return SYS_capget;
    if (name == "syscallSetCapabilities") return SYS_capset;
#endif
#ifdef SYS_times
    if (name == "syscallGetSystemTimes") return SYS_times;
#endif
    // Stretch: asynchronous I/O, then sandboxing. All of these are raw
    // passthroughs; the caller builds any struct in native memory and passes
    // its address.
#ifdef SYS_io_uring_setup
    if (name == "syscallSetupIoUring") return SYS_io_uring_setup;
#endif
#ifdef SYS_io_uring_enter
    if (name == "syscallEnterIoUring") return SYS_io_uring_enter;
#endif
#ifdef SYS_io_uring_register
    if (name == "syscallRegisterIoUring") return SYS_io_uring_register;
#endif
#ifdef SYS_landlock_create_ruleset
    if (name == "syscallCreateLandlockRuleset") return SYS_landlock_create_ruleset;
#endif
#ifdef SYS_landlock_add_rule
    if (name == "syscallAddLandlockRule") return SYS_landlock_add_rule;
#endif
#ifdef SYS_landlock_restrict_self
    if (name == "syscallRestrictLandlockSelf") return SYS_landlock_restrict_self;
#endif
#ifdef SYS_seccomp
    if (name == "syscallControlSeccomp") return SYS_seccomp;
#endif
    // poll/epoll availability differs by architecture: x86-64 has poll(2) and
    // epoll_wait(2), aarch64 only their ppoll(2)/epoll_pwait(2) forms. The
    // portable wrappers follow that split.
#if defined(__x86_64__)
    if (name == "syscallPollFileDescriptors") return SYS_poll;
    if (name == "syscallWaitForEvents") return SYS_epoll_wait;
#elif defined(__aarch64__)
    if (name == "syscallPollFileDescriptors") return SYS_ppoll;
    if (name == "syscallWaitForEvents") return SYS_epoll_pwait;
#endif
    // The explicit ppoll/epoll_pwait wrappers are present on both architectures.
#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
#ifdef SYS_ppoll
    if (name == "syscallPpollFileDescriptors") return SYS_ppoll;
#endif
#ifdef SYS_epoll_pwait
    if (name == "syscallWaitForEventsWithSignalMask") return SYS_epoll_pwait;
#endif
#endif
    known = false;
    return -1;
#else
    (void)name;
    known = false;
    return -1;
#endif
}
