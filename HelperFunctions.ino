// *************************************
//           HELPER FUNCTIONS
// *************************************

// some functions to streamline our code with regards to the bitwise byte arrays
void clearAll(byte type) // type == 0 for everything - something else eg. 1 for limited clear
{
  clearSelected(); // these are our own functions, declared below
  seqRunning = true;
  pulse = 0;
  seqTrueStep = 0;
  nowPlaying = 255;
  cued = 255;

  if (type == 0)
  {
    // clear automation
    //   for (byte i = 0; i < 192; i++)
    //     seqAutomate[i] = 0;
    for (byte i = 0; i < 12; i++)
    {
      for (byte j = 0; j < 4; j++)
      {
        track[i].stepOn[j] = 0;
        track[i].stepAccent[j] = 0;
        track[i].stepFlam[j] = 0;
      }
    }
    //  automate = false;
    seqCurrentStep = 0;
    clockPulse = 0;
  }
}

void clearSelected()
{
  for (byte i = 0; i < 4; i++)
    seqStepSelected[i] = B00000000;
}

void clearJust() // clear the "just" arrays
{
  for (byte i = 0; i < 40; i++)
  {
    justpressed[i] = 0;
    justreleased[i] = 0;
  }
}
void clearEdit() // clear the "edit" arrays
{
  for (byte i = 0; i < 4; i++)  
    *edit[i] = B00000000;
  if (mode < 3)
  {
    for (byte i = 0; i < 4; i++) // when clearEdit is slected, the edited bytes are always those for accents, so also clear noteOn and flams
      track[currentTrack].stepOn[i] = B00000000;
    for (byte i = 0; i < 4; i++)
      track[currentTrack].stepFlam[i] = B00000000;
  }
}
boolean checkSelected(byte thisStep) // check whether a step is selected or not - returns true if a step is selected, false if not
{
  if (seqStepSelected[thisStep/8] & (1<<thisStep%8)) //seqStepSelected is an array of unsigned bytes, so dividing thisStep by 8 will return 0, 1, 2, or 3 (ie. the byte we're concerned with)
    return true;
  else
    return false; 
}

boolean checkMute(byte thisStep) // check whether a step is muted or not - returns true if a step is mute, false if not
{
  if (seqStepMute[thisStep/8] & (1<<thisStep%8)) //seqStepMute is an array of unsigned bytes, so dividing thisStep by 8 will return 0, 1, 2, or 3 (ie. the byte we're concerned with)
    return true;
  else
    return false;  
}

boolean checkSkip(byte thisStep) // check whether a step is marked to skip or not - returns true if it is
{
  if (seqStepSkip[thisStep/8] & (1<<thisStep%8)) //seqStepSkip is an array of unsigned bytes, so dividing thisStep by 8 will return 0, 1, 2, or 3 (ie. the byte we're concerned with)
    return true;
  else
    return false;  
}

// POTS
// reading from 4051
// this function returns the analog value for the given channel
int getValue( int channel)
{
  // set the selector pins HIGH and LOW to match the binary value of channel
  for(int bit = 0; bit < 3; bit++)
  {
    int pin = select[bit]; // the pin wired to the multiplexer select bit
    int isBitSet =  bitRead(channel, bit); // true if given bit set in channel
    digitalWrite(pin, isBitSet);
  }
  return map((constrain(analogRead(analogPin), 25, 1000)), 25, 1000, 0, 1023); // we're only using readings between 25 and 1000, as the extreme ends are unstable
  //int reading = analogRead(analogPin);
  //return reading;
}

// a handy DIFFERENCE FUNCTION
int difference(int i, int j)
{
  int k = i - j;
  if (k < 0)
    k = j - i;
  return k;
}

