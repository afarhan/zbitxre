// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// core_sdr.c — the radio itself: tuning, modes, filters and the receiver.
//
// This is zbitxv2's sbitx.c. Its functions and its arrays keep their names,
// and the receiver's arithmetic is unchanged. sdr.h explains how the
// receiver and its filter work. Thanks to Phil Karn, KA9Q, whose method of
// filtering by fast convolution it follows.
//
// What has gone from sbitx.c has gone to the hardware layer: the pins are in
// hw/board.c, the Si5351 in hw/si5351.c, the sound card in hw/audio.c. The
// transmitter, the T/R switch and the settings file are still to come.
//
// WHERE THE RADIO WORKS
//
// The first oscillator is set so that the wanted frequency lands on
// center_bin, which the board decides. hw/board.c says why the zbitx works
// at bin 600.
//
// AM
//
// AM is not brought down to baseband. It is filtered where it is, at the IF,
// and then rectified. The carrier of the station tuned in lands on the tuned
// bin like any other signal, so the filter is put around that bin, as wide as
// r1:high on either side. On the zbitx that is around 28.125 kHz, bin 600,
// and on a board that works at bin 512 it is around 24 kHz. The filter has to
// follow the bin: a filter left at 24 kHz on a radio that works at bin 600
// passes what is 4 kHz below the station and cuts the station off.
//
// RECEIVING, AND THE MODEMS
//
// rx_linear() does what every mode needs: it turns each block of samples
// from the sound card into frequency bins, with one forward FFT, and paints
// the spectrum from those bins. Then it hands the bins to the modem of the
// mode in use, which does the rest.
//
// A modem is a file in modems/, and modem.h says what one is. Its rx() is
// given the bins. It uses the primitives here to turn them into sound:
// demod_ssb() and demod_am() carry on from the bins to demodulated audio,
// filtered and levelled, in the steps of zbitxv2's rx_linear(), and
// speaker_play() sends audio to the speaker. What else it does with the
// audio, decoding CW or FT8, is its own business.
//
// The modem follows the mode. sdr_request() finds the modem of the mode's
// name and offers it to the audio thread with the rest of the receiver; the
// audio thread takes it at the start of a block and resets it, so a mode
// never changes halfway through a block. A mode with no modem of its own,
// FT8 or DIGI for now, is received by the USB modem.
//
// THE SPECTRUM
//
// zbitxv2 did a second forward FFT, of the samples multiplied by a Hann
// window, only for the spectrum: without a window the spectrum looks
// horrible, and the demodulator must not have one. The same window can be
// put on the bins after the FFT, and cheaply: a Hann window in time is, in
// frequency, each bin taken at a half, less a quarter of each of its two
// neighbours. So the spectrum now comes from the demodulator's own bins, and
// the receiver does two FFTs a block where it did three. The window put on
// this way is the periodic Hann window where zbitxv2's was the symmetric
// one, so the spectrum differs from zbitxv2's by a little; the sound does
// not differ at all.
//
// WHO CHANGES WHAT
//
// The audio thread runs sound_block(), and from it sound_process(),
// rx_linear() and agc2(). rx_list belongs to that thread.
//
// sdr_request() and everything it calls are for the thread that owns the
// controls. That thread never touches rx_list. It works on rx_wanted, a
// receiver of its own, and when it has changed something it offers a copy:
// the mode, the AGC's speed, the tuned bin, the passband, and the filter's
// fir_coeff. At the start of a block the audio thread takes the newest copy
// offered and puts it into rx_list. The copies are kept three times over, so
// that one is never written while it is being read, and neither thread ever
// waits for the other.
//
// The S meter is worked out by the audio thread once a block, from its own
// samples, and sdr_request() reports the last value.
//
// SETTING UP
//
// setup() sets up the whole radio, in the order zbitxv2's setup() does: the
// board's pins, the Si5351, the transforms, the oscillators, the receiver
// and transmitter, and last the sound card, which starts the audio thread.
// teardown() undoes it. There is one way to set the radio up. The tests that
// run on a computer with no radio are built with tests/hw_stand_ins.c in
// place of hw/, and call the same setup().

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <complex.h>
#include <fftw3.h>
#include <unistd.h>
#include <stdint.h>
#include <stdatomic.h>
#include <sys/types.h>

#include "audio.h"
#include "board.h"
#include "i2c.h"
#include "si5351.h"
#include "sdr.h"
#include "modem.h"

static int center_bin = 512;

