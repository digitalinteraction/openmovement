// Structure of a .CWA binary file.
// .hexpat pattern file for ImHex: https://imhex.werwolv.net/
// ImHex: File / Import / Pattern File / Browse... / cwa.hexpat / Open
// https://docs.werwolv.net/pattern-language/

#pragma MIME application/x-cwa

// All values are stored in little-endian order (LSB first), and structures are tightly-packed.
#pragma endian little

// These definitions use the mem.pat types: [u|s]{8|16|32}
#include <std/mem.pat>
#include <std/core.pat>
#include <std/io.pat>
#include <type/magic.pat>

// Maximum number of data blocks to decode (for performance reasons)
#define MAXIMUM_BLOCKS 128
u32 SECTOR_SIZE = 512;  // as sizeof(CwaData) cannot be computed dynamically because of the code conditional on format

// Timestamps are packed into a 32-bit value: (MSB) YYYYYYMM MMDDDDDh hhhhmmmm mmssssss (LSB)
using cwa_timestamp_t;
fn format_timestamp(cwa_timestamp_t t) {
    return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}", 2000 + t.timestamp_parts.year, t.timestamp_parts.month, t.timestamp_parts.day, t.timestamp_parts.hours, t.timestamp_parts.minutes, t.timestamp_parts.seconds);
};
bitfield cwa_timestamp_parts_t {
    year : 6;
    month : 4;
    day : 5;
    hours : 5;
    minutes : 6;
    seconds : 6;
} [[bitfield_order(std::core::BitfieldOrder::MostToLeastSignificant, 32), static]];
union cwa_timestamp_t {
    cwa_timestamp_parts_t timestamp_parts;
    u32 timestamp;
} [[format("format_timestamp"), static]];

fn format_header(u16 header) {
    str type = "unknown";
    if (header == 0x444d) { 
        type = "header";
    } else if (header == 0x5841) {
        type = "data";
    } else if (header == 0xffff) {
        type = "empty";
    }
    return std::format("{:c}{:c} {:s} (0x{:04X})", header & 0xff, (header >> 8) & 0xff, type, header);
};

/*
enum hardware_type_t : u8 {
  HardwareAX3_unset = 0x00,
  HardwareUnknownLow = 0x01 ... 0x16,
  HardwareAX3_explicit = 0x17,
  HardwareUnknownMed = 0x18 ... 0x63,
  HardwareAX6 = 0x64,
  HardwareUnknownMed = 0x65 ... 0xfe,
  HardwareAX3_implied = 0xFF,
};
*/

fn format_hardware(u8 hardwareType) {
    str name = "unknown";
    if (hardwareType == 0x00 || hardwareType == 0xff || hardwareType == 0x17) { 
        name = "AX3";
    } else if (hardwareType == 0x64) {
        name = "AX6";
    }
    return std::format("{:s} (0x{:02X})", name, hardwareType);
};

fn format_config(u8 sensorConfig) {
    u8 gyroRange = 0;
    bool magnetometerEnabled = false;
    if (sensorConfig != 0x00 && sensorConfig != 0xff) { 
        gyroRange = 8000 / (1 << (sensorConfig & 0x0f));
        magnetometerEnabled = (sensorConfig & 0xf0) != 0;
    }
    if (!magnetometerEnabled) {
        if (gyroRange == 0) {
            return std::format("accelerometer-only (0x{:02X})", sensorConfig);
        } else {
            return std::format("gyro {:d} dps (0x{:02X})", gyroRange, sensorConfig);
        }
    } else {
        if (gyroRange == 0) {
            return std::format("magnetometer enabled (0x{:02X})", sensorConfig);
        } else {
            return std::format("gyro {:d} dps + magnetometer (0x{:02X})", gyroRange, sensorConfig);
        }
    }
};

fn format_rate(u8 rate) {
    return std::format("{:d} Hz +/-{:d}g (0x{:02X})", (3200/(1<<(15-(rate & 0x0f)))), (16 >> (rate >> 6)), rate);
};

