#include "env.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <AL/al.h>
#include <AL/alc.h>
#include <SDL3/SDL.h>
#include <glad/glad.h>

#include "cfg.h"
#include "compat.h"

int frame;

static SDL_Window *_window;

static int _num_gamepads;
static SDL_Gamepad *_gamepads[16];

typedef struct {
	uint32_t scroll;
	uint8_t ciram[0x800];
	uint8_t oam[256];
	uint8_t palram[32];
} PPUState;

static PPUState _ppu_state;

static int _render;

static int _ppuaddr;
static int _ppuaddr_inc;

static int _scnd_w;

static uint8_t _input_state_1, _input_state_2, _joy_state_1, _joy_state_2;

static GLuint _gpu_ppu_state;

#define _apu_cpu_hz 1789773.0
#define _audio_sample_rate 44100
#define _audio_frame_rate 60
#define _audio_samples_per_frame (_audio_sample_rate / _audio_frame_rate)
#define _audio_buffer_count 8
#define _audio_preroll_buffers 2

typedef struct {
	uint8_t control;
	int start;
	int divider;
	int decay;
} Envelope;

typedef struct {
	Envelope envelope;
	uint8_t sweep;
	int enabled;
	int length;
	int timer;
	int sweep_divider;
	int sweep_reload;
	double phase;
} PulseChannel;

typedef struct {
	uint8_t linear;
	int enabled;
	int length;
	int timer;
	int linear_counter;
	int linear_reload;
	double phase;
} TriangleChannel;

typedef struct {
	Envelope envelope;
	uint8_t mode_period;
	int enabled;
	int length;
	uint16_t shift;
	double phase;
} NoiseChannel;

typedef struct {
	PulseChannel pulse[2];
	TriangleChannel triangle;
	NoiseChannel noise;
	int dmc_raw;
	double frame_phase;
	int frame_step;
	double highpass_in;
	double highpass_out;
} APUState;

static APUState _apu_state = {
	.noise.shift = 1,
};

static ALCdevice *_audio_device;
static ALCcontext *_audio_context;
static ALuint _audio_source;
static ALuint _audio_buffers[_audio_buffer_count];
static ALuint _audio_free_buffers[_audio_buffer_count];
static int _audio_free_count;
static int _audio_ready;
static int16_t _audio_mix_buffer[_audio_samples_per_frame];

static const int _length_table[32] = {
	10, 254, 20, 2, 40, 4, 80, 6,
	160, 8, 60, 10, 14, 12, 26, 14,
	12, 16, 24, 18, 48, 20, 96, 22,
	192, 24, 72, 26, 16, 28, 32, 30,
};

static void pulse_control_w(PulseChannel *pulse, uint8_t d) {
	pulse->envelope.control = d;
}

static void pulse_sweep_w(PulseChannel *pulse, uint8_t d) {
	pulse->sweep = d;
	pulse->sweep_reload = 1;
}

static void pulse_lo_w(PulseChannel *pulse, uint8_t d) {
	pulse->timer = (pulse->timer & 0x700) | d;
}

static void pulse_hi_w(PulseChannel *pulse, uint8_t d) {
	pulse->timer = (pulse->timer & 0x0ff) | ((d & 0x07) << 8);
	pulse->phase = 0.0;
	pulse->envelope.start = 1;
	if (pulse->enabled) {
		pulse->length = _length_table[d >> 3];
	}
}

void env_ppuctrl_w(uint8_t d) {
	_ppu_state.scroll &= 0xff;
	_ppu_state.scroll |= (d&1)<<8;
	_ppuaddr_inc = (d&4)? 32 : 1;
}

void env_ppumask_w(uint8_t d) {
	_render = (d&(8|16)) != 0;
}

void env_oamaddr_w(uint8_t d) { }

void env_ppuscroll_w(uint8_t d) {
	if (!_scnd_w) {
		_ppu_state.scroll &= ~0xff;
		_ppu_state.scroll |= d;
	}
	_ppuaddr = 0;
	_scnd_w ^= 1;
}

void env_ppuaddr_w(uint8_t d) {
	_ppu_state.scroll = 0;
	if (!_scnd_w) {
		_ppuaddr = d<<8;
	} else {
		_ppuaddr |= d;
	}
	_scnd_w ^= 1;
}

