
#include <iostream>

#include "alloaudio/al_OutputMaster.hpp"
#include "allocore/system/al_Time.hpp"

#include "src/butter.c"

//#include "firfilter.h"

using namespace al;

OutputMaster::OutputMaster(int num_chnls, double sampleRate, const char *address, int port,
						   const char *sendAddress, int sendPort, al_sec msg_timeout):
	m_meterBuffer(1024 * sizeof(float)), m_framesPerSec(sampleRate),
	osc::Recv(port, address, msg_timeout),
	m_sendAddress(sendAddress), m_sendPort(sendPort)
{
	pthread_mutex_init(&m_paramMutex, NULL);
	pthread_mutex_init(&m_meterMutex, NULL);
	pthread_cond_init(&m_meterCond, NULL);
	allocateChannels(num_chnls);
	initializeData();

	if (port < 0) {
		std::cout << "OutputMaster: Not using OSC." << std::endl;
	} else {
		msghandler.outputmaster = this;
		if (!opened()) {
			std::cout << "Failed to open socket on UDP port " << port << " for OSC receipt." << std::endl;
			std::cout << "Probably another program is already listening on that port." << std::endl;
		} else {
			handler(OutputMaster::msghandler);
			timeout(0.1); // set receiver to block with timeout
			start();
		}
	}
	if (m_sendPort > 0 && strlen(sendAddress) > 1) {
		m_meterThread.start(OutputMaster::meterThreadFunc, (void *) this);
	}
}

// FIXME fix leaks  and issues when changing channel numbers (due to C pointers)

OutputMaster::~OutputMaster()
{
	for (int i = 0; i < m_numChnls; i++) {
		butter_free(m_lopass1[i]);
		butter_free(m_lopass2[i]);
		butter_free(m_hipass1[i]);
		butter_free(m_hipass2[i]);
	}
	stop();
	//    free(filters); //FIR filters
}

void OutputMaster::setFilters(double **irs, int filter_len)
{
	if (!irs) { /* if NULL, leave filtering off */
		m_filtersActive = false;
		return;
	}
	for (int i = 0; i < m_numChnls; i++) {
		//        FIRFILTER *new_filter = firfilter_create(irs[i], filter_len);
		//        FIRFILTER *old_filter = pp->filters[i];
		//        pp->filters[i] = new_filter;
		//        if (old_filter) {
		//            firfilter_free(old_filter);
		//        }
	}

	//    filtersActive = true;
}

// FIXME adjust parameter passing model. Should only block when passing the filter irs
void OutputMaster::setMasterGain(double gain)
{
	pthread_mutex_lock(&m_paramMutex);
	m_masterGain = gain;
	pthread_mutex_unlock(&m_paramMutex);
}

void OutputMaster::setGain(int channelIndex, double gain)
{
	pthread_mutex_lock(&m_paramMutex);
	if (channelIndex >= 0 && channelIndex < m_numChnls) {
		m_gains[channelIndex] = gain;
	} else {
		//        printf("Alloaudio error: set_gain() for invalid channel %i", channelIndex);
	}
	pthread_mutex_unlock(&m_paramMutex);

}

void OutputMaster::setMuteAll(bool muteAll)
{
	pthread_mutex_lock(&m_paramMutex);
	m_muteAll = muteAll;
	pthread_mutex_unlock(&m_paramMutex);

}

void OutputMaster::setClipperOn(bool clipperOn)
{
	pthread_mutex_lock(&m_paramMutex);
	m_clipperOn = clipperOn;
	pthread_mutex_unlock(&m_paramMutex);
}

void OutputMaster::setRoomCompensationOn(bool on)
{
	pthread_mutex_lock(&m_paramMutex);
	m_filtersActive = on;
	pthread_mutex_unlock(&m_paramMutex);

}

void OutputMaster::setMeterUpdateFreq(double freq)
{
	m_meterUpdateSamples = (int)(m_framesPerSec/freq);
}

void OutputMaster::setBassManagementFreq(double frequency)
{
	int i;
	if (frequency > 0) {
		pthread_mutex_lock(&m_paramMutex);
		for (i = 0; i < m_numChnls; i++) {
			butter_set_fc(m_lopass1[i], frequency);
			butter_set_fc(m_lopass2[i], frequency);
			butter_set_fc(m_hipass1[i], frequency);
			butter_set_fc(m_hipass2[i], frequency);
		}
		pthread_mutex_unlock(&m_paramMutex);
	}
}

void OutputMaster::setBassManagementMode(bass_mgmt_mode_t mode)
{
	pthread_mutex_lock(&m_paramMutex);
	m_BassManagementMode = mode;
	pthread_mutex_unlock(&m_paramMutex);
}

void OutputMaster::setSwIndeces(int i1, int i2, int i3, int i4)
{
	pthread_mutex_lock(&m_paramMutex);
	swIndex[0] = i1;
	swIndex[1] = i1;
	swIndex[2] = i1;
	swIndex[3] = i1;
	pthread_mutex_unlock(&m_paramMutex);
}

void OutputMaster::setMeterOn(bool meterOn)
{
	pthread_mutex_lock(&m_paramMutex);
	m_meterOn = meterOn;
	pthread_mutex_unlock(&m_paramMutex);

}

