 /*
 *Verifikovane 15.4.2022
 */

#include <avr/io.h>
#include <avr/fuse.h>


#define F_CPU 8000000UL  // 8 MHz
#include <util/delay.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>


#define setBit(reg, bit) (reg = reg | (1 << bit))
#define clearBit(reg, bit) (reg = reg & ~(1 << bit))
#define toggleBit(reg, bit) (reg = reg ^ (1 << bit))
#define clearFlag(reg, bit) (reg = reg | (1 << bit))

#define DATA_595 PD0    //RCLK
#define STROBE_595 PD1  //SRC
#define CLK_595 PD2     //SRCLK  
#define PERIODE 50      //delay_us() for snhc595 bit shifter

#define ADR_DAY 0
#define ADR_MONTH 1
#define ADR_YEAR 2 // consist of 2 bytes (2,3) for save first_year

void update_date();
void day_increment();
void light_digit();
void number_to_digits(uint32_t number);
void init_timer2();
void system_init();
void clock();
void strobe();
void data_submit(uint32_t data);
void calcute_days();
void set_date();
void set_actual_date();
void check_button(unsigned char n);
void check_setup();
void EEPROM_write(uint16_t uiAddress, unsigned char ucData);
unsigned char EEPROM_read(uint16_t uiAddress);
void read_first_date();
void write_first_date();
void interrupt_pd5();

uint16_t wake_up = 0;

uint32_t actual_day = 2;
uint32_t actual_month = 1;
uint32_t actual_year = 1980;

uint32_t first_day = 1;
uint32_t first_month = 1;
uint32_t first_year = 2020;

uint32_t a = 0;
uint32_t b = 0;

uint32_t new_date[3];
uint32_t press_time;
const uint32_t max_date[3] = {31, 12, 2030};
const uint32_t min_date[3] = {1, 1, 1980};

uint32_t sec_count = 0;
uint32_t days = 0;

uint32_t number_in_digits[5] = {0,0,0,0,0};
const uint32_t month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
const uint32_t seg_table[] = {
	//pgcdebfa common cathode
	0b00111111,	 // 0
	0b00100100,	 // 1
	0b01011101,	 // 2
	0b01110101,	 // 3
	0b01100110,	 // 4
	0b01110011,	 // 5
	0b01111011,	 // 6
	0b00100101,	 // 7
	0b01111111,	 // 8
	0b01110111,	 // 9
    0b00000000	 // 11   
};

void update_date(){
    if((actual_year % 4) == 0){
        if(actual_month == 2){
            if(actual_day > month[actual_month-1]+1){
                actual_day = 1;
                actual_month++;
                if(actual_month > 12){
                    actual_month = 1;
                    actual_year++;
                }
            }
        }
        else{
            if(actual_day > month[actual_month-1]){
                actual_day = 1;
                actual_month++;
                if(actual_month > 12){
                    actual_month = 1;
                    actual_year++;
                }
            }
        }
    }
    else{
        if(actual_day > month[actual_month-1]){
            actual_day = 1;
            actual_month++;
            if(actual_month > 12){
                actual_month = 1;
                actual_year++;
            }
        }
    }
}

void day_increment(){
    sec_count++;               
                              //2685249 ,2721600-36351=24h 
    if(sec_count >= 2685249){ //2721600 1 day 3150 = 100sec 
        days++;
        sec_count = 0;
        number_to_digits(days);
    }
}

ISR(PCINT2_vect) {
    wake_up = 500;
}


/*
 This Function is interupt vector for timer2 compare to COMPA
 */
ISR(TIMER2_COMPA_vect){    
    day_increment();
    update_date();
}

void light_digit(){
    const uint32_t digit[5] = {0xfe,0xfd,0xfb,0xf7,0xef}; //digit[0] prvy z lava
    for(unsigned char i = 0; i <= 4; i++){
        data_submit(seg_table[number_in_digits[i]]);
        PORTB = digit[i];
        _delay_ms(1);
        PORTB = 0xff;//help with no showing number in previous digit
        }
}

