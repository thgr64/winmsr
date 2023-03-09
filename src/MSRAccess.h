#pragma once

#include <stdint.h>
#include <Windows.h>

HANDLE glDriverInterface;

typedef struct _inp {
	DWORD core, msr;
} MSR;

bool OpenMSRDriver() {
	glDriverInterface = CreateFileW(L"\\\\.\\EnergyDriver", 0xc0000000, 0, (LPSECURITY_ATTRIBUTES)0x0, 3, 0, (HANDLE)0x0);
	if (glDriverInterface == INVALID_HANDLE_VALUE) {
		return false;
	}
	return true;
}

void CloseMSRDriver() {
	if (glDriverInterface != INVALID_HANDLE_VALUE) {
		CloseHandle(glDriverInterface);
	}
}

uint64_t readMSR(uint32_t reg, uint64_t core = 1) {
	uint64_t output = 0;
	DWORD length, err;
	MSR input;

	input.core = core;
	input.msr = reg;
	if (DeviceIoControl(glDriverInterface, 0x22e00a, &input, 8, &output, 8, (LPDWORD)&length, (LPOVERLAPPED)0x0)) {
		return output;
	}
	else {
		err = GetLastError();
	}
	return 0;
}