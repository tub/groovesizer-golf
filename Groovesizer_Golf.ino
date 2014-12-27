/************************************************************************
 ***   GROOVESIZER Golf v.020 - 12-Track, 32-Step MIDI Drum Sequencer
 ***   for the GROOVESIZER 8-Bit Musical Multiboard
 ***   http://groovesizer.com
 ************************************************************************
 * Copyright (C) 2013 MoShang (Jean Marais) moshang@groovesizer.com
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 * 
 ************************************************************************/

#include <stdint.h>
#include <avr/io.h>
#include <avr/pgmspace.h>       // included so we can use PROGMEM
#include <MIDI.h>               // MIDI library
#include <Wire.h>               // I2C library (required by the EEPROM library below)
#include <EEPROM24LC256_512.h>  // https://github.com/mikeneiderhauser/I2C-EEPROM-Arduino

EEPROM256_512 mem_1;            // define the eeprom chip
byte rwBuffer[64];              // our EEPROM read/write buffer


// sequencer variables
byte seqTrueStep; // we need to keep track of the true sequence position (independent of sequence length)
volatile byte seqCurrentStep; // the current step - can be changed in other various functions and value updates immediately
char seqNextStep = 1; // change this to -1 to reverse pattern playback
byte seqFirstStep = 0; // the first step of the pattern
byte seqLastStep = 31; // the last step of the pattern
byte seqLength = 32; // the length of the sequence from 1 step to 32

byte autoCounter = 0; // keeps count of the parameter auomation
byte seqStepSelected[4]; // "1" means a step is selected, "0" means it's not 
// each byte corresponds to a row of button/leds/steps, each bit of the byte corresponds to a single step   
// it would have been easier to create an array with 32 bytes, but hey, we only need 4 with a bit of bitwise math
// and since we're already doing bitwise operations to shift in and out, we may as well 
byte seqStepMute[4]; // is the step muted or not; 4 bytes correspond to each of the rows of buttons - "0" is top, "3" is bottom
byte seqStepSkip[4]; // is the step muted or not; 4 bytes correspond to each of the rows of buttons - "0" is top, "3" is bottom

boolean mutePage = true;
boolean skipPage = false;

byte programChange = 3;

volatile boolean seqMidiStep; // advance to the next step with midi clock
byte bpm = 96;
unsigned long currentTime;
unsigned int clockPulse = 0; // keep count of the  pulses
byte clockPulseDur = (60000/bpm)/24; // the duration of one midi clock  pulse in milliseconds (6  pulses in a 16th, 12  pulses in an 8th, 24  pulses in a quarter)
unsigned long pulseStartTime = 0;
unsigned int sixteenthDur = (60000/bpm)/4; // the duration of a sixteenth note in milliseconds (6  pulses in a 16th, 12  pulses in an 8th, 24  pulses in a quarter)
unsigned int swing16thDur = 0; // a swung 16th's duration will differ from the straight one above
byte swing = 0; // the amount of swing from 0 - 255 
unsigned long sixteenthStartTime = 0;
boolean seqRunning = true; // is the sequencer running?
boolean stepGo = false; // should we fire the next step?
//unsigned int onDur = 50; // how long the note should stay on

byte pulse; // to count incoming MIDI clocks

byte followAction = 0; // the behaviour of the pattern when it reaches the last step (0 = loop, 1 = next pattern, 2 = return to head, 3 = random chromatic, 4 = random major, 5 = random minor, 6 = random pentatonic)

byte head = 0; // the first in a series of chained patterns - any pattern triggered by hand is marked as the head (whether a followAction is set or not) 

boolean saved = false; // we need this variable for follow actions - only start doing the follow action once the pattern has been saved
// loading a pattern sets this variable to true - setting follow action to 1 or 2 sets this to false  

// we define a struct called Track - it contain 4 bytes for each stepOn, stepAccent, and stepFlam
typedef struct Track {
  byte stepOn[4]; // is the step on?
  byte level; // the MIDI velocity of a normal / non-accented step 
  byte stepAccent[4]; // does the step have an accent?
  byte accentLevel; // the MIDI velocity of an accented step
  byte stepFlam[4]; // does the step have a flam?
  unsigned int flamDelay; // the the distance between flam hits in milliseconds - selected by user
  byte flamDecay; // the amount by which each consecutive flam's velocity is lowered - selected by user
  unsigned long nextFlamTime; // when the next flam should occur
  byte nextFlamLevel; // what the velocity of the flam will be
  byte midiChannel; // the MIDI channel the track transmits on 
  byte midiNoteNumber; // the note number the track sends
} 
Track;

Track track[12]; // creates an array of 12 tracks

byte fxFlam[12] = 
{
  0, 0, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0
};

unsigned int fxFlamDelay; // the delay time for fx flams

char fxFlamDecay; // decay for fx flams (-128 to +128)

byte currentTrack = 0; // the track that's currently selected

byte toc[16] = {
  0, 0, 0, 0,
  0, 0, 0, 0,  // 14 bytes to serve as table of contents (toc) for our 112 memory locations (stored as 14 bytes on eeprom page 448) 
  0, 0, 0, 0,  // add 2 bytes at the end for the sake of simplicity, but their values will never change from 0
  0, 0, 0, 0}; // these are the last two unused rows on the 4th triger page (trigPage = 3)      



int pageNum;                // which EEPROM page we'll read or save to  
boolean recall = false;     // a pattern change was requested with a buttonpress in trigger mode 