int OutputMaster::getMeterValues(float *values)
{
	return m_meterBuffer.read((char *) values, m_numChnls * sizeof(float));
}

int OutputMaster::getNumChnls()
{
	return m_numChnls;
}

void OutputMaster::processBlock(AudioIOData &io)
{
	int i, chan = 0;
	int nframes = io.framesPerBuffer();
	double bass_buf[nframes];
	double filt_out[nframes];
	double filt_low[nframes];
	double in_buf[nframes];
	double master_gain;

	master_gain = m_masterGain * (m_muteAll ? 0.0 : 1.0);
	memset(bass_buf, 0, nframes * sizeof(double));
	for (chan = 0; chan < m_numChnls; chan++) {
		double gain = master_gain * m_gains[chan];
		const float *in = io.outBuffer(chan);  // Yes, the input here is the output from previous runs for the io object
		float *out = io.outBuffer(chan);
		double filt_temp[nframes];
		double *buf = bass_buf;

		for (i = 0; i < nframes; i++) {
			in_buf[i] = *in++;
		}
		switch (m_BassManagementMode) {
		case BASSMODE_NONE:
			break;
		case BASSMODE_MIX:
			for (i = 0; i < nframes; i++) {
				filt_low[i] = in_buf[i];
			}
			break;
		case BASSMODE_LOWPASS:
			butter_next(m_lopass1[chan], in_buf, filt_temp, nframes);
			butter_next(m_lopass2[chan], filt_temp, filt_low, nframes);
			break;
		case BASSMODE_HIGHPASS:
			for (i = 0; i < nframes; i++) {
				filt_low[i] = in_buf[i];
			}
			butter_next(m_hipass1[chan], in_buf, filt_temp, nframes);
			butter_next(m_hipass2[chan], filt_temp, filt_out, nframes);
			for (i = 0; i < nframes; i++) {
				in_buf[i] = filt_out[i];
			}
			break;
		case BASSMODE_FULL:
			butter_next(m_lopass1[chan], in_buf, filt_temp, nframes);
			butter_next(m_lopass2[chan], filt_temp, filt_low, nframes);
			butter_next(m_hipass1[chan], in_buf, filt_temp, nframes);
			butter_next(m_hipass2[chan], filt_temp, filt_out, nframes);
			for (i = 0; i < nframes; i++) {
				in_buf[i] = filt_out[i]; /* a bit inefficient to copy here, but makes code simpler below */
			}
			break;
		default:
			break;
		}
		for (i = 0; i < nframes; i++) { /* accumulate SW signal */
			*buf++ += filt_low[i];
		}
		if (m_filtersActive
				&& m_BassManagementMode == BASSMODE_NONE && !chanIsSubwoofer(chan)) { /* apply DRC filters */
			//            firfilter_next(pp->filters[chan],in_buf, filt_out, nframes, gain);
			//            for (i = 0; i < nframes; i++) {
			//                *out = filt_out[i];
			//                if (pp->clipper_on && *out > master_gain) {
			//                    *out = master_gain;
			//                }
			//                out++;
			//            }
		} else { /* No DRC filters, just apply gain */
			for (i = 0; i < nframes; i++) {
				*out = in_buf[i] * gain;
				if (m_clipperOn && *out > master_gain) {
					*out = master_gain;
				}
				out++;
			}
		}
	}
	if (m_BassManagementMode != BASSMODE_NONE) {
		int sw;
		for(sw = 0; sw < 4; sw++) {
			if (swIndex[sw] < 0) continue;
			float *out = io.outBuffer(swIndex[sw]);
			memset(out, 0, nframes * sizeof(float));
			for (i = 0; i < nframes; i++) {
				*out++ = bass_buf[i];
			}
		}
	}
	if (m_meterOn) {
		for (chan = 0; chan < m_numChnls; chan++) {
			float *out = io.outBuffer(chan);
			for (i = 0; i < nframes; i++) {
				if (m_meters[chan] < *out) {
					m_meters[chan] = *out;
				}
				out++;
			}
		}
		m_meterCounter += nframes;
		if (m_meterCounter >= m_meterUpdateSamples) {
			m_meterBuffer.write( (char *) m_meters.data(), sizeof(float) * m_numChnls);
			memset(m_meters.data(), 0, sizeof(float) * m_numChnls);
			m_meterCounter = 0; // A little jitter but efficient
			pthread_cond_signal(&m_meterCond);
		}
	}
}

int OutputMaster::chanIsSubwoofer(int index)
{
	int i;
	for (i = 0; i < 4; i++) {
		if (swIndex[i] == index && m_BassManagementMode != BASSMODE_NONE) return 1;
	}
	return 0;
}

void OutputMaster::initializeData()
{
	m_masterGain = 0.0;
	m_muteAll = false;
	m_clipperOn = true;
	m_filtersActive = false;

	m_meterCounter = 0;
	m_meterOn = false;

	setBassManagementMode(BASSMODE_NONE);
	setBassManagementFreq(150);
	setMeterUpdateFreq(10.0);
}

