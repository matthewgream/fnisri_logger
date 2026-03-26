/*
 * fnirsi_logger.c — FNIRSI USB power-meter logger (FNB48, FNB48S, FNB58, C1)
 *
 * Compile:
 *   gcc -O2 -Wall -o fnirsi_logger fnirsi_logger.c -lusb-1.0 -lm
 *
 * Requires: libusb-1.0-0-dev
 *
 * Usage examples:
 *   ./fnirsi_logger --capture=10sps --display=5s --save --verbose
 *   ./fnirsi_logger --save=run1 --events=MARKER --exec idf.py monitor
 *   ./fnirsi_logger --save --events="MARKER=POWER" --exec esp32-boot --target mydevice
 *
 * CSV output is space-separated and compatible with the original Python
 * fnirsi_logger and its companion gnuplot script (plot.gnuplot).
 *
 * With --exec, the logger spawns a child process, tees its combined
 * stdout/stderr to a .log log file (if save enabled), greps for --events
 * markers, and displays them to stdout and (if save enabled) also records
 * them with system wall-clock timestamps in a .marks.csv file for gnuplot
 * overlay. When the child exits the logger stops.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <poll.h>
#include <libusb-1.0/libusb.h>

/* ═══════════════════════════════════════════════════════════════════════
 * 1. Types, constants, CRC
 * ═══════════════════════════════════════════════════════════════════════ */

#define VID_STM    0x0483
#define PID_FNB48  0x003A
#define PID_C1     0x003B
#define VID_FNIRSI 0x2E3C
#define PID_FNB58  0x5558
#define PID_FNB48S 0x0049

#define DEVICE_SPS 100.0
#define DEVICE_DT  (1.0 / DEVICE_SPS)

typedef struct {
    double voltage;
    double current;
    double dp;
    double dn;
    double temp_C;
} sample_t;

/* ── CRC-8 (poly=0x39 init=0x42, no reflection, no final xor) ──────── */

static uint8_t crc8_table[256];

static void crc8_init(void) {
    const uint8_t poly = 0x39;
    for (int i = 0; i < 256; i++) {
        uint8_t c = (uint8_t)i;
        for (int j = 0; j < 8; j++)
            c = (c & 0x80) ? (uint8_t)((c << 1) ^ poly) : (uint8_t)(c << 1);
        crc8_table[i] = c;
    }
}

static uint8_t crc8_calc(const uint8_t *data, size_t len) {
    uint8_t crc = 0x42;
    for (size_t i = 0; i < len; i++)
        crc = crc8_table[crc ^ data[i]];
    return crc;
}

/* ── Signal state ───────────────────────────────────────────────────── */

static volatile sig_atomic_t g_stop = 0;
static volatile pid_t g_child_pid = 0;

static void sighandler(int sig) {
    (void)sig;
    g_stop = 1;
    if (g_child_pid > 0)
        kill(g_child_pid, SIGTERM);
}

/* ═══════════════════════════════════════════════════════════════════════
 * 2. Time helpers
 * ═══════════════════════════════════════════════════════════════════════ */