// things that need to happen as a consequence of the bpm changing
void bpmChange(byte BPM)
{
  bpm = BPM;
  clockPulseDur = (60000/bpm)/24;
  sixteenthDur = (60000/bpm)/4;
  //  if (onDur >= sixteenthDur && potTemp)
  //  {
  //    onDur = sixteenthDur - 10;
  // swing16thDur = map(swing, 0, 255, sixteenthDur, ((2*sixteenthDur)/3)*2);
  //  }      
  //swing16thDur = map(swing, 0, 255, sixteenthDur, ((2*sixteenthDur)/3)*2);
  if (seqTrueStep%2 != 0) // it's an uneven step, ie. the 2nd shorter 16th of the swing-pair
    swing16thDur = (2*sixteenthDur) - swing16thDur;
  else // it's an even step ie. the first longer 16th of the swing-pair
  {
    swing16thDur = map(swing, 0, 255, sixteenthDur, ((2*sixteenthDur)/3)*2);
  }
}

// read the table of contents (TOC) from the EEPROM (page 448)
// also recalls preferences
void tocRead()
{
  mem_1.readPage(448, rwBuffer);
  for (byte i = 0; i < 14; i++)
  {
    toc[i] = rwBuffer[i];
  }

  // update the preferences
  midiSyncOut = rwBuffer[14]; // send out 24ppq clock pulses to slave MIDI devices to this Groovesizer
  thruOn = rwBuffer[15]; // echo MIDI data received at input at output
  checkThru();
  midiTrigger = rwBuffer[16]; // send pattern trigger notes to change patterns on slaved Groovesizers
  triggerChannel = rwBuffer[17]; // the MIDI channel that pattern change messages are sent and received on
}

void assignPreferences()
{
  rwBuffer[14] = midiSyncOut; // send out 24ppq clock pulses to slave MIDI devices to this Groovesizer
  rwBuffer[15] = thruOn; // echo MIDI data received at input at output
  rwBuffer[16] = midiTrigger; // send pattern trigger notes to change patterns on slaved Groovesizers
  rwBuffer[17] = triggerChannel; // the MIDI channel that pattern change messages are sent and received on
}

void tocWrite(byte addLoc)
{
  byte tocByte = addLoc/8; // in which byte of the toc array is the location (each byte corresponds to a row of buttons)
  toc[tocByte] = bitSet(toc[tocByte], addLoc%8); // turns the appropriate byte on (1)
  for (byte i = 0; i < 14; i++) // update the read/write buffer with the current toc
    rwBuffer[i] = toc[i];

  // remember to save preferences too, since they're on the same EEPROM page
  assignPreferences();

  mem_1.writePage(448, rwBuffer); // write the buffer to the toc page on the EEPROM (448)
}

void tocClear(byte clearLoc)
{
  byte tocByte = clearLoc/8; // in which byte of the toc array is the location (each byte corresponds to a row of buttons)
  toc[tocByte] = bitClear(toc[tocByte], clearLoc%8); // turns the appropriate byte off (0)
  for (byte i = 0; i < 14; i++) // update the read/write buffer with the current toc
    rwBuffer[i] = toc[i];

  // remember to save preferences too, since they're on the same EEPROM page
  assignPreferences();

  mem_1.writePage(448, rwBuffer); // write it to the toc page on the EEPROM (448)

}

boolean checkToc(byte thisLoc) // check whether a memory location is already used or not
{
  byte tocByte = thisLoc/8; // in which byte of the toc array is the location (each byte corresponds to a row of buttons)
  if bitRead(toc[tocByte], thisLoc%8)
    return true;
  else
    return false;  
}

void savePrefs()
{
  //preferences are saved on the same EEPROM page (448) as the toc, so make sure to resave the toc when saving preferences
  for (byte i = 0; i < 14; i++) // update the read/write buffer with the current toc
    rwBuffer[i] = toc[i];

  assignPreferences(); // defined above

  mem_1.writePage(448, rwBuffer); // write the buffer to the toc page on the EEPROM (448) 
}