// The battery value is packed into a single byte, and the conversion is:
//   volts = (battery + 512.0) * 6000 / 1024 / 1000.0;
fn format_battery(u8 rawBattery) {
    u16 batteryAdc = rawBattery;
    batteryAdc = batteryAdc + 512;

    u32 batteryVoltage = batteryAdc;
    batteryVoltage = batteryVoltage * 6000 / 1024;
    
    return std::format("{:d} mV (ADC: {:d}) (Raw: {:d})", batteryVoltage, batteryAdc, rawBattery);
};


// Compose 32-bit device ID
//
// (as a section)
//std::mem::Section device_id_section = std::mem::create_section("device_id_section");
//u32 device_id @ 0x00 in device_id_section;
//
// (as a setting output)
//u32 device_id out;
//
// (as a global -- how to make visible, or add to block even though unplaced?)
//u32 device_id;
//
using device_id_t;
fn transformer_device_id(device_id_t deviceId) {
    u32 device_id = deviceId.lower;
    if (deviceId.upper != 0xffff) {
        device_id |= deviceId.upper << 16;
    }
    return device_id;
};
fn format_device_id(device_id_t deviceId) {
    u32 device_id = deviceId.lower;
    if (deviceId.upper != 0xffff) {
        device_id |= deviceId.upper << 16;
    }
    return std::format("{:d}", device_id);
};
bitfield device_id_t {
    lower : 16;
    padding : 32;
    upper : 16;
} [[transform("transformer_device_id"), format("format_device_id"), comment("Synthetic field composed from upper and lower parts"), static]];

fn format_annotation(auto annotation) {
    str annotation_str = annotation;
    annotation_str = std::string::replace(annotation_str, "\xFF", "\x00");
    annotation_str = std::string::replace(annotation_str, " ", "\x00");
    return annotation_str;
};

// Light field packed AAAGGGLLLLLLLLLL -- light is least significant 10 bits, accel scale 3-MSB, gyro scale next 3 bits.
//
// The light sensor is only suitable for a general, relative, indicator of light (the sensor is not placed with a light pipe, the case absorbs a lot of light, etc.)
// That said, you can use the following formula for the AX3:
//   log10LuxTimes10Power3 = ((light + 512.0) * 6000 / 1024);
//   lux = pow(10.0, log10LuxTimes10Power3 / 1000.0);
bitfield light_t {
    accelScale : 3;
    gyroScale : 3;
    light : 10;
} [[bitfield_order(std::core::BitfieldOrder::MostToLeastSignificant, 16), static]];

bitfield temperature_t {
    _reserved : 6;
    temperature : 10;
} [[bitfield_order(std::core::BitfieldOrder::MostToLeastSignificant, 16), static]];

// The temperature conversion (which comes from the ADC units measuring 3/1024 V, the sensor giving 0.5 V at 0 C, and 10 mV/C):
//   centigrade = (temperature * 75.0 / 256 - 50)
fn format_temp(temperature_t tempAdc) {
    float centigrade = tempAdc.temperature;
    centigrade = centigrade * 75.0 / 256 - 50;
    
    return std::format("{:.2f} ^C (ADC: {:d})", centigrade, tempAdc.temperature);
};

bitfield events_t {
    bool resumed : 1;
    bool _reservedSingleTap : 1 [[hidden]];
    bool _reservedDoubleTap : 1 [[hidden]];
    bool _reserved0 : 1 [[hidden]];
    bool _reservedHardwareBuffer : 1 [[hidden]];
    bool _reservedSoftwareBuffer : 1 [[hidden]];
    bool _reservedInternalFlag : 1 [[hidden]];
    bool _reserved1 : 1 [[hidden]];
} [[static]];

/*
enum num_axis_t : u8 {
    AccelPacked = 0x30,
    Accel = 0x32,
    GyroAccel = 0x62,
    GyroAccelMag = 0x92,
} [[static]];
*/

