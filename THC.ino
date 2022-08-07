/* Trin modded
 *  
 *  V1 -  replace sevseg with 16x2 i2C, renamed and changed inputs 
 *        and outputs pin names to match older THC standalone,
 *        added stepper outputs etc to use as standalone.
 *        **to do -- add on/off so passthrough can work
 *        
*/  
 
 /*Pin allocation
  - A0 = arcVoltageInput // voltage input from voltage divider in arcVoltageInput machine.
  - A2 = pulseIntervalInput //Stepper speed, ie, period between pulses in micro seconds
  - A3 = setPointInput //pot to adjust voltage setpoint.
  - A4 = SDA LCD
  - A5 = SCL LCD
   
   D2 = grblStepPin //For reading {grblStepPin} pulses from controller.
   D4 = arcOkInput //Arc OK signal from plasma machine.
   D5 = dirPin //Directly connected to stepper driver.
   D6 = downLedPin //HIGH if volts are high lower torch (red led pin)
   D7 = grblDirPin//dir signal from controller.
   D8 = upLedPin //HIGH if volts are low raise torch(yellow led pin)
   D9 = stepPin //to stepper controller
   D10 = arcOkLedPin // independant led pin
   D11 = engageThcSwitch //passthrough or thc control.
   D12 = dataDisplyInput //show last 200 recording of arcV

 *  
 *  A braindead plasma torch THC. Goes along with the board schematic in this repo.
The gist of how this is works is, on startup, you read the potentiometer and
pick a set point. Then it reads the analog pin for the plasma voltage, does
comparisons to that setpoint, and triggers the optocouplers accordingly.
There's a little bit of smoothing that goes into to the ADC reads to improve
the signal's stability a bit, but we can't do too much, since otherwise, the
LinuxCNC can't respond fast enough when the torch height really is changing.
I do a few tricks in here to make sure the loop() function runs quickly. Stuff
like only reading the setpoint on startup (so if you want to adjust it, hit the
reset button), using bitwise shifts for division, etc. Last I checked, this was
able to do about 6000 samples per second, whereas the maximum you could get
from simply looping analogRead() is supposed to be 9000 samples per second
(with 10 bit precision).
Two more noteworthy quirks:
Firstly, while we let the signals for the plasma change rather rapidly, we only
update the display several times a second and give it a much, much longer
average. This just makes it easier on the eyes.
Secondly, this is going to be giving the "UP" signal whenever the plasma
is not cutting (since the voltage will be 0), so it is important to use
LinuxCNC's HAL configs to only respect the THC's signals once the torch is
cutting and is not piercing or cornering. The "thcud" component should help
with that, as well as additional YouTube videos and files coming soon.
(c) The Swolesoft Development Group, 2019
License: http://250bpm.com/blog:82
*/

#include "Arduino.h"

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2); // set the LCD address to 0x27 or 0x3F for a 16 chars and 2 line display

#include <TimerOne.h>

//step pins

// Naming inputs and outputs pins.

namespace
{
constexpr int arcVoltageInput = A0;
constexpr int pulseIntervalInput = A2;
constexpr int setPointInput = A3;
constexpr int grblStepPin = 2;          //interrupt pin for stepping
constexpr int arcOkInput = 4;
constexpr int dirPin = 5;
constexpr int downLedPin = 6;
constexpr int grblDirPin = 7;
constexpr int upLedPin = 8;
constexpr int stepPin = 9;
constexpr int arcOkLedPin = 10;
constexpr int engageThcSwitch = 11;

long pulseInterval = 800;

bool arcOk;
bool engageThc = HIGH;

}


// Setting the scale for the converting analogRead values to volts.
// 4.489 AREF voltage * 50 built-in voltage divider / 1023 resolution = 0.21749755 ADC counts per volt
// As far as I can tell, the arithmetic below *does* get optimized out by the compiler.
#define SCALE (4.489*50/1023)

// bandwidth/Threshold in ADC counts for when we say the torch voltage is too high or low 
// Multiply by SCALE for the threshold in volts.
// FYI: Some degree of buffer is needed to prevent awful see-sawing.
#define THRESH 5

// Voltage adjustment range for the knob.
#define MINSET 135
#define MAXSET 160