void savePatch()
{
  //transposeFactor = 0; // reset the transpose factor to 0
  static boolean pageSaved = false;
  static byte progress; // we want to stagger the save process over consecutive loops, in case the process takes too long and causes an audible delay
  if (!pageSaved)
  {    
    clearJust();
    longPress = 0;
    //mem_1.readPage(pageNum, rwBuffer);
    pageSaved = true;
    progress = 0;
  }

  switch (progress) { // we want to break our save into 4 seperate writes on 4 consecutive passes through the loop
  case 0:
    // pack our 64 byte buffer with the goodies we want to send to the EEPROM

    for (byte i = 0; i < 3; i++) // for the first set of 3 tracks
    {
      packTrackBuffer(i);
    }  

    mem_1.writePage(pageNum, rwBuffer);
    progress = 1;
    break;

  case 1:

    for (byte i = 3; i < 6; i++) // for the second set of 3 tracks
    {
      packTrackBuffer(i);
    }  

    mem_1.writePage(pageNum + 1, rwBuffer);
    progress = 2;
    break;

  case 2:
    for (byte i = 6; i < 9; i++) // for the third set of 3 tracks
    {
      packTrackBuffer(i);
    }  

    // add first 7 of 14 master page bytes
    rwBuffer[57] = seqLength;
    rwBuffer[58] = seqFirstStep;
    rwBuffer[59] = programChange;
    for (byte i = 0; i < 4; i++)
      rwBuffer[60 + i] = seqStepMute[i];

    mem_1.writePage(pageNum + 2, rwBuffer);
    progress = 3;
    break;

  case 3:
    for (byte i = 9; i < 12; i++) // for the fourth set of 3 tracks
    {
      packTrackBuffer(i);
    }

    // add second 7 of 14 master page bytes
    rwBuffer[57] = swing;
    rwBuffer[58] = followAction;
    rwBuffer[59] = bpm;
    for (byte i = 0; i < 4; i++)
      rwBuffer[60 + i] = seqStepSkip[i];  

    mem_1.writePage(pageNum + 3, rwBuffer);
    save = false;
    pageSaved = false;
    break;
  }
}

void packTrackBuffer(byte Track)
{
  byte offset = 19 * (Track % 3); // each track is 19 bytes long
  rwBuffer[0 + offset] = track[Track].level;       
  rwBuffer[1 + offset] = track[Track].accentLevel;
  rwBuffer[2 + offset] = highByte(track[Track].flamDelay); // flamDelay stored as 2 bytes - the int is split into a high and low byte 
  rwBuffer[3 + offset] = lowByte(track[Track].flamDelay);
  rwBuffer[4 + offset] = track[Track].flamDecay;
  rwBuffer[5 + offset] = track[Track].midiChannel;
  rwBuffer[6 + offset] = track[Track].midiNoteNumber;
  for (byte j = 0; j < 4; j++)
  {
    rwBuffer[7 + offset + j] = track[Track].stepOn[j];
    rwBuffer[11 + offset + j] = track[Track].stepAccent[j];
    rwBuffer[15 + offset + j] = track[Track].stepFlam[j];
  } 
}

void unPackTrackBuffer(byte Track)
{
  byte offset = 19 * (Track % 3); // each track is 19 bytes long
  track[Track].level = rwBuffer[0 + offset];       
  track[Track].accentLevel = rwBuffer[1 + offset];
  track[Track].flamDelay = word(rwBuffer[2 + offset], rwBuffer[3 + offset]);
  track[Track].flamDecay = rwBuffer[4 + offset];
  track[Track].midiChannel = rwBuffer[5 + offset];
  track[Track].midiNoteNumber = rwBuffer[6 + offset];
  for (byte j = 0; j < 4; j++)
  {
    track[Track].stepOn[j] = rwBuffer[7 + offset + j];
    track[Track].stepAccent[j] = rwBuffer[11 + offset + j];
    track[Track].stepFlam[j] = rwBuffer[15 + offset + j];
  } 
}