static double now_epoch(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static double now_mono(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static const char *timestamp_human(char *buf, size_t bufsz) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    snprintf(buf, bufsz, "%04d%02d%02d%02d%02d%02d.%03ld", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000L);
    return buf;
}

static void make_timestamp_tag(char *buf, size_t bufsz) {
    const time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    snprintf(buf, bufsz, "%04d%02d%02d%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

/* ═══════════════════════════════════════════════════════════════════════
 * 3. Config — parse, validate, dump
 * ═══════════════════════════════════════════════════════════════════════ */

typedef struct {
    bool crc;
    bool verbose;
    double alpha;

    double capture_interval;
    double display_interval;
    bool native_capture; /* derived: capture at full device rate? */

    bool save;
    char save_prefix[256];

    const char *events_pattern;
    int exec_argc;
    char **exec_argv;
    bool plot;
    bool nostrip;
} config_t;

static double parse_capture(const char *s) {
    char *end = NULL;
    const double val = strtod(s, &end);
    if (val <= 0) {
        fprintf(stderr, "Invalid capture rate: %s\n", s);
        exit(1);
    }
    if (!end || *end == '\0' || strcasecmp(end, "sps") == 0)
        return 1.0 / val;
    if (strcasecmp(end, "spm") == 0)
        return 60.0 / val;
    fprintf(stderr, "Unknown capture unit '%s' (use sps or spm)\n", end);
    exit(1);
}

static double parse_display(const char *s) {
    char *end = NULL;
    const double val = strtod(s, &end);
    if (val <= 0) {
        fprintf(stderr, "Invalid display interval: %s\n", s);
        exit(1);
    }
    if (!end || *end == '\0' || strcasecmp(end, "s") == 0)
        return val;
    if (strcasecmp(end, "m") == 0)
        return val * 60.0;
    fprintf(stderr, "Unknown display unit '%s' (use s or m)\n", end);
    exit(1);
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [OPTIONS] [--exec COMMAND [ARGS...]]\n"
            "\n"
            "Options:\n"
            "  --crc                 Enable CRC-8 packet validation\n"
            "  --verbose             Print extra initialisation info\n"
            "  --alpha=F             Temperature EMA smoothing (0–1, default 0.9)\n"
            "  --capture=RATE        Capture rate (default: 100sps)\n"
            "                          e.g. 10sps  10  20spm\n"
            "  --display=INTERVAL    Screen display interval (default: 1s)\n"
            "                          e.g. 5s  30s  1m  5m\n"
            "  --save[=PREFIX]       Save to file (default prefix: powermon)\n"
            "  --events=PATTERN      Grep child output for PATTERN (e.g. MARKER=POWER)\n"
            "  --plot                Generate gnuplot PNG after capture completes\n"
            "  --nostrip             Keep ANSI escape codes in log/marks output\n"
            "  --exec CMD [ARGS..]   Spawn CMD after USB init; stops logger on exit\n"
            "  --help                Show this message\n"
            "\n"
            "The device samples at 100 sps internally.  --capture controls the\n"
            "rate at which averaged samples are recorded to file.  --display\n"
            "controls how often the latest sample is printed to the screen.\n"
            "\n"
            "With --save, the power readings are saved to a .csv file with the\n"
            "format '<prefix>-<YYYYMMDDHHMMSS>.power.csv', and if using --exec,\n"
            "the child's stdout/stderr is teed to a .log file with the format\n"
            "'<prefix>-<YYYYMMDDHHMMSS>.log', and lines matching --events are\n"
            "recorded with system timestamps in a .csv file, with the format\n"
            "'<prefix>-<YYYYMMDDHHMMSS>.marks.csv', for gnuplot overlay.\n"
            "\n"
            "--exec consumes all remaining arguments, so it must come last.\n",
            prog);
}

static int config_parse(config_t *cfg, int argc, char **argv) {
    *cfg = (config_t){
        .alpha = 0.9,
        .capture_interval = DEVICE_DT,
        .display_interval = 1.0,
        .save_prefix = "powermon",
    };

    strncpy(cfg->save_prefix, "powermon", sizeof(cfg->save_prefix));

    int getopt_argc = argc;
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--exec") == 0) {
            getopt_argc = i;
            if (i + 1 < argc) {
                cfg->exec_argv = &argv[i + 1];
                cfg->exec_argc = argc - (i + 1);
            } else {
                fprintf(stderr, "--exec requires a command\n");
                return -1;
            }
            break;
        }

    enum {
        OPT_CRC = 1000,
        OPT_VERBOSE,
        OPT_ALPHA,
        OPT_CAPTURE,
        OPT_DISPLAY,
        OPT_SAVE,
        OPT_EVENTS,
        OPT_PLOT,
        OPT_NOSTRIP,
        OPT_HELP,
    };

    static struct option long_opts[] = {
        { "crc", no_argument, NULL, OPT_CRC },
        { "verbose", no_argument, NULL, OPT_VERBOSE },
        { "alpha", required_argument, NULL, OPT_ALPHA },
        { "capture", required_argument, NULL, OPT_CAPTURE },
        { "display", required_argument, NULL, OPT_DISPLAY },
        { "save", optional_argument, NULL, OPT_SAVE },
        { "events", required_argument, NULL, OPT_EVENTS },
        { "plot", no_argument, NULL, OPT_PLOT },
        { "nostrip", no_argument, NULL, OPT_NOSTRIP },
        { "help", no_argument, NULL, OPT_HELP },
        { NULL, 0, NULL, 0 },
    };

    /* Temporarily null-terminate at --exec boundary for getopt */
    char *saved = argv[getopt_argc];
    argv[getopt_argc] = NULL;
    optind = 1; /* reset getopt */

    int opt;
    while ((opt = getopt_long(getopt_argc, argv, "", long_opts, NULL)) != -1) {
        switch (opt) {
        case OPT_CRC:
            cfg->crc = true;
            break;
        case OPT_VERBOSE:
            cfg->verbose = true;
            break;
        case OPT_ALPHA:
            cfg->alpha = atof(optarg);
            break;
        case OPT_CAPTURE:
            cfg->capture_interval = parse_capture(optarg);
            break;
        case OPT_DISPLAY:
            cfg->display_interval = parse_display(optarg);
            break;
        case OPT_SAVE:
            cfg->save = true;
            if (optarg)
                snprintf(cfg->save_prefix, sizeof(cfg->save_prefix), "%s", optarg);
            break;
        case OPT_EVENTS:
            cfg->events_pattern = optarg;
            break;
        case OPT_PLOT:
            cfg->plot = true;
            break;
        case OPT_NOSTRIP:
            cfg->nostrip = true;
            break;
        case OPT_HELP:
            usage(argv[0]);
            argv[getopt_argc] = saved;
            return 1;
        default:
            usage(argv[0]);
            argv[getopt_argc] = saved;
            return -1;
        }
    }
    argv[getopt_argc] = saved;

    cfg->native_capture = (cfg->capture_interval <= DEVICE_DT * 1.01);

    if (cfg->events_pattern && !cfg->exec_argv)
        fprintf(stderr, "Warning: --events has no effect without --exec\n");

    return 0;
}

static void config_dump(const config_t *cfg) {
    fprintf(stderr, "capture_interval = %.4f s  (%.2f sps)%s\n", cfg->capture_interval, 1.0 / cfg->capture_interval, cfg->native_capture ? "  [native — every sub-sample]" : "");
    fprintf(stderr, "display_interval = %.1f s\n", cfg->display_interval);
    fprintf(stderr, "alpha            = %.3f\n", cfg->alpha);
    fprintf(stderr, "CRC              = %s\n", cfg->crc ? "on" : "off");
    if (cfg->events_pattern)
        fprintf(stderr, "events_pattern   = \"%s\"\n", cfg->events_pattern);
    if (cfg->exec_argv) {
        fprintf(stderr, "exec             =");
        for (int i = 0; i < cfg->exec_argc; i++)
            fprintf(stderr, " %s", cfg->exec_argv[i]);
        fprintf(stderr, "\n");
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * 4. USB — open, read, write, decode, drain, close
 * ═══════════════════════════════════════════════════════════════════════ */

typedef struct {
    libusb_device_handle *handle;
    bool is_fnb58_or_fnb48s;
    int iface_num;
    uint8_t ep_in;
    uint8_t ep_out;
    double refresh_interval; /* how often to send continue cmd */
} usbdev_t;

static const struct {
    uint16_t vid;
    uint16_t pid;
    bool is58;
    const char *name;
} devtable[] = {
    { VID_STM, PID_FNB48, false, "FNB48" },
    { VID_STM, PID_C1, false, "C1" },
    { VID_FNIRSI, PID_FNB58, true, "FNB58" },
    { VID_FNIRSI, PID_FNB48S, true, "FNB48S" },
};

static bool usb_find_device(usbdev_t *ud, bool verbose) {
    for (size_t i = 0; i < sizeof(devtable) / sizeof(devtable[0]); i++) {
        libusb_device_handle *h = libusb_open_device_with_vid_pid(NULL, devtable[i].vid, devtable[i].pid);
        if (h) {
            ud->handle = h;
            ud->is_fnb58_or_fnb48s = devtable[i].is58;
            ud->refresh_interval = devtable[i].is58 ? 1.0 : 0.003;
            if (verbose)
                fprintf(stderr, "Found %s device (VID=%04x PID=%04x)\n", devtable[i].name, devtable[i].vid, devtable[i].pid);
            return true;
        }
    }
    return false;
}

static bool usb_find_hid_endpoints(usbdev_t *ud, bool verbose) {
    libusb_device *dev = libusb_get_device(ud->handle);
    struct libusb_config_descriptor *cfg = NULL;
    if (libusb_get_active_config_descriptor(dev, &cfg) != 0)
        if (libusb_get_config_descriptor(dev, 0, &cfg) != 0)
            return false;
    for (int i = 0; i < cfg->bNumInterfaces; i++) {
        const struct libusb_interface *iface = &cfg->interface[i];
        for (int a = 0; a < iface->num_altsetting; a++) {
            const struct libusb_interface_descriptor *alt = &iface->altsetting[a];
            if (alt->bInterfaceClass == 3 /* HID */) {
                ud->iface_num = alt->bInterfaceNumber;
                for (int e = 0; e < alt->bNumEndpoints; e++) {
                    const struct libusb_endpoint_descriptor *ep = &alt->endpoint[e];
                    if ((ep->bEndpointAddress & 0x80) == LIBUSB_ENDPOINT_IN)
                        ud->ep_in = ep->bEndpointAddress;
                    else
                        ud->ep_out = ep->bEndpointAddress;
                }
                if (verbose)
                    fprintf(stderr, "HID iface=%d  ep_in=0x%02x  ep_out=0x%02x\n", ud->iface_num, ud->ep_in, ud->ep_out);
                libusb_free_config_descriptor(cfg);
                return (ud->ep_in && ud->ep_out);
            }
        }
    }
    libusb_free_config_descriptor(cfg);
    return false;
}

static void usb_detach_kernel(usbdev_t *ud) {
    if (libusb_kernel_driver_active(ud->handle, ud->iface_num) == 1)
        if (libusb_detach_kernel_driver(ud->handle, ud->iface_num) != 0)
            fprintf(stderr, "Warning: could not detach kernel driver on iface %d\n", ud->iface_num);
}

static bool usb_write(usbdev_t *ud, const uint8_t *buf, int len) {
    int transferred = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
    const int r = libusb_interrupt_transfer(ud->handle, ud->ep_out, (uint8_t *)buf, len, &transferred, 1000);
#pragma GCC diagnostic pop
    return r == 0 && transferred == len;
}

static int usb_read(usbdev_t *ud, uint8_t *buf, int len, unsigned timeout_ms) {
    int transferred = 0;
    const int r = libusb_interrupt_transfer(ud->handle, ud->ep_in, buf, len, &transferred, timeout_ms);
    if (r == LIBUSB_ERROR_TIMEOUT)
        return 0;
    if (r != 0)
        return -1;
    return transferred;
}

static void usb_send_cmd(usbdev_t *ud, uint8_t cmd, uint8_t crc_byte) {
    uint8_t buf[64] = { 0 };
    buf[0] = 0xaa;
    buf[1] = cmd;
    buf[63] = crc_byte;
    usb_write(ud, buf, 64);
}

static bool usb_open(usbdev_t *ud, bool verbose) {
    memset(ud, 0, sizeof(*ud));
    if (libusb_init(NULL) != 0) {
        fprintf(stderr, "libusb_init failed\n");
        return false;
    }
    if (!usb_find_device(ud, verbose)) {
        fprintf(stderr, "No FNIRSI device found\n");
        libusb_exit(NULL);
        return false;
    }
    if (!usb_find_hid_endpoints(ud, verbose)) {
        fprintf(stderr, "Could not find HID endpoints\n");
        libusb_close(ud->handle);
        libusb_exit(NULL);
        return false;
    }
    usb_detach_kernel(ud);
    if (libusb_claim_interface(ud->handle, ud->iface_num) != 0) {
        fprintf(stderr, "Cannot claim interface %d\n", ud->iface_num);
        libusb_close(ud->handle);
        libusb_exit(NULL);
        return false;
    }
    usb_send_cmd(ud, 0x81, 0x8e);
    usb_send_cmd(ud, 0x82, 0x96);
    if (ud->is_fnb58_or_fnb48s)
        usb_send_cmd(ud, 0x82, 0x96);
    else
        usb_send_cmd(ud, 0x83, 0x9e);
    usleep(100000); /* 100 ms settle */
    return true;
}

static void usb_send_continue(usbdev_t *ud) {
    usb_send_cmd(ud, 0x83, 0x9e);
}

static void usb_drain(usbdev_t *ud, bool verbose) {
    fprintf(stderr, "\nDraining USB buffer …\n");
    uint8_t buf[64];
    for (int i = 0; i < 50; i++) {
        const int r = usb_read(ud, buf, 64, 200);
        if (r <= 0)
            break;
        if (verbose)
            fprintf(stderr, "Drained %d bytes\n", r);
    }
}

static void usb_close(usbdev_t *ud) {
    libusb_release_interface(ud->handle, ud->iface_num);
    libusb_close(ud->handle);
    libusb_exit(NULL);
}

static int usb_decode_packet(const uint8_t *data, int len, bool do_crc, sample_t out[4]) {
    if (len < 64 || data[1] != 0x04)
        return 0;
    if (do_crc) {
        const uint8_t actual = data[63], expected = crc8_calc(data + 1, 62);
        if (actual != expected) {
            fprintf(stderr, "CRC mismatch: expected %02x got %02x — skipping\n", expected, actual);
            return 0;
        }
    }
    for (int i = 0; i < 4; i++) {
        const int off = 2 + 15 * i;
        const uint32_t v_raw = (uint32_t)data[off + 0] | (uint32_t)data[off + 1] << 8 | (uint32_t)data[off + 2] << 16 | (uint32_t)data[off + 3] << 24;
        const uint32_t i_raw = (uint32_t)data[off + 4] | (uint32_t)data[off + 5] << 8 | (uint32_t)data[off + 6] << 16 | (uint32_t)data[off + 7] << 24;
        const uint16_t dp_raw = (uint16_t)data[off + 8] | (uint16_t)data[off + 9] << 8;
        const uint16_t dn_raw = (uint16_t)data[off + 10] | (uint16_t)data[off + 11] << 8;
        const uint16_t t_raw = (uint16_t)data[off + 13] | (uint16_t)data[off + 14] << 8;
        out[i].voltage = v_raw / 100000.0;
        out[i].current = i_raw / 100000.0;
        out[i].dp = dp_raw / 1000.0;
        out[i].dn = dn_raw / 1000.0;
        out[i].temp_C = t_raw / 10.0;
    }
    return 4;
}

/* ═══════════════════════════════════════════════════════════════════════
 * 5. Output — save file + screen display
 * ═══════════════════════════════════════════════════════════════════════ */

static FILE *output_open_save(const char *prefix, const char *tstag) {
    char path[512];
    snprintf(path, sizeof(path), "%s-%s.power.csv", prefix, tstag);
    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "Cannot open save file %s: %s\n", path, strerror(errno));
        return NULL;
    }
    fprintf(stderr, "Saving power data to %s\n", path);
    fprintf(fp, "\n");
    fprintf(fp, "timestamp sample_in_packet voltage_V current_A dp_V dn_V temp_C_ema energy_Ws capacity_As\n");
    return fp;
}

static void output_emit_save(FILE *fp, double epoch_ts, int sample_idx, double voltage, double current, double dp, double dn, double temp_ema, double energy, double capacity) {
    fprintf(fp, "%.3f %d %7.5f %7.5f %5.3f %5.3f %6.3f %f %f\n", epoch_ts, sample_idx, voltage, current, dp, dn, temp_ema, energy, capacity);
    fflush(fp);
}

static void output_emit_display(double epoch_ts, int sample_idx, double voltage, double current, double dp, double dn, double temp_ema, double energy, double capacity) {
    char ts[64];
    printf("%s: timestamp=%.3f sample_in_packet=%d voltage_V=%7.5f current_A=%7.5f dp_V=%5.3f dn_V=%5.3f temp_C_ema=%6.3f energy_Ws=%f capacity_As=%f\n", timestamp_human(ts, sizeof(ts)), epoch_ts, sample_idx, voltage, current, dp, dn,
           temp_ema, energy, capacity);
    fflush(stdout);
}

/* ═══════════════════════════════════════════════════════════════════════
 * 6. Child — fork/exec, pipe poll, marker extraction, drain, close
 * ═══════════════════════════════════════════════════════════════════════ */

typedef struct {
    int pipe_fd;         /* read end of child's stdout/stderr pipe, or -1 */
    bool alive;          /* child still running? */
    FILE *log_fp;        /* tee of all child output */
    FILE *evt_fp;        /* extracted marker lines */
    const char *pattern; /* event grep pattern, or NULL */
    char linebuf[8192];  /* line accumulation buffer */
    size_t linelen;
    bool strip;
} child_ctx_t;

/* Strip ANSI escape sequences (e.g. \033[0m) from a string in-place. */
static void strip_ansi(char *s) {
    char *rd = s, *wr = s;
    while (*rd) {
        if (*rd == '\033' && *(rd + 1) == '[') {
            rd += 2;
            while (*rd && !((*rd >= 'A' && *rd <= 'Z') || (*rd >= 'a' && *rd <= 'z')))
                rd++;
            if (*rd)
                rd++; /* skip the final letter */
        } else {
            *wr++ = *rd++;
        }
    }
    *wr = '\0';
}

static bool child_process_marker(const char *line, const char *pattern, FILE *fp) {
    const char *hit = strstr(line, pattern);
    if (!hit)
        return false;
    const double epoch = now_epoch();
    const char *p = hit + strlen(pattern);
    while (*p == ',' || *p == '=')
        p++;
    char payload[512];
    size_t len = 0;
    while (*p && *p != '\n' && *p != '\r' && len < sizeof(payload) - 1)
        payload[len++] = *p++;
    while (len > 0 && payload[len - 1] == ' ')
        len--;
    payload[len] = '\0';
    strip_ansi(payload);
    len = strlen(payload);
    for (size_t i = 0; i < len; i++)
        if (payload[i] == ',')
            payload[i] = ' ';
    if (fp) {
        fprintf(fp, "%.3f %s\n", epoch, payload);
        fflush(fp);
    }
    char ts[64];
    fprintf(stderr, "\033[1;33m%s EVENT: %s\033[0m\n", timestamp_human(ts, sizeof(ts)), payload);
    return true;
}

static void child_handle_line(child_ctx_t *ctx, const char *line) {
    fprintf(stderr, "%s\n", line);
    if (ctx->strip) {
        char clean[8192];
        snprintf(clean, sizeof(clean), "%s", line);
        strip_ansi(clean);
        if (ctx->log_fp) {
            fprintf(ctx->log_fp, "%s\n", clean);
            fflush(ctx->log_fp);
        }
        if (ctx->pattern)
            child_process_marker(clean, ctx->pattern, ctx->evt_fp);
    } else {
        if (ctx->log_fp) {
            fprintf(ctx->log_fp, "%s\n", line);
            fflush(ctx->log_fp);
        }
        if (ctx->pattern)
            child_process_marker(line, ctx->pattern, ctx->evt_fp);
    }
}

static void child_feed(child_ctx_t *ctx, const char *data, ssize_t len) {
    for (ssize_t i = 0; i < len; i++)
        if (data[i] == '\n' || ctx->linelen >= sizeof(ctx->linebuf) - 1) {
            ctx->linebuf[ctx->linelen] = '\0';
            child_handle_line(ctx, ctx->linebuf);
            ctx->linelen = 0;
        } else
            ctx->linebuf[ctx->linelen++] = data[i];
}

static bool child_open(child_ctx_t *ctx, const config_t *cfg, const char *tstag) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->pipe_fd = -1;
    ctx->pattern = cfg->events_pattern;
    ctx->strip = !cfg->nostrip;
    if (!cfg->exec_argv)
        return true; /* nothing to do */
    char logpath[512];
    snprintf(logpath, sizeof(logpath), "%s-%s.log", cfg->save_prefix, tstag);
    if (cfg->save) {
        ctx->log_fp = fopen(logpath, "w");
        if (ctx->log_fp)
            fprintf(stderr, "Child log → %s\n", logpath);
        else
            fprintf(stderr, "Warning: cannot open log file %s: %s\n", logpath, strerror(errno));
    }
    if (cfg->save && cfg->events_pattern) {
        char mpath[512];
        snprintf(mpath, sizeof(mpath), "%s-%s.marks.csv", cfg->save_prefix, tstag);
        ctx->evt_fp = fopen(mpath, "w");
        if (ctx->evt_fp) {
            fprintf(ctx->evt_fp, "# timestamp event_fields...\n");
            fprintf(stderr, "Markers → %s\n", mpath);
        } else
            fprintf(stderr, "Warning: cannot open markers file %s: %s\n", mpath, strerror(errno));
    }
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        perror("pipe");
        return false;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return false;
    }
    if (pid == 0) {
        /* Child */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execvp(cfg->exec_argv[0], cfg->exec_argv);
        perror("execvp");
        _exit(127);
    } else {
        /* Parent */
        close(pipefd[1]);
        ctx->pipe_fd = pipefd[0];
        ctx->alive = true;
        g_child_pid = pid;
        fprintf(stderr, "Spawned child PID %d: %s\n", pid, cfg->exec_argv[0]);
        return true;
    }
}

