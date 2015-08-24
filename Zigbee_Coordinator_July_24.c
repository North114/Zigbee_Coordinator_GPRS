/*
CPU : ATmega644pA
Frequency : 16MHz

Function : Zigbee Coordinator(Version 01)

Added Function:Read Real-Time Data of all Router or Specified Router

Some Volatile need to be added before variable declare

changing log:

description:changing recent data meaning,now,recent data means 'today's' data;
date:01-26-2015

description:close global interrupt when we use twi;
date:01-27-2015

description:close global interrupt when we store received data into eeprom
date:03-17-2015
*/
#define F_CPU 16000000UL  /* 16 MHz CPU clock */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

//#define DEBUG
/* DS1307 Related Macro */
#define DS1307 0x68//define 7-bit slave device address.

/* AT24C128 Related Macro */
#define AT24C128 0x50//define 7-bit slave device address.
#define BlockLength 15
#define EEpromSize 16384//Address Range: 0 ~ 16383
#define ReservedByteNum 34


/* TWI Related Macro */
/* status codes for master transmiter mode */
#define  START  0X08//A TWI_Start condition has been transmitted
#define  ReStart 0x10//A repeated Start condition has been transmitted
#define  MT_SLA_ACK  0X18//(Master Transmit SLave Address ACK)slave+w has been transmitted;ACK has been received
#define  MR_SLA_ACK 0x40//Master Receive Slave Address ACK
#define  MT_DATA_ACK  0X28//(Master Transmit DATA ACK)data byte has been transmitted;ACK has been received
#define  MR_DATA_ACK 0x50//Master Receive DATA ACK
#define  MR_DATA_NACK 0X58//Master Receive DATA Not ACK

#define Read 1
#define Write 0

#define Start() (TWCR=(1<<TWINT)|(1<<TWSTA)|(1<<TWEN))	//²úÉúSTARTÐÅºÅ
#define Stop() (TWCR=(1<<TWINT)|(1<<TWSTO)|(1<<TWEN))	//²úÉúSTOPÐÅºÅ
#define Wait() while(!(TWCR&(1<<TWINT)))				//µÈ´ýµ±Ç°²Ù×÷Íê³É
#define TestACK() (TWSR&0xF8)							//È¡³ö×´Ì¬Âë(TWI Status Register)
#define SetACK() (TWCR|=(1<<TWEA))						//²úÉúACK
#define ResetACK() (TWCR=(1<<TWINT)|(1<<TWEN))			//²úÉúNACK
#define Writebyte(twi_d) {TWDR=(twi_d);TWCR=(1<<TWINT)|(1<<TWEN);}	//·¢ËÍÒ»¸ö×Ö½Ú£¨twi_dÎªÐ´ÈëµÄÊý¾Ý£©

/* Zigbee Related Macro */
/** StartByte_Zigbee + UserIDByte + LeakageValueByteMSB + LeakageValueByteLSB
+ VoltageMSB + VoltagelSB + EndByte_Zigbee**/
#define recBufferSize_Zigbee 14// larger than PackLen
#define Zigbee_PackLen 7
#define Zigbee_AckLen 5

#define StartByte_Zigbee 0xAA
#define EndByte_Zigbee 0x75 //End byte should less than 128,since it's a character
#define ZigbeeQueryByte 0xCC //query command byte
#define RouterNum 5        //Total device number in a PAN

/* Bluetooth Related Macro */
#define recBufferSize_Bluetooth 5// larger than PackLen
#define StartByte_Bluetooth 0xBB
#define EndByte_Bluetooth 0x70 //end byte should less than 128,since it's a character

#define CACHE_TIME 250
#define CACHE_SPACE 100
#define T0IniVal 55
/* Function Declaration */

/* DS1307 Relatted Variable Defination */
unsigned char CurrentTime[7];

/* AT24C128 Relatted Variable Defination */
unsigned char W_EEprom_Array[BlockLength],R_EEprom_Array[BlockLength];
unsigned int FirstReadByteAddr = 0, LastReadByteAddr = 0;
unsigned int FirstUnReadByteAddr = 0,LastUnReadByteAddr = 0;
unsigned char EEpromFull;

/* Zigbee Relatted Variable Defination */
volatile unsigned char startFlag_Zigbee = 0;
volatile unsigned char recFlag_Zigbee = 0;//add a 'volatile' is very impportant
//we can also choose _Bool 
volatile unsigned char index_Zigbee = 0;
volatile unsigned char recNum_Zigbee = 0;
volatile unsigned char recBuffer_Zigbee[recBufferSize_Zigbee];

unsigned char ACK_Zigbee[Zigbee_AckLen] = {StartByte_Zigbee,0,0,0,EndByte_Zigbee};


/* Bluetooth Relatted Variable Defination */
volatile unsigned char startFlag_Bluetooth = 0,recFlag_Bluetooth;//add a 'volatile' is very impportant
volatile unsigned char index_Bluetooth = 0,recNum_Bluetooth = 0;
volatile unsigned char recBuffer_Bluetooth[recBufferSize_Bluetooth];
volatile unsigned char BlockNum;
volatile unsigned char RealTimeQuery = 0;

/* data cacheing */
volatile unsigned char cache_current[CACHE_SPACE] = {0};
volatile unsigned int cache_voltage[CACHE_SPACE] = {0};
volatile unsigned char cache_ttl[CACHE_SPACE] = {0};
volatile unsigned int T0_Count = 0;
volatile unsigned int bisecondCount = 0;
volatile unsigned char modTemp = 0;


inline unsigned char setBit(unsigned char d,unsigned char n) {
	return (d | (1<<n));
}
/* Example : data = clearBit(data,1),means clear 1st bit of data(note data holds 1 byte) */
inline unsigned char clearBit(unsigned char d,unsigned char n) {
	return (d & (~(1<<n)));
}

/*
Structure for read button status
*/
volatile struct {
    unsigned bit4:1;
    unsigned bit5:1;
}bitVar;
/*
Initialize Button and Led Pins
*/
void initIO() {
    DDRC |= 0x80; //Makes bit 7 of PORTC Output(for LED)
	DDRC &= ~(0x30);//Makes bit 4 and bit 5 of PORTC Iutput(for Button)
    DDRD |= 0x30; //make PortD(4:5) output;(for CD4052)
}