void loadPatch(byte patchNumber) // load the specified location number
{
  //  transposeFactor = 0; // reset the transpose factor to 0
  static boolean pageLoaded = false;
  static byte progress; // we want to stagger distributing the loaded values over consecutive loops, in case the process takes too long and causes an audible delay
  saved = true; // needed for follow actions 1 and 2
  recall = false;
  pageNum = patchNumber * 4; // each location is 4 pages long
  if (!pageLoaded)
  {
    clearSelected();
    clearJust();
    pageLoaded = true;
    progress = 0;
    loadContinue = true;
    cued = patchNumber; // for the duration of the load process, we set cued = patchNumber so we can loadContinue with loadPatch(cued)
  }
  switch (progress) { // we want to break our load into 4 seperate reads on 4 consecutive passes through the loop
  case 0:
    mem_1.readPage(pageNum, rwBuffer);

    // unpack our 64 byte buffer with the goodies from the EEPROM

    for (byte i = 0; i < 3; i++) // for the first set of 3 tracks
    {
      unPackTrackBuffer(i);
    }  

    progress = 1;
    break;

  case 1:

    mem_1.readPage(pageNum + 1, rwBuffer);

    for (byte i = 3; i < 6; i++) // for the second set of 3 tracks
    {
      unPackTrackBuffer(i);
    }  

    progress = 2;
    break;

  case 2:
    mem_1.readPage(pageNum + 2, rwBuffer);

    for (byte i = 6; i < 9; i++) // for the third set of 3 tracks
    {
      unPackTrackBuffer(i);
    }  

    // read first 7 of 14 master page bytes
    seqLength = rwBuffer[57];
    seqFirstStep = rwBuffer[58];
    seqLastStep = seqFirstStep + (seqLength - 1);
    // send a program change message if the patch is not the same as the current one
    if (programChange != rwBuffer[59])
    {
      programChange = rwBuffer[59];
      MIDI.sendProgramChange(programChange, track[0].midiChannel);
    }
    for (byte i = 0; i < 4; i++)
      seqStepMute[i] = rwBuffer[60 + i];


    progress = 3;
    break;

  case 3:
    mem_1.readPage(pageNum + 3, rwBuffer);

    for (byte i = 9; i < 12; i++) // for the third set of 3 tracks
    {
      unPackTrackBuffer(i);
    }

    // read second 7 of 14 master page bytes
    swing = rwBuffer[57];
    followAction = rwBuffer[58];
    bpm = rwBuffer[59];
    bpmChange(bpm);
    for (byte i = 0; i < 4; i++)
      seqStepSkip[i] = rwBuffer[60 + i];  

    nowPlaying = cued; // the pattern playing is the one that was cued before
    cued = 255; // out of range since 112 patterns only - we'll use 255 to see if something is cued
    pageLoaded = false;
    loadContinue = false;
    break;
  }

}

void addAccent()
{
  // check if a switch was just pressed and toggle the appropriate bit in the accent array 
  for (byte i = 0; i < 32; i++)
  { 
    if (justpressed[i])
    {   
      *edit[i / 8] ^= (1 << i % 8); // toggle the bit, ie. turn 1 to 0, and 0 to 1
      if (checkStepAccent(currentTrack, i))
        bitSet(track[currentTrack].stepOn[i / 8], i % 8);
      clearJust();
      shiftL = true; 
    }
  }
}

void addFlam()
{
  // check if a switch was just pressed and toggle the appropriate bit in the flam array 
  for (byte i = 0; i < 32; i++)
  { 
    if (justpressed[i])
    {   
      *edit[i / 8] ^= (1 << i % 8); // toggle the bit, ie. turn 1 to 0, and 0 to 1
      if (checkStepFlam(currentTrack, i))
        bitSet(track[currentTrack].stepOn[i / 8], i % 8);
      clearJust();
      shiftR = true;
    }
  } 
}

void buttonCheckSelected()
{
  // check if a switch was just pressed and toggle the appropriate bit in the selected array
  for (byte i = 0; i < 32; i++)
  { 
    if (justpressed[i])
    {   
      *edit[i/8] ^= (1<<i%8); // toggle the bit, ie. turn 1 to 0, and 0 to 1
      if (mode == 0 && !checkStepOn(currentTrack, i)) // if we've just turn off a step, clear the same step of flams and accents as well
      {
        bitClear(track[currentTrack].stepAccent[i/8], i % 8);
        bitClear(track[currentTrack].stepFlam[i/8], i % 8);
      }
      clearJust();
    }
  } 
}

