#ifndef DEDICATED_ONLY

#include <vector>
#include <list>
#ifdef __APPLE__
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif

#include "Debug.h"

#include "gusanos/gconsole.h"
#include "game/CGameObject.h"
#include "util/macros.h"
#include "sfxdriver_openal.h"
#include "sound_sample_openal.h"
#include "sound_sample.h"


using namespace std;

namespace
{
	std::list< Sound* > chanObject;

	// OpenAL device/context, previously managed by alutInit/alutExit.
	ALCdevice* g_alDevice = NULL;
	ALCcontext* g_alContext = NULL;
}


bool SfxDriverOpenAL::init()
{
	g_alDevice = alcOpenDevice(NULL); // default device
	if (g_alDevice == NULL)
	{
		errors << "SfxDriverOpenAL: could not open the default OpenAL device" << endl;
		return false;
	}
	g_alContext = alcCreateContext(g_alDevice, NULL);
	if (g_alContext == NULL || alcMakeContextCurrent(g_alContext) == ALC_FALSE)
	{
		errors << "SfxDriverOpenAL: could not create or activate the OpenAL context" << endl;
		if (g_alContext) { alcDestroyContext(g_alContext); g_alContext = NULL; }
		alcCloseDevice(g_alDevice); g_alDevice = NULL;
		return false;
	}
	volumeChange();
	// orientation doesn't change during the game
	ALfloat listenerOri[]={0.0,0.0,-1.0, 0.0,1.0,0.0};
	alListenerfv(AL_ORIENTATION,listenerOri);

	hints << "OpenAL lib initialized" << endl;
	return true;
}

void SfxDriverOpenAL::shutDown()
{
	alcMakeContextCurrent(NULL);
	if (g_alContext) { alcDestroyContext(g_alContext); g_alContext = NULL; }
	if (g_alDevice) { alcCloseDevice(g_alDevice); g_alDevice = NULL; }
}

void SfxDriverOpenAL::think()
{
	
	for (size_t i = 0; i < listeners.size(); ++i )
	{
		ALfloat listenerPos[]={listeners[i]->pos.x,listeners[i]->pos.y,(ALfloat)-SFX_LISTENER_DISTANCE };
		//cout<<"listener x,y,z "<<listenerPos[0]<<" "<<listenerPos[1]<<" "<<listenerPos[2]<<endl;
		alListenerfv(AL_POSITION,listenerPos);
		//multi listeners are not supported in OpenAL
		break;
	}
	
	foreach_delete(obj, chanObject)
	{

		if( !(*obj)->isValid())
		{
			chanObject.erase(obj);
		}
		else
		{
			(*obj)->updateObjSound();
		}
	}

}
	
void SfxDriverOpenAL::clear()
{
	chanObject.clear();
}


void SfxDriverOpenAL::volumeChange()
{
	float v = volume();
	//multi listeners are not supported in OpenAL
	alListenerf(AL_GAIN,(float)v);
}

SmartPointer<SoundSample> SfxDriverOpenAL::load(std::string const& filename)
{
	return new SoundSampleOpenAL(filename);
		
}
#endif