void env_ppudata_w(uint8_t d) {
	if (_ppuaddr >= 0x3f00) {
		int ea = _ppuaddr - 0x3f00;
		if (ea%4 == 0) { ea %= 16; }
		_ppu_state.palram[ea % 32] = d;
	} else {
		_ppu_state.ciram[_ppuaddr % 0x800] = d;
	}
	_ppuaddr += _ppuaddr_inc;
	_ppuaddr &= 0xffff;
}

void env_sq1_vol_w(uint8_t d) {
	pulse_control_w(&_apu_state.pulse[0], d);
}

void env_sq1_sweep_w(uint8_t d) {
	pulse_sweep_w(&_apu_state.pulse[0], d);
}

void env_sq1_lo_w(uint8_t d) {
	pulse_lo_w(&_apu_state.pulse[0], d);
}

void env_sq1_hi_w(uint8_t d) {
	pulse_hi_w(&_apu_state.pulse[0], d);
}

void env_sq2_vol_w(uint8_t d) {
	pulse_control_w(&_apu_state.pulse[1], d);
}

void env_sq2_sweep_w(uint8_t d) {
	pulse_sweep_w(&_apu_state.pulse[1], d);
}

void env_sq2_lo_w(uint8_t d) {
	pulse_lo_w(&_apu_state.pulse[1], d);
}

void env_sq2_hi_w(uint8_t d) {
	pulse_hi_w(&_apu_state.pulse[1], d);
}

void env_tri_linear_w(uint8_t d) {
	_apu_state.triangle.linear = d;
}

void env_tri_lo_w(uint8_t d) {
	_apu_state.triangle.timer = (_apu_state.triangle.timer & 0x700) | d;
}

void env_tri_hi_w(uint8_t d) {
	_apu_state.triangle.timer = (_apu_state.triangle.timer & 0x0ff) | ((d & 0x07) << 8);
	_apu_state.triangle.linear_reload = 1;
	if (_apu_state.triangle.enabled) {
		_apu_state.triangle.length = _length_table[d >> 3];
	}
}

void env_noise_vol_w(uint8_t d) {
	_apu_state.noise.envelope.control = d;
}

void env_noise_lo_w(uint8_t d) {
	_apu_state.noise.mode_period = d;
}

void env_noise_hi_w(uint8_t d) {
	_apu_state.noise.envelope.start = 1;
	if (_apu_state.noise.enabled) {
		_apu_state.noise.length = _length_table[d >> 3];
	}
}

void env_dmc_raw_w(uint8_t d) {
	_apu_state.dmc_raw = d & 0x7f;
}

void env_oamdma_w(uint8_t d) {
	memcpy(_ppu_state.oam, ram+0x200, 0x100);
}

void env_snd_chn_w(uint8_t d) {
	_apu_state.pulse[0].enabled = d & 0x01;
	_apu_state.pulse[1].enabled = d & 0x02;
	_apu_state.triangle.enabled = d & 0x04;
	_apu_state.noise.enabled = d & 0x08;
	
	if (!_apu_state.pulse[0].enabled) { _apu_state.pulse[0].length = 0; }
	if (!_apu_state.pulse[1].enabled) { _apu_state.pulse[1].length = 0; }
	if (!_apu_state.triangle.enabled) { _apu_state.triangle.length = 0; }
	if (!_apu_state.noise.enabled) { _apu_state.noise.length = 0; }
}

void env_joy1_w(uint8_t d) {
	_joy_state_1 = _input_state_1;
	_joy_state_2 = _input_state_2;
}

void env_joy2_w(uint8_t d) {
	
}

uint8_t env_ppustatus_r() {
	_scnd_w = 0;
	return 128;
}