float fft_bins[MAX_BINS]; // spectrum ampltiudes
int spectrum_plot[MAX_BINS];
fftw_complex *fft_spectrum;
static void set_rx1(int frequency);

// Wisdom Defines for the FFTW and FFTWF libraries
// Options for WISDOM_MODE from least to most rigorous are FFTW_ESTIMATE, FFTW_MEASURE, FFTW_PATIENT, and FFTW_EXHAUSTIVE
// The FFTW_ESTIMATE mode seems to make completely incorrect Wisdom plan choices sometimes, and is not recommended.
// Wisdom plans found in an existing Wisdom file will negate the need for time consuming Wisdom plan calculations
// if the Wisdom plans in the file were generated at the same or more rigorous level.
#define WISDOM_MODE FFTW_MEASURE
#define PLANTIME -1		// spend no more than plantime seconds finding the best FFT algorithm. -1 turns the platime cap off.
char wisdom_file[WISDOM_PATH_MAX] = "data/sbitx_wisdom.wis";

fftw_complex *fft_out;		// holds the incoming samples in freq domain (for rx as well as tx)
fftw_complex *fft_in;			// holds the incoming samples in time domain (for rx as well as tx)
fftw_complex *fft_m;			// holds previous samples for overlap and discard convolution
fftw_plan plan_fwd;
int bfo_freq = 40035000;
int freq_hdr = -1;

static int rx_gain = 100;
static int rx_vol = 100;
static double spectrum_speed = 0.3;
static int in_tx = 0;
struct rx *rx_list = NULL;
struct rx *tx_list = NULL;
struct filter *tx_filter;	//convolution filter
static int rx_pitch = 700; //used only to offset the lo for CW,CWR

#define MUTE_MAX 6
static int mute_count = 50;

#define TUNING_SHIFT (0)

#define BFO_CLOCK 1
#define OSCILLATOR_CLOCK 2

//the receiver as the controls want it, and the copies offered to the audio thread
#define OFFERS 3
#define WHICH_OFFER 3u
#define IS_NEW 4u

struct offer {
	const struct modem *modem;
	short mode;
	int agc_speed;
	long tuned_bin;
	int low_hz;
	int high_hz;
	complex float fir_coeff[MAX_BINS];
};

static struct rx *rx_wanted = NULL;
static struct offer offers[OFFERS];
static unsigned offer_filling = 0;
static unsigned offer_in_use = 1;
static atomic_uint offer_waiting = 2u;

static atomic_int s_meter;
static int is_set_up = 0;
static uint32_t oscillator_hz = 0;

static const struct modem *modem_wanted = NULL;
static const struct modem *modem_in_use = NULL;
static int32_t *speaker_now = NULL;

static void offer_the_receiver(){
	struct offer *o = &offers[offer_filling];

	o->modem = modem_wanted;
	o->mode = rx_wanted->mode;
	o->agc_speed = rx_wanted->agc_speed;
	o->tuned_bin = rx_wanted->tuned_bin;
	o->low_hz = rx_wanted->low_hz;
	o->high_hz = rx_wanted->high_hz;
	memcpy(o->fir_coeff, rx_wanted->filter->fir_coeff,
		sizeof(complex float) * MAX_BINS);
	offer_filling = atomic_exchange(&offer_waiting, offer_filling | IS_NEW)
		& WHICH_OFFER;
}

static void take_the_newest_offer(){
	if (!(atomic_load(&offer_waiting) & IS_NEW))
		return;
	offer_in_use = atomic_exchange(&offer_waiting, offer_in_use) & WHICH_OFFER;

	struct offer *o = &offers[offer_in_use];
	if (o->modem != modem_in_use && o->modem != NULL){
		modem_in_use = o->modem;
		if (modem_in_use->reset)
			modem_in_use->reset();
	}
	rx_list->mode = o->mode;
	rx_list->agc_speed = o->agc_speed;
	rx_list->tuned_bin = o->tuned_bin;
	rx_list->low_hz = o->low_hz;
	rx_list->high_hz = o->high_hz;
	memcpy(rx_list->filter->fir_coeff, o->fir_coeff,
		sizeof(complex float) * MAX_BINS);
}

static void radio_tune_to(u_int32_t f){
	int freq_shift = (int)(46.875 * (1.0 * rx_wanted->tuned_bin));

	if (rx_wanted->mode == MODE_CW)
		oscillator_hz = f + bfo_freq - freq_shift + TUNING_SHIFT - rx_pitch;
	else if (rx_wanted->mode == MODE_CWR)
		oscillator_hz = f + bfo_freq - freq_shift + TUNING_SHIFT + rx_pitch;
	else
		oscillator_hz = f + bfo_freq - freq_shift + TUNING_SHIFT;

	si5351_set_freq(OSCILLATOR_CLOCK, oscillator_hz);
}

