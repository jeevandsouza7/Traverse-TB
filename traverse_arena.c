#define F_CPU 14745600
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include <math.h> //included to support power function
#include "lcd.h"

void port_init();
void timer5_init();
void velocity(unsigned char, unsigned char);
void motors_delay();

void line_follow();
void traverse(unsigned char);
void pickup_1() ;

unsigned char ADC_Conversion(unsigned char);
unsigned char ADC_Value;
unsigned char flag = 0;
unsigned char Left_white_line = 0;
unsigned char Center_white_line = 0;
unsigned char Right_white_line = 0;
unsigned char thr = 12;

// posenc
volatile unsigned long int ShaftCountLeft = 0; //to keep track of left position encoder
volatile unsigned long int ShaftCountRight = 0; //to keep track of right position encoder
volatile unsigned int Degrees; //to accept angle in degrees for turning/*

#define BAUD 9600
#define BRC ((F_CPU/BAUD/16)-1)

//XBee*********************************************

void USART_init(void)
{
    UBRR0 = BRC;

    UCSR0C = ((0<<USBS0)|(1 << UCSZ01)|(1<<UCSZ00));
    UCSR0B = ((1<<RXEN0)|(1<<TXEN0));
}

void USART_send( unsigned char data)
{
    //while the transmit buffer is not empty loop
    while(!(UCSR0A & (1<<UDRE0)));

    //when the buffer is empty write data to the transmitted
    UDR0 = data;
}


//*******************************xbee

//Function to configure LCD port
void lcd_port_config (void)
{
 DDRC = DDRC | 0xF7; //all the LCD pin's direction set as output
 PORTC = PORTC & 0x80; // all the LCD pins are set to logic 0 except PORTC 7
}



//
//Function to initialize Buzzer
void buzzer_pin_config (void)
{
 DDRC = DDRC | 0x08;        //Setting PORTC 3 as output
 PORTC = PORTC & 0xF7;      //Setting PORTC 3 logic low to turnoff buzzer
}

void buzzer_on (void)
{
 unsigned char port_restore = 0;
 port_restore = PINC;
 port_restore = port_restore | 0x08;
 PORTC = port_restore;
}

void buzzer_off (void)
{
 unsigned char port_restore = 0;
 port_restore = PINC;
 port_restore = port_restore & 0xF7;
 PORTC = port_restore;
}
//

//ADC pin configuration
void adc_pin_config (void)
{
 DDRF = 0x00;
 PORTF = 0x00;
 DDRK = 0x00;
 PORTK = 0x00;
}

//Function to configure ports to enable robot's motion
void motion_pin_config (void)
{
 DDRA = DDRA | 0x0F;
 PORTA = PORTA & 0xF0;
 DDRL = DDRL | 0x18;   //Setting PL3 and PL4 pins as output for PWM generation
 PORTL = PORTL | 0x18; //PL3 and PL4 pins are for velocity control using PWM.
}

//posenc
//Function to configure INT4 (PORTE 4) pin as input for the left position encoder
void left_encoder_pin_config (void)
{
    DDRE  = DDRE & 0xEF;  //Set the direction of the PORTE 4 pin as input
    PORTE = PORTE | 0x10; //Enable internal pull-up for PORTE 4 pin
}

//Function to configure INT5 (PORTE 5) pin as input for the right position encoder
void right_encoder_pin_config (void)
{
    DDRE  = DDRE & 0xDF;  //Set the direction of the PORTE 4 pin as input
    PORTE = PORTE | 0x20; //Enable internal pull-up for PORTE 4 pin
}

//Function to Initialize PORTS
void port_init()
{
	//lcd_port_config();
	adc_pin_config();
	motion_pin_config();
    left_encoder_pin_config(); //left encoder pin config
    right_encoder_pin_config(); //right encoder pin config
    buzzer_pin_config();
}


void left_position_encoder_interrupt_init (void) //Interrupt 4 enable
{
    cli(); //Clears the global interrupt
    EICRB = EICRB | 0x02; // INT4 is set to trigger with falling edge
    EIMSK = EIMSK | 0x10; // Enable Interrupt INT4 for left position encoder
    sei();   // Enables the global interrupt
}

