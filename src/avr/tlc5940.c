/*
 * tlc5940.c
 *
 * Ensure that a) all SPI and TLC5940 pins are configured as outputs
 * and b) BLANK is HIGH and other output pins of TLC5940 are LOW after
 * init. This module doesn't do any configuration of pins.
 *
 *  Created on: 12.6.2012
 *      Author: Icchan
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include "tlc5940.h"
#include "pinMacros.h"
#include "init.h"
#include "main.h"
#include "../cube.h"

register uint8_t layer_bytes_left asm ("r4");
register uint8_t *send_ptr asm ("r2");
volatile uint8_t may_flip = 0;

#define NL "\n\t"

/* SPI transmit interrupt vector SPIF is cleared when entering this
 * interrupt vector. This is called when byte transmission is
 * finished.
 */
#ifdef ASM_ISRS
// Eliminating use of push and pop by using "cache register"
register uint16_t isr_z_cache asm ("r6");
ISR(SPI_STC_vect, ISR_NAKED)
{
	asm volatile(
		/* "Cache" SREG to r6, using LSB of isr_z_cache */
		NL "in r6,__SREG__"
		/* Jump if --layer_bytes_left == 0 */
		NL "dec r4"
		NL "breq spi_stc_last_byte"
		/* No instruction except DEC modifys SREG here. So, return
		   SREG from "cache" register r6 */
		NL "out __SREG__,r6"
		/* Store Z to isr_z_cache (r7:r6) */
		NL "movw r6,r30"
		/* Load send_ptr (r3:r2) to Z register (r31:r30) */
		NL "movw r30,r2"
		/* Load byte pointed by Z while incrementing
		   Z. Reusing LSB of send_ptr (r2) */
		NL "ld r2,Z+"
		/* Write byte to SPI bus (SPDR) */
		NL "out 0x2e,r2"
		/* Put incremented send_ptr back to its global
		 * register */
		NL "movw r2,r30"
		/* Restore Z from isr_z_cache */
		NL "movw r30,r6"
		NL "reti"
		/* When it is last byte, restore SREG and bail out */
		NL "spi_stc_last_byte:"
		NL "out __SREG__,r6"
		NL "reti"
		);
}
#else
// This looks simpler but is much slower (41 cycles compared to 15)
ISR(SPI_STC_vect)
{
	if(--layer_bytes_left) SPDR = *send_ptr++;
}
#endif

/*
 * BLANK timer interrupt Timer0
 * Interrupt if TCNT0 = OCR0A
 */
ISR(TIMER0_COMPA_vect)
{
	static uint8_t layer = 0x01;

	// Main screen turn off
	pin_high(BLANK);

	// "Activate" previously uploaded data
	pin_high(XLAT);
	pin_low(XLAT);

	// Send first byte
	SPDR = ~layer;

	// Main screen turn on and start PWM timers on TLC5940
	pin_low(BLANK);

	if (layer != 0x80) {
		// Advance layer
		layer <<= 1;
	} else {
		// Prepare drawing first layer
		layer=0x01;
		
		// If we have new buffer, flip to it
		if (may_flip) {
			gs_buf_swap();
			may_flip = 0;
		}

		// Roll send_ptr back to start of buffer
		send_ptr = gs_buf_front;
	}

	// Set up byte counter for SPI interrupt
	layer_bytes_left = BYTES_PER_LAYER + 1;
}