fn format_num_axes(u8 numAxes) {
    str name = "unknown";
    if (numAxes == 0x30) { 
        name = "AccelPacked";
    } else if (numAxes == 0x32) { 
        name = "Accel";
    } else if (numAxes == 0x62) { 
        name = "GyroAccel";
    } else if (numAxes == 0x92) { 
        name = "GyroAccelMag";
    }
    return std::format("{:s} (0x{:02X})", name, numAxes);
};

//   Accelerometer sample values are in units of (where g = 9.81 m/s/s) and need to have a scaling factor applied.
//   Accel scaling for 16-bit unpacked values: 1/2^(8+n) [g] -- expected values:
//     0: AX3   1/256   (2^8,  8+0)
//     3: +-16g 1/2048  (2^11, 8+3)
//     4: +-8g  1/4096  (2^12, 8+4)
//     5: +-4g  1/8192  (2^13, 8+5)
//     6: +-2g  1/16384 (2^14, 8+6)
//
//   Gyro scaling for 16-bit unpacked values: 4000/2^n [dps] -- expected values:
//     1: 2000 dps
//     2: 1000 dps
//     3:  500 dps
//     4:  250 dps
//     5:  125 dps

// * Packing format 0 (3-axis, 120 samples/packet): One 32-bit little-endian packed value, where the bits in the bytes ordered [3][2][1][0] are:
//        [byte-3] [byte-2] [byte-1] [byte-0]
//        eezzzzzz zzzzyyyy yyyyyyxx xxxxxxxx
//        10987654 32109876 54321098 76543210
//    x/y/z represents the three 10-bit axis values.
//    The left-most bit of each value is the sign bit, and must be sign-extended to fill the word to produce a signed value (-512 to 511).
//    The 'e' represents the binary exponent value (0-3) -- the number of bits to left-shift all the axis values.
bitfield packed_t {
    exponent : 2;
    signed az: 10;
    signed ay: 10;
    signed ax: 10;
} [[bitfield_order(std::core::BitfieldOrder::MostToLeastSignificant, 32), static]];

// Split the x/y/z/ values out, using the supplied exponent:
//#define UNPACK_X(value) ((short)( (short)((unsigned short)0xffc0 & (unsigned short)(value <<  6)) >> (6 - ((unsigned char)(value >> 30))) ))
//#define UNPACK_Y(value) ((short)( (short)((unsigned short)0xffc0 & (unsigned short)(value >>  4)) >> (6 - ((unsigned char)(value >> 30))) ))
//#define UNPACK_Z(value) ((short)( (short)((unsigned short)0xffc0 & (unsigned short)(value >> 14)) >> (6 - ((unsigned char)(value >> 30))) ))

struct accel_t {
    s16 ax;
    s16 ay;
    s16 az;
} [[static]];

struct gyro_accel_t {
    s16 gx;
    s16 gy;
    s16 gz;
    s16 ax;
    s16 ay;
    s16 az;
} [[static]];

struct gyro_accel_mag_t {
    s16 gx;
    s16 gy;
    s16 gz;
    s16 ax;
    s16 ay;
    s16 az;
    s16 mx;
    s16 my;
    s16 mz;
} [[static]];


// CWA sectors
// Header block nominally 1024 bytes, or treat as 512 and second block will start with 0xff/0xff and can be ignored.
// Data blocks always 512 bytes in length at a 512-byte offset, the first at offset 1024 after the header
struct CwaHeader
{
    //type::Magic<"MD">
    u16 packetHeader [[format("format_header")]];       ///< @ 0  +2   Header ASCII "MD", little-endian (0x444D); data block ASCII "AX", little-endian (0x5841)
    u16 packetLength;                           ///< @ 2  +2   Packet length; Header 1020 bytes (1024 bytes total); Data 508 bytes (512 bytes total); 

