
For the fnisri FNB-58

* reads and stores power data from connected USB device
* executes child process and captures log out from child
* can filter and select event based data from child log output and place on rendered plots
* can automatically run gnuplot at end of cycle to generate rendered plots

For example, with lightweight instrumentation:

```
#define DIAGNOSTICS_POWER_EVENT(name)   ESP_LOGI(__func__, "POWER=%s", name)

...

    DIAGNOSTICS_POWER_EVENT("EPD1680-UPDATE-BEGIN");
    ESP_RETURN_ON_ERROR(epd1680_trigger_update(mode), __tag_device_epd1680, "write: update");
    DIAGNOSTICS_POWER_EVENT("EPD1680-UPDATE-COMPLETE");

```
And the following output from the below execution (esp32-boot just restarts the device via USB and executes idf monitor):


```
root@workshop:/opt/sensor-depth-snow/embedded# fnirsi_logger --verbose --save=powermon/powermon --events=POWER --plot --exec tools/esp32-boot
capture_interval = 0.0100 s  (100.00 sps)  [native — every sub-sample]
display_interval = 1.0 s
alpha            = 0.900
CRC              = off
events_pattern   = "POWER"
exec             = tools/esp32-boot
Found FNB58 device (VID=2e3c PID=5558)
HID iface=3  ep_in=0x83  ep_out=0x03
Child log → powermon/powermon-20260326132624.log
Markers → powermon/powermon-20260326132624.marks.csv
Spawned child PID 3543414: tools/esp32-boot
Saving power data to powermon/powermon-20260326132624.power.csv
20260326132625.077: timestamp=1774531585.067 sample_in_packet=3 voltage_V=5.02821 current_A=0.02656 dp_V=3.037 dn_V=0.072 temp_C_ema=28.200 energy_Ws=0.131149 capacity_As=0.026104
--- esp-idf-monitor 1.9.0 on /dev/ttyACM0 115200
--- Quit: Ctrl+] | Menu: Ctrl+T | Help: Ctrl+T followed by Ctrl+H
Executing action: monitor
Running idf_monitor in directory /opt/sensor-depth-snow/embedded
Executing "/root/.espressif/python_env/idf5.5_py3.13_env/bin/python /opt/esp32/esp-idf/tools/idf_monitor.py -p /dev/ttyACM0 -b 115200 --toolchain-prefix riscv32-esp-elf- --target esp32c3 --revision 3 --decode-panic backtrace /opt/sensor-depth-snow/embedded/build/sensor_depth_snow.elf /opt/sensor-depth-snow/embedded/build/bootloader/bootloader.elf -m '/root/.espressif/python_env/idf5.5_py3.13_env/bin/python' '/opt/esp32/esp-idf/tools/idf.py' '-p' '/dev/ttyACM0'"...
ESP-ROM:esp32c3-api1-20210207
Build:Feb  7 2021
rst:0x15 (USB_UART_CHIP_RESET),boot:0xd (SPI_FAST_FLASH_BOOT)
Saved PC:0x40383f1e
--- 0x40383f1e: rv_utils_wait_for_intr at /opt/esp32/esp-idf/components/riscv/include/riscv/rv_utils.h:79
--- (inlined by) esp_cpu_wait_for_intr at /opt/esp32/esp-idf/components/esp_hw_support/cpu.c:62
SPIWP:0xee
mode:DIO, clock div:1
load:0x3fcd5830,len:0x40c
load:0x403cbf10,len:0x904
--- 0x403cbf10: call_start_cpu0 at /opt/esp32/esp-idf/components/bootloader/subproject/main/bootloader_start.c:25
load:0x403ce710,len:0x23b0
--- 0x403ce710: esp_flash_encryption_enabled at /opt/esp32/esp-idf/components/bootloader_support/src/flash_encrypt.c:89
entry 0x403cbf10
--- 0x403cbf10: call_start_cpu0 at /opt/esp32/esp-idf/components/bootloader/subproject/main/bootloader_start.c:25
I (85) cpu_start: Unicore app
I (93) cpu_start: Pro cpu start user code
I (93) cpu_start: cpu freq: 160000000 Hz
I (93) cpu_start: cpu freq: 160000000 Hz
I (94) app_init: Application information:
I (94) app_init: Project name:     sensor_depth_snow
I (94) app_init: App version:      ec9cf2c
I (94) app_init: Compile time:     Mar 25 2026 21:41:42
I (94) app_init: ELF file SHA256:  b2c770c27...
I (94) app_init: ESP-IDF:          v5.5.2
I (94) efuse_init: Min chip rev:     v0.3
I (94) efuse_init: Max chip rev:     v1.99
I (94) efuse_init: Chip rev:         v0.4
I (95) heap_init: Initializing. RAM available for dynamic allocation:
I (95) heap_init: At 3FC956C0 len 0002A940 (170 KiB): RAM
I (95) heap_init: At 3FCC0000 len 0001C710 (113 KiB): Retention RAM
I (95) heap_init: At 3FCDC710 len 00002950 (10 KiB): Retention RAM
I (95) heap_init: At 50001838 len 000007A0 (1 KiB): RTCRAM
I (96) spi_flash: detected chip: generic
I (96) spi_flash: flash io: dio
W (96) spi_flash: Detected size(4096k) larger than the size in the binary image header(2048k). Using the size in the binary image header.
I (97) sleep_gpio: Configure to isolate all GPIO pins in sleep state
I (97) sleep_gpio: Enable automatic switching of GPIO sleep configuration
I (98) main_task: Started on CPU0
I (98) main_task: Calling app_main()
I (98) app_hardware_entry: POWER=HARDWARE-ENTRY
20260326132625.350 EVENT: HARDWARE-ENTRY
I (98) app_set_log_level: log_level[*]=verbose (5) (usb_connected || debug_pin_low)
I (98) temperature_sensor: Range [-10°C ~ 80°C], error < 1°C
D (98) temperature_sensor: range changed, change to index 2

...

D (2768) spi_hal: eff: 4000, limit: 80000k(/0), 0 dummy, -1 delay
D (2768) spi_master: SPI2: New device added to CS5, effective clock: 4000000 Hz
D (2768) hw_spi_start: spi started: mosi=7 clk=5 dc=8 busy=10 cs=-1 freq=4000000
I (2898) device-epd1680: started
D (2898) device-epd1680: [is_ready=1]
20260326132628.157: timestamp=1774531588.148 sample_in_packet=3 voltage_V=5.01624 current_A=0.06424 dp_V=2.676 dn_V=0.321 temp_C_ema=28.200 energy_Ws=0.551259 capacity_As=0.109657
I (3048) epd1680_write: POWER=EPD1680-UPDATE-BEGIN
20260326132628.229 EVENT: EPD1680-UPDATE-BEGIN
20260326132629.197: timestamp=1774531589.188 sample_in_packet=3 voltage_V=5.01829 current_A=0.05834 dp_V=2.767 dn_V=0.344 temp_C_ema=28.200 energy_Ws=0.847833 capacity_As=0.168727
I (4578) epd1680_write: POWER=EPD1680-UPDATE-COMPLETE
20260326132629.749 EVENT: EPD1680-UPDATE-COMPLETE
I (4578) device-epd1680: write: complete - full 128x296 at (0,0)
D (4578) device-epd1680: [is_ready=1]
20260326132630.237: timestamp=1774531590.227 sample_in_packet=3 voltage_V=5.01954 current_A=0.05558 dp_V=2.735 dn_V=0.327 temp_C_ema=28.200 energy_Ws=1.147770 capacity_As=0.228493
20260326132631.237: timestamp=1774531591.227 sample_in_packet=3 voltage_V=5.01954 current_A=0.05364 dp_V=2.785 dn_V=0.203 temp_C_ema=28.200 energy_Ws=1.419154 capacity_As=0.282557
20260326132632.237: timestamp=1774531592.228 sample_in_packet=3 voltage_V=5.02016 current_A=0.05365 dp_V=2.699 dn_V=0.263 temp_C_ema=28.200 energy_Ws=1.688431 capacity_As=0.336198
D (7078) device-epd1680: [is_ready=1]
I (7088) epd1680_write: POWER=EPD1680-UPDATE-BEGIN
20260326132632.269 EVENT: EPD1680-UPDATE-BEGIN
20260326132633.277: timestamp=1774531593.268 sample_in_packet=3 voltage_V=5.01845 current_A=0.05786 dp_V=2.794 dn_V=0.183 temp_C_ema=28.200 energy_Ws=1.979758 capacity_As=0.394239
I (8618) epd1680_write: POWER=EPD1680-UPDATE-COMPLETE
20260326132633.789 EVENT: EPD1680-UPDATE-COMPLETE
I (8618) device-epd1680: write: complete - full 128x296 at (0,0)
D (8618) device-epd1680: [is_ready=1]
20260326132634.317: timestamp=1774531594.308 sample_in_packet=3 voltage_V=5.01931 current_A=0.05524 dp_V=2.776 dn_V=0.346 temp_C_ema=28.200 energy_Ws=2.278965 capacity_As=0.453858
20260326132635.317: timestamp=1774531595.308 sample_in_packet=3 voltage_V=5.02016 current_A=0.05369 dp_V=2.918 dn_V=0.367 temp_C_ema=28.200 energy_Ws=2.550092 capacity_As=0.507870

...

D (47868) hw_spi_stop: spi stopped: pins released
D (47868) gpio: GPIO[9]| InputEn: 1| OutputEn: 0| OpenDrain: 0| Pullup: 0| Pulldown: 0| Intr:0
I (47868) device-epd1680: stopped
I (47868) d_interface_test: EPD1680 (SPI+DC+BUSY+RES): PASS [custom 128x296]
I (47968) d_interface_test: ========================================
I (47968) d_interface_test:   RESULTS: 1 PASS, 0 FAIL, 1 TOTAL
I (47968) d_interface_test: ========================================
I (47968) m_powermaster_disable: POWER=POWERMASTER-DISABLE
20260326132713.149 EVENT: POWERMASTER-DISABLE
I (48018) m_powermaster: disabled
D (48018) nvs: nvs_close 1
20260326132713.237: timestamp=1774531633.227 sample_in_packet=3 voltage_V=5.02231 current_A=0.02315 dp_V=2.755 dn_V=0.397 temp_C_ema=28.102 energy_Ws=13.068376 capacity_As=2.603371
20260326132714.277: timestamp=1774531634.267 sample_in_packet=3 voltage_V=5.03200 current_A=0.02303 dp_V=2.760 dn_V=0.362 temp_C_ema=28.110 energy_Ws=13.197082 capacity_As=2.628967
20260326132715.317: timestamp=1774531635.307 sample_in_packet=3 voltage_V=5.03172 current_A=0.02304 dp_V=2.720 dn_V=0.290 temp_C_ema=28.119 energy_Ws=13.317580 capacity_As=2.652914
20260326132716.357: timestamp=1774531636.347 sample_in_packet=3 voltage_V=5.03211 current_A=0.02301 dp_V=2.680 dn_V=0.334 temp_C_ema=28.100 energy_Ws=13.438089 capacity_As=2.676863
20260326132717.397: timestamp=1774531637.387 sample_in_packet=3 voltage_V=5.03204 current_A=0.02300 dp_V=2.744 dn_V=0.323 temp_C_ema=28.100 energy_Ws=13.558493 capacity_As=2.700790
20260326132718.437: timestamp=1774531638.427 sample_in_packet=3 voltage_V=5.03204 current_A=0.02300 dp_V=2.648 dn_V=0.330 temp_C_ema=28.100 energy_Ws=13.678859 capacity_As=2.724710
20260326132719.477: timestamp=1774531639.467 sample_in_packet=3 voltage_V=5.03211 current_A=0.02300 dp_V=2.843 dn_V=0.422 temp_C_ema=28.100 energy_Ws=13.799247 capacity_As=2.748634
20260326132720.477: timestamp=1774531640.467 sample_in_packet=3 voltage_V=5.03180 current_A=0.02300 dp_V=2.746 dn_V=0.415 temp_C_ema=28.100 energy_Ws=13.914977 capacity_As=2.771634
20260326132721.517: timestamp=1774531641.507 sample_in_packet=3 voltage_V=5.03223 current_A=0.02302 dp_V=2.729 dn_V=0.211 temp_C_ema=28.100 energy_Ws=14.035410 capacity_As=2.795567
20260326132722.517: timestamp=1774531642.507 sample_in_packet=3 voltage_V=5.03133 current_A=0.02302 dp_V=2.793 dn_V=0.273 temp_C_ema=28.100 energy_Ws=14.151284 capacity_As=2.818596
I (58018) app_hardware_restart_bysoftware: POWER=HARDWARE-EXIT-RESTART-BY-SOFTWARE
20260326132723.229 EVENT: HARDWARE-EXIT-RESTART-BY-SOFTWARE
ESP-ROM:esp32c3-api1-20210207

...

D (98) nvs: nvs_get_str_or_blob a/config
I (98) app_main: conditions: temp=30.9°C
20260326132723.549: timestamp=1774531643.539 sample_in_packet=3 voltage_V=5.03180 current_A=0.02356 dp_V=2.770 dn_V=0.378 temp_C_ema=28.100 energy_Ws=14.273578 capacity_As=2.842900


Child exited with status 0
Total energy:   14.321070 Ws
Total capacity: 2.852338 As

Draining USB buffer …
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Drained 64 bytes
Running: /usr/local/bin/plot.gnuplot powermon/powermon-20260326132624
Using data file: powermon/powermon-20260326132624.power.csv
Using markers:   powermon/powermon-20260326132624.marks.csv
Data duration:   59.9 s  →  image width: 1600 px
Output:          powermon/powermon-20260326132624.png
root@workshop:/opt/sensor-depth-snow/embedded# ls -l powermon/
total 672
-rw-r--r-- 1 root root  12322 Mar 26 13:27 powermon-20260326132624.log
-rw-r--r-- 1 root root   1031 Mar 26 13:27 powermon-20260326132624.marks.csv
-rw-r--r-- 1 root root 244586 Mar 26 13:27 powermon-20260326132624.png
-rw-r--r-- 1 root root 421718 Mar 26 13:27 powermon-20260326132624.power.csv
root@workshop:/opt/sensor-depth-snow/embedded# for i in powermon/*.{csv,log}; do echo '+++' $i; head -5 $i; done
+++ powermon/powermon-20260326132624.marks.csv
# timestamp event_fields...
1774531585.350 HARDWARE-ENTRY
1774531587.790 POWERMASTER-ENABLE
1774531588.230 EPD1680-UPDATE-BEGIN
1774531589.750 EPD1680-UPDATE-COMPLETE
+++ powermon/powermon-20260326132624.power.csv

timestamp sample_in_packet voltage_V current_A dp_V dn_V temp_C_ema energy_Ws capacity_As
1774531584.029 0 5.02325 0.05776 3.035 0.072 28.200 0.002901 0.000578
1774531584.039 1 5.02325 0.05801 3.036 0.071 28.200 0.005815 0.001158
1774531584.049 2 5.02325 0.05751 3.036 0.071 28.200 0.008704 0.001733
+++ powermon/powermon-20260326132624.log
--- esp-idf-monitor 1.9.0 on /dev/ttyACM0 115200
--- Quit: Ctrl+] | Menu: Ctrl+T | Help: Ctrl+T followed by Ctrl+H
Executing action: monitor
Running idf_monitor in directory /opt/sensor-depth-snow/embedded
Executing "/root/.espressif/python_env/idf5.5_py3.13_env/bin/python /opt/esp32/esp-idf/tools/idf_monitor.py -p /dev/ttyACM0 -b 115200 --toolchain-prefix riscv32-esp-elf- --target esp32c3 --revision 3 --decode-panic backtrace /opt/sensor-depth-snow/embedded/build/sensor_depth_snow.elf /opt/sensor-depth-snow/embedded/build/bootloader/bootloader.elf -m '/root/.espressif/python_env/idf5.5_py3.13_env/bin/python' '/opt/esp32/esp-idf/tools/idf.py' '-p' '/dev/ttyACM0'"...
root@workshop:/opt/sensor-depth-snow/embedded#

```
