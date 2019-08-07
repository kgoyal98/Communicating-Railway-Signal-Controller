/* 
 * Copyright (C) 2012-2014 Chris McClelland
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <makestuff.h>
#include <libfpgalink.h>
#include <libbuffer.h>
#include <limits.h>
#include <liberror.h>
#include <libdump.h>
#include <argtable2.h>
#include <readline/readline.h>
#include <readline/history.h>
#ifdef WIN32
#include <Windows.h>
#else
#include <sys/time.h>
#endif
#include <unistd.h>


bool sigIsRaised(void);
void sigRegisterHandler(void);

static const char *ptr;
static bool enableBenchmarking = false;
bool nthbit( long, long);
long encrypter( long, long);
long decrypter( long, long);
void decToHexa32(unsigned long, char*);
void send_data(long , char*);
void send_chan(long , char*, int);
void read_chan(char*, char*, int);
void editfile(int,int,char*,long);
long Key=15;
long Kack1=274,Kack2=274;


static bool isHexDigit(char ch) {
	return
		(ch >= '0' && ch <= '9') ||
		(ch >= 'a' && ch <= 'f') ||
		(ch >= 'A' && ch <= 'F');
}

static uint16 calcChecksum(const uint8 *data, size_t length) {
	uint16 cksum = 0x0000;
	while ( length-- ) {
		cksum = (uint16)(cksum + *data++);
	}
	return cksum;
}

static bool getHexNibble(char hexDigit, uint8 *nibble) {
	if ( hexDigit >= '0' && hexDigit <= '9' ) {
		*nibble = (uint8)(hexDigit - '0');
		return false;
	} else if ( hexDigit >= 'a' && hexDigit <= 'f' ) {
		*nibble = (uint8)(hexDigit - 'a' + 10);
		return false;
	} else if ( hexDigit >= 'A' && hexDigit <= 'F' ) {
		*nibble = (uint8)(hexDigit - 'A' + 10);
		return false;
	} else {
		return true;
	}
}

static int getHexByte(uint8 *byte) {
	uint8 upperNibble;
	uint8 lowerNibble;
	if ( !getHexNibble(ptr[0], &upperNibble) && !getHexNibble(ptr[1], &lowerNibble) ) {
		*byte = (uint8)((upperNibble << 4) | lowerNibble);
		byte += 2;
		return 0;
	} else {
		return 1;
	}
}

static const char *const errMessages[] = {
	NULL,
	NULL,
	"Unparseable hex number",
	"Channel out of range",
	"Conduit out of range",
	"Illegal character",
	"Unterminated string",
	"No memory",
	"Empty string",
	"Odd number of digits",
	"Cannot load file",
	"Cannot save file",
	"Bad arguments"
};

typedef enum {
	FLP_SUCCESS,
	FLP_LIBERR,
	FLP_BAD_HEX,
	FLP_CHAN_RANGE,
	FLP_CONDUIT_RANGE,
	FLP_ILL_CHAR,
	FLP_UNTERM_STRING,
	FLP_NO_MEMORY,
	FLP_EMPTY_STRING,
	FLP_ODD_DIGITS,
	FLP_CANNOT_LOAD,
	FLP_CANNOT_SAVE,
	FLP_ARGS
} ReturnCode;

static ReturnCode doRead(
	struct FLContext *handle, uint8 chan, uint32 length, FILE *destFile, uint16 *checksum,
	const char **error, long tim)
{
	ReturnCode retVal = FLP_SUCCESS;
	uint32 bytesWritten;
	FLStatus fStatus;
	uint32 chunkSize;
	const uint8 *recvData;
	uint32 actualLength;
	const uint8 *ptr;
	uint16 csVal = 0x0000;
	#define READ_MAX 65536

	// Read first chunk
	chunkSize = length >= READ_MAX ? READ_MAX : length;
	fStatus = flReadChannelAsyncSubmit(handle, chan, chunkSize, NULL, error, tim);
	CHECK_STATUS(fStatus, FLP_LIBERR, cleanup, "doRead()");
	length = length - chunkSize;

	while ( length ) {
		// Read chunk N
		chunkSize = length >= READ_MAX ? READ_MAX : length;
		fStatus = flReadChannelAsyncSubmit(handle, chan, chunkSize, NULL, error, tim);
		CHECK_STATUS(fStatus, FLP_LIBERR, cleanup, "doRead()");
		length = length - chunkSize;
		
		// Await chunk N-1
		fStatus = flReadChannelAsyncAwait(handle, &recvData, &actualLength, &actualLength, error, tim);
		CHECK_STATUS(fStatus, FLP_LIBERR, cleanup, "doRead()");

		// Write chunk N-1 to file
		bytesWritten = (uint32)fwrite(recvData, 1, actualLength, destFile);
		CHECK_STATUS(bytesWritten != actualLength, FLP_CANNOT_SAVE, cleanup, "doRead()");

		// Checksum chunk N-1
		chunkSize = actualLength;
		ptr = recvData;
		while ( chunkSize-- ) {
			csVal = (uint16)(csVal + *ptr++);
		}
	}

	// Await last chunk
	fStatus = flReadChannelAsyncAwait(handle, &recvData, &actualLength, &actualLength, error, tim);
	CHECK_STATUS(fStatus, FLP_LIBERR, cleanup, "doRead()");
	
	// Write last chunk to file
	bytesWritten = (uint32)fwrite(recvData, 1, actualLength, destFile);
	CHECK_STATUS(bytesWritten != actualLength, FLP_CANNOT_SAVE, cleanup, "doRead()");

	// Checksum last chunk
	chunkSize = actualLength;
	ptr = recvData;
	while ( chunkSize-- ) {
		csVal = (uint16)(csVal + *ptr++);
	}
	
	// Return checksum to caller
	*checksum = csVal;
cleanup:
	return retVal;
}

static ReturnCode doWrite(
	struct FLContext *handle, uint8 chan, FILE *srcFile, size_t *length, uint16 *checksum,
	const char **error)
{
	ReturnCode retVal = FLP_SUCCESS;
	size_t bytesRead, i;
	FLStatus fStatus;
	const uint8 *ptr;
	uint16 csVal = 0x0000;
	size_t lenVal = 0;
	#define WRITE_MAX (65536 - 5)
	uint8 buffer[WRITE_MAX];

	do {
		// Read Nth chunk
		bytesRead = fread(buffer, 1, WRITE_MAX, srcFile);
		if ( bytesRead ) {
			// Update running total
			lenVal = lenVal + bytesRead;

			// Submit Nth chunk
			fStatus = flWriteChannelAsync(handle, chan, bytesRead, buffer, error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup, "doWrite()");

			// Checksum Nth chunk
			i = bytesRead;
			ptr = buffer;
			while ( i-- ) {
				csVal = (uint16)(csVal + *ptr++);
			}
		}
	} while ( bytesRead == WRITE_MAX );

	// Wait for writes to be received. This is optional, but it's only fair if we're benchmarking to
	// actually wait for the work to be completed.
	fStatus = flAwaitAsyncWrites(handle, error);
	CHECK_STATUS(fStatus, FLP_LIBERR, cleanup, "doWrite()");

	// Return checksum & length to caller
	*checksum = csVal;
	*length = lenVal;
cleanup:
	return retVal;
}

static int parseLine(struct FLContext *handle, const char *line, const char **error, long tim) {
	ReturnCode retVal = FLP_SUCCESS, status;
	FLStatus fStatus;
	struct Buffer dataFromFPGA = {0,};
	BufferStatus bStatus;
	uint8 *data = NULL;
	char *fileName = NULL;
	FILE *file = NULL;
	double totalTime, speed;
	#ifdef WIN32
		LARGE_INTEGER tvStart, tvEnd, freq;
		DWORD_PTR mask = 1;
		SetThreadAffinityMask(GetCurrentThread(), mask);
		QueryPerformanceFrequency(&freq);
	#else
		struct timeval tvStart, tvEnd;
		long long startTime, endTime;
	#endif
	bStatus = bufInitialise(&dataFromFPGA, 1024, 0x00, error);
	CHECK_STATUS(bStatus, FLP_LIBERR, cleanup);
	ptr = line;
	do {
		while ( *ptr == ';' ) {
			ptr++;
		}
		switch ( *ptr ) {
		case 'r':{
			uint32 chan;
			uint32 length = 1;
			char *end;
			ptr++;
			
			// Get the channel to be read:
			errno = 0;
			chan = (uint32)strtoul(ptr, &end, 16);
			CHECK_STATUS(errno, FLP_BAD_HEX, cleanup);

			// Ensure that it's 0-127
			CHECK_STATUS(chan > 127, FLP_CHAN_RANGE, cleanup);
			ptr = end;

			// Only three valid chars at this point:
			CHECK_STATUS(*ptr != '\0' && *ptr != ';' && *ptr != ' ', FLP_ILL_CHAR, cleanup);

			if ( *ptr == ' ' ) {
				ptr++;

				// Get the read count:
				errno = 0;
				length = (uint32)strtoul(ptr, &end, 16);
				CHECK_STATUS(errno, FLP_BAD_HEX, cleanup);
				ptr = end;
				
				// Only three valid chars at this point:
				CHECK_STATUS(*ptr != '\0' && *ptr != ';' && *ptr != ' ', FLP_ILL_CHAR, cleanup);
				if ( *ptr == ' ' ) {
					const char *p;
					const char quoteChar = *++ptr;
					CHECK_STATUS(
						(quoteChar != '"' && quoteChar != '\''),
						FLP_ILL_CHAR, cleanup);
					
					// Get the file to write bytes to:
					ptr++;
					p = ptr;
					while ( *p != quoteChar && *p != '\0' ) {
						p++;
					}
					CHECK_STATUS(*p == '\0', FLP_UNTERM_STRING, cleanup);
					fileName = malloc((size_t)(p - ptr + 1));
					CHECK_STATUS(!fileName, FLP_NO_MEMORY, cleanup);
					CHECK_STATUS(p - ptr == 0, FLP_EMPTY_STRING, cleanup);
					strncpy(fileName, ptr, (size_t)(p - ptr));
					fileName[p - ptr] = '\0';
					ptr = p + 1;
				}
			}
			if ( fileName ) {
				uint16 checksum = 0x0000;

				// Open file for writing
				file = fopen(fileName, "wb");
				CHECK_STATUS(!file, FLP_CANNOT_SAVE, cleanup);
				free(fileName);
				fileName = NULL;

				#ifdef WIN32
					QueryPerformanceCounter(&tvStart);
					status = doRead(handle, (uint8)chan, length, file, &checksum, error, tim);
					QueryPerformanceCounter(&tvEnd);
					totalTime = (double)(tvEnd.QuadPart - tvStart.QuadPart);
					totalTime /= freq.QuadPart;
					speed = (double)length / (1024*1024*totalTime);
				#else
					gettimeofday(&tvStart, NULL);
					status = doRead(handle, (uint8)chan, length, file, &checksum, error, tim);
					gettimeofday(&tvEnd, NULL);
					startTime = tvStart.tv_sec;
					startTime *= 1000000;
					startTime += tvStart.tv_usec;
					endTime = tvEnd.tv_sec;
					endTime *= 1000000;
					endTime += tvEnd.tv_usec;
					totalTime = (double)(endTime - startTime);
					totalTime /= 1000000;  // convert from uS to S.
					speed = (double)length / (1024*1024*totalTime);
				#endif
				if ( enableBenchmarking ) {
					printf(
						"Read %d bytes (checksum 0x%04X) from channel %d at %f MiB/s\n",
						length, checksum, chan, speed);
				}
				CHECK_STATUS(status, status, cleanup);

				// Close the file
				fclose(file);
				file = NULL;
			} else {
				size_t oldLength = dataFromFPGA.length;
				bStatus = bufAppendConst(&dataFromFPGA, 0x00, length, error);
				CHECK_STATUS(bStatus, FLP_LIBERR, cleanup);
				#ifdef WIN32
					QueryPerformanceCounter(&tvStart);
					fStatus = flReadChannel(handle, (uint8)chan, length, dataFromFPGA.data + oldLength, error, tim);
					QueryPerformanceCounter(&tvEnd);
					totalTime = (double)(tvEnd.QuadPart - tvStart.QuadPart);
					totalTime /= freq.QuadPart;
					speed = (double)length / (1024*1024*totalTime);
				#else
					gettimeofday(&tvStart, NULL);
					fStatus = flReadChannel(handle, (uint8)chan, length, dataFromFPGA.data + oldLength, error, tim);
					gettimeofday(&tvEnd, NULL);
					startTime = tvStart.tv_sec;
					startTime *= 1000000;
					startTime += tvStart.tv_usec;
					endTime = tvEnd.tv_sec;
					endTime *= 1000000;
					endTime += tvEnd.tv_usec;
					totalTime = (double)(endTime - startTime);
					totalTime /= 1000000;  // convert from uS to S.
					speed = (double)length / (1024*1024*totalTime);
				#endif
				if ( enableBenchmarking ) {
					printf(
						"Read %d bytes (checksum 0x%04X) from channel %d at %f MiB/s\n",
						length, calcChecksum(dataFromFPGA.data + oldLength, length), chan, speed);
				}
				CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			}
			break;
		}
		case 'w':{
			unsigned long int chan;
			size_t length = 1, i;
			char *end, ch;
			const char *p;
			ptr++;
			
			// Get the channel to be written:
			errno = 0;
			chan = strtoul(ptr, &end, 16);
			CHECK_STATUS(errno, FLP_BAD_HEX, cleanup);

			// Ensure that it's 0-127
			CHECK_STATUS(chan > 127, FLP_CHAN_RANGE, cleanup);
			ptr = end;

			// There must be a space now:
			CHECK_STATUS(*ptr != ' ', FLP_ILL_CHAR, cleanup);

			// Now either a quote or a hex digit
		   ch = *++ptr;
			if ( ch == '"' || ch == '\'' ) {
				uint16 checksum = 0x0000;

				// Get the file to read bytes from:
				ptr++;
				p = ptr;
				while ( *p != ch && *p != '\0' ) {
					p++;
				}
				CHECK_STATUS(*p == '\0', FLP_UNTERM_STRING, cleanup);
				fileName = malloc((size_t)(p - ptr + 1));
				CHECK_STATUS(!fileName, FLP_NO_MEMORY, cleanup);
				CHECK_STATUS(p - ptr == 0, FLP_EMPTY_STRING, cleanup);
				strncpy(fileName, ptr, (size_t)(p - ptr));
				fileName[p - ptr] = '\0';
				ptr = p + 1;  // skip over closing quote

				// Open file for reading
				file = fopen(fileName, "rb");
				CHECK_STATUS(!file, FLP_CANNOT_LOAD, cleanup);
				free(fileName);
				fileName = NULL;
				
				#ifdef WIN32
					QueryPerformanceCounter(&tvStart);
					status = doWrite(handle, (uint8)chan, file, &length, &checksum, error);
					QueryPerformanceCounter(&tvEnd);
					totalTime = (double)(tvEnd.QuadPart - tvStart.QuadPart);
					totalTime /= freq.QuadPart;
					speed = (double)length / (1024*1024*totalTime);
				#else
					gettimeofday(&tvStart, NULL);
					status = doWrite(handle, (uint8)chan, file, &length, &checksum, error);
					gettimeofday(&tvEnd, NULL);
					startTime = tvStart.tv_sec;
					startTime *= 1000000;
					startTime += tvStart.tv_usec;
					endTime = tvEnd.tv_sec;
					endTime *= 1000000;
					endTime += tvEnd.tv_usec;
					totalTime = (double)(endTime - startTime);
					totalTime /= 1000000;  // convert from uS to S.
					speed = (double)length / (1024*1024*totalTime);
				#endif
				if ( enableBenchmarking ) {
					printf(
						"Wrote "PFSZD" bytes (checksum 0x%04X) to channel %lu at %f MiB/s\n",
						length, checksum, chan, speed);
				}
				CHECK_STATUS(status, status, cleanup);

				// Close the file
				fclose(file);
				file = NULL;
			} else if ( isHexDigit(ch) ) {
				// Read a sequence of hex bytes to write
				uint8 *dataPtr;
				p = ptr + 1;
				while ( isHexDigit(*p) ) {
					p++;
				}
				CHECK_STATUS((p - ptr) & 1, FLP_ODD_DIGITS, cleanup);
				length = (size_t)(p - ptr) / 2;
				data = malloc(length);
				dataPtr = data;
				for ( i = 0; i < length; i++ ) {
					getHexByte(dataPtr++);
					ptr += 2;
				}
				#ifdef WIN32
					QueryPerformanceCounter(&tvStart);
					fStatus = flWriteChannel(handle, (uint8)chan, length, data, error);
					QueryPerformanceCounter(&tvEnd);
					totalTime = (double)(tvEnd.QuadPart - tvStart.QuadPart);
					totalTime /= freq.QuadPart;
					speed = (double)length / (1024*1024*totalTime);
				#else
					gettimeofday(&tvStart, NULL);
					fStatus = flWriteChannel(handle, (uint8)chan, length, data, error);
					gettimeofday(&tvEnd, NULL);
					startTime = tvStart.tv_sec;
					startTime *= 1000000;
					startTime += tvStart.tv_usec;
					endTime = tvEnd.tv_sec;
					endTime *= 1000000;
					endTime += tvEnd.tv_usec;
					totalTime = (double)(endTime - startTime);
					totalTime /= 1000000;  // convert from uS to S.
					speed = (double)length / (1024*1024*totalTime);
				#endif
				if ( enableBenchmarking ) {
					printf(
						"Wrote "PFSZD" bytes (checksum 0x%04X) to channel %lu at %f MiB/s\n",
						length, calcChecksum(data, length), chan, speed);
				}
				CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
				free(data);
				data = NULL;
			} else {
				FAIL(FLP_ILL_CHAR, cleanup);
			}
			break;
		}
		case '+':{
			uint32 conduit;
			char *end;
			ptr++;

			// Get the conduit
			errno = 0;
			conduit = (uint32)strtoul(ptr, &end, 16);
			CHECK_STATUS(errno, FLP_BAD_HEX, cleanup);

			// Ensure that it's 0-127
			CHECK_STATUS(conduit > 255, FLP_CONDUIT_RANGE, cleanup);
			ptr = end;

			// Only two valid chars at this point:
			CHECK_STATUS(*ptr != '\0' && *ptr != ';', FLP_ILL_CHAR, cleanup);

			fStatus = flSelectConduit(handle, (uint8)conduit, error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			break;
		}
		default:
			FAIL(FLP_ILL_CHAR, cleanup);
		}
	} while ( *ptr == ';' );
	CHECK_STATUS(*ptr != '\0', FLP_ILL_CHAR, cleanup);

	dump(0x00000000, dataFromFPGA.data, dataFromFPGA.length);

cleanup:
	bufDestroy(&dataFromFPGA);
	if ( file ) {
		fclose(file);
	}
	free(fileName);
	free(data);
	if ( retVal > FLP_LIBERR ) {
		const int column = (int)(ptr - line);
		int i;
		fprintf(stderr, "%s at column %d\n  %s\n  ", errMessages[retVal], column, line);
		for ( i = 0; i < column; i++ ) {
			fprintf(stderr, " ");
		}
		fprintf(stderr, "^\n");
	}
	return retVal;
}

static const char *nibbles[] = {
	"0000",  // '0'
	"0001",  // '1'
	"0010",  // '2'
	"0011",  // '3'
	"0100",  // '4'
	"0101",  // '5'
	"0110",  // '6'
	"0111",  // '7'
	"1000",  // '8'
	"1001",  // '9'

	"XXXX",  // ':'
	"XXXX",  // ';'
	"XXXX",  // '<'
	"XXXX",  // '='
	"XXXX",  // '>'
	"XXXX",  // '?'
	"XXXX",  // '@'

	"1010",  // 'A'
	"1011",  // 'B'
	"1100",  // 'C'
	"1101",  // 'D'
	"1110",  // 'E'
	"1111"   // 'F'
};

int main(int argc, char *argv[]) {
	ReturnCode retVal = FLP_SUCCESS, pStatus;
	struct arg_str *ivpOpt = arg_str0("i", "ivp", "<VID:PID>", "            vendor ID and product ID (e.g 04B4:8613)");
	struct arg_str *vpOpt = arg_str1("v", "vp", "<VID:PID[:DID]>", "       VID, PID and opt. dev ID (e.g 1D50:602B:0001)");
	struct arg_str *fwOpt = arg_str0("f", "fw", "<firmware.hex>", "        firmware to RAM-load (or use std fw)");
	struct arg_str *portOpt = arg_str0("d", "ports", "<bitCfg[,bitCfg]*>", " read/write digital ports (e.g B13+,C1-,B2?)");
	struct arg_str *queryOpt = arg_str0("q", "query", "<jtagBits>", "         query the JTAG chain");
	struct arg_str *progOpt = arg_str0("p", "program", "<config>", "         program a device");
	struct arg_uint *conOpt = arg_uint0("c", "conduit", "<conduit>", "        which comm conduit to choose (default 0x01)");
	struct arg_str *railOpt = arg_str0("e", "multiplesteps", "<actionString>", "    ##########");
	struct arg_str *actOpt = arg_str0("a", "action", "<actionString>", "    a series of CommFPGA actions");
	struct arg_lit *shellOpt  = arg_lit0("s", "shell", "                    start up an interactive CommFPGA session");
	struct arg_lit *benOpt  = arg_lit0("b", "benchmark", "                enable benchmarking & checksumming");
	struct arg_lit *rstOpt  = arg_lit0("r", "reset", "                    reset the bulk endpoints");
	struct arg_str *dumpOpt = arg_str0("l", "dumploop", "<ch:file.bin>", "   write data from channel ch to file");
	struct arg_lit *helpOpt  = arg_lit0("h", "help", "                     print this help and exit");
	struct arg_str *eepromOpt  = arg_str0(NULL, "eeprom", "<std|fw.hex|fw.iic>", "   write firmware to FX2's EEPROM (!!)");
	struct arg_str *backupOpt  = arg_str0(NULL, "backup", "<kbitSize:fw.iic>", "     backup FX2's EEPROM (e.g 128:fw.iic)\n");
	struct arg_end *endOpt   = arg_end(20);
	void *argTable[] = {
		ivpOpt, vpOpt, fwOpt, portOpt, queryOpt, progOpt, conOpt, railOpt, actOpt,
		shellOpt, benOpt, rstOpt, dumpOpt, helpOpt, eepromOpt, backupOpt, endOpt
	};
	const char *progName = "flcli";
	int numErrors;
	struct FLContext *handle = NULL;
	FLStatus fStatus;
	const char *error = NULL;
	const char *ivp = NULL;
	const char *vp = NULL;
	bool isNeroCapable, isCommCapable;
	uint32 numDevices, scanChain[16], i;
	const char *line = NULL;
	uint8 conduit = 0x01;

	if ( arg_nullcheck(argTable) != 0 ) {
		fprintf(stderr, "%s: insufficient memory\n", progName);
		FAIL(1, cleanup);
	}

	numErrors = arg_parse(argc, argv, argTable);

	if ( helpOpt->count > 0 ) {
		printf("FPGALink Command-Line Interface Copyright (C) 2012-2014 Chris McClelland\n\nUsage: %s", progName);
		arg_print_syntax(stdout, argTable, "\n");
		printf("\nInteract with an FPGALink device.\n\n");
		arg_print_glossary(stdout, argTable,"  %-10s %s\n");
		FAIL(FLP_SUCCESS, cleanup);
	}

	if ( numErrors > 0 ) {
		arg_print_errors(stdout, endOpt, progName);
		fprintf(stderr, "Try '%s --help' for more information.\n", progName);
		FAIL(FLP_ARGS, cleanup);
	}

	fStatus = flInitialise(0, &error);
	CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);

	vp = vpOpt->sval[0];

	printf("Attempting to open connection to FPGALink device %s...\n", vp);
	fStatus = flOpen(vp, &handle, NULL);
	if ( fStatus ) {
		if ( ivpOpt->count ) {
			int count = 60;
			uint8 flag;
			ivp = ivpOpt->sval[0];
			printf("Loading firmware into %s...\n", ivp);
			if ( fwOpt->count ) {
				fStatus = flLoadCustomFirmware(ivp, fwOpt->sval[0], &error);
			} else {
				fStatus = flLoadStandardFirmware(ivp, vp, &error);
			}
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			
			printf("Awaiting renumeration");
			flSleep(1000);
			do {
				printf(".");
				fflush(stdout);
				fStatus = flIsDeviceAvailable(vp, &flag, &error);
				CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
				flSleep(250);
				count--;
			} while ( !flag && count );
			printf("\n");
			if ( !flag ) {
				fprintf(stderr, "FPGALink device did not renumerate properly as %s\n", vp);
				FAIL(FLP_LIBERR, cleanup);
			}

			printf("Attempting to open connection to FPGLink device %s again...\n", vp);
			fStatus = flOpen(vp, &handle, &error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
		} else {
			fprintf(stderr, "Could not open FPGALink device at %s and no initial VID:PID was supplied\n", vp);
			FAIL(FLP_ARGS, cleanup);
		}
	}

	printf(
		"Connected to FPGALink device %s (firmwareID: 0x%04X, firmwareVersion: 0x%08X)\n",
		vp, flGetFirmwareID(handle), flGetFirmwareVersion(handle)
	);

	if ( eepromOpt->count ) {
		if ( !strcmp("std", eepromOpt->sval[0]) ) {
			printf("Writing the standard FPGALink firmware to the FX2's EEPROM...\n");
			fStatus = flFlashStandardFirmware(handle, vp, &error);
		} else {
			printf("Writing custom FPGALink firmware from %s to the FX2's EEPROM...\n", eepromOpt->sval[0]);
			fStatus = flFlashCustomFirmware(handle, eepromOpt->sval[0], &error);
		}
		CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
	}

	if ( backupOpt->count ) {
		const char *fileName;
		const uint32 kbitSize = strtoul(backupOpt->sval[0], (char**)&fileName, 0);
		if ( *fileName != ':' ) {
			fprintf(stderr, "%s: invalid argument to option --backup=<kbitSize:fw.iic>\n", progName);
			FAIL(FLP_ARGS, cleanup);
		}
		fileName++;
		printf("Saving a backup of %d kbit from the FX2's EEPROM to %s...\n", kbitSize, fileName);
		fStatus = flSaveFirmware(handle, kbitSize, fileName, &error);
		CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
	}

	if ( rstOpt->count ) {
		// Reset the bulk endpoints (only needed in some virtualised environments)
		fStatus = flResetToggle(handle, &error);
		CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
	}

	if ( conOpt->count ) {
		conduit = (uint8)conOpt->ival[0];
	}

	isNeroCapable = flIsNeroCapable(handle);
	isCommCapable = flIsCommCapable(handle, conduit);

	if ( portOpt->count ) {
		uint32 readState;
		char hex[9];
		const uint8 *p = (const uint8 *)hex;
		printf("Configuring ports...\n");
		fStatus = flMultiBitPortAccess(handle, portOpt->sval[0], &readState, &error);
		CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
		sprintf(hex, "%08X", readState);
		printf("Readback:   28   24   20   16    12    8    4    0\n          %s", nibbles[*p++ - '0']);
		printf(" %s", nibbles[*p++ - '0']);
		printf(" %s", nibbles[*p++ - '0']);
		printf(" %s", nibbles[*p++ - '0']);
		printf("  %s", nibbles[*p++ - '0']);
		printf(" %s", nibbles[*p++ - '0']);
		printf(" %s", nibbles[*p++ - '0']);
		printf(" %s\n", nibbles[*p++ - '0']);
		flSleep(100);
	}

	if ( queryOpt->count ) {
		if ( isNeroCapable ) {
			fStatus = flSelectConduit(handle, 0x00, &error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			fStatus = jtagScanChain(handle, queryOpt->sval[0], &numDevices, scanChain, 16, &error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			if ( numDevices ) {
				printf("The FPGALink device at %s scanned its JTAG chain, yielding:\n", vp);
				for ( i = 0; i < numDevices; i++ ) {
					printf("  0x%08X\n", scanChain[i]);
				}
			} else {
				printf("The FPGALink device at %s scanned its JTAG chain but did not find any attached devices\n", vp);
			}
		} else {
			fprintf(stderr, "JTAG chain scan requested but FPGALink device at %s does not support NeroProg\n", vp);
			FAIL(FLP_ARGS, cleanup);
		}
	}

	if ( progOpt->count ) {
		printf("Programming device...\n");
		if ( isNeroCapable ) {
			fStatus = flSelectConduit(handle, 0x00, &error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			fStatus = flProgram(handle, progOpt->sval[0], NULL, &error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
		} else {
			fprintf(stderr, "Program operation requested but device at %s does not support NeroProg\n", vp);
			FAIL(FLP_ARGS, cleanup);
		}
	}

	if ( benOpt->count ) {
		enableBenchmarking = true;
	}

	if ( railOpt->count ) {
		///////////////////////
		int chan=0,DONE=0,n=63,X=0,Y=0,checkstart=0;
		while(true) {
			if(DONE==1){
				n=chan;
			}
			for(int i=chan;i<=n;i=(i+1)%64){
				if(checkstart==0){
					printf("Receiving coordinates from fpga %s...\n", vp);
					if ( isCommCapable ) {
						uint8 isRunning;
						fStatus = flSelectConduit(handle, conduit, &error);
						CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
						fStatus = flIsFPGARunning(handle, &isRunning, &error);
						CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
						if ( isRunning ) {
							char str1[40]=" ";
							// printf("%s...\n", str1);
							read_chan("coordinate_enc.dat", str1, 2*i);
							// printf("%s\n", str1);
							pStatus = parseLine(handle, str1, &error, UINT_MAX);
							CHECK_STATUS(pStatus, pStatus, cleanup);
						} else {
							fprintf(stderr, "The FPGALink device at %s is not ready to talk - did you forget --program?\n", vp);
							FAIL(FLP_ARGS, cleanup);
						}
					} else {
						fprintf(stderr, "Action requested but device at %s does not support CommFPGA\n", vp);
						FAIL(FLP_ARGS, cleanup);
					}
				}
				else{
					checkstart=0;
				}
				// printf("Sending coordinates to fpga %s...\n", vp);
				// int a,b,c,d,e;
				FILE *fptr1;
				// int X=0,Y=0,Track_OK[8],Track_exists[8],Next_Signal[8];
				fptr1 = fopen("coordinate_enc.dat","r");
				char ch;
				long inp=0;
				printf("Encrypted input: ");
				while((ch=fgetc(fptr1))!=EOF) {
					int x;
					if ((int)ch < 0){
						x = (int)ch + 256;
					}
					else{
						x = (int)ch;
					}
					inp = inp * 16 * 16 + x;
					printf("%X",x);
				}
				printf("\n");
				fclose(fptr1);
				long decrypted_inp=decrypter(inp,Key);
				Y=decrypted_inp%16;
				X=decrypted_inp/16;
				printf("Coordinates Received: %X,%X\n",X,Y);
				//////////////////////////////Send back coordinates
				printf("Sending coordinates to fpga %s...\n", vp);
				if ( isCommCapable ) {
					uint8 isRunning;
					fStatus = flSelectConduit(handle, conduit, &error);
					CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
					fStatus = flIsFPGARunning(handle, &isRunning, &error);
					CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
					if ( isRunning ) {
						char str1[40]=" ";
						send_chan(X*16+Y,str1,2*i+1);
						// printf("%s\n",str1);
						pStatus = parseLine(handle, str1, &error, UINT_MAX);
						CHECK_STATUS(pStatus, pStatus, cleanup);
					} else {
						fprintf(stderr, "The FPGALink device at %s is not ready to talk - did you forget --program?\n", vp);
						FAIL(FLP_ARGS, cleanup);
					}
				} else {
					fprintf(stderr, "Action requested but device at %s does not support CommFPGA\n", vp);
					FAIL(FLP_ARGS, cleanup);
				}
						
				//////////////////////////////receive ACK1
				int qwerty=0;
				for(;qwerty<2;qwerty++){
					printf("Receiving ACK1 %s...\n", vp);
					if ( isCommCapable ) {
						uint8 isRunning;
						fStatus = flSelectConduit(handle, conduit, &error);
						CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
						fStatus = flIsFPGARunning(handle, &isRunning, &error);
						CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
						if ( isRunning ) {
							char str1[40]=" ";
							read_chan("ACK.dat",str1,2*i);
							pStatus = parseLine(handle, str1, &error, UINT_MAX);
							CHECK_STATUS(pStatus, pStatus, cleanup);
						} else {
							fprintf(stderr, "The FPGALink device at %s is not ready to talk - did you forget --program?\n", vp);
							FAIL(FLP_ARGS, cleanup);
						}
					} else {
						fprintf(stderr, "Action requested but device at %s does not support CommFPGA\n", vp);
						FAIL(FLP_ARGS, cleanup);
					}
					//////////////////////////////////
					FILE *fptrAck;
					// int X=0,Y=0,Track_OK[8],Track_exists[8],Next_Signal[8];
					fptrAck = fopen("ACK.dat","r");
					inp=0;
					printf("Encrypted Ack: ");
					while((ch=fgetc(fptrAck))!=EOF) {
						int x;
						if ((int)ch < 0){
							x = (int)ch + 256;
						}
						else{
							x = (int)ch;
						}
						inp = inp * 16 * 16 + x;
						printf("%X",x);
					}
					printf("\n");
					fclose(fptrAck);
					decrypted_inp=decrypter(inp,Key);
					// printf("dec%ld\n", decrypted_inp);
					if(decrypted_inp == Kack1){
						// printf("1\n");
						DONE=1;
						chan=i;
						break;
					}
					else{
						sleep(5);
					}
				}
				if(qwerty==2){
					continue;
				}
				if(DONE==1){
					break;
				}

			}
			//////////////////////////////////send ACK2
			printf("Sending ACK2 %s...\n", vp);
			if ( isCommCapable ) {
				uint8 isRunning;
				fStatus = flSelectConduit(handle, conduit, &error);
				CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
				fStatus = flIsFPGARunning(handle, &isRunning, &error);
				CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
				if ( isRunning ) {
					char str1[40]=" ";
					send_chan(Kack2,str1,2*chan+1);
					pStatus = parseLine(handle, str1, &error, UINT_MAX);
					CHECK_STATUS(pStatus, pStatus, cleanup);
				} else {
					fprintf(stderr, "The FPGALink device at %s is not ready to talk - did you forget --program?\n", vp);
					FAIL(FLP_ARGS, cleanup);
				}
			} else {
				fprintf(stderr, "Action requested but device at %s does not support CommFPGA\n", vp);
				FAIL(FLP_ARGS, cleanup);
			}
			///////////////////////////////////Start sending data
			int a,b,c,d,e,Track_OK[8],Track_exists[8],Next_Signal[8];
			for(int i=0;i<8;i++){
				Track_OK[i]=0;
				Track_exists[i]=0;
				Next_Signal[i]=0;
			}
			FILE *fptr;
			if ((fptr = fopen("track_data.csv","r")) == NULL){
				printf("Error! opening file\n");
			}
			while(fscanf(fptr,"%d,%d,%d,%d,%d", &a,&b,&c,&d,&e)!=EOF){
				if(a==X && b==Y){
					Track_exists[c]=1;
					Next_Signal[c]=e;
					Track_OK[c]=d;
				}
			}
			fclose(fptr);
			long out[8];
			for(int i=0;i<8;i++){
				out[i]=0;
				out[i]+=i*32+ Track_exists[i]*16+Track_OK[i]*8+Next_Signal[i];
			}
			long alpha=0,beta=0;
			for(int i=0;i<4;i++){
				alpha=alpha*256+out[i];
				beta=beta*256+out[4+i];
			}
			///////////////////////////////send first 4 bytes
			printf("Sending first 4 bytes %s...\n", vp);
			if ( isCommCapable ) {
				uint8 isRunning;
				fStatus = flSelectConduit(handle, conduit, &error);
				CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
				fStatus = flIsFPGARunning(handle, &isRunning, &error);
				CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
				if ( isRunning ) {
					char str1[40]=" ";
					send_chan(alpha,str1,2*chan+1);
					pStatus = parseLine(handle, str1, &error, UINT_MAX);
					CHECK_STATUS(pStatus, pStatus, cleanup);
				} else {
					fprintf(stderr, "The FPGALink device at %s is not ready to talk - did you forget --program?\n", vp);
					FAIL(FLP_ARGS, cleanup);
				}
			} else {
				fprintf(stderr, "Action requested but device at %s does not support CommFPGA\n", vp);
				FAIL(FLP_ARGS, cleanup);
			}
			//////////////////////////////////receive ACK
			int qwerty=0;
			for(;qwerty<256;qwerty++){
				printf("Reading ACK %s...\n", vp);
				if ( isCommCapable ) {
					uint8 isRunning;
					fStatus = flSelectConduit(handle, conduit, &error);
					CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
					fStatus = flIsFPGARunning(handle, &isRunning, &error);
					CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
					if ( isRunning ) {
						char str1[40]=" ";
						read_chan("ACK.dat",str1,2*chan);
						pStatus = parseLine(handle, str1, &error, UINT_MAX);
						CHECK_STATUS(pStatus, pStatus, cleanup);
					} else {
						fprintf(stderr, "The FPGALink device at %s is not ready to talk - did you forget --program?\n", vp);
						FAIL(FLP_ARGS, cleanup);
					}
				} else {
					fprintf(stderr, "Action requested but device at %s does not support CommFPGA\n", vp);
					FAIL(FLP_ARGS, cleanup);
				}
				FILE *fptrAckAgain;
				fptrAckAgain = fopen("ACK.dat","r");
				long inp=0;
				char ch;
				printf("Encrypted Ack: ");
				while((ch=fgetc(fptrAckAgain))!=EOF) {
					int x;
					if ((int)ch < 0){
						x = (int)ch + 256;
					}
					else{
						x = (int)ch;
					}
					inp = inp * 16 * 16 + x;
					printf("%X",x);
				}
				printf("\n");
				fclose(fptrAckAgain);
				int decrypted_inp=decrypter(inp,Key);
				if(decrypted_inp == Kack1){
					break;
				}
				else{
					sleep(1);
				}
			}
			if(qwerty==256){
				continue;
			}
			///////////////////////////////////send next 4 bytes
			printf("Sending next 4 bytes %s...\n", vp);
			if ( isCommCapable ) {
				uint8 isRunning;
				fStatus = flSelectConduit(handle, conduit, &error);
				CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
				fStatus = flIsFPGARunning(handle, &isRunning, &error);
				CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
				if ( isRunning ) {
					char str1[40]=" ";
					send_chan(beta,str1,2*chan+1);
					pStatus = parseLine(handle, str1, &error, UINT_MAX);
					CHECK_STATUS(pStatus, pStatus, cleanup);
				} else {
					fprintf(stderr, "The FPGALink device at %s is not ready to talk - did you forget --program?\n", vp);
					FAIL(FLP_ARGS, cleanup);
				}
			} else {
				fprintf(stderr, "Action requested but device at %s does not support CommFPGA\n", vp);
				FAIL(FLP_ARGS, cleanup);
			}
			////////////////////////////////////receive next ACK
			qwerty=0;
			for(;qwerty<256;qwerty++){
				printf("Reading ACK %s...\n", vp);
				if ( isCommCapable ) {
					uint8 isRunning;
					fStatus = flSelectConduit(handle, conduit, &error);
					CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
					fStatus = flIsFPGARunning(handle, &isRunning, &error);
					CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
					if ( isRunning ) {
						char str1[40]=" ";
						read_chan("ACK.dat",str1,2*chan);
						pStatus = parseLine(handle, str1, &error, UINT_MAX);
						CHECK_STATUS(pStatus, pStatus, cleanup);
					} else {
						fprintf(stderr, "The FPGALink device at %s is not ready to talk - did you forget --program?\n", vp);
						FAIL(FLP_ARGS, cleanup);
					}
				} else {
					fprintf(stderr, "Action requested but device at %s does not support CommFPGA\n", vp);
					FAIL(FLP_ARGS, cleanup);
				}
				FILE *fptrAckAgain2;
				fptrAckAgain2 = fopen("ACK.dat","r");
				long inp=0;
				char ch;
				printf("Encrypted Ack: ");
				while((ch=fgetc(fptrAckAgain2))!=EOF) {
					int x;
					if ((int)ch < 0){
						x = (int)ch + 256;
					}
					else{
						x = (int)ch;
					}
					inp = inp * 16 * 16 + x;
					printf("%X",x);
				}
				printf("\n");
				fclose(fptrAckAgain2);
				int decrypted_inp=decrypter(inp,Key);
				// printf("%d\n", decrypted_inp);
				if(decrypted_inp == Kack1){
					break;
				}
				else{
					sleep(1);
				}
			}
			if(qwerty==256)
				continue;
			////////////////////////////////////send final ACK
			printf("Sending ACK %s...\n", vp);
			if ( isCommCapable ) {
				uint8 isRunning;
				fStatus = flSelectConduit(handle, conduit, &error);
				CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
				fStatus = flIsFPGARunning(handle, &isRunning, &error);
				CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
				if ( isRunning ) {
					char str1[40] = " ";
					send_chan(Kack2,str1,2*chan+1);
					pStatus = parseLine(handle, str1, &error, UINT_MAX);
					CHECK_STATUS(pStatus, pStatus, cleanup);
				} else {
					fprintf(stderr, "The FPGALink device at %s is not ready to talk - did you forget --program?\n", vp);
					FAIL(FLP_ARGS, cleanup);
				}
			} else {
				fprintf(stderr, "Action requested but device at %s does not support CommFPGA\n", vp);
				FAIL(FLP_ARGS, cleanup);
			}
			///////////////////////////////////Try getting new data
			sleep(24);
			printf("Trying to get data %s...\n", vp);
			if ( isCommCapable ) {
				uint8 isRunning;
				fStatus = flSelectConduit(handle, conduit, &error);
					CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
				fStatus = flIsFPGARunning(handle, &isRunning, &error);
				CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
				if ( isRunning ) {
					char str1[40]=" ";
					read_chan("Update.dat",str1,2*chan);
					// printf("%s\n",str1 );
					pStatus = parseLine(handle, str1, &error, UINT_MAX);
					CHECK_STATUS(pStatus, pStatus, cleanup);
				} else {
					fprintf(stderr, "The FPGALink device at %s is not ready to talk - did you forget --program?\n", vp);
					FAIL(FLP_ARGS, cleanup);
				}
			} else {
				fprintf(stderr, "Action requested but device at %s does not support CommFPGA\n", vp);
				FAIL(FLP_ARGS, cleanup);
			}
			FILE *fptrUp;
			fptrUp = fopen("Update.dat", "r");
			long inp = 0;
			char ch;
			while ((ch = fgetc(fptrUp)) != EOF) {
				// printf("%d\n",(int)ch );
				int x;
				if ((int)ch < 0){
					x = (int)ch + 256;
				}
				else{
					x = (int)ch;
				}
				inp = inp * 16 * 16 + x;
			}
			fclose(fptrUp);
			// printf("%ld\n", inp);
			long decrypted_inp = decrypter(inp, Key), special1=1, special2=0;
			// printf("%ldnew data\n",decrypted_inp);
			if (decrypted_inp/256 == special1) {
				/////////////////////// if got new data
				printf("Data received : %X\n", (unsigned int)decrypted_inp%256);
				editfile(X,Y,"track_data.csv", decrypted_inp%256);
				checkstart = 0;
				/////////////////done
			}
			else if (decrypted_inp/256 == special2) {
				///////////////////////////if got coordinates again, go back.......
				FILE *fptrAckAgain2;
				FILE *fptrUp;
				fptrUp=fopen("Update.dat","r");
				fptrAckAgain2 = fopen("coordinate_enc.dat", "w");
				while ((ch = fgetc(fptrUp)) != EOF) {
					fputc(ch,fptrAckAgain2);
				}
				checkstart = 1;
				fclose(fptrAckAgain2);
				fclose(fptrUp);
				/////////////////////////go back to top...........
			}
			else{
				checkstart=0;
			}
		}
	}
	
	if ( actOpt->count ) {	
		printf("Executing CommFPGA actions on FPGALink device %s...\n", vp);
		if ( isCommCapable ) {
			uint8 isRunning;
			fStatus = flSelectConduit(handle, conduit, &error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			fStatus = flIsFPGARunning(handle, &isRunning, &error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			if ( isRunning ) {
				pStatus = parseLine(handle, actOpt->sval[0], &error, UINT_MAX);
				CHECK_STATUS(pStatus, pStatus, cleanup);
			} else {
				fprintf(stderr, "The FPGALink device at %s is not ready to talk - did you forget --program?\n", vp);
				FAIL(FLP_ARGS, cleanup);
			}
		} else {
			fprintf(stderr, "Action requested but device at %s does not support CommFPGA\n", vp);
			FAIL(FLP_ARGS, cleanup);
		}
	}

	if ( dumpOpt->count ) {
		const char *fileName;
		unsigned long chan = strtoul(dumpOpt->sval[0], (char**)&fileName, 10);
		FILE *file = NULL;
		const uint8 *recvData;
		uint32 actualLength;
		if ( *fileName != ':' ) {
			fprintf(stderr, "%s: invalid argument to option -l|--dumploop=<ch:file.bin>\n", progName);
			FAIL(FLP_ARGS, cleanup);
		}
		fileName++;
		printf("Copying from channel %lu to %s", chan, fileName);
		file = fopen(fileName, "wb");
		CHECK_STATUS(!file, FLP_CANNOT_SAVE, cleanup);
		sigRegisterHandler();
		fStatus = flSelectConduit(handle, conduit, &error);
		CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
		fStatus = flReadChannelAsyncSubmit(handle, (uint8)chan, 22528, NULL, &error, UINT_MAX);
		CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
		do {
			fStatus = flReadChannelAsyncSubmit(handle, (uint8)chan, 22528, NULL, &error, UINT_MAX);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			fStatus = flReadChannelAsyncAwait(handle, &recvData, &actualLength, &actualLength, &error, UINT_MAX);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			fwrite(recvData, 1, actualLength, file);
			printf(".");
		} while ( !sigIsRaised() );
		printf("\nCaught SIGINT, quitting...\n");
		fStatus = flReadChannelAsyncAwait(handle, &recvData, &actualLength, &actualLength, &error, UINT_MAX);
		CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
		fwrite(recvData, 1, actualLength, file);
		fclose(file);
	}

	if ( shellOpt->count ) {
		printf("\nEntering CommFPGA command-line mode:\n");
		if ( isCommCapable ) {
		   uint8 isRunning;
			fStatus = flSelectConduit(handle, conduit, &error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			fStatus = flIsFPGARunning(handle, &isRunning, &error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			if ( isRunning ) {
				do {
					do {
						line = readline("> ");
					} while ( line && !line[0] );
					if ( line && line[0] && line[0] != 'q' ) {
						add_history(line);
						pStatus = parseLine(handle, line, &error, UINT_MAX);
						CHECK_STATUS(pStatus, pStatus, cleanup);
						free((void*)line);
					}
				} while ( line && line[0] != 'q' );
			} else {
				fprintf(stderr, "The FPGALink device at %s is not ready to talk - did you forget --xsvf?\n", vp);
				FAIL(FLP_ARGS, cleanup);
			}
		} else {
			fprintf(stderr, "Shell requested but device at %s does not support CommFPGA\n", vp);
			FAIL(FLP_ARGS, cleanup);
		}
	}

cleanup:
	free((void*)line);
	flClose(handle);
	if ( error ) {
		fprintf(stderr, "%s\n", error);
		flFreeError(error);
	}
	return retVal;
}

void decToHexa32(unsigned long n, char* hexaDeciNum){
    int i = 7;
    while(i>=0)
    {   
        int temp;
        temp = n % 16;
        if(temp < 10){
        	hexaDeciNum[i] = (char)(temp + 48);
            i = i - 1;
        }
        else{
            hexaDeciNum[i] = temp + 87;
            i = i-1;
        }
        n = n/16;
    }
}
void send_chan(long data, char* write, int channel){
	data=encrypter(data,Key);
	char ch1[2], ch2[3];
	ch1[1]='\0';
	ch2[2]='\0';
	char chan[8];
	decToHexa32(channel,chan);
	ch1[0]=chan[7];
	char temp[]="00000000", wrs[]="w";
	char g[]=" ";
	char *wr = malloc(strlen(wrs)+strlen(ch2)+strlen(ch1)+strlen(g)+strlen(temp)+1);
	strcpy(wr, wrs);
	decToHexa32(data, temp);
	if(channel>=16){
		ch2[0]=chan[6];
		ch2[1]=chan[7];
		strcat(wr,ch2);
	}
	else{
		strcat(wr,ch1);
	}
	strcat(wr,g);
	strcat(wr,temp);
	strcpy(write,wr);
	free(wr);
}
void read_chan(char* fileName, char* write, int channel){
	char ch1[2], ch2[3];
	ch1[1]='\0';
	ch2[2]='\0';
	char chan[8];
	decToHexa32(channel,chan);
	ch1[0]=chan[7];
	char temp[]="\"", wrs[]="r";
	char g[]=" 4 ";
	char *wr = malloc(strlen(wrs)+strlen(ch2)+strlen(ch1)+strlen(g)+strlen(temp)+strlen(fileName)+strlen(temp)+1);
	strcpy(wr, wrs);
	if(channel>=16){
		ch2[0]=chan[6];
		ch2[1]=chan[7];
		strcat(wr,ch2);
	}
	else{
		strcat(wr,ch1);
	}
	strcat(wr,g);
	strcat(wr,temp);
	strcat(wr,fileName);
	strcat(wr,temp);
	strcpy(write,wr);
	free(wr);
}
void send_data(long data, char* write){
	data=encrypter(data,Key);
	char temp[]="00000000", wr[]="w1 00000000";
	strcpy(write,wr);
	decToHexa32(data, temp);
	for(int i=0;i<8;i++){
		write[3+i]=temp[i];
	}
}
long decrypter( long C, long K){
	long P,T;
	P=C;
	bool T3=nthbit(K,31)^nthbit(K,27)^nthbit(K,23)^nthbit(K,19)^nthbit(K,15)^nthbit(K,11)^nthbit(K,7)^nthbit(K,3);
	bool T2=nthbit(K,30)^nthbit(K,26)^nthbit(K,22)^nthbit(K,18)^nthbit(K,14)^nthbit(K,10)^nthbit(K,6)^nthbit(K,2);
	bool T1=nthbit(K,29)^nthbit(K,25)^nthbit(K,21)^nthbit(K,17)^nthbit(K,13)^nthbit(K,9)^nthbit(K,5)^nthbit(K,1);
	bool T0=nthbit(K,28)^nthbit(K,24)^nthbit(K,20)^nthbit(K,16)^nthbit(K,12)^nthbit(K,8)^nthbit(K,4)^nthbit(K,0);
	T=(T3?(1<<3):0)+(T2?(1<<2):0)+(T1?(1<<1):0)+(T0?(1<<0):0);
	T=(T+15)%16;
	for(long i=0;i<32;i+=1){
		if(!nthbit(K,i)){
			long Tnew=(T<<28)+(T<<24)+(T<<20)+(T<<16)+(T<<12)+(T<<8)+(T<<4)+T;
			T=(T+15)%16;
			P=P^Tnew;
		}
	}
	return P;
}
long encrypter( long P, long K){
	long C,T;
	C=P;
	bool T3=nthbit(K,31)^nthbit(K,27)^nthbit(K,23)^nthbit(K,19)^nthbit(K,15)^nthbit(K,11)^nthbit(K,7)^nthbit(K,3);
	bool T2=nthbit(K,30)^nthbit(K,26)^nthbit(K,22)^nthbit(K,18)^nthbit(K,14)^nthbit(K,10)^nthbit(K,6)^nthbit(K,2);
	bool T1=nthbit(K,29)^nthbit(K,25)^nthbit(K,21)^nthbit(K,17)^nthbit(K,13)^nthbit(K,9)^nthbit(K,5)^nthbit(K,1);
	bool T0=nthbit(K,28)^nthbit(K,24)^nthbit(K,20)^nthbit(K,16)^nthbit(K,12)^nthbit(K,8)^nthbit(K,4)^nthbit(K,0);
	T=(T3?(1<<3):0)+(T2?(1<<2):0)+(T1?(1<<1):0)+(T0?(1<<0):0);
	for(long i=0;i<32;i+=1){
		if(nthbit(K,i)){
			long Tnew=(T<<28)+(T<<24)+(T<<20)+(T<<16)+(T<<12)+(T<<8)+(T<<4)+T;
			T=(T+1)%16;
			C=C^Tnew;
		}
	}
	return C;
}
bool nthbit( long n, long k){
	return (n & 1<<k);
}
void editfile(int X,int Y,char* fileName, long S){
	FILE *fptr, *fptr1;
	fptr=fopen(fileName,"r");
	fptr1=fopen("temp.csv","w");
	int a,b,c,d,e,dir=S/32,t_exists=nthbit(S,4);
	while(fscanf(fptr,"%d,%d,%d,%d,%d", &a,&b,&c,&d,&e)!=EOF){
		if(!(a==X && b==Y && c==dir)){
			fprintf(fptr1, "%d,%d,%d,%d,%d\n",a,b,c,d,e);
		}
	}
	if(t_exists==1){
		fprintf(fptr1, "%d,%d,%d,%d,%d\n",X,Y,dir,nthbit(S,3),(int)(S%8));
	}
	fclose(fptr);
	fclose(fptr1);
	fptr1=fopen(fileName,"w");
	fptr=fopen("temp.csv","r");
	while(fscanf(fptr,"%d,%d,%d,%d,%d", &a,&b,&c,&d,&e)!=EOF){
		fprintf(fptr1, "%d,%d,%d,%d,%d\n",a,b,c,d,e);
	}
	fclose(fptr);
	fclose(fptr1);
}