void number_to_digits(uint32_t number){
    const uint32_t divider[5] = {10000,1000,100,10,1};  //delic pre ulozenie poctu dni po cisliciach do pola
    uint32_t count = 0;       //pocitadlo pre pocet cislicu ulozenu do pola

    /////////////////////////////////////////////////////////////////////////////
    ////////////ulozenie poctu dni po cisliciach do pola/////////////////////////
    for (unsigned char i = 0; i <= 4; i++) {
      while (number >= divider[i]) {
        number -= divider[i];
        count++;
      }
      number_in_digits[4-i] = count;
      count = 0;
    }
    /////////////////////////////////////////////////////////////////////////////
    /////Maska pre nuly nula=nesvieti/////////////////////////////////////////
    for (unsigned char i = 0; i <= 4; i++) {
      if (number_in_digits[4-i] == 0)
        number_in_digits[4-i] = 10;
      else
        break;
    }
 }

/*
 This Function configurate timer2
 */
void init_timer2(){
    //CTC mode
    clearBit(TCCR2A, WGM20);
    setBit(TCCR2A, WGM21);
    clearBit(TCCR2B, WGM22);
    
    // Configure clock source to be clock io at 1024 pre-scale
    setBit(TCCR2B, CS20);				
	setBit(TCCR2B, CS21);
	setBit(TCCR2B, CS22);
    
}
/*
 This Function is for system initialisations.
 */
void system_init(){
    DDRD = 0x07;
    DDRB = 0x3F; //set pins 12-16  as output 
    }
/*
 *This function will enable the Clock.
 */
void clock(){
    PORTD |= (1 << CLK_595);
    _delay_us(PERIODE);
    PORTD ^= (1 << CLK_595);
   _delay_us(PERIODE);
}

/*
 *This function will strobe and enable the output trigger.
 */

void strobe(){
    PORTD |= (1 << STROBE_595);
    _delay_us(PERIODE);
    PORTD ^= (1 << STROBE_595);
    }

/*
 * This function will send the data to shift register
 */
void data_submit(uint32_t data){
    for (unsigned char i=0 ; i<8 ; i++){
        PORTD = (((data >> i) & 0x01) << DATA_595);
        clock();
    }
    strobe(); // Data finally submitted 
}

void calcute_days() {
   
    uint32_t count_days = 0;
    uint32_t dif_day = first_day;
    uint32_t dif_month = first_month;
    uint32_t dif_year = first_year;
    
    
    if(dif_day > actual_day){
        count_days += month[dif_month - 1] - dif_day;
        if((dif_year % 4 == 0) && (dif_month <= 2)){
            count_days += 1;   
        }
        dif_day = 0;
        dif_month++;
    }
    if(dif_year < actual_year){
        while(dif_month-1 != 12){
            count_days += month[dif_month - 1];
            if((dif_year % 4 == 0) && (dif_month <= 2)){
                count_days += 1;   
            }
            dif_month++;   
        }
        dif_month = 1;
        dif_year++;
    }
    
    while(dif_year < actual_year){
        if(dif_year % 4 == 0){
            count_days += 366; 
            dif_year++;
        }
        else{
            count_days += 365;
            dif_year++;
        }
    }
    count_days += actual_day - dif_day;
    while(dif_month < actual_month){
        count_days += month[dif_month - 1];
        if((dif_year % 4 == 0) && (dif_month <= 2)){
            count_days += 1;   
        }
        dif_month++;  
    }
    days = count_days;
    number_to_digits(days);
    }

void set_date(){
    for(unsigned char x = 0; x < 3; x++){
        check_button(x);
    }
    first_day = new_date[0];
    first_month = new_date[1];
    first_year = new_date[2];
}