//required for sample averaging...
#define BUFSIZE 512  // Would technically let us do running averages up to BUFSIZE samples. In testing, shorter averages seemed better.
#define SAMP 16  // Use this many samples in the average; must be a power of 2 and no larger than BUFSIZE.
#define DISP 1024 // The number of samples to use in calculating a slower average for the display. Must also be a power of 2.

unsigned int shift = 0;

unsigned int values[BUFSIZE] = {0}; // buffer for ADC reads
unsigned long total = 0; // for keeping a rolling total of the buffer
unsigned long disp = 0;  // for separately tracking ADC reads for the display
unsigned long target = 0; // voltage target, in ADC counts

// for tracking when to move torch up and down
int diff = 0;
int mean = 0;
int mode = -1;

// generic temp vars
unsigned long tmp = 0;
float ftmp = 0;
float ftmp2 = 0;

// generic looping vars
int i = 0;
int j = 0;

// for the startup adjustment period
unsigned long timelimit = 0;
unsigned long ms = 0;



void setup() {

  pulseInterval = map(analogRead(pulseIntervalInput), 0, 1024, 10000, 0);//In microseconds.
  
  //THC inputs and outputs
  pinMode(arcOkInput, INPUT_PULLUP);
  pinMode(dirPin, OUTPUT);
  pinMode(stepPin, OUTPUT);
  
  pinMode(grblDirPin, INPUT_PULLUP);
  pinMode(grblStepPin, INPUT_PULLUP);
  
  pinMode(engageThcSwitch, INPUT_PULLUP);
  pinMode(arcOkLedPin, OUTPUT);
  
  pinMode(setPointInput, INPUT);
  pinMode(arcVoltageInput, INPUT);
  pinMode(upLedPin, OUTPUT);
  pinMode(downLedPin, OUTPUT);

  //grblStepPin interrupt, will step any time if GRBL sends a step...
  //hopefully will allow plasma cutting on 3D models ie a curved surface. 
  attachInterrupt(digitalPinToInterrupt(grblStepPin), passThrough, FALLING);
  //grblStepPin
  
  //used for step speed
  Timer1.initialize();

  // Set the reference voltage to the external linear regulator
  // Do a few throwaway reads so the ADC stabilizes, as recommended by the docs.
  analogReference(EXTERNAL);
  analogRead(arcVoltageInput); analogRead(arcVoltageInput); analogRead(arcVoltageInput); analogRead(arcVoltageInput); analogRead(arcVoltageInput);

  // We need to calculate how big the shift must be, for a given sample size.
  // Since we are using bitshifting instead of division, I'm using a != here,
  // so your shit will be totally broke if you don't set SAMP to a power of 2.
  while((1 << shift) != SAMP)
    shift++;

  // Set up the LCD. Set an easily identifiable string
  // and show it for a moment. This makes it easy to see when the arduino reboots.
  
  //Serial.begin(115200);
  //Serial.println("Stand alone THC starting");
  lcd.init(); // initialize the lcd
  
  // Print a message to the LCD.
  lcd.backlight();
  lcd.setCursor(3, 0);
  lcd.print("Trin's THC");
  delay(2000);
  lcd.clear();
  
  // Now enter the period where you can set the voltage via the potentiomenter.
  // Default 5s period, plus an extension 2s as long as you keep adjusting it.
  // By fixing this after boot, we save cycles from needing to do two ADC reads per loop(),
  // avoid any nonsense from potentiometer drift, and don't need to think about the
  // capacitance of the ADC muxer.
  i=0;
  ms = millis();
  timelimit = ms + 5000;

  while (ms < timelimit) {
    tmp = analogRead(setPointInput);

    // Keep a rotating total, buffer, and average.  Since this value only moves
    // a small amount due to noise in the AREF voltage and the physical
    // potentiometer itself, 10 samples is fine.
    total = total + tmp - values[i];
    values[i] = tmp;
    target = total / 10;

    // Calculate the setpoint, based on min/max, and chop it to one decimal point.
    ftmp2 = MINSET + ((MAXSET-MINSET) * (target/1023.0));
    ftmp2 = ((int) (ftmp2*10))/10.0;

    if (ftmp != ftmp2) {
      ftmp = ftmp2;
      timelimit = max(timelimit, ms + 2000);
      lcd.setCursor(3, 0);
      lcd.print(ftmp);
      lcd.print("V");
      lcd.setCursor(13, 0);
    int stepSpeed = map(analogRead(pulseIntervalInput), 0, 1024, 1, 99);//speed in %
    lcd.print(stepSpeed); //display pulseInterval
    if (stepSpeed<10){
      lcd.setCursor(14, 0);
      lcd.print(" ");
    }
    lcd.setCursor(15, 0);
    lcd.print("%");
    }

    i = (i + 1) % 10;
    ms = millis();

  }

  // Convert the voltage target back into an int, for ADC comparison, with the scale the plasma pin uses.
  target = ftmp / SCALE;

  // Before carrying on, we now reset some of those variables.
  for (i = 0; i < BUFSIZE; i++)
    values[i] = 0;

  total = 0;
  i = 0;
  j = 1; // Keeps display from triggering until we've done BUFSIZE samples.
}


