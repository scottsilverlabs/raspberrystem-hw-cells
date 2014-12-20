/*
 * LED_Matrix_ATINY48.c
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
#include <string.h>

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
#define MAX_COLOR (NUM_COLORS - 1)

#define NUM_PWM_SLOTS 63

#define IS_CS_ACTIVE() ( ! ((PINB >> 2) & 1))

#define PACKED_FB_SIZE 32

#define REFRESH_RATE_HZ 120
#define REFRESH_RATE_US (1000000/REFRESH_RATE_HZ)
#define POST_BARS_DELAY_US (40000)
#define POST_PLUS_DELAY_US (20000)
#define POST_PLUS_GRADIENT_US (1000000)

// SPI ping-pong packed framebuffer storage
volatile unsigned char arr1[PACKED_FB_SIZE];
volatile unsigned char arr2[PACKED_FB_SIZE];
volatile unsigned char * swap;
volatile unsigned char * show;

// Unpacked framebuffer.  Each pixel is a color converted to PWM value.
// fb2 is just for use for POST concentric squares test.
volatile unsigned char fb[NUM_COLS][NUM_ROWS];
volatile unsigned char fb2[NUM_COLS][NUM_ROWS];

typedef enum {
    START,
    POST_VERTICAL_BARS,
    POST_HORIZONTAL_BARS,
    POST_PLUS,
    POST_PLUS_2,
    POST_PLUS_3,
    POST_PLUS_4,
    RUNNING,
    RUNNING_1,
} STATE;

// All variables used in the ISR have been hand optimized to registers
//
// Count needs to be in r16 and up in order to allow the ANDI mnemonic
volatile register unsigned char index asm("r16");
volatile register unsigned char isr_hasnt_occured asm("r3");
// store is a word (16-bit), so it uses r4 and r5, too!
volatile register unsigned char * store asm("r4");

// ISR for SPI byte received.
//
// Implemented as a ring buffer of size PACKED_FB_SIZE bytes.  The newest byte
// is received, and the oldest byte in the ring is registered to be sent out on
// next transmit.
ISR(SPI_STC_vect) {
    store[index] = SPDR;

    //Optimized version of:
    //  index = (index + 1) & (PACKED_FB_SIZE - 1);
    asm volatile(
        "inc	r16\n\t"
        "andi	r16, %0\n\t"
        :: "I" (PACKED_FB_SIZE - 1));

    SPDR = store[index];

    // Optimization: Important that this flag's polarity has been chosen so it
    // can be set to 0 here in the ISR.  Setting to zero is a faster operation
    // than setting to one (for registers < r16).
    isr_hasnt_occured = 0;
}

// PWM mapping
//
// Linear colors converted directly to PWM appear too different on the low end
// (and conversely too similar on the high end).  Instead, convert all colors
// to a PWM using an approximate geometric sequence of 16 values, with the
// ratio 1.32 (determined experimentally to give a max value of 63).
const unsigned char color_to_pwm_mapping[NUM_COLORS] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 15, 20, 26, 35, 47, 63,
};

inline unsigned char color_to_pwm(unsigned char color)
{
    return color_to_pwm_mapping[color & MAX_COLOR];
}


int main(void) {
    STATE state = START;
    int post_count = 0;
    int post_col = 0;
    int post_row = 0;

    store = arr1;
    show = arr2;

    // Set output ports (all others remain inputs)
    DDRB = (1<<PORTB4); // MISO
    DDRD = 0xFF; // All columns
    DDRC = ROW0 | ROW2 | ROW3 | ROW4 | ROW5 | ROW6 | ROW7;
    DDRA = ROW1;

    // CS pull-up
    PORTB |= (1<<PORTB2);

    // Initialize SPI
    SPCR = (1<<SPE) | (1<<SPIE);   // Enable SPI
    SPCR &= ~((1 << CPHA) | (1 << CPOL));
    SPSR = (1 << SPI2X);

    // Initialize SPI ring buffers
    memset(show, 0, sizeof(show));
    memset(store, 0, sizeof(show));

    // Prep SPI transmit with first byte from ring buffer
    isr_hasnt_occured = 1;
    index = 0;
    SPDR = store[index];

    for(;;) {
        if (state == RUNNING_1) {
            // Do nothing.  This state is out of order, because its the
            // "normal" state, and therefore we've optimized by putting the
            // test for it first.

        } else if (state == START) {
            state = POST_VERTICAL_BARS;

        } else if (state == POST_VERTICAL_BARS) {
            if (post_count < (POST_BARS_DELAY_US / REFRESH_RATE_US)) {
                post_count++;
            } else {
                post_count = 0;
                memset(fb, 0, sizeof(fb));
                for (unsigned char row = 0; row < NUM_ROWS; row++) {
                    fb[post_col][row] = color_to_pwm(MAX_COLOR);
                }

                post_col++;
                if (post_col == NUM_COLS) {
                    state = POST_HORIZONTAL_BARS;
                }
            }

        } else if (state == POST_HORIZONTAL_BARS) {
            if (post_count < (POST_BARS_DELAY_US / REFRESH_RATE_US)) {
                post_count++;
            } else {
                post_count = 0;
                memset(fb, 0, sizeof(fb));
                for (unsigned char col = 0; col < NUM_ROWS; col++) {
                    fb[col][post_row] = color_to_pwm(MAX_COLOR);
                }

                post_row++;
                if (post_row == NUM_ROWS) {
                    if (IS_CS_ACTIVE()) {
                        state = POST_PLUS;
                    } else {
                        state = RUNNING;
                    }
                }
            }

        } else if (state == POST_PLUS) {
            // Fill the framebuffer with a gradient of all colors
            for (unsigned char col = 0; col < NUM_COLS; col++) {
                for (unsigned char row = 0; row < NUM_ROWS/2; row++) {
                    fb[col][row] = color_to_pwm(col);
                }
            }
            for (unsigned char col = 0; col < NUM_COLS; col++) {
                for (unsigned char row = NUM_ROWS/2; row < NUM_ROWS; row++) {
                    fb[col][row] = color_to_pwm(MAX_COLOR - col);
                }
            }
            post_count = 0;
            state = POST_PLUS_2;

        } else if (state == POST_PLUS_2) {
            // State on the gradient for a while
            if (post_count < (POST_PLUS_GRADIENT_US / REFRESH_RATE_US)) {
                post_count++;
            } else {
                state = POST_PLUS_3;
            }

            if ( ! IS_CS_ACTIVE()) {
                state = RUNNING;
            }

        } else if (state == POST_PLUS_3) {
            // Assuming NUM_COLS == NUM_ROWS, create concentric squares of
            // varying intensities
            for (unsigned char i = 0; i < NUM_COLS/2; i++) {
                for (unsigned char r = i; r < NUM_COLS - i; r++) {
                    unsigned char color = 4 * i;
                    fb2[r][i] = color;
                    fb2[r][NUM_COLS - i - 1] = color;
                    fb2[i][r] = color;
                    fb2[NUM_COLS - i - 1][r] = color;
                }
            }
            state = POST_PLUS_4;

        } else if (state == POST_PLUS_4) {
            // Increment the intensity of all pixels in the framebuffer.
            if (post_count < (POST_PLUS_DELAY_US / REFRESH_RATE_US)) {
                post_count++;
            } else {
                post_count = 0;
                for (unsigned char col = 0; col < NUM_COLS; col++) {
                    for (unsigned char row = 0; row < NUM_ROWS; row++) {
                        if (fb2[col][row] == 0xF) {
                            fb2[col][row] = 0x1E;
                        } else if (fb2[col][row] == 0x11) {
                            fb2[col][row] = 0x0;
                        } else if (fb2[col][row] < 0x10) {
                            fb2[col][row]++;
                        } else {
                            fb2[col][row]--;
                        }
                    }
                }
                for (unsigned char col = 0; col < NUM_COLS; col++) {
                    for (unsigned char row = 0; row < NUM_ROWS; row++) {
                        fb[col][row] = color_to_pwm(fb2[col][row]);
                    }
                }
            }

            if ( ! IS_CS_ACTIVE()) {
                state = RUNNING;
            }

        } else if (state == RUNNING) {
            memset(fb, 0, sizeof(fb));

            // Enable global interrupts, allowing SPI recevies
            sei();
            state = RUNNING_1;
        }

        // If it has just received data
        if ( ! isr_hasnt_occured && ! IS_CS_ACTIVE()) {
            // Swap store/show SPI data buffer pointers and reset for the next
            // ISR.  Don't let ISR occur here, although we still must get it
            // in a reasonable time or we'll overwrite the SPI data buffer.
            cli();
            swap = store;
            store = show;
            show = swap;
            isr_hasnt_occured = 1;
            index = 0;
            sei();

            // Copy from packed SPI data to expanded frame buffer.  Rationale:
            // unpack here to minimize work in display loop which occurs more
            // frequently.
            unsigned char i = 0;
            for (unsigned char col = 0; col < NUM_COLS; col++) {
                for (unsigned char row = 0; row < NUM_ROWS; row += 2) {
                    fb[col][row] = color_to_pwm(show[i] & MAX_COLOR);
                    fb[col][row + 1] = color_to_pwm(show[i] >> 4);
                    i++;
                }
            }
        } else {
            // Scan columns
            for(unsigned char col = 0; col < NUM_COLS; col++) {
                // Output PWM for each row
                for(unsigned char pwm = 0; pwm < NUM_PWM_SLOTS; pwm++) {
                    // Write all rows for given column - shut off all columns
                    // while writing rows to have a clean turn-on for the
                    // column.  Careful!!!  Not all rows are on the same output
                    // port!
                    unsigned char porta;
                    unsigned char portc;
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
                    _delay_us(REFRESH_RATE_US / ( NUM_COLS * NUM_PWM_SLOTS));
                }
            }
        }
    }
}
