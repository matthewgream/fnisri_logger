#!/usr/bin/env -S gnuplot -c

# ── Resolve input files ───────────────────────────────────────────────
# Usage:
#   plot.gnuplot <stem>              → loads <stem>.power.csv + <stem>.marks.csv
#   plot.gnuplot <stem>.power.csv    → strips suffix, same as above
#   plot.gnuplot <file> [--live]     → legacy: use <file> directly
#
# Pass --live as any argument for interactive/refresh mode.

LIVE = 0
stem = ""

# Scan all args
do for [i=1:ARGC] {
  arg = value("ARG" . sprintf("%d", i))
  if (arg eq "--live") {
    LIVE = 1
  } else {
    if (stem eq "") { stem = arg }
  }
}

if (stem eq "") {
  stem = "live"
}

# Derive power data file and markers file from stem.
# If stem ends with .power.csv, strip it to get the bare stem.
# If stem ends with .csv, strip it.  Otherwise use as-is.
f = ""
markers_file = ""

if (strlen(stem) > 10 && stem[strlen(stem)-9:] eq ".power.csv") {
  base = stem[:strlen(stem)-10]
  f = stem
  markers_file = base . ".marks.csv"
} else if (strlen(stem) > 4 && stem[strlen(stem)-3:] eq ".csv") {
  # Legacy: plain .csv file — try old .markers.csv convention too
  f = stem
  base = stem[:strlen(stem)-4]
  markers_file = base . ".marks.csv"
  if (system("test -f '" . markers_file . "' && echo 1 || echo 0") + 0 == 0) {
    markers_file = base . ".markers.csv"
  }
} else {
  # Bare stem: append standard suffixes
  f = stem . ".power.csv"
  markers_file = stem . ".marks.csv"
}

# Verify power data file exists; fall back to stem as literal filename
if (system("test -f '" . f . "' && echo 1 || echo 0") + 0 == 0) {
  if (system("test -f '" . stem . "' && echo 1 || echo 0") + 0 == 1) {
    f = stem
    print "Using data file: " . f
  } else {
    print "Error: cannot find data file: " . f
    exit
  }
} else {
  print "Using data file: " . f
}

# Derive output PNG name from stem/base
if (exists("base")) {
  out_png = base . ".png"
} else {
  out_png = stem . ".png"
}

# Check markers file
HAS_MARKERS = 0
if (system("test -f '" . markers_file . "' && echo 1 || echo 0") + 0 == 1) {
  HAS_MARKERS = 1
  print "Using markers:   " . markers_file
}

# ── Compute data duration → dynamic image width ─────────────────────
# Extract first and last epoch timestamps from the power CSV.
# Scale width proportionally: PIX_PER_SEC pixels per second of data,
# with a minimum of 1600px.

PIX_PER_SEC = 20
MIN_W = 1600

t_first = system("awk 'NF && !/^timestamp/{print $1; exit}' '" . f . "'") + 0.0
t_last  = system("awk 'NF && !/^timestamp/{v=$1} END{print v}' '" . f . "'") + 0.0
data_dur = t_last - t_first

dyn_w = (data_dur > 0) ? int(data_dur * PIX_PER_SEC) : MIN_W
if (dyn_w < MIN_W) { dyn_w = MIN_W }

print sprintf("Data duration:   %.1f s  →  image width: %d px", data_dur, dyn_w)

# Interactive
if (LIVE != 0) {
  set terminal wxt size 1900,1200 enhanced
  e="every 10"
  TERM_W = 1900.0
  TERM_H = 1200.0
} else {
  e=""
  TERM_W = dyn_w + 0.0
  TERM_H = 1400.0
}

f = "<grep -v ^timestamp " . f


# ── Load markers into arrays for drawing ──────────────────────────────
# Format: <epoch_ts> <label fields...>

n_markers = 0
if (HAS_MARKERS) {
  n_markers = system("grep -cv '^#' '" . markers_file . "' 2>/dev/null || echo 0") + 0
}

if (n_markers > 0) {
  array marker_ts[n_markers]
  array marker_label[n_markers]

  do for [i=1:n_markers] {
    line = system("grep -v '^#' '" . markers_file . "' | sed -n '" . sprintf("%d", i) . "p'")
    marker_ts[i] = word(line, 1) + 0.0
    marker_label[i] = ""
    do for [w=2:words(line)] {
      if (w > 2) { marker_label[i] = marker_label[i] . " " }
      marker_label[i] = marker_label[i] . word(line, w)
    }
  }
}


# ── Layout constants ─────────────────────────────────────────────────
# 6 equal vertical slots: 5 data panels + 1 label gap (between rows 1 & 2).
# Manual positioning with set origin / set size — no layout command.

ph = 0.145           # panel height (screen fraction)
slot_top = 0.98      # top of slot 1

# Slot bottom-y positions (origin y for set origin):
s1_y = slot_top - ph          # 0.835  — Voltage + Current
s2_y = s1_y - ph              # 0.690  — Label gap (no subplot)
s3_y = s2_y - ph              # 0.545  — Energy
s4_y = s3_y - ph              # 0.400  — Power + ESR
s5_y = s4_y - ph              # 0.255  — D+ / D-
s6_y = s5_y - ph              # 0.110  — Temperature