uint8_t env_ppudata_r() {
	static const uint8_t chrrom_end[] = {
		0x20, 0xa6, 0x54, 0x26, 0x20, 0xc6, 0x54, 0x26, 0x20, 0xe6, 0x54, 0x26, 0x21, 0x06, 0x54, 0x26,
		0x20, 0x85, 0x01, 0x44, 0x20, 0x86, 0x54, 0x48, 0x20, 0x9a, 0x01, 0x49, 0x20, 0xa5, 0xc9, 0x46,
		0x20, 0xba, 0xc9, 0x4a, 0x20, 0xa6, 0x0a, 0xd0, 0xd1, 0xd8, 0xd8, 0xde, 0xd1, 0xd0, 0xda, 0xde,
		0xd1, 0x20, 0xc6, 0x0a, 0xd2, 0xd3, 0xdb, 0xdb, 0xdb, 0xd9, 0xdb, 0xdc, 0xdb, 0xdf, 0x20, 0xe6,
		0x0a, 0xd4, 0xd5, 0xd4, 0xd9, 0xdb, 0xe2, 0xd4, 0xda, 0xdb, 0xe0, 0x21, 0x06, 0x0a, 0xd6, 0xd7,
		0xd6, 0xd7, 0xe1, 0x26, 0xd6, 0xdd, 0xe1, 0xe1, 0x21, 0x26, 0x14, 0xd0, 0xe8, 0xd1, 0xd0, 0xd1,
		0xde, 0xd1, 0xd8, 0xd0, 0xd1, 0x26, 0xde, 0xd1, 0xde, 0xd1, 0xd0, 0xd1, 0xd0, 0xd1, 0x26, 0x21,
		0x46, 0x14, 0xdb, 0x42, 0x42, 0xdb, 0x42, 0xdb, 0x42, 0xdb, 0xdb, 0x42, 0x26, 0xdb, 0x42, 0xdb,
		0x42, 0xdb, 0x42, 0xdb, 0x42, 0x26, 0x21, 0x66, 0x46, 0xdb, 0x21, 0x6c, 0x0e, 0xdf, 0xdb, 0xdb,
		0xdb, 0x26, 0xdb, 0xdf, 0xdb, 0xdf, 0xdb, 0xdb, 0xe4, 0xe5, 0x26, 0x21, 0x86, 0x14, 0xdb, 0xdb,
		0xdb, 0xde, 0x43, 0xdb, 0xe0, 0xdb, 0xdb, 0xdb, 0x26, 0xdb, 0xe3, 0xdb, 0xe0, 0xdb, 0xdb, 0xe6,
		0xe3, 0x26, 0x21, 0xa6, 0x14, 0xdb, 0xdb, 0xdb, 0xdb, 0x42, 0xdb, 0xdb, 0xdb, 0xd4, 0xd9, 0x26,
		0xdb, 0xd9, 0xdb, 0xdb, 0xd4, 0xd9, 0xd4, 0xd9, 0xe7, 0x21, 0xc5, 0x16, 0x5f, 0x95, 0x95, 0x95,
		0x95, 0x95, 0x95, 0x95, 0x95, 0x97, 0x98, 0x78, 0x95, 0x96, 0x95, 0x95, 0x97, 0x98, 0x97, 0x98,
		0x95, 0x7a, 0x21, 0xed, 0x0e, 0xcf, 0x01, 0x09, 0x08, 0x05, 0x24, 0x17, 0x12, 0x17, 0x1d, 0x0e,
		0x17, 0x0d, 0x18, 0x22, 0x4b, 0x0d, 0x01, 0x24, 0x19, 0x15, 0x0a, 0x22, 0x0e, 0x1b, 0x24, 0x10,
		0x0a, 0x16, 0x0e, 0x22, 0x8b, 0x0d, 0x02, 0x24, 0x19, 0x15, 0x0a, 0x22, 0x0e, 0x1b, 0x24, 0x10,
		0x0a, 0x16, 0x0e, 0x22, 0xec, 0x04, 0x1d, 0x18, 0x19, 0x28, 0x22, 0xf6, 0x01, 0x00, 0x23, 0xc9,
		0x56, 0x55, 0x23, 0xe2, 0x04, 0x99, 0xaa, 0xaa, 0xaa, 0x23, 0xea, 0x04, 0x99, 0xaa, 0xaa, 0xaa,
		0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	};
	
	static uint8_t buf;
	uint8_t r = buf;
	if (_ppuaddr >= 0x1ec0 && _ppuaddr < 0x2000) {
		buf = chrrom_end[_ppuaddr - 0x1ec0];
	}
	_ppuaddr += _ppuaddr_inc;
	_ppuaddr &= 0xffff;
	return r;
}

uint8_t env_joy1_r() {
	uint8_t r = _joy_state_1 & 1;
	_joy_state_1 >>= 1;
	return r;
}

uint8_t env_joy2_r() {
	uint8_t r = _joy_state_2 & 1;
	_joy_state_2 >>= 1;
	return r;
}

static void wrap_phase(double *phase) {
	*phase -= floor(*phase);
}

