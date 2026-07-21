#ifndef LITEWIN_STRUTIL_H
#define LITEWIN_STRUTIL_H

/* Pure string helpers for collector labels. No windows.h — unit-tested natively. */

/* PDH NIC instance -> windows_exporter nic label: non-alphanumeric -> '_'. */
void lw_sanitize_nic(const char *in, char *out, int outsz);

/* PDH LogicalDisk instance ("C:", "1 C:", "0 C: D:") -> volume (last token). */
void lw_instance_volume(const char *inst, char *out, int outsz);

#endif