    //if (packetHeader == 0x444d) {
    u8  hardwareType [[format("format_hardware")]]; ///< @ 4  +1 * Hardware type (0x00/0xff/0x17 = AX3, 0x64 = AX6)
    device_id_t deviceId;                   ///< (@5 +8, synthetic element to compose two 16-bit lower/upper device IDs separated by session id)
    u16 deviceIdLower @ 5;                  ///< @ 5  +2   Device identifier (lower 16-bits)
    u32 sessionId @ 7;                      ///< @ 7  +4   Unique session identifier
    u16 deviceIdUpper @ 11;                 ///< @11  +2 * Upper word of device id (if 0xffff is read, treat as 0x0000)
    cwa_timestamp_t loggingStartTime;       ///< @13  +4   Start time for delayed logging
    cwa_timestamp_t loggingEndTime;         ///< @17  +4   Stop time for delayed logging
    u32 loggingCapacity;                    ///< @21  +4   (Deprecated: preset maximum number of samples to collect, 0 = unlimited)
    u8 _reserved1[1] [[hidden]];            ///< @25  +1   (1 byte reserved)
    u8  flashLed;                           ///< @26  +1   Flash LED during recording
    u8 _reserved2[8] [[hidden]];            ///< @27  +8   (8 bytes reserved)
    u8  sensorConfig [[format("format_config")]];   ///< @35  +1 * Fixed rate sensor configuration, 0x00 or 0xff means accel only, otherwise bottom nibble is gyro range (8000/2^n dps): 2=2000, 3=1000, 4=500, 5=250, 6=125, top nibble non-zero is magnetometer enabled.
    u8  samplingRate [[format("format_rate")]];     ///< @36  +1   Sampling rate code, frequency (3200/(1<<(15-(rate & 0x0f)))) Hz, range (+/-g) (16 >> (rate >> 6)).
    cwa_timestamp_t lastChangeTime;         ///< @37  +4   Last change metadata time
    u8  firmwareRevision;                   ///< @41  +1   Firmware revision number
    s16 timeZone [[hidden]];                ///< @42  +2   (Unused: originally reserved for a "Time Zone offset from UTC in minutes", 0xffff = -1 = unknown)
    u8 _reserved3[20] [[hidden]];           ///< @44  +20  (20 bytes reserved)
    char annotation[448] [[format("format_annotation"), color("00000000")]];                   ///< @64  +448 Scratch buffer / meta-data (448 ASCII characters, ignore trailing 0x20/0x00/0xff bytes, url-encoded UTF-8 name-value pairs)
    //u8 annotation[while(((std::mem::read_unsigned($, 1) - 0x21) & 0xff) < 0x5f)];

    u8 _reservedBlock[512] [[hidden]];      ///< @512 +512 Reserved for device-specific meta-data (512 bytes, ASCII characters, ignore trailing 0x20/0x00/0xff bytes, url-encoded UTF-8 name-value pairs, leading '&' if present?)

    // Populate 32-bit device ID
    //device_id = deviceIdLower;
    //if (deviceIdUpper != 0xffff) {
    //    device_id |= deviceIdUpper << 16;
    //}
} [[static]];

struct CwaData
{
    //type::Magic<"AX"> 
    u16 packetHeader [[format("format_header")]];   ///< @ 0  +2   Data block ASCII "AX", little-endian (0x5841)
    u16 packetLength;                           ///< @ 2  +2   Packet length; Header 1020 bytes (1024 bytes total); Data 508 bytes (512 bytes total); 