boolean clearMem = false; // have we marked a memory location to be erased?

// needed by savePatch()
boolean save = false;
unsigned long longPress = 0;

// how dense are the notes created in the pattern randomizer
byte noteDensity;

byte trigPage = 0; // which trigger page are we on?
byte controlLEDrow = B00000010; 

int pot[6]; // to store the values of out 6 pots
int potLock[6]; // to lock the pots when they're not being adjusted
unsigned long lockTimer;

boolean potTemp; // a temp variable just while we do cleanup

unsigned long buttonTimer; // to display numbers for a period after a button was pressed

unsigned long windowTimer; // to display the playback window for a period after it was adjusted
boolean showingWindow = false; // are we busy displaying numbers

// for the rows of LEDs
// the byte that will be shifted out 
byte LEDrow[5];

// setup code for controlling the LEDs via 74HC595 serial to parallel shift registers
// based on ShiftOut http://arduino.cc/en/Tutorial/ShiftOut
const byte LEDlatchPin = 2;
const byte LEDclockPin = 6;
const byte LEDdataPin = 4;

// setup code for reading the buttons via CD4021B parallel to serial shift registers
// based on ShiftIn http://www.arduino.cc/en/Tutorial/ShiftIn
const byte BUTTONlatchPin = 7;
const byte BUTTONclockPin = 8;
const byte BUTTONdataPin = 9;

// needed by the button debouncer
// based on http://www.adafruit.com/blog/2009/10/20/example-code-for-multi-button-checker-with-debouncing/
#define DEBOUNCE 5
byte pressed[40], justpressed[40], justreleased[40], buttons[40];

// define the byte variables that the button states are read into
byte BUTTONvar[5];

// this determines what the buttons (and pots)are currently editing
byte mode = 0;
// 0 = step on/off
// 1 = accents
// 2 = flams
// 3 = master page
// 4 = trigger mode

byte *edit[4]; // here come the pointers - things are getting a little tricky
// we want to be able to choose which of the byte-arrays in the sequencer struct we want to edit
// sadly, we can't simply assign a whole array to a variable ie. editThis = stepMute[];
// that means we'd have to duplicate a bunch of code
// we can, however, assign the address in memory of a byte in the arrays to a pointer variable eg. *edit
// (the star/asterisk indicates that we're declaring a pointer variable)
// like so: edit1 = &seqStepMute[1] (the & means we're assigning the address of seqStepMute[1], NOT the value is holds)
// when we use the variable again, we'll use an asterik again (dereferencing);
// good info on pointers here http://www.tutorialspoint.com/cprogramming/c_pointers.htm
// and here http://pw1.netcom.com/~tjensen/ptr/ch1x.htm

// 4051 for the potentiometers
// array of pins used to select 1 of 8 inputs on multiplexer
const byte select[] = {
  17,16,15}; // pins connected to the 4051 input select lines
const byte analogPin = 0;      // the analog pin connected to multiplexer output

// variables for changing the voices of each oscilator
//byte voice1 = 0; 
//byte lastVoice1 = 0;

int decay = 100; // like filter decay/release

//byte xorDistortion = 0; // the amount of XOR distortion - set by pot2 in pot-shift mode
byte distortion = 0; // either the XOR distortion amount, or a fixed value for accents
//int xorDistPU = 50; // pickup shouldn't be true at the start


byte nowPlaying = 255; // the pattern that's currently playing. 255 is a value we'll use to check for if no pattern is currently playing
byte cued = 255; // the pattern that will play after this one. 255 is a value we'll use to check for if no pattern is currently cued
byte confirm = 255; // the memory location that will be overwritten - needs to be confirmed. 255 means nothing to confirm
boolean cancelSave = false;


byte bpmTaps[10];

// some functions take place over multiple loops, we use these at the top of the loop to check if we're still busy   
boolean loadContinue = false;
//********* UI **********

boolean shiftL = false;
boolean shiftR = false;


//***** PREFERENCES *****
//preferences - these are saved on the same EEPROM page (448) as the toc
byte thruOn; // echo MIDI data received at input at output: 0 = off, 1 = full 2 = DifferentChannel - all the messages but the ones on the Input Channel will be sent back (our Input Channel is Omni, so all note messages will be blocked, but clock will pass through).
boolean midiSyncOut; // send out MIDI sync pulses
boolean midiTrigger; // send out trigger messages on channel 10 for slaved Groovesizers
byte triggerChannel = 16;

boolean showingNumbers = false; // are we busy displaying numbers
byte number; // the value that will be displayed by the showNumber() function;

//***** MIDI *****
unsigned long lastClock = 1; // when was the last time we received a clock pulse (can't be zero at the start)
boolean midiClock = false; // true if we are receiving midi clock
boolean syncStarted = false; // so we can start and stop the sequencer while receiving clock
boolean clockStarted = false; // are we receiving clock and we've had a start message? 

// *************************************
//           THE SETUP
// *************************************

