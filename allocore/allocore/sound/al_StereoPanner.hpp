#ifndef INCLUDE_AL_PANNING_STEREO
#define INCLUDE_AL_PANNING_STEREO

#include "allocore/sound/al_AudioScene.hpp"
//#include "allocore/math/al_Quat.hpp"
////#include "allocore/spatial/al_CoordinateFrame.hpp"

namespace al{

class StereoPanner : public Spatializer{
public:
	
    StereoPanner(SpeakerLayout &sl) : Spatializer(sl), numSpeakers(0){}
    
	void dump() {
		printf("Using Stereo Panning- need to add panner info for dump function\n");
	}
		
	void compile(Listener& listener){
		mListener = &listener;
		numSpeakers = mSpeakers.size();
        if(numSpeakers == 2)
        {
            printf("Stereo Panner Compiled with %d speakers\n", numSpeakers);
        }
        else
            printf("Stereo Panner Requires exactly 2 speakers, no panning will occur!\n", numSpeakers);
	}
	
#if !ALLOCORE_GENERIC_AUDIOSCENE

    ///Per Sample Processing
    void perform(AudioIOData& io, SoundSource& src, Vec3d& relpos, const int& numFrames, int& frameIndex, float& sample)
    {
        if(numSpeakers == 2 && mEnabled)
        {
            float gainL, gainR;
            equalPowerPan(relpos.x, gainL, gainR);
            
            io.out(0, frameIndex) = gainL*sample;
            io.out(1, frameIndex) = gainR*sample;
        }
        else // dont pan
        {
            for(int i = 0; i < numSpeakers; i++)
                io.out(i, frameIndex) = sample;
        }
        
    }
    
    /// Per Buffer Processing
    void perform(AudioIOData& io, SoundSource& src, Vec3d& relpos, const int& numFrames, float *samples)
    {
        if(numSpeakers == 2 && mEnabled)
        {
            float *bufL = io.outBuffer(0);
            float *bufR = io.outBuffer(1);
            
            float gainL, gainR;
            equalPowerPan(relpos.x, gainL, gainR);
            
            for(int i = 0; i < numFrames; i++)
            {
                bufL[i] = gainL*samples[i];
                bufR[i] = gainR*samples[i];
            }
        }
        else // dont pan
        {
            for(int i = 0; i < numSpeakers; i++)
                memcpy(io.outBuffer(i), samples, sizeof(float)*numFrames);
        }
    }
    
#else
    
    ///Per Sample Processing
    void perform(float** outputBuffers, SoundSource& src, Vec3d& relpos, const int& numFrames, int& frameIndex, float& sample)
    {
        if(numSpeakers == 2 && mEnabled)
        {
            float *bufL = outputBuffers[0];
            float *bufR = outputBuffers[1];
            
            float gainL, gainR;
            equalPowerPan(relpos.x, gainL, gainR);
            
            bufL[frameIndex] = gainL*sample;
            bufR[frameIndex] = gainR*sample;
        }
        else // dont pan
        {
            for(int i = 0; i < numSpeakers; i++)
            {
                float *buf = outputBuffers[i];
                buf[frameIndex] = sample;
            }
        }
        
	}
    
    /// Per Buffer Processing
	void perform(float** outputBuffers, SoundSource& src, Vec3d& relpos, const int& numFrames, float *samples)
    {
        if(numSpeakers == 2 && mEnabled)
        {
            float *bufL = outputBuffers[0];
            float *bufR = outputBuffers[1];
            
            float gainL, gainR;
            equalPowerPan(relpos.x, gainL, gainR);
            
            for(int i = 0; i < numFrames; i++)
            {
                bufL[i] = gainL*samples[i];
                bufR[i] = gainR*samples[i];
            }
        }
        else // dont pan
        {
            for(int i = 0; i < numSpeakers; i++)
                memcpy(outputBuffers[i], samples, sizeof(float)*numFrames);
        }
	}
    
#endif
    
	
private:
	Listener* mListener;
	int numSpeakers;
    
    void equalPowerPan(float xPos, float &gainL, float &gainR)
    {
        if(xPos > 1) xPos = 1;
        else if(xPos < -1) xPos = -1;
        
        float panVal = (xPos + 1) /2.f; //[0, 1], L to R
        
        gainL = M_PI*cos((M_PI/2)*panVal);
        gainR = M_PI*sin((M_PI/2)*panVal);
    }
    
};
	
	
} // al::

#endif
