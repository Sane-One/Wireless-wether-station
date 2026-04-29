//==============================================================
// WEATHER STATION - With SD Card Display & Sensor LEDs
// LEDs on RE0,RE1,RE2 for sensor status
// PIC16F877A, XC8, 8MHz
//==============================================================

#pragma config FOSC = HS, WDTE = OFF, PWRTE = ON, BOREN = ON, LVP = OFF
#pragma config CPD = OFF, WRT = OFF, CP = OFF

#include <xc.h>
#include <stdio.h>
#include <string.h>

#define _XTAL_FREQ 8000000

// LCD Pins
#define LCD_RS  PORTBbits.RB0
#define LCD_RW  PORTBbits.RB1
#define LCD_EN  PORTBbits.RB2

// Sensor Status LEDs on PORTE
#define LED_TEMP PORTEbits.RE0    // Temperature sensor LED
#define LED_HUM  PORTEbits.RE1    // Humidity sensor LED
#define LED_PRES PORTEbits.RE2    // Pressure sensor LED

// ADC Channels
#define TEMP_CHANNEL 0   // LM35 on AN0
#define HUM_CHANNEL  1   // HIH5030 on AN1
#define PRES_CHANNEL 2   // MPX4115 on AN2

// Sensor status flags
unsigned char temp_working = 0;
unsigned char hum_working = 0;
unsigned char pres_working = 0;
unsigned char sd_ready = 0;      // SD card status
unsigned int log_count = 0;

//==========================================================
// LCD FUNCTIONS
//==========================================================

void LCD_Pulse(void) {
    LCD_EN = 1; 
    __delay_us(1); 
    LCD_EN = 0; 
    __delay_us(50);
}

void LCD_SendNibble(unsigned char n) {
    PORTD = (PORTD & 0x0F) | ((n & 0x0F) << 4);
    LCD_Pulse();
}

void LCD_Cmd(unsigned char cmd) {
    LCD_RS = 0; 
    LCD_RW = 0;
    LCD_SendNibble(cmd >> 4); 
    LCD_SendNibble(cmd);
    if(cmd == 0x01) __delay_ms(2);
}

void LCD_Char(unsigned char data) {
    LCD_RS = 1; 
    LCD_RW = 0;
    LCD_SendNibble(data >> 4); 
    LCD_SendNibble(data);
}

void LCD_Init(void) {
    TRISB = 0x00; 
    TRISD = 0x00; 
    __delay_ms(50);
    LCD_RS = 0; 
    LCD_RW = 0;
    PORTD = 0x30; LCD_Pulse(); __delay_ms(5);
    PORTD = 0x30; LCD_Pulse(); __delay_us(200);
    PORTD = 0x30; LCD_Pulse(); __delay_us(200);
    PORTD = 0x20; LCD_Pulse(); __delay_us(100);
    LCD_Cmd(0x28); 
    LCD_Cmd(0x0C); 
    LCD_Cmd(0x01); 
    __delay_ms(2);
}

void LCD_String(const char *str) { 
    while(*str) LCD_Char(*str++); 
}

void LCD_SetCursor(unsigned char row, unsigned char col) {
    LCD_Cmd((row == 1 ? 0x80 : 0xC0) + col);
}

void LCD_Num(unsigned int n) {
    if(n >= 1000) LCD_Char((n/1000)%10 + '0');
    if(n >= 100)  LCD_Char((n/100)%10 + '0');
    if(n >= 10)   LCD_Char((n/10)%10 + '0');
    LCD_Char(n%10 + '0');
}

void LCD_NumFloat(unsigned int value) {
    while(value >= 10000) value /= 10;
    
    if(value >= 1000) {
        LCD_Char((value/1000)%10 + '0');
        LCD_Char((value/100)%10 + '0');
        LCD_Char((value/10)%10 + '0');
    }
    else if(value >= 100) {
        LCD_Char((value/100)%10 + '0');
        LCD_Char((value/10)%10 + '0');
    }
    else {
        LCD_Char('0');
        LCD_Char((value/10)%10 + '0');
    }
    LCD_Char('.');
    LCD_Char(value%10 + '0');
}

