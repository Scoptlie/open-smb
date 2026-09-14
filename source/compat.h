#pragma once

#include <stdint.h>

extern uint8_t ram[0x800];

extern uint8_t rom[0x8000];

static uint8_t mem_r(uint16_t addr) {
	if (addr & 0x8000) {
		return rom[addr & 0x7fff];
	} else {
		return ram[addr & 0x7ff];
	}
}

static void mem_w(uint16_t addr, uint8_t d) {
	if (addr & 0x8000) {
		rom[addr & 0x7fff] = d;
	} else {
		ram[addr & 0x7ff] = d;
	}
}

extern uint8_t reg_a, reg_x, reg_y, reg_s;

typedef struct { uint8_t
	c : 1,
	z : 1,
	i : 1,
	d : 1,
	b : 1,
	one : 1,
	v : 1,
	n : 1;
} RegP;

extern RegP reg_p;

static void set_nz(uint8_t d) {
	reg_p.n = d>>7;
	reg_p.z = !d;
}

static void set_a(uint8_t d) {
	reg_a = d;
	set_nz(reg_a);
}

static void and_a(uint8_t d) {
	set_a(reg_a & d);
}

static void or_a(uint8_t d) {
	set_a(reg_a | d);
}

static void eor_a(uint8_t d) {
	set_a(reg_a ^ d);
}

static void add_a(uint8_t d) {
	int r = reg_a + d + reg_p.c;
	reg_p.c = r > 255;
	reg_p.v = ((~(reg_a ^ d) & (r ^ d)) >> 7) & 1;
	set_a(r);
}

static void sub_a(uint8_t d) {
	add_a(~d);
}

static void cmp_a(uint8_t d) {
	set_nz(reg_a-d);
	reg_p.c = reg_a >= d;
}

static void bit_a(uint8_t d) {
	reg_p.n = d>>7;
	reg_p.v = (d>>6)&1;
	reg_p.z = !(reg_a&d);
}
	
static void set_x(uint8_t d) {
	reg_x = d;
	set_nz(reg_x);
}

static void cmp_x(uint8_t d) {
	set_nz(reg_x - d);
	reg_p.c = reg_x >= d;
}

static void set_y(uint8_t d) {
	reg_y = d;
	set_nz(reg_y);
}

static void cmp_y(uint8_t d) {
	set_nz(reg_y - d);
	reg_p.c = reg_y >= d;
}

static uint8_t inc(uint8_t d) {
	set_nz(d + 1);
	return d + 1;
}

static uint8_t dec(uint8_t d) {
	set_nz(d - 1);
	return d - 1;
}

static uint8_t shl(uint8_t d) {
	uint8_t r = d << 1;
	reg_p.c = d >> 7;
	set_nz(r);
	return r;
}

static uint8_t shr(uint8_t d) {
	uint8_t r = d >> 1;
	reg_p.c = d & 1;
	set_nz(r);
	return r;
}

static uint8_t rol(uint8_t d) {
	uint8_t r = (d<<1) | reg_p.c;
	reg_p.c = d >> 7;
	set_nz(r);
	return r;
}

static uint8_t ror(uint8_t d) {
	uint8_t r = (d>>1) | (reg_p.c<<7);
	reg_p.c = d & 1;
	set_nz(r);
	return r;
}

static void push(uint8_t d) {
	ram[0x100 + reg_s--] = d;
}

static uint8_t pull() {
	return ram[0x100 + ++reg_s];
}