void right_position_encoder_interrupt_init (void) //Interrupt 5 enable
{
    cli(); //Clears the global interrupt
    EICRB = EICRB | 0x08; // INT5 is set to trigger with falling edge
    EIMSK = EIMSK | 0x20; // Enable Interrupt INT5 for right position encoder
    sei();   // Enables the global interrupt
}

//ISR for right position encoder
ISR(INT5_vect)
{
    ShaftCountRight++;  //increment right shaft position count
}


//ISR for left position encoder
ISR(INT4_vect)
{
    ShaftCountLeft++;  //increment left shaft position count
}


void drop() {
    buzzer_on();
    _delay_ms(500);
    buzzer_off();
    _delay_ms(500);
}

// Timer 5 initialized in PWM mode for velocity control
// Prescale:256
// PWM 8bit fast, TOP=0x00FF
// Timer Frequency:225.000Hz
void timer5_init()
{
	TCCR5B = 0x00;	//Stop
	TCNT5H = 0xFF;	//Counter higher 8-bit value to which OCR5xH value is compared with
	TCNT5L = 0x01;	//Counter lower 8-bit value to which OCR5xH value is compared with
	OCR5AH = 0x00;	//Output compare register high value for Left Motor
	OCR5AL = 0xFF;	//Output compare register low value for Left Motor
	OCR5BH = 0x00;	//Output compare register high value for Right Motor
	OCR5BL = 0xFF;	//Output compare register low value for Right Motor
	OCR5CH = 0x00;	//Output compare register high value for Motor C1
	OCR5CL = 0xFF;	//Output compare register low value for Motor C1
	TCCR5A = 0xA9;	/*{COM5A1=1, COM5A0=0; COM5B1=1, COM5B0=0; COM5C1=1 COM5C0=0}
 					  For Overriding normal port functionality to OCRnA outputs.
				  	  {WGM51=0, WGM50=1} Along With WGM52 in TCCR5B for Selecting FAST PWM 8-bit Mode*/

	TCCR5B = 0x0B;	//WGM12=1; CS12=0, CS11=1, CS10=1 (Prescaler=64)
}

void adc_init()
{
	ADCSRA = 0x00;
	ADCSRB = 0x00;		//MUX5 = 0
	ADMUX = 0x20;		//Vref=5V external --- ADLAR=1 --- MUX4:0 = 0000
	ACSR = 0x80;
	ADCSRA = 0x86;		//ADEN=1 --- ADIE=1 --- ADPS2:0 = 1 1 0
}

//Function For ADC Conversion
unsigned char ADC_Conversion(unsigned char Ch)
{
	unsigned char a;
	if(Ch>7)
	{
		ADCSRB = 0x08;
	}
	Ch = Ch & 0x07;
	ADMUX= 0x20| Ch;
	ADCSRA = ADCSRA | 0x40;		//Set start conversion bit
	while((ADCSRA&0x10)==0);	//Wait for conversion to complete
	a=ADCH;
	ADCSRA = ADCSRA|0x10; //clear ADIF (ADC Interrupt Flag) by writing 1 to it
	ADCSRB = 0x00;
	return a;
}

/*
//Function To Print Sesor Values At Desired Row And Coloumn Location on LCD
void print_sensor(char row, char coloumn,unsigned char channel)
{

	ADC_Value = ADC_Conversion(channel);
	lcd_print(row, coloumn, ADC_Value, 3);
}
*/
//Function for velocity control
void velocity (unsigned char left_motor, unsigned char right_motor)
{
	OCR5AL = (unsigned char)left_motor;
	OCR5BL = (unsigned char)right_motor;
}

//Function To Print Sesor Values At Desired Row And Coloumn Location on LCD
void print_sensor(char row, char coloumn,unsigned char channel)
{

    ADC_Value = ADC_Conversion(channel);
    lcd_print(row, coloumn, ADC_Value, 3);
}

