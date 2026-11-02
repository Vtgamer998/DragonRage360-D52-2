/* SPDX-License-Identifier: MIT
 * xsdk file API usage as in src/main.c of the demo (xsdk/samples/template/main.c and src/file.c, MIT, Mena). */
#include "xb_disc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "xsdk/kernel.h"
#include "xsdk/log.h"

#define XB_PATH_MAX 300
#define XB_READ_CHUNK (1u << 20) /* one NtReadFile never asks for more than this (TODO-VERIFICAR: no known limit) */

static HANDLE sImage;
static dr_disc sDisc;
static int sMounted;
static uint64_t sGeneration;
uint64_t XbDisc_Generation(void) {return sGeneration;}

static int image_read(void *ctx, uint64_t offset, void *data, size_t n) {
    HANDLE h = *(HANDLE *)ctx;
    uint8_t *out = data;

    while (n > 0) {
        size_t part = n < XB_READ_CHUNK ? n : XB_READ_CHUNK;
        IO_STATUS_BLOCK io;
        uint64_t at = offset;
        NTSTATUS status = NtReadFile(h, 0, NULL, NULL, &io, out, (uint32_t)part, &at);

        if (!NT_SUCCESS(status) || io.information != part) {
            xsdk_log_write("disc: NtReadFile offset %llu size %u: 0x%08X", (unsigned long long)offset, (unsigned)part,
                           (unsigned)status);
            return -1;
        }
        offset += part;
        out += part;
        n -= part;
    }
    return 0;
}

static NTSTATUS open_image(const char *path, HANDLE *handle, uint64_t *size) {
    /* Kernel paths follow xsdk/src/path.c's drive mapping, exactly as src/main.c does. */
    char resolved[XB_PATH_MAX + 40];
    const char *tail = path, *prefix = "";
    int len;
    char *p;
    IO_STATUS_BLOCK io;
    FILE_NETWORK_OPEN_INFORMATION info;
    NTSTATUS status;

    if (!strncasecmp(path, "usb0:/", 6)) {
        prefix = "\\Device\\Mass0\\";
        tail = path + 6;
    }
    if (!strncasecmp(path, "usb1:/", 6)) {
        prefix = "\\Device\\Mass1\\";
        tail = path + 6;
    }
    len = snprintf(resolved, sizeof(resolved), "%s%s", prefix, tail);
    if (len < 0 || (unsigned)len >= sizeof(resolved)) {
        return STATUS_INVALID_PARAMETER;
    }
    for (p = resolved; *p; p++) {
        if (*p == '/') {
            *p = '\\';
        }
    }
    {
        ANSI_STRING name = {(uint16_t)len, (uint16_t)(len + 1), resolved};
        OBJECT_ATTRIBUTES attrs = {resolved[0] == '\\' ? 0 : OB_DOS_DEVICES, &name, 0};

        status = NtCreateFile(handle, (uint32_t)GENERIC_READ | SYNCHRONIZE, &attrs, &io, NULL, FILE_ATTRIBUTE_NORMAL,
                              FILE_SHARE_READ, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    }
    if (!NT_SUCCESS(status)) {
        return status;
    }
    status = NtQueryInformationFile(*handle, &io, &info, sizeof(info), FILE_NETWORK_OPEN_INFORMATION_CLASS);
    if (!NT_SUCCESS(status)) {
        NtClose(*handle);
        *handle = 0;
        return status;
    }
    *size = info.end_of_file;
    return STATUS_SUCCESS;
}

int XbDisc_Mount(const char *path) {
    uint64_t size = 0;
    NTSTATUS status;
    int rc;

    XbDisc_Unmount();
    status = open_image(path, &sImage, &size);
    if (!NT_SUCCESS(status)) {
        xsdk_log_write("disc: cannot open %s: 0x%08X", path, (unsigned)status);
        return (int)status < 0 ? (int)status : -1;
    }
    rc = dr_mount(&sDisc, image_read, &sImage, size);
    if (rc != DR_OK) {
        xsdk_log_write("disc: %s is not a readable game image: %s", path, dr_error(rc));
        NtClose(sImage);
        sImage = 0;
        return rc;
    }
    sMounted = 1;
    xsdk_log_write("disc: mounted %s (%llu bytes)", path, (unsigned long long)size);
    return 0;
}

void XbDisc_Unmount(void) {
    ++sGeneration;
    if (sMounted) {
        NtClose(sImage);
    }
    sImage = 0;
    sMounted = 0;
    memset(&sDisc, 0, sizeof(sDisc));
}

const dr_disc *XbDisc_Get(void) { return sMounted ? &sDisc : NULL; }

int XbDisc_FindFile(const char *rel, dr_span *out) {
    return sMounted ? dr_game_file(&sDisc, rel, out) : DR_IO;
}

int XbDisc_ReadSpan(const dr_span *span, uint64_t off, void *buf, size_t n) {
    return sMounted ? dr_read(&sDisc, span, off, buf, n) : DR_IO;
}

/* A whole small file by xsdk path (the strip list, next to the program). Caller free()s. 0 = ok. */
int XbDisc_ReadHostFile(const char *path, uint8_t **buf, size_t *size) {
    HANDLE h = 0;
    uint64_t n = 0;
    NTSTATUS status = open_image(path, &h, &n);

    *buf = NULL;
    if (!NT_SUCCESS(status) || n > (64u << 20)) {
        if (NT_SUCCESS(status)) {
            NtClose(h);
        }
        return -1;
    }
    *buf = malloc((size_t)n + 1);
    if (*buf == NULL || image_read(&h, 0, *buf, (size_t)n) != 0) {
        free(*buf);
        *buf = NULL;
        NtClose(h);
        return -1;
    }
    NtClose(h);
    *size = (size_t)n;
    return 0;
}