uint32_t oscillator_frequency(void){
	return oscillator_hz;
}

static void fft_init(){
	fft_m = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * MAX_BINS/2);
	fft_in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * MAX_BINS);
	fft_out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * MAX_BINS);
	fft_spectrum = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * MAX_BINS);

	memset(fft_spectrum, 0, sizeof(fftw_complex) * MAX_BINS);
	memset(fft_in, 0, sizeof(fftw_complex) * MAX_BINS);
	memset(fft_out, 0, sizeof(fftw_complex) * MAX_BINS);
	memset(fft_m, 0, sizeof(fftw_complex) * MAX_BINS/2);

	fftw_set_timelimit(PLANTIME);
	fftwf_set_timelimit(PLANTIME);
	int e = fftw_import_wisdom_from_filename(wisdom_file);
	if (e == 0)
	{
		printf("Generating Wisdom File...\n");
	}
	plan_fwd = fftw_plan_dft_1d(MAX_BINS, fft_in, fft_out, FFTW_FORWARD, WISDOM_MODE); // Was FFTW_ESTIMATE N3SB
	fftw_export_wisdom_to_filename(wisdom_file);

	//zero up the previous 'M' bins
	for (int i= 0; i < MAX_BINS/2; i++){
		__real__ fft_m[i]  = 0.0;
		__imag__ fft_m[i]  = 0.0;
	}

}

//the T/R switch, still to come, calls this and spectrum_reset(); they
//become static with it
void fft_reset_m_bins(){
	//zero up the previous 'M' bins
	memset(fft_in, 0, sizeof(fftw_complex) * MAX_BINS);
	memset(fft_out, 0, sizeof(fftw_complex) * MAX_BINS);
	memset(fft_m, 0, sizeof(fftw_complex) * MAX_BINS/2);
	memset(fft_spectrum, 0, sizeof(fftw_complex) * MAX_BINS);
	memset(tx_list->fft_time, 0, sizeof(fftw_complex) * MAX_BINS);
	memset(tx_list->fft_freq, 0, sizeof(fftw_complex) * MAX_BINS);
}

void set_spectrum_speed(int speed){
	spectrum_speed = speed;
	for (int i = 0; i < MAX_BINS; i++)
		fft_bins[i] = 0;
}

void spectrum_reset(){
	for (int i = 0; i < MAX_BINS; i++)
		fft_bins[i] = 0;
}

static void spectrum_update(){
	//we are only using the lower half of the bins,
	//so this copies twice as many bins,
	//it can be optimized. leaving it here just in case
	//someone wants to try I Q channels
	//in hardware

	//we shift the bins by the offset of the center bin
	int offset_bin = center_bin - MAX_BINS/4;

	// this has been hand optimized to lower
	//the inordinate cpu usage
	for (int i = 1269; i < 1803; i++){

		fft_bins[i] = ((1.0 - spectrum_speed) * fft_bins[i]) +
			(spectrum_speed * cabs(fft_spectrum[i - offset_bin]));

		int y = power2dB(cnrmf(fft_bins[i]));
		spectrum_plot[i] = y;
	}
}

#define SCALING_TRIM 200.0 // Use this to tune your meter response 2.7 worked at 51% and my inverted L
// S-Meter test W2JON
static int calculate_s_meter() {
	double signal_strength = 0.0;

	// Summing up the magnitudes of the FFT output bins
	for (int i = 0; i < MAX_BINS / 2; i++)
	{
		double magnitude = cabs(rx_list->fft_time[i]); // Magnitude of complex FFT output in time domain
		signal_strength += magnitude;
	}

	// Now average out the "signal strength"
	signal_strength /= (MAX_BINS / 2);

	// Logarithmic scaling based on rx_gain setting in percentage [0-100]
	double gain_scaling_factor = log10((rx_gain * 1.0) / 100.0 + 1.0);

	// Convert to pseudo dB
	double reference_power = 1e-4; // 0.1 mW
	double signal_power = signal_strength * signal_strength * reference_power;
	double s_meter_db = 10 * log10(signal_power / reference_power); // pseudo dB

	s_meter_db += gain_scaling_factor * SCALING_TRIM; // Adjust calcs dynamically based on rx_gain * SCALING_TRIM

	// Calculate S-units and additional dB
	int s_units = (int)(s_meter_db / 6.0);				   // Each S-unit corresponds to 6 dB
	int additional_db = (int)(s_meter_db - (s_units * 6)); // Remaining 'dB' above S9

	// Ensure non-negative values
	if (s_units < 0)
		s_units = 0;
	if (additional_db < 0)
		additional_db = 0;

	// Cap additional S-units at 20+ for simplicity
	if (s_units >= 9)
	{
		if (additional_db > 20)
			additional_db = 20;
	}

	// Return the value formatted as "S-unit * 100 + additional dB"
	return (s_units * 100) + additional_db;
}

