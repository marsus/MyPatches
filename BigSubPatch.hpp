#ifndef __BigSubPatch_hpp__
#define __BigSubPatch_hpp__

#include "Patch.h"
#include "Envelope.h"
#include "VoltsPerOctave.h"
#include "PolyBlepOscillator.h"
#include "BiquadFilter.h"
#include "SmoothValue.h"
#include "SineOscillator.h"
#include "Phasor.h"

#define MIDI_VOICES 8
#define PITCHBEND_RANGE 8

int8_t basenote = -69;

/*
  PolySub version for Magus
  todo: filter envelope
  pitch | waveshape | cutoff | resonance | LFO1 rate | LFO1 out  | LFO2 rate | LFO2 out |
  a     | d         | s      | r         | a         | d         | s      | r           |
*/
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
  void setEnvelope(float attack, float decay, float sustain, float release){
    env.setAttack(attack);
    env.setDecay(decay);
    env.setSustain(sustain);
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
    // if(notes[ch] != note && allocation[ch] == TAKEN){
    //   // stealing channel
      release(ch, samples);
    // }else{
      notes[ch] = note;
      allocation[ch] = TAKEN;
    // }
    float gain = velocity*velocity/16129.0f + 0.1;
    gain *= 1.0f/VOICES;
    voice[ch]->setGain(gain);
    voice[ch]->setGate(true, samples);
    // debugMessage("gain", gain);
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
      voice[i]->setGain(0.5/VOICES);
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
    // debugMessage("idx/note/vel", minidx, note, velocity);
  }

  void noteOff(uint8_t note, uint16_t samples){
    for(int i=0; i<VOICES; ++i)
      if(notes[i] == note)
	release(i, samples);
  }

  void setParameters(float shape, float fc, float q, float attack, float decay, float sustain, float release, float pb){
    for(int i=0; i<VOICES; ++i){
      float freq = 440.0f*exp2f((notes[i]+basenote + pb*PITCHBEND_RANGE)/12.0);
      voice[i]->setFrequency(freq);
      voice[i]->setWaveshape(shape);
      voice[i]->setFilter(fc, q);
      voice[i]->setEnvelope(attack, decay, sustain, release);
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

class BigSubPatch : public Patch {
private:
  Voices<MIDI_VOICES> voices;
  BiquadFilter* hp;
  Phasor lfo1;
  SineOscillator lfo2;
public:
  BigSubPatch()
    : voices(getSampleRate(), getBlockSize()),
      lfo1(getSampleRate()/getBlockSize()),
      lfo2(getSampleRate()/getBlockSize())  {
    // osc
    registerParameter(PARAMETER_A, "Pitch");
    registerParameter(PARAMETER_B, "Waveshape");    
    setParameterValue(PARAMETER_A, 0.5);
    setParameterValue(PARAMETER_B, 0.0);
    // filter
    registerParameter(PARAMETER_C, "Cutoff");
    registerParameter(PARAMETER_D, "Resonance");
    setParameterValue(PARAMETER_C, 0.8);
    setParameterValue(PARAMETER_D, 0.0);
    // envelope
    registerParameter(PARAMETER_AA, "Attack");
    registerParameter(PARAMETER_AB, "Decay");
    registerParameter(PARAMETER_AC, "Sustain");
    registerParameter(PARAMETER_AD, "Release");
    setParameterValue(PARAMETER_AA, 0.05);
    setParameterValue(PARAMETER_AB, 0.1);
    setParameterValue(PARAMETER_AC, 0.8);
    setParameterValue(PARAMETER_AD, 0.2);
    // lfo
    registerParameter(PARAMETER_E, "LFO1");
    registerParameter(PARAMETER_F, "LFO1>");
    registerParameter(PARAMETER_G, "LFO2");
    registerParameter(PARAMETER_H, "LFO2>");
    setParameterValue(PARAMETER_E, 0.5);
    setParameterValue(PARAMETER_G, 0.5);

    hp = BiquadFilter::create(2);
    hp->setHighPass(40/(getSampleRate()/2), FilterStage::BUTTERWORTH_Q);
  }
  ~BigSubPatch(){
    BiquadFilter::destroy(hp);
  }
  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples){
    if(bid == PUSHBUTTON){
      if(value)
	voices.allNotesOn();
      else
	voices.allNotesOff();
    }else if(bid == BUTTON_B){
      if(value)
      	voices.noteOn(48, 80, samples);
      else
      	voices.noteOff(48, samples);
    }else if(bid == BUTTON_C){
      if(value)
      	voices.noteOn(53, 80, samples);
      else
      	voices.noteOff(53, samples);
    }else if(bid == BUTTON_D){
      if(value)
      	voices.noteOn(60, 80, samples);
      else
      	voices.noteOff(60, samples);
    }    
    // debugMessage("button/value/samples", bid, value, samples);
  }
  SmoothFloat pitchbend;
  SmoothFloat cutoff;
  float pb = 0.0f;
  float mod = 0.0f;
  void processMidi(MidiMessage msg){
    uint16_t samples = 0;
    if(msg.isNoteOn()){
      voices.noteOn(msg.getNote(), (uint16_t)msg.getVelocity(), samples);
      debugMessage("note on", msg.getPort(), msg.getNote(), msg.getVelocity());
    }else if(msg.isNoteOff()){
      debugMessage("note off", msg.getPort(), msg.getNote(), msg.getVelocity());
      voices.noteOff(msg.getNote(), samples);
    }else if(msg.isPitchBend()){      
      // debugMessage("pb", msg.getPort(), msg.getChannel(), msg.getPitchBend());
      pb = msg.getPitchBend()/8192.0f;
    }else if(msg.isControlChange()){
      // debugMessage("cc", msg.getPort(), msg.getControllerNumber(), msg.getControllerValue());
      if(msg.getControllerNumber() == MIDI_CC_MODULATION){
	// or use CC74 (for MPE Y-axis movement)
	mod = msg.getControllerValue()/127.0f;
      }
    }
  }
  void setBasenote(float note){
    int i = (int)(note+0.5);
    if(abs(note-i) < 0.8)
      basenote = note;
  }
    
  void processAudio(AudioBuffer &buffer) {
    setBasenote(getParameterValue(PARAMETER_A)*24 - 81);
    float cf = getParameterValue(PARAMETER_C)*0.48 + 0.01;
    float q = getParameterValue(PARAMETER_D)*3+0.75;
    cf += mod*0.25;
    cf = max(0.001, min(0.499, cf));
    cutoff = cf;
    float shape = getParameterValue(PARAMETER_B)*2;
    pitchbend = pb;
    float attack = getParameterValue(PARAMETER_AA);
    float decay = getParameterValue(PARAMETER_AB);
    float sustain = getParameterValue(PARAMETER_AC);
    float release = getParameterValue(PARAMETER_AD);
    FloatArray left = buffer.getSamples(LEFT_CHANNEL);
    FloatArray right = buffer.getSamples(RIGHT_CHANNEL);
    voices.setParameters(shape, cutoff, q, attack, decay, sustain, release, pitchbend);
    voices.getSamples(left);
    left.multiply(0.8);
    left.tanh();
    hp->process(left);
    right.copyFrom(left);

    // lfo
    float rate = getParameterValue(PARAMETER_E)*2;
    lfo1.setFrequency(rate);
    setParameterValue(PARAMETER_F, lfo1.getNextSample()*0.5+0.5);
    rate = getParameterValue(PARAMETER_G)*2;
    lfo2.setFrequency(rate);
    setParameterValue(PARAMETER_H, lfo2.getNextSample()*0.5+0.5);
}
};

#endif   // __BigSubPatch_hpp__
