/*
 * filter103.c
 *
 * saturated SVF for stm32f103 blue pill
 *
 *  Created on: Apr 25, 2021
 *      Author: Evgeniy "mrf" Chernykh
 *      GPL
 */
//#include "dsp.h"

#include "stm32f1xx.h"


//one should touch
#define SAMPLE_RATE 48000


//one may touch
#define HEADROOM_CONTROL 16384
#define HEADROOM_SIGNAL  65536




//one may not touch

#define FLT_CUTOFF_COEF (0x6487ED511ULL / SAMPLE_RATE) //2PI/SR

int32_t w; //cutoff coef
int32_t d; //q coef

///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
// control rate


#define HEADROOM_CONTROL_NORM_COEF (0x100000000ULL / HEADROOM_CONTROL)

//you should make your own note to HZ scale function
//bear in mind, that on 48k it may not be in perfect tune
//resonance left aligned full scale uint
void filter_set_freq(uint16_t freq_in_hz, uint16_t resonance)
{
	//limit to prevent overflow
	if (freq_in_hz > (0xFFFFFFFF/FLT_CUTOFF_COEF))
		freq_in_hz = (0xFFFFFFFF/FLT_CUTOFF_COEF);

	//calculate cut - W
	int32_t tmp_w = freq_in_hz * FLT_CUTOFF_COEF / HEADROOM_CONTROL_NORM_COEF;

	//calculate resonance limiting factor
	uint32_t reso_limit = 0x1F851EB85ULL / HEADROOM_CONTROL_NORM_COEF; // low resonance limit is 1.97
	if (tmp_w > (0xB3333333 / HEADROOM_CONTROL_NORM_COEF)) // if cutoff freq > 0.7 (i.e. too high)
	{
		reso_limit -= tmp_w - (0xB3333333 / HEADROOM_CONTROL_NORM_COEF) * 3; //increase resonance then
	}

	int32_t tmp_d = resonance * 0x6000 / 0x8000; //make it 1.5 of 32768 scale
	tmp_d = 0x7FFF - tmp_d;

	//make curve in resonance point to increase control
	if (tmp_d < 0)
		tmp_d = tmp_d * (-tmp_d) / (0x40000000 / HEADROOM_CONTROL);
	else
		tmp_d = tmp_d * tmp_d / (0x40000000 / HEADROOM_CONTROL); //0x10000 65536
	tmp_d = tmp_d + tmp_d;
	//limit low resonance to prevent bang
	if (tmp_d > reso_limit)
		tmp_d = reso_limit;

	//atomic write coefficients to prevent blow-up
	__disable_irq();
	w = tmp_w;
	d = tmp_d;
	__enable_irq();
}
///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
// audio rate

#if HEADROOM_SIGNAL == 65536
#define SSAT_POS 17
#else
#error "change saturation value according to headroom"
#endif

int32_t bp;
int32_t lp;

//!!!! INPUT MUST NOT EXCEED HEADROOM_SIGNAL
//!!!! INPUT SHOULD BE AT LEAST 3 TIMES LOWER THAN HEADROOM_SIGNAL
// i.e. if HEADROOM_SIGNAL is 65536, than input is -16384..16383 is okay.
//it returns high pass. you can get bp and lp from the feedback variables
int32_t filter_one_tap(int32_t in)
{
	int32_t s1_hp = in - bp * d / HEADROOM_SIGNAL + lp;
	int32_t s2_bp = bp + s1_hp * w / HEADROOM_CONTROL;
	//int32_t s2_bps;
	// holy f, it's "6.47.3.1 Simple Constraints"
	asm ("ssat %0, %1, %2" : "=r" (s2_bp) :"n"(SSAT_POS), "r" (s2_bp));
	int32_t s3_lp = lp + s2_bp * w / HEADROOM_CONTROL;
	//int32_t s3_lps;
	asm ("ssat %0, %1, %2" : "=r" (s3_lp) :"n"(SSAT_POS), "r" (s3_lp));
	//end
	bp = s2_bp;
	lp = s3_lp;
	return s1_hp;
}


#define HEADROOM_SATURATE (HEADROOM_SIGNAL/2)
int32_t filter_softyfy(int32_t in)
{
	int32_t s1 = in/2;
	if (s1<0)
	{
		//negative
		s1 += HEADROOM_SATURATE;
		s1 = s1 * s1 / HEADROOM_SATURATE;
		s1 -= HEADROOM_SATURATE;
	}
	else
	{
		//positive
		s1 = HEADROOM_SATURATE - s1;
		s1 = s1*s1 / HEADROOM_SATURATE;
		s1 = HEADROOM_SATURATE - s1;
	}
	return s1;
}




///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////


// usage example for 17bit
void sample_rate_tap()
{
	int16_t some_signal; //from -32768 to +32767

	filter_one_tap(some_signal/2);
	int32_t out = filter_softyfy(lp); //from -65536 to +65535

	int16_t out2 = out / 2; //from -32768 to 32767
}









