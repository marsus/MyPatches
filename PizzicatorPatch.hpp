#include "StompBox.h"
#include "VoltsPerOctave.h"
#include "SmoothValue.h"

class PizzicatorPatch : public Patch {
private:
  VoltsPerOctave hzin;
  VoltsPerOctave hzout;
  const float cent = 1/(12*1000); // One cent at 1V/Octave is apprx 0.83 mV
  const float semitone = 1/12.0f;

  /*
    A: pitch
    B: pitch pattern: scale + range + sequence + number of unique notes
    C: number of and distribution of beats
    D: tempo
    Left in: pitch trigger
    Right in: amplitude trigger
    Left out: pitch trigger
    Right out: amplitude trigger
  */
public:
  const float MAXTEMPO_BPM = 360*4; // 360
  const float maxincr;
  PizzicatorPatch() :
    hzin(true), hzout(false),
    maxincr((1<<24)/(getSampleRate()/(MAXTEMPO_BPM/60))) {
    registerParameter(PARAMETER_A, "Pitch");
    registerParameter(PARAMETER_B, "Notes");
    registerParameter(PARAMETER_C, "Beats");
    registerParameter(PARAMETER_D, "Tempo");    
  }

  int patterns[9][16] = { { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
			  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 7, 6, 5, 4, 3, 2, 1 },
			  { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 },
			  {  0,  2,  4,  5,  7,  9, 11, /* 1st octave */ 12, 14, 16, 17, 19, 23, 24, 26, 28 },
			  {  0,  2,  4,  5,  7,  9, 11, /* 1st octave */ 12, 14, 12, 11,  9,  7,  5,  4,  2 },
			  { 28, 26, 24, 23, 19, 17, 16, /* 1st octave */ 14, 12, 11,  9,  7,  5,  4,  2,  0 },
			  {  0,  1,  3,  5,  6,  8, 10, /* 1st octave */ 12, 13, 15, 17, 18, 22, 24, 25, 27 },
			  {  0,  2,  3,  5,  7,  8, 10, /* 1st octave */ 12, 14, 15, 17, 19, 20, 22, 24, 26 },
			  {  0,  2,  4,  5,  7,  9, 10, /* 1st octave */ 12, 14, 16, 17, 19, 21, 22, 24, 26 } };
  bool playing = false;
  float lastpitch = 0;
  float multiplier;  
  uint32_t pos = 0;
  //  const uint32_t maxpos = 7l<<24;
  uint32_t incr = 0;

  void processAudio(AudioBuffer& buffer){
    float pitch = getParameterValue(PARAMETER_A)*2-1;
    int pattern = getParameterValue(PARAMETER_B)*5;
    int length = getParameterValue(PARAMETER_C)*15+1;
    incr = (int)(getParameterValue(PARAMETER_D)*maxincr);
    FloatArray left = buffer.getSamples(LEFT_CHANNEL);
    FloatArray right = buffer.getSamples(RIGHT_CHANNEL);
    int size = buffer.getSize();
    for(int i=0; i<size; ++i){
      float v = hzin.sampleToVolts(left[i]);
      float t = hzin.sampleToVolts(right[i]);
      if(playing && t < 1.0){
	playing = false;
      }else if(!playing && t > 1.0){
	playing = true;
	pos = 0;
      }
      int index = (pos>>24) % length;
      if(playing){
	int tone = patterns[pattern][index];
	left[i] = lastpitch = hzout.voltsToSample(v + tone*semitone + pitch);
	if(!(pos & (1l<<23)))
	  right[i] = hzout.voltsToSample(5.0);
	else
	  right[i] = hzout.voltsToSample(0.0);
	pos += incr;
	// if(pos > maxpos){
	//   pos = 0;
	//   playing = false;
	// }
      }else{
	left[i] = lastpitch;
	right[i] = hzout.voltsToSample(-0.1);
      }
    }
  }
};