static void set_rx1(int frequency){
	if (frequency == freq_hdr)
		return;
	radio_tune_to(frequency);
	freq_hdr = frequency;
}

static void tx_init(int frequency, short mode, int bpf_low, int bpf_high){
	(void)frequency;
	(void)mode;
	(void)bpf_low;
	(void)bpf_high;

	//we assume that there are 96000 samples / sec, giving us a 48khz slice
	//the tuning can go up and down only by 22 KHz from the center_freq

	tx_filter = filter_new(1024, 1025);
}

static struct rx *add_tx(int frequency, short mode, int bpf_low, int bpf_high){
	(void)frequency;

	//we assume that there are 96000 samples / sec, giving us a 48khz slice
	//the tuning can go up and down only by 22 KHz from the center_freq

	struct rx *r = malloc(sizeof(struct rx));
	r->low_hz = bpf_low;
	r->high_hz = bpf_high;
	r->tuned_bin = 512;

	//create fft complex arrays to convert the frequency back to time
	r->fft_time = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * MAX_BINS);
	r->fft_freq = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * MAX_BINS);

	int e = fftw_import_wisdom_from_filename(wisdom_file);
	if (e == 0)
	{
		printf("Generating Wisdom File...\n");
	}
	r->plan_rev = fftw_plan_dft_1d(MAX_BINS, r->fft_freq, r->fft_time, FFTW_BACKWARD, WISDOM_MODE); // Was FFTW_ESTIMATE N3SB
	fftw_export_wisdom_to_filename(wisdom_file);

	r->output = 0;
	r->next = NULL;
	r->mode = mode;

	r->filter = filter_new(1024, 1025);
	filter_tune(r->filter, (1.0 * bpf_low)/96000.0, (1.0 * bpf_high)/96000.0 , 5);

	r->agc_speed = 10;
	r->agc_threshold = -60;
	r->agc_loop = 0;

	//the modems drive the tx at 12000 Hz, this has to be upconverted
	//to the radio's sampling rate

	r->next = tx_list;
	tx_list = r;
	return r;
}

static struct rx *add_rx(int frequency, short mode, int bpf_low, int bpf_high){
	(void)frequency;

	//we assume that there are 96000 samples / sec, giving us a 48khz slice
	//the tuning can go up and down only by 22 KHz from the center_freq

	struct rx *r = malloc(sizeof(struct rx));
	r->low_hz = bpf_low;
	r->high_hz = bpf_high;
	r->tuned_bin = 512;
	r->agc_gain = 0.0;

	//create fft complex arrays to convert the frequency back to time
	r->fft_time = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * MAX_BINS);
	r->fft_freq = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * MAX_BINS);

	int e = fftw_import_wisdom_from_filename(wisdom_file);
	if (e == 0)
	{
		printf("Generating Wisdom File...\n");
	}
	r->plan_rev = fftw_plan_dft_1d(MAX_BINS, r->fft_freq, r->fft_time, FFTW_BACKWARD, WISDOM_MODE);  // Was FFTW_ESTIMATE N3SB
	fftw_export_wisdom_to_filename(wisdom_file);

	r->output = 0;
	r->next = NULL;
	r->mode = mode;

	r->filter = filter_new(1024, 1025);
	filter_tune(r->filter, (1.0 * bpf_low)/96000.0, (1.0 * bpf_high)/96000.0 , 5);

	r->agc_speed = 300;
	r->agc_threshold = -60;
	r->agc_loop = 0;
	r->signal_avg = 0;

	// the modems are driven by 12000 samples/sec
	// the queue is for 20 seconds, 5 more than 15 sec needed for the FT8

	r->next = rx_list;
	rx_list = r;
	return r;
}

