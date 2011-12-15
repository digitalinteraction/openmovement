// CWA Data Conversion
// Dan Jackson, 2010-2011
//
// SQLite support added by Stefan Diewald <stefan.diewald@mytum.de>


#ifdef _WIN32
#define _CRT_SECURE_NO_DEPRECATE
//#include <windows.h>
#define strcasecmp _stricmp
#define timegm _mkgmtime
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <limits.h>

#ifdef _DEBUG
#include <conio.h>
#endif

#include "cwa.h"
#include "sqlite3.h"

typedef enum { FORMAT_DEFAULT, FORMAT_CSV, FORMAT_RAW, FORMAT_WAV, FORMAT_SQLITE } Format;
typedef enum { VALUES_DEFAULT, VALUES_INT, VALUES_FLOAT } Values;
typedef enum { TIME_DEFAULT, TIME_NONE, TIME_SEQUENCE, TIME_SECONDS, TIME_DAYS, TIME_SERIAL, TIME_EXCEL, TIME_MATLAB, TIME_BLOCK, TIME_TIMESTAMP } Time;
typedef enum { OPTIONS_NONE = 0x00, OPTIONS_LIGHT = 0x01, OPTIONS_TEMP = 0x02, OPTIONS_BATT = 0x04, OPTIONS_EVENTS = 0x08 } Options;

//#define DEFAULT_SAMPLE_RATE 100.0f      // HACK: Remove this, use value from file.

static void HexDump(const void *data, int length)
{
    int o;
    for (o = 0; o < length; o++)
    {
        printf("0x%02X,", ((const unsigned char *)data)[o]);
        if ((o + 1) % 16 == 0) { printf("\n"); }
    }
}


// Simple byte-wise addition into a 16-bit int
static unsigned short sum8(unsigned char *data, size_t len)
{
    unsigned short value = 0x0000;
    for (; len; --len)
    {
        value += *data++;
    }
    return value;
}

// 16-bit word-size addition, returns two's compliment
static unsigned short sum16(void *dataPointer, size_t lenBytes)
{
	unsigned short *data = (unsigned short *)dataPointer;
	size_t len = (lenBytes >> 1);
    unsigned short value = 0x0000;
    for (; len; --len)
    {
        value += *data++;
    }
    return value;
	// TODO: Should've treated as unsigned shorts, then take sum and bitwise NOT, then add 1 -- then total sum of words would be zero.
}

// 16-bit word-size addition, returns two's compliment
static unsigned short checksum16(void *data, size_t lenBytes)
{
	// Sum and bitwise NOT, then add 1 (then total sum of words would be zero)
    return (~sum16(data, lenBytes)) + 1;
}


#define SECTOR_SIZE 512
static unsigned char buffer[SECTOR_SIZE];

// Endian-independent short/long read/write
static void fputshort(unsigned short v, FILE *fp) { fputc((unsigned char)((v >> 0) & 0xff), fp); fputc((unsigned char)((v >> 8) & 0xff), fp); }
static void fputlong(unsigned long v, FILE *fp) { fputc((unsigned char)((v >> 0) & 0xff), fp); fputc((unsigned char)((v >> 8) & 0xff), fp); fputc((unsigned char)((v >> 16) & 0xff), fp); fputc((unsigned char)((v >> 24) & 0xff), fp); }

