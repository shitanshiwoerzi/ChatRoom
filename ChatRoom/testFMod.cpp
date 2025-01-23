#define _USE_MATH_DEFINES
#include <fmod.hpp>
#include <fmod_errors.h>
#include <cmath>
#include <thread>
#include <chrono>
#include <iostream>
#include <conio.h>
#include <vector>
#pragma comment(lib, "fmod_vc.lib")


FMOD_RESULT F_CALLBACK sineCallback(FMOD_DSP_STATE* dsp_state, float* inbuffer, float* outbuffer, unsigned int length, int
	inchannels, int* outchannels)
{
	static float phase = 0.0f;
	const float frequency = 440.0f;
	const float sampleRate = 48000.0f;
	for (unsigned int i = 0; i < length; i++) {
		float sample = sinf(phase);
		phase += 2.0f * M_PI * frequency / sampleRate;
		if (phase >= 2.0f * M_PI) {
			phase -= 2.0f * M_PI;
		}
		for (int j = 0; j < *outchannels; j++) {
			outbuffer[i * (*outchannels) + j] = sample;
		}
	}
	return FMOD_OK;
}

void sineWave(FMOD::System* system) {
	FMOD_DSP_DESCRIPTION dspDesc = {};
	dspDesc.version = 0x00010000;
	dspDesc.numinputbuffers = 0;
	dspDesc.numoutputbuffers = 1;
	dspDesc.read = sineCallback;
	FMOD::DSP* dsp;
	system->createDSP(&dspDesc, &dsp);

	system->playDSP(dsp, NULL, false, NULL);

	std::cout << "Press any key to quit" << std::endl;
	while (true)
	{
		std::cin.get();
		break;
	}

	dsp->release();
}

void playMusic(FMOD::System* system) {
	FMOD::Sound* sound = NULL;
	FMOD::Channel* channel = NULL;
	system->createSound("audio/music.mp3", FMOD_DEFAULT, NULL, &sound);
	system->playSound(sound, NULL, false, &channel);
	std::cout << "Press any key to quit" << std::endl;
	while (true)
	{
		std::cin.get();
		break;
	}
	sound->release();
}

void playMultipleMusic(FMOD::System* system) {
	std::cout << "Playing Multiple Audio Channels" << std::endl;
	FMOD::Sound* sound1 = NULL;
	FMOD::Channel* channel1 = NULL;
	system->createSound("audio/music.mp3", FMOD_DEFAULT, NULL, &sound1);
	sound1->setMode(FMOD_LOOP_NORMAL);
	system->playSound(sound1, NULL, false, &channel1);
	FMOD::Sound* sound2 = NULL;
	FMOD::Channel* channel2 = NULL;
	system->createSound("audio/machinegun.mp3", FMOD_DEFAULT, NULL, &sound2);
	sound2->setMode(FMOD_LOOP_OFF);
	std::cout << "Press space to play sound effect, press escape to quit" << std::endl;
	while (true) {
		system->update();
		if (_kbhit()) {
			int key = _getch();
			if (key == 32) {
				bool isPlaying = false;
				channel2->isPlaying(&isPlaying);
				if (isPlaying == false) {
					system->playSound(sound2, NULL, false, &channel2);
				}
			}
			if (key == 27) {
				std::cout << std::endl;
				break;
			}
		}
	}
	sound1->release();
}

void playMusicSpatial(FMOD::System* system) {
	std::cout << "Playing Spatial Audio" << std::endl;
	FMOD::Sound* sound = NULL;
	FMOD::Channel* channel = NULL;
	system->createSound("audio/music.mp3", FMOD_3D, NULL, &sound);
	system->set3DSettings(1.0f, 1.0f, 1.0f);
	FMOD_VECTOR playerPos = { 0.0f, 0.0f, 0.0f };
	FMOD_VECTOR playerVel = { 0.0f, 0.0f, 0.0f };
	FMOD_VECTOR playerForward = { 0.0f, 0.0f, 1.0f };
	FMOD_VECTOR playerUp = { 0.0f, 1.0f, 0.0f };
	system->set3DListenerAttributes(0, &playerPos, &playerVel, &playerForward, &playerUp);
	FMOD_VECTOR soundPos = { 5.0f, 0.0f, 0.0f };
	FMOD_VECTOR soundVel = { 0.0f, 0.0f, 0.0f };
	system->playSound(sound, NULL, true, &channel);
	channel->set3DAttributes(&soundPos, &soundVel);
	channel->setPaused(false);
	float angle = 0.0f;
	const float radius = 5.0f;
	const float speed = 0.1f;
	std::cout << "Press any key to quit" << std::endl;
	while (true) {
		soundPos.x = radius * cos(angle);
		soundPos.z = radius * sin(angle);
		channel->set3DAttributes(&soundPos, &soundVel);
		angle += speed;
		if (angle > 2.0f * M_PI) {
			angle -= 2.0f * M_PI;
		}
		system->update();
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		if (_kbhit()) {
			_getch();
			break;
		}
	}
	sound->release();
}

int s() {
	FMOD::System* system;
	FMOD::System_Create(&system);
	system->setSoftwareFormat(0, FMOD_SPEAKERMODE_STEREO, 0);
	system->init(512, FMOD_INIT_NORMAL, NULL);


	//sineWave(system);
	//playMusic(system);
	//playMultipleMusic(system);
	playMusicSpatial(system);

	system->close();
	system->release();
	return 0;
}