static double agc2(struct rx *r){
	int i;
	double signal_strength, agc_gain_should_be;

	//do nothing if agc is off
	if (r->agc_speed == -1){
		for (i=0; i < MAX_BINS/2; i++)
			__imag__ (r->fft_time[i+(MAX_BINS/2)]) *=10000000;
		return 10000000;
	}

	//find the peak signal amplitude
	signal_strength = 0.0;
	for (i=0; i < MAX_BINS/2; i++){
		double s = cimag(r->fft_time[i+(MAX_BINS/2)]) * 1000;
		if (signal_strength < s)
			signal_strength = s;
	}
	//also calculate the moving average of the signal strength
	r->signal_avg = (r->signal_avg * 0.93) + (signal_strength * 0.07);
	if (signal_strength == 0)
		agc_gain_should_be = 10000000;
	else
		agc_gain_should_be = 100000000000/signal_strength;
	r->signal_strength = signal_strength;

	double agc_ramp = 0.0;

	// climb up the agc quickly if the signal is louder than before
	if (agc_gain_should_be < r->agc_gain){
		r->agc_gain = agc_gain_should_be;
		//reset the agc to hang count down
		r->agc_loop = r->agc_speed;
	}
	else if (r->agc_loop <= 0){
		agc_ramp = (agc_gain_should_be - r->agc_gain) / (MAX_BINS/2);
	}

	if (agc_ramp != 0){
		for (i = 0; i < MAX_BINS/2; i++){
			__imag__ (r->fft_time[i+(MAX_BINS/2)]) *= r->agc_gain;
		}
		r->agc_gain += agc_ramp;
	}
	else
		for (i = 0; i < MAX_BINS/2; i++)
			__imag__ (r->fft_time[i+(MAX_BINS/2)]) *= r->agc_gain;

	r->agc_loop--;

	return 100000000000 / r->agc_gain;
}

static void my_fftw_execute(fftw_plan f){
	fftw_execute(f);
}

//TODO : optimize the memory copy and moves to use the memcpy
static void rx_linear(int32_t *input_rx,  int32_t *input_mic,
	int32_t *output_speaker, int32_t *output_tx, int n_samples)
{
	int i, j = 0;
	double i_sample, q_sample;

	(void)input_mic;
	(void)n_samples;

	//STEP 1: first add the previous M samples to
	for (i = 0; i < MAX_BINS/2; i++)
		fft_in[i]  = fft_m[i];

	//STEP 2: then add the new set of samples
	// m is the index into incoming samples, starting at zero
	// i is the index into the time samples, picking from
	// the samples added in the previous step
	int m = 0;
	//gather the samples into a time domain array
	for (i= MAX_BINS/2; i < MAX_BINS; i++){
		i_sample = (1.0  *input_rx[j])/20000000.0;
		q_sample = 0;

		j++;

		__real__ fft_m[m] = i_sample;
		__imag__ fft_m[m] = q_sample;

		__real__ fft_in[i]  = i_sample;
		__imag__ fft_in[i]  = q_sample;
		m++;
	}

	// STEP 3: convert the time domain samples to  frequency domain
	my_fftw_execute(plan_fwd);

	//STEP 3B: this is a side line, we use these frequency domain
	// values to paint the spectrum in the user interface
	// I discovered that the raw time samples give horrible spectrum
	// and they need to be multiplied wiht a window function
	// the window is put on the bins themselves: a hann window is
	// each bin at a half, less a quarter of each of its neighbours
	// NOTE: the spectrum update has nothing to do with the actual
	// signal processing. If you are not showing the spectrum or the
	// waterfall, you can skip these steps
	for (i = 0; i < MAX_BINS; i++){
		int before = i - 1;
		int after = i + 1;
		if (before < 0)
			before = MAX_BINS - 1;
		if (after >= MAX_BINS)
			after = 0;
		fft_spectrum[i] = 0.5 * fft_out[i] - 0.25 * (fft_out[before] + fft_out[after]);
	}

	// the spectrum display is updated
	spectrum_update();

	// ... back to the actual processing, after spectrum update

	//keep transmit buffer empty
	memset(output_tx, 0, MAX_BINS/2 * sizeof(int32_t));

	//STEP 4 onwards are the modem's, it demodulates the bins and
	//sends what it wants heard to the speaker
	speaker_now = output_speaker;
	memset(output_speaker, 0, MAX_BINS/2 * sizeof(int32_t));
	if (modem_in_use)
		modem_in_use->rx(fft_out);
	speaker_now = NULL;

	if (mute_count){
		memset(output_speaker, 0, MAX_BINS/2 * sizeof(int32_t));
		mute_count--;
	}
}

