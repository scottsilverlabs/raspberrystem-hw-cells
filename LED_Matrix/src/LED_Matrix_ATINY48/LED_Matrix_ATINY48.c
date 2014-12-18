/*
 * accel_server.c
*
* Copyright (c) 2014, Scott Silver Labs, LLC.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#define F_CPU 8000000
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdlib.h>

// The column port fits nicely on one port, where each pin maps directly to a
// column - this makes column scanning easy.  Not so with the rows, which have
// to be mapped, and are on different ports.
#define COL_PORT PORTD
#define ROW0 (1<<PORTC7)
#define ROW1 (1<<PORTA1)
#define ROW2 (1<<PORTC0)
#define ROW3 (1<<PORTC1)
#define ROW4 (1<<PORTC2)
#define ROW5 (1<<PORTC3)
#define ROW6 (1<<PORTC4)
#define ROW7 (1<<PORTC5)

#define NUM_COLS 8
#define NUM_ROWS 8

#define NUM_COLORS 16
#define NUM_PWM_SLOTS (NUM_COLORS - 1)

#define IS_CS_ACTIVE() ( ! ((PINB >> 2) & 1))

#define PACKED_FB_SIZE 32

#define REFRESH_RATE_HZ 120
#define REFRESH_RATE_US (1000000/120)
#define DELAY_US (REFRESH_RATE_US / ( NUM_COLS * NUM_PWM_SLOTS))

volatile unsigned char arr1[PACKED_FB_SIZE];
volatile unsigned char arr2[PACKED_FB_SIZE];
volatile unsigned char fb[NUM_COLS][NUM_ROWS];
volatile unsigned char * swap;
volatile unsigned char * show;
volatile unsigned char * store;
volatile int count = 0;
unsigned char isr_occured = 0;

// This interrupt fires when the SPDR register is done loading a new byte
// Load data for next matrix
// TBD: required?
ISR(SPI_STC_vect) {
    store[count] = SPDR;
    count = (count + 1) & (PACKED_FB_SIZE - 1);
    SPDR = store[count];
    isr_occured = 1;
}

int main(void) {
    store = arr1;
    show = arr2;

    // Set input/output ports
    DDRB = (1<<PORTB4);
    DDRD = 0xFF;
    DDRC = ROW0 | ROW2 | ROW3 | ROW4 | ROW5 | ROW6 | ROW7;
    DDRA = ROW1;
    PORTB |= (1<<PORTB7);
    PORTB |= (1<<PORTB2);

    // Initialize SPI
    SPCR = (1<<SPE) | (1<<SPIE);   // Enable SPI
    SPCR &= ~((1 << CPHA) | (1 << CPOL));
    SPSR = (1 << SPI2X);

    // Enable global interrupts
    sei();

    for(int i = 0; i < PACKED_FB_SIZE; i++){
        show[i] = 0;
        store[i] = 0;
        show[i] = ((0+i)&31)/4 | ((((0+i)&31)/4) << 4);
    }
            int i = 0;
            for (int col = 0; col < NUM_COLS; col++) {
                for (int row = 0; row < NUM_ROWS; row += 2) {
                    fb[col][row] = show[i] & 0xF;
                    fb[col][row + 1] = show[i] >> 4;
                    i++;
                }
            }

    // Initialize output data
    // Needed?
    SPDR = show[count];

    for(;;) {
        // If it has just received data
        if (isr_occured && ! IS_CS_ACTIVE()) {
            // Swap store/show SPI data buffer pointers and reset for the next
            // ISR.  Don't let ISR occur here, although we still must get it
            // in a reasonable time or we'll overwrite the SPI data buffer.
            cli();
            swap = store;
            store = show;
            show = swap;
            isr_occured = 0;
            count = 0;
            sei();

            // Copy from packed SPI data to expanded frame buffer.  Rationale:
            // unpack here to minimize work in display loop which occurs more
            // frequently.
            int i = 0;
            for (int col = 0; col < NUM_COLS; col++) {
                for (int row = 0; row < NUM_ROWS; row += 2) {
                    fb[col][row] = show[i] & 0xF;
                    fb[col][row + 1] = show[i] >> 4;
                    i++;
                }
            }
        } else {
            // Scan columns
            for(int col = 0; col < NUM_COLS; col++) {
                // Output PWM for each row
                for(int pwm = 0; pwm < NUM_PWM_SLOTS; pwm++) {
                    // Write all rows for given column - shut off all columns
                    // while writing rows to have a clean turn-on for the
                    // column.  Careful!!!  Not all rows are on the same output
                    // port!
                    int porta;
                    int portc;
                    COL_PORT = 0x00;
                    porta = 0;
                    portc = 0;
                    if (pwm < fb[col][0]) portc |= ROW0;
                    if (pwm < fb[col][1]) porta |= ROW1;
                    if (pwm < fb[col][2]) portc |= ROW2;
                    if (pwm < fb[col][3]) portc |= ROW3;
                    if (pwm < fb[col][4]) portc |= ROW4;
                    if (pwm < fb[col][5]) portc |= ROW5;
                    if (pwm < fb[col][6]) portc |= ROW6;
                    if (pwm < fb[col][7]) portc |= ROW7;
                    PORTA = porta;
                    PORTC = portc;
                    COL_PORT = (1 << col);

                    // It may be possible to save power by sleeping here,
                    // instead of a busy loop
                    _delay_us(DELAY_US);
                }
            }
        }
    }
}
