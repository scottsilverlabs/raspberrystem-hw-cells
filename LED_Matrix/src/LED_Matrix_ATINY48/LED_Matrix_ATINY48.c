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

volatile unsigned char arr1[32];
volatile unsigned char arr2[32];
volatile unsigned char *swap;
volatile unsigned char *show;
volatile unsigned char *store;
volatile int count = 0;
unsigned char fliped = 1;

#define F_CPU 8000000
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdlib.h>

#define SET(PORT,PIN,VALUE) ((PORT) & ~(1 << (PIN))) | (((VALUE)&1) << (PIN))
#define COL_PORT PORTD
#define ROW0 PORTC7
#define ROW1 PORTA1
#define ROW2 PORTC0
#define ROW3 PORTC1
#define ROW4 PORTC2
#define ROW5 PORTC3
#define ROW6 PORTC4
#define ROW7 PORTC5


//This interrupt fires when the SPDR register is done loading a new byte
ISR(SPI_STC_vect) {
    //Store contents
    store[count] = SPDR;
    //Increment Count
    count = (count + 1)&31;
    //Load data for next matrix
    SPDR = store[count];
    //Reset change variable
    fliped = 0;
}

int main(void) {
    store = arr1;
    show = arr2;
    //Set output ports
    DDRB = (1<<PORTB4);
    DDRD = 0xFF;
    DDRC = (1<<ROW0) | (1<<ROW2) | (1<<ROW3) | (1<<ROW4) | (1<<ROW5) | (1<<ROW6) | (1<<ROW7);
    DDRA = (1<<ROW1);
    PORTB = SET(PORTB,PORTB7,1);
    PORTB = SET(PORTB,PORTB2,1);
    //Initialize SPI
    SPCR = (1<<SPE) | (1<<SPIE);   //Enable SPI
    SPCR &= ~((1 << CPHA) | (1 << CPOL));
    SPSR = (1 << SPI2X);
    //Enable global interrupts
    sei();

    //Initialize matrix with gradient, will be removed for final version
    if(!((PINB >> 7) & 1)){
        for(unsigned char i = 0; i < 32; i++){
            show[i] = ((0+i)&31)/4 | ((((0+i)&31)/4) << 4);
        }
    } else {
        for(unsigned char i = 0; i < 32; i++){
            show[i] = 0x0;
        }
    }
    for(int i = 0; i < 32; i++){
        store[i] = 0;
    }

    unsigned char one;
    unsigned char two;
    unsigned char three;
    unsigned char four;
    unsigned char iterations = 0;

    //Initialize output data
    SPDR = show[count];

    for(;;) {
        //If it has just received data
        if(fliped == 0 && ((PINB >> 2) & 0x01)==0x01) {
            //Show the data in storage, store new data in the old array
            swap = store;
            store = show;
            show = swap;
            fliped = 1;
            count = 0;
        } else {
            //Software PWM loop
            for(unsigned char i = 0; i < 8; i++){
                one = show[i*4];
                two = show[i*4 + 1];
                three = show[i*4 + 2];
                four = show[i*4 + 3];

                COL_PORT = 0x00;
                PORTC = SET(PORTC,ROW0,((one&0x0F)>iterations));
                PORTA = SET(PORTA,ROW1,((one>>4)>iterations));
                PORTC = SET(PORTC,ROW2,((two&0x0F)>iterations));
                PORTC = SET(PORTC,ROW3,((two>>4)>iterations));
                PORTC = SET(PORTC,ROW4,((three&0x0F)>iterations));
                PORTC = SET(PORTC,ROW5,((three>>4)>iterations));
                PORTC = SET(PORTC,ROW6,((four&0x0F)>iterations));
                PORTC = SET(PORTC,ROW7,(four>>4)>iterations);
                COL_PORT = (1 << i);
//              _delay_us(10);
            }
            iterations = (++iterations)&15;
        }
    }
}