static char DumpFile(const char *filename, const char *outfile, Format format, Values values, Time time, Options options, float amplify, unsigned long iStart, unsigned long iLength, unsigned long iStep)
{
    unsigned long outputSize = 0;
    unsigned long totalSamples = 0;
    unsigned long lengthBytes;
    unsigned long lengthSectors;
    unsigned long sequence = 0;
    unsigned int n;
	double tStart = 0;
	double tLast = 0;
    char timestring[48] = "";
    float *floatBuffer = NULL;
    int floatBufferSize = 0;
	unsigned char events = 0x00;
	unsigned long deviceSessionId = 0;
    FILE *fp;
    FILE *ofp;
    sqlite3 *dbconn;
    sqlite3_stmt * stmt;
    char sql[256];
    char sql_prepare[256];

    // Process any default parameters
    if (format == FORMAT_DEFAULT)
    {
        format = FORMAT_CSV;
    }
    if (values == VALUES_DEFAULT)
    {
        values = VALUES_INT;
        if (format == FORMAT_CSV) { values = VALUES_FLOAT; }
    }
    if (time == TIME_DEFAULT)
    {
        time = TIME_NONE;
        if (format == FORMAT_CSV) { time = TIME_TIMESTAMP; }
    }
    if (format == FORMAT_SQLITE)
    {
        values = VALUES_INT;
    }

    fp = fopen(filename, "rb");
    if (fp == NULL) { return 1; }
    
    ofp = NULL;
    dbconn = NULL;
    if (outfile != NULL && outfile[0] != '\0')
    {
        if (format == FORMAT_CSV)
        {
            ofp = fopen(outfile, "w");
        }
        else if (format == FORMAT_SQLITE)
        {
            sqlite3_open(outfile, &dbconn);
            strcpy(sql, "CREATE TABLE acc (time INTEGER, x INTEGER, y INTEGER, z INTEGER");
            strcpy(sql_prepare, "INSERT INTO acc VALUES (?, ?, ?, ?");
			if (options & OPTIONS_LIGHT)
			{
				strcat(sql, ", light INTEGER");
				strcat(sql_prepare, ", ?");
			}
			if (options & OPTIONS_TEMP)
			{
				strcat(sql, ", temperature INTEGER");
				strcat(sql_prepare, ", ?");
			}
			if (options & OPTIONS_BATT) 
			{ 
				strcat(sql, ", battery INTEGER");
				strcat(sql_prepare, ", ?");
			}
            strcat(sql, ");");
            strcat(sql_prepare, ");");
            sqlite3_exec(dbconn, "DROP TABLE acc;", 0, 0, 0);
            sqlite3_exec(dbconn, sql, 0, 0, 0);
            sqlite3_exec(dbconn, "CREATE INDEX time_hash ON acc (time);", 0, 0, 0);
            if (sqlite3_prepare_v2(dbconn, sql_prepare, -1, &stmt, NULL) != SQLITE_OK) {
                printf("\nCould not prepare statement.\n");
                printf("%s\n", sql_prepare);
                return -1;
            }
            sqlite3_exec(dbconn, "BEGIN;", 0, 0, 0);
            ofp = (FILE *)-1;
        }
        else
        {
            ofp = fopen(outfile, "wb");
        }
    }
    else if (format == FORMAT_SQLITE)
    {
    	fprintf(stderr, "ERROR: No filename given. Necessary for SQLite.\n");
    	return -1;
    }
    
    if (ofp == NULL) { ofp = stdout; }

    fseek(fp, 0, SEEK_END);
    lengthBytes = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    lengthSectors = lengthBytes / SECTOR_SIZE;

    if (format == FORMAT_WAV)
    {
        unsigned long  nSamplesPerSec = 100;
        unsigned short nChannels = 3;
        unsigned short wBitsPerSample = (values == VALUES_FLOAT) ? 32 : 16;
        unsigned short wSubFormatTag = (values == VALUES_FLOAT) ? 3 : 1;     // From KSDATAFORMAT_SUBTYPE_IEEE_FLOAT or KSDATAFORMAT_SUBTYPE_PCM
        unsigned short nBlockAlign = nChannels * ((wBitsPerSample + 7) / 8);
        unsigned long  nAvgBytesPerSec = nSamplesPerSec * nBlockAlign;

        // Write 3-channel 'WAVE_FORMAT_EXTENSIBLE' .WAV header
        unsigned long expectedLength = (lengthSectors > 256) ? ((lengthSectors - 256) * 480) * (wBitsPerSample / 16) : 0;

        //  0, 1, 2, 3 = 'RIFF'
        fputc('R', ofp); fputc('I', ofp); fputc('F', ofp); fputc('F', ofp); 

        //  4, 5, 6, 7 = (file size - 8 bytes header) = (data size + 68 - 8)
        fputlong(expectedLength + 68 - 8, ofp);

        //  8, 9,10,11 = 'WAVE'
        fputc('W', ofp); fputc('A', ofp); fputc('V', ofp); fputc('E', ofp); 

        // 12,13,14,15 = 'fmt '
        fputc('f', ofp); fputc('m', ofp); fputc('t', ofp); fputc(' ', ofp); 

        // 16,17,18,19 = format size
        fputlong(40, ofp);          // For WAVE_FORMAT_EXTENSIBLE format

        // WAVEFORMATEX
        fputshort(0xFFFE, ofp);         // 20 WORD  wFormatTag = 0xFFFE; (WAVE_FORMAT_EXTENSIBLE)
        fputshort(nChannels, ofp);      // 22 WORD  nChannels; 
        fputlong(nSamplesPerSec, ofp);  // 24 DWORD nSamplesPerSec; 
        fputlong(nAvgBytesPerSec, ofp); // 28 DWORD nAvgBytesPerSec = nSamplesPerSec * nBlockAlign; 
        fputshort(nBlockAlign, ofp);    // 32 WORD  nBlockAlign = nChannels * ((wBitsPerSample + 7) / 8);
        fputshort(wBitsPerSample, ofp); // 34 WORD  wBitsPerSample; 
        fputshort(22, ofp);             // 36 WORD  cbSize = formatSize - 18;
        fputshort(wBitsPerSample, ofp); // 38 WORD  wValidBitsPerSample; (or wSamplesPerBlock if wBitsPerSample==0, or wReserved
        fputlong(0, ofp);               // 40 DWORD dwChannelMask;

        // 44 GUID SubFormat = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 }; (KSDATAFORMAT_SUBTYPE_PCM)
        // 44 GUID SubFormat = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 }; (KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
        fputc((wSubFormatTag & 0xff), ofp); fputc(((wSubFormatTag >> 8) & 0xff), ofp); fputc(0x00, ofp); fputc(0x00, ofp); 
        fputc(0x00, ofp); fputc(0x00, ofp); fputc(0x10, ofp); fputc(0x00, ofp); 
        fputc(0x80, ofp); fputc(0x00, ofp); fputc(0x00, ofp); fputc(0xAA, ofp); 
        fputc(0x00, ofp); fputc(0x38, ofp); fputc(0x9B, ofp); fputc(0x71, ofp); 

        // 60,61,62,63 = 'data'
        fputc('d', ofp); fputc('a', ofp); fputc('t', ofp); fputc('a', ofp); 

        // 64,65,66,67 = data size
        fputlong(expectedLength, ofp);
    }

    fprintf(stderr, "\rReading %d sectors...\n", (int)lengthSectors);
    for (n = 0; n < lengthSectors; n++)
    {
        unsigned long offset;
        //fprintf(stderr, "\rSECTOR %5d/%5d (%3d%%): ...\b\b\b", n, lengthSectors, 100 * n / lengthSectors);
        offset = n * SECTOR_SIZE;
        fseek(fp, offset, SEEK_SET);

        if (sizeof(DataPacket) != SECTOR_SIZE)
        {
            fprintf(stderr, "SEVERE WARNING: DataPacket size not equal to a sector size!\n");
        }

        if (fread(buffer, 1, SECTOR_SIZE, fp) != SECTOR_SIZE)
        {
            fprintf(stderr, "ERROR: Problem reading sector.\n");
        }
        else
        {
            unsigned short header = *((unsigned short *)buffer);
            if (header == HEADER_UNDEFINED_BLOCK)
            {
                fprintf(stderr, ".");
            }
            else if (header == HEADER_METADATA)
            {
                DataMeta *dataMeta;
                dataMeta = (DataMeta *)buffer;
                fprintf(stderr, "[MD]");
				deviceSessionId = dataMeta->sessionId;
            }
            else if (header == HEADER_USAGEBLOCK)
            {
                DataBlocksAvailable *dataBlocksAvailable;
                dataBlocksAvailable = (DataBlocksAvailable *)buffer;
                fprintf(stderr, "[UB]");
            }
            else if (header == HEADER_SESSIONINFO)
            {
                fprintf(stderr, "[SI]");
            }
            else if (header == HEADER_ACCELEROMETER)
            {
                int i, z, j; // , requiredFloatBufferSize;
                DataPacket *dataPacket;
				char ver;
				unsigned short sum;
				char checksumFail = 0;

                dataPacket = (DataPacket *)buffer;

				// See which format the packet is
				if (dataPacket->sampleRate == 0) 
				{
					ver = 0;
					sum = 0;
				}
				else
				{
					ver = 1;

					// If old type of checksum (very rare), replace with new checksum
					if (dataPacket->checksum == sum8((unsigned char *)dataPacket, sizeof(DataPacket) - 2)) { dataPacket->checksum = checksum16((unsigned char*)dataPacket, sizeof(DataPacket) - 2); }

					// Calculate sum of packet (should be zero)
					sum = sum16(dataPacket, sizeof(DataPacket));
				}

				if (sum != 0x0000) { checksumFail = 1; }
				
				if (checksumFail)
				{
					fprintf(stderr, "[!]");
				}
				else
				{
					unsigned short sampleCount = dataPacket->sampleCount;
					char bps = 0;

					// fprintf(stderr, "[%f V, %d, %d, %f ]\n", dataPacket->battery / 256.0 * 6.0, dataPacket->events, dataPacket->light, dataPacket->temperature * 19.0 / 64.0 - 50.0);
					if (((dataPacket->numAxesBPS >> 4) & 0x0f) != 3) { fprintf(stderr, "[ERROR: num-axes not expected]"); }
					if ((dataPacket->numAxesBPS & 0x0f) == 2) { bps = 6; }
					else if ((dataPacket->numAxesBPS & 0x0f) == 0) { bps = 4; }

					if (bps == 0) { fprintf(stderr, "[ERROR: format not expected]"); }
					else if (dataPacket->sessionId != deviceSessionId) { fprintf(stderr, "[X]"); }
					else if (format == FORMAT_RAW || format == FORMAT_WAV)
					{
						if (bps != 6)
						{
							fprintf(stderr, "[ERROR: RAW/WAV output does not yet support new 32-bit packed accelerometer format]");
						}
						else 
						{
							totalSamples += sampleCount;

							fprintf(stderr, "*");
							if (values == VALUES_FLOAT)
							{
								float divide;
 
								// Ensure float buffer exists
								if (dataPacket->sampleCount > floatBufferSize)
								{
									floatBuffer = (float *)realloc(floatBuffer, 3 * dataPacket->sampleCount * sizeof(float));
									floatBufferSize = dataPacket->sampleCount;
								}

								// Convert to floating point and amplify
								divide = 1.0f;                                          // Original data in (1/256) g
								if (format == FORMAT_RAW) { divide = 256.0f; }          // In 'g'
								else if (format == FORMAT_WAV) { divide = 32768.0f; }   // Range-scaled for .WAV (-1 to 1 range)

								for (i = 0; i < dataPacket->sampleCount; i++)
								{
									for (z = 0; z < 3; z++)
									{
										floatBuffer[3 * i + z] = amplify * (float)dataPacket->sampleData[i].accel[z] / divide;
									}
								}
								outputSize += 3 * sizeof(float) * fwrite(floatBuffer, 3 * sizeof(float), dataPacket->sampleCount, ofp);
							}
							else
							{
								// Amplify integer value in-place (if required)
								if (amplify != 1.0f)
								{
									for (i = 0; i < dataPacket->sampleCount; i++)
									{
										for (z = 0; z < 3; z++)
										{
											float v = amplify * dataPacket->sampleData[i].accel[z];
											if (v < -32768.0f) { v = -32768.0f; }
											if (v >  32767.0f) { v =  32767.0f; }
											dataPacket->sampleData[i].accel[z] = (unsigned short)v;
										}
									}
								}
								outputSize += 3 * sizeof(unsigned short) * fwrite(&(dataPacket->sampleData[0]), 3 * sizeof(unsigned short), dataPacket->sampleCount, ofp);
							}
						}
					}
					else    // (format == FORMAT_CSV)
					{
						float freq;
						float offsetStart;

						// Block start and end time
						struct tm tm0;
						time_t time0;
						double t0, t1;

						fprintf(stderr, "*");
						totalSamples += sampleCount;

						// Calculate block start time
						memset(&tm0, 0, sizeof(tm0));
						tm0.tm_year = 2000 - 1900 + DATETIME_YEAR(dataPacket->timestamp);	// since 1900
						tm0.tm_mon = DATETIME_MONTH(dataPacket->timestamp) - 1;			// 0-11
						tm0.tm_mday = DATETIME_DAY(dataPacket->timestamp);				// 1-31
						tm0.tm_hour = DATETIME_HOURS(dataPacket->timestamp);
						tm0.tm_min = DATETIME_MINUTES(dataPacket->timestamp);
						tm0.tm_sec = DATETIME_SECONDS(dataPacket->timestamp);
						time0 = timegm(&tm0);

						if (ver == 0) 
						{
							// Old format, frequency stored directly
							freq = (float)(unsigned short)(dataPacket->timestampOffset);
							offsetStart = 0.0f;
						}
						else
						{
							// New format
							freq = 3200.0f / (1 << (15 - (dataPacket->sampleRate & 0x0f)));
							if (freq <= 0.0f) { freq = 1.0f; }
							offsetStart = -dataPacket->timestampOffset / freq;
						}
						time0 += (int)floor(offsetStart);			// Fix so time0 takes negative offset into account (for < :00 s boundaries)
						offsetStart -= (float)floor(offsetStart);	// ...and so offsetStart is always positive

						// Start and end of packet
						t0 = (double)time0 + offsetStart;												// Packet start time
						t1 = (double)time0 + offsetStart + (float)dataPacket->sampleCount / freq;		// Packet end time

						// Fix so packet boundary times are always the same (pushes error to last packet, would be better to distribute any error over multiple packets -- would require buffering a few packets)
						if (tLast != 0 && t0 - tLast < 1.0) 
						{ 
							t0 = tLast;
						}
						tLast = t1;

						// Record recording start time
						if (tStart == 0) { tStart = t0; }

						// Mask in any events (will appear at next emitted line)
						events |= dataPacket->events;

						for (i = 0; i < dataPacket->sampleCount; i++)
						{
							if (sequence >= iStart && sequence - iStart < iLength && ((sequence - iStart) % iStep) == 0)
							{
								double t = t0 + (double)i * (t1 - t0) / dataPacket->sampleCount;
								short x, y, z;

								if (time == TIME_SEQUENCE) { sprintf(timestring, "%u", (unsigned int)sequence); }
								else if (time == TIME_SECONDS && ver == 0) { sprintf(timestring, "%0.2f", (double)sequence / freq); }
								else if (time == TIME_SECONDS)
								{
									sprintf(timestring, "%0.4f", t - tStart);
								}
								else if (time == TIME_DAYS && ver == 0) { sprintf(timestring, "%0.11f", (double)sequence / freq / 86400.0); }
								else if (time == TIME_DAYS)
								{
									sprintf(timestring, "%0.12f", (t - tStart) / 86400.0);
								}
								else if (time == TIME_SERIAL)
								{
									sprintf(timestring, "%0.4f", t);
								}
								else if (time == TIME_EXCEL)
								{
									sprintf(timestring, "%0.12f", t / 86400.0 + 25569.0);
								}
								else if (time == TIME_MATLAB)
								{
									sprintf(timestring, "%0.12f", t / 86400.0 + 25569.0 + 693960.0);
								}
								else if (time == TIME_BLOCK || (time == TIME_TIMESTAMP && ver == 0))
								{
									sprintf(timestring, "%04d-%02d-%02d %02d:%02d:%02d", 2000 + DATETIME_YEAR(dataPacket->timestamp), DATETIME_MONTH(dataPacket->timestamp), DATETIME_DAY(dataPacket->timestamp), DATETIME_HOURS(dataPacket->timestamp), DATETIME_MINUTES(dataPacket->timestamp), DATETIME_SECONDS(dataPacket->timestamp)); 
								}
								else if (time == TIME_TIMESTAMP)
								{
									time_t tn = (time_t)t;
									struct tm *tmn = gmtime(&tn);
									float sec = tmn->tm_sec + (float)(t - (time_t)t);
									sprintf(timestring, "%04d-%02d-%02d %02d:%02d:%02d.%03d", 1900 + tmn->tm_year, tmn->tm_mon + 1, tmn->tm_mday, tmn->tm_hour, tmn->tm_min, (int)sec, (int)((sec - (int)sec) * 1000));
								}
								else // if (time == TIME_NONE)
								{
									timestring[0] = '\0';
								}

								if (bps == 4)
								{
									unsigned int *values = (unsigned int *)dataPacket->sampleData;
									unsigned int value = values[i];

									// [byte-3] [byte-2] [byte-1] [byte-0]
									// eezzzzzz zzzzyyyy yyyyyyxx xxxxxxxx
									// 10987654 32109876 54321098 76543210

									// -------- -------- [byte-1] [byte-0]
									// zzzzzzyy yyyyyyyy xxxxxxxx xx000000    << 6
									// ####eezz zzzzzzzz yyyyyyyy yyxxxxxx    >> 4
									// ######## ######ee zzzzzzzz zzyyyyyy    >> 14
									//                   11111111 11000000    & 0xffc0

									//                   ######vv vvvvvvvv    >> 6  (6 - 0)
									//                   #####vvv vvvvvvv0    >> 5  (6 - 1)
									//                   ####vvvv vvvvvv00    >> 4  (6 - 2)
									//                   ###vvvvv vvvvv000    >> 3  (6 - 3)

									x = (signed short)((unsigned short)(value <<  6) & (unsigned short)0xffc0) >> (6 - (unsigned char)(value >> 30));		// Sign-extend 10-bit value, adjust for exponent
									y = (signed short)((unsigned short)(value >>  4) & (unsigned short)0xffc0) >> (6 - (unsigned char)(value >> 30));		// Sign-extend 10-bit value, adjust for exponent
									z = (signed short)((unsigned short)(value >> 14) & (unsigned short)0xffc0) >> (6 - (unsigned char)(value >> 30));		// Sign-extend 10-bit value, adjust for exponent
								}
								else if (bps == 6)
								{
									x = dataPacket->sampleData[i].accelX;
									y = dataPacket->sampleData[i].accelY;
									z = dataPacket->sampleData[i].accelZ;
								}
								else
								{
									x = y = z = 0;
								}

								if (values == VALUES_INT)
								{
                                    if (format == FORMAT_SQLITE)
                                    {
                                        if (sqlite3_bind_double(stmt, 1, (double)(t - 3600.0))) {
                                            fprintf(stderr, "ERROR: sqlite bind double (time).\n");
                                        }
                                        if (sqlite3_bind_int(stmt, 2, (int)(amplify * x))) {
                                            fprintf(stderr, "ERROR: sqlite bind int (x).\n");
                                        }
                                        if (sqlite3_bind_int(stmt, 3, (int)(amplify * y))) {
                                            fprintf(stderr, "ERROR: sqlite bind int (y).\n");
                                        }
                                        if (sqlite3_bind_int(stmt, 4, (int)(amplify * z))) {
                                            fprintf(stderr, "ERROR: sqlite bind int (z).\n");
                                        }
                                        
                                        j = 5;
                            			if (options & OPTIONS_LIGHT)
                            			{
                            				if (sqlite3_bind_int(stmt, j, dataPacket->light))
                            				{
												fprintf(stderr, "ERROR: sqlite bind int (light).\n");
											}
                            				j++;
                            			}
                            			if (options & OPTIONS_TEMP)
                            			{
                            				if (sqlite3_bind_double(stmt, j, (double)(dataPacket->temperature * 19.0 / 64.0 - 50.0)))
                            				{
												fprintf(stderr, "ERROR: sqlite bind double (temperature).\n");
											}
                            				j++;
                            			}
                            			if (options & OPTIONS_BATT) 
                            			{ 
                            				if (sqlite3_bind_double(stmt, j, (double)(dataPacket->battery / 256.0 * 6.0)))
                            				{
												fprintf(stderr, "ERROR: sqlite bind double (battery).\n");
											}
                            			}
                                    }                                        
                                    else
                                    {
									    outputSize += fprintf(ofp, "%s%s%d,%d,%d", (time == TIME_NONE) ? "" : timestring, (time == TIME_NONE) ? "" : ",", (int)(amplify * x), (int)(amplify * y), (int)(amplify * z));
                                    }
								}
								else if (values == VALUES_FLOAT)
								{
									outputSize += fprintf(ofp, "%s%s%f,%f,%f", (time == TIME_NONE) ? "" : timestring, (time == TIME_NONE) ? "" : ",", amplify * x / 256.0f, amplify * y / 256.0f, amplify * z / 256.0f);
								}

                                if (format == FORMAT_SQLITE)
                                {
                                    if (sqlite3_step(stmt) != SQLITE_DONE)
                                    {
                                        fprintf(stderr, "ERROR: Could not step (execute) SQL stmt: %s\n", sqlite3_errmsg(dbconn));
                                        return 1;
                                    }
                                    sqlite3_reset(stmt);
                                }
                                else
                                {
    								if (options & OPTIONS_LIGHT)  { fprintf(ofp, ",%u", dataPacket->light); }
    								if (options & OPTIONS_TEMP)   { fprintf(ofp, ",%u", dataPacket->temperature); }
    								if (options & OPTIONS_BATT)   { fprintf(ofp, ",%u", dataPacket->battery); }
    								if (options & OPTIONS_EVENTS) 
    								{
    									unsigned char e = events;
    									if (checksumFail) { e |= DATA_EVENT_CHECKSUM_FAIL; }
    									fprintf(ofp, ","); 
    									if (e & DATA_EVENT_RESUME)              { fprintf(ofp, "r"); }
    									if (e & DATA_EVENT_SINGLE_TAP)          { fprintf(ofp, "s"); }
    									if (e & DATA_EVENT_DOUBLE_TAP)          { fprintf(ofp, "d"); }
    									if (e & DATA_EVENT_EVENT)               { fprintf(ofp, "e"); }
    									if (e & DATA_EVENT_FIFO_OVERFLOW)       { fprintf(ofp, "F"); }
    									if (e & DATA_EVENT_BUFFER_OVERFLOW)     { fprintf(ofp, "B"); }
    									if (e & DATA_EVENT_UNHANDLED_INTERRUPT) { fprintf(ofp, "I"); }
    									if (e & DATA_EVENT_CHECKSUM_FAIL)       { fprintf(ofp, "X"); }
    									events = 0x00;
    								}
                                    fprintf(ofp, "\n");
                                }
							}

							sequence++;
						}
					}
                }
            }
            else
            {
                fprintf(stderr, "?[%02x]", header);
            }
        }

		// Early out
		if (sequence >= iStart && sequence - iStart >= iLength) { break; }
    }

    // Patch up WAV header with actual number of samples
    if (format == FORMAT_WAV && ofp != stdout)
    {
        fseek(ofp, 4, SEEK_SET);  fputlong(outputSize + 68 - 8, ofp);
        fseek(ofp, 64, SEEK_SET); fputlong(outputSize, ofp);
    }

    if (ofp != stdout)
    { 
        if (format == FORMAT_SQLITE)
        {
            sqlite3_exec(dbconn, "DELETE FROM acc WHERE time < 0", 0, 0, 0);
            sqlite3_exec(dbconn, "COMMIT;", 0, 0, 0);
            sqlite3_exec(dbconn, "END;", 0, 0, 0);
            sqlite3_close(dbconn);
        }
        else
            fclose(ofp); 
    }
    fprintf(stderr, "\r\nWrote %u bytes of data (%u samples).\r\n", (unsigned int)outputSize, (unsigned int)totalSamples);

    if (floatBuffer != NULL) { free(floatBuffer); floatBuffer = NULL; }

    return 0;
}


