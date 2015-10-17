
/*
 *   O2EM Free Odyssey2 / Videopac+ Emulator
 *
 *   Created by Daniel Boris <dboris@comcast.net>  (c) 1997,1998
 *
 *   Developed by Andre de la Rocha   <adlroc@users.sourceforge.net>
 *             Arlindo M. de Oliveira <dgtec@users.sourceforge.net>
 *
 *   http://o2em.sourceforge.net
 *
 *
 *
 *   O2 audio emulation
 */


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"
#include "types.h"
#include "config.h"
#include "vmachine.h"
#include "audio.h"

#ifndef __LIBRETRO__
#include "allegro.h"
#else
extern int SND;
extern uint8_t soundBuffer[1056];
#endif

#define SAMPLE_RATE 44100
#define PERIOD1 11
#define PERIOD2 44

#define SOUND_BUFFER_LEN 1056

#define AUD_CTRL  0xAA
#define AUD_D0	  0xA7
#define AUD_D1	  0xA8
#define AUD_D2	  0xA9


int sound_IRQ;
#ifndef __LIBRETRO__
static AUDIOSTREAM *stream=NULL;
#endif
FILE *sndlog=NULL;

static double flt_a=0.0, flt_b=0.0;
static unsigned char flt_prv = 0;


static void o2_filter(unsigned char *buf, unsigned long len);


void audio_process(unsigned char *buffer){
	unsigned long aud_data;
	int volume, re_circ, noise, enabled, intena, period, pnt, cnt, rndbit, pos;
	
	aud_data = (VDCwrite[AUD_D2] | (VDCwrite[AUD_D1] << 8) | (VDCwrite[AUD_D0] << 16));

	intena = VDCwrite[0xA0] & 0x04;

	pnt = cnt = 0;

	noise = VDCwrite[AUD_CTRL] & 0x10;
	enabled = VDCwrite[AUD_CTRL] & 0x80;
	rndbit = (enabled && noise) ? (rand()%2) : 0;

	while (pnt < SOUND_BUFFER_LEN) {
		pos = (tweakedaudio) ? (pnt/3) : (MAXLINES-1);
		volume = AudioVector[pos] & 0x0F;
		enabled = AudioVector[pos] & 0x80;
		period = (AudioVector[pos] & 0x20) ? PERIOD1 : PERIOD2;
		re_circ = AudioVector[pos] & 0x40;
		
		buffer[pnt++] = (enabled) ? ((aud_data & 0x01)^rndbit) * (0x10 * volume) : 0;
		cnt++;

		if (cnt >= period) {
			cnt=0;
			aud_data = (re_circ) ? ((aud_data >> 1) | ((aud_data & 1) << 23)) : (aud_data >> 1);
			rndbit = (enabled && noise) ? (rand()%2) : 0;

			if (enabled && intena && (!sound_IRQ)) {
				sound_IRQ = 1;
				ext_IRQ();
			}		
		}
	}
	
	if (app_data.filter) o2_filter(buffer, SOUND_BUFFER_LEN);
}



void update_audio(void) {
#ifndef __LIBRETRO__
	unsigned char *p;
	if (app_data.sound_en) {
		p = (unsigned char *)get_audio_stream_buffer(stream);
		if (p) {
			audio_process(p);
			if (sndlog) fwrite(p,1,SOUND_BUFFER_LEN,sndlog);
			free_audio_stream_buffer(stream);
		}
   	}
#else
	//unsigned char *p;
	//p = (unsigned char *)SNDBUF;
	//if (p) {
	//	audio_process(p);
        audio_process(soundBuffer);
	//}
#endif
}


void init_audio(void) {
	int i;

	sound_IRQ=0;		
	if ((app_data.sound_en) || (app_data.voice)) {
		printf("Initializing sound system...\n");
#ifndef __LIBRETRO__
		i = install_sound(DIGI_AUTODETECT, MIDI_NONE, NULL);
		if (i != 0) {
		   printf("  ERROR: could not initialize sound card\n   %s\n",allegro_error);
		   printf("  Sound system disabled\n");
		   app_data.sound_en = 0;
		   app_data.voice = 0;
		}
		else {
			if (digi_driver->name && (strlen(digi_driver->name)>0)){
				printf("  Sound system initialized ok\n");
				printf("  Sound driver [%s] detected\n",digi_driver->name);
				set_volume(-1,-1);
				init_sound_stream();
			} else {
				printf("  ERROR: could not initialize sound card\n");
				printf("  Sound system disabled\n");
				app_data.sound_en = 0;
				app_data.voice = 0;
			}
		}
#else
	init_sound_stream();
#endif
	}

	sndlog = NULL;
}


void init_sound_stream(void){
	int vol;
	if (app_data.sound_en){
		if (app_data.filter)
			vol = (255*app_data.svolume)/100;
		else
			vol = (255*app_data.svolume)/200;
#ifndef __LIBRETRO__
		stream = play_audio_stream(SOUND_BUFFER_LEN,8,0,SAMPLE_RATE,vol,128);
		if (!stream) {
			printf("Error creating audio stream!\n");
			app_data.sound_en=0;
		}
#endif
		flt_a = flt_b = 0.0;
		flt_prv = 0;
	}
}


void mute_audio(void){
#ifndef __LIBRETRO__
	if (app_data.sound_en && stream){
		stop_audio_stream(stream);
		stream=NULL;
	}
#else
	SND=0;
#endif
}


void close_audio(void) {
#ifndef __LIBRETRO__
	if (app_data.sound_en && stream) {
		stop_audio_stream(stream);
	}
#endif
	if (sndlog) fclose(sndlog);
	app_data.sound_en=0;
}


static void o2_filter(unsigned char *buffer, unsigned long len){
	static unsigned char buf[SOUND_BUFFER_LEN];
	int t;
	unsigned long i;
	if (len>SOUND_BUFFER_LEN) return;
	memcpy(buf,buffer,len);	
	for (i=0; i<len; i++){
		t = (i==0)?(buf[0]-flt_prv):(buf[i]-buf[i-1]);
		if (t) flt_b = (double)t;
		flt_a += flt_b/4.0 - flt_a/80.0;
		flt_b -= flt_b/4.0;
		if ((flt_a>255.0)||(flt_a<-255.0)) flt_a=0.0;
		buffer[i] = (unsigned char)((flt_a+255.0)/2.0);
	}
	flt_prv = buf[len-1];
}


