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

	// Set OCR2A = 95
	write_io_reg(avr, p->comp[AVR_TIMER_COMPA].r_ocr, 95);

	// Enable Timer2 Compare Match A Interrupt (OCIE2A = 1 in TIMSK2, data address 0x70)
	write_io_reg(avr, 0x70, 1 << p->comp[AVR_TIMER_COMPA].interrupt.enable.bit);

	// Set prescaler 1024 (CS22=1, CS21=1, CS20=1 in TCCR2B, data address 0xb1)
	write_io_reg(avr, 0xb1, 0x07);

	// Enable global interrupts (S_I bit)
	avr->sreg[S_I] = 1;

	// Flash program: SLEEP instruction (0x9588) followed by NOP
	avr->flash[0] = 0x88;
	avr->flash[1] = 0x95;
	avr->flash[2] = 0x00;
	avr->flash[3] = 0x00;
	avr->pc = 0;

	avr_cycle_count_t timer_start_cycle = avr->cycle;

	// Execute SLEEP instruction
	avr_run_one(avr);

	// CPU should now be sleeping
	if (avr->state != cpu_Sleeping) {
		fail("Expected CPU state cpu_Sleeping (%d), got %d", cpu_Sleeping, avr->state);
	}

	// Calculate predicted sleep cycle count via avr_cycle_timer_process
	avr_cycle_count_t sleep_cycles = avr_cycle_timer_process(avr);

	// Run single step (avr_callback_run_raw) when cpu_Sleeping to advance cycles by sleep duration and trigger interrupt
	avr_callback_run_raw(avr);

	avr_cycle_count_t total_elapsed_cycles = avr->cycle - timer_start_cycle;
	double elapsed_seconds = (double)total_elapsed_cycles / avr->frequency;

	printf("Sleep time cycles predicted by timer process: %llu\n", (unsigned long long)sleep_cycles);
	printf("Total elapsed cycles from timer enable to wakeup: %llu\n", (unsigned long long)total_elapsed_cycles);
	printf("Elapsed seconds: %.4f s\n", elapsed_seconds);

	// Timer configured for prescaler 1024 and OCR2A = 95 gives (95 + 1) * 1024 = 98304 cycles = 3.0 seconds
	if (total_elapsed_cycles != 98305) { // 98304 timer cycles + 1 cycle for SLEEP instruction execution = 98305
		fail("Expected total elapsed cycles = 98305 (~3.0s), got %llu", (unsigned long long)total_elapsed_cycles);
	}

	tests_success();
	return 0;
}