/*
	the steps 4 to 8 of the receiver, that every mode that is
	demodulated from one sideband shares. the bins are rotated to
	bring the tuned frequency to zero, the other sideband is cut
	off, the filter is applied and they are turned back into time.
	CW and CWR are single sideband signals too.
*/
static void baseband(struct rx *r, fftw_complex *bins, int shift, int one_sideband){
	int i;

	//STEP 4: we rotate the bins around by r-tuned_bin
	for (i = 0; i < MAX_BINS; i++){
		int b =  i + shift;
		if (b >= MAX_BINS)
			b = b - MAX_BINS;
		if (b < 0)
			b = b + MAX_BINS;
		r->fft_freq[i] = bins[b];
	}

	// STEP 5:zero out the other sideband
	if (one_sideband && (r->mode == MODE_LSB || r->mode == MODE_CWR))
		for (i = 0; i < MAX_BINS/2; i++){
			__real__ r->fft_freq[i] = 0;
			__imag__ r->fft_freq[i] = 0;
		}
	else if (one_sideband)
		for (i = MAX_BINS/2; i < MAX_BINS; i++){
			__real__ r->fft_freq[i] = 0;
			__imag__ r->fft_freq[i] = 0;
		}

	// STEP 6: apply the filter to the signal,
	// in frequency domain we just multiply the filter
	// coefficients with the frequency domain samples
	for (i = 0; i < MAX_BINS; i++)
		r->fft_freq[i] *= r->filter->fir_coeff[i];

	//STEP 7: convert back to time domain
	my_fftw_execute(r->plan_rev);

	//STEP 8 : AGC
	agc2(r);
}

/*
	single sideband: USB, LSB, CW, CWR, and the digital modes that
	ride on USB. the sideband is the receiver's mode.
	fills baseband_output with MAX_BINS/2 samples of audio
*/
void demod_ssb(fftw_complex *bins, int32_t *baseband_output){
	// we may add another sub receiver within the pass band later,
	// hence, the linkced list of receivers here
	// at present, we handle just the first receiver
	struct rx *r = rx_list;
	int i;

	baseband(r, bins, r->tuned_bin, 1);

	//STEP 9: send the output back to where it needs to go
	for (i= 0; i < MAX_BINS/2; i++)
		baseband_output[i] = cimag(r->fft_time[i+(MAX_BINS/2)]);
}

/*
	AM is not rotated to baseband, it is filtered around its carrier
	where it is, both sidebands kept, and rectified
*/
void demod_am(fftw_complex *bins, int32_t *baseband_output){
	struct rx *r = rx_list;
	int i;

	baseband(r, bins, 0, 0);

	//STEP 9: send the output back to where it needs to go
	for (i= 0; i < MAX_BINS/2; i++)
		baseband_output[i] = cabs(r->fft_time[i+(MAX_BINS/2)]);
}

/*
	the passband a modem wants, as the core's r1:low and r1:high,
	with zbitxv2's limits: the low edge no lower than 50 Hz and the
	high edge no higher than 5 kHz
*/
void set_passband(int low_hz, int high_hz){
	char request[64], response[100];

	if (low_hz < 50)
		low_hz = 50;
	if (high_hz > 5000)
		high_hz = 5000;
	sprintf(request, "r1:low=%d", low_hz);
	sdr_request(request, response);
	sprintf(request, "r1:high=%d", high_hz);
	sdr_request(request, response);
}

const struct modem *modem_find(const char *name){
	for (int i = 0; modem_list[i] != NULL; i++){
		if (!strcmp(modem_list[i]->name, name))
			return modem_list[i];
	}
	return NULL;
}

/*
	what a modem wants heard. it is given the audio of one block,
	MAX_BINS/2 samples, and it goes to the speaker with this block.
	a modem that plays nothing leaves the speaker silent.
*/
void speaker_play(const int32_t *samples, int count){
	if (speaker_now == NULL)
		return;
	if (count > MAX_BINS/2)
		count = MAX_BINS/2;
	memcpy(speaker_now, samples, count * sizeof(int32_t));
}

/*
	This is called each time there is a block of signal samples ready
	either from the mic or from the rx IF
*/
static void sound_process(
	int32_t *input_rx, int32_t *input_mic,
	int32_t *output_speaker, int32_t *output_tx,
	int n_samples)
{
	take_the_newest_offer();
	if (!in_tx)
		rx_linear(input_rx, input_mic, output_speaker, output_tx, n_samples);
	atomic_store(&s_meter, calculate_s_meter());
}

/*
	The sound card hands over a block through this. The samples are halved
	on the way in, as zbitxv2's sound loop halved them.
*/
static int32_t input_rx_halved[MAX_BINS/2];
static int32_t input_mic_halved[MAX_BINS/2];