    // if (packetHeader == 0x5841) {
    u16 deviceFractional;                   ///< @ 4  +2   Top bit set: 15-bit fraction of a second for the time stamp, the timestampOffset was already adjusted to minimize this assuming ideal sample rate; Top bit clear: 15-bit device identifier, 0 = unknown;
    u32 sessionId;                          ///< @ 6  +4   Unique session identifier, 0 = unknown
    u32 sequenceId;                         ///< @10  +4   Sequence counter (0-indexed), each packet has a new number (reset if restarted)
    cwa_timestamp_t timestamp;              ///< @14  +4   Last reported RTC value, 0 = unknown
    light_t lightScale;                     ///< @18  +2   AAAGGGLLLLLLLLLL Bottom 10 bits is last recorded light sensor value in raw units, 0 = none; top three bits are unpacked accel scale (1/2^(8+n) g); next three bits are gyro scale	(8000/2^n dps)
    temperature_t temperature [[format("format_temp")]];    ///< @20  +2   Last recorded temperature sensor value in raw units (bottom-10 bits), 0 = none; (top 6-bits reserved)
    events_t events;                        ///< @22  +1   Event flags since last packet, b0 = resume logging, b1 = reserved for single-tap event, b2 = reserved for double-tap event, b3 = reserved, b4 = reserved for diagnostic hardware buffer, b5 = reserved for diagnostic software buffer, b6 = reserved for diagnostic internal flag, b7 = reserved)
    u8  battery [[format("format_battery")]]; ///< @23  +1   Last recorded battery level in scaled/cropped raw units (double and add 512 for 10-bit ADC value), 0 = unknown
    u8  sampleRate [[format("format_rate")]]; ///< @24  +1   Sample rate code, frequency (3200/(1<<(15-(rate & 0x0f)))) Hz, range (+/-g) (16 >> (rate >> 6)).
    //num_axis_t 
    u8 numAxesBPS [[format("format_num_axes")]];                  ///< @25  +1   0x32 (top nibble: number of axes, 3=Axyz, 6=Gxyz/Axyz, 9=Gxyz/Axyz/Mxyz; bottom nibble: packing format - 2 = 3x 16-bit signed, 0 = 3x 10-bit signed + 2-bit exponent)
    s16 timestampOffset;                    ///< @26  +2   Relative sample index from the start of the buffer where the whole-second timestamp is valid
    u16 sampleCount;                        ///< @28  +2   Number of sensor samples (if this sector is full -- Axyz: 80 or 120 samples, Gxyz/Axyz: 40 samples)

    //if (numAxesBPS == num_axis_t::AccelPacked) {
    if (numAxesBPS == 0x30) {
        packed_t samplesAccelPacked[120];                 ///< @30  +480 Raw sample data (3-axis packed)
    } else if (numAxesBPS == 0x32) {
        accel_t samplesAccel[80];              ///< @30  +480 Raw sample data (3-axis unpacked)
    } else if (numAxesBPS == 0x62) {
        gyro_accel_t samplesGyroAccel[40];         ///< @30  +480 Raw sample data (6-axis unpackd)
    } else if (numAxesBPS == 0x92) {
        gyro_accel_mag_t samplesGyroAccelMag[26];     ///< @30  +480 Raw sample data (9-axis unpacked)
        u8 _padding[12] [[hidden]];
    } else {
        u8  samplesUnknown[480];                 ///< @30  +480 Raw sample data.  Each sample is either 3x/6x/9x 16-bit signed values (x, y, z) or one 32-bit packed value (The bits in bytes [3][2][1][0]: eezzzzzz zzzzyyyy yyyyyyxx xxxxxxxx, e = binary exponent, lsb on right)
    }

    u16 checksum;                           ///< @510 +2   Checksum of packet (16-bit word-wise sum of the whole packet should be zero)
};  // Not sure if this can be [[static]] as the interpretation of the samples depends on the values


CwaHeader header @0;

// Calculate number of data blocks to decode
u32 countBlocks = 0;
if (std::mem::size() > sizeof(CwaHeader)) {
    countBlocks = (std::mem::size() - sizeof(CwaHeader)) / SECTOR_SIZE;
}
if (MAXIMUM_BLOCKS > 0 && countBlocks > MAXIMUM_BLOCKS) {
    countBlocks = MAXIMUM_BLOCKS;
}
CwaData data[countBlocks] @sizeof(CwaHeader);