boolean checkStepOn(byte trackNum, byte stepNum)
{
  return bitRead(track[trackNum].stepOn[stepNum / 8], stepNum % 8);
}

boolean checkStepAccent(byte trackNum, byte stepNum)
{
  return bitRead(track[trackNum].stepAccent[stepNum / 8], stepNum % 8);
}

boolean checkStepFlam(byte trackNum, byte stepNum)
{
  return bitRead(track[trackNum].stepFlam[stepNum / 8], stepNum % 8);
}

void tapTempo()
{
  static unsigned long lastTap = 0;
  unsigned long now = millis(); // the current time in milliseconds
  static byte tapsNumber = 0; // how many bpms have been added to the bpmTaps array
  int bpmSum = 0; // the sum of the values in the bpmTaps[] array 

  if ((now - lastTap) < 1334) // a quarter is 1333ms long at 45bpm (our slowest allowed bpm)
  {
    bpmTaps[tapsNumber % 10] = 60000/(now - lastTap);
    tapsNumber++;
    lastTap = now;
  }
  else
  {
    for (byte i = 0; i < 10; i++) // clear the bpmTaps array
    {
      bpmTaps[i] = 0;
      tapsNumber = 0;
    }
    lastTap = now;
  }

  for (byte i = 0; i < 10; i++)
  {
    if (bpmTaps[i] > 0)
    {
      bpmSum += bpmTaps[i];
    }
  }

  if (bpmSum > 0)
  {
    byte i = (tapsNumber < 11) ? tapsNumber : 10;
    // bpm = bpmSum / i;
    bpmChange(bpmSum / i);
  }

  seqTrueStep = seqLength - 1;
  seqCurrentStep = seqLength - 1;
  autoCounter = 0;
  sixteenthStartTime = 0;
}

/**********************************************************************************************
 ***
 *** ShiftOut to address 42 LEDs via 4 74HC595s serial to parallel shifting registers 
 *** Based on http://arduino.cc/en/Tutorial/ShiftOut
 ***
 ***********************************************************************************************/
//***TOP*OF*THE*SKETCH**************************************************************************
// remember to add the following to the start of your sketch before (before setup)
/*
int LEDlatchPin = 2;
 int LEDclockPin = 3;
 int LEDdataPin = 4;
 */
//***SETUP**************************************************************************************
// and add the following pin modes to setup()
/*
pinMode(LEDlatchPin, OUTPUT);
 pinMode(LEDclockPin, OUTPUT); 
 pinMode(LEDdataPin, OUTPUT);
 */

void shiftOut(int myLEDdataPin, int myLEDclockPin, byte myDataOut) {
  // This shifts 8 bits out MSB first, 
  // on the rising edge of the clock,
  // clock idles low

  //internal function setup
  int i=0;
  int pinState;
  pinMode(myLEDclockPin, OUTPUT);
  pinMode(myLEDdataPin, OUTPUT);

  //clear everything out just in case to
  //prepare shift register for bit shifting
  digitalWrite(myLEDdataPin, 0);
  digitalWrite(myLEDclockPin, 0);

  //for each bit in the byte myDataOut
  //NOTICE THAT WE ARE COUNTING DOWN in our for loop
  //This means that %00000001 or "1" will go through such
  //that it will be pin Q0 that lights. 
  for (i=7; i>=0; i--)  {
    digitalWrite(myLEDclockPin, 0);

    //if the value passed to myDataOut and a bitmask result 
    // true then... so if we are at i=6 and our value is
    // %11010100 it would the code compares it to %01000000 
    // and proceeds to set pinState to 1.
    if ( myDataOut & (1<<i) ) {
      pinState= 1;
    }
    else {	
      pinState= 0;
    }

    //Sets the pin to HIGH or LOW depending on pinState
    digitalWrite(myLEDdataPin, pinState);
    //register shifts bits on upstroke of clock pin  
    digitalWrite(myLEDclockPin, 1);
    //zero the data pin after shift to prevent bleed through
    digitalWrite(myLEDdataPin, 0);
  }

  //stop shifting
  digitalWrite(myLEDclockPin, 0);
}

