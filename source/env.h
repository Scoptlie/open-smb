#pragma once

#include <stdint.h>

extern int frame;

void env_ppuctrl_w(uint8_t d);
void env_ppumask_w(uint8_t d);
void env_oamaddr_w(uint8_t d);
void env_ppuscroll_w(uint8_t d);
void env_ppuaddr_w(uint8_t d);
void env_ppudata_w(uint8_t d);
void env_sq1_vol_w(uint8_t d);
void env_sq1_sweep_w(uint8_t d);
void env_sq1_lo_w(uint8_t d);
void env_sq1_hi_w(uint8_t d);
void env_sq2_vol_w(uint8_t d);
void env_sq2_sweep_w(uint8_t d);
void env_sq2_lo_w(uint8_t d);
void env_sq2_hi_w(uint8_t d);
void env_tri_linear_w(uint8_t d);
void env_tri_lo_w(uint8_t d);
void env_tri_hi_w(uint8_t d);
void env_noise_vol_w(uint8_t d);
void env_noise_lo_w(uint8_t d);
void env_noise_hi_w(uint8_t d);
void env_dmc_raw_w(uint8_t d);
void env_oamdma_w(uint8_t d);
void env_snd_chn_w(uint8_t d);
void env_joy1_w(uint8_t d);
void env_joy2_w(uint8_t d);

uint8_t env_ppustatus_r();
uint8_t env_ppudata_r();
uint8_t env_joy1_r();
uint8_t env_joy2_r();

void env_update();
void env_init();