void setup() {

  //define pin modes
  pinMode(LEDlatchPin, OUTPUT);
  pinMode(LEDclockPin, OUTPUT); 
  pinMode(LEDdataPin, OUTPUT);

  pinMode(BUTTONlatchPin, OUTPUT);
  pinMode(BUTTONclockPin, OUTPUT); 
  pinMode(BUTTONdataPin, INPUT);

  for(byte bit = 0; bit < 3; bit++)
    pinMode(select[bit], OUTPUT);  // set the three 4051 select pins to output

  clearAll(0); // clear everything at the start, just to be on the safe side

  seqTrueStep = 0;
  seqCurrentStep = 0;

  // initialize the track variables
  for (byte i = 0; i < 12; i++)
  {
    track[i].midiChannel = 10;
    track[i].midiNoteNumber = 36 + i;
    track[i].level = 85;
    track[i].accentLevel = 127;
    track[i].flamDelay = 100;
    track[i].flamDecay = 20;
  }

  // Initiate MIDI communications, listen to all channels
  MIDI.begin(MIDI_CHANNEL_OMNI);    

  // Connect the HandleNoteOn function to the library, so it is called upon reception of a NoteOn.
  MIDI.setHandleNoteOn(HandleNoteOn);  // only put the name of the function here - functions defined in HandleMidi

  MIDI.setHandleNoteOff(HandleNoteOff);

  MIDI.setHandleClock(HandleClock);

  MIDI.setHandleStart(HandleStart);

  MIDI.setHandleStop(HandleStop); 

  //begin I2C Bus
  Wire.begin();

  //begin EEPROM with I2C Address 
  mem_1.begin(0,0);//addr 0 (DEC) type 0 (defined as 24LC256)

  // check if character definitions exists in EEPROM by seeing if the first 10 bytes of the expected character array add up to 93
  mem_1.readPage(508, rwBuffer); // 0 is the first page of the EEPROM (511 is last) - a page is 64 bytes long
  byte x = 0;
  for (byte i = 0; i < 10; i++)
  {
    x = x + rwBuffer[i];
  }
  if (x != 93) // yikes, there's a problem - the EEPROM isn't set up properly
  {
    //give us a bad sign!
    LEDrow[0] =  B10101010;
    LEDrow[1] =  B01010101;
    LEDrow[2] =  B10101010;
    LEDrow[3] =  B01010101;
    LEDrow[4] =  B10101010;
    for (byte i = 0; i < 100; i++)
    { 
      updateLeds();
      delay(5);
    }  
  }

  else
  {
    //give us a good sign!
    for (byte i = 0; i < 5; i++)
      LEDrow[i] =  B11111111;

    for (byte i = 0; i < 100; i++)
    { 
      updateLeds();
      delay(5);
    }
  }

  //set the pot locks
  for (byte i = 0; i < 20; i++) // do this a bunch of times for the average values to settle
    getPots();
  lockPot(6); // use value of 6 to lock all pots

  tocRead(); // update the toc array with a read from EEPROM page 448 - defined in HelperFunctions
  // also loads preferences

  triggerChannel = (triggerChannel > 16) ? 16 : triggerChannel; // in case the prefs haven't been saved yet and we have a weird value here
  thruOn = (thruOn > 2) ? 1 : thruOn; // again, in case prefs haven't been saved yet
  // midiSyncOut = true; // the MPX8 doesn't like sync out ;^(          

  // message on boot
  for (int i = 0; i < 1000; i++)
  {
    number = 20; // the version number
    showNumber();
    updateLeds();
  }
}


// *************************************
//           THE LOOP
// *************************************