void LEDON() {
    PORTC |= 0x80; //Turns ON LEDs
}
void LEDOFF() {
    PORTC &= ~(0x80); //Turns OFF LEDs
}

void readButtonSatus() {
	volatile unsigned char temp;

	temp = PINC & 0x30;//button status
	bitVar.bit4 = temp >> 4;
	bitVar.bit5 = temp >> 5;
}

int checkStatus() {
	//if bit changed , then change the pin accordingly
	if(bitVar.bit5 ^ (PORTD & (1 << 5))) {
		if(bitVar.bit5)PORTD = setBit(PORTD,5); //make output 10(connect to GPRS);
	   	else PORTD = clearBit(PORTD,5);
	}

	//if bit changed , then change the pin accordingly
   	if(bitVar.bit4 ^ (PORTD & (1 << 4))) {
    	if(bitVar.bit4)PORTD = setBit(PORTD,4); //make output 10(connect to GPRS);
    	else PORTD = clearBit(PORTD,4);
    }

    if((~bitVar.bit4) && bitVar.bit5)return 1;
    else return 0;
}
/*
Initialize Watch Timer Dog
*/
void InitWatchDogTimer()
{
	/* Start Timed Sequence */
	WDTCSR |= (1<<WDCE)|(1<<WDE);//set WDCE to change WDE and prescaler
	/* Set New Prescaler Time-Out Value */
	WDTCSR = (1<<WDE)|(1<<WDP3)|(1<<WDP0);	//Time-Out is 8 seconds,system reset mode(@page 61)
	//Watch Dog Timer in High Fuse Bit(WDTON = 1)
}
/*
Initialize Timer0
*/
void Timer0_Init()
{
	TCCR0B = (1<<CS02);//PRE-SCALE : 256 (@page 110)
	TIMSK0 = (1<<TOIE0);//ENABLE TIMER 0 OVERFLOW INTERRUPT(@page 111)
	
	TCNT0 = T0IniVal;//Timing/Counter Register
}
/*
Initialize TWI
*/
void TWI_Init()
{
	/* @page 234 */
    TWBR=0x20;	//TWI Bit Rate Register
	TWCR=0x44;	//TWI Control Register
	TWSR=0;		//TWI Status Register
}
/*
Initialize USART0
*/
void USART0_Init(unsigned int baud)
{
	UCSR0A = 0x00;//defalut value
	UCSR0B = 0x00;//USART Control and Status Register B 		    //¿ØÖÆ¼Ä´æÆ÷ÇåÁã
	UCSR0C = 3<<UCSZ00;//8 bit data
                                                        //Ñ¡ÔñUCSRC£¬Òì²½Ä£Ê½£¬½ûÖ¹                        
                                                     //   Ð£Ñé£¬1Î»Í£Ö¹Î»£¬8Î»Êý¾ÝÎ»
	baud = F_CPU/16/baud - 1	;   //²¨ÌØÂÊ×î´óÎª65K
	UBRR0L = baud; 					     	  
	UBRR0H = baud>>8; 		   //ÉèÖÃ²¨ÌØÂÊ
   
	UCSR0B = (1<<TXEN0)|(1<<RXEN0)|(1<<RXCIE0); //½ÓÊÕ¡¢·¢ËÍÊ¹ÄÜ£¬½ÓÊÕÖÐ¶ÏÊ¹ÄÜ
   
	//SREG = BIT(7);	       //È«¾ÖÖÐ¶Ï¿ª·Å
	DDRD |= 0x02;	           //ÅäÖÃTX0 pin(PD1) ÎªÊä³ö£¨ºÜÖØÒª£©
}
/*
Initialize USART1
*/
void USART1_Init(unsigned int baud)
{
	UCSR1A = 0x00;
	UCSR1B = 0x00;//USART Control and Status Register B 		    //¿ØÖÆ¼Ä´æÆ÷ÇåÁã
	UCSR1C = 3<<UCSZ10;//8 bit data
                                                        //Ñ¡ÔñUCSRC£¬Òì²½Ä£Ê½£¬½ûÖ¹                        
                                                     //   Ð£Ñé£¬1Î»Í£Ö¹Î»£¬8Î»Êý¾ÝÎ»
	baud = F_CPU/16/baud - 1	;   //²¨ÌØÂÊ×î´óÎª65K
	UBRR1L = baud; 					     	  
	UBRR1H = baud>>8; 		   //ÉèÖÃ²¨ÌØÂÊ
	
	UCSR1B = (1<<TXEN1)|(1<<RXEN1)|(1<<RXCIE1); //½ÓÊÕ¡¢·¢ËÍÊ¹ÄÜ£¬½ÓÊÕÖÐ¶ÏÊ¹ÄÜ
   	
	//SREG = BIT(7);	       //È«¾ÖÖÐ¶Ï¿ª·Å
	DDRD |= 0x08;	           //ÅäÖÃTX1 pin(PD3) ÎªÊä³ö£¨ºÜÖØÒª£©
}
/*
Read 1 data byte from DS1307
*/
unsigned char ReadDS1307(unsigned char DevAddr,unsigned char RegAddr)
{
	unsigned char data;
	
	DevAddr = (DevAddr<<1)|(Write);//????????????????
	/* Start TWI */
	Start();
	Wait();
	if(TestACK()!=START)
	{
	   return 0;
	}
	/* Write Device Address */
	Writebyte(DevAddr);//SLA+W
	Wait();
	if(TestACK()!=MT_SLA_ACK)
	{
	   return 0;
	}
	/* Write Register Address 0x00) */
	Writebyte(RegAddr);
	Wait();
	if(TestACK()!=MT_DATA_ACK)
	{
	   return 0;
	}

	/* Restart */	

	Start();
	Wait();
	if(TestACK()!=ReStart)
	{
	   return 0;
	}
	Writebyte(DevAddr + 1);//SLW+R
	Wait();
	if(TestACK()!=MR_SLA_ACK)//
	{
	   return 0;
	}
	
	/* added for certian purpose */
	//TWCR=(1<<TWINT)|(1<<TWEN);
	ResetACK();
	Wait();
	if(TestACK()!=MR_DATA_NACK)//
	{
	   return 0;
	}
	/* Receive Data(1 byte only) */
	data = TWDR;
	
	Stop();

	_delay_ms(5);
	
	return data;

}
/*
Read All Time data
*/
unsigned char Read_Current_Time(unsigned char DevAddr,unsigned char *p,unsigned char num)
{
	//unsigned char data;
	unsigned char i = 0;
	cli();		//Diaable Global Interrupt
	DevAddr = (DevAddr<<1)|(Write);//????????????????
	/* Start TWI */
	Start();
	Wait();
	if(TestACK()!=START)
	{
	   return 0;
	}
	/* Write Device Address */
	Writebyte(DevAddr);//SLA+W
	Wait();
	if(TestACK()!=MT_SLA_ACK)
	{
	   return 0;
	}
	/* Write Register Address 0x00) */
	Writebyte(0);
	Wait();
	if(TestACK()!=MT_DATA_ACK)
	{
	   return 0;
	}

	/* Restart */	

	Start();
	Wait();
	if(TestACK()!=ReStart)
	{
	   return 0;
	}
	Writebyte(DevAddr + 1);//SLW+R
	Wait();
	if(TestACK()!=MR_SLA_ACK)//
	{
	   return 0;
	}

	/* Rstart TWI */
	//TWCR=(1<<TWINT)|(1<<TWEN);
	//Wait();

	/* Read MultiByte From DS1307 ???? */
	for(i = 0;i < (num-1);i++){
		/* Receive Data(1 byte only) */
		SetACK();
		Wait();
		if(TestACK()!=MR_DATA_ACK)//
		{
	   		return 0;
		}
		*(p+i) = TWDR;
		//to do (ACK)????	
	}
	ResetACK();
	Wait();
	if(TestACK()!=MR_DATA_NACK)//
	{
	   	return 0;
	}
	*(p+num-1) = TWDR;

	Stop();

	_delay_ms(5);
	
	sei();//Enable Global Interrupt
	return 1;

}
/*
Write 1 byte data to EEPROM
*/
unsigned char WriteEEPROM(unsigned char DevAddr,unsigned int MemAddr,unsigned char data)
{
	/* Start TWI */
	Start();
	Wait();
	if(TestACK()!=START)
	{
	   return 0;
	}
	/* Write Device Address */
	Writebyte((DevAddr<<1)|(Write));
	Wait();
	if(TestACK()!=MT_SLA_ACK)
	{
	   return 0;
	}
	/* Write Memory Address High Byte) */
	Writebyte(MemAddr>>8);
	Wait();
	if(TestACK()!=MT_DATA_ACK)
	{
		return 0;
	}
	/* Write Memory Address Low Byte) */
	Writebyte(MemAddr&0xff);
	Wait();
	if(TestACK()!=MT_DATA_ACK)
	{
		return 0;
	}
	/* Write Data to EEPROM */
	Writebyte(data);
	Wait();
	if(TestACK()!=MT_DATA_ACK)
	{
	   return 0;
	}
	Stop();
	_delay_ms(5);
	
	return 1;
}
/*
Write n byte data to EEPROM
*/
unsigned char Write_EEPROM_Block(unsigned char DevAddr,unsigned int MemAddr,unsigned char *p,unsigned char num)
{
	unsigned char i;	
	cli();		//Disable Globle Interrupt
	/* Start TWI */
	Start();
	Wait();
	if(TestACK()!=START)
	{
	   return 0;
	}
	/* Write Device Address */
	Writebyte((DevAddr<<1)|(Write));
	Wait();
	if(TestACK()!=MT_SLA_ACK)
	{
	   return 0;
	}
	/* Write Memory Address High Byte) */
	Writebyte(MemAddr>>8);
	Wait();
	if(TestACK()!=MT_DATA_ACK)
	{
		return 0;
	}
	/* Write Memory Address Low Byte) */
	Writebyte(MemAddr&0xff);
	Wait();
	if(TestACK()!=MT_DATA_ACK)
	{
		return 0;
	}
	/* Write Data to EEPROM */
	for(i = 0;i < num;i++){
		Writebyte(*(p+i));
		Wait();
		if(TestACK()!=MT_DATA_ACK)
		{
	   		return 0;
		}
	}

	Stop();
	_delay_ms(5);
	sei();            //Enable Global Interrupt
	return 1;
}
/*
Read 1 byte data from EEPROM
*/
unsigned char ReadEEPROM(unsigned char DevAddr,unsigned int MemAddr)
{
	unsigned char data;
	/* Start TWI */
	Start();
	Wait();
	if(TestACK()!=START)
	{
		return 0;
	}	
	/* Write Device Address */
	Writebyte((DevAddr<<1)|(Write));//SLA+W
	Wait();
	if(TestACK()!=MT_SLA_ACK)
	{
		return 0;
	}
	/* Write Memory Address High Byte) */
	Writebyte(MemAddr>>8);
	Wait();
	if(TestACK()!=MT_DATA_ACK)
	{
		return 0;
	}
	/* Write Memory Address Low Byte) */
	Writebyte(MemAddr&0xff);
	Wait();
	if(TestACK()!=MT_DATA_ACK)
	{
		return 0;
	}
	/* Restart */	
	Start();
	Wait();
	if(TestACK()!=ReStart)
	{
		return 0;
	}
	/* Write Device Address(read format) */
	Writebyte((DevAddr<<1)|(Read));//SLW+R
	Wait();
	if(TestACK()!=MR_SLA_ACK)//
	{
		return 0;
	}

	/* added for certian purpose */
	TWCR=(1<<TWINT)|(1<<TWEN);
	Wait();

	/* Receive Data(1 byte only) */

	data = TWDR;

	/* Stop TWI */

	Stop();

	_delay_ms(5);
	return data;
}
/*
Read n byte data from EEPROM
*/
unsigned char Read_EEPROM_Block(unsigned char DevAddr,unsigned int MemAddr,unsigned char *p,unsigned char num)
{
	//unsigned char data;
	unsigned char i = 0;
	cli();		//Disable Global Interrupt
	DevAddr = (DevAddr<<1)|(Write);//????????????????
	/* Start TWI */
	Start();
	Wait();
	if(TestACK()!=START)
	{
	   return 0;
	}
	/* Write Device Address */
	Writebyte(DevAddr);//SLA+W
	Wait();
	if(TestACK()!=MT_SLA_ACK)
	{
	   return 0;
	}
	/* Write Memory Address High Byte) */
	Writebyte(MemAddr>>8);
	Wait();
	if(TestACK()!=MT_DATA_ACK)
	{
		return 0;
	}
	/* Write Memory Address Low Byte) */
	Writebyte(MemAddr&0xff);
	Wait();
	if(TestACK()!=MT_DATA_ACK)
	{
		return 0;
	}
	/* Restart */	
	Start();
	Wait();
	if(TestACK()!=ReStart)
	{
	   return 0;
	}
	/* Write Device Address(read format) */
	Writebyte(DevAddr + 1);//SLW+R
	Wait();
	if(TestACK()!=MR_SLA_ACK)//
	{
	   return 0;
	}
	/* Read MultiByte From EEPROM */
	for(i = 0;i < (num-1);i++){
		/* Receive Data(1 byte only) */
		SetACK();
		Wait();
		if(TestACK()!=MR_DATA_ACK)//
		{
	   		return 0;
		}
		*(p+i) = TWDR;
		//to do (ACK)????	
	}
	ResetACK();
	Wait();
	if(TestACK()!=MR_DATA_NACK)//
	{
	   	return 0;
	}
	*(p+num-1) = TWDR;

	Stop();

	_delay_ms(5);
	
	sei();		//Enable Global Interrupt
	return 1;

}
/*
Send Data Through USART0
*/
inline void USART0_Send_Byte(unsigned char data)
{
	/* waitting for a empty USART Data Register */
	while(!(UCSR0A&(1<<UDRE0))) ;
	UDR0 = data;//USART Data Register
   
	/* waitting for USART Transmit Complete */
	while(!(UCSR0A&(1<<TXC0)));
	UCSR0A |= 1<<TXC0;//set TXC bit manually
}
/*
Send Data Through USART1
*/
void USART1_Send_Byte(unsigned char data)
{
	/* waitting for a empty USART Data Register */
	while(!(UCSR1A&(1<<UDRE1))) ;
	UDR1 = data;//USART Data Register
   
	/* waitting for USART Transmit Complete */
	while(!(UCSR1A&(1<<TXC1)));
	UCSR1A |= 1<<TXC1;//set TXC bit manually
}
/*
USART0 Receive Interrupt Service Routing
*/
ISR(USART0_RX_vect)//USART Receive Complete Vector
{
	unsigned char temp;	
	UCSR0B &= (~(1<<RXCIE0));//disable receiver interrupt(reset bit)
	temp = UDR0;//read data
	/* Process Received Data that comes from Bluetooth */
    if((startFlag_Bluetooth == 1)&&(index_Bluetooth < recBufferSize_Bluetooth - 1)){
     	recBuffer_Bluetooth[index_Bluetooth] = temp;
        index_Bluetooth++;
    }
    /* here we decide weather received data are valid */
    if(temp == StartByte_Bluetooth){
        startFlag_Bluetooth = 1;//when we received a start byte,set startFlag

        index_Bluetooth = 0;//initialize index_Zigbee,very important
    }
    else if((startFlag_Bluetooth == 1)&&(temp == EndByte_Bluetooth)){//endByte only make sense when startByte appeare
        startFlag_Bluetooth = 0;//when we received a end byte,reset startFlag
        recNum_Bluetooth = index_Bluetooth - 1;
        index_Bluetooth = 0;
        recFlag_Bluetooth = 1;
    }
    else{}

	UCSR0B |= (1<<RXCIE0);//re-enable receiver interrupt(set bit)
}
/*
USART1 Receive Interrupt Service Routing
*/
ISR(USART1_RX_vect)//USART Receive Complete Vector
{
	unsigned char temp;

	UCSR1B &= (~(1<<RXCIE1));//disable receiver interrupt(reset bit)
	temp = UDR1;//read data

	/* Overrun Error */
    /*if (DOR1 == 1) {
        temp = RCREG;
        temp = RCREG;
        CREN = 0;
        CREN = 1;
    }*/

	/* Process Received Data that comes from Zigbee */
    if((startFlag_Zigbee == 1)&&(index_Zigbee < recBufferSize_Zigbee - 1)){
     	recBuffer_Zigbee[index_Zigbee] = temp;
        index_Zigbee++;
    }
    /* here we decide weather received data are valid */
    if(temp == StartByte_Zigbee){
        startFlag_Zigbee = 1;//when we received a start byte,set startFlag
        index_Zigbee = 0;//initialize index_Zigbee,very important
    }
    else if((startFlag_Zigbee == 1)&&(temp == EndByte_Zigbee)){//endByte only make sense when startByte appeare
        startFlag_Zigbee = 0;//when we received a end byte,reset startFlag
        recNum_Zigbee = index_Zigbee - 1;
        index_Zigbee = 0;
        recFlag_Zigbee = 1;
    }
    else{}
	//USART1_Send_Byte(temp);

	UCSR1B |= (1<<RXCIE1);//re-enable receiver interrupt(set bit)
}
/*
Timer0 Service Routing
*/
ISR(TIMER0_OVF_vect)//Timer0 Overflow Interrupt Vector
{
	/* Hardware will clear Interrupt Flag for us */
	T0_Count++;
	if(T0_Count >= 624) {//about 2 second
		T0_Count = 0;

		/* feed dog every 2 second */
		__asm__ __volatile__ ("wdr");//Watch Dog Timer Reset ??

		//process down operation per 2 seconds,so we can finish all down operation in 10 seconds
		modTemp = bisecondCount % 100;
		if(cache_ttl[modTemp] != 0) {
			if(cache_ttl[modTemp] >= 100)cache_ttl[modTemp] = cache_ttl[modTemp] - 100;
			else cache_ttl[modTemp] = 0;
		}

		++bisecondCount;

		if (bisecondCount >= 100)bisecondCount = 0;//cuz the maximum retransmit time is about 300 seconds,we set clear cache circle abot 500 second

		//TIMSK0 = (0<<TOIE0);//DISABLE TIMER 0 OVERFLOW INTERRUPT(@page 111)
	}
	TCNT0 = T0IniVal;//Timing/Counter Register
}
/*
Store Received data into EEPROM
*/
void StoreZigbeeReceivedData()
{
	unsigned char i;
	unsigned char ReadTimeStatus;
	unsigned char WriteEEPROMStatus;
	unsigned int temp;	

	/* --- Step 2: After transmit ACK data ,read time ,then Archive the data along with ZigbBee data into EEPROM --- */  

	/* for cache data (big endian)*/
	if ((recBuffer_Zigbee[0] <= CACHE_SPACE) && (recBuffer_Zigbee[Zigbee_PackLen - 2] == 0))//we only store data when router are not poweroffed
	{
		cache_ttl[recBuffer_Zigbee[0]] = CACHE_TIME;
		temp = recBuffer_Zigbee[1] * 256 + recBuffer_Zigbee[2];
		cache_current[recBuffer_Zigbee[0]] = temp / 100;
		temp = recBuffer_Zigbee[3] * 256 + recBuffer_Zigbee[4];
		cache_voltage[recBuffer_Zigbee[0]] = temp / 100;
	}

	/* Check Weather we need to send data to GPRS or BlueTooth*/
	readButtonSatus();
	_delay_ms(1);//if status changes , we just need to delay for a while to it
	if(checkStatus() > 0) {
		USART0_Send_Byte(StartByte_Zigbee);
		USART0_Send_Byte(recBuffer_Zigbee[0]);
		USART0_Send_Byte(recBuffer_Zigbee[1]);
		USART0_Send_Byte(recBuffer_Zigbee[2]);
		USART0_Send_Byte(recBuffer_Zigbee[3]);
		USART0_Send_Byte(recBuffer_Zigbee[4]);
		USART0_Send_Byte(0x01);//type indicate
		USART0_Send_Byte(EndByte_Zigbee);
	}

	/* Load data into W_EEprom_Array */
	for(i = 0;i < (Zigbee_PackLen - 2);i++)
	{
		W_EEprom_Array[i] = recBuffer_Zigbee[i];
		/* Then ,Clear ZigBee receive data buffer(Zigbee_Rec)*/
		recBuffer_Zigbee[i] = 0;
	}

	/* This step also read time from DS1307,and write a block data into AT24C128 */
	ReadTimeStatus = Read_Current_Time(DS1307,CurrentTime,7);
	
	/* Add Time Stamp */
	W_EEprom_Array[Zigbee_PackLen - 2] = CurrentTime[6];//year
	W_EEprom_Array[Zigbee_PackLen - 1] = CurrentTime[5];//month
	W_EEprom_Array[Zigbee_PackLen] = CurrentTime[4];//date
	W_EEprom_Array[Zigbee_PackLen + 1] = CurrentTime[2];//hour
	W_EEprom_Array[Zigbee_PackLen + 2] = CurrentTime[1];//minute
	W_EEprom_Array[Zigbee_PackLen + 3] = CurrentTime[0];//second
	/* Fill Currenttly Unused Part */
	W_EEprom_Array[Zigbee_PackLen + 4] = 0x23;//ASCII of '#' mark,it means the data are unread
	for(i = (Zigbee_PackLen + 5);i < BlockLength;i++){
		W_EEprom_Array[i] = 0;//the remaining data byte are set to zero
	}
	/* --- Step 3: Write data into EEProm --- */
	//LastUnReadedByte are stored in address of 21,22.
	LastUnReadByteAddr = 256*ReadEEPROM(AT24C128,21);//this address are stored in EEPROM(address:)High Byte
	LastUnReadByteAddr += ReadEEPROM(AT24C128,22);//Low Byte

	//WriteEEPROMStatus = Write_EEPROM_Block(AT24C128,LastUnReadByteAddr+1,W_EEprom_Array,BlockLength - 3);//we have no need to write 0 into EEPROM

	/* now , we write data to eeprom byte by byte */
	for(i = 0;i < BlockLength;i++)
	{
		WriteEEPROMStatus = WriteEEPROM(AT24C128,LastUnReadByteAddr + i + 1,W_EEprom_Array[i]);
		_delay_ms(1);//delay 1 mili-second
	}
	/* --- Step 4: Prepare for next time --- */
	LastUnReadByteAddr += BlockLength;
	/* Handling data that out of range */
	if(LastUnReadByteAddr >= EEpromSize - 1){//maximum address : EEpromSize - 1
		LastUnReadByteAddr = ReservedByteNum - 1;//pull the address back to initial address
		EEpromFull = 1;//now EEProm are full
		WriteEEPROM(AT24C128,23,EEpromFull);
		// TO DO		
		//EEPROM full state should written into it self
		//now certain statement should be added here for FirstReadByteAddr are related to LastUnReadByteAddr
	}
	if(LastUnReadByteAddr < ReservedByteNum-1) LastUnReadByteAddr = ReservedByteNum-1;
	
	EEpromFull = ReadEEPROM(AT24C128,23);	
	if(EEpromFull == 1){
		// now,we entering a circle storage state.
		//	and these two address are neighbour in the following part.
		FirstReadByteAddr = LastUnReadByteAddr+1;
		WriteEEPROM(AT24C128,15,FirstReadByteAddr>>8);//write back High Byte
		WriteEEPROM(AT24C128,16,FirstReadByteAddr&0xFF);//write back Low Byte
				
		//	Besides,data district of read data might be invaded
		//	So,we should move the address forward accordingly,although it might corrupt
		//	data,we just have to do that to minimize our losses.
		LastReadByteAddr = 256*ReadEEPROM(AT24C128,17);
		LastReadByteAddr += ReadEEPROM(AT24C128,18);
				
		if(FirstReadByteAddr > LastReadByteAddr + 1){
			//LastReadByteAddr
			WriteEEPROM(AT24C128,17,(FirstReadByteAddr - 1)>>8);//write back High Byte
			WriteEEPROM(AT24C128,18,(FirstReadByteAddr - 1)&0xFF);//write back Low Byte
			//FirstunReadByteAddr
			WriteEEPROM(AT24C128,19,(FirstReadByteAddr)>>8);//write back High Byte
			WriteEEPROM(AT24C128,20,(FirstReadByteAddr)&0xFF);//write back Low Byte	
		}
	}
	//	Write new LastUnReadByteAddr to EEPROM
	WriteEEPROM(AT24C128,21,LastUnReadByteAddr>>8);//write back High Byte
	WriteEEPROM(AT24C128,22,LastUnReadByteAddr&0xFF);//write back Low Byte

}
/*
Read Command From Bluetooth
*/
void ReadCommandFromBluetooth()
{
	volatile unsigned char i,j,k;
	volatile unsigned char count;
	unsigned char ReadEEPROMStatus,date,month;
	unsigned int ADDR_temp;
	unsigned int cache_temp;

	switch(recBuffer_Bluetooth[0]){
	case 0x10://Read Unread data
	{	/** in this case , we just transmit unread data to BlueTooth end **/
		/**== the following program only changes 'FirstUnReadByteAddr' and 'LastReadByteAddr' ==**/
		/*=== first:transmit leakage data into BlueTooth ===*/
		//read unread data address
		date = ReadDS1307(DS1307,0x04);//read date,because we just transmit today's data
		month = ReadDS1307(DS1307,0x05);//read month

		LastUnReadByteAddr = 256*ReadEEPROM(AT24C128,21);//this address are stored in EEPROM(address:)High Byte
		LastUnReadByteAddr += ReadEEPROM(AT24C128,22);//Low Byte
					
		
		/** check back if there are some data to be read **/

		/* Convert Address to First-Some Address */
		if(LastUnReadByteAddr <= (ReservedByteNum - 1))
			ADDR_temp = EEpromSize - BlockLength;
		else 
			ADDR_temp = LastUnReadByteAddr - BlockLength + 1;//??
		
		count = 0;

		/* Read data and transmit it if they meet our need */
		while(1){
			
			count++;
			
			if(ADDR_temp <  ReservedByteNum)ADDR_temp = EEpromSize - BlockLength;
			/** when there are some data unread **/	
			ReadEEPROMStatus = Read_EEPROM_Block(AT24C128,ADDR_temp,R_EEprom_Array,BlockLength);
			/* Check weather the data is today received */
			if((R_EEprom_Array[7] != date)||(R_EEprom_Array[6] != month))break;
			
			if(count > 8)break;
			
			USART0_Send_Byte(StartByte_Zigbee);				
			for(j = 0;j < 6;j++)
			{
				USART0_Send_Byte(R_EEprom_Array[j]);			
			}
			_delay_ms(100);//added for debug@01-15
			for(j = 6;j < 13;j++)
			{
				USART0_Send_Byte(R_EEprom_Array[j]);			
			}
			_delay_ms(100);//added for debug@01-15
			for(j = 13;j < BlockLength;j++)
			{
				USART0_Send_Byte(R_EEprom_Array[j]);			
			}
			USART0_Send_Byte(EndByte_Zigbee);
			//_delay_ms(500);//added for debug@01-15
			ADDR_temp -= BlockLength;
		}
		USART0_Send_Byte(0xBB);//End byte

			/** acknowledgement(confirm data have been received by blueTooth end) **/
			/*while(Serial.available() <= 0)
			{delay(200);}//Waiting for data come from BlueTooth
				//delay(5000);//delay for 5 seconds
			if(Serial.available() > 0){/*** here we use if condition ***/
							
			/*Bluetooth_Temp = Serial.read();//read end byte of last package,and discard it.
						
			if(Bluetooth_Temp == StartByte_Bluetooth){
			ByteNum_Bluetooth = Serial.readBytesUntil(EndByte_Bluetooth, Bluetooth_ACK_Temp, 1);
			Bluetooth_Temp = 0;//very important
			}
			if((ByteNum_Bluetooth == 1)&&(Bluetooth_ACK_Temp[0] == (char)BlockNum)){//ack block length
			Bluetooth_Ack_Flag = true;
			ByteNum_Bluetooth = 0;
			Bluetooth_ACK_Temp[0] = 0x00;//very important
			//Serial.println("I am in here now!");//for test purpose
			//break;
			}
			//WriteDataFromEEpromToBluetooth(BlockNum,FirstUnReadByteAddr);
			//if no ACK received,treat it as nothing happened,we never retry it.
			//Serial.println(Bluetooth_ACK_Temp[0]);
			}
						
			//refresh read and unread data address,then write it into EEprom 
			/*if(Bluetooth_Ack_Flag == 1){//if acknowledgement are success, then refresh address
				Bluetooth_Ack_Flag = 0;	
				FirstUnReadByteAddr = LastUnReadByteAddr + 1;
				LastReadByteAddr = LastUnReadByteAddr;
				Write2EEPROM(17,LastReadByteAddr>>8);//High Byte
				Write2EEPROM(18,LastReadByteAddr&0xFF);//Low Byte
				Write2EEPROM(19,FirstUnReadByteAddr>>8);//High Byte
				Write2EEPROM(20,FirstUnReadByteAddr&0xFF);//Low Byte
				//Serial.println("I am in here now!");
			}*/
			break;//(break out case condition)very important
		}
		/* Read All Restored Data */	
		case 0x20:
		{
			/* in this condition,we will transmit all leakage data into BlueTooth end, */
			/* which including unread and read data. */
			/* here no acknowledgement are needed because no need to change some thing in EEPROM*/
			/* besides,if user didn't receive data,he or she can retouch the icon. */
			FirstReadByteAddr = 256*ReadEEPROM(AT24C128,15);
			FirstReadByteAddr += ReadEEPROM(AT24C128,16);
			LastUnReadByteAddr = 256*ReadEEPROM(AT24C128,21);
			LastUnReadByteAddr += ReadEEPROM(AT24C128,22);
			EEpromFull =  ReadEEPROM(AT24C128,23);

			BlockNum = 0;//default no data are to be read
			if(LastUnReadByteAddr > FirstReadByteAddr)BlockNum = (LastUnReadByteAddr - FirstReadByteAddr + 1)/BlockLength;
			/* take '=' condition into consideration !! */
			if((LastUnReadByteAddr <= FirstReadByteAddr - 1) && (EEpromFull)){//when EEprom runs into circle storage state
			BlockNum = (EEpromSize-FirstReadByteAddr)/BlockLength +(LastUnReadByteAddr-(ReservedByteNum-1))/BlockLength;}
			
			/** check if there are some data to be read **/
			ADDR_temp = FirstReadByteAddr;
			for(i = 0;i < BlockNum;i++){
				if(ADDR_temp >= EEpromSize)ADDR_temp = ReservedByteNum;
				/** when there are some data unread **/	
				ReadEEPROMStatus = Read_EEPROM_Block(AT24C128,ADDR_temp,R_EEprom_Array,BlockLength);
				USART0_Send_Byte(StartByte_Zigbee);				
				for(j = 0;j < 6;j++)
				{
					USART0_Send_Byte(R_EEprom_Array[j]);			
				}
				_delay_ms(100);//added for debug@01-15
				for(j = 6;j < 13;j++)
				{
					USART0_Send_Byte(R_EEprom_Array[j]);			
				}
				_delay_ms(100);//added for debug@01-15
				for(j = 13;j < BlockLength;j++)
				{
					USART0_Send_Byte(R_EEprom_Array[j]);			
				}
				USART0_Send_Byte(EndByte_Zigbee);
				_delay_ms(100);//added for debug@01-15
				ADDR_temp += BlockLength;
			}
			USART0_Send_Byte(0xBB);//End byte
			BlockNum = 0;//reset block number
			break;//break out switch case (very important)
		}
		/* Read Data of all Routers Immediately */
		case 0x30:
		{
			RealTimeQuery = 1;			
			/* Query Each Router */
			for(i = 1;i <= RouterNum;i++)//Start From 1
			{
				/* if the data is cached */
				if(cache_ttl[i] > 0) {
					/* Transmit cached data to Bluetooth end */
					USART0_Send_Byte(StartByte_Zigbee);//_delay_ms(10);
					USART0_Send_Byte(i);//_delay_ms(10);
					cache_temp = cache_current[i] * 100;
					USART0_Send_Byte(cache_temp / 256);
					USART0_Send_Byte(cache_temp % 256);//_delay_ms(10);
					cache_temp = cache_voltage[i] * 100;
					USART0_Send_Byte(cache_temp / 256);//_delay_ms(10);
					USART0_Send_Byte(cache_temp % 256);
					USART0_Send_Byte(0x50);//type indicator
					USART0_Send_Byte(EndByte_Zigbee);
					//USART0_Send_Byte(0x11);	//for debug
					continue;//skip following code
				}

				/* Send Query Command to Routers */				
				USART1_Send_Byte(StartByte_Zigbee);
				//_delay_ms(1);
				USART1_Send_Byte(i);
				//_delay_ms(1);
				USART1_Send_Byte(ZigbeeQueryByte);//Command Byte
				//_delay_ms(1);
				USART1_Send_Byte(EndByte_Zigbee);
				/* Wait for ACK */
				for(k = 0;k < 60;k++)
				{
					if(1 == recFlag_Zigbee) break;
					else _delay_ms(10);
				}
				//_delay_ms(600);//replace by for loop up there
				if(1 == recFlag_Zigbee)
				{
					recFlag_Zigbee = 0;
					/* --- Step 1: Send ACK_Zigbee to ZigBee router --- */
					/* then router stop send data to coordinator */
					ACK_Zigbee[1] = recBuffer_Zigbee[0];//router device id
					ACK_Zigbee[2] = recBuffer_Zigbee[1];//leak current high byte
					ACK_Zigbee[3] = recBuffer_Zigbee[2];//leak current low byte
					for(j = 0;j < Zigbee_AckLen;j++)	//you can no longer use variable i,so we choose j
					{
						USART1_Send_Byte(ACK_Zigbee[j]);
					}
					//subtract start and end byte of received data
					
					//if((recNum_Zigbee == (Zigbee_PackLen - 2))&&(recBuffer_Zigbee[0] == i)&&(RealTimeQuery == 1))
					//{
						/* Transmit Received data to Bluetooth end */
						USART0_Send_Byte(StartByte_Zigbee);//_delay_ms(10);
						USART0_Send_Byte(recBuffer_Zigbee[0]);//_delay_ms(10);
						USART0_Send_Byte(recBuffer_Zigbee[1]);
						USART0_Send_Byte(recBuffer_Zigbee[2]);//_delay_ms(10);
						USART0_Send_Byte(recBuffer_Zigbee[3]);//_delay_ms(10);
						USART0_Send_Byte(recBuffer_Zigbee[4]);
						USART0_Send_Byte(0x50);//type indicator
						USART0_Send_Byte(EndByte_Zigbee);
					//}
	   			}
				else//If no Ack are Received(retry just 1 time!)@version3
				{
					/* Send Query Command to Routers Again */ 
					//USART1_Send_Byte(StartByte_Zigbee
					//USART1_Send_Byte(i);
					//_delay_ms(5);
					//USART1_Send_Byte(0xFF);//Command 
					//USART1_Send_Byte(EndByte_Zigbee);

					/* Transmit Received data to Bluetooth end */
					USART0_Send_Byte(StartByte_Zigbee);//_delay_ms(10);
					USART0_Send_Byte(i);//_delay_ms(10);
					USART0_Send_Byte(0);
					USART0_Send_Byte(0);//_delay_ms(10);
					USART0_Send_Byte(0);//_delay_ms(10);
					USART0_Send_Byte(0);
					USART0_Send_Byte(0x50);//type indicator
					USART0_Send_Byte(EndByte_Zigbee);//_delay_ms(10);		
				}
			}//end of for loop
			_delay_ms(500);
			USART0_Send_Byte(StartByte_Zigbee);//_delay_ms(10);
			USART0_Send_Byte(i);//_delay_ms(10);
			USART0_Send_Byte(0);
			USART0_Send_Byte(0);//_delay_ms(10);
			USART0_Send_Byte(0);//_delay_ms(10);
			USART0_Send_Byte(0);
			USART0_Send_Byte(0x80);//end indicator
			USART0_Send_Byte(EndByte_Zigbee);//_delay_ms(10);
			RealTimeQuery = 0;
			break;	
		}
				
		default:{
			//no ideal what to do yet!
		}
		
	}//end of switch function
	
	/* Read Data of Specified Router Immediately */
	if(recBuffer_Bluetooth[0] > 0x30)
	{
		RealTimeQuery = 1;	
		/* if the data is cached */
		if(cache_ttl[recBuffer_Bluetooth[0] - 0x30] > 0) {
			/* Transmit cached data to Bluetooth end */
			USART0_Send_Byte(StartByte_Zigbee);//_delay_ms(10);
			USART0_Send_Byte(recBuffer_Bluetooth[0] - 0x30);//_delay_ms(10);
			cache_temp = cache_current[recBuffer_Bluetooth[0] - 0x30] * 100;
			USART0_Send_Byte(cache_temp / 256);_delay_ms(50);
			USART0_Send_Byte(cache_temp % 256);//_delay_ms(10);
			cache_temp = cache_voltage[recBuffer_Bluetooth[0] - 0x30] * 100;
			USART0_Send_Byte(cache_temp / 256);//_delay_ms(10);
			USART0_Send_Byte(cache_temp % 256);_delay_ms(50);
			USART0_Send_Byte(EndByte_Zigbee);
			//USART0_Send_Byte(0x22);	//for debug
			USART0_Send_Byte(0xBB);//End byte
		}
		else {
			/* Query Certain Router */
			/* Send Query Command to Routers */				
			USART1_Send_Byte(StartByte_Zigbee);
			USART1_Send_Byte(recBuffer_Bluetooth[0] - 0x30);
			USART1_Send_Byte(ZigbeeQueryByte);//Command Byte
			USART1_Send_Byte(EndByte_Zigbee);
			/* Wait for ACK */
			_delay_ms(400);//Only delay for 100 milisecond
			if(1 == recFlag_Zigbee)
			{
				recFlag_Zigbee = 0;
				/* --- Step 1: Send ACK_Zigbee to ZigBee router --- */
				/* then router stop send data to coordinator */
				ACK_Zigbee[1] = recBuffer_Zigbee[0];//router device id
				ACK_Zigbee[2] = recBuffer_Zigbee[1];//leak current high byte
				ACK_Zigbee[3] = recBuffer_Zigbee[2];//leak current low byte
				for(i = 0;i < Zigbee_AckLen;i++)
				{
					USART1_Send_Byte(ACK_Zigbee[i]);
				}
				//subtract start and end byte of received data
				if((recNum_Zigbee == (Zigbee_PackLen - 2))&&(recBuffer_Zigbee[0] == (recBuffer_Bluetooth[0] - 0x30))&&(RealTimeQuery == 1))
				{
					/* Transmit Received data to Bluetooth end */
					USART0_Send_Byte(StartByte_Zigbee);_delay_ms(1);
					USART0_Send_Byte(recBuffer_Zigbee[0]);_delay_ms(1);
					USART0_Send_Byte(recBuffer_Zigbee[1]);_delay_ms(1);
					USART0_Send_Byte(recBuffer_Zigbee[2]);_delay_ms(1);
					USART0_Send_Byte(recBuffer_Zigbee[3]);_delay_ms(1);
					USART0_Send_Byte(recBuffer_Zigbee[4]);_delay_ms(1);
					USART0_Send_Byte(EndByte_Zigbee);
					USART0_Send_Byte(0xBB);//End byte
				}
			}
			else//If no Ack are Received
			{
				/* Transmit Received data to Bluetooth end */
				USART0_Send_Byte(StartByte_Zigbee);_delay_ms(1);
				USART0_Send_Byte(recBuffer_Bluetooth[0] - 0x30);_delay_ms(1);
				USART0_Send_Byte(0);_delay_ms(1);
				USART0_Send_Byte(0);_delay_ms(1);
				USART0_Send_Byte(0);_delay_ms(1);
				USART0_Send_Byte(0);_delay_ms(1);
				USART0_Send_Byte(EndByte_Zigbee);_delay_ms(1);
				USART0_Send_Byte(0xBB);//End byte				
			}
			RealTimeQuery = 0;
		}
	}
		
	recBuffer_Bluetooth[0] = 0;//CLear Buffer
}