//Function used for setting motor's direction
void motion_set (unsigned char Direction)
{
 unsigned char PortARestore = 0;

 Direction &= 0x0F; 		// removing upper nibbel for the protection
 PortARestore = PORTA; 		// reading the PORTA original status
 PortARestore &= 0xF0; 		// making lower direction nibbel to 0
 PortARestore |= Direction; // adding lower nibbel for forward command and restoring the PORTA status
 PORTA = PortARestore; 		// executing the command
}

void forward (void)
{
  motion_set (0x06);
}

void left (void) //Left wheel backward, Right wheel forward
{
  motion_set(0x05);
}

void right (void) //Left wheel forward, Right wheel backward
{
  motion_set(0x0A);
}

void soft_left (void) //Left wheel stationary, Right wheel forward
{
    motion_set(0x04);
}

void soft_right (void) //Left wheel forward, Right wheel is stationary
{
    motion_set(0x02);
}

void soft_left_2 (void) //Left wheel backward, right wheel stationary
{
    motion_set(0x01);
}

void soft_right_2 (void) //Left wheel stationary, Right wheel backward
{
    motion_set(0x08);
}

void stop (void)
{
    motion_set(0x00);
}

//Function used for turning robot by specified degrees
void angle_rotate(unsigned int Degrees)
{
    float ReqdShaftCount = 0;
    unsigned long int ReqdShaftCountInt = 0;

    ReqdShaftCount = (float) Degrees/ 4.090; // division by resolution to get shaft count
    ReqdShaftCountInt = (unsigned int) ReqdShaftCount;
    ShaftCountRight = 0;
    ShaftCountLeft = 0;

    while (1)
    {
        if((ShaftCountRight >= ReqdShaftCountInt) | (ShaftCountLeft >= ReqdShaftCountInt))
        break;
    }
    stop(); //Stop robot
}

//Function used for moving robot forward by specified distance

void linear_distance_mm(unsigned int DistanceInMM)
{
    float ReqdShaftCount = 0;
    unsigned long int ReqdShaftCountInt = 0;

    ReqdShaftCount = DistanceInMM / 5.338; // division by resolution to get shaft count
    ReqdShaftCountInt = (unsigned long int) ReqdShaftCount;

    ShaftCountRight = 0;
    while(1)
    {
        if(ShaftCountRight > ReqdShaftCountInt)
        {
            break;
        }
    }
    stop(); //Stop robot
}

void forward_mm(unsigned int DistanceInMM)
{
    forward();
    linear_distance_mm(DistanceInMM);
}
/*
void back_mm(unsigned int DistanceInMM)
{
    back();
    linear_distance_mm(DistanceInMM);
}
*/

void left_degrees(unsigned int Degrees)
{
    // 88 pulses for 360 degrees rotation 4.090 degrees per count
    left(); //Turn left
    angle_rotate(Degrees);
}



void right_degrees(unsigned int Degrees)
{
    // 88 pulses for 360 degrees rotation 4.090 degrees per count
    right(); //Turn right
    angle_rotate(Degrees);
}


void soft_left_degrees(unsigned int Degrees)
{
    // 176 pulses for 360 degrees rotation 2.045 degrees per count
    soft_left(); //Turn soft left
    Degrees=Degrees*2;
    angle_rotate(Degrees);
}

void soft_right_degrees(unsigned int Degrees)
{
    // 176 pulses for 360 degrees rotation 2.045 degrees per count
    soft_right();  //Turn soft right
    Degrees=Degrees*2;
    angle_rotate(Degrees);
}

void soft_left_2_degrees(unsigned int Degrees)
{
    // 176 pulses for 360 degrees rotation 2.045 degrees per count
    soft_left_2(); //Turn reverse soft left
    Degrees=Degrees*2;
    angle_rotate(Degrees);
}

void soft_right_2_degrees(unsigned int Degrees)
{
    // 176 pulses for 360 degrees rotation 2.045 degrees per count
    soft_right_2();  //Turn reverse soft right
    Degrees=Degrees*2;
    angle_rotate(Degrees);
}