// this function shifts out the the 4 bytes corresponding to the led rows
void updateLeds(void)
{
  static boolean lastSentTop = false; // we want to alternate sending the top 2 and bottom 3 rows to prevent an edge case where 4 rows of LEDs lit at the same time sourcing too much current
  //ground LEDlatchPin and hold low for as long as you are transmitting
  digitalWrite(LEDlatchPin, 0);
  if (!lastSentTop) // send the top to rows
  {
    shiftOut(LEDdataPin, LEDclockPin, B00000000);
    shiftOut(LEDdataPin, LEDclockPin, B00000000); 
    shiftOut(LEDdataPin, LEDclockPin, B00000000);
    shiftOut(LEDdataPin, LEDclockPin, LEDrow[1]); 
    shiftOut(LEDdataPin, LEDclockPin, LEDrow[0]);
    lastSentTop = true;
  }
  else // ie. lastSentTop is true, then send the bottom 3 rows
  {
    shiftOut(LEDdataPin, LEDclockPin, LEDrow[4]);
    shiftOut(LEDdataPin, LEDclockPin, LEDrow[3]); 
    shiftOut(LEDdataPin, LEDclockPin, LEDrow[2]);
    shiftOut(LEDdataPin, LEDclockPin, B00000000); 
    shiftOut(LEDdataPin, LEDclockPin, B00000000);
    lastSentTop = false;
  }
  //return the latch pin high to signal chip that it 
  //no longer needs to listen for information
  digitalWrite(LEDlatchPin, 1);
}

void showNumber()
{
  static byte nums[25] = 
  {
    B01110010,
    B01010010,
    B01010010,
    B01010010,
    B01110010,

    B01110111,
    B00010001,
    B01110111,
    B01000001,
    B01110111,

    B01010111,
    B01010100,
    B01110111,
    B00010001,
    B00010111,

    B01000111,
    B01000001,
    B01110001,
    B01010001,
    B01110001,

    B01110111,
    B01010101,
    B01110111,
    B01010001,
    B01110001    
  };

  byte offset = 0; // horizontal offset for even numbers 
  byte row = 0; // vertical offset for the start of the digit

  ledsOff();

  if (number > 199) // add two dots for numbers 200 and over
  {
    LEDrow[3] = B00000001;
    LEDrow[4] = B00000001;
  }

  else if (number > 99) // add a dot for numbers 100 and over
    LEDrow[4] = B00000001;

  //digits under 10
  if ((number % 10) % 2 == 0) // even numbers
    offset = 4;
  else
    offset = 0;
  row = ((number % 10)/2)*5;      

  for (byte j = 0; j < 5; j++)
  {
    for (byte i = 0; i < 3; i++) // 3 pixels across
    {      
      if (bitRead(nums[row + j], offset + i)) // ie. if it returns 1  
        LEDrow[j] = bitSet(LEDrow[j], 7 - i);
    }
  }

  // digits over 10
  if (((number / 10) % 10) % 2 == 0) // even numbers
    offset = 4;
  else
    offset = 0;
  row = (((number / 10) % 10)/2)*5;      

  for (byte j = 0; j < 5; j++)
  {
    for (byte i = 0; i < 3; i++) // 3 pixels across
    {      
      if (bitRead(nums[row + j], offset + i)) // ie. if it returns 1  
        LEDrow[j] = bitSet(LEDrow[j], 3 - i);
    }
  }
}

void ledsOff()
{
  for (byte i = 0; i < 5; i++)
    LEDrow[i] =  B00000000;
}

void getPots()
{
  // we want to get a running average to smooth the pot values
  static int potValues[6][10] ; // to store our raw pot readings
  static byte index;
  for (byte i = 0; i < 6; i++) // for each of the 6 pots
    potValues[i][index] = getValue(i); 
  index = (index < 9) ? index + 1 : 0;  
  for (byte i = 0; i < 6; i++)
  {
    int tempVal = 0;
    for (byte j = 0; j < 10; j++)
    {
      tempVal += potValues[i][j];
    }
    pot[i] = tempVal / 10; // get the average
  }
}