static int envelope_output(Envelope const *envelope) {
	if (envelope->control & 0x10) {
		return envelope->control & 0x0f;
	}
	return envelope->decay;
}

static void clock_envelope(Envelope *envelope) {
	if (envelope->start) {
		envelope->start = 0;
		envelope->decay = 15;
		envelope->divider = envelope->control & 0x0f;
		return;
	}
	
	if (envelope->divider > 0) {
		envelope->divider--;
		return;
	}
	
	envelope->divider = envelope->control & 0x0f;
	if (envelope->decay > 0) {
		envelope->decay--;
	} else if (envelope->control & 0x20) {
		envelope->decay = 15;
	}
}

static void clock_length_counter(int *length, int halt) {
	if (*length > 0 && !halt) {
		(*length)--;
	}
}

static int pulse_sweep_target(PulseChannel const *pulse, int index) {
	int shift = pulse->sweep & 0x07;
	int change = pulse->timer >> shift;
	if (pulse->sweep & 0x08) {
		return pulse->timer - change - (index == 0);
	}
	return pulse->timer + change;
}

static int pulse_sweep_mutes(PulseChannel const *pulse, int index) {
	int shift = pulse->sweep & 0x07;
	if (pulse->timer < 8) {
		return 1;
	}
	if (shift == 0) {
		return 0;
	}
	int target = pulse_sweep_target(pulse, index);
	return target < 0 || target > 0x7ff;
}

static void clock_sweep(PulseChannel *pulse, int index) {
	int period = (pulse->sweep >> 4) & 0x07;
	if (pulse->sweep_divider == 0) {
		int shift = pulse->sweep & 0x07;
		if ((pulse->sweep & 0x80) && shift != 0 && !pulse_sweep_mutes(pulse, index)) {
			pulse->timer = pulse_sweep_target(pulse, index);
		}
		pulse->sweep_divider = period;
	} else {
		pulse->sweep_divider--;
	}
	
	if (pulse->sweep_reload) {
		pulse->sweep_reload = 0;
		pulse->sweep_divider = period;
	}
}

static void clock_triangle_linear() {
	TriangleChannel *triangle = &_apu_state.triangle;
	if (triangle->linear_reload) {
		triangle->linear_counter = triangle->linear & 0x7f;
	} else if (triangle->linear_counter > 0) {
		triangle->linear_counter--;
	}
	
	if (!(triangle->linear & 0x80)) {
		triangle->linear_reload = 0;
	}
}

static void clock_quarter_frame() {
	clock_envelope(&_apu_state.pulse[0].envelope);
	clock_envelope(&_apu_state.pulse[1].envelope);
	clock_envelope(&_apu_state.noise.envelope);
	clock_triangle_linear();
}

static void clock_half_frame() {
	clock_length_counter(&_apu_state.pulse[0].length, _apu_state.pulse[0].envelope.control & 0x20);
	clock_length_counter(&_apu_state.pulse[1].length, _apu_state.pulse[1].envelope.control & 0x20);
	clock_length_counter(&_apu_state.triangle.length, _apu_state.triangle.linear & 0x80);
	clock_length_counter(&_apu_state.noise.length, _apu_state.noise.envelope.control & 0x20);
	clock_sweep(&_apu_state.pulse[0], 0);
	clock_sweep(&_apu_state.pulse[1], 1);
}

static void clock_frame_counter() {
	_apu_state.frame_phase += 240.0 / _audio_sample_rate;
	while (_apu_state.frame_phase >= 1.0) {
		_apu_state.frame_phase -= 1.0;
		clock_quarter_frame();
		if (_apu_state.frame_step & 1) {
			clock_half_frame();
		}
		_apu_state.frame_step = (_apu_state.frame_step + 1) & 3;
	}
}

static int pulse_output(PulseChannel const *pulse, int index) {
	static const uint8_t duty_table[4][8] = {
		{ 0, 1, 0, 0, 0, 0, 0, 0 },
		{ 0, 1, 1, 0, 0, 0, 0, 0 },
		{ 0, 1, 1, 1, 1, 0, 0, 0 },
		{ 1, 0, 0, 1, 1, 1, 1, 1 },
	};
	
	if (!pulse->enabled || pulse->length == 0 || pulse_sweep_mutes(pulse, index)) {
		return 0;
	}
	
	int duty = pulse->envelope.control >> 6;
	int step = (int)(pulse->phase * 8.0) & 7;
	if (!duty_table[duty][step]) {
		return 0;
	}
	return envelope_output(&pulse->envelope);
}