void line_follow() {
        Left_white_line = ADC_Conversion(3);    //Getting data of Left WL Sensor
        Center_white_line = ADC_Conversion(2);  //Getting data of Center WL Sensor
        Right_white_line = ADC_Conversion(1);   //Getting data of Right WL Sensor

       // flag=0;

        print_sensor(1,1,3);    //Prints value of White Line Sensor1
        print_sensor(1,5,2);    //Prints Value of White Line Sensor2
        print_sensor(1,9,1);    //Prints Value of White Line Sensor3



        if(Center_white_line>=thr)
        {
            flag=1;
            forward();
            velocity(250,250);
        }

        if((Left_white_line<thr)&&(Right_white_line>=thr) && (Center_white_line<thr))
        {
            flag=1;
            forward();
            velocity(150,120);
        }

        if((Right_white_line<thr) && (Left_white_line>=thr)&&(Center_white_line<thr))
        {
            flag=1;
            forward();
            velocity(120,150);
        }

        if(Center_white_line<thr && Left_white_line<thr && Right_white_line<thr)
        {
            //forward();
            if(flag<3){
            right();
            velocity(100,100);
            flag++;
            }
            else if(flag<12){
            left();
            velocity(100,100);
            flag++;
            }
            else if(flag<15) {
            right();
            velocity(100,100);
            flag++;
            }
            else{
            forward();
            velocity(0,0);
            }
        }
}

void line_follow_1(unsigned char wheel_speed) {
        Left_white_line = ADC_Conversion(3);    //Getting data of Left WL Sensor
        Center_white_line = ADC_Conversion(2);  //Getting data of Center WL Sensor
        Right_white_line = ADC_Conversion(1);   //Getting data of Right WL Sensor

       // flag=0;

        print_sensor(1,1,3);    //Prints value of White Line Sensor1
        print_sensor(1,5,2);    //Prints Value of White Line Sensor2
        print_sensor(1,9,1);    //Prints Value of White Line Sensor3



        if(Center_white_line>=thr)
        {
            flag=1;
            forward();
            velocity(wheel_speed,wheel_speed);
        }

        if((Left_white_line<thr)&&(Right_white_line>=thr) && (Center_white_line<thr))
        {
            flag=1;
            forward();
            velocity(wheel_speed,wheel_speed-30);
        }

        if((Right_white_line<thr) && (Left_white_line>=thr)&&(Center_white_line<thr))
        {
            flag=1;
            forward();
            velocity(wheel_speed-30,wheel_speed);
        }

        if(Center_white_line<thr && Left_white_line<thr && Right_white_line<thr)
        {
            //forward();
            if(flag<3){
            right();
            velocity(80,80);
            flag++;
            }
            else if(flag<12){
            left();
            velocity(80,80);
            flag++;
            }
            else if(flag<15) {
            right();
            velocity(80,80);
            flag++;
            }
            else{
            forward();
            velocity(0,0);
            }
        }
}


