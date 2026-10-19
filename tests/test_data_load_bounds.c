#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tests.h"
#include "sim_avr.h"
#include "sim_elf.h"

int main(int argc, char **argv) {
	tests_init(argc, argv);

	avr_t * avr = avr_make_mcu_by_name("atmega324p");
	if (!avr) {
		fail("Failed to create atmega324p MCU");
	}
	avr_init(avr);

	if (!avr->data) {
		fail("avr->data is NULL after avr_init");
	}

	uint16_t ramstart = avr->ioend + 1;
	uint16_t ramsize = avr->ramend - avr->ioend; // RAMSIZE = ramend - ioend

	// Mark I/O register right before RAMSTART with a sentinel value
	avr->data[avr->ioend] = 0xAA;

	// Construct an elf_firmware_t with a DATA chunk filling all of RAM
	elf_firmware_t fw = {0};
	fw_chunk_t *chunk = calloc(1, sizeof(fw_chunk_t) + ramsize);
	chunk->type = DATA;
	chunk->addr = 0; // Relative to AVR_SEGMENT_OFFSET_DATA in firmware
	chunk->size = ramsize;
	chunk->fill_size = ramsize;
	memset(chunk->data, 0x55, ramsize);
	chunk->data[0] = 0x11;
	chunk->data[ramsize - 1] = 0x99;

	fw.chunks = chunk;

#if ELF_SYMBOLS
	// Test symbols in data space to verify data_names and symbol loading logic
	avr_symbol_t *sym = malloc(sizeof(avr_symbol_t) + 16);
	sym->addr = AVR_SEGMENT_OFFSET_DATA + ramstart;
	sym->size = 2;
	strcpy((char *)sym->symbol, "test_sym");

	fw.symbol = malloc(sizeof(avr_symbol_t *));
	fw.symbol[0] = sym;
	fw.symbolcount = 1;
	fw.highest_data_symbol = ramstart + 1;
#endif

	avr_load_firmware(avr, &fw);

	// Verify avr->data pointer is still non-NULL
	if (!avr->data) {
		fail("avr->data became NULL after avr_load_firmware");
	}

	// Verify ioend was not overwritten by the DATA chunk
	if (avr->data[avr->ioend] != 0xAA) {
		fail("DATA chunk overwrote avr->data[ioend] (expected 0xAA, got 0x%02x)",
		     avr->data[avr->ioend]);
	}

	// Verify RAMSTART received the first byte
	if (avr->data[ramstart] != 0x11) {
		fail("RAMSTART byte incorrect (expected 0x11, got 0x%02x)",
		     avr->data[ramstart]);
	}

	// Verify RAMEND received the last byte
	if (avr->data[avr->ramend] != 0x99) {
		fail("RAMEND byte incorrect (expected 0x99, got 0x%02x)",
		     avr->data[avr->ramend]);
	}

	tests_success();
	return 0;
}