static bool child_poll(child_ctx_t *ctx) {
    if (ctx->pipe_fd < 0)
        return ctx->alive;
    struct pollfd pfd = { .fd = ctx->pipe_fd, .events = POLLIN };
    while (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        char tmp[1024];
        const ssize_t nr = read(ctx->pipe_fd, tmp, sizeof(tmp));
        if (nr <= 0) {
            close(ctx->pipe_fd);
            ctx->pipe_fd = -1;
            break;
        }
        child_feed(ctx, tmp, nr);
    }
    if (ctx->alive && g_child_pid > 0) {
        int status;
        if (waitpid(g_child_pid, &status, WNOHANG) > 0) {
            ctx->alive = false;
            if (WIFEXITED(status))
                fprintf(stderr, "\nChild exited with status %d\n", WEXITSTATUS(status));
            else if (WIFSIGNALED(status))
                fprintf(stderr, "\nChild killed by signal %d\n", WTERMSIG(status));
            g_child_pid = 0;
        }
    }
    return ctx->alive;
}

static void child_drain(child_ctx_t *ctx) {
    if (ctx->pipe_fd < 0)
        return;
    char tmp[1024];
    ssize_t nr;
    while ((nr = read(ctx->pipe_fd, tmp, sizeof(tmp))) > 0)
        child_feed(ctx, tmp, nr);
    close(ctx->pipe_fd);
    ctx->pipe_fd = -1;
}

