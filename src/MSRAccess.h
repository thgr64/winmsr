#pragma once

#include <stdint.h>
#include <Windows.h>

#define DRIVER_NAME L"\\\\.\\EnergyDriver"
#define IOCTL_READMSR 0x22e00a
#define IOCTL_READMULTIPLEMSRS 0x22e002

HANDLE glMSRDriverInterface;

typedef struct _inp {
	DWORD core, msr;
} MSR;

// Returns the core count of the first group of processors
uint32_t GetCoreCount() {
#ifdef _WIN32
	return GetActiveProcessorCount(0);
#else
	return 0;
#endif
}

// Opens the driver, if installed and necessary rights are granted
bool OpenMSRDriver() {
	glMSRDriverInterface = CreateFileW(DRIVER_NAME, 0xc0000000, 0, (LPSECURITY_ATTRIBUTES)0x0, 3, 0, (HANDLE)0x0);
	if (glMSRDriverInterface == INVALID_HANDLE_VALUE) {
		return false;
	}
	return true;
}

// closes the driver
void CloseMSRDriver() {
	if (glMSRDriverInterface != INVALID_HANDLE_VALUE) {
		CloseHandle(glMSRDriverInterface);
	}
}

// Reads a single MSR, if driver is opened, to read data from core [0..31]
uint64_t readMSR(uint32_t reg, uint8_t core = 0) {
	uint64_t output = 0;
	DWORD length, err;
	MSR input = { 0 };

	input.core = core & 0x1f;
	input.msr = reg;
	if (!DeviceIoControl(glMSRDriverInterface, IOCTL_READMSR, &input, 8, &output, 8, (LPDWORD)&length, (LPOVERLAPPED)0x0)) {	
		err = GetLastError();
		DebugBreak();
	}
	return output;
}

bool readMSRAllCores(uint32_t reg, uint64_t* buf) {
	DWORD length;
	DWORD inp[3] = { 0 };
	inp[1] = reg;
	inp[2] = 32; //probably ignored
	if (!DeviceIoControl(glMSRDriverInterface, IOCTL_READMSR, &inp, 12, &buf, 32*8, (LPDWORD)&length, (LPOVERLAPPED)0x0)) {
		err = GetLastError();
		DebugBreak();
	}
	return true;
}

int readMSRs(uint32_t* list, uint64_t* output, uint32_t count) {
	DWORD length, err=0;

	if (DeviceIoControl(glMSRDriverInterface, IOCTL_READMULTIPLEMSRS, list, (count + 2) * 4, output, (count + 2) * 8, (LPDWORD)&length, (LPOVERLAPPED)0x0)) {
		return err;
	}
	else {
		err = GetLastError();
	}
	return -(int)err;
}

typedef struct _msrquery {
	uint8_t		corecount;
	uint8_t		msrs;
	uint8_t		_result;
	uint8_t		_res;
	uint32_t	_count;
	uint64_t* output;
	uint32_t* _input;
} *MSRQuery;

//Get sample count
uint32_t GetSampleCount(MSRQuery q) {
	return q->_count;
}

//
bool IsSampleValid(MSRQuery q) {
	return q->result == 0 ? true : false;
}

// Returns the time stamp counter, of a successful query
uint64_t GetTSCFromQuery(MSRQuery q) {
	return q->output[1];
}

// Returns the begin of a list of retrieved SMR values starting at core
uint64_t* GetMSRValues(MSRQuery q, uint32_t core = 0) {
	return &q->output[core * q->msrs + 2];
}

// Returns the MSR list of the query
uint32_t* GetMSRRegList(MSRQuery q) {
	return &q->_input[2 + q->corecount];
}

MSRQuery AllocateQuery(uint32_t cores, uint32_t msrs) {
	MSRQuery ret = (MSRQuery)malloc(sizeof(_msrquery));
	if (ret == NULL)
		return ret;
	size_t alloc = (cores + msrs + 2) * sizeof(uint32_t);
	ret->_input = (uint32_t*)malloc(alloc);
	if (ret->_input == NULL) {
		free(ret);
		return NULL;
	}
	memset(ret->_input, 0xdd, alloc);
	alloc = (msrs * cores + 2) * sizeof(uint64_t);
	ret->output = (uint64_t*)malloc(alloc);
	if (ret->output == NULL) {
		free(ret->_input);
		free(ret);
		return NULL;
	}
	memset(ret->output, 0xff, alloc);
	ret->_result = 0xff;
	ret->_resv = 0xff;
	ret->_count = 0;
	return ret;
}
// Clones the query, without the output
MSRQuery CloneQuery(MSRQuery q)
{
	if (q == NULL)
		return NULL;

	MSRQuery ret = AllocateQuery(q->corecount, q->msrs + 1);
	if (ret != NULL) {
		size_t alloc = (q->corecount + q->msrs + 1 + 2) * sizeof(uint32_t);
		memcpy(ret->_input, q->_input, alloc);
		ret->_result = 0xff;
		ret->_resv = 0xff;
		ret->_count = 0;
	}
	return ret;
}

// Frees the memory allocated for a query
void ReleaseQuery(MSRQuery query) {
	if (query != NULL) {
		free(query->_input);
		free(query->output);
		free(query);
	}
}

// Create query with a corelist [0..31], if NULL, a corelist will be created, corecount [0..31]
MSRQuery CreateMSRQuery(uint32_t* corelist, uint8_t corecount, uint32_t* msrlist, uint8_t len) {

	MSRQuery ret = AllocateQuery(corecount, len + 1);

	if (ret != NULL) {
		uint32_t* inp = ret->_input;
		ret->corecount = corecount;
		ret->msrs = len;
		*inp = corecount;
		if (corelist == NULL) {
			for (uint8_t i = 0; i < ret->corecount; i++) {
				inp++;
				*inp = i;
			}
		}
		else {
			for (uint8_t i = 0; i < ret->corecount; i++) {
				inp++;
				*inp = corelist[i];
			}
		}

		inp++;
		*inp = len;
		for (uint8_t i = 0; i < len; i++) {
			inp++;
			*inp = msrlist[i];
		}
		inp++;
		*inp = 0;
	}

	return ret;
}

// Creates a compelx query for all available cores requesting mutiple MSRs in a single call, if possible
MSRQuery CreateMSRQueryAllCores(uint32_t* msrlist, uint8_t len) {

	uint8_t corecount = GetCoreCount();

	return CreateMSRQuery(NULL, corecount, msrlist, len);
}

// Collects data, if the driver has been successfully opened, otherwise it returns false 
bool PerformMSRQuery(MSRQuery q) {
	if (q != NULL) {
		q->_result = (uint8_t)readMSRs(q->_input, q->output, q->corecount * q->msrs);
		if (IsSampleValid(q)) {
			q->_count++;
		}
		else {
			return false;
		}
		return true;
	}
	return false;
}


// Test function, to print output raw
void printRawOutput(MSRQuery q) {

	if (q != NULL) {
		for (int i = 0; i < q->msrs * q->corecount; i++) {
			printf("%p,", (uint64_t*)q->output[i + 2]);
		}
	}
}

// Test function to print query information
void printQuery(MSRQuery q) {
	if (q != NULL) {
		printf("CC: %u MSRs: %u Sample: %u List: ", q->corecount, q->msrs, q->_count);
		for (int i = 0; i < q->msrs; i++) {
			printf("%xh,", q->_input[q->corecount + 2 + i]);
		}
		printf("\n");
	}
}
