#ifndef DEDICATED_ONLY

#include "sound_sample_openal.h"
#include "gusanos/resource_list.h"
#include "game/CGameObject.h"
#include "gusanos/allegro.h"

#ifdef __APPLE__
#ifndef __MACOSX__
#define __MACOSX__
#endif
#endif

#include <vorbis/vorbisfile.h>
#ifdef __APPLE__
#include <OpenAL/al.h>
#else
#include <AL/al.h>
#endif
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#include <iostream>
#include "StringUtils.h"


using namespace std;


namespace
{
	const int BUFFER_SIZE = 32768;       // 32 KB buffers
}


// This function loads a .ogg file into a memory buffer and returns
// the format and frequency.
static bool LoadOGG(const char *fileName, vector<char> &buffer, ALenum &format, ALsizei &freq) {
    int endian = 0;                         // 0 for Little-Endian, 1 for Big-Endian
    int bitStream;
    long bytes;
    char array[BUFFER_SIZE];                // Local fixed size array
    FILE *f;
	
    // Open for binary reading
    f = OpenGameFile(fileName, "rb");
	
    if (f == NULL)
	{
        //cerr << "Cannot open " << fileName << " for reading..." << endl;
        return false;
	}
    // end if
	
    vorbis_info *pInfo = NULL;
    OggVorbis_File oggFile;
	
    // Try opening the given file
	// Note: Use ov_open_callbacks instead of ov_open to avoid problems
	// on Windows when linked to different libc versions.
    if (ov_open_callbacks(f, &oggFile, NULL, 0, OV_CALLBACKS_DEFAULT) != 0)
	{
        errors << "Error opening " << fileName << " for decoding..." << endl;
        return false;
	}
    // end if
	
    // Get some information about the OGG file
    pInfo = ov_info(&oggFile, -1);
	
    // Check the number of channels... always use 16-bit samples
    if (pInfo->channels == 1)
        format = AL_FORMAT_MONO16;
    else
        format = AL_FORMAT_STEREO16;
    // end if
	
    // The frequency of the sampling rate
    freq = (ALsizei) pInfo->rate;
	
    // Keep reading until all is read
    do
	{
        // Read up to a buffer's worth of decoded sound data
        bytes = ov_read(&oggFile, array, BUFFER_SIZE, endian, 2, 1, &bitStream);
		
        if (bytes < 0)
		{
            ov_clear(&oggFile);
            errors << "Error decoding " << fileName << "..." << endl;
			return false;
		}
        // end if
		
        // Append to end of buffer
        buffer.insert(buffer.end(), array, array + bytes);
	}
    while (bytes > 0);
	
    // Clean up!
    ov_clear(&oggFile);

	if (buffer.empty()) {
		errors << "Error decoding " << fileName << "..." << endl;
		return false;
	}
	
	return true;
}
// end of LoadOGG