static void advance_pulse(PulseChannel *pulse) {
	double frequency = _apu_cpu_hz / (16.0 * (pulse->timer + 1));
	pulse->phase += frequency / _audio_sample_rate;
	wrap_phase(&pulse->phase);
}

static int triangle_output(TriangleChannel const *triangle) {	
	static const uint8_t table[32] = {
		15, 14, 13, 12, 11, 10, 9, 8,
		7, 6, 5, 4, 3, 2, 1, 0,
		0, 1, 2, 3, 4, 5, 6, 7,
		8, 9, 10, 11, 12, 13, 14, 15,
	};
	
	if (!triangle->enabled || triangle->length == 0 || triangle->linear_counter == 0 || triangle->timer < 2) {
		return 0;
	}
	
	int step = (int)(triangle->phase * 32.0) & 31;
	return table[step];
}

static void advance_triangle(TriangleChannel *triangle) {
	if (triangle->timer < 2) {
		return;
	}
	
	double frequency = _apu_cpu_hz / (32.0 * (triangle->timer + 1));
	triangle->phase += frequency / _audio_sample_rate;
	wrap_phase(&triangle->phase);
}

static void clock_noise_shift() {
	NoiseChannel *noise = &_apu_state.noise;
	int tap = (noise->mode_period & 0x80)? 6 : 1;
	int feedback = (noise->shift ^ (noise->shift >> tap)) & 1;
	noise->shift = (noise->shift >> 1) | (feedback << 14);
	if (!noise->shift) {
		noise->shift = 1;
	}
}

static int noise_output(NoiseChannel const *noise) {
	if (!noise->enabled || noise->length == 0 || (noise->shift & 1)) {
		return 0;
	}
	return envelope_output(&noise->envelope);
}

static void advance_noise(NoiseChannel *noise) {
	static const int period_table[16] = {
		4, 8, 16, 32, 64, 96, 128, 160,
		202, 254, 380, 508, 762, 1016, 2034, 4068,
	};
	
	int period = period_table[noise->mode_period & 0x0f];
	noise->phase += (_apu_cpu_hz / period) / _audio_sample_rate;
	while (noise->phase >= 1.0) {
		noise->phase -= 1.0;
		clock_noise_shift();
	}
}

static double mix_apu(int pulse1, int pulse2, int triangle, int noise, int dmc) {
	int pulse_sum = pulse1 + pulse2;
	double pulse_out = pulse_sum? 95.88 / (8128.0 / pulse_sum + 100.0) : 0.0;
	
	int tnd_sum = 3 * triangle + 2 * noise + dmc;
	double tnd_out = tnd_sum? 163.67 / (24329.0 / tnd_sum + 100.0) : 0.0;
	return pulse_out + tnd_out;
}

static int16_t apu_sample() {
	int pulse1 = pulse_output(&_apu_state.pulse[0], 0);
	int pulse2 = pulse_output(&_apu_state.pulse[1], 1);
	int triangle = triangle_output(&_apu_state.triangle);
	int noise = noise_output(&_apu_state.noise);
	
	advance_pulse(&_apu_state.pulse[0]);
	advance_pulse(&_apu_state.pulse[1]);
	advance_triangle(&_apu_state.triangle);
	advance_noise(&_apu_state.noise);
	clock_frame_counter();
	
	double mixed = mix_apu(pulse1, pulse2, triangle, noise, _apu_state.dmc_raw);
	double sample = mixed - _apu_state.highpass_in + 0.996 * _apu_state.highpass_out;
	_apu_state.highpass_in = mixed;
	_apu_state.highpass_out = sample;
	
	int pcm = (int)lrint(sample * 30000.0);
	if (pcm > INT16_MAX) { return INT16_MAX; }
	if (pcm < INT16_MIN) { return INT16_MIN; }
	return (int16_t)pcm;
}

static void render_audio_frame(int16_t *samples) {
	for (int i = 0; i < _audio_samples_per_frame; i++) {
		samples[i] = apu_sample();
	}
}

static ALuint take_audio_buffer() {
	if (_audio_free_count == 0) {
		return 0;
	}
	return _audio_free_buffers[--_audio_free_count];
}

