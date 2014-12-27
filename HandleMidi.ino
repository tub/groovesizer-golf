// midi.h library info http://playground.arduino.cc/Main/MIDILibrary
// Reference http://arduinomidilib.fortyseveneffects.com/

boolean windMode = false;
byte lastNote = 0;

void HandleNoteOn(byte channel, byte pitch, byte velocity) 
{ 
  // Do whatever you want when you receive a Note On.
  // Try to keep your callbacks short (no delays ect) as the contrary would slow down the loop()
  // and have a bad impact on real-time performance.
  if (channel == 10 && pitch < 112) // receive patch changes on triggerChannel (default channel 10); there are 112 memory locations
  {      
    if (checkToc(pitch))
    {
      recall = true; // we're ready to recall a preset
      pageNum = pitch * 4; // one memory location is 4 pages long
      cued = pitch; // the pattern that will play next is the value of "pitch" - need this to blink lights
      head = pitch; // need this for pattern chaining to work properly
      mode = 4; // make sure we're in trigger mode, if we weren't already
      trigPage = pitch/32; // switch to the correct trigger page
      controlLEDrow = B00000001; // light the LED for trigger mode
      controlLEDrow = bitSet(controlLEDrow, trigPage + 1); // light the LED for the page we're on     
    }
  }
  else if (mode != 7  && pitch > 0 && channel == 10)
  {
    if (!seqRunning && !midiClock)
    { 
      if (velocity != 0) // some instruments send note off as note on with 0 velocity
      {
        lastNote = pitch;
        if (!windMode)
        {

          int newVelocity = (pot[2] + velocity < 1023) ? pot[2] + velocity : 1023; 

        }
        //automate = false;
        //delay(2);
      }
      else // velocity = 0;
      {
      }
    }
  }
}

void HandleNoteOff(byte channel, byte pitch, byte velocity) 
{
} 

void HandleClock(void) // what to do for 24ppq clock pulses
{
  lastClock = millis();
  static unsigned long sixPulses; // 6 pulses is a sixteenth
  seqRunning = false;
  if (syncStarted)
  {
    pulse++; // 6 for a 16th
    autoCounter = (autoCounter < 191) ? autoCounter + 1 : 0;
    //if (!updateAuto)
    //  sendCC(); 
    static boolean long16th = true; // is this the first/longer 16th of the swing pair?
    char swingPulse = swing / 86; // to give us some swing when synced to midi clock (sadly only 2 levels: full swing and half swing)
    // takes a value between 0 and 255 and returns 0, 1 or 2
    swingPulse = (!long16th) ? (0 - swingPulse) : swingPulse;                              
    if (pulse >= (6 + swingPulse))
    {
      currentTime = millis();
      pulse = 0;
      seqMidiStep = true;
      //seqTrueStep = (seqTrueStep+1)%32; 
      if (seqNextStep == -1 && seqCurrentStep == 0) // special case for reversed pattern
        seqCurrentStep = seqLength - 1;
      else
        seqCurrentStep = (((seqCurrentStep - seqFirstStep) + seqNextStep)%seqLength) + seqFirstStep; // advance the step counter
      long16th = !long16th;

      sixteenthStartTime = millis();
    }
  }
  if (pulse == 0) // so we can do tempo synced flams/delays when receiving MIDI clock
  {
    unsigned long tmpTime = millis();
    sixteenthDur = tmpTime - sixPulses;
    sixPulses = tmpTime;
  }
}

void HandleStart (void)
{
  syncStarted = true;
  seqMidiStep = true;
  seqReset();
}

void HandleStop (void)
{
  syncStarted = false;
  seqReset();
//  if (midiNoteOut)
//  {
//    for (byte i = 0; i < 128; i++) // turn off all midi notes
//      MIDI.sendNoteOff(i, 0, noteChannel);
//  }
  seqRunning = false;
}

void sendCC()
{
}
