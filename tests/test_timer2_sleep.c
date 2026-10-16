#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tests.h"
#include "sim_avr.h"
#include "sim_core.h"
#include "avr_timer.h"

static void write_io_reg(avr_t *avr, avr_io_addr_t addr, uint8_t v) {
	avr_io_addr_t io = AVR_DATA_TO_IO(addr);
	if (avr->io[io].w.c) {
		avr->io[io].w.c(avr, addr, v, avr->io[io].w.param);
	} else {
		avr_core_watch_write(avr, addr, v);
	}
}

int main(int argc, char **argv) {
	tests_init(argc, argv);

	avr_t * avr = avr_make_mcu_by_name("atmega328");
	if (!avr) {
		fail("Failed to create atmega328 MCU");
	}
	avr->frequency = 32768;
	avr_init(avr);

	avr_timer_t * p = NULL;
	for (avr_io_t * port = avr->io_port; port; port = port->next) {
		if (port->kind && strcmp(port->kind, "timer") == 0) {
			avr_timer_t * t = (avr_timer_t*)port;
			if (t->name == '2') {
				p = t;
				break;
			}
		}
	}

	if (!p) fail("Timer2 not found on MCU");

	// Enable AS2 mode on Timer2 (32kHz watch crystal)
	write_io_reg(avr, p->as2.reg, 1 << p->as2.bit);

	// Set prescaler 1 (CS20 = 1 in TCCR2B, data address 0xb1)
	write_io_reg(avr, 0xb1, 0x01);

	// Enable Timer2 Overflow Interrupt ONLY (TOIE2 = 1 in TIMSK2, data address 0x70)
	// Compare match interrupts (OCIE2A, OCIE2B) are left disabled (0)
	write_io_reg(avr, 0x70, 0x01);

	// Enable global interrupts (S_I bit)
	avr->sreg[S_I] = 1;

	// Execute SLEEP instruction (opcode 0x9588)
	avr->flash[0] = 0x88;
	avr->flash[1] = 0x95;
	avr->pc = 0;

	avr_run_one(avr);

	// CPU should now be sleeping
	if (avr->state != cpu_Sleeping) {
		fail("Expected CPU state cpu_Sleeping (%d), got %d", cpu_Sleeping, avr->state);
	}

	// Before overflow occurs, no interrupt is pending yet
	if (avr_has_pending_interrupts(avr)) {
		fail("avr_has_pending_interrupts returned non-zero before overflow");
	}

	// Calculate predicted sleep cycle count
	avr_cycle_count_t sleep_cycles = avr_cycle_timer_process(avr);

	// Since compare interrupts are disabled and OCR is 0, sleep_cycles should equal
	// p->tov_cycles (255 or 256 cycles until TOV2), NOT 1 cycle!
	if (sleep_cycles < 255) {
		fail("avr_cycle_timer_process returned premature sleep count %llu (expected >= 255)",
			(unsigned long long)sleep_cycles);
	}

	tests_success();
	return 0;
}