static void child_close(child_ctx_t *ctx) {
    child_drain(ctx);
    if (g_child_pid > 0) {
        fprintf(stderr, "Waiting for child to exit …\n");
        kill(g_child_pid, SIGTERM);
        int status;
        waitpid(g_child_pid, &status, 0);
        g_child_pid = 0;
    }
    if (ctx->log_fp) {
        fclose(ctx->log_fp);
        ctx->log_fp = NULL;
    }
    if (ctx->evt_fp) {
        fclose(ctx->evt_fp);
        ctx->evt_fp = NULL;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * 7. Exec — main sampling loop
 * ═══════════════════════════════════════════════════════════════════════ */

typedef struct {
    double voltage, current, dp, dn, temp;
    int count;
} log_accum_t;

typedef struct {
    /* Snapshot of last captured sample for display */
    double epoch, voltage, current, dp, dn, temp;
    int idx;
    bool valid;
} display_snap_t;

static void exec(const config_t *cfg, usbdev_t *ud, child_ctx_t *child, const char *tstag) {
    FILE *savefp = NULL;
    double energy_Ws = 0.0, capacity_As = 0.0;

    const double t0 = now_mono();
    double next_capture = t0 + cfg->capture_interval;
    double next_display = t0 + cfg->display_interval;
    double next_refresh = t0 + ud->refresh_interval;

    log_accum_t acc = { 0 };
    double temp_ema = -1.0;
    int capture_count = 0;
    display_snap_t snap = { 0 };

    uint8_t buf[64];
    const bool has_child = (cfg->exec_argv != NULL);

    while (!g_stop) {

        if (has_child && !child_poll(child))
            g_stop = 1; /* child exited → stop */

        const int n = usb_read(ud, buf, 64, has_child ? 50 : 200); // short timeout to stay responsive to child/signals
        if (n < 0) {
            if (!g_stop)
                fprintf(stderr, "USB read error\n");
            break;
        }

        const double t_now = now_mono();
        if (t_now >= next_refresh) {
            usb_send_continue(ud);
            next_refresh = t_now + ud->refresh_interval;
        }

        sample_t samples[4];
        const int nsamples = (n > 0) ? usb_decode_packet(buf, n, cfg->crc, samples) : 0;
        if (nsamples > 0) {
            const double epoch_base = now_epoch() - 4 * DEVICE_DT;

            for (int i = 0; i < nsamples; i++) {
                const double epoch_ts = epoch_base + i * DEVICE_DT;

                /* Temperature EMA */
                temp_ema = (temp_ema < 0) ? samples[i].temp_C : samples[i].temp_C * (1.0 - cfg->alpha) + temp_ema * cfg->alpha;
                /* Running totals (always at native rate) */
                const double power = samples[i].voltage * samples[i].current;
                energy_Ws += power * DEVICE_DT;
                capacity_As += samples[i].current * DEVICE_DT;

                if (cfg->native_capture) {
                    /* Emit every sub-sample */
                    capture_count++;
                    if (cfg->save && !savefp)
                        savefp = output_open_save(cfg->save_prefix, tstag);
                    if (savefp)
                        output_emit_save(savefp, epoch_ts, i, samples[i].voltage, samples[i].current, samples[i].dp, samples[i].dn, temp_ema, energy_Ws, capacity_As);

                    snap = (display_snap_t){ epoch_ts, samples[i].voltage, samples[i].current, samples[i].dp, samples[i].dn, temp_ema, i, true };

                } else {
                    /* Accumulate for averaged capture */
                    acc.voltage += samples[i].voltage;
                    acc.current += samples[i].current;
                    acc.dp += samples[i].dp;
                    acc.dn += samples[i].dn;
                    acc.temp += temp_ema;
                    acc.count++;

                    if (t_now >= next_capture && acc.count > 0) {
                        const double inv = 1.0 / acc.count;
                        capture_count++;

                        if (cfg->save && !savefp)
                            savefp = output_open_save(cfg->save_prefix, tstag);
                        if (savefp)
                            output_emit_save(savefp, epoch_ts, capture_count, acc.voltage * inv, acc.current * inv, acc.dp * inv, acc.dn * inv, acc.temp * inv, energy_Ws, capacity_As);

                        snap = (display_snap_t){ epoch_ts, acc.voltage * inv, acc.current * inv, acc.dp * inv, acc.dn * inv, acc.temp * inv, capture_count, true };

                        memset(&acc, 0, sizeof(acc));
                        next_capture = t_now + cfg->capture_interval;
                    }
                }
            }
        }

        if (t_now >= next_display && snap.valid) {
            output_emit_display(snap.epoch, snap.idx, snap.voltage, snap.current, snap.dp, snap.dn, snap.temp, energy_Ws, capacity_As);
            next_display = t_now + cfg->display_interval;
        }

        if (access("fnirsi_stop", F_OK) == 0)
            g_stop = 1;
    }

    if (savefp)
        fclose(savefp);

    fprintf(stderr, "Total energy:   %.6f Ws\n", energy_Ws);
    fprintf(stderr, "Total capacity: %.6f As\n", capacity_As);
}

/* ═══════════════════════════════════════════════════════════════════════
 * 8. Plot — find and invoke gnuplot script
 * ═══════════════════════════════════════════════════════════════════════ */

static void run_plot(const char *save_prefix, const char *tstag) {
    /* Locate plot.gnuplot next to this executable via /proc/self/exe */
    char exe[512], script[512];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) {
        fprintf(stderr, "Warning: cannot resolve executable path, skipping plot\n");
        return;
    }
    exe[n] = '\0';

    /* dirname: find last '/' */
    char *slash = strrchr(exe, '/');
    if (slash)
        *(slash + 1) = '\0';
    else
        strcpy(exe, "./");

    snprintf(script, sizeof(script), "%splot.gnuplot", exe);

    if (access(script, X_OK) != 0) {
        fprintf(stderr, "Warning: plot script not found at %s, skipping plot\n", script);
        return;
    }

    /* Build stem: <save_prefix>-<tstag> */
    char stem[512];
    snprintf(stem, sizeof(stem), "%s-%s", save_prefix, tstag);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "%s %s", script, stem);
    fprintf(stderr, "Running: %s\n", cmd);
    int rc = system(cmd);
    if (rc != 0)
        fprintf(stderr, "Warning: plot command exited with status %d\n", rc);
}

/* ═══════════════════════════════════════════════════════════════════════
 * 9. main — lifecycle orchestration
 * ═══════════════════════════════════════════════════════════════════════ */

int main(int argc, char **argv) {

    config_t cfg;
    const int cr = config_parse(&cfg, argc, argv);
    if (cr > 0)
        return 0; /* --help */
    else if (cr < 0)
        return 1; /* error  */

    if (cfg.verbose)
        config_dump(&cfg);
    if (cfg.crc)
        crc8_init();

    usbdev_t ud;
    if (!usb_open(&ud, cfg.verbose))
        return 1;

    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);

    char tstag[32];
    make_timestamp_tag(tstag, sizeof(tstag));

    child_ctx_t child;
    if (!child_open(&child, &cfg, tstag)) {
        usb_drain(&ud, cfg.verbose);
        usb_close(&ud);
        return 1;
    }

    exec(&cfg, &ud, &child, tstag);

    child_close(&child);
    usb_drain(&ud, cfg.verbose);
    usb_close(&ud);

    if (cfg.plot && cfg.save)
        run_plot(cfg.save_prefix, tstag);

    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * EOF
 * ═══════════════════════════════════════════════════════════════════════ */
