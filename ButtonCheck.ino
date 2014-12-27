/**********************************************************************************************
 ***
 *** Button checker with shift-in for 4 CD4021Bs parallel to serial shift registers (32 buttons)
 *** Adapted from Adafruit http://www.adafruit.com/blog/2009/10/20/example-code-for-multi-button-checker-with-debouncing/
 *** ShiftIn based on  http://www.arduino.cc/en/Tutorial/ShiftIn
 ***
 ***********************************************************************************************/
//***TOP*OF*THE*SKETCH*********************************************************************
// remember to add the following to the start of your sketch before (before setup)
/*
byte BUTTONlatchPin = 7;
 byte BUTTONclockPin = 8;
 byte BUTTONdataPin = 9;
 #define DEBOUNCE 5 // the debounce time in milliseconds
 byte pressed[32], justpressed[32], justreleased[32], buttons[32];
 //define the byte variables that the button states are read into
 byte BUTTONvar1; 
 byte BUTTONvar2; 
 byte BUTTONvar3; 
 byte BUTTONvar4;
 */
//***SETUP*************************************************************************************
// and add the following pin modes to setup()
/*
pinMode(BUTTONlatchPin, OUTPUT);
 pinMode(BUTTONclockPin, OUTPUT); 
 pinMode(BUTTONdataPin, INPUT);
 */
//***SHIFTIN***********************************************************************************
// definition of the shiftIn function
// just needs the location of the data pin and the clock pin
// it returns a byte with each bit in the byte corresponding
// to a pin on the shift register. leftBit 7 = Pin 7 / Bit 0= Pin 0

byte shiftIn(int myDataPin, int myClockPin) { 
  int i;
  int temp = 0;
  int pinState;
  byte myDataIn = 0;

  pinMode(myClockPin, OUTPUT);
  pinMode(myDataPin, INPUT);

  //we will be holding the clock pin high 8 times (0,..,7) at the
  //end of each time through the for loop

  //at the begining of each loop when we set the clock low, it will
  //be doing the necessary low to high drop to cause the shift
  //register's DataPin to change state based on the value
  //of the next bit in its serial information flow.
  //The register transmits the information about the pins from pin 7 to pin 0
  //so that is why our function counts down
  for (i=0; i<=7; i++)
  {
    digitalWrite(myClockPin, 0);
    delayMicroseconds(2);
    temp = digitalRead(myDataPin);
    if (temp) {
      pinState = 1;
      //set the bit to 0 no matter what
      myDataIn = myDataIn | (1 << i);
    }
    else {
      //turn it off -- only necessary for debuging
      //print statement since myDataIn starts as 0
      pinState = 0;
    }

    //Debuging print statements
    //Serial.print(pinState);
    //Serial.print("     ");
    //Serial.println (dataIn, BIN);

    digitalWrite(myClockPin, 1);

  }
  //debuging print statements whitespace
  //Serial.println();
  //Serial.println(myDataIn, BIN);
  return myDataIn;
}

//*********************************************************************************************

void check_switches()
{
  static byte previousstate[40];
  static byte currentstate[40];
  static long lasttime;
  byte index;



  if (millis() < lasttime) {
    // we wrapped around, lets just try again
    lasttime = millis();
  }

  if ((lasttime + DEBOUNCE) > millis()) {
    // not enough time has passed to debounce
    return; 
  }
  // ok we have waited DEBOUNCE milliseconds, lets reset the timer
  lasttime = millis();

  //BUTTONS
  //Collect button data from the 4901s
  //Pulse the latch pin:
  //set it to 1 to collect parallel data
  digitalWrite(BUTTONlatchPin,1);
  //set it to 1 to collect parallel data, wait
  delayMicroseconds(20);
  //set it to 0 to transmit data serially  
  digitalWrite(BUTTONlatchPin,0);

  //while the shift register is in serial mode
  //collect each shift register into a byte
  //the register attached to the chip comes in first 
  for (byte k = 0; k < 5; k++)
    BUTTONvar[k] = shiftIn(BUTTONdataPin, BUTTONclockPin);


  //write the button states to the currentstate array
  for (byte j = 0; j < 5; j++)
  {
    for (byte i = 0; i<=7; i++)
      buttons[(j * 8) + i] = BUTTONvar[j] & (1 << i);
  }

  for (index = 0; index < 40; index++) {
    justpressed[index] = 0;       // when we start, we clear out the "just" indicators
    justreleased[index] = 0;

    currentstate[index] = (buttons[index] == 0) ? HIGH : LOW;

    if (currentstate[index] == previousstate[index]) {
      if ((pressed[index] == LOW) && (currentstate[index] == LOW)) {
        // just pressed
        justpressed[index] = 1;
      }
      else if ((pressed[index] == HIGH) && (currentstate[index] == HIGH)) {
        // just released
        justreleased[index] = 1;
      }
      pressed[index] = !currentstate[index];  // remember, digital HIGH means NOT pressed
    }
    //Serial.println(pressed[index], DEC);
    previousstate[index] = currentstate[index];   // keep a running tally of the buttons
  }
}