void temp_fn() {

    right_degrees(45);

    while(1) {
        if(ADC_Conversion(1)>thr||ADC_Conversion(2)>thr||ADC_Conversion(3)>thr)
            break;
    }

    while(flag<15) {
    line_follow();
	}

	while(1) {
	right();
	velocity(100,100);
	if(ADC_Conversion(1)>thr||ADC_Conversion(2)>thr||ADC_Conversion(3)>thr)
		break;

	}

    // A
    unsigned char count = 3;
    uint8_t turn=1;
    while(count) {
        line_follow_1(150);

        if(count==1&&turn==1){
            pickup_1();
            turn=0;
        }

        if((ADC_Conversion(1)>thr || ADC_Conversion(3)>thr) && ADC_Conversion(2)>thr ) {
            count--;
            forward();
            velocity(100,100);
            _delay_ms(1000);
        }
    }

    //B
	forward();
	velocity(0,0);
	right();
	velocity(100,100);
	_delay_ms(2000);

    while(1) {
        right();
        velocity(150,150);
        if(ADC_Conversion(1)>thr||ADC_Conversion(2)>thr||ADC_Conversion(3)>thr)
            break;
    }

    count = 2;

    while(count) {
        line_follow();
        if((ADC_Conversion(1)>thr || ADC_Conversion(3)>thr) && ADC_Conversion(2)>thr ) {
            count--;
            forward();
            velocity(100,100);
            _delay_ms(1000);
        }
    }

    unsigned char i=0;
    for(i=0;i<2;i++) {
        right();
        velocity(100,100);
        _delay_ms(1000);

        while(1) {
            right();
            velocity(150,150);
            if(ADC_Conversion(1)>thr||ADC_Conversion(2)>thr||ADC_Conversion(3)>thr)
                break;
        }
    }

    buzzer_on();
    _delay_ms(100);
    buzzer_off();
    _delay_ms(100);
	//velocity(0,0);

    // S
    count = 1;
     while(count) {
        line_follow();
        if((ADC_Conversion(1)>thr || ADC_Conversion(3)>thr) && ADC_Conversion(2)>thr ) {
            count--;
            forward();
            velocity(100,100);
            _delay_ms(1000);
        }
    }

    count=10;
    while(count){
        line_follow();
        count--;
    }

	velocity(0,0);

}

