#pragma once
#include <fmod.hpp>
#include <string>

#pragma comment(lib, "fmod_vc.lib")

FMOD_RESULT F_CALLBACK MyChannelCallback(
	FMOD_CHANNELCONTROL* cc,
	FMOD_CHANNELCONTROL_TYPE cctype,
	FMOD_CHANNELCONTROL_CALLBACK_TYPE callbacktype,
	void* commanddata1,
	void* commanddata2)
{
	if (callbacktype == FMOD_CHANNELCONTROL_CALLBACK_END)
	{
		FMOD::Channel* channel = (FMOD::Channel*)cc;
		FMOD::Sound* sound = nullptr;
		channel->getCurrentSound(&sound);
		if (sound)
			sound->release();
	}
	return FMOD_OK;
}

class audioManager {
public:
	audioManager() {
		FMOD::System_Create(&system);
		system->setSoftwareFormat(0, FMOD_SPEAKERMODE_STEREO, 0);
		system->init(512, FMOD_INIT_NORMAL, nullptr);
	}

	~audioManager() {
		if (system)
		{
			system->close();
			system->release();
			system = nullptr;
		}
	}

	void play(const std::string& filepath) {
		if (!system) return;
		FMOD::Sound* sound = NULL;
		FMOD::Channel* channel = NULL;
		system->createSound(filepath.c_str(), FMOD_DEFAULT, NULL, &sound);
		system->playSound(sound, NULL, false, &channel);
		channel->setCallback(MyChannelCallback);
	}

private:
	FMOD::System* system = nullptr;
};