static void release_audio_buffer(ALuint buffer) {
	if (_audio_free_count < _audio_buffer_count) {
		_audio_free_buffers[_audio_free_count++] = buffer;
	}
}

static void queue_audio_buffer(ALuint buffer, int16_t const *samples) {
	alBufferData(buffer, AL_FORMAT_MONO16, samples, sizeof(int16_t) * _audio_samples_per_frame, _audio_sample_rate);
	alSourceQueueBuffers(_audio_source, 1, &buffer);
}

static void update_audio() {
	if (!_audio_ready) {
		render_audio_frame(_audio_mix_buffer);
		return;
	}
	
	ALint processed = 0;
	alGetSourcei(_audio_source, AL_BUFFERS_PROCESSED, &processed);
	while (processed-- > 0) {
		ALuint buffer = 0;
		alSourceUnqueueBuffers(_audio_source, 1, &buffer);
		release_audio_buffer(buffer);
	}
	
	render_audio_frame(_audio_mix_buffer);
	ALuint buffer = take_audio_buffer();
	if (buffer) {
		queue_audio_buffer(buffer, _audio_mix_buffer);
	}
	
	ALint queued = 0;
	ALint state = 0;
	alGetSourcei(_audio_source, AL_BUFFERS_QUEUED, &queued);
	alGetSourcei(_audio_source, AL_SOURCE_STATE, &state);
	if (queued > 0 && state != AL_PLAYING) {
		alSourcePlay(_audio_source);
	}
}

static void init_audio() {
	_audio_device = alcOpenDevice(NULL);
	if (!_audio_device) {
		puts("failed to open OpenAL device; continuing without audio");
		return;
	}
	
	_audio_context = alcCreateContext(_audio_device, NULL);
	if (!_audio_context || !alcMakeContextCurrent(_audio_context)) {
		puts("failed to create OpenAL context; continuing without audio");
		if (_audio_context) { alcDestroyContext(_audio_context); }
		alcCloseDevice(_audio_device);
		_audio_context = NULL;
		_audio_device = NULL;
		return;
	}
	
	alGenSources(1, &_audio_source);
	alGenBuffers(_audio_buffer_count, _audio_buffers);
	if (alGetError() != AL_NO_ERROR) {
		puts("failed to create OpenAL buffers; continuing without audio");
		return;
	}
	
	alSourcef(_audio_source, AL_GAIN, 0.85f);
	_audio_free_count = 0;
	for (int i = 0; i < _audio_buffer_count; i++) {
		release_audio_buffer(_audio_buffers[i]);
	}
	
	alSourcef(_audio_source, AL_PITCH, CFG_SPEED);
	
	int16_t silence[_audio_samples_per_frame] = { 0 };
	for (int i = 0; i < _audio_preroll_buffers; i++) {
		ALuint buffer = take_audio_buffer();
		if (buffer) {
			queue_audio_buffer(buffer, silence);
		}
	}
	
	_audio_ready = 1;
	alSourcePlay(_audio_source);
}

static void update_viewport() {
	int win_w, win_h;
	SDL_GetWindowSize(_window, &win_w, &win_h);
	
	float sx = win_w/256.0f, sy = win_h/240.0f;
	float s = (sx<sy)? sx : sy;
	
	int vp_w = 256 * s, vp_h = 240 * s;
	int vp_x = (win_w - vp_w) / 2;
	int vp_y = (win_h - vp_h) / 2;
	glViewport(vp_x, vp_y, vp_w, vp_h);
}

