
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "cfg.h"
#include "compat.h"
#include "env.h"
#include "smb.h"

int main(int argc, char **argv) {
	env_init();
	smb_start();
	
	while (true) {
		env_update();
		smb_non_maskable_interrupt();
	}
	return 0;
}
