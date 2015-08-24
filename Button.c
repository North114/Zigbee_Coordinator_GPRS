#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

# define F_CPU 16000000/* 16 MHz CPU clock */

struct {
    unsigned bit4:1;
    unsigned bit5:1;
}bitVar;

void initLED() {
    DDRC |= 0x80; //Makes bit 7 of PORTC Output
}

void  initButton(){
    DDRC &= ~(0x30);//Makes bit 4 and bit 5 of PORTC Iutput
}

void LEDON() {
    PORTC |= 0x80; //Turns ON LEDs

}
void LEDOFF() {
    PORTC &= ~(0x80); //Turns OFF LEDs
}

int main()
{
    unsigned temp;

    initLED();
    initButton();

    while(1) {
        temp = PINC & 0x30;
        bitVar.bit4 = temp >> 4;
        bitVar.bit5 = temp >> 5;

        if(bitVar.bit5 & (~bitVar.bit4)) LEDON();
        else LEDOFF();
        temp = 0x00;
    }
    return 0;
}