void env_update() {
	update_audio();
	
	update_viewport();
	
	if (_render) {
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(PPUState), &_ppu_state);
		glDrawArrays(GL_TRIANGLES, 0, 6);
	} else {
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}
	
	SDL_GL_SwapWindow(_window);
	
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		if (e.type == SDL_EVENT_QUIT) {
			exit(EXIT_SUCCESS);
		}
	}
	
	bool const *keys = SDL_GetKeyboardState(NULL);
	
	bool a = keys[SDL_SCANCODE_Z],
		b = keys[SDL_SCANCODE_X],
		select = keys[SDL_SCANCODE_RSHIFT],
		start = keys[SDL_SCANCODE_RETURN],
		up = keys[SDL_SCANCODE_UP],
		down = keys[SDL_SCANCODE_DOWN],
		left = keys[SDL_SCANCODE_LEFT],
		right = keys[SDL_SCANCODE_RIGHT];
	
	for (int i = 0; i < _num_gamepads; i++) {
		SDL_Gamepad *gamepad = _gamepads[i];
		
		if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH) |
			SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_NORTH)) {
			
			a = true;
		}
		
		if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST) |
			SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_WEST)) {
			
			b = true;
		}
		
		if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_BACK)) {
			select = true;
		}
		
		if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START)) {
			start = true;
		}
		
		if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP) ||
			SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY) < -32767/3) {
			
			up = true;
		}
		
		if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) ||
			SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY) > 32767/3) {
			
			down = true;
		}
		
		if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) ||
			SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX) < -32767/3) {
			
			left = true;
		}
		
		if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ||
			SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX) > 32767/3) {
			
			right = true;
		}
	}
	
	_input_state_1 =
		a | (b<<1) | (select<<2) | (start<<3) | (up<<4) | (down<<5) |
		(left<<6) | (right<<7);
	_input_state_2 = _input_state_1;
	
	frame++;
}

#define show_sdl_error(msg)\
	puts(msg);\
	puts(SDL_GetError());

static void init_sdl() {
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
		show_sdl_error("failed to initialise SDL:");
		goto error;
	}
	
	_window = SDL_CreateWindow("", 3*256, 3*240, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
	if (!_window) {
		show_sdl_error("failed to create SDL window:");
		goto error;
	}
	
	if (!SDL_GL_CreateContext(_window)) {
		show_sdl_error("failed to create SDL OpenGL context:");
		goto error;
	}
	SDL_GL_SetSwapInterval(1);
	
	int num_joysticks;
	SDL_JoystickID *joysticks = SDL_GetJoysticks(&num_joysticks);
	for (int i = 0; i < num_joysticks && _num_gamepads < 16; i++) {
		if (!SDL_IsGamepad(joysticks[i])) { continue; }
		_gamepads[_num_gamepads++] = SDL_OpenGamepad(joysticks[i]);
	}
	SDL_free(joysticks);
	
	return;
	
	error: exit(EXIT_FAILURE);
}

static GLuint create_shader(GLenum type, char const *src) {
	GLuint sh = glCreateShader(type);
	glShaderSource(sh, 1, &src, NULL);
	glCompileShader(sh);
	
	GLint compiled;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &compiled);
	if (!compiled) {
		GLint log_len;
		glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &log_len);
		
		char *log = malloc(log_len + 1);
		glGetShaderInfoLog(sh, log_len, NULL, log);
		log[log_len] = 0;
		
		puts("failed to compile shader:");
		puts(log);
		exit(EXIT_FAILURE);
	}
	
	return sh;
}

static char vsh_src[] = {
	#embed "shaders/vert.glsl"
	, 0
};
static char fsh_src[] = {
	#embed "shaders/frag.glsl"
	, 0
};

static void init_gl() {
	if (!gladLoadGLLoader((void*(*)(char const*))SDL_GL_GetProcAddress)) {
		puts("failed to load OpenGL");
		exit(EXIT_FAILURE);
	}

	GLuint vsh = create_shader(GL_VERTEX_SHADER, vsh_src);
	GLuint fsh = create_shader(GL_FRAGMENT_SHADER, fsh_src);
	
	GLuint shp = glCreateProgram();
	glAttachShader(shp, vsh);
	glAttachShader(shp, fsh);
	glLinkProgram(shp);
	
	GLint linked;
	glGetProgramiv(shp, GL_LINK_STATUS, &linked);
	if (!linked) {
		GLint log_len;
		glGetProgramiv(shp, GL_INFO_LOG_LENGTH, &log_len);

		char log[log_len + 1];
		glGetProgramInfoLog(shp, log_len, NULL, log);
		log[log_len] = 0;

		puts("failed to link shader program:");
		puts(log);
		exit(EXIT_FAILURE);
	}
	
	glGenBuffers(1, &_gpu_ppu_state);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, _gpu_ppu_state);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(PPUState), NULL, GL_STATIC_DRAW);
	
	GLuint vao;
	glCreateVertexArrays(1, &vao);
	
	glUseProgram(shp);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, _gpu_ppu_state);
	glBindVertexArray(vao);
}

void env_init() {
	init_sdl();
	init_gl();
	init_audio();
}