// Loads a PCM .wav (RIFF) file into a memory buffer and returns the OpenAL
// format and frequency. This replaces freealut's alutCreateBufferFromFile for
// the non-ogg samples shipped with the game, which are all PCM WAV. Multi-byte
// header fields are assembled from little-endian bytes (the WAV byte order), so
// the parser itself is host-endian independent; the 16-bit sample payload is
// passed through unchanged, which is correct on the little-endian targets OLX
// runs on.
static bool LoadWAV(const char *fileName, vector<char> &buffer, ALenum &format, ALsizei &freq) {
	FILE *f = OpenGameFile(fileName, "rb");
	if(f == NULL)
		return false;

	struct FileCloser {
		FILE* f;
		FileCloser(FILE* file) : f(file) {}
		~FileCloser() { if(f) fclose(f); }
	} closer(f);

	unsigned char riff[12];
	if(fread(riff, 1, 12, f) != 12)
		return false;
	if(memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0)
		return false;

	int channels = 0, bits = 0;
	long sampleRate = 0;
	bool haveFmt = false, haveData = false;

	while(true) {
		unsigned char hdr[8];
		if(fread(hdr, 1, 8, f) != 8)
			break; // no more chunks
		unsigned long chunkSize =
			(unsigned long)hdr[4] | ((unsigned long)hdr[5] << 8) |
			((unsigned long)hdr[6] << 16) | ((unsigned long)hdr[7] << 24);

		if(memcmp(hdr, "fmt ", 4) == 0) {
			unsigned char fmt[16];
			if(chunkSize < 16 || fread(fmt, 1, 16, f) != 16)
				return false;
			int audioFormat = fmt[0] | (fmt[1] << 8);
			channels        = fmt[2] | (fmt[3] << 8);
			sampleRate      = (long)((unsigned long)fmt[4] | ((unsigned long)fmt[5] << 8) |
			                         ((unsigned long)fmt[6] << 16) | ((unsigned long)fmt[7] << 24));
			bits            = fmt[14] | (fmt[15] << 8);
			if(audioFormat != 1) // only uncompressed PCM is supported
				return false;
			if(chunkSize > 16) // skip any extra format bytes
				fseek(f, (long)(chunkSize - 16), SEEK_CUR);
			haveFmt = true;
		}
		else if(memcmp(hdr, "data", 4) == 0) {
			buffer.resize(chunkSize);
			if(chunkSize > 0 && fread(&buffer[0], 1, chunkSize, f) != chunkSize)
				return false;
			haveData = true;
			break; // ignore anything after the data chunk
		}
		else {
			// skip unknown chunk, respecting word-alignment padding
			fseek(f, (long)(chunkSize + (chunkSize & 1)), SEEK_CUR);
		}
	}

	if(!haveFmt || !haveData || channels < 1)
		return false;

	if(channels == 1)
		format = (bits == 8) ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;
	else
		format = (bits == 8) ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
	freq = (ALsizei)sampleRate;
	return true;
}


ALuint  LoadSoundFromFile( const char* inSoundFile )
{
    ALuint bufferID;                        // The OpenAL sound buffer ID
    ALenum format;                          // The sound data format
    ALsizei freq;                           // The frequency of the sound data
    vector<char> bufferData;                // The sound buffer data from file

	// Load the OGG file into memory
	if(!LoadOGG(inSoundFile, bufferData, format, freq))
		return 0;

	// Create sound buffer and source
    alGenBuffers(1, &bufferID);

    // Upload sound data to buffer
    alBufferData(bufferID, format, &bufferData[0], static_cast<ALsizei>(bufferData.size()), freq);
    return bufferID;
  
}


struct OpenALBuffer {
	std::string name;
	ALuint bufferID;
	size_t size;

	OpenALBuffer(ALuint bufid, const std::string& n) : name(n), bufferID(bufid), size(0) {
		ALint s = 0;
		alGetBufferi(bufferID, AL_SIZE, &s);
		if(s >= 0) size = s;
	}

	~OpenALBuffer() {
		if(bufferID != AL_NONE)
			alDeleteBuffers(1,&bufferID);
	}
};

std::string SoundSampleOpenAL::name() { return buffer.get() ? buffer->name : "<notloaded>"; }
size_t SoundSampleOpenAL::GetMemorySize() { return buffer.get() ? buffer->size : 0; }

template <> void SmartPointer_ObjectDeinit<OpenALBuffer> ( OpenALBuffer * obj )
{
	delete obj;
}



SoundSampleOpenAL::SoundSampleOpenAL(std::string const& filename)
{	
	m_sound = 0;

	if(!IsFileAvailable(filename))
		// we silently ignore this
		return;

    ALuint bufferID = 0;           // The OpenAL sound buffer ID
	
	if (filename.size() >= 4 && stringcaseequal(filename.substr(filename.size() - 4), ".ogg"))
	{
		bufferID=LoadSoundFromFile( filename.c_str());
		if (bufferID==0)
			return;
	}
	else
	{
		vector<char> wavData;
		ALenum wavFormat = AL_FORMAT_MONO16;
		ALsizei wavFreq = 0;
		if (!LoadWAV(filename.c_str(), wavData, wavFormat, wavFreq))
		{
			notes << "SoundSampleOpenAL: cannot load " << filename << ": unsupported or invalid audio file" << endl;
			return;
		}
		alGenBuffers(1, &bufferID);
		alBufferData(bufferID, wavFormat, wavData.empty() ? NULL : &wavData[0],
					 static_cast<ALsizei>(wavData.size()), wavFreq);
		if (bufferID == AL_NONE)
			return;
	}
	
	buffer = new OpenALBuffer(bufferID, filename);
	initSound();
}