# Vertical span for marker lines
markers_top = slot_top
markers_bot = s6_y


while (1) {

if (LIVE == 0) {
  set terminal pngcairo size dyn_w,1400 noenhanced
  set output out_png
  print "Output:          " . out_png
}

set multiplot

# ── Common settings for all data panels ──────────────────────────────
set lmargin 15
set rmargin 15
set timefmt "%s"
set xdata time
set grid
set format x ""      # suppress x-axis ticks on all but bottom panel
unset xlabel


# ══════════════════════════════════════════════════════════════════════
# Panel 1: Voltage + Current
# ══════════════════════════════════════════════════════════════════════
set origin 0, s1_y
set size 1, ph
set tmargin 1
set bmargin 1

set ylabel "Voltage [V]"
set y2tics
set y2label "Current [A]"
set yrange [*<2.0:5.2<*]
set format y2 "%.3s %cA"

plot f u 1:3 @e w steps lw 2 title "Voltage", \
    "" u 1:4 @e w steps lw 2 axis x1y2 title "Current"

# ── Capture x-axis mapping from first panel for marker positioning ───
data_x_min = GPVAL_X_MIN
data_x_max = GPVAL_X_MAX
scr_plot_left  = GPVAL_TERM_XMIN / TERM_W
scr_plot_right = GPVAL_TERM_XMAX / TERM_W

unset y2label
unset y2tics
unset format y2


# ══════════════════════════════════════════════════════════════════════
# (Slot 2 is the label gap — no subplot plotted here)
# ══════════════════════════════════════════════════════════════════════


# ══════════════════════════════════════════════════════════════════════
# Panel 2: Energy
# ══════════════════════════════════════════════════════════════════════
set origin 0, s3_y
set size 1, ph

set ylabel "Energy [Wh]"
set yrange [0:*]
set format y "%.3s %cWh"

plot f u 1:($8/3600) @e w steps lw 2 title "Energy"


# ══════════════════════════════════════════════════════════════════════
# Panel 3: Power + ESR
# ══════════════════════════════════════════════════════════════════════
set origin 0, s4_y
set size 1, ph

set ylabel "Power [W]"
set y2tics
set y2label "ESR [Ohm]"
set yrange [0.0:]
set y2range [0.0:*<100]
set format y "%.3s %cW"

plot f u 1:($3*$4) @e w steps lw 2 title "Power", \
     f u 1:($4 > 0.01 ? $3/$4 : 10000) @e w steps lw 2 axis x1y2 title "ESR"

unset format y
unset y2label
unset y2tics


# ══════════════════════════════════════════════════════════════════════
# Panel 4: D+ / D-
# ══════════════════════════════════════════════════════════════════════
set origin 0, s5_y
set size 1, ph

set ylabel "Voltage [V]"
set yrange [0.0:0.2<*]

plot f u 1:5 @e w steps lw 2 title "D+", "" u 1:6 w steps lw 3 title "D-"


# ══════════════════════════════════════════════════════════════════════
# Panel 5: Temperature  (bottom — show x-axis)
# ══════════════════════════════════════════════════════════════════════
set origin 0, s6_y
set size 1, ph
set bmargin 4

set format x "%H:%M:%S"
set xlabel "Time"
set ylabel "Temperature [degC]"
set yrange [*:*]

plot f u 1:7 every 10 w steps lw 2 title "Temperature"


# ══════════════════════════════════════════════════════════════════════
# Marker overlay — screen-coordinate arrows & labels drawn on top
# ══════════════════════════════════════════════════════════════════════
# Full-canvas overlay for marker lines and labels.
# Lines: screen-coordinate arrows spanning all panels (continuous).
# Labels: vertical text in the gap between panels 1 and 2.

if (n_markers > 0) {
  unset border
  unset grid
  unset tics
  unset xlabel
  unset ylabel
  unset y2label
  unset y2tics
  set origin 0, 0
  set size 1, 1
  set tmargin 0
  set bmargin 0
  set lmargin 0
  set rmargin 0

  unset arrow
  unset label

  do for [i=1:n_markers] {
    ts = marker_ts[i]
    lbl = marker_label[i]
    is_on = (strstrt(lbl, "ON") > 0) ? 1 : 0
    clr = is_on ? "0x228B22" : "0xCC3333"

    # Convert timestamp → screen x
    frac = (ts - data_x_min) / (data_x_max - data_x_min)
    sx = scr_plot_left + frac * (scr_plot_right - scr_plot_left)

    # Full-height vertical dashed line (continuous across all panels)
    set arrow i from screen sx, markers_top to screen sx, markers_bot \
        nohead lc rgb clr lw 1 dt 2 front

    # Vertical label in the gap — anchored at top, text grows downward
    label_y = s1_y - 0.005
    set label i lbl at screen sx, label_y rotate by 90 \
        font ",8" tc rgb clr right noenhanced
  }

  # Invisible plot to trigger rendering of the arrows and labels
  unset xdata
  set xrange [0:1]
  set yrange [0:1]
  plot 1/0 notitle
}

unset multiplot
unset output

if (LIVE == 0) {
  break
} else {
  pause 10  # for interactive redraw
}

}