// welcome to the loop
void loop() {
  // the following variables are all marked static, because we only want to create them once the first time through the loop
  // we create the variables here and not before the setup, because they only apply to the loop (they don't have global scope)

  static int longPressDur = 300; // the expected duration of a long press in milliseconds


  // blink the led for each step in the sequence
  static byte stepLEDrow[4];

  // controlLEDrow is defined as global variable above so we can set it from helper functions

  static unsigned long ledBlink; // for blinking an LED

  static unsigned long ledBlink2; // for blinking another LED

  // Call MIDI.read the fastest you can for real-time performance.
  MIDI.read();
  // there is no need to check if there are messages incoming if they are bound to a Callback function. 

  midiClock = (millis() - lastClock < 150) ? true : false; // are we currently receiving MIDI clock
  clockStarted = (syncStarted && midiClock) ? true : false; // both need to be true for clockStarted to be true

    boolean tempoDelayChange = false;

  if (loadContinue)
    loadPatch(cued);

  // we check buttons at the top of the loop, the "just" flags get reset here every time
  // it's best to reset the "just" flags manually whenever we check for "just" and find it true - use "clearJust()"
  // it prevents another just function from being true further down in the same loop
  check_switches(); //defined in ButtonCheck in the tab above

  // get the pot values
  getPots(); // defined in HelperFunctions

    // see if it's time to lock the pots
  if (lockTimer != 0 && millis() - lockTimer > 1000)
    lockPot(6); // value of 6 locks all pots

  ledsOff();

  buttonFunction(); // the functions of the buttons depend on the mode 

  switch (mode)
  {
    // *****************************
    // *** MODE 0 - STEP ON / OFF **
    // *****************************
  case 0:
    // POTS

    if (unlockedPot(5))
    {
      if (track[currentTrack].accentLevel != pot[5] >> 3)
      {
        track[currentTrack].accentLevel = pot[5] >> 3;
        lockTimer = millis();
        number = track[currentTrack].accentLevel;
      }
    }

    if (unlockedPot(4))
    {
      if (track[currentTrack].level != pot[4] >> 3)
      {
        track[currentTrack].level = pot[4] >> 3;
        lockTimer = millis();
        number = track[currentTrack].level;
      }
    }

    if (unlockedPot(3))
    {
      byte tmp = map(pot[3], 0, 1023, 40, 250);
      if (track[currentTrack].flamDelay != tmp)
      {
        track[currentTrack].flamDelay = tmp;
        lockTimer = millis();
        number = track[currentTrack].flamDelay;
      }
    }

    if (unlockedPot(2))
    {
      byte tmp = map(pot[2], 0, 1023, (track[currentTrack].level / 2), 1);
      if (track[currentTrack].flamDecay != tmp)
      {
        track[currentTrack].flamDecay = tmp;
        lockTimer = millis();
        number = track[currentTrack].flamDecay;
      }
    }

    if (unlockedPot(1))
    {
      byte tmp = map(pot[1], 0, 1023, 0, 127);
      if (track[currentTrack].midiNoteNumber != tmp)
      {
        track[currentTrack].midiNoteNumber = tmp;
        lockTimer = millis();
        number = track[currentTrack].midiNoteNumber;
      }
    }

    if (unlockedPot(0))
    {
      byte tmp = map(pot[0], 0, 1023, 1, 16);
      if (track[currentTrack].midiChannel != tmp)
      {
        track[currentTrack].midiChannel = tmp;
        lockTimer = millis();
        number = track[currentTrack].midiChannel;
      }
    }    

    // BUTTONS
    //change mode

    if (justpressed[32])
    {
      mode = 1;
      clearJust();  
    }
    if (justpressed[39])
    {
      mode = 2;
      clearJust();  
    }
    for (byte i = 0; i < 6; i++)
    {
      if (justpressed[33 + i])
      {
        if ((currentTrack < 6 && currentTrack == i) || (currentTrack > 5 && currentTrack == i + 6)) // the track is already selected
        {
          // send a note on that track
          MIDI.sendNoteOff(track[currentTrack].midiNoteNumber, track[currentTrack].level, track[currentTrack].midiChannel);
          MIDI.sendNoteOn(track[currentTrack].midiNoteNumber, track[currentTrack].level, track[currentTrack].midiChannel);
        }
        else // select the track
        {
          currentTrack = (currentTrack < 6) ? i : i + 6;
        }
        clearJust();
      }
    }
    buttonCheckSelected(); // defined in HelperFunctions
    break;      

    // *****************************
    // ***** MODE 1 - ACCENTS ******
    // *****************************
  case 1:
    if (justreleased[32])
    {
      if (shiftL)
      { 
        mode = 0; // back to step on/off
        shiftMode(); // lock pots, clear just array, shift L & R = false
      }
      else
      {
        mode = 3; // change to master page (mode 3)
        shiftMode(); // lock pots, clear just array, shift L & R = false
      }  
    }
    if (pressed[32]) // shift L is held
    {
      // pots
      if (unlockedPot(3)) // flamdelay according to tempo
      {
        byte tmp = map(pot[3], 0, 1023, 1, 8);
        if (track[currentTrack].flamDelay != tempoDelay(tmp))
        {
          track[currentTrack].flamDelay = tempoDelay(tmp);
          lockTimer = millis();
          number = tmp;
          shiftL = true;
        }
      }

      // buttons
      if (justreleased[33]) // clear array currently being edited
      {
        clearEdit();
        shiftL = true;
        clearJust();
      }
      else if (justreleased[39]) // clear everything
      {
        clearAll(0);
        shiftL = true;
        clearJust();
      }
    }

    addAccent(); // check if a button was pressed and adds an accent
    break;

    // *****************************
    // ****** MODE 2 - FLAMS *******
    // *****************************
  case 2:
    if (justreleased[39])
    {
      if (!shiftR)
      {
        currentTrack = (currentTrack < 6) ? currentTrack + 6 : currentTrack - 6;
        mode = 0; 
        clearJust();
      }
      else
      {
        shiftR = false;
        mode = 0;
        shiftMode(); // lock pots, clear just array, shift L & R = false
      }
    }
    addFlam();
    break;


    // *****************************
    // *** MODE 3 - MASTER PAGE ****
    // *****************************
  case 3:

    // should we be displaying the playback window?
    if (windowTimer != 0 && millis() - windowTimer < 1000)
      showingWindow = true;
    else if (windowTimer != 0)
    {
      showingWindow = false;
      windowTimer = 0;
      lockPot(6);
    }

    // POTS
    if (unlockedPot(0)) // program change
    {
      if (programChange != map(pot[0], 0, 1023, 0, 127))
      {
        programChange = map(pot[0], 0, 1023, 0, 127);
        MIDI.sendProgramChange(programChange, track[0].midiChannel);
        lockTimer = millis();
        number = programChange;
      }
    }

    if (unlockedPot(1)) // set the start of the playback window
    {
      if (seqFirstStep != map(pot[1], 0, 1023, 0, 32 - seqLength))
      {
        seqFirstStep = map(pot[1], 0, 1023, 0, 32 - seqLength);
        seqLastStep = seqFirstStep + (seqLength - 1);
        seqCurrentStep = seqFirstStep; // adjust the playback position
        windowTimer = millis();
        showingNumbers = false;
      }
    }

    if (unlockedPot(2)) // set the first step
    {
      if (seqFirstStep != map(pot[2], 0, 1023, 0, seqLastStep))
      {
        seqFirstStep = map(pot[2], 0, 1023, 0, seqLastStep);
        windowTimer = millis();
        seqLength = (seqLastStep - seqFirstStep) + 1;
        showingNumbers = false;
      }
    }

    if (unlockedPot(3)) // set the last step
    {
      if (seqLastStep != map(pot[3], 0, 1023, seqFirstStep, 31))
      {
        seqLastStep = map(pot[3], 0, 1023, seqFirstStep, 31);
        windowTimer = millis();
        seqLength = (seqLastStep - seqFirstStep) + 1;
        showingNumbers = false;
      }
    }

    if (unlockedPot(4)) // adjust swing
    {
      if (swing != map(pot[4], 0, 1023, 0, 255))
      {
        swing = map(pot[4], 0, 1023, 0, 255);
        lockTimer = millis();
        number = swing;
      }
    }

    if (unlockedPot(5)) // adjust bpm
    {
      if (bpm != map(pot[5], 0, 1023, 45, 255))
      {
        bpmChange(map(pot[5], 0, 1023, 45, 255));
        lockTimer = millis();
        number = bpm;
      }
    }

    // should we be showing numbers for button presses?
    if (buttonTimer != 0 && millis() - buttonTimer < 1000)
      showingNumbers = true;
    else if (buttonTimer != 0)
    {
      showingNumbers = false;
      buttonTimer = 0;
    }

    // buttons
    if (pressed[32])
    {
      if (justpressed[33])
      {       
        clearEdit();
        shiftL = true;
        clearJust();
      }

      if (justpressed[39])
      {       
        clearAll(0);
        shiftL = true;
        clearJust();
      }
    }

    if (pressed[39])
    {
      if (justreleased[32])
      {
        mode = 5; // change to references mode
        lockPot(6);
        clearJust();
      }
    }

    if (justreleased[32])
    {
      if (shiftL) // don't change mode
      { 
        shiftMode(); // lock pots, clear just array, shift L & R = false
      }
      else
      {
        mode = 4; // change to trigger mode(mode 3)
        shiftMode(); // lock pots, clear just array, shift L & R = false 
      }  
    }

    if (justpressed[33]) // stop/start the sequencer
    {
      if (!midiClock)
      {
        seqRunning = !seqRunning;

        if (!seqRunning)
        {
          seqReset();
          if (midiSyncOut)
            MIDI.sendRealTime(midi::Stop); // send a midi clock stop signal)
        }
        else if (midiSyncOut) // && seqRunning
          MIDI.sendRealTime(midi::Start); // send a midi clock start signal)
      }
      else // midiClock is present
      {
        syncStarted = !syncStarted;
        if (!syncStarted)
          seqReset();
        else
          seqMidiStep = true; // without this line, the first step is skipped
      } 
      clearJust();
    }

    if (justpressed[34])
    {
      tapTempo();
      clearJust();
    }

    if (justpressed[35]) // switch to mute page
    {
      mutePage = true;
      skipPage = false;
      clearJust();  
    }

    if (justpressed[36]) // switch to skip page
    {
      mutePage = false;
      skipPage = true;
      clearJust();  
    }

    if (justpressed[37]) // nudge tempo slower
    {
      if (!clockStarted)
      {
        byte tmpBPM = (bpm > 45) ? bpm - 1 : bpm; 
        bpmChange(tmpBPM);
        buttonTimer = millis();
        number = bpm;
      }
      else
        pulse = (pulse != 0) ? pulse - 1 : 0; 
      clearJust();
    }

    if (justpressed[38]) // nudge tempo faster
    {
      if (!clockStarted)
      {
        byte tmpBPM = (bpm < 255) ? bpm + 1 : bpm; 
        bpmChange(tmpBPM);
        buttonTimer = millis();
        number = bpm;
      }
      else 
        pulse++;
      clearJust();
    }

    if (justreleased[39]) // set trueStep - useful to flip the swing "polarity"
    {
      if (shiftR)
      {
        shiftMode();
      }
      else
      {
        seqCurrentStep = seqTrueStep;
        clearJust();
      }
    }

    buttonCheckSelected();

    break;

    // *****************************
    // *** MODE 4 - TRIGGER MODE ***
    // *****************************
  case 4:
    if (justreleased[32])
    {
      if (shiftL) // don't change mode
      { 
        shiftMode(); // lock pots, clear just array, shift L & R = false
      }
      else
      {
        mode = 0; // change to step on/off
        shiftMode(); // lock pots, clear just array, shift L & R = false 
      }  
    }

    for (byte i = 0; i < 32; i++)
    {
      if (justpressed[i])
      {
        i += (trigPage * 32); // there are 32 locations to a page
        if (i < 112) // there are only 112 save locations
        {
          if (pressed[32])
          {
            if (i != nowPlaying && checkToc(i))
            {       
              clearMem = true;
              confirm = i;
              shiftL = true;
              clearJust;
            }
            else
            {
              shiftL = true;
              clearJust;
              confirm = 255;
            }  
          }
          else
          {                  
            if (i == confirm)
            {
              if (clearMem)
              {
                clearMem = false;
                tocClear(confirm);
                confirm = 255; // turns confirm off - we chose 255 because it's a value that can't be arrived at with a buttonpress
                clearJust;
                longPress = 0; 
              }
              else
              {
                save = true; // we're go to save a preset
                pageNum = confirm*4; // the number of the EEPROM page we want to write to is the number of the button times 4 - one memory location is 4 pages long
                tocWrite(confirm); // write a new toc that adds the current save location
                clearJust;
                confirm = 255; // turns confirm off                     
              }
            }
            else if (confirm == 255)
            {
              longPress = millis();
              save = false;
              recall = false;
              clearJust();
            }
            else
            {
              confirm = 255;
              save = false;
              recall = false;
              cancelSave = true;
              clearJust();
            }
          }
        }
        clearJust();
      }

      else if (pressed[i])
      {
        i += (trigPage * 32); // multiply the trigpage by 32
        if (i < 112) // there are only 112 save locations
        {
          if (longPress != 0 && (millis() - longPress) > longPressDur) // has the button been pressed long enough
          {
            if(!checkToc(i))
            {
              save = true; // we're go to save a preset
              pageNum = i*4; // the number of the EEPROM page we want to write to is the number of the button times 4 - one memory location is 4 pages long
              tocWrite(i); // write a new toc that adds the current save location
              longPress = 0;
            }
            else // a patch is already saved in that location
            {
              confirm = i;
              longPress = 0;
            }
          }
        }
      }
      else if (justreleased[i]) // we don't have to check if save is true, since we wouldn't get here if it were
      {
        i += (trigPage * 32); // multiply the trigpage by 32 and add to to i
        if (i < 112) // there are only 112 save locations
        {
          if (cancelSave)
          {
            cancelSave = false;
          }
          else if (checkToc(i) && confirm == 255)
          {
            recall = true; // we're ready to recall a preset
            pageNum = i*4; // one memory location is 4 pages long
            clearJust();
            if (midiTrigger)
            {
              MIDI.sendNoteOn(i, 127, triggerChannel); // send a pattern trigger note on triggerChannel (default is channel 10)
            }
            cued = i; // the pattern that will play next is i - need this to blink lights
            head = i; // the first pattern in a series of chained patterns
            followAction = 0; // allow the next loaded patch to prescribe to followAction
          }
        }
      }  
    } 
    if (pressed[32])
    {
      if (justpressed[33])
      {
        trigPage = 0;
        clearJust();
        shiftL = true;    
      }  
      else if (justpressed[34])
      {
        trigPage = 1;
        clearJust();
        shiftL = true;  
      }
      else if (justpressed[35])
      {
        trigPage = 2;
        clearJust();
        shiftL = true;  
      }
      else if (justpressed[36])
      {
        trigPage = 3;
        clearJust();
        shiftL = true;
      }
      else if (justpressed[37]) // set followAction to 2 - return to the head
      {
        if (followAction != 2)
        {
          followAction = 2;
          //saved = false; // these follow actions only become active once the patch has been saved and loaded
          shiftL = true;
        }
        else
        {
          followAction = 0;
          shiftL = true;
        }
        save = true; // save the patch with its new followAction
        clearJust();
      }
      else if (justpressed[38]) // set followAction to 1 - play the next pattern
      {
        if (followAction != 1)
        {
          followAction = 1;
          saved = false; // these follow actions only become active once the patch has been saved and loaded
          shiftL = true;
        }
        else
        {
          followAction = 0;
          shiftL = true;
        }
        save = true; // save the patch with its new followAction 
        clearJust();
      }
    }
    else if (pressed[33]) // step repeat
    {
      seqNextStep = 0; 
      if (justpressed[37])
        {
          seqCurrentStep = (seqCurrentStep - 1) % seqLength;
        }
        else if (justpressed[38])
        {
          seqCurrentStep = (seqCurrentStep + 1) % seqLength;
          clearJust();
        }
      clearJust();
    }
    else if (justreleased[33])
    {
      seqNextStep = 1;
      seqCurrentStep = seqTrueStep;
      clearJust();
    }
    else if (pressed[34]) // momentary reverse
    {
      seqNextStep = -1; 
      clearJust();
    }
    else if (justreleased[34])
    {
      seqNextStep = 1;
      seqCurrentStep = seqTrueStep;
      clearJust();
    }

    else if (justpressed[35])
    {
      fxFlamSet();
      fxFlamDelay = (sixteenthDur * 4) / 3;
      fxFlamDecay = 10;
      clearJust();
    }

    if(save)
      savePatch();

    break;

    // *****************************
    // *** MODE 5 - PREFERENCES  ***
    // *****************************
  case 5:
    showingNumbers = false;

    // buttons
    if (justpressed[32])
    {
      mode = 3;
      shiftL = true;
      savePrefs(); // save the preferences on exit
    }

    if (justpressed[39])
    {
      mode = 3;
      shiftR = true;
      savePrefs(); // save the preferences on exit
    }

    if (justpressed[33])
    {
      midiSyncOut = !midiSyncOut;
      clearJust();
    }

    if (pressed[34])
    {
      showingNumbers = true;
      number = thruOn;
      if (unlockedPot(5)) // adjust midi echo mode
      {
        if (thruOn != map(pot[5], 0, 1023, 0, 2))
        {
          thruOn = map(pot[5], 0, 1023, 0, 2);
          lockTimer = millis();
        }
      }
    }

    if (pressed[35])
    {
      showingNumbers = true;
      number = triggerChannel;
      if (unlockedPot(5)) // adjust trigger channel
      {
        if (triggerChannel != map(pot[5], 0, 1023, 0, 16))
        {
          triggerChannel = map(pot[5], 0, 1023, 0, 16);
          midiTrigger = (triggerChannel == 0) ? false : true;
          lockTimer = millis();
        }
      }
    }

    break;
  }
  // ******************************************
  //     LED setup: work out what is lit
  //     end of button check if/else structure
  // ******************************************
  if (showingNumbers)
  {
    showNumber();
  }
  else if (showingWindow)
  {
    for (byte i = seqFirstStep; i < (seqFirstStep + seqLength); i++)
      bitSet(LEDrow[i / 8], i % 8);
  }
  else
  {
    if (mode < 3) // editing tracks 0 - 11
    {
      // we always start with steps that are On and then work out what to blink or flash from there
      for (byte i = 0; i < 32; i++)
      {
        if (checkStepOn(currentTrack, i))
          bitSet(LEDrow[i / 8], i % 8); 
      }
    }

    if (mode == 3)
    {
      for (byte i = 0; i < 12; i++) // show all the active steps (on all tracks) on one grid
      {
        for (byte j = 0; j < 4; j++)
          LEDrow[j] |= track[i].stepOn[j]; 
      }
    }

    if (mode < 4 && mode > 0) // blink selected steps if we're not in trigger mode
    {
      // blink step selected leds
      if (millis() > ledBlink + 100)
      {
        for (byte i = 0; i < 4; i++)
          LEDrow[i] ^= *edit[i];

        if (millis() > ledBlink + 200)
          ledBlink = millis(); 
      } 
    }

    if (mode == 4) // we're in trigger mode
    {
      for (byte i = 0; i < 4; i++)
        LEDrow[i] = toc[trigPage*4 + i];

      // slow blink nowPlaying
      if (nowPlaying < 255 && nowPlaying / 32 == trigPage) // we use 255 to turn nowPlaying "off", and only show if we're on the appropriate trigger page
      {
        if (millis() > ledBlink + 200)
        {
          LEDrow[(nowPlaying % 32) / 8] ^= 1<<(nowPlaying%8);

          if (millis() > ledBlink + 400)
            ledBlink = millis(); 
        }
      } 

      // (fast) blink the pattern that's cued to play next
      if (cued < 255 && cued / 32 == trigPage) // we use 255 turn cued "off", and only show if we're on the appropriate trigger page
      {
        if (millis() > ledBlink2 + 100)
        {
          LEDrow[(cued % 32) / 8] ^= 1<<(cued%8);

          if (millis() > ledBlink2 + 200)
            ledBlink2 = millis(); 
        }
      }

      // (fast) blink the location that needs to be confirmed to overwrite
      else if (confirm < 255 && confirm / 32 == trigPage)
      {
        if (millis() > ledBlink2 + 100)
        {
          LEDrow[(confirm % 32) / 8] ^= 1<<(confirm%8);

          if (millis() > ledBlink2 + 200)
            ledBlink2 = millis(); 
        }
      }

      bitClear(controlLEDrow, 6);
      bitClear(controlLEDrow, 5);
    }

    // this blinks the led for the current step
    if (seqRunning || clockStarted)
    {
      for (byte i = 0; i < 4; i++)
      {
        stepLEDrow[i] = (seqCurrentStep / 8 == i) ? B00000001 << (seqCurrentStep % 8) : B00000000;
        LEDrow[i] ^= stepLEDrow[i];
      }
    }

    // update the controlLEDrow
    switch (mode)
    {
    case 0:
      controlLEDrow = B00000010;
      controlLEDrow = controlLEDrow << currentTrack % 6;
      if (currentTrack > 5)
        bitSet(controlLEDrow, 7);
      break;
    case 3:
      controlLEDrow = B10000001; // master page
      if (seqRunning || clockStarted)
        bitSet(controlLEDrow, 1);
      if (mutePage)
        bitSet(controlLEDrow, 3);
      if (skipPage)
        bitSet(controlLEDrow, 4);        
      break;
    case 4:
      controlLEDrow = B00000001; // trigger page
      controlLEDrow = bitSet(controlLEDrow, trigPage + 1); // light the LED for the page we're on
      if (followAction == 1 || followAction == 2)  // light the LED for followAction 1 or 2
          controlLEDrow = bitSet(controlLEDrow, 7 - followAction);
      break;
    case 5:
      controlLEDrow = 0; // clear it
      // set according to the preferences
      if (midiSyncOut)
        bitSet(controlLEDrow, 1);
      if (thruOn)
        bitSet(controlLEDrow, 2);
      if (midiTrigger)
        bitSet(controlLEDrow, 3);
      break;
    }

    LEDrow[4] = controlLEDrow; // light the LED for the mode we're in
  }
  // this function shifts out the the 5 bytes corresponding to the led rows
  // declared in ShiftOut (see above)
  updateLeds();

  // *************************************
  //     SEQUENCER inside the LOOP
  // *************************************

  if (currentTime == 0 && (seqRunning || clockStarted)) // the fist time through the loop
  {
    stepGo = true;
    currentTime = millis();
    sixteenthStartTime = currentTime;
    pulseStartTime = currentTime;
    swing16thDur = map(swing, 0, 255, sixteenthDur, ((2*sixteenthDur)/3)*2);
    autoCounter = 0;
  }
  else if (seqRunning) 
  {
    currentTime = millis();

    if (clockPulse < 11) // if the clockPulse is 11, wait for the next 8th (16th may swing) so they stay nicely in sync
    {
      if (currentTime - pulseStartTime >= clockPulseDur) // is it time to fire a clock pulse?
      {
        pulseStartTime = currentTime;
        if (midiSyncOut)
        {
          MIDI.sendRealTime(midi::Clock); // send a midi clock pulse
        }
        sendCC();
        clockPulse++; // advance the pulse counter
        autoCounter = (autoCounter < 191) ? autoCounter + 1 : 0;          
      }  
    }

    if (currentTime - sixteenthStartTime >= swing16thDur)
    { 
      //swing16thDur = (swing16thDur > sixteenthDur) ? ((2*sixteenthDur) - swing16thDur) : map(swing, 0, 255, sixteenthDur, ((2*sixteenthDur)/3)*2);
      //onDur = (onDur > swing16thDur) ? swing16thDur : onDur;
      stepGo = true;
      sixteenthStartTime = currentTime;
      seqTrueStep = (seqTrueStep+1)%32;
      if (seqTrueStep%2 != 0) // it's an uneven step, ie. the 2nd shorter 16th of the swing-pair
        swing16thDur = (2*sixteenthDur) - swing16thDur;
      else // it's an even step ie. the first longer 16th of the swing-pair
      {
        swing16thDur = map(swing, 0, 255, sixteenthDur, ((2*sixteenthDur)/3)*2);
        //MIDI.sendSongPosition(seqCurrentStep); 

        while (clockPulse < 11)
        {
          if (midiSyncOut)
          {
            MIDI.sendRealTime(midi::Clock); // send a midi clock pulse
          }
          sendCC();
          clockPulse++;
          delay(1);
        }

        if (clockPulse = 11)
        {
          pulseStartTime = currentTime;
          clockPulse = 0;
          if (midiSyncOut)
          {
            MIDI.sendRealTime(midi::Clock); // send a midi clock pulse
          }
          sendCC();  
        }
      }
      //     onDur = (onDur > swing16thDur) ? swing16thDur : onDur;

      do 
      {
        if (seqNextStep >= 0)
          seqCurrentStep = (((seqCurrentStep - seqFirstStep) + seqNextStep)%seqLength) + seqFirstStep; // advance the step counter
        else // seqNextStep is negative
        {
          if ((seqCurrentStep + seqNextStep) < seqFirstStep || (seqCurrentStep + seqNextStep) > 32)
            seqCurrentStep = seqLastStep + 1 + ((seqCurrentStep - seqFirstStep) + seqNextStep);
          else
            seqCurrentStep += seqNextStep;
        }
      }
      while (checkSkip(seqCurrentStep)); // do it again if the step is marked as a skip

        if (seqCurrentStep == 0)
      {
        autoCounter = 0;
      }
      else  
        autoCounter = (autoCounter < 191) ? autoCounter + 1 : 0;
    }
  }

  // is it time to move to the next step?
  if ((stepGo || seqMidiStep))
  {
    if (seqCurrentStep == 0 || seqCurrentStep == 16 || seqCurrentStep == seqFirstStep)
    {
      if (recall)
      {
        loadPatch(cued); // defined in HelperFunctions - everything that happens with a patch/pattern change
      }
      //deal with the follow actions if we're on the first step
      else if (seqCurrentStep == 0 && followAction != 0)
      {
        switch (followAction)
        {
        case 1: // play the next pattern      
          if (checkToc(nowPlaying + 1) && saved == true) // check if there is a pattern stored in the next location
          {
            loadPatch(nowPlaying + 1); // load the next patch
          }
          break;
        case 2:
          if (saved == true) // we don't want to go over page breaks (30 is the second to last location on the page), and check if there is a pattern stored in the next location
          {
            loadPatch(head); // load the patch marked as the head
          }
          break;
        }
      }
    }

    stepGo = false;
    seqMidiStep = false;

    if (!checkMute(seqCurrentStep)) // don't play anything is the step is muted on the master page
    {
      for (byte i = 0; i < 12; i++) // for each of the tracks
      {
        if (fxFlam[i] == 1 && (seqCurrentStep % 2 == 0) && !checkStepFlam(i, seqCurrentStep))
        {
          scheduleFlam(i, track[i].accentLevel);
        }
        if (checkStepAccent(i, seqCurrentStep))
        {
          MIDI.sendNoteOff(track[i].midiNoteNumber, track[i].accentLevel, track[i].midiChannel);
          MIDI.sendNoteOn(track[i].midiNoteNumber, track[i].accentLevel, track[i].midiChannel);
          if (checkStepFlam(i, seqCurrentStep)) // schedule the first flam if this step is marked as flam
          {
            scheduleFlam(i, track[i].accentLevel);
          }
          else // if the this step is not marked as a flam, turn off any sceduled flams
          track[i].nextFlamTime = 0;
        }
        else if (checkStepOn(i, seqCurrentStep))
        {
          MIDI.sendNoteOff(track[i].midiNoteNumber, track[i].level, track[i].midiChannel);
          MIDI.sendNoteOn(track[i].midiNoteNumber, track[i].level, track[i].midiChannel);
          if (checkStepFlam(i, seqCurrentStep)) // schedule the first flam if this step is marked as flam
            scheduleFlam(i, track[i].level);
          else // if the this step is not marked as a flam, turn off any sceduled flams
          track[i].nextFlamTime = 0;
        }
      }
    }
  }
  // see if it's time to play a flam, and schedule the next one
  for (byte i = 0; i < 12; i++) // for each of the twelve tracks
  {
    if (track[i].nextFlamTime != 0 && millis() > track[i].nextFlamTime)
    {
      // send the flam note
      MIDI.sendNoteOff(track[i].midiNoteNumber, 127, track[i].midiChannel);
      MIDI.sendNoteOn(track[i].midiNoteNumber, track[i].nextFlamLevel, track[i].midiChannel);

      // schedule the next flam
      scheduleFlam(i, track[i].nextFlamLevel); 
    }
  }
}



