int main()
{
    volatile unsigned char i = 0;
	volatile unsigned char r_staus;
	volatile unsigned t;
	
	cli();

	/* Initialization */
	TWI_Init();
	USART0_Init(38400);//Initialize USART0 with baud rate of 38400
	USART1_Init(38400);//Initialize USART1 with baud rate of 38400
	Timer0_Init();
	InitWatchDogTimer();
	initIO();
	
	sei();            //Enable Gloabal Interrupt
	_delay_ms(50);

	#ifdef DEBUG
	USART0_Send_Byte(0x55);//for debug Watch Dog Timer
	#endif

	while(1)
	{
		readButtonSatus();
		t = checkStatus();
		/* If Valid Data have been Received From Zigbee */
		if(1 == recFlag_Zigbee)
		{
			cli();	//clear global interrupt
			recFlag_Zigbee = 0;
			/* --- Step 1: Send ACK_Zigbee to ZigBee router --- */
			/* then router stop send data to coordinator */
			ACK_Zigbee[1] = recBuffer_Zigbee[0];//router device id
			ACK_Zigbee[2] = recBuffer_Zigbee[1];//leak current high byte
			ACK_Zigbee[3] = recBuffer_Zigbee[2];//leak current low byte
			for(i = 0;i < Zigbee_AckLen;i++)
			{
				/* Send Acknowledgement Packet to Router */
				USART1_Send_Byte(ACK_Zigbee[i]);
			}
			/* Store Received Data to EEPROM */
			if(recNum_Zigbee == (Zigbee_PackLen - 1))//added 1 byte (07-15-2015)
			{
				LEDON();
				if(RealTimeQuery == 0)StoreZigbeeReceivedData();//Ignore Query Staus
				LEDOFF();
				//USART0_Send_Byte(0x35);
				//USART0_Send_Byte(0x0A);
			}

			/* clear receive buffer @ 06_09 */
			for (i = 0; i < recBufferSize_Zigbee; ++i)
			{
				recBuffer_Zigbee[i] = 0;
			}
			sei();	//set global interrupt
	   	}
		/* If Valid Data have been Received From Bluetooth */
		if(1 == recFlag_Bluetooth)
		{
			recFlag_Bluetooth = 0;
			//subtract start and end byte of received data
			if(1 == recNum_Bluetooth)ReadCommandFromBluetooth();	
	   	}
	   	_delay_ms(1);
	

	}
	return 0;
}