SoundSampleOpenAL::~SoundSampleOpenAL()
{
	if ( m_sound)  
		alDeleteSources(1,&m_sound);
}

SoundSampleOpenAL::SoundSampleOpenAL(const SoundSampleOpenAL& s) {
	m_sound = 0;

	buffer = s.buffer;
	if(buffer.get())
		initSound();
}

void SoundSampleOpenAL::initSound() {
    ALuint sourceID = 0;                        // The OpenAL sound source
    alGenSources(1, &sourceID);
	
    // Attach sound buffer to source
    alSourcei(sourceID, AL_BUFFER, buffer->bufferID);
	alSourcef(sourceID,AL_ROLLOFF_FACTOR,2);
	alSourcef(sourceID,AL_GAIN,0.7f);
	//alSourcef(	 mALSource,AL_REFERENCE_DISTANCE,20);
    /*alSource3f(mALSource, AL_POSITION,        0.0, 0.0, 0.0);
	 alSource3f(mALSource, AL_VELOCITY,        0.0, 0.0, 0.0);
	 alSource3f(mALSource, AL_DIRECTION,       0.0, 0.0, 0.0);
	 alSourcef (mALSource, AL_ROLLOFF_FACTOR,  0.0          );
	 alSourcei (mALSource, AL_SOURCE_RELATIVE, AL_TRUE      );*/
	m_sound=sourceID;	
}

size_t SoundSampleOpenAL::currentSimulatiousPlays() {
	// WARNING: This pretty much depends on the behaviour of Sfx::playSimple*
	// and the way we are doing this right now!
	return MAX(buffer.getRefCount() - 1, 0);
}



bool SoundSampleOpenAL::avail()
{
	return (m_sound!=0);
}


void SoundSampleOpenAL::play( float pitch,float volume)
{
	if( m_sound ) 
	{
		//cout<<"Play "<<endl;

		float _pos[3];
		alGetListenerfv(AL_POSITION, _pos);
		_pos[2] = 0;
		
		alSourcefv( m_sound , AL_POSITION, _pos);		
		alSourcef(	 m_sound ,AL_REFERENCE_DISTANCE,100.0/100*50); //ok?

		
		alSourcef(	 m_sound ,AL_PITCH,pitch);
		alSourcef(	 m_sound ,AL_GAIN,volume );
		alSourcePlay( m_sound) ;			
	}
}


void SoundSampleOpenAL::play2D(const Vec& pos, float loudness, float pitch)
{
	if( m_sound ) 
	{
		//cout<<"Play 2D"<<endl;
			float _pos[3] = { pos.x, pos.y, 0 };
			//cout<<"sound: x,y: "<<pos.x<<" "<<pos.y<<endl;
			alSourcefv( m_sound , AL_POSITION, _pos);
			alSourcef(	 m_sound ,AL_PITCH,pitch);
			alSourcef(	 m_sound ,AL_REFERENCE_DISTANCE,loudness/100*50); //ok?
			alSourcePlay( m_sound) ;			
	}
}

void SoundSampleOpenAL::play2D(CGameObject* obj, float loudness, float pitch)
{
	//cout<<"Play 2d(obj)"<<endl;
	play2D(obj->pos(), loudness, pitch);
}

bool SoundSampleOpenAL::isPlaying()
{
	if(!m_sound) return false;
	
	ALint state;
	alGetSourcei(m_sound,AL_SOURCE_STATE,&state);
	if (state==AL_PLAYING) 
	{
		return true;
	}
	return false; //I think
}

void SoundSampleOpenAL::updateObjSound(Vec& vec)
{
	ALfloat pos[3] = { vec.x, vec.y, 0 };
	alSourcefv( m_sound , AL_POSITION, pos);	
}



#endif