void traverse(unsigned char pick_up) {
    unsigned char rot,rot2,jn,temp,i;
    if(pick_up<7) {
        rot = (pick_up%2==0)?(pick_up/2):(pick_up+1)/2;
        for(i=0;i<rot;i++) {
            while(ADC_Conversion(1)>thr||ADC_Conversion(2)>thr||ADC_Conversion(3)>thr) {
                forward();
                velocity(130,50);
				_delay_ms(1000);
            }
            //stop();
            //_delay_ms(100);
            while(ADC_Conversion(1)<thr&&ADC_Conversion(2)<thr&&ADC_Conversion(3)<thr) {
                forward();
                velocity(130,50);
            }
            stop();
            _delay_ms(100);
        }//end for
/*
        jn=2;
        while(jn) {
            line_follow();
            if(ADC_Conversion(1)>thr && ADC_Conversion(3)>thr) {
                jn--;
                // A little forward
                forward();
                velocity(100,100);
                _delay_ms(50);
            }
        }// end while
        stop();
        _delay_ms(50);
*/

        while(ADC_Conversion(1)>thr||ADC_Conversion(2)>thr||ADC_Conversion(3)>thr) {
            line_follow();
        }
        stop();
        _delay_ms(50);
        while(ADC_Conversion(1)<thr && ADC_Conversion(2)<thr && ADC_Conversion(3)<thr){
            forward();
            velocity(130,50);
        }

        temp = 2-(pick_up%2); // pickup point
        while(temp) {
            line_follow();
            if(ADC_Conversion(3)>thr&&ADC_Conversion(2)>thr){
                temp--;
                // A little forward
                forward();
                velocity(100,100);
                _delay_ms(50);
            }
        }//end while

        stop();
        _delay_ms(50);

            pickup_1();

        while(ADC_Conversion(1)<thr&&ADC_Conversion(2)>thr){
            line_follow();
        }

        // may need some changes
        while(ADC_Conversion(3)>thr || ADC_Conversion(1)>thr || ADC_Conversion(2)<thr){
            forward();
            velocity(130,50);
        }
        stop();
        _delay_ms(50);
        jn=2;

        while(jn) {
            line_follow();
            if(ADC_Conversion(1)>thr && ADC_Conversion(3)>thr) {
                jn--;
                // A little forward
                forward();
                velocity(100,100);
                _delay_ms(50);
            }
        }//end while

        stop();
        _delay_ms(50);

        rot2 = 3-rot;
        for(i=0;i<rot2;i++) {
            while(ADC_Conversion(1)>thr||ADC_Conversion(2)>thr||ADC_Conversion(3)>thr) {
                forward();
                velocity(130,50);
            }
            stop();
            _delay_ms(100);
            while(ADC_Conversion(1)<thr&&ADC_Conversion(2)<thr&&ADC_Conversion(3)<thr) {
                forward();
                velocity(130,50);
            }
            stop();
            _delay_ms(100);
        }//end for

        jn = 1;
        while(jn) {
            line_follow();
            if(ADC_Conversion(1)>thr && ADC_Conversion(2)>thr && ADC_Conversion(3)>thr) {
                jn--;
            }
        }


        drop();

    } // end if

    else {
        rot = 7 - (pick_up%2==0)?(pick_up/2):(pick_up+1)/2;
        for(i=0;i<rot;i++) {
            while(ADC_Conversion(1)>thr || ADC_Conversion(2)>thr || ADC_Conversion(3)>thr) {
                forward();
                velocity(50,130);
            }
            stop();
            _delay_ms(100);
            while(ADC_Conversion(1)<thr && ADC_Conversion(2)<thr && ADC_Conversion(3)<thr) {
                forward();
                velocity(50,130);
            }
            stop();
            _delay_ms(100);
        }//end for

        jn=2;
        while(jn) {
            line_follow();
            if(ADC_Conversion(1)>thr && ADC_Conversion(3)>thr) {
                jn--;
                // A little forward
                forward();
                velocity(100,100);
                _delay_ms(50);
            }
        }// end while
        stop();
        _delay_ms(50);

        while(ADC_Conversion(1)>thr || ADC_Conversion(2)>thr || ADC_Conversion(3)>thr) {
            forward();
            velocity(100,100);
        }
        stop();
        _delay_ms(50);
        while(ADC_Conversion(1)<thr && ADC_Conversion(2)<thr && ADC_Conversion(3)<thr){
            forward();
            velocity(50,130);
        }

        temp = 2-(pick_up%2); // pickup point
        while(temp) {
            line_follow();
            if(ADC_Conversion(1)>thr && ADC_Conversion(2)>thr){
                temp--;
                // A little forward
                forward();
                velocity(100,100);
                _delay_ms(50);
            }
        }//end while

        stop();
        _delay_ms(50);
        //
            pickup_1();
        //
        while(ADC_Conversion(3)<thr && ADC_Conversion(2)>thr){
            line_follow();
        }

        // may need some changes
        while(ADC_Conversion(3)>thr || ADC_Conversion(1)>thr || ADC_Conversion(2)<thr){
            forward();
            velocity(50,130);
        }
        stop();
        _delay_ms(50);
        jn=2;

        while(jn) {
            line_follow();
            if(ADC_Conversion(1)>thr && ADC_Conversion(3)>thr) {
                jn--;
                // A little forward
                forward();
                velocity(100,100);
                _delay_ms(50);
            }
        }//end while

        stop();
        _delay_ms(50);

        rot2 = 3-rot;
        for(i=0;i<rot2;i++) {
            while(ADC_Conversion(1)>thr||ADC_Conversion(2)>thr||ADC_Conversion(3)>thr) {
                forward();
                velocity(50,130);
            }
            stop();
            _delay_ms(100);
            while(ADC_Conversion(1)<thr&&ADC_Conversion(2)<thr&&ADC_Conversion(3)<thr) {
                forward();
                velocity(50,130);
            }
            stop();
            _delay_ms(100);
        }//end for

        jn = 1;
        while(jn) {
            line_follow();
            if(ADC_Conversion(1)>thr && ADC_Conversion(2)>thr && ADC_Conversion(3)>thr) {
                jn--;
            }
        }

        //
        drop();
        //
    }
}

void init_devices (void)
{
 	cli(); //Clears the global interrupts
	port_init();
	adc_init();
	timer5_init();
    left_position_encoder_interrupt_init();
    right_position_encoder_interrupt_init();
	port_init1();
    timer1_init();
    USART_init();
	sei();   //Enables the global interrupts
}




//Configure PORTB 5 pin for servo motor 1 operation
void servo1_pin_config (void)
{
 DDRB  = DDRB | 0x20;  //making PORTB 5 pin output
 PORTB = PORTB | 0x20; //setting PORTB 5 pin to logic 1
}

