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

	// Enable Timer0 Compare Match A Interrupt (OCIE0A = 1 in TIMSK0, data address 0x6e)
	// and set OCR0A = 10, prescaler = 1 (CS00=1 in TCCR0B, data address 0x45)
	write_io_reg(avr, 0x47, 10);   // OCR0A = 10
	write_io_reg(avr, 0x6e, 1 << 1); // OCIE0A = 1
	write_io_reg(avr, 0x45, 0x01); // TCCR0B = 1 (prescaler 1)

	// Set SMCR register (data address 0x53) to Power-save mode (SE=1, SM=011 => 0x07)
	write_io_reg(avr, 0x53, 0x07);

	// Flash program: SLEEP instruction (0x9588) followed by NOP
	avr->flash[0] = 0x88;
	avr->flash[1] = 0x95;
	avr->flash[2] = 0x00;
	avr->flash[3] = 0x00;

	// Find Timer0
	avr_timer_t * p0 = NULL;
	for (avr_io_t * port = avr->io_port; port; port = port->next) {
		if (port->kind && strcmp(port->kind, "timer") == 0) {
			avr_timer_t * t = (avr_timer_t*)port;
			if (t->name == '0') {
				p0 = t;
				break;
			}
		}
	}

	// Put RETI opcode (0x9518) at Timer2 and Timer0 CompA vector addresses
	uint32_t vector_addr = p->comp[AVR_TIMER_COMPA].interrupt.vector * avr->vector_size;
	avr->flash[vector_addr] = 0x18;
	avr->flash[vector_addr + 1] = 0x95;

	if (p0) {
		uint32_t v0 = p0->comp[AVR_TIMER_COMPA].interrupt.vector * avr->vector_size;
		avr->flash[v0] = 0x18;
		avr->flash[v0 + 1] = 0x95;
	}

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

	// Run simulation while cpu_Sleeping until an allowed interrupt wakes the CPU
	while (avr->state == cpu_Sleeping) {
		avr_callback_run_raw(avr);
	}

	avr_cycle_count_t total_elapsed_cycles = avr->cycle - timer_start_cycle;
	double elapsed_seconds = (double)total_elapsed_cycles / avr->frequency;

	printf("Sleep time cycles predicted by timer process: %llu\n", (unsigned long long)sleep_cycles);
	printf("Total elapsed cycles from timer enable to wakeup: %llu\n", (unsigned long long)total_elapsed_cycles);
	printf("Elapsed seconds: %.4f s\n", elapsed_seconds);

	// Timer2 configured for prescaler 1024 and OCR2A = 95 gives (95 + 1) * 1024 = 98304 cycles = 3.0 seconds
	// Timer0 should have fired many times during this interval without waking the CPU from Power-save mode.
	if (total_elapsed_cycles != 98305) { // 98304 timer cycles + 1 cycle for SLEEP instruction execution = 98305
		fail("Expected total elapsed cycles = 98305 (~3.0s), got %llu", (unsigned long long)total_elapsed_cycles);
	}

	// Run simulation while awake to allow any pending Timer0 ISR to be serviced and cleared
	avr->sreg[S_I] = 1;
	while (avr_has_pending_interrupts(avr)) {
		avr_callback_run_raw(avr);
	}

	// Test repeated sleep/wake cycle
	avr->sreg[S_I] = 1;
	avr->pc = 0;
	avr_run_one(avr); // SLEEP
	if (avr->state != cpu_Sleeping) {
		fail("Repeated sleep: Expected CPU state cpu_Sleeping (%d), got %d", cpu_Sleeping, avr->state);
	}
	while (avr->state == cpu_Sleeping) {
		avr_callback_run_raw(avr); // Wake on Timer2
	}
	if (avr->state != cpu_Running) {
		fail("Repeated wake: Expected CPU state cpu_Running (%d), got %d", cpu_Running, avr->state);
	}

	tests_success();
	return 0;
}
