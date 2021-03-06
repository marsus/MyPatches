#ifndef __PolyVoicePatch_hpp__
#define __PolyVoicePatch_hpp__

#include "Patch.h"
#include "Envelope.h"
#include "VoltsPerOctave.h"
#include "PolyBlepOscillator.h"
#include "BiquadFilter.h"
#include "SmoothValue.h"

// The per-note messages sent over each channel are limited to Note On, Note Off, Channel Pressure (for finger pressure), Pitch Bend (for x-axis movement) and CC74 (for Y-axis movement).
  
class SynthVoice {
private:
  PolyBlepOscillator osc;
  BiquadFilter* filter;
  AdsrEnvelope env;
  SmoothFloat fc, q;
  float gain;
public:
  SynthVoice(float sr, BiquadFilter* f) : 
    osc(sr), filter(f), env(sr), fc(0.25), q(0.77), gain(1.0f) {    
    env.setSustain(1.0);
    env.setDecay(0.0);
    env.setRelease(0.0);
  }
  static SynthVoice* create(float sr){
    // 8-pole filter 48dB
    return new SynthVoice(sr, BiquadFilter::create(4));
  }
  static void destroy(SynthVoice* voice){
    BiquadFilter::destroy(voice->filter);    
  }
  void setFrequency(float freq){
    osc.setFrequency(freq);
  }
  void setFilter(float freq, float res){
    fc = min(0.45, max(0.05, freq));
    fc = freq;
    q = res;
  }
  void setWaveshape(float shape){
    float pw = 0.5;
    if(shape > 1.0){
      pw += 0.49*(shape-1.0); // pw 0.5 to 0.99
      shape = 1.0; // square wave
    }
    osc.setShape(shape);
    osc.setPulseWidth(pw);
  }
  void setEnvelope(float attack, float release){
    env.setAttack(attack);
    env.setRelease(release);    
  }
  void setGain(float value){
    gain = value;
  }
  void setGate(bool state, int delay){
    env.gate(state, delay);
  }
  void getSamples(FloatArray samples){
    filter->setLowPass(fc, q);
    osc.getSamples(samples);
    filter->process(samples);
    samples.multiply(gain*(0.8-q*0.2)); // gain compensation for high q
    env.attenuate(samples);
  }
};

template<int VOICES>
class Voices {
private:
  static const uint8_t EMPTY = 0xff;
  static const uint16_t TAKEN = 0xffff;
  SynthVoice* voice[VOICES];
  uint8_t notes[VOICES];
  uint16_t allocation[VOICES];
  uint16_t allocated;
  FloatArray buffer;

private:
  void take(uint8_t ch, uint8_t note, uint16_t velocity, uint16_t samples){
    notes[ch] = note;
    allocation[ch] = TAKEN;
    voice[ch]->setGain(velocity/(4095.0f*(VOICES/2)));
    voice[ch]->setGate(true, samples);
  }

  void release(uint8_t ch, uint16_t samples){
    // notes[ch] = EMPTY;
    allocation[ch] = ++allocated;
    voice[ch]->setGate(false, samples);
  }

public:
  Voices(float sr, int bs) : allocated(0) {
    for(int i=0; i<VOICES; ++i){
      voice[i] = SynthVoice::create(sr);
      notes[i] = 69; // middle A, 440Hz
      allocation[i] = 0;
    }
    buffer = FloatArray::create(bs);
  }

  ~Voices(){
    for(int i=0; i<VOICES; ++i)
      SynthVoice::destroy(voice[i]);
    FloatArray::destroy(buffer);
  }

  void allNotesOff(){
    for(int i=0; i<VOICES; ++i)
      release(i, 0);
    allocated = 0;
  }

  void allNotesOn(){
    for(int i=0; i<VOICES; ++i){
      voice[i]->setGain(0.6/VOICES);
      voice[i]->setGate(true, 0);
    }
  }

  void noteOn(uint8_t note, uint16_t velocity, uint16_t samples){
    uint16_t minval = allocation[0];
    uint8_t minidx = 0;
    // take oldest free voice, to allow voices to ring out
    for(int i=1; i<VOICES; ++i){
      if(notes[i] == note){
	minidx = i;
	break;
      }
      if(allocation[i] < minval){
	minidx = i;
	minval = allocation[i];
      }
    }
    // take oldest voice
    take(minidx, note, velocity, samples);
  }

  void noteOff(uint8_t note, uint16_t samples){
    for(int i=0; i<VOICES; ++i)
      if(notes[i] == note)
	release(i, samples);
  }

  void setParameters(float shape, float fc, float q, float attack, float release, float pb){
    for(int i=0; i<VOICES; ++i){
      float freq = 440.0f*exp2f((notes[i]-69 + pb*2)/12.0);
      voice[i]->setFrequency(freq);
      voice[i]->setWaveshape(shape);
      voice[i]->setFilter(fc, q);
      voice[i]->setEnvelope(attack, release);
    }
  }

  void getSamples(FloatArray samples){
    voice[0]->getSamples(samples);
    for(int i=1; i<VOICES; ++i){
      voice[i]->getSamples(buffer);
      samples.add(buffer);
    }
  }
};

class PolyVoicePatch : public Patch {
private:
  Voices<8> voices;
public:
  PolyVoicePatch() : voices(getSampleRate(), getBlockSize()) {
    registerParameter(PARAMETER_A, "Waveshape");
    registerParameter(PARAMETER_B, "Fc");
    registerParameter(PARAMETER_C, "Resonance");
    registerParameter(PARAMETER_D, "Envelope");
  }
  ~PolyVoicePatch(){
  }
  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples){
    if(bid >= MIDI_NOTE_BUTTON){
      debugMessage("note/velocity", bid - MIDI_NOTE_BUTTON, value);
      uint8_t note = bid - MIDI_NOTE_BUTTON;
      if(value) // note on
	voices.noteOn(note, value, samples);
      else // note off
	voices.noteOff(note, samples);
    }else if(bid == PUSHBUTTON){
      if(value)
	voices.allNotesOn();
      else
	voices.allNotesOff();
    }
  }
  void processAudio(AudioBuffer &buffer) {
    float shape = getParameterValue(PARAMETER_A)*2;
    float cutoff = getParameterValue(PARAMETER_B)*0.48 + 0.01;
    float q = getParameterValue(PARAMETER_C)*3+0.75;
    float df = getParameterValue(PARAMETER_D)*4;
    cutoff += getParameterValue(PARAMETER_F)*0.25; // MIDI CC1/Modulation
    float pitchbend = getParameterValue(PARAMETER_G); // MIDI Pitchbend
    int di = (int)df;
    float attack, release;
    switch(di){
      // a/d
    case 0: // l/s
      attack = 1.0-df;
      release = 0.0;
      break;
    case 1: // s/s
      attack = 0.0;
      release = df-1;
      break;
    case 2: // s/l
      attack = df-2;
      release = 1.0;
      break;
    case 3: // l/l
      attack = 1.0;
      release = 1.0;
      break;
    }
    FloatArray left = buffer.getSamples(LEFT_CHANNEL);
    FloatArray right = buffer.getSamples(RIGHT_CHANNEL);
    voices.setParameters(shape, cutoff, q, attack, release, pitchbend);
    voices.getSamples(left);
    right.copyFrom(left);
  }
};

#endif   // __PolyVoicePatch_hpp__
