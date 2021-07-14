# AX3/AX6 FAQ


## Maximum recording duration

Approximate maximum recording duration from a single full battery charge.

| Device  | Configuration  |   Sample Capacity |       6.25 Hz |     12.5 Hz |      25 Hz |    50 Hz |  100 Hz | 200 Hz | 400 Hz | 800 Hz | 1600 Hz | 3200 Hz |
|---------|----------------|------------------:|--------------:|------------:|-----------:|---------:|--------:|-------:|-------:|-------:|--------:|--------:|
| AX3     | packed         | 119009040 samples |             - | ~65d (110d) | ~48d (55d) |     27d* |  13.5d* |   6.8d |   3.4d |  41.3h |   20.6h |   10.3h |
| AX3     | unpacked       |  79339360 samples |  >65d? (146d) | ~65d? (73d) |      36.5d |      18d |     9d* |   4.5d |   2.3d |  27.5h |   13.7h |    6.8h |
| AX6     | accel. only    | 158714880 samples | >146d? (293d) |        146d |        73d |    36.5d |  18.3d  |   9.1d |   4.5d |  55.1h |   27.5h |       - |
| AX6     | accel. + gyro. |  79357440 samples |             - |           - | 9d (36.7d) | 9d (18d) |     9d* |   4.5d |   2.3d |  27.5h |   13.7h |       - |

* `d` days (<= 400 Hz)
* `h` hours (>= 800 Hz)
* `-` an unsupported configuration. 
* `?` an extrapolated best-guess
* `~` uncertainty in the value (charge-limited, and device-dependent)
* `()` where a recording is likely to be battery-limited, times in brackets are the storage capacity (which could only be reached if recharged). 
* `*` where the storage- and battery-limits are similar, which is reached first may be device-dependent. 

<!-- AX6 12.5Hz Accelerometer-only 149 days 100%-39% battery -->


## Sensor Data Characteristics

| Characteristic             | AX3                                             | AX6                                           |
|----------------------------|-------------------------------------------------|-----------------------------------------------|
| Sample Rate                | 6.25`*`/12.5/25/50/100/200/400/800/1600/3200 Hz | 6.25`*`/12.5`*`/25/50/100/200/400/800/1600 Hz |
| Accelerometer Range        | &plusmn;2/4/8/16 _g_                            | &plusmn;2/4/8/16 _g_                          |
| Gyroscope Range            | _none_                                           | 125/250/500/1000/2000 &deg;/s                 |
| Underlying sensing device  | [Analog Devices ADXL345](https://www.analog.com/media/en/technical-documentation/data-sheets/ADXL345.pdf) | [Bosch BMI160](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi160-ds000.pdf) |
| Notes                      | `*` 'Unpacked' recording mode only.             | `*` With gyroscope off.                       |

<!-- | Samples per 512 byte storage block | Packed (10-bit mode): 120; Unpacked (full resolution): 80. | Accelerometer-only: 80; Accelerometer+Gyroscope: 40. | -->


## Synchronizing data between devices, or with other devices

The AX devices have an internal real-time clock to keep track of time.  When the device is configured, the internal clock is set to the system time of the configuring computer.  This time is set to the nearest second, and the operation itself may be subject to some jitter (e.g. from the operating system performing other actions).  Afterwards, as with any clock, it will be subject to clock drift, potentially of the order of around ±2 seconds per day.  
 
There is no external communication while the AX devices are recording, so they cannot directly have their clocks set or read during normal use.  However, depending on your application, you may have some options, including:
 
* For a single sensor over, say, a week or so, this clock drift rate may be acceptable to directly combine with some external sensors, such as GPS or a mobile phone (assuming the configuring computer had a similar time when configuring).
 
* If you are placing multiple accelerometers on a single moving body over a long period of time (e.g. a person), then there is some software that synchronizes the signal from devices that are likely to see a similar movement: [timesync](https://github.com/digitalinteraction/timesync/).
 
* Where the setup/access allows (e.g. a lab-based recording, or one with frequent points of contact), you can introduce a "marker" -- a specific movement signal at one or more points (e.g. vigorous shaking before and after a session) that has its time externally recorded.  For a lab-based session, it might be appropriate to video record the session in a way that captures the shaking times directly.  It might be useful to introduce an external clock on a screen, e.g. this page: [Time Sync Clock](https://config.openmovement.dev/timesync/) -- on some supported phones/browsers (e.g. Chrome browser on Android), you can hold the phone against the device and tap-and-hold the screen to introduce an optical and vibration marker/pattern for the time.