int main(int argc, char *argv[])
{
    char help = 0;
    char *filename = NULL;
    char *outfilename = NULL;
    Format format = FORMAT_DEFAULT;
    Values values = VALUES_DEFAULT;
    Time time = TIME_DEFAULT;
	Options options = OPTIONS_NONE;
    int positional = 0;
    float amplify = 1.0f;
	unsigned long iStart = 0;
	unsigned long iLength = ULONG_MAX;
	unsigned long iStep = 1;
    int i;
    //Cwa *cwa;
    //Cwa *ocwa;
    //int x;
    
#ifdef _DEBUG
atexit(_getch);
#endif

    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-' || argv[i][0] == '/')
        {
            if (strcasecmp(argv[i], "--help") == 0)
            {
                help = 1; 
            }
            else if (strcasecmp(argv[i], "-f:csv") == 0 || strcasecmp(argv[i], "-csv") == 0) { format = FORMAT_CSV; }
            else if (strcasecmp(argv[i], "-f:raw") == 0 || strcasecmp(argv[i], "-raw") == 0) { format = FORMAT_RAW; }
            else if (strcasecmp(argv[i], "-f:wav") == 0 || strcasecmp(argv[i], "-wav") == 0) { format = FORMAT_WAV; }
            else if (strcasecmp(argv[i], "-f:sqlite") == 0 || strcasecmp(argv[i], "-sqlite") == 0) { format = FORMAT_SQLITE; }
            else if (strcasecmp(argv[i], "-v:int") == 0)       { values = VALUES_INT; }
            else if (strcasecmp(argv[i], "-v:float") == 0)     { values = VALUES_FLOAT; }
            else if (strcasecmp(argv[i], "-t:none") == 0)      { time = TIME_NONE; }
            else if (strcasecmp(argv[i], "-t:sequence") == 0)  { time = TIME_SEQUENCE; }
            else if (strcasecmp(argv[i], "-t:secs") == 0)      { time = TIME_SECONDS; }
            else if (strcasecmp(argv[i], "-t:days") == 0)      { time = TIME_DAYS; }
            else if (strcasecmp(argv[i], "-t:serial") == 0)    { time = TIME_SERIAL; }
            else if (strcasecmp(argv[i], "-t:excel") == 0)     { time = TIME_EXCEL; }
            else if (strcasecmp(argv[i], "-t:matlab") == 0)    { time = TIME_MATLAB; }
            else if (strcasecmp(argv[i], "-t:block") == 0)     { time = TIME_BLOCK; }
            else if (strcasecmp(argv[i], "-t:timestamp") == 0) { time = TIME_TIMESTAMP; }
			else if (strcasecmp(argv[i], "-light") == 0)       { options = (Options)((unsigned int)options | OPTIONS_LIGHT); }
			else if (strcasecmp(argv[i], "-temp") == 0)        { options = (Options)((unsigned int)options | OPTIONS_TEMP); }
			else if (strcasecmp(argv[i], "-batt") == 0)        { options = (Options)((unsigned int)options | OPTIONS_BATT); }
			else if (strcasecmp(argv[i], "-events") == 0)      { options = (Options)((unsigned int)options | OPTIONS_EVENTS); }
            else if (strcasecmp(argv[i], "-start") == 0)       { i++; iStart = atol(argv[i]); }
            else if (strcasecmp(argv[i], "-length") == 0)      { i++; iLength = atol(argv[i]); }
            else if (strcasecmp(argv[i], "-step") == 0 || strcasecmp(argv[i], "-skip") == 0) { i++; iStep = atol(argv[i]); }
            else if (strcasecmp(argv[i], "-out") == 0)
            {
                i++; 
                outfilename = argv[i]; 
                fprintf(stderr, "Output Filename: %s\n", outfilename);
            }
            else if (strcasecmp(argv[i], "-amplify") == 0)
            {
                i++; 
                amplify = (float)atof(argv[i]); 
                fprintf(stderr, "Amplify: %f\n", amplify); 
            }
            else
            {
                fprintf(stderr, "ERROR: Unrecognized parameter: %s\n", argv[i]);
                help = 1;
            }
        }
        else
        {
#ifdef _WIN32
            // On Windows: allow /?, /H, /HELP as parameters
            if (strcasecmp(argv[i], "/?") == 0 || strcasecmp(argv[i], "/H") == 0 || strcasecmp(argv[i], "/HELP") == 0)
            {
                help = 1; 
            }
            else
#endif
            if (positional == 0)
            {
                filename = argv[i];
            }
            else
            {
                fprintf(stderr, "ERROR: Too many positional parameters (%d): %s\n", positional + 1, argv[i]);
                help = 1;
            }
            positional++;
        }
    }
    
    if (filename == NULL)
    {
        fprintf(stderr, "ERROR: File not specified\n");
        help = 1;
    }
    
    if (help)
    {
        fprintf(stderr, "CWA by Daniel Jackson, 2010-2011\n");
        fprintf(stderr, "Usage: CWA <filename.cwa> [-f:csv|-f:raw|-f:wav|-f:sqlite] [-v:float|-v:int] [-t:timestamp|-t:none|-t:sequence|-t:secs|-t:days|-t:serial|-t:excel|-t:matlab|-t:block] [-light] [-temp] [-batt] [-events] [-amplify 1.0] [-start 0] [-length <len>] [-step 1] [-out <outfile>]\n");
        return 1;
    }
    

#ifdef _DEBUG
    fprintf(stderr, "DEBUG: Opening file: %s\n", filename);
#endif

    if (DumpFile(filename, outfilename, format, values, time, options, amplify, iStart, iLength, iStep))
	{
	    fprintf(stderr, "ERROR: Problem dumping file: %s (check exists, readable and not corrupted.)\n", filename);
	}
/*
    cwa = CwaRead(filename);
    if (cwa == NULL)
    {
        fprintf(stderr, "ERROR: Problem reading file: %s\n", filename);
        return 4;
    }

    // Create CWA
    //ocwa = CwaCreate(cwa->samples);
    for (x = 0; x < cwa->samples; x++)
    {
        //CwaSetSample(cwa, x, !CwaGetSample(cwa, x));
    }

    if (outfilename != NULL && strlen(outfilename) > 0)
    {
#ifdef _DEBUG
    fprintf(stderr, "DEBUG: Writing file: %s\n", outfilename);
#endif
        CwaWrite(cwa, outfilename);
    }

    //CwaDelete(ocwa);
    CwaDelete(cwa);
*/

    return 0;
}