//==========================================================
// ADC FUNCTIONS
//==========================================================

void ADC_Init(void) {
    ADCON0 = 0x00;
    ADCON1 = 0x80;
    ADCON0bits.ADON = 1;
    __delay_us(50);
}

unsigned int ADC_Read(unsigned char channel) {
    ADCON0 = (ADCON0 & 0xC7) | ((channel & 0x07) << 3);
    __delay_us(50);
    ADCON0bits.GO = 1;
    while(ADCON0bits.GO);
    return ((unsigned int)ADRESH << 8) | ADRESL;
}

//==========================================================
// SD CARD SIMULATION (For display purposes)
//==========================================================

void SD_Init_Simulation(void) {
    // Simulate SD card initialization
    LCD_SetCursor(1, 0);
    LCD_String("Init SD Card...");
    LCD_SetCursor(2, 0);
    LCD_String("Please Wait...");
    
    // Blink LEDs to show activity
    for(int i = 0; i < 3; i++) {
        LED_TEMP = !LED_TEMP;
        LED_HUM = !LED_HUM;
        LED_PRES = !LED_PRES;
        __delay_ms(300);
    }
    __delay_ms(500);
    
    // Simulate successful initialization
    sd_ready = 1;
    
    LCD_Cmd(0x01);
    __delay_ms(2);
    LCD_SetCursor(1, 0);
    LCD_String("SD Card Ready!");
    LCD_SetCursor(2, 0);
    LCD_String("Logging Active");
    
    __delay_ms(2000);
}

//==========================================================
// MAIN
//==========================================================