void OutputMaster::allocateChannels(int numChnls)
{
	m_gains.resize(numChnls);
	m_meters.resize(numChnls);
	//    filters = (FIRFILTER **) calloc(numChnls, sizeof(FIRFILTER *));
	m_lopass1.resize(numChnls);
	m_lopass2.resize(numChnls);
	m_hipass1.resize(numChnls);
	m_hipass2.resize(numChnls);
	swIndex[0] = numChnls - 1;
	swIndex[1] =  swIndex[2] = swIndex[3] = -1;

	for (int i = 0; i < numChnls; i++) {
		m_gains[i] = 1.0;
		m_meters[i] = 0;
		m_lopass1[i] = butter_create(m_framesPerSec, BUTTER_LP);
		m_lopass2[i] = butter_create(m_framesPerSec, BUTTER_LP);
		m_hipass1[i] = butter_create(m_framesPerSec, BUTTER_HP);
		m_hipass2[i] = butter_create(m_framesPerSec, BUTTER_HP);

	}
	m_numChnls = numChnls;
}

void *OutputMaster::meterThreadFunc(void *arg) {
	int chanindex = 0;
	OutputMaster *om = static_cast<OutputMaster *>(arg);
	float meter_levels[om->m_numChnls];

	al::osc::Send s(om->m_sendPort, om->m_sendAddress.c_str());
	while(om->m_runMeterThread) {
		pthread_mutex_lock(&om->m_meterMutex);
		pthread_cond_wait(&om->m_meterCond, &om->m_meterMutex);
		int bytes_read = om->m_meterBuffer.read((char *) meter_levels, om->m_numChnls * sizeof(float));
		if (bytes_read) {
			if (bytes_read !=  om->m_numChnls * sizeof(float)) {
				std::cerr << "Alloaudio: Warning. Meter values underrun." << std::endl;
			}
			for (int i = 0; i < bytes_read/sizeof(float); i++) {
				//            char addr[64];
				//            sprintf(addr,"/Alloaudio/meter%i", i);
				//            lo_send(t, "/Alloaudio/meter", "if", i, meter_levels[i]);
				//            lo_send(t, "/Alloaudio/meterdb", "if", i, 20.0 * log10(meter_levels[i]));
				//            lo_send(t, addr, "f", 20.0 * log10(meter_levels[i]));
				//            lo_send(t, addr, "f", meter_levels[i]);

				s.send("/Alloaudio/meterdb", chanindex++,
					   (float) (20.0 * log10(meter_levels[i])));
				if (chanindex == om->m_numChnls) {
					chanindex = 0;
				}
			}
		}
		pthread_mutex_unlock(&om->m_meterMutex);
	}
	return NULL;
}

// FIXME: Make this thread safe!
void OutputMaster::OSCHandler::onMessage(osc::Message &m)
{
	if (m.addressPattern() == "/Alloaudio/gain") {
		if (m.typeTags() == "if") {
			int chan;
			float gain;
			m >> chan >> gain;
			outputmaster->setGain(chan, gain);
		} else {
			std::cerr << "Alloaudio: Wrong type tags for /Alloaudio/gain message: "
					  << m.typeTags() << std::endl;
		}
	} else if (m.addressPattern() == "/Alloaudio/global_gain") {
		if (m.typeTags() == "f") {
			float gain;
			m >> gain;
			outputmaster->setMasterGain(gain);
		} else {
			std::cerr << "Alloaudio: Wrong type tags for /Alloaudio/global_gain message: "
					  << m.typeTags() << std::endl;
		}
	} else if (m.addressPattern() == "/Alloaudio/clipper_on") {
		if (m.typeTags() == "i") {
			int clipper_on;
			m >> clipper_on;
			outputmaster->setClipperOn(clipper_on != 0);
		} else {
			std::cerr << "Alloaudio: Wrong type tags for /Alloaudio/clipper_on: "
					  << m.typeTags() << std::endl;
		}
	} else if (m.addressPattern() == "/Alloaudio/mute_all") {
		if (m.typeTags() == "i") {
			int mute_all;
			m >> mute_all;
			outputmaster->setMuteAll(mute_all != 0);
		} else {
			std::cerr << "Alloaudio: Wrong type tags for /Alloaudio/mute_all: "
					  << m.typeTags() << std::endl;
		}
	} else if (m.addressPattern() == "/Alloaudio/meter_on") {
		if (m.typeTags() == "i") {
			int on;
			m >> on;
			outputmaster->setMeterOn(on != 0);
		} else {
			std::cerr << "Alloaudio: Wrong type tags for /Alloaudio/meter_update_freq: "
					 << m.typeTags() << std::endl;
		}
	} else if (m.addressPattern() == "/Alloaudio/meter_update_freq") {
		if (m.typeTags() == "f") {
			float freq;
			m >> freq;
			outputmaster->setMeterUpdateFreq(freq);
		} else {
			std::cerr << "Alloaudio: Wrong type tags for /Alloaudio/meter_update_freq: "
					 << m.typeTags() << std::endl;
		}
	} else {
		std::cout << "Alloaudio: Unrecognized address pattern: " << m.addressPattern() << std::endl;
	}
}