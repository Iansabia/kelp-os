/*
 * openclaw.h — Shared kernel/userspace ioctl definitions
 *
 * This header is used by both the kernel module and userspace tools.
 */
#ifndef _OPENCLAW_KMOD_H
#define _OPENCLAW_KMOD_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
#else
#include <sys/ioctl.h>
#include <stdint.h>
#endif

#define OPENCLAW_DEVICE_NAME  "oclaw"
#define OPENCLAW_CLASS_NAME   "openclaw"
#define OPENCLAW_PROC_NAME    "openclaw"

/* ioctl magic number */
#define OCLAW_IOC_MAGIC  'O'

/* ioctl commands */
#define OCLAW_IOC_GET_VERSION    _IOR(OCLAW_IOC_MAGIC, 0, struct oclaw_version)
#define OCLAW_IOC_GET_STATS      _IOR(OCLAW_IOC_MAGIC, 1, struct oclaw_stats)
#define OCLAW_IOC_SET_LOG_LEVEL  _IOW(OCLAW_IOC_MAGIC, 2, int)
#define OCLAW_IOC_ENABLE_NF      _IOW(OCLAW_IOC_MAGIC, 3, int)
#define OCLAW_IOC_QUERY_STATUS   _IOR(OCLAW_IOC_MAGIC, 4, struct oclaw_status)

#define OCLAW_IOC_MAXNR  4

/* Shared structures */
struct oclaw_version {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
    char     build[64];
};

struct oclaw_stats {
    uint64_t messages_processed;
    uint64_t bytes_read;
    uint64_t bytes_written;
    uint64_t active_sessions;
    uint64_t netfilter_packets;
    uint64_t netfilter_blocked;
    uint64_t uptime_seconds;
};

struct oclaw_status {
    int      netfilter_enabled;
    int      log_level;
    int      chardev_open_count;
    uint64_t start_time;
};

/* Maximum message size through /dev/oclaw */
#define OCLAW_MAX_MSG_SIZE  (64 * 1024)

/* Netfilter action codes */
#define OCLAW_NF_LOG_ONLY   0
#define OCLAW_NF_ANALYZE    1

#endif /* _OPENCLAW_KMOD_H */
