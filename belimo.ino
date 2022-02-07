#define pwm 11
#define reg A3
#define THERMISTORPIN A2
#define THERMISTORNOMINAL 4700
#define TEMPERATURENOMINAL 25
#define NUMSAMPLES 5
#define BCOEFFICIENT 3950
#define SERIESRESISTOR 6700
//#include <EEPROM.h>
int samples[NUMSAMPLES];
int readreg;
int settemp;
int heat = 5;             // connect the red LED to the port 13
int cool = 6;             // We connect the green LED to the port12
int eko = 7;             // Connect the blue LED to the port 11
int ButPin = 0;               // Connecting the button to the output10
int flag = 0;                  // status flag
int regim;
int address = 0;
void setup(void) {
pinMode(pwm, OUTPUT);
pinMode(reg, INPUT);
pinMode(heat, OUTPUT);   
pinMode(cool, OUTPUT);   
pinMode(eko, OUTPUT);  
Serial.begin(9600);
analogReference(EXTERNAL);
regim = EEPROM.read(address);
}
void loop(void) {
readreg = analogRead(reg);
uint8_t i;
float average;
for (i=0; i< NUMSAMPLES; i++) {
samples[i] = analogRead(THERMISTORPIN);
delay(10);
}
average = 0;
for (i=0; i< NUMSAMPLES; i++) {
average += samples[i];
}
average /= NUMSAMPLES;
average = 1023 / average - 1;
average = SERIESRESISTOR / average;
float steinhart;
steinhart = average / THERMISTORNOMINAL; // (R/Ro)
steinhart = log(steinhart); // ln(R/Ro)
steinhart /= BCOEFFICIENT; // 1/B * ln(R/Ro)
steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
steinhart = 1.0 / steinhart; // инвертируем
steinhart -= 273.15; // convert to degrees Celsius
if(readreg <=10){
settemp=19;  
} 
if(readreg >=120){
settemp=20;  
}
if(readreg >=340){
settemp=21;  
} 
if(readreg >=500){// or readreg <=600){
settemp=22;  
} 
if(readreg >=680){
settemp=23;  
} 
if(readreg >=850){
settemp=24;  
} 
if(readreg >=1000){
settemp=25;  
}
  if(digitalRead(ButPin) == HIGH && flag == 0)      
    {                                              
      regim ++;
      flag = 1;
      if(regim > 3)                     // If the mode number exceeds the required one, then
        {                               // the countdown starts from zero
          regim = 1;
        }
    }
  
  if(digitalRead(ButPin) == LOW && flag == 1)
    {
       flag = 0;
       EEPROM.write(address, regim);
    } 
     if(regim == 1)
    {
      digitalWrite(heat, HIGH);
      digitalWrite(cool, LOW);
      digitalWrite(eko, LOW);      
    }
  if(regim == 2)
    {
      digitalWrite(heat, LOW);
      digitalWrite(cool, HIGH);
      digitalWrite(eko, LOW);      
    }
  if(regim == 3)
    {
      digitalWrite(heat, LOW);
      digitalWrite(cool, LOW);
      digitalWrite(eko, HIGH);      
    } 
    if(regim == 1 && steinhart >= settemp){
     analogWrite(pwm,0); 
    }
     if(regim == 2 && steinhart <= settemp){
     analogWrite(pwm,0); 
    }
     if(regim == 1 && steinhart <=(settemp-0.5)){
     analogWrite(pwm,130); 
    }
     if(regim == 1 && steinhart <= (settemp-1)){
     analogWrite(pwm,255); 
    }
      if(regim == 2 && steinhart >= (settemp+0.5)){
     analogWrite(pwm,130); 
    }
     if(regim == 2 && steinhart >= (settemp+1)){
     analogWrite(pwm,255);
    }
//Serial.println(flag);
//Serial.print(steinhart,1);
//Serial.println(" *C");
//Serial.println(readreg);
//Serial.println(settemp);
//delay(500);
}