void checkThru()
{
  if(thruOn == 0)
    MIDI.turnThruOff();
  else if (thruOn == 1)
    MIDI.turnThruOn(midi::Full);
  else if (thruOn == 2)
    MIDI.turnThruOn(midi::DifferentChannel);
}

void buttonFunction()
{
  switch (mode)
  {
  case 0: // stepOn for tracks 0 - 11
    for (byte i = 0; i < 4; i++)
      edit[i] = &track[currentTrack].stepOn[i];
    break;
  case 1: // accents for tracks 0 - 11
    for (byte i = 0; i < 4; i++)
      edit[i] = &track[currentTrack].stepAccent[i];
    break;
  case 2: // flams for tracks 0 - 11
    for (byte i = 0; i < 4; i++)
      edit[i] = &track[currentTrack].stepFlam[i];
    break;
  case 3: //
    if (mutePage)
    {
      for (byte i = 0; i < 4; i++)
        edit[i] = &seqStepMute[i]; 
    }
    else if (skipPage)
    {
      for (byte i = 0; i < 4; i++)
        edit[i] = &seqStepSkip[i]; 
    }    
    break;
  }
}

void lockPot(byte Pot) // values of 0 - 5 for each pot and 6 for all
{
  if (Pot == 6)
  {
    for (byte i = 0; i < 6; i++)
      potLock[i] = pot[i];
  }
  else
    potLock[Pot] = pot[Pot];
  showingNumbers = false;
  lockTimer = 0;
}

boolean unlockedPot(byte Pot) // check if a pot is locked or not
{
  if (potLock[Pot] == 9999)
    return true;
  else if (difference(potLock[Pot], pot[Pot]) > 35) // can lower the threshold value (35) for less jittery pots
  {
    for (byte i = 0; i < 6; i++)// lock all the other pots
    {
      if (i != Pot)
        lockPot(i);
    }
    showingNumbers = true;
    potLock[Pot] = 9999;
    return true;
  }
  else
    return false;
}

void scheduleFlam(byte Track, byte Velocity)
{
  if (fxFlam[Track] != 0)
  {
    if (Velocity > fxFlamDecay)
    {
      track[Track].nextFlamLevel = Velocity - fxFlamDecay;
      track[Track].nextFlamTime = millis() + fxFlamDelay;
      fxFlam[Track] = 2;
    }
    else
    {
      track[Track].nextFlamTime = 0;
      for (byte i = 0; i < 12; i++)
        fxFlam[i] = 0; 
    }
  }
  else if (fxFlam[Track] == 0)
  {
    if (Velocity > track[Track].flamDecay)
    {
      track[Track].nextFlamLevel = Velocity - track[Track].flamDecay;
      track[Track].nextFlamTime = millis() + track[Track].flamDelay;
    }
    else
      track[Track].nextFlamTime = 0;
  }
}

int tempoDelay(byte choice)
{
  switch (choice)
  {
  case 1:
    return sixteenthDur / 4;
    break;
  case 2:
    return sixteenthDur / 2;
    break;
  case 3:
    return (sixteenthDur * 2) / 3;
    break;
  case 4:
    return sixteenthDur;
    break;
  case 5:
    return (sixteenthDur * 4) / 3;
    break;
  case 6:
    return sixteenthDur * 2;
    break;
  case 7:
    return (sixteenthDur * 8) / 3;
    break;
  case 8:
    return sixteenthDur * 4;
    break;
  }
}

void shiftMode()
{
  lockPot(6);
  clearJust();
  shiftL = false;
  shiftR = false;
}

void seqReset() // reset the sequencer
{
  seqTrueStep = 0;
  seqCurrentStep = 0;
  autoCounter = 0;
  sixteenthStartTime = 0;
  currentTime = 0;
}

void fxFlamSet()
{
  for (byte i = 0; i < 12; i++)
  {
    byte rnd = random(0, 100);
    if (rnd > 30) // 30% probability that a track will get a flam
      fxFlam[i] = 1;
  }
}
















