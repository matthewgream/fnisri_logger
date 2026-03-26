CC = gcc
CFLAGS = -O3 \
	-Wfloat-conversion -Werror=float-conversion \
	-Wall -Wextra -Werror -Wpedantic \
	-Wstrict-prototypes -Wold-style-definition \
	-Wcast-align -Wcast-qual -Wconversion \
	-Wfloat-equal -Wformat=2 -Wformat-security \
	-Winit-self -Wjump-misses-init \
	-Wlogical-op -Wmissing-include-dirs \
	-Wnested-externs -Wpointer-arith \
	-Wredundant-decls -Wshadow \
	-Wstrict-overflow=2 -Wswitch-default \
	-Wunreachable-code -Wunused \
	-Wwrite-strings \
	-Wdouble-promotion \
	-Wnull-dereference \
	-Wduplicated-cond \
	-Wduplicated-branches \
	-Wrestrict \
	-Wstringop-overflow \
	-Wundef \
	-Wvla \
	-Wno-duplicated-branches
LDLIBS = -lusb-1.0 -lm

TARGET = fnirsi_logger
TARGET_PLOT = plot.gnuplot

.PHONY: all clean install

all: $(TARGET)

$(TARGET): fnirsi_logger.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/
	install -m 755 $(TARGET_PLOT) /usr/local/bin/