static void sound_block(struct audio_block *block){
	for (int i = 0; i < block->frames; i++){
		input_rx_halved[i] = block->from_receiver[i] / 2;
		input_mic_halved[i] = block->from_microphone[i] / 2;
	}
	sound_process(input_rx_halved, input_mic_halved,
		block->to_speaker, block->to_transmitter, block->frames);
}

static void set_rx_filter(){
	//on AM filter at the IF level, instead of the baseband
	//the carrier is at the tuned bin, the filter goes around it
	if (rx_wanted->mode == MODE_AM){
		double carrier_hz = 46.875 * (1.0 * rx_wanted->tuned_bin);
		filter_tune(rx_wanted->filter,
			(1.0 * (carrier_hz - rx_wanted->high_hz))/96000.0 ,
			(1.0 * (carrier_hz + rx_wanted->high_hz))/96000.0 ,
			5);
	}
	else if(rx_wanted->mode == MODE_LSB || rx_wanted->mode == MODE_CWR)
		filter_tune(rx_wanted->filter,
			(1.0 * -rx_wanted->high_hz)/96000.0,
			(1.0 * -rx_wanted->low_hz)/96000.0 ,
			5);
	else
		filter_tune(rx_wanted->filter,
			(1.0 * rx_wanted->low_hz)/96000.0,
			(1.0 * rx_wanted->high_hz)/96000.0 ,
			5);
	offer_the_receiver();
}

static void setup_oscillators(){
	//initialize the SI5351

	usleep(200000);
	si5351_init();
	usleep(200000);
	si5351_set_freq(BFO_CLOCK, bfo_freq);

	usleep(200000);
	si5351_set_freq(BFO_CLOCK, bfo_freq);

	si5351_reset_plls();
}

int setup(const struct board *board){
	if (is_set_up || board == NULL)
		return -1;
	if (board_open(board) < 0)
		return -1;
	if (i2c_open_bus_of(SI5351_ADDRESS) < 0)
		return -1;

	bfo_freq = board->bfo_hz;
	center_bin = board->centre_bin;

	fft_init();
	vfo_init_phase_table();
	setup_oscillators();

	for (int m = 0; modem_list[m] != NULL; m++){
		if (modem_list[m]->start)
			modem_list[m]->start();
	}
	modem_wanted = modem_find("LSB");
	modem_in_use = modem_wanted;

	add_rx(7000000, MODE_LSB, -3000, -300);
	add_tx(7000000, MODE_LSB, -3000, -300);
	rx_list->tuned_bin = center_bin;
	tx_list->tuned_bin = center_bin;
	tx_init(7000000, MODE_LSB, -3000, -150);

	//the receiver that the controls work on
	rx_wanted = malloc(sizeof(struct rx));
	memcpy(rx_wanted, rx_list, sizeof(struct rx));
	rx_wanted->filter = filter_new(1024, 1025);
	memcpy(rx_wanted->filter->fir_coeff, rx_list->filter->fir_coeff,
		sizeof(complex float) * MAX_BINS);
	rx_wanted->next = NULL;

	atomic_store(&s_meter, 0);
	freq_hdr = -1;
	mute_count = 50;
	is_set_up = 1;

	if (audio_open(sound_block) < 0)
		return -1;
	return 0;
}

void teardown(void){
	if (!is_set_up)
		return;
	is_set_up = 0;
	audio_close();
	si5351_clock_off(OSCILLATOR_CLOCK);
	si5351_clock_off(BFO_CLOCK);
	i2c_close();
	board_close();
}