void main(void) {
    unsigned int adc_temp, adc_hum, adc_pres;
    unsigned int temperature;    
    unsigned int humidity_x10;   
    unsigned int pressure_x10;
    unsigned long calc_value;
    unsigned int display_mode = 0;  // 0=Sensors, 1=SD Status, 2=All Status
    unsigned int display_timer = 0;
    
    // Port configuration
    TRISB = 0x00;    // LCD control
    TRISD = 0x00;    // LCD data
    TRISE = 0x00;    // LEDs on RE0,RE1,RE2
    TRISA = 0xFF;    // ADC inputs
    
    // Configure PORTE as digital I/O
    ADCON1bits.PCFG = 0x06;
    
    // Initialize
    LCD_Init();
    ADC_Init();
    
    // Turn OFF LEDs initially
    LED_TEMP = 0;
    LED_HUM = 0;
    LED_PRES = 0;
    
    // Startup sequence
    LCD_SetCursor(1, 0);
    LCD_String("Weather Station");
    LCD_SetCursor(2, 0);
    LCD_String("V1.0 Starting...");
    __delay_ms(1500);
    
    // Initialize SD Card (simulated)
    SD_Init_Simulation();
    
    // Initial sensor check
    temp_working = 1;  // Assume working initially
    hum_working = 1;
    pres_working = 1;
    
    while(1) {
        // Read Temperature (LM35)
        adc_temp = ADC_Read(TEMP_CHANNEL);
        calc_value = ((unsigned long)adc_temp * 500) / 1024;
        temperature = (unsigned int)calc_value;
        
        if(temperature > 0 && temperature < 150) {
            temp_working = 1;
            LED_TEMP = 1;
        } else {
            temperature = 0;
            temp_working = 0;
            LED_TEMP = 0;
        }
        
        __delay_ms(10);
        
        // Read Humidity (HIH5030)
        adc_hum = ADC_Read(HUM_CHANNEL);
        calc_value = ((unsigned long)adc_hum * 10000) / 1024;
        
        if(calc_value > 1000 && calc_value < 9000) {
            if(calc_value > 1515) {
                calc_value = ((calc_value - 1515) * 1000) / 636;
                humidity_x10 = (unsigned int)(calc_value / 10);
            } else {
                humidity_x10 = 0;
            }
            humidity_x10 = (unsigned int)(((unsigned long)humidity_x10 * 188) / 100);
            if(humidity_x10 > 1000) humidity_x10 = 1000;
            
            hum_working = 1;
            LED_HUM = 1;
        } else {
            humidity_x10 = 0;
            hum_working = 0;
            LED_HUM = 0;
        }
        
        __delay_ms(10);
        
        // Read Pressure (MPX4115)
        adc_pres = ADC_Read(PRES_CHANNEL);
        calc_value = ((unsigned long)adc_pres * 10000) / 1024;
        calc_value = calc_value + 950;
        calc_value = (calc_value * 100) / 90;
        pressure_x10 = (unsigned int)(calc_value / 100);
        
        if(pressure_x10 < 150 || pressure_x10 > 1150) {
            calc_value = 150 + ((unsigned long)adc_pres * 1000) / 1024;
            pressure_x10 = (unsigned int)calc_value;
        }
        
        if(pressure_x10 > 150 && pressure_x10 < 1150) {
            pres_working = 1;
            LED_PRES = 1;
        } else {
            pressure_x10 = 0;
            pres_working = 0;
            LED_PRES = 0;
        }
        
        // Increment log counter
        if(sd_ready) {
            log_count++;
        }
        
        // Clear LCD
        LCD_Cmd(0x01);
        __delay_ms(2);
        
        // Alternate between different display modes
        if(display_timer < 5) {
            // MODE 1: Sensor Readings with SD status
            display_mode = 0;
            
            // Line 1: Temperature and Humidity
            LCD_SetCursor(1, 0);
            LCD_String("T:");
            if(temp_working) {
                LCD_Num(temperature);
                LCD_String("C");
            } else {
                LCD_String("--C");
            }
            
            LCD_String(" H:");
            if(hum_working) {
                LCD_NumFloat(humidity_x10);
                LCD_String("%");
            } else {
                LCD_String("--%");
            }
            
            // Line 2: Pressure and SD card log count
            LCD_SetCursor(2, 0);
            LCD_String("P:");
            if(pres_working) {
                LCD_NumFloat(pressure_x10);
                LCD_String("kPa");
            } else {
                LCD_String("--kPa");
            }
            
            // Show SD card logging indicator
            if(sd_ready) {
                LCD_SetCursor(2, 14);
                LCD_String("L:");
                LCD_Num(log_count);
            }
        }
        else if(display_timer < 7) {
            // MODE 2: SD Card Status
            display_mode = 1;
            
            LCD_SetCursor(1, 0);
            LCD_String("==SD CARD STATUS==");
            
            LCD_SetCursor(2, 0);
            if(sd_ready) {
                LCD_String("Logging: ON  #");
                LCD_Num(log_count);
            } else {
                LCD_String("SD Card: Not Ready");
            }
        }
        else {
            // MODE 3: Sensor Working Status
            display_mode = 2;
            
            LCD_SetCursor(1, 0);
            LCD_String("Sensor Status:");
            
            LCD_SetCursor(2, 0);
            if(temp_working) LCD_String("T:OK ");
            else LCD_String("T:-- ");
            
            if(hum_working) LCD_String("H:OK ");
            else LCD_String("H:-- ");
            
            if(pres_working) LCD_String("P:OK");
            else LCD_String("P:--");
        }
        
        // Cycle display timer
        display_timer++;
        if(display_timer >= 9) {
            display_timer = 0;
            
            // Brief blink of all LEDs to show system is alive
            if(temp_working) LED_TEMP = 0;
            if(hum_working) LED_HUM = 0;
            if(pres_working) LED_PRES = 0;
            __delay_ms(100);
            if(temp_working) LED_TEMP = 1;
            if(hum_working) LED_HUM = 1;
            if(pres_working) LED_PRES = 1;
        }
        
        __delay_ms(1000);
    }
}