void loop() {


  tmp = analogRead(arcVoltageInput);
  disp += tmp; // non-rolling tally for the lower sample rate display

  // Rolling window for a smaller sample
  total = total + tmp - values[i];
  values[i] = tmp;

  // This mean truncates downwards. Oh well. At least it's fast.
  mean = total >> shift;
  diff = mean - target;

//is arcOK input on, if not do nothing
arcOk = !digitalRead(arcOkInput);//Switches THC on only when arcOk signal from plasma machine.
if (arcOk){

  // If the mean is very low, then the plasma is turned off - it's just ADC
  // noise you're seeing and it and should be ignored.
  // This effectively checks if it's less than 2^4, ie. 16 counts, or ~3V with my scale factor.
  if (!(mean>>4)) {
      mode = 0;
      digitalWrite(upLedPin, LOW);
      digitalWrite(downLedPin, LOW);
      Timer1.pwm(stepPin, 1024, pulseInterval);
  }

  // Otherwise, set pins as per reading.
  // Set 0's first to turn off one direction before turning on reverse.
  // We should never have both the UP and DOWN pins set to 1 - that would be nonsense.
  // Checking for current 'mode' setting before flipping saves a few cycles.
  else if (diff > THRESH) {
    if (mode != 2) {
      mode = 2;
      digitalWrite(upLedPin, LOW);
      digitalWrite(downLedPin, HIGH);
      digitalWrite(dirPin, HIGH);  
      Timer1.pwm(stepPin, 512, pulseInterval);
    }
  }

  else if (diff < -THRESH) {
    if (mode != 1) {
      mode = 1;
      digitalWrite(downLedPin, LOW);
      digitalWrite(upLedPin, HIGH);
      digitalWrite(dirPin, LOW);  
      Timer1.pwm(stepPin, 512, pulseInterval);
    }
  }

  else {
      mode = 0;
      digitalWrite(upLedPin, 0);
      digitalWrite(downLedPin, 0);
      stop();
  }

}
//if arcOk signal is not OK stop movement
else{
  mode = 0;
      digitalWrite(upLedPin, LOW);
      digitalWrite(downLedPin, LOW);
      Timer1.pwm(stepPin, 1024, pulseInterval);
}
  // Every DISP reads, update what's displayed on the screen with a slower average.
  // This would be roughly 5 or 6 times per second at our current speeds.
  if (!j) {
    lcd.setCursor(3, 1);
    lcd.print((float) ((disp / DISP) * SCALE));
    disp = 0;
  }

  // Code below resets i and j to zero once SAMP and DISP are about to be reached 
  // Faster than modular arithmetic, by far. Doing that drops us down to ~3kS/sec.
  i = (i + 1) & (SAMP - 1);
  j = (j + 1) & (DISP - 1);
}




//********** STOP func *********
void stop()
{
  Timer1.stop();
  Timer1.disablePwm(stepPin);
  digitalWrite(dirPin, digitalRead(grblDirPin));
        digitalWrite(upLedPin, LOW);
        digitalWrite(downLedPin, LOW);  
}
//********** ^STOP func *********


//*** Pass Through ***
void passThrough()
{
  if (arcOk){
   digitalWrite(dirPin, digitalRead(grblDirPin));
   digitalWrite(stepPin, LOW);
   digitalWrite(stepPin, HIGH);
}
}
//*** ^Pass Through ***