void sdr_request(char *request, char *response){
	char cmd[100], value[1000];

	char *p = strchr(request, '=');
	if (!p || !is_set_up){
		strcpy(response, "error");
		return;
	}
	int n = p - request;
	strncpy(cmd, request, n);
	cmd[n] = 0;
	strcpy(value, request+n+1);

	if (!strcmp(cmd, "stat:tx")){
		if (in_tx)
			strcpy(response, "ok on");
		else
			strcpy(response, "ok off");
	}
	else if (!strcmp(cmd, "r1:freq")){
		int d = atoi(value);
		set_rx1(d);
		strcpy(response, "ok");
	}
	else if (!strcmp(cmd, "smeter")){
		sprintf(response, "%d", atomic_load(&s_meter));
	}
	else if (!strcmp(cmd, "r1:mode")){
		if (!strcmp(value, "LSB"))
			rx_wanted->mode = MODE_LSB;
		else if (!strcmp(value, "CW"))
			rx_wanted->mode = MODE_CW;
		else if (!strcmp(value, "CWR"))
			rx_wanted->mode = MODE_CWR;
		else if (!strcmp(value, "2TONE"))
			rx_wanted->mode = MODE_2TONE;
		else if (!strcmp(value, "TUNE"))
			rx_wanted->mode = MODE_TUNE;
		else if (!strcmp(value, "FT8"))
			rx_wanted->mode = MODE_FT8;
		else if (!strcmp(value, "AM"))
			rx_wanted->mode = MODE_AM;
		else if (!strcmp(value, "DIGI"))
			rx_wanted->mode = MODE_DIGITAL;
		else if (!strcmp(value, "USB"))
			rx_wanted->mode = MODE_USB;
		else {
			strcpy(response, "error");
			return;
		}

		//the modem of the mode's name, and USB for a mode with none
		modem_wanted = modem_find(value);
		if (modem_wanted == NULL)
			modem_wanted = modem_find("USB");

		//set the tx mode to that of the rx1
		tx_list->mode = rx_wanted->mode;

		// An interesting but non-essential note:
		// the sidebands inverted twice, to come out correctly after all
		// conisder that the second oscillator is set to 27.025 MHz and
		// a 7 MHz signal is tuned in by a 34 Mhz oscillator.
		// The first IF will be 25 Mhz, converted to a second IF of 25 KHz
		// Now, imagine that the signal at 7 Mhz moves up by 1 Khz
		// the IF now is going to be 34 - 7.001 MHz = 26.999 MHz which
		// converts to a second IF of 26.999 - 27.025 = 26 KHz
		// Effectively, if a signal moves up, so does the second IF

		if (rx_wanted->mode == MODE_AM){
			filter_tune(tx_list->filter,
				(1.0 * 20000)/96000.0,
				(1.0 * 30000)/96000.0 ,
				5);
			filter_tune(tx_filter,
				(1.0 * 20000)/96000.0,
				(1.0 * 30000)/96000.0 ,
				5);
		}
		else if (rx_wanted->mode == MODE_LSB || rx_wanted->mode == MODE_CWR){
			filter_tune(tx_list->filter,
				(1.0 * -3000)/96000.0,
				(1.0 * -300)/96000.0 ,
				5);
			filter_tune(tx_filter,
				(1.0 * -3000)/96000.0,
				(1.0 * -300)/96000.0 ,
				5);
		}
		else {
			filter_tune(tx_list->filter,
				(1.0 * 300)/96000.0,
				(1.0 * 3000)/96000.0 ,
				5);
			filter_tune(tx_filter,
				(1.0 * 300)/96000.0,
				(1.0 * 3000)/96000.0 ,
				5);
		}

		offer_the_receiver();

		//we need to nudge the oscillator to adjust
		//to cw offset. setting it to the already tuned freq
		//doesnt recalculte the offsets

		int f = freq_hdr;
		set_rx1(f-10);
		set_rx1(f);

		strcpy(response, "ok");
	}
	else if (!strcmp(cmd, "rx_pitch")){
		rx_pitch = atoi(value);
		strcpy(response, "ok");
	}
	else if(!strcmp(cmd, "r1:gain")){
		rx_gain = atoi(value);
		if(!in_tx)
			audio_set_receiver(rx_gain);
		strcpy(response, "ok");
	}
	else if (!strcmp(cmd, "r1:volume")){
		rx_vol = atoi(value);
		if(!in_tx)
			audio_set_speaker(rx_vol);
		strcpy(response, "ok");
	}
	else if(!strcmp(cmd, "r1:high")){
		rx_wanted->high_hz = atoi(value);
		set_rx_filter();
		strcpy(response, "ok");
	}
	else if(!strcmp(cmd, "r1:low")){
		rx_wanted->low_hz = atoi(value);
		set_rx_filter();
		strcpy(response, "ok");
	}
	else if (!strcmp(cmd, "r1:agc")){
		if (!strcmp(value, "OFF"))
			rx_wanted->agc_speed = -1;
		else if (!strcmp(value, "SLOW"))
			rx_wanted->agc_speed = 100;
		else if (!strcmp(value, "MED"))
			rx_wanted->agc_speed = 33;
		else if (!strcmp(value, "FAST"))
			rx_wanted->agc_speed = 10;
		else {
			strcpy(response, "error");
			return;
		}
		offer_the_receiver();
		strcpy(response, "ok");
	}
	else
		strcpy(response, "error unknown");
}
