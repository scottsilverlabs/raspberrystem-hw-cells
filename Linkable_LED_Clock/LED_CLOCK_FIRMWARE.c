/*
 * LEDCLOCK.c
 *
 * Created: 9/26/2014 10:00:20 AM
 *  Author: Joseph
 */ 

#define F_CPU 8000000
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdlib.h>

#define DELAY 1

volatile unsigned char * ptr1;
volatile unsigned char * ptr2;
volatile unsigned char * tmp;
volatile unsigned char count = 0;
volatile unsigned char fliped = 0;

ISR(SPI_STC_vect) {
	unsigned char temp = ptr2[count];
	ptr2[count] = SPDR;
	SPDR = temp;
	count++;
	if(count > 4) {
		count = 0;
		fliped = 1;
	}
}

int main(void) {
	ptr1 = (unsigned char *) malloc(5);
	ptr2 = (unsigned char *) malloc(5);
	ptr1[0] = 0x00;
	ptr1[1] = 0xFF;
	ptr1[2] = 0x00;
	ptr1[3] = 0xFF;
	ptr1[4] = 0x00;
	DDRD = 0xFF;
	DDRC = 0xFF;
	SPCR = (1<<SPE) | (1<<SPIE);   //Enable SPI
	sei();
	for(;;) {
		if(fliped && (PINB & 0b00000100)) {
			count = 0;
			fliped = 0;
			tmp = ptr1;
			ptr1 = ptr2;
			ptr2 = tmp;
		}
		PORTC = 0x00;
		PORTD = ptr1[0];
		PORTC = 0b00000001;
		_delay_us(DELAY);
		PORTC = 0x00;
		PORTD = ptr1[1];
		PORTC = 0b00000010;
		_delay_us(DELAY);
		PORTC = 0x00;
		PORTD = ptr1[2];
		PORTC = 0b00000100;
		_delay_us(DELAY);
		PORTC = 0x00;
		PORTD = ptr1[3];
		PORTC = 0b00001000;
		_delay_us(DELAY);
		PORTC = 0x00;
		PORTD = ptr1[4];
		PORTC = 0b00010000;
		_delay_us(DELAY);
	}
}