#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tests.h"
#include "sim_avr.h"
#include "sim_elf.h"

int main(int argc, char **argv) {
	tests_init(argc, argv);

	const char *paths[] = {
		"examples/board_sh1106/firmware_no_mmcu.elf",
		"../examples/board_sh1106/firmware_no_mmcu.elf",
		"../../examples/board_sh1106/firmware_no_mmcu.elf",
		NULL
	};
	const char *path = NULL;
	for (int i = 0; paths[i]; i++) {
		FILE *f = fopen(paths[i], "r");
		if (f) {
			fclose(f);
			path = paths[i];
			break;
		}
	}
	if (!path) {
		fail("Could not find firmware_no_mmcu.elf");
	}

	elf_firmware_t fw = {0};
	int res = elf_read_firmware(path, &fw);
	if (res != 0) {
		fail("elf_read_firmware failed for firmware_no_mmcu.elf (returned %d)", res);
	}

	if (strcmp(fw.mmcu, "atmega32u4") != 0) {
		fail("Expected MCU name 'atmega32u4', got '%s'", fw.mmcu);
	}

	if (!fw.chunks) {
		fail("firmware.chunks is NULL after reading firmware_no_mmcu.elf");
	}

	tests_success();
	return 0;
}