//Configure PORTB 6 pin for servo motor 2 operation
void servo2_pin_config (void)
{
 DDRB  = DDRB | 0x40;  //making PORTB 6 pin output
 PORTB = PORTB | 0x40; //setting PORTB 6 pin to logic 1
}



//Initialize the ports
void port_init1(void)
{
 servo1_pin_config(); //Configure PORTB 5 pin for servo motor 1 operation
 servo2_pin_config(); //Configure PORTB 6 pin for servo motor 2 operation
}

//TIMER1 initialization in 10 bit fast PWM mode
//prescale:256
// WGM: 7) PWM 10bit fast, TOP=0x03FF
// actual value: 52.25Hz
void timer1_init(void)
{
 TCCR1B = 0x00; //stop
 TCNT1H = 0xFC; //Counter high value to which OCR1xH value is to be compared with
 TCNT1L = 0x01;	//Counter low value to which OCR1xH value is to be compared with
 OCR1AH = 0x03;	//Output compare Register high value for servo 1
 OCR1AL = 0xFF;	//Output Compare Register low Value For servo 1
 OCR1BH = 0x03;	//Output compare Register high value for servo 2
 OCR1BL = 0xFF;	//Output Compare Register low Value For servo 2
 OCR1CH = 0x03;	//Output compare Register high value for servo 3
 OCR1CL = 0xFF;	//Output Compare Register low Value For servo 3
 ICR1H  = 0x03;
 ICR1L  = 0xFF;
 TCCR1A = 0xAB; /*{COM1A1=1, COM1A0=0; COM1B1=1, COM1B0=0; COM1C1=1 COM1C0=0}
 					For Overriding normal port functionality to OCRnA outputs.
				  {WGM11=1, WGM10=1} Along With WGM12 in TCCR1B for Selecting FAST PWM Mode*/
 TCCR1C = 0x00;
 TCCR1B = 0x0C; //WGM12=1; CS12=1, CS11=0, CS10=0 (Prescaler=256)
}



//Function to rotate Servo 1 by a specified angle in the multiples of 1.86 degrees
void servo_1(unsigned char degrees)
{
 float PositionPanServo = 0;
  PositionPanServo = ((float)degrees / 1.86) + 35.0;
 OCR1AH = 0x00;
 OCR1AL = (unsigned char) PositionPanServo;
}


//Function to rotate Servo 2 by a specified angle in the multiples of 1.86 degrees
void servo_2(unsigned char degrees)
{
 float PositionTiltServo = 0;
 PositionTiltServo = ((float)degrees / 1.86) + 35.0;
 OCR1BH = 0x00;
 OCR1BL = (unsigned char) PositionTiltServo;
}



//servo_free functions unlocks the servo motors from the any angle
//and make them free by giving 100% duty cycle at the PWM. This function can be used to
//reduce the power consumption of the motor if it is holding load against the gravity.

void servo_1_free (void) //makes servo 1 free rotating
{
 OCR1AH = 0x03;
 OCR1AL = 0xFF; //Servo 1 off
}

void servo_2_free (void) //makes servo 2 free rotating
{
 OCR1BH = 0x03;
 OCR1BL = 0xFF; //Servo 2 off
}



//Main Function
int main()
{
	init_devices();
	lcd_set_4bit();
	lcd_init();

    temp_fn();
	//traverse(1);
}

void pickup_1() {
	velocity(250,250);
    left_degrees(90);
    _delay_ms(100);

    //ServoCode

 unsigned char i = 0;
 init_devices();

 for (i = 0; i <90; i++)
 {
  servo_1(i);
  _delay_ms(30);
 }
_delay_ms(1000);
for (i = 0; i <90; i++)
 {
  servo_2(i);
  _delay_ms(30);
 }

 _delay_ms(200);
 servo_1_free();
 servo_2_free();

    //ServoCodeEnd

    right_degrees(90);
    _delay_ms(100);
	velocity(150,150);
}