void set_actual_date(){
    number_to_digits(88888);
    while (1) {
      press_time = 0;
      while(PIND & (1 << PD5)){
        press_time ++;
        light_digit();
      }
      if (press_time >= 100) {
            break;
      }
      else if ((press_time > 2) && (press_time < 100)) {
            break;
      }
      light_digit();
    }
      
    for(unsigned char x = 0; x < 3; x++){
        check_button(x);
    }
    actual_day = new_date[0];
    actual_month = new_date[1];
    actual_year = new_date[2];
}

void check_button(unsigned char n) {
  new_date[n] = min_date[n];
  number_to_digits(new_date[n]);
  while (1) {
    press_time = 0;
    while(PIND & (1 << PD5)){
      press_time ++;
      light_digit();
    }
    if (press_time >= 100) {
      return;
    }
    else if ((press_time > 2) && (press_time < 100)) {
      new_date[n]++;
      if (new_date[n] > max_date[n]) {
        new_date[n] = min_date[n];
      }
      number_to_digits(new_date[n]);
    }
    light_digit();
  }
}

void check_setup() {
  press_time = 0;
  number_to_digits(days);
  while (PIND & (1 << PD5)){
    press_time ++;
    light_digit();
  }
  if (press_time >= 100) {
    cli(); //disable interupt
    set_date();
    write_first_date();
    calcute_days();
    sei(); //enable interupt
  }
}

void EEPROM_write(uint16_t uiAddress, unsigned char ucData){
/* Wait for completion of previous write */
while(EECR & (1<<EEPE));
/* Set up address and Data Registers */
EEAR = uiAddress;
EEDR = ucData;
/* Write logical one to EEMPE */
EECR |= (1<<EEMPE);
/* Start eeprom write by setting EEPE */
EECR |= (1<<EEPE);
}

unsigned char EEPROM_read(uint16_t uiAddress){
/* Wait for completion of previous write */
while(EECR & (1<<EEPE));
/* Set up address register */
EEAR = uiAddress;
/* Start eeprom read by writing EERE */
EECR |= (1<<EERE);
/* Return data from Data Register */
return EEDR;
}

void read_first_date(){
    first_day = EEPROM_read(ADR_DAY);
    first_month = EEPROM_read(ADR_MONTH);
    a = EEPROM_read(ADR_YEAR);
    b = EEPROM_read(ADR_YEAR + 1);
    first_year = (b << 8) + a;
}

void write_first_date(){
    EEPROM_write(ADR_DAY, first_day);
    EEPROM_write(ADR_MONTH, first_month);
    EEPROM_write(ADR_YEAR,first_year & 0x000000FF);
    EEPROM_write((ADR_YEAR + 1),(first_year >> 8) & 0x000000FF);
}

void interrupt_pd5(){
    PCICR = (1 << PCIE2); // interrupts from C and D
    PCMSK2 = (1 << PD5); // bit 2 on D will interrupt
}

int main(void) { 
    system_init(); // set output and input pins
    interrupt_pd5();

    if((actual_day == 0) || (actual_month == 0) || (actual_year == 0)){
       set_actual_date();
    }
    read_first_date();
    
    if((first_day == 255) || (first_month == 255) || (first_year == 255)){
        set_date();
        write_first_date();
    } 
    
    calcute_days();
    number_to_digits(days);
    
    //setBit(ASSR, AS2); //external clock source
    sei();  //enable all interupts
    init_timer2(); 
    OCR2A = 251; //value to comapare timer2 251 =0.032s for 8MHz, timer2 191 = 6s for 32KHz
    setBit(TIMSK2, OCIE2A); //enable timer2 interupt match to OCR2A 
    
    //init sleep mode
    SMCR |= (3<<SM0);   //set power save mode
    
    
    wake_up = 500;
    while(1){
        while(wake_up > 0){
            light_digit();
            check_setup();
            wake_up--;
        }

        SMCR |= (1<<SE); //enable sleep mode
        sleep_mode();
        }
}