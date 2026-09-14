#include "smb.h"

void smb_start() {
	reg_p.i = 1; // pretty standard 6502 type init here
	reg_p.d = 0;
	set_a(0b00010000); // init PPU control register 1
	env_ppuctrl_w(reg_a);
	set_x(0xff); // reset stack pointer
	reg_s = reg_x;
v_blank_1:;
	set_a(env_ppustatus_r()); // wait two frames
	if (!reg_p.n) { goto v_blank_1; }
v_blank_2:;
	set_a(env_ppustatus_r());
	if (!reg_p.n) { goto v_blank_2; }
	set_y((smb_cold_boot_offset & 0xff)); // load default cold boot pointer
	set_x(0x05); // this is where we check for a warm boot
w_boot_check:;
	set_a(ram[smb_top_score_display + reg_x]); // check each score digit in the top score
	cmp_a((10)); // to see if we have a valid digit
	if (reg_p.c) { goto cold_boot; } // if not, give up and proceed with cold boot
	set_x(reg_x-1);
	if (!reg_p.n) { goto w_boot_check; }
	set_a(ram[smb_warm_boot_validation]); // second checkpoint, check to see if
	cmp_a(0xa5); // another location has a specific value
	if (!reg_p.z) { goto cold_boot; }
	set_y((smb_warm_boot_offset & 0xff)); // if passed both, load warm boot pointer
cold_boot:;
	smb_initialize_memory(); // clear memory using pointer in Y
	env_dmc_raw_w(reg_a); // reset delta counter load register
	ram[smb_oper_mode] = reg_a; // reset primary mode of operation
	set_a(0xa5); // set warm boot flag
	ram[smb_warm_boot_validation] = reg_a;
	ram[smb_pseudo_random_bit_reg] = reg_a; // set seed for pseudorandom register
	set_a(0b00001111);
	env_snd_chn_w(reg_a); // enable all sound channels except dmc
	set_a(0b00000110);
	env_ppumask_w(reg_a); // turn off clipping for OAM and background
	smb_move_all_sprites_offscreen();
	smb_initialize_name_tables(); // initialize both name tables
	ram[smb_disable_screen_flag] = inc(ram[smb_disable_screen_flag]); // set flag to disable screen output
	set_a(ram[smb_mirror_ppu_ctrl_reg_1]);
	or_a(0b10000000); // enable NMIs
	smb_write_ppu_reg_1();
//endless_loop:;
//	goto endless_loop; // endless loop, need I say more?
}

void smb_non_maskable_interrupt() {
	set_a(ram[smb_mirror_ppu_ctrl_reg_1]); // disable NMIs in mirror reg
	and_a(0b01111111); // save all other bits
	ram[smb_mirror_ppu_ctrl_reg_1] = reg_a;
	and_a(0b01111110); // alter name table address to be $2800
	env_ppuctrl_w(reg_a); // (essentially $2000) but save other bits
	set_a(ram[smb_mirror_ppu_ctrl_reg_2]); // disable OAM and background display by default
	and_a(0b11100110);
	set_y(ram[smb_disable_screen_flag]); // get screen disable flag
	if (!reg_p.z) { goto screen_off; } // if set, used bits as-is
	set_a(ram[smb_mirror_ppu_ctrl_reg_2]); // otherwise reenable bits and save them
	or_a(0b00011110);
screen_off:;
	ram[smb_mirror_ppu_ctrl_reg_2] = reg_a; // save bits for later but not in register at the moment
	and_a(0b11100111); // disable screen for now
	env_ppumask_w(reg_a);
	set_x(env_ppustatus_r()); // reset flip-flop and reset scroll registers to zero
	set_a(0x00);
	smb_init_scroll();
	env_oamaddr_w(reg_a); // reset spr-ram address register
	set_a(0x02); // perform spr-ram DMA access on $0200-$02ff
	env_oamdma_w(reg_a);
	set_x(ram[smb_vram_buffer_addr_ctrl]); // load control for pointer to buffer contents
	set_a(rom[smb_vram_addr_table_low + reg_x]); // set indirect at $00 to pointer
	ram[0x0000] = reg_a;
	set_a(rom[smb_vram_addr_table_high + reg_x]);
	ram[0x0001] = reg_a;
	smb_update_screen(); // update screen with buffer contents
	set_y(0x00);
	set_x(ram[smb_vram_buffer_addr_ctrl]); // check for usage of $0341
	cmp_x(0x06);
	if (!reg_p.z) { goto init_buffer; }
	set_y(reg_y+1); // get offset based on usage
init_buffer:;
	set_x(rom[smb_vram_buffer_offset + reg_y]);
	set_a(0x00); // clear buffer header at last location
	ram[smb_vram_buffer_1_offset + reg_x] = reg_a;
	ram[smb_vram_buffer_1 + reg_x] = reg_a;
	ram[smb_vram_buffer_addr_ctrl] = reg_a; // reinit address control to $0301
	set_a(ram[smb_mirror_ppu_ctrl_reg_2]); // copy mirror of $2001 to register
	env_ppumask_w(reg_a);
	smb_sound_engine(); // play sound
	smb_read_joypads(); // read joypads
	smb_pause_routine(); // handle pause
	smb_update_top_score();
	set_a(ram[smb_game_pause_status]); // check for pause status
	reg_a = shr(reg_a);
	if (reg_p.c) { goto pause_skip; }
	set_a(ram[smb_timer_control]); // if master timer control not set, decrement
	if (reg_p.z) { goto dec_timers; } // all frame and interval timers
	ram[smb_timer_control] = dec(ram[smb_timer_control]);
	if (!reg_p.z) { goto no_dec_timers; }
dec_timers:;
	set_x(0x14); // load end offset for end of frame timers
	ram[smb_interval_timer_control] = dec(ram[smb_interval_timer_control]); // decrement interval timer control,
	if (!reg_p.n) { goto dec_timers_loop; } // if not expired, only frame timers will decrement
	set_a(0x14);
	ram[smb_interval_timer_control] = reg_a; // if control for interval timers expired,
	set_x(0x23); // interval timers will decrement along with frame timers
dec_timers_loop:;
	set_a(ram[smb_select_timer + reg_x]); // check current timer
	if (reg_p.z) { goto skip_exp_timer; } // if current timer expired, branch to skip,
	ram[smb_select_timer + reg_x] = dec(ram[smb_select_timer + reg_x]); // otherwise decrement the current timer
skip_exp_timer:;
	set_x(reg_x-1); // move onto next timer
	if (!reg_p.n) { goto dec_timers_loop; } // do this until all timers are dealt with
no_dec_timers:;
	ram[smb_frame_counter] = inc(ram[smb_frame_counter]); // increment frame counter
pause_skip:;
	set_x(0x00);
	set_y(0x07);
	set_a(ram[smb_pseudo_random_bit_reg]); // get first memory locaion of LSFR bytes
	and_a(0b00000010); // mask out all but d1
	ram[0x0000] = reg_a; // save here
	set_a(ram[smb_pseudo_random_bit_reg+1]); // get second memory location
	and_a(0b00000010); // mask out all but d1
	eor_a(ram[0x0000]); // perform exclusive-OR on d1 from first and second bytes
	reg_p.c = 0; // if neither or both are set, carry will be clear
	if (reg_p.z) { goto rot_p_random_bit; }
	reg_p.c = 1; // if one or the other is set, carry will be set
rot_p_random_bit:;
	ram[smb_pseudo_random_bit_reg + reg_x] = ror(ram[smb_pseudo_random_bit_reg + reg_x]); // rotate carry into d7, and rotate last bit into carry
	set_x(reg_x+1); // increment to next byte
	set_y(reg_y-1); // decrement for loop
	if (!reg_p.z) { goto rot_p_random_bit; }
	set_a(ram[smb_sprite_0_hit_detect_flag]); // check for flag here
	if (reg_p.z) { goto skip_sprite_0; }
sprite_0_clr:;
	set_a(env_ppustatus_r()); // wait for sprite 0 flag to clear, which will
	and_a(0b01000000); // not happen until vblank has ended
	if (!reg_p.z) { goto sprite_0_clr; }
	set_a(ram[smb_game_pause_status]); // if in pause mode, do not bother with sprites at all
	reg_a = shr(reg_a);
	if (reg_p.c) { goto sprite_0_hit; }
	smb_move_sprites_except_0_offscreen();
	smb_sprite_shuffler();
sprite_0_hit:;
//	set_a(env_ppustatus_r()); // do sprite #0 hit detection
//	and_a(0b01000000);
//	if (reg_p.z) { goto sprite_0_hit; }
	set_y(0x14); // small delay, to wait until we hit orizontal blank time
h_blank_delay:;
	set_y(reg_y-1);
	if (!reg_p.z) { goto h_blank_delay; }
skip_sprite_0:;
	set_a(ram[smb_horizontal_scroll]); // set scroll registers from variables
	env_ppuscroll_w(reg_a);
	set_a(ram[smb_vertical_scroll]);
	env_ppuscroll_w(reg_a);
	set_a(ram[smb_mirror_ppu_ctrl_reg_1]); // load saved mirror of $2000
	push(reg_a);
	env_ppuctrl_w(reg_a);
	set_a(ram[smb_game_pause_status]); // if in pause mode, do not perform operation mode stuff
	reg_a = shr(reg_a);
	if (reg_p.c) { goto skip_main_oper; }
	smb_oper_mode_execution_tree(); // otherwise do one of many, many possible subroutines
skip_main_oper:;
	set_a(env_ppustatus_r()); // reset flip-flip
	set_a(pull());
	or_a(0b10000000); // reactivate NMIs
	env_ppuctrl_w(reg_a);
	return; // we are done until the next frame!
	smb_pause_routine(); return;
}

void smb_pause_routine() {
	set_a(ram[smb_oper_mode]); // are we in victory mode?
	cmp_a((smb_victory_mode_value)); // if so, go ahead
	if (reg_p.z) { goto chk_pause_timer; }
	cmp_a((smb_game_mode_value)); // are we in game mode?
	if (!reg_p.z) { goto exit_pause; } // if not, leave
	set_a(ram[smb_oper_mode_task]); // if we are in game mode, are we running game engine?
	cmp_a(0x03);
	if (!reg_p.z) { goto exit_pause; } // if not, leave
chk_pause_timer:;
	set_a(ram[smb_game_pause_timer]); // check if pause timer is still counting down
	if (reg_p.z) { goto chk_start; }
	ram[smb_game_pause_timer] = dec(ram[smb_game_pause_timer]); // if so, decrement and leave
	return;
chk_start:;
	set_a(ram[smb_saved_joypad_1_bits]); // check to see if start is pressed
	and_a((smb_start_button)); // on controller 1
	if (reg_p.z) { goto clr_pause_timer; }
	set_a(ram[smb_game_pause_status]); // check to see if timer flag is set
	and_a(0b10000000); // and if so, do not reset timer (residual,
	if (!reg_p.z) { goto exit_pause; } // joypad reading routine makes this unnecessary)
	set_a(0x2b); // set pause timer
	ram[smb_game_pause_timer] = reg_a;
	set_a(ram[smb_game_pause_status]);
	set_y(reg_a);
	set_y(reg_y+1); // set pause sfx queue for next pause mode
	ram[smb_pause_sound_queue] = reg_y;
	eor_a(0b00000001); // invert d0 and set d7
	or_a(0b10000000);
	if (!reg_p.z) { goto set_pause; } // unconditional branch
clr_pause_timer:;
	set_a(ram[smb_game_pause_status]); // clear timer flag if timer is at zero and start button
	and_a(0b01111111); // is not pressed
set_pause:;
	ram[smb_game_pause_status] = reg_a;
exit_pause:;
	return;
	smb_sprite_shuffler(); return;
}

void smb_sprite_shuffler() {
	set_y(ram[smb_area_type]); // load level type, likely residual code
	set_a(0x28); // load preset value which will put it at
	ram[0x0000] = reg_a; // sprite #10
	set_x(0x0e); // start at the end of OAM data offsets
shuffle_loop:;
	set_a(ram[smb_spr_data_offset + reg_x]); // check for offset value against
	cmp_a(ram[0x0000]); // the preset value
	if (!reg_p.c) { goto next_spr_offset; } // if less, skip this part
	set_y(ram[smb_spr_shuffle_amt_offset]); // get current offset to preset value we want to add
	reg_p.c = 0;
	add_a(ram[smb_spr_shuffle_amt + reg_y]); // get shuffle amount, add to current sprite offset
	if (!reg_p.c) { goto str_spr_offset; } // if not exceeded $ff, skip second add
	reg_p.c = 0;
	add_a(ram[0x0000]); // otherwise add preset value $28 to offset
str_spr_offset:;
	ram[smb_spr_data_offset + reg_x] = reg_a; // store new offset here or old one if branched to here
next_spr_offset:;
	set_x(reg_x-1); // move backwards to next one
	if (!reg_p.n) { goto shuffle_loop; }
	set_x(ram[smb_spr_shuffle_amt_offset]); // load offset
	set_x(reg_x+1);
	cmp_x(0x03); // check if offset + 1 goes to 3
	if (!reg_p.z) { goto set_amt_offset; } // if offset + 1 not 3, store
	set_x(0x00); // otherwise, init to 0
set_amt_offset:;
	ram[smb_spr_shuffle_amt_offset] = reg_x;
	set_x(0x08); // load offsets for values and storage
	set_y(0x02);
set_misc_offset:;
	set_a(ram[smb_spr_data_offset+5 + reg_y]); // load one of three OAM data offsets
	ram[smb_misc_spr_data_offset-2 + reg_x] = reg_a; // store first one unmodified, but
	reg_p.c = 0; // add eight to the second and eight
	add_a(0x08); // more to the third one
	ram[smb_misc_spr_data_offset-1 + reg_x] = reg_a; // note that due to the way X is set up,
	reg_p.c = 0; // this code loads into the misc sprite offsets
	add_a(0x08);
	ram[smb_misc_spr_data_offset + reg_x] = reg_a;
	set_x(reg_x-1);
	set_x(reg_x-1);
	set_x(reg_x-1);
	set_y(reg_y-1);
	if (!reg_p.n) { goto set_misc_offset; } // do this until all misc spr offsets are loaded
	return;
	smb_oper_mode_execution_tree(); return;
}

void smb_oper_mode_execution_tree() {
	set_a(ram[smb_oper_mode]);
	static void(*targets[])() = {
		smb_title_screen_mode,
		smb_game_mode,
		smb_victory_mode,
		smb_game_over_mode
	};
	targets[reg_a](); return;
	smb_move_all_sprites_offscreen(); return;
}

void smb_move_all_sprites_offscreen() {
	set_y(0x00); // this routine moves all sprites off the screen
	smb_move_sprites_offscreen(); return;
}

void smb_move_sprites_except_0_offscreen() {
	set_y(0x04); // this routine moves all but sprite 0
	smb_move_sprites_offscreen(); return;
}

void smb_move_sprites_offscreen() {
	set_a(0xf8); // off the screen
spr_init_loop:;
	ram[smb_sprite_y_position + reg_y] = reg_a; // write 248 into OAM data's Y coordinate
	set_y(reg_y+1); // which will move it off the screen
	set_y(reg_y+1);
	set_y(reg_y+1);
	set_y(reg_y+1);
	if (!reg_p.z) { goto spr_init_loop; }
	return;
	smb_title_screen_mode(); return;
}

void smb_title_screen_mode() {
	set_a(ram[smb_oper_mode_task]);
	static void(*targets[])() = {
		smb_initialize_game,
		smb_screen_routines,
		smb_primary_game_setup,
		smb_game_menu_routine
	};
	targets[reg_a](); return;
}

void smb_game_menu_routine() {
	set_y(0x00);
	set_a(ram[smb_saved_joypad_1_bits]); // check to see if either player pressed
	or_a(ram[smb_saved_joypad_2_bits]); // only the start button (either joypad)
	cmp_a((smb_start_button));
	if (reg_p.z) { goto start_game; }
	cmp_a(0x90); // ||check to see if A + start was pressed
	if (!reg_p.z) { goto chk_select; } // if not, branch to check select button
start_game:;
	goto chk_continue; // if either start or A + start, execute here
chk_select:;
	cmp_a((smb_select_button)); // check to see if the select button was pressed
	if (reg_p.z) { goto select_b_logic; } // if so, branch reset demo timer
	set_x(ram[smb_demo_timer]); // otherwise check demo timer
	if (!reg_p.z) { goto chk_world_sel; } // if demo timer not expired, branch to check world selection
	ram[smb_select_timer] = reg_a; // set controller bits here if running demo
	smb_demo_engine(); // run through the demo actions
	if (reg_p.c) { goto reset_title; } // if carry flag set, demo over, thus branch
	goto run_demo; // otherwise, run game engine for demo
chk_world_sel:;
	set_x(ram[smb_world_select_enable_flag]); // check to see if world selection has been enabled
	if (reg_p.z) { goto null_joypad; }
	cmp_a((smb_b_button)); // if so, check to see if the B button was pressed
	if (!reg_p.z) { goto null_joypad; }
	set_y(reg_y+1); // if so, increment Y and execute same code as select
select_b_logic:;
	set_a(ram[smb_demo_timer]); // if select or B pressed, check demo timer one last time
	if (reg_p.z) { goto reset_title; } // if demo timer expired, branch to reset title screen mode
	set_a(0x18); // otherwise reset demo timer
	ram[smb_demo_timer] = reg_a;
	set_a(ram[smb_select_timer]); // check select/B button timer
	if (!reg_p.z) { goto null_joypad; } // if not expired, branch
	set_a(0x10); // otherwise reset select button timer
	ram[smb_select_timer] = reg_a;
	cmp_y(0x01); // was the B button pressed earlier?  if so, branch
	if (reg_p.z) { goto inc_world_sel; } // note this will not be run if world selection is disabled
	set_a(ram[smb_number_of_players]); // if no, must have been the select button, therefore
	eor_a(0b00000001); // change number of players and draw icon accordingly
	ram[smb_number_of_players] = reg_a;
	smb_draw_mushroom_icon();
	goto null_joypad;
inc_world_sel:;
	set_x(ram[smb_world_select_number]); // increment world select number
	set_x(reg_x+1);
	set_a(reg_x);
	and_a(0b00000111); // mask out higher bits
	ram[smb_world_select_number] = reg_a; // store as current world select number
	smb_go_continue();
update_shroom:;
	set_a(rom[smb_w_select_buffer_template + reg_x]); // write template for world select in vram buffer
	ram[smb_vram_buffer_1-1 + reg_x] = reg_a; // do this until all bytes are written
	set_x(reg_x+1);
	cmp_x(0x06);
	if (reg_p.n) { goto update_shroom; }
	set_y(ram[smb_world_number]); // get world number from variable and increment for
	set_y(reg_y+1); // proper display, and put in blank byte before
	ram[smb_vram_buffer_1+3] = reg_y; // null terminator
null_joypad:;
	set_a(0x00); // clear joypad bits for player 1
	ram[smb_saved_joypad_1_bits] = reg_a;
run_demo:;
	smb_game_core_routine(); // run game engine
	set_a(ram[smb_game_engine_subroutine]); // check to see if we're running lose life routine
	cmp_a(0x06);
	if (!reg_p.z) { goto exit_menu; } // if not, do not do all the resetting below
reset_title:;
	set_a(0x00); // reset game modes, disable
	ram[smb_oper_mode] = reg_a; // sprite 0 check and disable
	ram[smb_oper_mode_task] = reg_a; // screen output
	ram[smb_sprite_0_hit_detect_flag] = reg_a;
	ram[smb_disable_screen_flag] = inc(ram[smb_disable_screen_flag]);
	return;
chk_continue:;
	set_y(ram[smb_demo_timer]); // if timer for demo has expired, reset modes
	if (reg_p.z) { goto reset_title; }
	reg_a = shl(reg_a); // check to see if A button was also pushed
	if (!reg_p.c) { goto start_world_1; } // if not, don't load continue function's world number
	set_a(ram[smb_continue_world]); // load previously saved world number for secret
	smb_go_continue(); // continue function when pressing A + start
start_world_1:;
	smb_load_area_pointer();
	ram[smb_hidden_1_up_flag] = inc(ram[smb_hidden_1_up_flag]); // set 1-up box flag for both players
	ram[smb_off_scr_hidden_1_up_flag] = inc(ram[smb_off_scr_hidden_1_up_flag]);
	ram[smb_fetch_new_game_timer_flag] = inc(ram[smb_fetch_new_game_timer_flag]); // set fetch new game timer flag
	ram[smb_oper_mode] = inc(ram[smb_oper_mode]); // set next game mode
	set_a(ram[smb_world_select_enable_flag]); // if world select flag is on, then primary
	ram[smb_primary_hard_mode] = reg_a; // hard mode must be on as well
	set_a(0x00);
	ram[smb_oper_mode_task] = reg_a; // set game mode here, and clear demo timer
	ram[smb_demo_timer] = reg_a;
	set_x(0x17);
	set_a(0x00);
init_scores:;
	ram[smb_score_and_coin_display + reg_x] = reg_a; // clear player scores and coin displays
	set_x(reg_x-1);
	if (!reg_p.n) { goto init_scores; }
exit_menu:;
	return;
	smb_go_continue(); return;
}

void smb_go_continue() {
	ram[smb_world_number] = reg_a; // start both players at the first area
	ram[smb_off_scr_world_number] = reg_a; // of the previously saved world number
	set_x(0x00); // note that on power-up using this function
	ram[smb_area_number] = reg_x; // will make no difference
	ram[smb_off_scr_area_number] = reg_x;
	return;
}

void smb_draw_mushroom_icon() {
	set_y(0x07); // read eight bytes to be read by transfer routine
icon_data_read:;
	set_a(rom[smb_mushroom_icon_data + reg_y]); // note that the default position is set for a
	ram[smb_vram_buffer_1-1 + reg_y] = reg_a; // 1-player game
	set_y(reg_y-1);
	if (!reg_p.n) { goto icon_data_read; }
	set_a(ram[smb_number_of_players]); // check number of players
	if (reg_p.z) { goto exit_icon; } // if set to 1-player game, we're done
	set_a(0x24); // otherwise, load blank tile in 1-player position
	ram[smb_vram_buffer_1+3] = reg_a;
	set_a(0xce); // then load shroom icon tile in 2-player position
	ram[smb_vram_buffer_1+5] = reg_a;
exit_icon:;
	return;
}

void smb_demo_engine() {
	set_x(ram[smb_demo_action]); // load current demo action
	set_a(ram[smb_demo_action_timer]); // load current action timer
	if (!reg_p.z) { goto do_action; } // if timer still counting down, skip
	set_x(reg_x+1);
	ram[smb_demo_action] = inc(ram[smb_demo_action]); // if expired, increment action, X, and
	reg_p.c = 1; // set carry by default for demo over
	set_a(rom[smb_demo_timing_data-1 + reg_x]); // get next timer
	ram[smb_demo_action_timer] = reg_a; // store as current timer
	if (reg_p.z) { goto demo_over; } // if timer already at zero, skip
do_action:;
	set_a(rom[smb_demo_action_data-1 + reg_x]); // get and perform action (current or next)
	ram[smb_saved_joypad_1_bits] = reg_a;
	ram[smb_demo_action_timer] = dec(ram[smb_demo_action_timer]); // decrement action timer
	reg_p.c = 0; // clear carry if demo still going
demo_over:;
	return;
	smb_victory_mode(); return;
}

void smb_victory_mode() {
	smb_victory_mode_subroutines(); // run victory mode subroutines
	set_a(ram[smb_oper_mode_task]); // get current task of victory mode
	if (reg_p.z) { goto auto_player; } // if on bridge collapse, skip enemy processing
	set_x(0x00);
	ram[smb_object_offset] = reg_x; // otherwise reset enemy object offset
	smb_enemies_and_loops_core(); // and run enemy code
auto_player:;
	smb_relative_player_position(); // get player's relative coordinates
	smb_player_gfx_handler(); return; // draw the player, then leave
	smb_victory_mode_subroutines(); return;
}

void smb_victory_mode_subroutines() {
	set_a(ram[smb_oper_mode_task]);
	static void(*targets[])() = {
		smb_bridge_collapse,
		smb_setup_victory_mode,
		smb_player_victory_walk,
		smb_print_victory_messages,
		smb_player_end_world
	};
	targets[reg_a](); return;
	smb_setup_victory_mode(); return;
}

void smb_setup_victory_mode() {
	set_x(ram[smb_screen_right_page_loc]); // get page location of right side of screen
	set_x(reg_x+1); // increment to next page
	ram[smb_destination_page_loc] = reg_x; // store here
	set_a((smb_end_of_castle_music));
	ram[smb_event_music_queue] = reg_a; // play win castle music
	smb_inc_mode_task_b(); return; // jump to set next major task in victory mode
	smb_player_victory_walk(); return;
}

void smb_player_victory_walk() {
	set_y(0x00); // set value here to not walk player by default
	ram[smb_victory_walk_control] = reg_y;
	set_a(ram[smb_player_page_loc]); // get player's page location
	cmp_a(ram[smb_destination_page_loc]); // compare with destination page location
	if (!reg_p.z) { goto perform_walk; } // if page locations don't match, branch
	set_a(ram[smb_player_x_position]); // otherwise get player's horizontal position
	cmp_a(0x60); // compare with preset horizontal position
	if (reg_p.c) { goto dont_walk; } // if still on other page, branch ahead
perform_walk:;
	ram[smb_victory_walk_control] = inc(ram[smb_victory_walk_control]); // otherwise increment value and Y
	set_y(reg_y+1); // note Y will be used to walk the player
dont_walk:;
	set_a(reg_y); // put contents of Y in A and
	smb_auto_control_player(); // use A to mvoe player to the right or not
	set_a(ram[smb_screen_left_page_loc]); // check page location of left side of screen
	cmp_a(ram[smb_destination_page_loc]); // against set value here
	if (reg_p.z) { goto exit_v_walk; } // branch if equal to change modes if necessary
	set_a(ram[smb_scroll_fractional]);
	reg_p.c = 0; // do fixed point math on fractional part of scroll
	add_a(0x80);
	ram[smb_scroll_fractional] = reg_a; // save fractional movement amount
	set_a(0x01); // set 1 pixel per frame
	add_a(0x00); // add carry from previous addition
	set_y(reg_a); // use as scroll amount
	smb_scroll_screen(); // do sub to scroll the screen
	smb_upd_scroll_var(); // do another sub to update screen and scroll variables
	ram[smb_victory_walk_control] = inc(ram[smb_victory_walk_control]); // increment value to stay in this routine
exit_v_walk:;
	set_a(ram[smb_victory_walk_control]); // load value set here
	if (reg_p.z) { smb_inc_mode_task_a(); return; } // if zero, branch to change modes
	return; // otherwise leave
	smb_print_victory_messages(); return;
}

void smb_print_victory_messages() {
	set_a(ram[smb_secondary_msg_counter]); // load secondary message counter
	if (!reg_p.z) { goto inc_msg_counter; } // if set, branch to increment message counters
	set_a(ram[smb_primary_msg_counter]); // otherwise load primary message counter
	if (reg_p.z) { goto thank_player; } // if set to zero, branch to print first message
	cmp_a(0x09); // if at 9 or above, branch elsewhere (this comparison
	if (reg_p.c) { goto inc_msg_counter; } // is residual code, counter never reaches 9)
	set_y(ram[smb_world_number]); // check world number
	cmp_y((smb_world_8));
	if (!reg_p.z) { goto m_retainer_msg; } // if not at world 8, skip to next part
	cmp_a(0x03); // check primary message counter again
	if (!reg_p.c) { goto inc_msg_counter; } // if not at 3 yet (world 8 only), branch to increment
	sub_a(0x01); // otherwise subtract one
	goto thank_player; // and skip to next part
m_retainer_msg:;
	cmp_a(0x02); // check primary message counter
	if (!reg_p.c) { goto inc_msg_counter; } // if not at 2 yet (world 1-7 only), branch
thank_player:;
	set_y(reg_a); // put primary message counter into Y
	if (!reg_p.z) { goto second_part_msg; } // if counter nonzero, skip this part, do not print first message
	set_a(ram[smb_current_player]); // otherwise get player currently on the screen
	if (reg_p.z) { goto eval_for_music; } // if mario, branch
	set_y(reg_y+1); // otherwise increment Y once for luigi and
	if (!reg_p.z) { goto eval_for_music; } // do an unconditional branch to the same place
second_part_msg:;
	set_y(reg_y+1); // increment Y to do world 8's message
	set_a(ram[smb_world_number]);
	cmp_a((smb_world_8)); // check world number
	if (reg_p.z) { goto eval_for_music; } // if at world 8, branch to next part
	set_y(reg_y-1); // otherwise decrement Y for world 1-7's message
	cmp_y(0x04); // if counter at 4 (world 1-7) only
	if (reg_p.c) { goto set_end_timer; } // branch to set victory end timer
	cmp_y(0x03); // if counter at 3 (world 1-7 only)
	if (reg_p.c) { goto inc_msg_counter; } // branch to keep counting
eval_for_music:;
	cmp_y(0x03); // if counter not yet at 3 (world 8 only), branch
	if (!reg_p.z) { goto print_msg; } // to print message only (note world 1-7 will only
	set_a((smb_victory_music)); // reach this code if counter = 0, and will always branch)
	ram[smb_event_music_queue] = reg_a; // otherwise load victory music first (world 8 only)
print_msg:;
	set_a(reg_y); // put primary message counter in A
	reg_p.c = 0; // add $0c or 12 to counter thus giving an appropriate value,
	add_a(0x0c); // ($0c-$0d = first), ($0e = world 1-7's), ($0f-$12 = world 8's)
	ram[smb_vram_buffer_addr_ctrl] = reg_a; // write message counter to vram address controller
inc_msg_counter:;
	set_a(ram[smb_secondary_msg_counter]);
	reg_p.c = 0;
	add_a(0x04); // add four to secondary message counter
	ram[smb_secondary_msg_counter] = reg_a;
	set_a(ram[smb_primary_msg_counter]);
	add_a(0x00); // add carry to primary message counter
	ram[smb_primary_msg_counter] = reg_a;
	cmp_a(0x07); // check primary counter one more time
set_end_timer:;
	if (!reg_p.c) { smb_exit_msgs(); return; } // if not reached value yet, branch to leave
	set_a(0x06);
	ram[smb_world_end_timer] = reg_a; // otherwise set world end timer
	smb_inc_mode_task_a(); return;
}

void smb_inc_mode_task_a() {
	ram[smb_oper_mode_task] = inc(ram[smb_oper_mode_task]); // move onto next task in mode
	smb_exit_msgs(); return;
}

void smb_exit_msgs() {
	return; // leave
	smb_player_end_world(); return;
}

void smb_player_end_world() {
	set_a(ram[smb_world_end_timer]); // check to see if world end timer expired
	if (!reg_p.z) { smb_end_exit_one(); return; } // branch to leave if not
	set_y(ram[smb_world_number]); // check world number
	cmp_y((smb_world_8)); // if on world 8, player is done with game,
	if (reg_p.c) { smb_end_chk_b_button(); return; } // thus branch to read controller
	set_a(0x00);
	ram[smb_area_number] = reg_a; // otherwise initialize area number used as offset
	ram[smb_level_number] = reg_a; // and level number control to start at area 1
	ram[smb_oper_mode_task] = reg_a; // initialize secondary mode of operation
	ram[smb_world_number] = inc(ram[smb_world_number]); // increment world number to move onto the next world
	smb_load_area_pointer(); // get area address offset for the next area
	ram[smb_fetch_new_game_timer_flag] = inc(ram[smb_fetch_new_game_timer_flag]); // set flag to load game timer from header
	set_a((smb_game_mode_value));
	ram[smb_oper_mode] = reg_a; // set mode of operation to game mode
	smb_end_exit_one(); return;
}

void smb_end_exit_one() {
	return; // and leave
	smb_end_chk_b_button(); return;
}

void smb_end_chk_b_button() {
	set_a(ram[smb_saved_joypad_1_bits]);
	or_a(ram[smb_saved_joypad_2_bits]); // check to see if B button was pressed on
	and_a((smb_b_button)); // either controller
	if (reg_p.z) { goto end_exit_two; } // branch to leave if not
	set_a(0x01); // otherwise set world selection flag
	ram[smb_world_select_enable_flag] = reg_a;
	set_a(0xff); // remove onscreen player's lives
	ram[smb_number_of_lives] = reg_a;
	smb_terminate_game(); // do sub to continue other player or end game
end_exit_two:;
	return; // leave
}

void smb_floatey_numbers_routine() {
	set_a(ram[smb_floatey_num_control + reg_x]); // load control for floatey number
	if (reg_p.z) { smb_end_exit_one(); return; } // if zero, branch to leave
	cmp_a(0x0b); // if less than $0b, branch
	if (!reg_p.c) { goto chk_num_timer; }
	set_a(0x0b); // otherwise set to $0b, thus keeping
	ram[smb_floatey_num_control + reg_x] = reg_a; // it in range
chk_num_timer:;
	set_y(reg_a); // use as Y
	set_a(ram[smb_floatey_num_timer + reg_x]); // check value here
	if (!reg_p.z) { goto dec_num_timer; } // if nonzero, branch ahead
	ram[smb_floatey_num_control + reg_x] = reg_a; // initialize floatey number control and leave
	return;
dec_num_timer:;
	ram[smb_floatey_num_timer + reg_x] = dec(ram[smb_floatey_num_timer + reg_x]); // decrement value here
	cmp_a(0x2b); // if not reached a certain point, branch
	if (!reg_p.z) { goto chk_tall_enemy; }
	cmp_y(0x0b); // check offset for $0b
	if (!reg_p.z) { goto load_num_tiles; } // branch ahead if not found
	ram[smb_number_of_lives] = inc(ram[smb_number_of_lives]); // give player one extra life (1-up)
	set_a((smb_sfx_extra_life));
	ram[smb_square_2_sound_queue] = reg_a; // and play the 1-up sound
load_num_tiles:;
	set_a(rom[smb_score_update_data + reg_y]); // load point value here
	reg_a = shr(reg_a); // move high nybble to low
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	set_x(reg_a); // use as X offset, essentially the digit
	set_a(rom[smb_score_update_data + reg_y]); // load again and this time
	and_a(0b00001111); // mask out the high nybble
	ram[smb_digit_modifier + reg_x] = reg_a; // store as amount to add to the digit
	smb_add_to_score(); // update the score accordingly
chk_tall_enemy:;
	set_y(ram[smb_enemy_spr_data_offset + reg_x]); // get OAM data offset for enemy object
	set_a(ram[smb_enemy_id + reg_x]); // get enemy object identifier
	cmp_a((smb_spiny));
	if (reg_p.z) { goto floatey_part; } // branch if spiny
	cmp_a((smb_piranha_plant));
	if (reg_p.z) { goto floatey_part; } // branch if piranha plant
	cmp_a((smb_hammer_bro));
	if (reg_p.z) { goto get_alt_offset; } // branch elsewhere if hammer bro
	cmp_a((smb_grey_cheep_cheep));
	if (reg_p.z) { goto floatey_part; } // branch if cheep-cheep of either color
	cmp_a((smb_red_cheep_cheep));
	if (reg_p.z) { goto floatey_part; }
	cmp_a((smb_tall_enemy));
	if (reg_p.c) { goto get_alt_offset; } // branch elsewhere if enemy object => $09
	set_a(ram[smb_enemy_state + reg_x]);
	cmp_a(0x02); // if enemy state defeated or otherwise
	if (reg_p.c) { goto floatey_part; } // $02 or greater, branch beyond this part
get_alt_offset:;
	set_x(ram[smb_spr_data_offset_ctrl]); // load some kind of control bit
	set_y(ram[smb_alt_spr_data_offset + reg_x]); // get alternate OAM data offset
	set_x(ram[smb_object_offset]); // get enemy object offset again
floatey_part:;
	set_a(ram[smb_floatey_num_y_pos + reg_x]); // get vertical coordinate for
	cmp_a(0x18); // floatey number, if coordinate in the
	if (!reg_p.c) { goto setup_num_spr; } // status bar, branch
	sub_a(0x01);
	ram[smb_floatey_num_y_pos + reg_x] = reg_a; // otherwise subtract one and store as new
setup_num_spr:;
	set_a(ram[smb_floatey_num_y_pos + reg_x]); // get vertical coordinate
	sub_a(0x08); // subtract eight and dump into the
	smb_dump_two_spr(); // left and right sprite's Y coordinates
	set_a(ram[smb_floatey_num_x_pos + reg_x]); // get horizontal coordinate
	ram[smb_sprite_x_position + reg_y] = reg_a; // store into X coordinate of left sprite
	reg_p.c = 0;
	add_a(0x08); // add eight pixels and store into X
	ram[smb_sprite_x_position+4 + reg_y] = reg_a; // coordinate of right sprite
	set_a(0x02);
	ram[smb_sprite_attributes + reg_y] = reg_a; // set palette control in attribute bytes
	ram[smb_sprite_attributes+4 + reg_y] = reg_a; // of left and right sprites
	set_a(ram[smb_floatey_num_control + reg_x]);
	reg_a = shl(reg_a); // multiply our floatey number control by 2
	set_x(reg_a); // and use as offset for look-up table
	set_a(rom[smb_floatey_num_tile_data + reg_x]);
	ram[smb_sprite_tilenumber + reg_y] = reg_a; // display first half of number of points
	set_a(rom[smb_floatey_num_tile_data+1 + reg_x]);
	ram[smb_sprite_tilenumber+4 + reg_y] = reg_a; // display the second half
	set_x(ram[smb_object_offset]); // get enemy object offset and leave
	return;
	smb_screen_routines(); return;
}

void smb_screen_routines() {
	set_a(ram[smb_screen_routine_task]); // run one of the following subroutines
	static void(*targets[])() = {
		smb_init_screen,
		smb_setup_intermediate,
		smb_write_top_status_line,
		smb_write_bottom_status_line,
		smb_display_time_up,
		smb_reset_sprites_and_screen_timer,
		smb_display_intermediate,
		smb_reset_sprites_and_screen_timer,
		smb_area_parser_task_control,
		smb_get_area_palette,
		smb_get_background_color,
		smb_get_alternate_palette_1,
		smb_draw_title_screen,
		smb_clear_buffers_draw_icon,
		smb_write_top_score
	};
	targets[reg_a](); return;
	smb_init_screen(); return;
}

void smb_init_screen() {
	smb_move_all_sprites_offscreen(); // initialize all sprites including sprite #0
	smb_initialize_name_tables(); // and erase both name and attribute tables
	set_a(ram[smb_oper_mode]);
	if (reg_p.z) { smb_next_subtask(); return; } // if mode still 0, do not load
	set_x(0x03); // into buffer pointer
	smb_set_vram_addr_a(); return;
	smb_setup_intermediate(); return;
}

void smb_setup_intermediate() {
	set_a(ram[smb_background_color_ctrl]); // save current background color control
	push(reg_a); // and player status to stack
	set_a(ram[smb_player_status]);
	push(reg_a);
	set_a(0x00); // set background color to black
	ram[smb_player_status] = reg_a; // and player status to not fiery
	set_a(0x02); // this is the ONLY time background color control
	ram[smb_background_color_ctrl] = reg_a; // is set to less than 4
	smb_get_player_colors();
	set_a(pull()); // we only execute this routine for
	ram[smb_player_status] = reg_a; // the intermediate lives display
	set_a(pull()); // and once we're done, we return bg
	ram[smb_background_color_ctrl] = reg_a; // color ctrl and player status from stack
	smb_inc_subtask(); return; // then move into the next task
}

void smb_get_area_palette() {
	set_y(ram[smb_area_type]); // select appropriate palette to load
	set_x(rom[smb_area_palette + reg_y]); // based on area type
	smb_set_vram_addr_a(); return;
}

void smb_set_vram_addr_a() {
	ram[smb_vram_buffer_addr_ctrl] = reg_x; // store offset into buffer control
	smb_next_subtask(); return;
}

void smb_next_subtask() {
	smb_inc_subtask(); return; // move onto next task
}

void smb_get_background_color() {
	set_y(ram[smb_background_color_ctrl]); // check background color control
	if (reg_p.z) { goto no_bg_color; } // if not set, increment task and fetch palette
	set_a(rom[smb_bg_color_ctrl_addr-4 + reg_y]); // put appropriate palette into vram
	ram[smb_vram_buffer_addr_ctrl] = reg_a; // note that if set to 5-7, $0301 will not be read
no_bg_color:;
	ram[smb_screen_routine_task] = inc(ram[smb_screen_routine_task]); // increment to next subtask and plod on through
	smb_get_player_colors(); return;
}

void smb_get_player_colors() {
	set_x(ram[smb_vram_buffer_1_offset]); // get current buffer offset
	set_y(0x00);
	set_a(ram[smb_current_player]); // check which player is on the screen
	if (reg_p.z) { goto chk_fiery; }
	set_y(0x04); // load offset for luigi
chk_fiery:;
	set_a(ram[smb_player_status]); // check player status
	cmp_a(0x02);
	if (!reg_p.z) { goto start_clr_get; } // if fiery, load alternate offset for fiery player
	set_y(0x08);
start_clr_get:;
	set_a(0x03); // do four colors
	ram[0x0000] = reg_a;
clr_get_loop:;
	set_a(rom[smb_player_colors + reg_y]); // fetch player colors and store them
	ram[smb_vram_buffer_1+3 + reg_x] = reg_a; // in the buffer
	set_y(reg_y+1);
	set_x(reg_x+1);
	ram[0x0000] = dec(ram[0x0000]);
	if (!reg_p.n) { goto clr_get_loop; }
	set_x(ram[smb_vram_buffer_1_offset]); // load original offset from before
	set_y(ram[smb_background_color_ctrl]); // if this value is four or greater, it will be set
	if (!reg_p.z) { goto set_bg_color; } // therefore use it as offset to background color
	set_y(ram[smb_area_type]); // otherwise use area type bits from area offset as offset
set_bg_color:;
	set_a(rom[smb_background_colors + reg_y]); // to background color instead
	ram[smb_vram_buffer_1+3 + reg_x] = reg_a;
	set_a(0x3f); // set for sprite palette address
	ram[smb_vram_buffer_1 + reg_x] = reg_a; // save to buffer
	set_a(0x10);
	ram[smb_vram_buffer_1+1 + reg_x] = reg_a;
	set_a(0x04); // write length byte to buffer
	ram[smb_vram_buffer_1+2 + reg_x] = reg_a;
	set_a(0x00); // now the null terminator
	ram[smb_vram_buffer_1+7 + reg_x] = reg_a;
	set_a(reg_x); // move the buffer pointer ahead 7 bytes
	reg_p.c = 0; // in case we want to write anything else later
	add_a(0x07);
	smb_set_vram_offset(); return;
}

void smb_set_vram_offset() {
	ram[smb_vram_buffer_1_offset] = reg_a; // store as new vram buffer offset
	return;
	smb_get_alternate_palette_1(); return;
}

void smb_get_alternate_palette_1() {
	set_a(ram[smb_area_style]); // check for mushroom level style
	cmp_a(0x01);
	if (!reg_p.z) { smb_no_alt_pal(); return; }
	set_a(0x0b); // if found, load appropriate palette
	smb_set_vram_addr_b(); return;
}

void smb_set_vram_addr_b() {
	ram[smb_vram_buffer_addr_ctrl] = reg_a;
	smb_no_alt_pal(); return;
}

void smb_no_alt_pal() {
	smb_inc_subtask(); return; // now onto the next task
	smb_write_top_status_line(); return;
}

void smb_write_top_status_line() {
	set_a(0x00); // select main status bar
	smb_write_game_text(); // output it
	smb_inc_subtask(); return; // onto the next task
	smb_write_bottom_status_line(); return;
}

void smb_write_bottom_status_line() {
	smb_get_sb_nybbles(); // write player's score and coin tally to screen
	set_x(ram[smb_vram_buffer_1_offset]);
	set_a(0x20); // write address for world-area number on screen
	ram[smb_vram_buffer_1 + reg_x] = reg_a;
	set_a(0x73);
	ram[smb_vram_buffer_1+1 + reg_x] = reg_a;
	set_a(0x03); // write length for it
	ram[smb_vram_buffer_1+2 + reg_x] = reg_a;
	set_y(ram[smb_world_number]); // first the world number
	set_y(reg_y+1);
	set_a(reg_y);
	ram[smb_vram_buffer_1+3 + reg_x] = reg_a;
	set_a(0x28); // next the dash
	ram[smb_vram_buffer_1+4 + reg_x] = reg_a;
	set_y(ram[smb_level_number]); // next the level number
	set_y(reg_y+1); // increment for proper number display
	set_a(reg_y);
	ram[smb_vram_buffer_1+5 + reg_x] = reg_a;
	set_a(0x00); // put null terminator on
	ram[smb_vram_buffer_1+6 + reg_x] = reg_a;
	set_a(reg_x); // move the buffer offset up by 6 bytes
	reg_p.c = 0;
	add_a(0x06);
	ram[smb_vram_buffer_1_offset] = reg_a;
	smb_inc_subtask(); return;
	smb_display_time_up(); return;
}

void smb_display_time_up() {
	set_a(ram[smb_game_timer_expired_flag]); // if game timer not expired, increment task
	if (reg_p.z) { goto no_time_up; } // control 2 tasks forward, otherwise, stay here
	set_a(0x00);
	ram[smb_game_timer_expired_flag] = reg_a; // reset timer expiration flag
	set_a(0x02); // output time-up screen to buffer
	smb_output_inter(); return;
no_time_up:;
	ram[smb_screen_routine_task] = inc(ram[smb_screen_routine_task]); // increment control task 2 tasks forward
	smb_inc_subtask(); return;
	smb_display_intermediate(); return;
}

void smb_display_intermediate() {
	set_a(ram[smb_oper_mode]); // check primary mode of operation
	if (reg_p.z) { smb_no_inter(); return; } // if in title screen mode, skip this
	cmp_a((smb_game_over_mode_value)); // are we in game over mode?
	if (reg_p.z) { smb_game_over_inter(); return; } // if so, proceed to display game over screen
	set_a(ram[smb_alt_entrance_control]); // otherwise check for mode of alternate entry
	if (!reg_p.z) { smb_no_inter(); return; } // and branch if found
	set_y(ram[smb_area_type]); // check if we are on castle level
	cmp_y(0x03); // and if so, branch (possibly residual)
	if (reg_p.z) { goto player_inter; }
	set_a(ram[smb_disable_intermediate]); // if this flag is set, skip intermediate lives display
	if (!reg_p.z) { smb_no_inter(); return; } // and jump to specific task, otherwise
player_inter:;
	smb_draw_player_intermediate(); // put player in appropriate place for
	set_a(0x01); // lives display, then output lives display to buffer
	smb_output_inter(); return;
}

void smb_output_inter() {
	smb_write_game_text();
	smb_reset_screen_timer();
	set_a(0x00);
	ram[smb_disable_screen_flag] = reg_a; // reenable screen output
	return;
	smb_game_over_inter(); return;
}

void smb_game_over_inter() {
	set_a(0x12); // set screen timer
	ram[smb_screen_timer] = reg_a;
	set_a(0x03); // output game over screen to buffer
	smb_write_game_text();
	smb_inc_mode_task_b(); return;
	smb_no_inter(); return;
}

void smb_no_inter() {
	set_a(0x08); // set for specific task and leave
	ram[smb_screen_routine_task] = reg_a;
	return;
	smb_area_parser_task_control(); return;
}

void smb_area_parser_task_control() {
	ram[smb_disable_screen_flag] = inc(ram[smb_disable_screen_flag]); // turn off screen
task_loop:;
	smb_area_parser_task_handler(); // render column set of current area
	set_a(ram[smb_area_parser_task_num]); // check number of tasks
	if (!reg_p.z) { goto task_loop; } // if tasks still not all done, do another one
	ram[smb_column_sets] = dec(ram[smb_column_sets]); // do we need to render more column sets?
	if (!reg_p.n) { goto output_col; }
	ram[smb_screen_routine_task] = inc(ram[smb_screen_routine_task]); // if not, move on to the next task
output_col:;
	set_a(0x06); // set vram buffer to output rendered column set
	ram[smb_vram_buffer_addr_ctrl] = reg_a; // on next NMI
	return;
	smb_draw_title_screen(); return;
}

void smb_draw_title_screen() {
	set_a(ram[smb_oper_mode]); // are we in title screen mode?
	if (!reg_p.z) { smb_inc_mode_task_b(); return; } // if not, exit
	set_a((smb_title_screen_data_offset >> 8)); // load address $1ec0 into
	env_ppuaddr_w(reg_a); // the vram address register
	set_a((smb_title_screen_data_offset & 0xff));
	env_ppuaddr_w(reg_a);
	set_a(0x03); // put address $0300 into
	ram[0x0001] = reg_a; // the indirect at $00
	set_y(0x00);
	ram[0x0000] = reg_y;
	set_a(env_ppudata_r()); // do one garbage read
output_t_scr:;
	set_a(env_ppudata_r()); // get title screen from chr-rom
	mem_w(*(uint16_t*)&ram[0x0000] + reg_y, reg_a); // store 256 bytes into buffer
	set_y(reg_y+1);
	if (!reg_p.z) { goto chk_hi_byte; } // if not past 256 bytesw, do not increment
	ram[0x0001] = inc(ram[0x0001]); // otherwise increment high byte of indirect
chk_hi_byte:;
	set_a(ram[0x0001]); // heck high byte?
	cmp_a(0x04); // at $0400?
	if (!reg_p.z) { goto output_t_scr; } // if not, loop back and do another
	cmp_y(0x3a); // check if offset points past end of data
	if (!reg_p.c) { goto output_t_scr; } // if not, loop back and do another
	set_a(0x05); // set buffer transfer control to $0300,
	smb_set_vram_addr_b(); return; // increment task and exit
	smb_clear_buffers_draw_icon(); return;
}

void smb_clear_buffers_draw_icon() {
	set_a(ram[smb_oper_mode]); // check game mode
	if (!reg_p.z) { smb_inc_mode_task_b(); return; } // if not title screen mnode, leave
	set_x(0x00); // otherwise, clear buffer space
t_scr_clear:;
	ram[smb_vram_buffer_1-1 + reg_x] = reg_a;
	ram[smb_vram_buffer_1+0xff + reg_x] = reg_a;
	set_x(reg_x-1);
	if (!reg_p.z) { goto t_scr_clear; }
	smb_draw_mushroom_icon(); // draw player select icon
	smb_inc_subtask(); return;
}

void smb_inc_subtask() {
	ram[smb_screen_routine_task] = inc(ram[smb_screen_routine_task]); // move onto next task
	return;
	smb_write_top_score(); return;
}

void smb_write_top_score() {
	set_a(0xfa); // run display routine to display top score on title
	smb_update_number();
	smb_inc_mode_task_b(); return;
}

void smb_inc_mode_task_b() {
	ram[smb_oper_mode_task] = inc(ram[smb_oper_mode_task]); // move onto next mode
	return;
}

void smb_write_game_text() {
	push(reg_a); // save text number to stack
	reg_a = shl(reg_a);
	set_y(reg_a); // multiply by 2 and use as offset
	cmp_y(0x04); // if set to do top status bar on world/lives display,
	if (!reg_p.c) { goto ld_game_text; } // branch to use current offset as-is
	cmp_y(0x08); // if set to do time-up or game over,
	if (!reg_p.c) { goto chk_2_players; } // branch to check players
	set_y(0x08); // otherwise warp zone, therefore set offset
chk_2_players:;
	set_a(ram[smb_number_of_players]); // check for number of players
	if (!reg_p.z) { goto ld_game_text; } // if there are two, use current offset to also print name
	set_y(reg_y+1); // otherwise increment offset by one to not print name
ld_game_text:;
	set_x(rom[smb_game_text_offsets + reg_y]); // get offset to message we want to print
	set_y(0x00);
game_text_loop:;
	set_a(rom[smb_game_text + reg_x]); // load message data
	cmp_a(0xff); // check for terminator
	if (reg_p.z) { goto end_game_text; } // branch to end text if found
	ram[smb_vram_buffer_1 + reg_y] = reg_a; // otherwise write data to buffer
	set_x(reg_x+1); // and increment increment
	set_y(reg_y+1);
	if (!reg_p.z) { goto game_text_loop; } // do this for 256 bytes if no terminator found
end_game_text:;
	set_a(0x00); // put null terminator at end
	ram[smb_vram_buffer_1 + reg_y] = reg_a;
	set_a(pull()); // pull original text number from stack
	set_x(reg_a);
	cmp_a(0x04); // are we printing warp zone?
	if (reg_p.c) { goto print_warp_zone_numbers; }
	set_x(reg_x-1); // are we printing the world/lives display?
	if (!reg_p.z) { goto check_player_name; } // if not, branch to check player's name
	set_a(ram[smb_number_of_lives]); // otherwise, check number of lives
	reg_p.c = 0; // and increment by one for display
	add_a(0x01);
	cmp_a((10)); // more than 9 lives?
	if (!reg_p.c) { goto put_lives; }
	sub_a((10)); // if so, subtract 10 and put a crown tile
	set_y(0x9f); // next to the difference...strange things happen if
	ram[smb_vram_buffer_1+7] = reg_y; // the number of lives exceeds 19
put_lives:;
	ram[smb_vram_buffer_1+8] = reg_a;
	set_y(ram[smb_world_number]); // write world and level numbers (incremented for display)
	set_y(reg_y+1); // to the buffer in the spaces surrounding the dash
	ram[smb_vram_buffer_1+0x13] = reg_y;
	set_y(ram[smb_level_number]);
	set_y(reg_y+1);
	ram[smb_vram_buffer_1+0x15] = reg_y; // we're done here
	return;
check_player_name:;
	set_a(ram[smb_number_of_players]); // check number of players
	if (reg_p.z) { goto exit_chk_name; } // if only 1 player, leave
	set_a(ram[smb_current_player]); // load current player
	set_x(reg_x-1); // check to see if current message number is for time up
	if (!reg_p.z) { goto chk_luigi; }
	set_y(ram[smb_oper_mode]); // check for game over mode
	cmp_y((smb_game_over_mode_value));
	if (reg_p.z) { goto chk_luigi; }
	eor_a(0b00000001); // if not, must be time up, invert d0 to do other player
chk_luigi:;
	reg_a = shr(reg_a);
	if (!reg_p.c) { goto exit_chk_name; } // if mario is current player, do not change the name
	set_y(0x04);
name_loop:;
	set_a(rom[smb_luigi_name + reg_y]); // otherwise, replace "MARIO" with "LUIGI"
	ram[smb_vram_buffer_1+3 + reg_y] = reg_a;
	set_y(reg_y-1);
	if (!reg_p.n) { goto name_loop; } // do this until each letter is replaced
exit_chk_name:;
	return;
print_warp_zone_numbers:;
	sub_a(0x04); // subtract 4 and then shift to the left
	reg_a = shl(reg_a); // twice to get proper warp zone number
	reg_a = shl(reg_a); // offset
	set_x(reg_a);
	set_y(0x00);
warp_num_loop:;
	set_a(rom[smb_warp_zone_numbers + reg_x]); // print warp zone numbers into the
	ram[smb_vram_buffer_1+0x1b + reg_y] = reg_a; // placeholders from earlier
	set_x(reg_x+1);
	set_y(reg_y+1); // put a number in every fourth space
	set_y(reg_y+1);
	set_y(reg_y+1);
	set_y(reg_y+1);
	cmp_y(0x0c);
	if (!reg_p.c) { goto warp_num_loop; }
	set_a(0x2c); // load new buffer pointer at end of message
	smb_set_vram_offset(); return;
	smb_reset_sprites_and_screen_timer(); return;
}

void smb_reset_sprites_and_screen_timer() {
	set_a(ram[smb_screen_timer]); // check if screen timer has expired
	if (!reg_p.z) { smb_no_reset(); return; } // if not, branch to leave
	smb_move_all_sprites_offscreen(); // otherwise reset sprites now
	smb_reset_screen_timer(); return;
}

void smb_reset_screen_timer() {
	set_a(0x07); // reset timer again
	ram[smb_screen_timer] = reg_a;
	ram[smb_screen_routine_task] = inc(ram[smb_screen_routine_task]); // move onto next task
	smb_no_reset(); return;
}

void smb_no_reset() {
	return;
	smb_render_area_graphics(); return;
}

void smb_render_area_graphics() {
	set_a(ram[smb_current_column_pos]); // store LSB of where we're at
	and_a(0x01);
	ram[0x0005] = reg_a;
	set_y(ram[smb_vram_buffer_2_offset]); // store vram buffer offset
	ram[0x0000] = reg_y;
	set_a(ram[smb_current_nt_addr_low]); // get current name table address we're supposed to render
	ram[smb_vram_buffer_2+1 + reg_y] = reg_a;
	set_a(ram[smb_current_nt_addr_high]);
	ram[smb_vram_buffer_2 + reg_y] = reg_a;
	set_a(0x9a); // store length byte of 26 here with d7 set
	ram[smb_vram_buffer_2+2 + reg_y] = reg_a; // to increment by 32 (in columns)
	set_a(0x00); // init attribute row
	ram[0x0004] = reg_a;
	set_x(reg_a);
draw_mt_loop:;
	ram[0x0001] = reg_x; // tore init value of 0 or incremented offset for buffer
	set_a(ram[smb_metatile_buffer + reg_x]); // get first metatile number, and mask out all but 2 MSB
	and_a(0b11000000);
	ram[0x0003] = reg_a; // store attribute table bits here
	reg_a = shl(reg_a); // note that metatile format is:
	reg_a = rol(reg_a); // %xx000000 - attribute table bits
	reg_a = rol(reg_a); // %00xxxxxx - metatile number
	set_y(reg_a); // rotate bits to d1-d0 and use as offset here
	set_a(rom[smb_metatile_graphics_low + reg_y]); // get address to graphics table from here
	ram[0x0006] = reg_a;
	set_a(rom[smb_metatile_graphics_high + reg_y]);
	ram[0x0007] = reg_a;
	set_a(ram[smb_metatile_buffer + reg_x]); // get metatile number again
	reg_a = shl(reg_a); // multiply by 4 and use as tile offset
	reg_a = shl(reg_a);
	ram[0x0002] = reg_a;
	set_a(ram[smb_area_parser_task_num]); // get current task number for level processing and
	and_a(0b00000001); // mask out all but LSB, then invert LSB, multiply by 2
	eor_a(0b00000001); // to get the correct column position in the metatile,
	reg_a = shl(reg_a); // then add to the tile offset so we can draw either side
	add_a(ram[0x0002]); // of the metatiles
	set_y(reg_a);
	set_x(ram[0x0000]); // use vram buffer offset from before as X
	set_a(mem_r(*(uint16_t*)&ram[0x0006] + reg_y));
	ram[smb_vram_buffer_2+3 + reg_x] = reg_a; // get first tile number (top left or top right) and store
	set_y(reg_y+1);
	set_a(mem_r(*(uint16_t*)&ram[0x0006] + reg_y)); // now get the second (bottom left or bottom right) and store
	ram[smb_vram_buffer_2+4 + reg_x] = reg_a;
	set_y(ram[0x0004]); // get current attribute row
	set_a(ram[0x0005]); // get LSB of current column where we're at, and
	if (!reg_p.z) { goto right_check; } // branch if set (clear = left attrib, set = right)
	set_a(ram[0x0001]); // get current row we're rendering
	reg_a = shr(reg_a); // branch if LSB set (clear = top left, set = bottom left)
	if (reg_p.c) { goto l_left; }
	ram[0x0003] = rol(ram[0x0003]); // rotate attribute bits 3 to the left
	ram[0x0003] = rol(ram[0x0003]); // this in d1-d0, for upper left square
	ram[0x0003] = rol(ram[0x0003]);
	goto set_attrib;
right_check:;
	set_a(ram[0x0001]); // get LSB of current row we're rendering
	reg_a = shr(reg_a); // branch if set (clear = top right, set = bottom right)
	if (reg_p.c) { goto next_mt_row; }
	ram[0x0003] = shr(ram[0x0003]); // shift attribute bits 4 to the right
	ram[0x0003] = shr(ram[0x0003]);
	ram[0x0003] = shr(ram[0x0003]);
	ram[0x0003] = shr(ram[0x0003]);
	goto set_attrib;
l_left:;
	ram[0x0003] = shr(ram[0x0003]); // shift attribute bits 2 to the right
	ram[0x0003] = shr(ram[0x0003]); // this in d5-d4 for lower left square
next_mt_row:;
	ram[0x0004] = inc(ram[0x0004]); // move onto next attribute row
set_attrib:;
	set_a(ram[smb_attribute_buffer + reg_y]); // get previously saved bits from before
	or_a(ram[0x0003]); // if any, and put new bits, if any
	ram[smb_attribute_buffer + reg_y] = reg_a; // onto the old, and store
	ram[0x0000] = inc(ram[0x0000]); // increment vram buffer offset by 2
	ram[0x0000] = inc(ram[0x0000]);
	set_x(ram[0x0001]); // get current gfx buffer row, and check for
	set_x(reg_x+1); // the bottom of the screen
	cmp_x(0x0d);
	if (!reg_p.c) { goto draw_mt_loop; } // if not there yet, loop back
	set_y(ram[0x0000]); // get current vram buffer offset, increment by 3
	set_y(reg_y+1); // (for name table address and length bytes)
	set_y(reg_y+1);
	set_y(reg_y+1);
	set_a(0x00);
	ram[smb_vram_buffer_2 + reg_y] = reg_a; // put null terminator at end of data for name table
	ram[smb_vram_buffer_2_offset] = reg_y; // store new buffer offset
	ram[smb_current_nt_addr_low] = inc(ram[smb_current_nt_addr_low]); // increment name table address low
	set_a(ram[smb_current_nt_addr_low]); // check current low byte
	and_a(0b00011111); // if no wraparound, just skip this part
	if (!reg_p.z) { goto exit_draw_m; }
	set_a(0x80); // if wraparound occurs, make sure low byte stays
	ram[smb_current_nt_addr_low] = reg_a; // just under the status bar
	set_a(ram[smb_current_nt_addr_high]); // and then invert d2 of the name table address high
	eor_a(0b00000100); // to move onto the next appropriate name table
	ram[smb_current_nt_addr_high] = reg_a;
exit_draw_m:;
	smb_set_vram_ctrl(); return; // jump to set buffer to $0341 and leave
	smb_render_attribute_tables(); return;
}

void smb_render_attribute_tables() {
	set_a(ram[smb_current_nt_addr_low]); // get low byte of next name table address
	and_a(0b00011111); // to be written to, mask out all but 5 LSB,
	reg_p.c = 1; // subtract four
	sub_a(0x04);
	and_a(0b00011111); // mask out bits again and store
	ram[0x0001] = reg_a;
	set_a(ram[smb_current_nt_addr_high]); // get high byte and branch if borrow not set
	if (reg_p.c) { goto set_at_high; }
	eor_a(0b00000100); // otherwise invert d2
set_at_high:;
	and_a(0b00000100); // mask out all other bits
	or_a(0x23); // add $2300 to the high byte and store
	ram[0x0000] = reg_a;
	set_a(ram[0x0001]); // get low byte - 4, divide by 4, add offset for
	reg_a = shr(reg_a); // attribute table and store
	reg_a = shr(reg_a);
	add_a(0xc0); // we should now have the appropriate block of
	ram[0x0001] = reg_a; // attribute table in our temp address
	set_x(0x00);
	set_y(ram[smb_vram_buffer_2_offset]); // get buffer offset
attrib_loop:;
	set_a(ram[0x0000]);
	ram[smb_vram_buffer_2 + reg_y] = reg_a; // store high byte of attribute table address
	set_a(ram[0x0001]);
	reg_p.c = 0; // get low byte, add 8 because we want to start
	add_a(0x08); // below the status bar, and store
	ram[smb_vram_buffer_2+1 + reg_y] = reg_a;
	ram[0x0001] = reg_a; // also store in temp again
	set_a(ram[smb_attribute_buffer + reg_x]); // fetch current attribute table byte and store
	ram[smb_vram_buffer_2+3 + reg_y] = reg_a; // in the buffer
	set_a(0x01);
	ram[smb_vram_buffer_2+2 + reg_y] = reg_a; // store length of 1 in buffer
	reg_a = shr(reg_a);
	ram[smb_attribute_buffer + reg_x] = reg_a; // clear current byte in attribute buffer
	set_y(reg_y+1); // increment buffer offset by 4 bytes
	set_y(reg_y+1);
	set_y(reg_y+1);
	set_y(reg_y+1);
	set_x(reg_x+1); // increment attribute offset and check to see
	cmp_x(0x07); // if we're at the end yet
	if (!reg_p.c) { goto attrib_loop; }
	ram[smb_vram_buffer_2 + reg_y] = reg_a; // put null terminator at the end
	ram[smb_vram_buffer_2_offset] = reg_y; // store offset in case we want to do any more
	smb_set_vram_ctrl(); return;
}

void smb_set_vram_ctrl() {
	set_a(0x06);
	ram[smb_vram_buffer_addr_ctrl] = reg_a; // set buffer to $0341 and leave
	return;
}

void smb_color_rotation() {
	set_a(ram[smb_frame_counter]); // get frame counter
	and_a(0x07); // mask out all but three LSB
	if (!reg_p.z) { goto exit_color_rot; } // branch if not set to zero to do this every eighth frame
	set_x(ram[smb_vram_buffer_1_offset]); // check vram buffer offset
	cmp_x(0x31);
	if (reg_p.c) { goto exit_color_rot; } // if offset over 48 bytes, branch to leave
	set_y(reg_a); // otherwise use frame counter's 3 LSB as offset here
get_blank_pal:;
	set_a(rom[smb_blank_palette + reg_y]); // get blank palette for palette 3
	ram[smb_vram_buffer_1 + reg_x] = reg_a; // store it in the vram buffer
	set_x(reg_x+1); // increment offsets
	set_y(reg_y+1);
	cmp_y(0x08);
	if (!reg_p.c) { goto get_blank_pal; } // do this until all bytes are copied
	set_x(ram[smb_vram_buffer_1_offset]); // get current vram buffer offset
	set_a(0x03);
	ram[0x0000] = reg_a; // set counter here
	set_a(ram[smb_area_type]); // get area type
	reg_a = shl(reg_a); // multiply by 4 to get proper offset
	reg_a = shl(reg_a);
	set_y(reg_a); // save as offset here
get_area_pal:;
	set_a(rom[smb_palette_3_data + reg_y]); // fetch palette to be written based on area type
	ram[smb_vram_buffer_1+3 + reg_x] = reg_a; // store it to overwrite blank palette in vram buffer
	set_y(reg_y+1);
	set_x(reg_x+1);
	ram[0x0000] = dec(ram[0x0000]); // decrement counter
	if (!reg_p.n) { goto get_area_pal; } // do this until the palette is all copied
	set_x(ram[smb_vram_buffer_1_offset]); // get current vram buffer offset
	set_y(ram[smb_color_rotate_offset]); // get color cycling offset
	set_a(rom[smb_color_rotate_palette + reg_y]);
	ram[smb_vram_buffer_1+4 + reg_x] = reg_a; // get and store current color in second slot of palette
	set_a(ram[smb_vram_buffer_1_offset]);
	reg_p.c = 0; // add seven bytes to vram buffer offset
	add_a(0x07);
	ram[smb_vram_buffer_1_offset] = reg_a;
	ram[smb_color_rotate_offset] = inc(ram[smb_color_rotate_offset]); // increment color cycling offset
	set_a(ram[smb_color_rotate_offset]);
	cmp_a(0x06); // check to see if it's still in range
	if (!reg_p.c) { goto exit_color_rot; } // if so, branch to leave
	set_a(0x00);
	ram[smb_color_rotate_offset] = reg_a; // otherwise, init to keep it in range
exit_color_rot:;
	return; // leave
}

void smb_remove_coin_axe() {
	set_y(0x41); // set low byte so offset points to $0341
	set_a(0x03); // load offset for default blank metatile
	set_x(ram[smb_area_type]); // check area type
	if (!reg_p.z) { goto write_blank_mt; } // if not water type, use offset
	set_a(0x04); // otherwise load offset for blank metatile used in water
write_blank_mt:;
	smb_put_block_metatile(); // do a sub to write blank metatile to vram buffer
	set_a(0x06);
	ram[smb_vram_buffer_addr_ctrl] = reg_a; // set vram address controller to $0341 and leave
	return;
	smb_replace_block_metatile(); return;
}

void smb_replace_block_metatile() {
	smb_write_block_metatile(); // write metatile to vram buffer to replace block object
	ram[smb_block_residual_counter] = inc(ram[smb_block_residual_counter]); // increment unused counter (residual code)
	ram[smb_block_rep_flag + reg_x] = dec(ram[smb_block_rep_flag + reg_x]); // decrement flag (residual code)
	return; // leave
	smb_destroy_block_metatile(); return;
}

void smb_destroy_block_metatile() {
	set_a(0x00); // force blank metatile if branched/jumped to this point
	smb_write_block_metatile(); return;
}

void smb_write_block_metatile() {
	set_y(0x03); // load offset for blank metatile
	cmp_a(0x00); // check contents of A for blank metatile
	if (reg_p.z) { goto use_b_offset; } // branch if found (unconditional if branched from 8a6b)
	set_y(0x00); // load offset for brick metatile w/ line
	cmp_a(0x58);
	if (reg_p.z) { goto use_b_offset; } // use offset if metatile is brick with coins (w/ line)
	cmp_a(0x51);
	if (reg_p.z) { goto use_b_offset; } // use offset if metatile is breakable brick w/ line
	set_y(reg_y+1); // increment offset for brick metatile w/o line
	cmp_a(0x5d);
	if (reg_p.z) { goto use_b_offset; } // use offset if metatile is brick with coins (w/o line)
	cmp_a(0x52);
	if (reg_p.z) { goto use_b_offset; } // use offset if metatile is breakable brick w/o line
	set_y(reg_y+1); // if any other metatile, increment offset for empty block
use_b_offset:;
	set_a(reg_y); // put Y in A
	set_y(ram[smb_vram_buffer_1_offset]); // get vram buffer offset
	set_y(reg_y+1); // move onto next byte
	smb_put_block_metatile(); // get appropriate block data and write to vram buffer
	smb_move_v_offset(); return;
}

void smb_move_v_offset() {
	set_y(reg_y-1); // decrement vram buffer offset
	set_a(reg_y); // add 10 bytes to it
	reg_p.c = 0;
	add_a((10));
	smb_set_vram_offset(); return; // branch to store as new vram buffer offset
	smb_put_block_metatile(); return;
}

void smb_put_block_metatile() {
	ram[0x0000] = reg_x; // store control bit from smb_spr_data_offset_ctrl
	ram[0x0001] = reg_y; // store vram buffer offset for next byte
	reg_a = shl(reg_a);
	reg_a = shl(reg_a); // multiply A by four and use as X
	set_x(reg_a);
	set_y(0x20); // load high byte for name table 0
	set_a(ram[0x0006]); // get low byte of block buffer pointer
	cmp_a(0xd0); // check to see if we're on odd-page block buffer
	if (!reg_p.c) { goto save_h_addr; } // if not, use current high byte
	set_y(0x24); // otherwise load high byte for name table 1
save_h_addr:;
	ram[0x0003] = reg_y; // save high byte here
	and_a(0x0f); // mask out high nybble of block buffer pointer
	reg_a = shl(reg_a); // multiply by 2 to get appropriate name table low byte
	ram[0x0004] = reg_a; // and then store it here
	set_a(0x00);
	ram[0x0005] = reg_a; // initialize temp high byte
	set_a(ram[0x0002]); // get vertical high nybble offset used in block buffer routine
	reg_p.c = 0;
	add_a(0x20); // add 32 pixels for the status bar
	reg_a = shl(reg_a);
	ram[0x0005] = rol(ram[0x0005]); // shift and rotate d7 onto d0 and d6 into carry
	reg_a = shl(reg_a);
	ram[0x0005] = rol(ram[0x0005]); // shift and rotate d6 onto d0 and d5 into carry
	add_a(ram[0x0004]); // add low byte of name table and carry to vertical high nybble
	ram[0x0004] = reg_a; // and store here
	set_a(ram[0x0005]); // get whatever was in d7 and d6 of vertical high nybble
	add_a(0x00); // add carry
	reg_p.c = 0;
	add_a(ram[0x0003]); // then add high byte of name table
	ram[0x0005] = reg_a; // store here
	set_y(ram[0x0001]); // get vram buffer offset to be used
	smb_rem_bridge(); return;
}

void smb_rem_bridge() {
	set_a(rom[smb_block_gfx_data + reg_x]); // write top left and top right
	ram[smb_vram_buffer_1+2 + reg_y] = reg_a; // tile numbers into first spot
	set_a(rom[smb_block_gfx_data+1 + reg_x]);
	ram[smb_vram_buffer_1+3 + reg_y] = reg_a;
	set_a(rom[smb_block_gfx_data+2 + reg_x]); // write bottom left and bottom
	ram[smb_vram_buffer_1+7 + reg_y] = reg_a; // right tiles numbers into
	set_a(rom[smb_block_gfx_data+3 + reg_x]); // second spot
	ram[smb_vram_buffer_1+8 + reg_y] = reg_a;
	set_a(ram[0x0004]);
	ram[smb_vram_buffer_1 + reg_y] = reg_a; // write low byte of name table
	reg_p.c = 0; // into first slot as read
	add_a(0x20); // add 32 bytes to value
	ram[smb_vram_buffer_1+5 + reg_y] = reg_a; // write low byte of name table
	set_a(ram[0x0005]); // plus 32 bytes into second slot
	ram[smb_vram_buffer_1-1 + reg_y] = reg_a; // write high byte of name
	ram[smb_vram_buffer_1+4 + reg_y] = reg_a; // table address to both slots
	set_a(0x02);
	ram[smb_vram_buffer_1+1 + reg_y] = reg_a; // put length of 2 in
	ram[smb_vram_buffer_1+6 + reg_y] = reg_a; // both slots
	set_a(0x00);
	ram[smb_vram_buffer_1+0x09 + reg_y] = reg_a; // put null terminator at end
	set_x(ram[0x0000]); // get offset control bit here
	return; // and leave
}

void smb_initialize_name_tables() {
	set_a(env_ppustatus_r()); // reset flip-flop
	set_a(ram[smb_mirror_ppu_ctrl_reg_1]); // load mirror of ppu reg $2000
	or_a(0b00010000); // set sprites for first 4k and background for second 4k
	and_a(0b11110000); // clear rest of lower nybble, leave higher alone
	smb_write_ppu_reg_1();
	set_a(0x24); // set vram address to start of name table 1
	smb_write_nt_addr();
	set_a(0x20); // and then set it to name table 0
	smb_write_nt_addr(); return;
}

void smb_write_nt_addr() {
	env_ppuaddr_w(reg_a);
	set_a(0x00);
	env_ppuaddr_w(reg_a);
	set_x(0x04); // clear name table with blank tile #24
	set_y(0xc0);
	set_a(0x24);
init_nt_loop:;
	env_ppudata_w(reg_a); // count out exactly 768 tiles
	set_y(reg_y-1);
	if (!reg_p.z) { goto init_nt_loop; }
	set_x(reg_x-1);
	if (!reg_p.z) { goto init_nt_loop; }
	set_y((64)); // now to clear the attribute table (with zero this time)
	set_a(reg_x);
	ram[smb_vram_buffer_1_offset] = reg_a; // init vram buffer 1 offset
	ram[smb_vram_buffer_1] = reg_a; // init vram buffer 1
init_at_loop:;
	env_ppudata_w(reg_a);
	set_y(reg_y-1);
	if (!reg_p.z) { goto init_at_loop; }
	ram[smb_horizontal_scroll] = reg_a; // reset scroll variables
	ram[smb_vertical_scroll] = reg_a;
	smb_init_scroll(); return; // initialize scroll registers to zero
	smb_read_joypads(); return;
}

void smb_read_joypads() {
	set_a(0x01); // reset and clear strobe of joypad ports
	env_joy1_w(reg_a);
	reg_a = shr(reg_a);
	set_x(reg_a); // start with joypad 1's port
	env_joy1_w(reg_a);
	smb_read_port_bits();
	set_x(reg_x+1); // increment for joypad 2's port
	smb_read_port_bits(); return;
}

void smb_read_port_bits() {
	set_y(0x08);
port_loop:;
	push(reg_a); // push previous bit onto stack
	set_a((reg_x==0)? env_joy1_r() : env_joy2_r()); // read current bit on joypad port
	ram[0x0000] = reg_a; // check d1 and d0 of port output
	reg_a = shr(reg_a); // this is necessary on the old
	or_a(ram[0x0000]); // famicom systems in japan
	reg_a = shr(reg_a);
	set_a(pull()); // read bits from stack
	reg_a = rol(reg_a); // rotate bit from carry flag
	set_y(reg_y-1);
	if (!reg_p.z) { goto port_loop; } // count down bits left
	ram[smb_saved_joypad_1_bits + reg_x] = reg_a; // save controller status here always
	push(reg_a);
	and_a(0b00110000); // check for select or start
	and_a(ram[smb_joypad_bit_mask + reg_x]); // if neither saved state nor current state
	if (reg_p.z) { goto save_8_bits; } // have any of these two set, branch
	set_a(pull());
	and_a(0b11001111); // otherwise store without select
	ram[smb_saved_joypad_1_bits + reg_x] = reg_a; // or start bits and leave
	return;
save_8_bits:;
	set_a(pull());
	ram[smb_joypad_bit_mask + reg_x] = reg_a; // save with all bits in another place and leave
	return;
	smb_write_buffer_to_screen(); return;
}

void smb_write_buffer_to_screen() {
	env_ppuaddr_w(reg_a); // store high byte of vram address
	set_y(reg_y+1);
	set_a(mem_r(*(uint16_t*)&ram[0x0000] + reg_y)); // load next byte (second)
	env_ppuaddr_w(reg_a); // store low byte of vram address
	set_y(reg_y+1);
	set_a(mem_r(*(uint16_t*)&ram[0x0000] + reg_y)); // load next byte (third)
	reg_a = shl(reg_a); // shift to left and save in stack
	push(reg_a);
	set_a(ram[smb_mirror_ppu_ctrl_reg_1]); // load mirror of $2000,
	or_a(0b00000100); // set ppu to increment by 32 by default
	if (reg_p.c) { goto setup_writes; } // if d7 of third byte was clear, ppu will
	and_a(0b11111011); // only increment by 1
setup_writes:;
	smb_write_ppu_reg_1(); // write to register
	set_a(pull()); // pull from stack and shift to left again
	reg_a = shl(reg_a);
	if (!reg_p.c) { goto get_length; } // if d6 of third byte was clear, do not repeat byte
	or_a(0b00000010); // otherwise set d1 and increment Y
	set_y(reg_y+1);
get_length:;
	reg_a = shr(reg_a); // shift back to the right to get proper length
	reg_a = shr(reg_a); // note that d1 will now be in carry
	set_x(reg_a);
output_to_vram:;
	if (reg_p.c) { goto repeat_byte; } // if carry set, repeat loading the same byte
	set_y(reg_y+1); // otherwise increment Y to load next byte
repeat_byte:;
	set_a(mem_r(*(uint16_t*)&ram[0x0000] + reg_y)); // load more data from buffer and write to vram
	env_ppudata_w(reg_a);
	set_x(reg_x-1); // done writing?
	if (!reg_p.z) { goto output_to_vram; }
	reg_p.c = 1;
	set_a(reg_y);
	add_a(ram[0x0000]); // add end length plus one to the indirect at $00
	ram[0x0000] = reg_a; // to allow this routine to read another set of updates
	set_a(0x00);
	add_a(ram[0x0001]);
	ram[0x0001] = reg_a;
	set_a(0x3f); // sets vram address to $3f00
	env_ppuaddr_w(reg_a);
	set_a(0x00);
	env_ppuaddr_w(reg_a);
	env_ppuaddr_w(reg_a); // then reinitializes it for some reason
	env_ppuaddr_w(reg_a);
	smb_update_screen(); return;
}

void smb_update_screen() {
	set_x(env_ppustatus_r()); // reset flip-flop
	set_y(0x00); // load first byte from indirect as a pointer
	set_a(mem_r(*(uint16_t*)&ram[0x0000] + reg_y));
	if (!reg_p.z) { smb_write_buffer_to_screen(); return; } // if byte is zero we have no further updates to make here
	smb_init_scroll(); return;
}

void smb_init_scroll() {
	env_ppuscroll_w(reg_a); // store contents of A into scroll registers
	env_ppuscroll_w(reg_a); // and end whatever subroutine led us here
	return;
	smb_write_ppu_reg_1(); return;
}

void smb_write_ppu_reg_1() {
	env_ppuctrl_w(reg_a); // write contents of A to PPU register 1
	ram[smb_mirror_ppu_ctrl_reg_1] = reg_a; // and its mirror
	return;
}

void smb_print_status_bar_numbers() {
	ram[0x0000] = reg_a; // store player-specific offset
	smb_output_numbers(); // use first nybble to print the coin display
	set_a(ram[0x0000]); // move high nybble to low
	reg_a = shr(reg_a); // and print to score display
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	smb_output_numbers(); return;
}

void smb_output_numbers() {
	reg_p.c = 0; // add 1 to low nybble
	add_a(0x01);
	and_a(0b00001111); // mask out high nybble
	cmp_a(0x06);
	if (reg_p.c) { goto exit_output_n; }
	push(reg_a); // save incremented value to stack for now and
	reg_a = shl(reg_a); // shift to left and use as offset
	set_y(reg_a);
	set_x(ram[smb_vram_buffer_1_offset]); // get current buffer pointer
	set_a(0x20); // put at top of screen by default
	cmp_y(0x00); // are we writing top score on title screen?
	if (!reg_p.z) { goto setup_nums; }
	set_a(0x22); // if so, put further down on the screen
setup_nums:;
	ram[smb_vram_buffer_1 + reg_x] = reg_a;
	set_a(rom[smb_status_bar_data + reg_y]); // write low vram address and length of thing
	ram[smb_vram_buffer_1+1 + reg_x] = reg_a; // we're printing to the buffer
	set_a(rom[smb_status_bar_data+1 + reg_y]);
	ram[smb_vram_buffer_1+2 + reg_x] = reg_a;
	ram[0x0003] = reg_a; // save length byte in counter
	ram[0x0002] = reg_x; // and buffer pointer elsewhere for now
	set_a(pull()); // pull original incremented value from stack
	set_x(reg_a);
	set_a(rom[smb_status_bar_offset + reg_x]); // load offset to value we want to write
	reg_p.c = 1;
	sub_a(rom[smb_status_bar_data+1 + reg_y]); // subtract from length byte we read before
	set_y(reg_a); // use value as offset to display digits
	set_x(ram[0x0002]);
digit_p_loop:;
	set_a(ram[smb_top_score_display + reg_y]); // write digits to the buffer
	ram[smb_vram_buffer_1+3 + reg_x] = reg_a;
	set_x(reg_x+1);
	set_y(reg_y+1);
	ram[0x0003] = dec(ram[0x0003]); // do this until all the digits are written
	if (!reg_p.z) { goto digit_p_loop; }
	set_a(0x00); // put null terminator at end
	ram[smb_vram_buffer_1+3 + reg_x] = reg_a;
	set_x(reg_x+1); // increment buffer pointer by 3
	set_x(reg_x+1);
	set_x(reg_x+1);
	ram[smb_vram_buffer_1_offset] = reg_x; // store it in case we want to use it again
exit_output_n:;
	return;
	smb_digits_math_routine(); return;
}

void smb_digits_math_routine() {
	set_a(ram[smb_oper_mode]); // check mode of operation
	cmp_a(0x00);
	if (reg_p.z) { goto erase_d_mods; } // if in title screen mode, branch to lock score
	set_x(0x05);
add_mod_loop:;
	set_a(ram[smb_digit_modifier + reg_x]); // load digit amount to increment
	reg_p.c = 0;
	add_a(ram[smb_top_score_display + reg_y]); // add to current digit
	if (reg_p.n) { goto borrow_one; } // if result is a negative number, branch to subtract
	cmp_a(0x0a);
	if (reg_p.c) { goto carry_one; } // if digit greater than $09, branch to add
store_new_d:;
	ram[smb_top_score_display + reg_y] = reg_a; // store as new score or game timer digit
	set_y(reg_y-1); // move onto next digits in score or game timer
	set_x(reg_x-1); // and digit amounts to increment
	if (!reg_p.n) { goto add_mod_loop; } // loop back if we're not done yet
erase_d_mods:;
	set_a(0x00); // store zero here
	set_x(0x06); // start with the last digit
erase_m_loop:;
	ram[smb_digit_modifier-1 + reg_x] = reg_a; // initialize the digit amounts to increment
	set_x(reg_x-1);
	if (!reg_p.n) { goto erase_m_loop; } // do this until they're all reset, then leave
	return;
borrow_one:;
	ram[smb_digit_modifier-1 + reg_x] = dec(ram[smb_digit_modifier-1 + reg_x]); // decrement the previous digit, then put $09 in
	set_a(0x09); // the game timer digit we're currently on to "borrow
	if (!reg_p.z) { goto store_new_d; } // the one", then do an unconditional branch back
carry_one:;
	reg_p.c = 1; // subtract ten from our digit to make it a
	sub_a(0x0a); // proper BCD number, then increment the digit
	ram[smb_digit_modifier-1 + reg_x] = inc(ram[smb_digit_modifier-1 + reg_x]); // preceding current digit to "carry the one" properly
	goto store_new_d; // go back to just after we branched here
	smb_update_top_score(); return;
}

void smb_update_top_score() {
	set_x(0x05); // start with mario's score
	smb_top_score_check();
	set_x(0x0b); // now do luigi's score
	smb_top_score_check(); return;
}

void smb_top_score_check() {
	set_y(0x05); // start with the lowest digit
	reg_p.c = 1;
get_score_diff:;
	set_a(ram[smb_score_and_coin_display + reg_x]); // subtract each player digit from each high score digit
	sub_a(ram[smb_top_score_display + reg_y]); // from lowest to highest, if any top score digit exceeds
	set_x(reg_x-1); // any player digit, borrow will be set until a subsequent
	set_y(reg_y-1); // subtraction clears it (player digit is higher than top)
	if (!reg_p.n) { goto get_score_diff; }
	if (!reg_p.c) { goto no_top_sc; } // check to see if borrow is still set, if so, no new high score
	set_x(reg_x+1); // increment X and Y once to the start of the score
	set_y(reg_y+1);
copy_score:;
	set_a(ram[smb_score_and_coin_display + reg_x]); // store player's score digits into high score memory area
	ram[smb_top_score_display + reg_y] = reg_a;
	set_x(reg_x+1);
	set_y(reg_y+1);
	cmp_y(0x06); // do this until we have stored them all
	if (!reg_p.c) { goto copy_score; }
no_top_sc:;
	return;
}

void smb_initialize_game() {
	set_y(0x6f); // clear all memory as in initialization procedure,
	smb_initialize_memory(); // but this time, clear only as far as $076f
	set_y(0x1f);
clr_snd_loop:;
	ram[smb_sound_memory + reg_y] = reg_a; // clear out memory used
	set_y(reg_y-1); // by the sound engines
	if (!reg_p.n) { goto clr_snd_loop; }
	set_a(0x18); // set demo timer
	ram[smb_demo_timer] = reg_a;
	smb_load_area_pointer();
	smb_initialize_area(); return;
}

void smb_initialize_area() {
	set_y(0x4b); // clear all memory again, only as far as $074b
	smb_initialize_memory(); // this is only necessary if branching from
	set_x(0x21);
	set_a(0x00);
clr_timers_loop:;
	ram[smb_timers + reg_x] = reg_a; // clear out memory between
	set_x(reg_x-1); // $0780 and $07a1
	if (!reg_p.n) { goto clr_timers_loop; }
	set_a(ram[smb_halfway_page]);
	set_y(ram[smb_alt_entrance_control]); // if smb_alt_entrance_control not set, use halfway page, if any found
	if (reg_p.z) { goto start_page; }
	set_a(ram[smb_entrance_page]); // otherwise use saved entry page number here
start_page:;
	ram[smb_screen_left_page_loc] = reg_a; // set as value here
	ram[smb_current_page_loc] = reg_a; // also set as current page
	ram[smb_backloading_flag] = reg_a; // set flag here if halfway page or saved entry page number found
	smb_get_screen_position(); // get pixel coordinates for screen borders
	set_y(0x20); // if on odd numbered page, use $2480 as start of rendering
	and_a(0b00000001); // otherwise use $2080, this address used later as name table
	if (reg_p.z) { goto set_init_nt_high; } // address for rendering of game area
	set_y(0x24);
set_init_nt_high:;
	ram[smb_current_nt_addr_high] = reg_y; // store name table address
	set_y(0x80);
	ram[smb_current_nt_addr_low] = reg_y;
	reg_a = shl(reg_a); // store LSB of page number in high nybble
	reg_a = shl(reg_a); // of block buffer column position
	reg_a = shl(reg_a);
	reg_a = shl(reg_a);
	ram[smb_block_buffer_column_pos] = reg_a;
	ram[smb_area_object_length] = dec(ram[smb_area_object_length]); // set area object lengths for all empty
	ram[smb_area_object_length+1] = dec(ram[smb_area_object_length+1]);
	ram[smb_area_object_length+2] = dec(ram[smb_area_object_length+2]);
	set_a(0x0b); // set value for renderer to update 12 column sets
	ram[smb_column_sets] = reg_a; // 12 column sets = 24 metatile columns = 1 1/2 screens
	smb_get_area_data_addrs(); // get enemy and level addresses and load header
	set_a(ram[smb_primary_hard_mode]); // check to see if primary hard mode has been activated
	if (!reg_p.z) { goto set_sec_hard; } // if so, activate the secondary no matter where we're at
	set_a(ram[smb_world_number]); // otherwise check world number
	cmp_a((smb_world_5)); // if less than 5, do not activate secondary
	if (!reg_p.c) { goto check_halfway; }
	if (!reg_p.z) { goto set_sec_hard; } // if not equal to, then world > 5, thus activate
	set_a(ram[smb_level_number]); // otherwise, world 5, so check level number
	cmp_a((smb_level_3)); // if 1 or 2, do not set secondary hard mode flag
	if (!reg_p.c) { goto check_halfway; }
set_sec_hard:;
	ram[smb_secondary_hard_mode] = inc(ram[smb_secondary_hard_mode]); // set secondary hard mode flag for areas 5-3 and beyond
check_halfway:;
	set_a(ram[smb_halfway_page]);
	if (reg_p.z) { goto done_init_area; }
	set_a(0x02); // if halfway page set, overwrite start position from header
	ram[smb_player_entrance_ctrl] = reg_a;
done_init_area:;
	set_a((smb_silence)); // silence music
	ram[smb_area_music_queue] = reg_a;
	set_a(0x01); // disable screen output
	ram[smb_disable_screen_flag] = reg_a;
	ram[smb_oper_mode_task] = inc(ram[smb_oper_mode_task]); // increment one of the modes
	return;
	smb_primary_game_setup(); return;
}

void smb_primary_game_setup() {
	set_a(0x01);
	ram[smb_fetch_new_game_timer_flag] = reg_a; // set flag to load game timer from header
	ram[smb_player_size] = reg_a; // set player's size to small
	set_a(0x02);
	ram[smb_number_of_lives] = reg_a; // give each player three lives
	ram[smb_off_scr_numberof_lives] = reg_a;
	smb_secondary_game_setup(); return;
}

void smb_secondary_game_setup() {
	set_a(0x00);
	ram[smb_disable_screen_flag] = reg_a; // enable screen output
	set_y(reg_a);
clear_vr_loop:;
	ram[smb_vram_buffer_1-1 + reg_y] = reg_a; // clear buffer at $0300-$03ff
	set_y(reg_y+1);
	if (!reg_p.z) { goto clear_vr_loop; }
	ram[smb_game_timer_expired_flag] = reg_a; // clear game timer exp flag
	ram[smb_disable_intermediate] = reg_a; // clear skip lives display flag
	ram[smb_backloading_flag] = reg_a; // clear value here
	set_a(0xff);
	ram[smb_bal_platform_alignment] = reg_a; // initialize balance platform assignment flag
	set_a(ram[smb_screen_left_page_loc]); // get left side page location
	ram[smb_mirror_ppu_ctrl_reg_1] = shr(ram[smb_mirror_ppu_ctrl_reg_1]); // shift LSB of ppu register #1 mirror out
	and_a(0x01); // mask out all but LSB of page location
	reg_a = ror(reg_a); // rotate LSB of page location into carry then onto mirror
	ram[smb_mirror_ppu_ctrl_reg_1] = rol(ram[smb_mirror_ppu_ctrl_reg_1]); // this is to set the proper PPU name table
	smb_get_area_music(); // load proper music into queue
	set_a(0x38); // load sprite shuffle amounts to be used later
	ram[smb_spr_shuffle_amt+2] = reg_a;
	set_a(0x48);
	ram[smb_spr_shuffle_amt+1] = reg_a;
	set_a(0x58);
	ram[smb_spr_shuffle_amt] = reg_a;
	set_x(0x0e); // load default OAM offsets into $06e4-$06f2
shuf_amt_loop:;
	set_a(rom[smb_default_spr_offsets + reg_x]);
	ram[smb_spr_data_offset + reg_x] = reg_a;
	set_x(reg_x-1); // do this until they're all set
	if (!reg_p.n) { goto shuf_amt_loop; }
	set_y(0x03); // set up sprite #0
i_spr_0_loop:;
	set_a(rom[smb_sprite_0_data + reg_y]);
	ram[smb_sprite_data + reg_y] = reg_a;
	set_y(reg_y-1);
	if (!reg_p.n) { goto i_spr_0_loop; }
	smb_do_nothing_2(); // these jsrs doesn't do anything useful
	smb_do_nothing_1();
	ram[smb_sprite_0_hit_detect_flag] = inc(ram[smb_sprite_0_hit_detect_flag]); // set sprite #0 check flag
	ram[smb_oper_mode_task] = inc(ram[smb_oper_mode_task]); // increment to next task
	return;
	smb_initialize_memory(); return;
}

void smb_initialize_memory() {
	set_x(0x07); // set initial high byte to $0700-$07ff
	set_a(0x00); // set initial low byte to start of page (at $00 of page)
	ram[0x0006] = reg_a;
init_page_loop:;
	ram[0x0007] = reg_x;
init_byte_loop:;
	cmp_x(0x01); // check to see if we're on the stack ($0100-$01ff)
	if (!reg_p.z) { goto init_byte; } // if not, go ahead anyway
	cmp_y(0x60); // otherwise, check to see if we're at $0160-$01ff
	if (reg_p.c) { goto skip_byte; } // if so, skip write
init_byte:;
	mem_w(*(uint16_t*)&ram[0x0006] + reg_y, reg_a); // otherwise, initialize byte with current low byte in Y
skip_byte:;
	set_y(reg_y-1);
	cmp_y(0xff); // do this until all bytes in page have been erased
	if (!reg_p.z) { goto init_byte_loop; }
	set_x(reg_x-1); // go onto the next page
	if (!reg_p.n) { goto init_page_loop; } // do this until all pages of memory have been erased
	return;
}

void smb_get_area_music() {
	set_a(ram[smb_oper_mode]); // if in title screen mode, leave
	if (reg_p.z) { goto exit_get_m; }
	set_a(ram[smb_alt_entrance_control]); // check for specific alternate mode of entry
	cmp_a(0x02); // if found, branch without checking starting position
	if (reg_p.z) { goto chk_area_type; } // from area object data header
	set_y(0x05); // select music for pipe intro scene by default
	set_a(ram[smb_player_entrance_ctrl]); // check value from level header for certain values
	cmp_a(0x06);
	if (reg_p.z) { goto store_music; } // load music for pipe intro scene if header
	cmp_a(0x07); // start position either value $06 or $07
	if (reg_p.z) { goto store_music; }
chk_area_type:;
	set_y(ram[smb_area_type]); // load area type as offset for music bit
	set_a(ram[smb_cloud_type_override]);
	if (reg_p.z) { goto store_music; } // check for cloud type override
	set_y(0x04); // select music for cloud type level if found
store_music:;
	set_a(rom[smb_music_select_data + reg_y]); // otherwise select appropriate music for level type
	ram[smb_area_music_queue] = reg_a; // store in queue and leave
exit_get_m:;
	return;
}

void smb_entrance_game_timer_setup() {
	set_a(ram[smb_screen_left_page_loc]); // set current page for area objects
	ram[smb_player_page_loc] = reg_a; // as page location for player
	set_a(0x28); // store value here
	ram[smb_vertical_force_down] = reg_a; // for fractional movement downwards if necessary
	set_a(0x01); // set high byte of player position and
	ram[smb_player_facing_dir] = reg_a; // set facing direction so that player faces right
	ram[smb_player_y_high_pos] = reg_a;
	set_a(0x00); // set player state to on the ground by default
	ram[smb_player_state] = reg_a;
	ram[smb_player_collision_bits] = dec(ram[smb_player_collision_bits]); // initialize player's collision bits
	set_y(0x00); // initialize halfway page
	ram[smb_halfway_page] = reg_y;
	set_a(ram[smb_area_type]); // check area type
	if (!reg_p.z) { goto chk_st_pos; } // if water type, set swimming flag, otherwise do not set
	set_y(reg_y+1);
chk_st_pos:;
	ram[smb_swimming_flag] = reg_y;
	set_x(ram[smb_player_entrance_ctrl]); // get starting position loaded from header
	set_y(ram[smb_alt_entrance_control]); // check alternate mode of entry flag for 0 or 1
	if (reg_p.z) { goto set_st_pos; }
	cmp_y(0x01);
	if (reg_p.z) { goto set_st_pos; }
	set_x(rom[smb_alt_y_pos_offset-2 + reg_y]); // if not 0 or 1, override $0710 with new offset in X
set_st_pos:;
	set_a(rom[smb_player_starting_x_pos + reg_y]); // load appropriate horizontal position
	ram[smb_player_x_position] = reg_a; // and vertical positions for the player, using
	set_a(rom[smb_player_starting_y_pos + reg_x]); // smb_alt_entrance_control as offset for horizontal and either $0710
	ram[smb_player_y_position] = reg_a; // or value that overwrote $0710 as offset for vertical
	set_a(rom[smb_player_bg_priority_data + reg_x]);
	ram[smb_player_spr_attrib] = reg_a; // set player sprite attributes using offset in X
	smb_get_player_colors(); // get appropriate player palette
	set_y(ram[smb_game_timer_setting]); // get timer control value from header
	if (reg_p.z) { goto chk_over_r; } // if set to zero, branch (do not use dummy byte for this)
	set_a(ram[smb_fetch_new_game_timer_flag]); // do we need to set the game timer? if not, use
	if (reg_p.z) { goto chk_over_r; } // old game timer setting
	set_a(rom[smb_game_timer_data + reg_y]); // if game timer is set and game timer flag is also set,
	ram[smb_game_timer_display] = reg_a; // use value of game timer control for first digit of game timer
	set_a(0x01);
	ram[smb_game_timer_display+2] = reg_a; // set last digit of game timer to 1
	reg_a = shr(reg_a);
	ram[smb_game_timer_display+1] = reg_a; // set second digit of game timer
	ram[smb_fetch_new_game_timer_flag] = reg_a; // clear flag for game timer reset
	ram[smb_star_invincible_timer] = reg_a; // clear star mario timer
chk_over_r:;
	set_y(ram[smb_joypad_override]); // if controller bits not set, branch to skip this part
	if (reg_p.z) { goto chk_swim_e; }
	set_a(0x03); // set player state to climbing
	ram[smb_player_state] = reg_a;
	set_x(0x00); // set offset for first slot, for block object
	smb_init_block_xy_pos();
	set_a(0xf0); // set vertical coordinate for block object
	ram[smb_block_y_position] = reg_a;
	set_x(0x05); // set offset in X for last enemy object buffer slot
	set_y(0x00); // set offset in Y for object coordinates used earlier
	smb_setup_vine(); // do a sub to grow vine
chk_swim_e:;
	set_y(ram[smb_area_type]); // if level not water-type,
	if (!reg_p.z) { goto set_pe_sub; } // skip this subroutine
	smb_setup_bubble(); // otherwise, execute sub to set up air bubbles
set_pe_sub:;
	set_a(0x07); // set to run player entrance subroutine
	ram[smb_game_engine_subroutine] = reg_a; // on the next frame of game engine
	return;
}

void smb_player_lose_life() {
	ram[smb_disable_screen_flag] = inc(ram[smb_disable_screen_flag]); // disable screen and sprite 0 check
	set_a(0x00);
	ram[smb_sprite_0_hit_detect_flag] = reg_a;
	set_a((smb_silence)); // silence music
	ram[smb_event_music_queue] = reg_a;
	ram[smb_number_of_lives] = dec(ram[smb_number_of_lives]); // take one life from player
	if (!reg_p.n) { goto still_in_game; } // if player still has lives, branch
	set_a(0x00);
	ram[smb_oper_mode_task] = reg_a; // initialize mode task,
	set_a((smb_game_over_mode_value)); // switch to game over mode
	ram[smb_oper_mode] = reg_a; // and leave
	return;
still_in_game:;
	set_a(ram[smb_world_number]); // multiply world number by 2 and use
	reg_a = shl(reg_a); // as offset
	set_x(reg_a);
	set_a(ram[smb_level_number]); // if in area -3 or -4, increment
	and_a(0x02); // offset by one byte, otherwise
	if (reg_p.z) { goto get_halfway; } // leave offset alone
	set_x(reg_x+1);
get_halfway:;
	set_y(rom[smb_halfway_page_nybbles + reg_x]); // get halfway page number with offset
	set_a(ram[smb_level_number]); // check area number's LSB
	reg_a = shr(reg_a);
	set_a(reg_y); // if in area -2 or -4, use lower nybble
	if (reg_p.c) { goto mask_hp_nyb; }
	reg_a = shr(reg_a); // move higher nybble to lower if area
	reg_a = shr(reg_a); // number is -1 or -3
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
mask_hp_nyb:;
	and_a(0b00001111); // mask out all but lower nybble
	cmp_a(ram[smb_screen_left_page_loc]);
	if (reg_p.z) { goto set_halfway; } // left side of screen must be at the halfway page,
	if (!reg_p.c) { goto set_halfway; } // otherwise player must start at the
	set_a(0x00); // beginning of the level
set_halfway:;
	ram[smb_halfway_page] = reg_a; // store as halfway page for player
	smb_transpose_players(); // switch players around if 2-player game
	smb_continue_game(); return; // continue the game
	smb_game_over_mode(); return;
}

void smb_game_over_mode() {
	set_a(ram[smb_oper_mode_task]);
	static void(*targets[])() = {
		smb_setup_game_over,
		smb_screen_routines,
		smb_run_game_over
	};
	targets[reg_a](); return;
	smb_setup_game_over(); return;
}

void smb_setup_game_over() {
	set_a(0x00); // reset screen routine task control for title screen, game,
	ram[smb_screen_routine_task] = reg_a; // and game over modes
	ram[smb_sprite_0_hit_detect_flag] = reg_a; // disable sprite 0 check
	set_a((smb_game_over_music));
	ram[smb_event_music_queue] = reg_a; // put game over music in secondary queue
	ram[smb_disable_screen_flag] = inc(ram[smb_disable_screen_flag]); // disable screen output
	ram[smb_oper_mode_task] = inc(ram[smb_oper_mode_task]); // set secondary mode to 1
	return;
	smb_run_game_over(); return;
}

void smb_run_game_over() {
	set_a(0x00); // reenable screen
	ram[smb_disable_screen_flag] = reg_a;
	set_a(ram[smb_saved_joypad_1_bits]); // check controller for start pressed
	and_a((smb_start_button));
	if (!reg_p.z) { smb_terminate_game(); return; }
	set_a(ram[smb_screen_timer]); // if not pressed, wait for
	if (!reg_p.z) { smb_game_is_on(); return; } // screen timer to expire
	smb_terminate_game(); return;
}

void smb_terminate_game() {
	set_a((smb_silence)); // silence music
	ram[smb_event_music_queue] = reg_a;
	smb_transpose_players(); // check if other player can keep
	if (!reg_p.c) { smb_continue_game(); return; } // going, and do so if possible
	set_a(ram[smb_world_number]); // otherwise put world number of current
	ram[smb_continue_world] = reg_a; // player into secret continue function variable
	set_a(0x00);
	reg_a = shl(reg_a); // residual ASL instruction
	ram[smb_oper_mode_task] = reg_a; // reset all modes to title screen and
	ram[smb_screen_timer] = reg_a; // leave
	ram[smb_oper_mode] = reg_a;
	return;
	smb_continue_game(); return;
}

void smb_continue_game() {
	smb_load_area_pointer(); // update level pointer with
	set_a(0x01); // actual world and area numbers, then
	ram[smb_player_size] = reg_a; // reset player's size, status, and
	ram[smb_fetch_new_game_timer_flag] = inc(ram[smb_fetch_new_game_timer_flag]); // set game timer flag to reload
	set_a(0x00); // game timer from header
	ram[smb_timer_control] = reg_a; // also set flag for timers to count again
	ram[smb_player_status] = reg_a;
	ram[smb_game_engine_subroutine] = reg_a; // reset task for game core
	ram[smb_oper_mode_task] = reg_a; // set modes and leave
	set_a(0x01); // if in game over mode, switch back to
	ram[smb_oper_mode] = reg_a; // game mode, because game is still on
	smb_game_is_on(); return;
}

void smb_game_is_on() {
	return;
	smb_transpose_players(); return;
}

void smb_transpose_players() {
	reg_p.c = 1; // set carry flag by default to end game
	set_a(ram[smb_number_of_players]); // if only a 1 player game, leave
	if (reg_p.z) { goto ex_trans; }
	set_a(ram[smb_off_scr_numberof_lives]); // does offscreen player have any lives left?
	if (reg_p.n) { goto ex_trans; } // branch if not
	set_a(ram[smb_current_player]); // invert bit to update
	eor_a(0b00000001); // which player is on the screen
	ram[smb_current_player] = reg_a;
	set_x(0x06);
trans_loop:;
	set_a(ram[smb_onscreen_player_info + reg_x]); // transpose the information
	push(reg_a); // of the onscreen player
	set_a(ram[smb_offscreen_player_info + reg_x]); // with that of the offscreen player
	ram[smb_onscreen_player_info + reg_x] = reg_a;
	set_a(pull());
	ram[smb_offscreen_player_info + reg_x] = reg_a;
	set_x(reg_x-1);
	if (!reg_p.n) { goto trans_loop; }
	reg_p.c = 0;
ex_trans:;
	return;
	smb_do_nothing_1(); return;
}

void smb_do_nothing_1() {
	set_a(0xff); // this is residual code, this value is
	ram[smb_unused_06_c_9] = reg_a; // not used anywhere in the program
	smb_do_nothing_2(); return;
}

void smb_do_nothing_2() {
	return;
	smb_area_parser_task_handler(); return;
}

void smb_area_parser_task_handler() {
	set_y(ram[smb_area_parser_task_num]); // check number of tasks here
	if (!reg_p.z) { goto do_ap_tasks; } // if already set, go ahead
	set_y(0x08);
	ram[smb_area_parser_task_num] = reg_y; // otherwise, set eight by default
do_ap_tasks:;
	set_y(reg_y-1);
	set_a(reg_y);
	smb_area_parser_tasks();
	ram[smb_area_parser_task_num] = dec(ram[smb_area_parser_task_num]); // if all tasks not complete do not
	if (!reg_p.z) { goto skip_at_render; } // render attribute table yet
	smb_render_attribute_tables();
skip_at_render:;
	return;
	smb_area_parser_tasks(); return;
}

void smb_area_parser_tasks() {
	static void(*targets[])() = {
		smb_increment_column_pos,
		smb_render_area_graphics,
		smb_render_area_graphics,
		smb_area_parser_core,
		smb_increment_column_pos,
		smb_render_area_graphics,
		smb_render_area_graphics,
		smb_area_parser_core
	};
	targets[reg_a](); return;
	smb_increment_column_pos(); return;
}

void smb_increment_column_pos() {
	ram[smb_current_column_pos] = inc(ram[smb_current_column_pos]); // increment column where we're at
	set_a(ram[smb_current_column_pos]);
	and_a(0b00001111); // mask out higher nybble
	if (!reg_p.z) { goto no_col_wrap; }
	ram[smb_current_column_pos] = reg_a; // if no bits left set, wrap back to zero (0-f)
	ram[smb_current_page_loc] = inc(ram[smb_current_page_loc]); // and increment page number where we're at
no_col_wrap:;
	ram[smb_block_buffer_column_pos] = inc(ram[smb_block_buffer_column_pos]); // increment column offset where we're at
	set_a(ram[smb_block_buffer_column_pos]);
	and_a(0b00011111); // mask out all but 5 LSB (0-1f)
	ram[smb_block_buffer_column_pos] = reg_a; // and save
	return;
}

void smb_area_parser_core() {
	set_a(ram[smb_backloading_flag]); // check to see if we are starting right of start
	if (reg_p.z) { goto render_scenery_terrain; } // if not, go ahead and render background, foreground and terrain
	smb_process_area_data(); // otherwise skip ahead and load level data
render_scenery_terrain:;
	set_x(0x0c);
	set_a(0x00);
clr_mt_buf:;
	ram[smb_metatile_buffer + reg_x] = reg_a; // clear out metatile buffer
	set_x(reg_x-1);
	if (!reg_p.n) { goto clr_mt_buf; }
	set_y(ram[smb_background_scenery]); // do we need to render the background scenery?
	if (reg_p.z) { goto rend_fore; } // if not, skip to check the foreground
	set_a(ram[smb_current_page_loc]); // otherwise check for every third page
third_p:;
	cmp_a(0x03);
	if (reg_p.n) { goto rend_back; } // if less than three we're there
	reg_p.c = 1;
	sub_a(0x03); // if 3 or more, subtract 3 and
	if (!reg_p.n) { goto third_p; } // do an unconditional branch
rend_back:;
	reg_a = shl(reg_a); // move results to higher nybble
	reg_a = shl(reg_a);
	reg_a = shl(reg_a);
	reg_a = shl(reg_a);
	add_a(rom[smb_b_scene_data_offsets-1 + reg_y]); // add to it offset loaded from here
	add_a(ram[smb_current_column_pos]); // add to the result our current column position
	set_x(reg_a);
	set_a(rom[smb_back_scenery_data + reg_x]); // load data from sum of offsets
	if (reg_p.z) { goto rend_fore; } // if zero, no scenery for that part
	push(reg_a);
	and_a(0x0f); // save to stack and clear high nybble
	reg_p.c = 1;
	sub_a(0x01); // subtract one (because low nybble is $01-$0c)
	ram[0x0000] = reg_a; // save low nybble
	reg_a = shl(reg_a); // multiply by three (shift to left and add result to old one)
	add_a(ram[0x0000]); // note that since d7 was nulled, the carry flag is always clear
	set_x(reg_a); // save as offset for background scenery metatile data
	set_a(pull()); // get high nybble from stack, move low
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	set_y(reg_a); // use as second offset (used to determine height)
	set_a(0x03); // use previously saved memory location for counter
	ram[0x0000] = reg_a;
sce_loop_1:;
	set_a(rom[smb_back_scenery_metatiles + reg_x]); // load metatile data from offset of (lsb - 1) * 3
	ram[smb_metatile_buffer + reg_y] = reg_a; // store into buffer from offset of (msb / 16)
	set_x(reg_x+1);
	set_y(reg_y+1);
	cmp_y(0x0b); // if at this location, leave loop
	if (reg_p.z) { goto rend_fore; }
	ram[0x0000] = dec(ram[0x0000]); // decrement until counter expires, barring exception
	if (!reg_p.z) { goto sce_loop_1; }
rend_fore:;
	set_x(ram[smb_foreground_scenery]); // check for foreground data needed or not
	if (reg_p.z) { goto rend_terr; } // if not, skip this part
	set_y(rom[smb_f_scene_data_offsets-1 + reg_x]); // load offset from location offset by header value, then
	set_x(0x00); // reinit X
sce_loop_2:;
	set_a(rom[smb_fore_scenery_data + reg_y]); // load data until counter expires
	if (reg_p.z) { goto no_fore; } // do not store if zero found
	ram[smb_metatile_buffer + reg_x] = reg_a;
no_fore:;
	set_y(reg_y+1);
	set_x(reg_x+1);
	cmp_x(0x0d); // store up to end of metatile buffer
	if (!reg_p.z) { goto sce_loop_2; }
rend_terr:;
	set_y(ram[smb_area_type]); // check world type for water level
	if (!reg_p.z) { goto ter_m_tile; } // if not water level, skip this part
	set_a(ram[smb_world_number]); // check world number, if not world number eight
	cmp_a((smb_world_8)); // then skip this part
	if (!reg_p.z) { goto ter_m_tile; }
	set_a(0x62); // if set as water level and world number eight,
	goto store_mt; // use castle wall metatile as terrain type
ter_m_tile:;
	set_a(rom[smb_terrain_metatiles + reg_y]); // otherwise get appropriate metatile for area type
	set_y(ram[smb_cloud_type_override]); // check for cloud type override
	if (reg_p.z) { goto store_mt; } // if not set, keep value otherwise
	set_a(0x88); // use cloud block terrain
store_mt:;
	ram[0x0007] = reg_a; // store value here
	set_x(0x00); // initialize X, use as metatile buffer offset
	set_a(ram[smb_terrain_control]); // use yet another value from the header
	reg_a = shl(reg_a); // multiply by 2 and use as yet another offset
	set_y(reg_a);
terr_loop:;
	set_a(rom[smb_terrain_render_bits + reg_y]); // get one of the terrain rendering bit data
	ram[0x0000] = reg_a;
	set_y(reg_y+1); // increment Y and use as offset next time around
	ram[0x0001] = reg_y;
	set_a(ram[smb_cloud_type_override]); // skip if value here is zero
	if (reg_p.z) { goto no_cloud_2; }
	cmp_x(0x00); // otherwise, check if we're doing the ceiling byte
	if (reg_p.z) { goto no_cloud_2; }
	set_a(ram[0x0000]); // if not, mask out all but d3
	and_a(0b00001000);
	ram[0x0000] = reg_a;
no_cloud_2:;
	set_y(0x00); // start at beginning of bitmasks
terr_b_chk:;
	set_a(rom[smb_bitmasks + reg_y]); // load bitmask, then perform AND on contents of first byte
	bit_a(ram[0x0000]);
	if (reg_p.z) { goto next_t_bit; } // if not set, skip this part (do not write terrain to buffer)
	set_a(ram[0x0007]);
	ram[smb_metatile_buffer + reg_x] = reg_a; // load terrain type metatile number and store into buffer here
next_t_bit:;
	set_x(reg_x+1); // continue until end of buffer
	cmp_x(0x0d);
	if (reg_p.z) { goto rend_b_buf; } // if we're at the end, break out of this loop
	set_a(ram[smb_area_type]); // check world type for underground area
	cmp_a(0x02);
	if (!reg_p.z) { goto end_u_chk; } // if not underground, skip this part
	cmp_x(0x0b);
	if (!reg_p.z) { goto end_u_chk; } // if we're at the bottom of the screen, override
	set_a(0x54); // old terrain type with ground level terrain type
	ram[0x0007] = reg_a;
end_u_chk:;
	set_y(reg_y+1); // increment bitmasks offset in Y
	cmp_y(0x08);
	if (!reg_p.z) { goto terr_b_chk; } // if not all bits checked, loop back
	set_y(ram[0x0001]);
	if (!reg_p.z) { goto terr_loop; } // unconditional branch, use Y to load next byte
rend_b_buf:;
	smb_process_area_data(); // do the area data loading routine now
	set_a(ram[smb_block_buffer_column_pos]);
	smb_get_block_buffer_addr(); // get block buffer address from where we're at
	set_x(0x00);
	set_y(0x00); // init index regs and start at beginning of smaller buffer
chk_mt_low:;
	ram[0x0000] = reg_y;
	set_a(ram[smb_metatile_buffer + reg_x]); // load stored metatile number
	and_a(0b11000000); // mask out all but 2 MSB
	reg_a = shl(reg_a);
	reg_a = rol(reg_a); // make %xx000000 into %000000xx
	reg_a = rol(reg_a);
	set_y(reg_a); // use as offset in Y
	set_a(ram[smb_metatile_buffer + reg_x]); // reload original unmasked value here
	cmp_a(rom[smb_block_buff_low_bounds + reg_y]); // check for certain values depending on bits set
	if (reg_p.c) { goto str_block; } // if equal or greater, branch
	set_a(0x00); // if less, init value before storing
str_block:;
	set_y(ram[0x0000]); // get offset for block buffer
	mem_w(*(uint16_t*)&ram[0x0006] + reg_y, reg_a); // store value into block buffer
	set_a(reg_y);
	reg_p.c = 0; // add 16 (move down one row) to offset
	add_a(0x10);
	set_y(reg_a);
	set_x(reg_x+1); // increment column value
	cmp_x(0x0d);
	if (!reg_p.c) { goto chk_mt_low; } // continue until we pass last row, then leave
	return;
}

void smb_process_area_data() {
	set_x(0x02); // start at the end of area object buffer
proc_ad_loop:;
	ram[smb_object_offset] = reg_x;
	set_a(0x00); // reset flag
	ram[smb_behind_area_parser_flag] = reg_a;
	set_y(ram[smb_area_data_offset]); // get offset of area data pointer
	set_a(mem_r(*(uint16_t*)&ram[smb_area_data] + reg_y)); // get first byte of area object
	cmp_a(0xfd); // if end-of-area, skip all this crap
	if (reg_p.z) { goto rdy_decode; }
	set_a(ram[smb_area_object_length + reg_x]); // check area object buffer flag
	if (!reg_p.n) { goto rdy_decode; } // if buffer not negative, branch, otherwise
	set_y(reg_y+1);
	set_a(mem_r(*(uint16_t*)&ram[smb_area_data] + reg_y)); // get second byte of area object
	reg_a = shl(reg_a); // check for page select bit (d7), branch if not set
	if (!reg_p.c) { goto chk_1_row_13; }
	set_a(ram[smb_area_object_page_sel]); // check page select
	if (!reg_p.z) { goto chk_1_row_13; }
	ram[smb_area_object_page_sel] = inc(ram[smb_area_object_page_sel]); // if not already set, set it now
	ram[smb_area_object_page_loc] = inc(ram[smb_area_object_page_loc]); // and increment page location
chk_1_row_13:;
	set_y(reg_y-1);
	set_a(mem_r(*(uint16_t*)&ram[smb_area_data] + reg_y)); // reread first byte of level object
	and_a(0x0f); // mask out high nybble
	cmp_a(0x0d); // row 13?
	if (!reg_p.z) { goto chk_1_row_14; }
	set_y(reg_y+1); // if so, reread second byte of level object
	set_a(mem_r(*(uint16_t*)&ram[smb_area_data] + reg_y));
	set_y(reg_y-1); // decrement to get ready to read first byte
	and_a(0b01000000); // check for d6 set (if not, object is page control)
	if (!reg_p.z) { goto check_rear; }
	set_a(ram[smb_area_object_page_sel]); // if page select is set, do not reread
	if (!reg_p.z) { goto check_rear; }
	set_y(reg_y+1); // if d6 not set, reread second byte
	set_a(mem_r(*(uint16_t*)&ram[smb_area_data] + reg_y));
	and_a(0b00011111); // mask out all but 5 LSB and store in page control
	ram[smb_area_object_page_loc] = reg_a;
	ram[smb_area_object_page_sel] = inc(ram[smb_area_object_page_sel]); // increment page select
	goto next_a_obj;
chk_1_row_14:;
	cmp_a(0x0e); // row 14?
	if (!reg_p.z) { goto check_rear; }
	set_a(ram[smb_backloading_flag]); // check flag for saved page number and branch if set
	if (!reg_p.z) { goto rdy_decode; } // to render the object (otherwise bg might not look right)
check_rear:;
	set_a(ram[smb_area_object_page_loc]); // check to see if current page of level object is
	cmp_a(ram[smb_current_page_loc]); // behind current page of renderer
	if (!reg_p.c) { goto set_behind; } // if so branch
rdy_decode:;
	smb_decode_area_data(); // do sub and do not turn on flag
	goto chk_length;
set_behind:;
	ram[smb_behind_area_parser_flag] = inc(ram[smb_behind_area_parser_flag]); // turn on flag if object is behind renderer
next_a_obj:;
	smb_inc_area_obj_offset(); // increment buffer offset and move on
chk_length:;
	set_x(ram[smb_object_offset]); // get buffer offset
	set_a(ram[smb_area_object_length + reg_x]); // check object length for anything stored here
	if (reg_p.n) { goto proc_loopb; } // if not, branch to handle loopback
	ram[smb_area_object_length + reg_x] = dec(ram[smb_area_object_length + reg_x]); // otherwise decrement length or get rid of it
proc_loopb:;
	set_x(reg_x-1); // decrement buffer offset
	if (!reg_p.n) { goto proc_ad_loop; } // and loopback unless exceeded buffer
	set_a(ram[smb_behind_area_parser_flag]); // check for flag set if objects were behind renderer
	if (!reg_p.z) { smb_process_area_data(); return; } // branch if true to load more level data, otherwise
	set_a(ram[smb_backloading_flag]); // check for flag set if starting right of page $00
	if (!reg_p.z) { smb_process_area_data(); return; } // branch if true to load more level data, otherwise leave
	smb_end_a_parse(); return;
}

void smb_end_a_parse() {
	return;
	smb_inc_area_obj_offset(); return;
}

void smb_inc_area_obj_offset() {
	ram[smb_area_data_offset] = inc(ram[smb_area_data_offset]); // increment offset of level pointer
	ram[smb_area_data_offset] = inc(ram[smb_area_data_offset]);
	set_a(0x00); // reset page select
	ram[smb_area_object_page_sel] = reg_a;
	return;
	smb_decode_area_data(); return;
}

void smb_decode_area_data() {
	set_a(ram[smb_area_object_length + reg_x]); // check current buffer flag
	if (reg_p.n) { goto chk_1_st_b; }
	set_y(ram[smb_area_obj_offset_buffer + reg_x]); // if not, get offset from buffer
chk_1_st_b:;
	set_x(0x10); // load offset of 16 for special row 15
	set_a(mem_r(*(uint16_t*)&ram[smb_area_data] + reg_y)); // get first byte of level object again
	cmp_a(0xfd);
	if (reg_p.z) { smb_end_a_parse(); return; } // if end of level, leave this routine
	and_a(0x0f); // otherwise, mask out low nybble
	cmp_a(0x0f); // row 15?
	if (reg_p.z) { goto chk_row_14; } // if so, keep the offset of 16
	set_x(0x08); // otherwise load offset of 8 for special row 12
	cmp_a(0x0c); // row 12?
	if (reg_p.z) { goto chk_row_14; } // if so, keep the offset value of 8
	set_x(0x00); // otherwise nullify value by default
chk_row_14:;
	ram[0x0007] = reg_x; // store whatever value we just loaded here
	set_x(ram[smb_object_offset]); // get object offset again
	cmp_a(0x0e); // row 14?
	if (!reg_p.z) { goto chk_row_13; }
	set_a(0x00); // if so, load offset with $00
	ram[0x0007] = reg_a;
	set_a(0x2e); // and load A with another value
	if (!reg_p.z) { goto norm_obj; } // unconditional branch
chk_row_13:;
	cmp_a(0x0d); // row 13?
	if (!reg_p.z) { goto chk_s_rows; }
	set_a(0x22); // if so, load offset with 34
	ram[0x0007] = reg_a;
	set_y(reg_y+1); // get next byte
	set_a(mem_r(*(uint16_t*)&ram[smb_area_data] + reg_y));
	and_a(0b01000000); // mask out all but d6 (page control obj bit)
	if (reg_p.z) { smb_leave_par(); return; } // if d6 clear, branch to leave (we handled this earlier)
	set_a(mem_r(*(uint16_t*)&ram[smb_area_data] + reg_y)); // otherwise, get byte again
	and_a(0b01111111); // mask out d7
	cmp_a(0x4b); // check for loop command in low nybble
	if (!reg_p.z) { goto mask_2_msb; } // (plus d6 set for object other than page control)
	ram[smb_loop_command] = inc(ram[smb_loop_command]); // if loop command, set loop command flag
mask_2_msb:;
	and_a(0b00111111); // mask out d7 and d6
	goto norm_obj; // and jump
chk_s_rows:;
	cmp_a(0x0c); // row 12-15?
	if (reg_p.c) { goto spec_obj; }
	set_y(reg_y+1); // if not, get second byte of level object
	set_a(mem_r(*(uint16_t*)&ram[smb_area_data] + reg_y));
	and_a(0b01110000); // mask out all but d6-d4
	if (!reg_p.z) { goto lrg_obj; } // if any bits set, branch to handle large object
	set_a(0x16);
	ram[0x0007] = reg_a; // otherwise set offset of 24 for small object
	set_a(mem_r(*(uint16_t*)&ram[smb_area_data] + reg_y)); // reload second byte of level object
	and_a(0b00001111); // mask out higher nybble and jump
	goto norm_obj;
lrg_obj:;
	ram[0x0000] = reg_a; // store value here (branch for large objects)
	cmp_a(0x70); // check for vertical pipe object
	if (!reg_p.z) { goto not_w_pipe; }
	set_a(mem_r(*(uint16_t*)&ram[smb_area_data] + reg_y)); // if not, reload second byte
	and_a(0b00001000); // mask out all but d3 (usage control bit)
	if (reg_p.z) { goto not_w_pipe; } // if d3 clear, branch to get original value
	set_a(0x00); // otherwise, nullify value for warp pipe
	ram[0x0000] = reg_a;
not_w_pipe:;
	set_a(ram[0x0000]); // get value and jump ahead
	goto move_ao_id;
spec_obj:;
	set_y(reg_y+1); // branch here for rows 12-15
	set_a(mem_r(*(uint16_t*)&ram[smb_area_data] + reg_y));
	and_a(0b01110000); // get next byte and mask out all but d6-d4
move_ao_id:;
	reg_a = shr(reg_a); // move d6-d4 to lower nybble
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
norm_obj:;
	ram[0x0000] = reg_a; // store value here (branch for small objects and rows 13 and 14)
	set_a(ram[smb_area_object_length + reg_x]); // is there something stored here already?
	if (!reg_p.n) { smb_run_a_obj(); return; } // if so, branch to do its particular sub
	set_a(ram[smb_area_object_page_loc]); // otherwise check to see if the object we've loaded is on the
	cmp_a(ram[smb_current_page_loc]); // same page as the renderer, and if so, branch
	if (reg_p.z) { smb_init_rear(); return; }
	set_y(ram[smb_area_data_offset]); // if not, get old offset of level pointer
	set_a(mem_r(*(uint16_t*)&ram[smb_area_data] + reg_y)); // and reload first byte
	and_a(0b00001111);
	cmp_a(0x0e); // row 14?
	if (!reg_p.z) { smb_leave_par(); return; }
	set_a(ram[smb_backloading_flag]); // if so, check backloading flag
	if (!reg_p.z) { smb_star_a_obj(); return; } // if set, branch to render object, else leave
	smb_leave_par(); return;
}

void smb_leave_par() {
	return;
	smb_init_rear(); return;
}

void smb_init_rear() {
	set_a(ram[smb_backloading_flag]); // check backloading flag to see if it's been initialized
	if (reg_p.z) { smb_back_col_c(); return; } // branch to column-wise check
	set_a(0x00); // if not, initialize both backloading and
	ram[smb_backloading_flag] = reg_a; // behind-renderer flags and leave
	ram[smb_behind_area_parser_flag] = reg_a;
	ram[smb_object_offset] = reg_a;
	smb_loop_cmd_e(); return;
}

void smb_loop_cmd_e() {
	return;
	smb_back_col_c(); return;
}

void smb_back_col_c() {
	set_y(ram[smb_area_data_offset]); // get first byte again
	set_a(mem_r(*(uint16_t*)&ram[smb_area_data] + reg_y));
	and_a(0b11110000); // mask out low nybble and move high to low
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	cmp_a(ram[smb_current_column_pos]); // is this where we're at?
	if (!reg_p.z) { smb_leave_par(); return; } // if not, branch to leave
	smb_star_a_obj(); return;
}

void smb_star_a_obj() {
	set_a(ram[smb_area_data_offset]); // if so, load area obj offset and store in buffer
	ram[smb_area_obj_offset_buffer + reg_x] = reg_a;
	smb_inc_area_obj_offset(); // do sub to increment to next object data
	smb_run_a_obj(); return;
}

void smb_run_a_obj() {
	set_a(ram[0x0000]); // get stored value and add offset to it
	reg_p.c = 0; // then use the jump engine with current contents of A
	add_a(ram[0x0007]);
	static void(*targets[])() = {
		smb_vertical_pipe,
		smb_area_style_object,
		smb_row_of_bricks,
		smb_row_of_solid_blocks,
		smb_row_of_coins,
		smb_column_of_bricks,
		smb_column_of_solid_blocks,
		smb_vertical_pipe,
		smb_hole_empty,
		smb_pulley_rope_object,
		smb_bridge_high,
		smb_bridge_middle,
		smb_bridge_low,
		smb_hole_water,
		smb_question_block_row_high,
		smb_question_block_row_low,
		smb_endless_rope,
		smb_balance_plat_rope,
		smb_castle_object,
		smb_staircase_object,
		smb_exit_pipe,
		smb_flag_balls_residual,
		smb_question_block,
		smb_question_block,
		smb_question_block,
		smb_hidden_1_up_block,
		smb_brick_with_item,
		smb_brick_with_item,
		smb_brick_with_item,
		smb_brick_with_coins,
		smb_brick_with_item,
		smb_water_pipe,
		smb_empty_block,
		smb_jump_spring,
		smb_intro_pipe,
		smb_flagpole_object,
		smb_axe_obj,
		smb_chain_obj,
		smb_castle_bridge_obj,
		smb_scroll_lock_object_warp,
		smb_scroll_lock_object,
		smb_scroll_lock_object,
		smb_area_frenzy,
		smb_area_frenzy,
		smb_area_frenzy,
		smb_loop_cmd_e,
		smb_alter_area_attributes
	};
	targets[reg_a](); return;
	smb_alter_area_attributes(); return;
}

void smb_alter_area_attributes() {
	set_y(ram[smb_area_obj_offset_buffer + reg_x]); // load offset for level object data saved in buffer
	set_y(reg_y+1); // load second byte
	set_a(mem_r(*(uint16_t*)&ram[smb_area_data] + reg_y));
	push(reg_a); // save in stack for now
	and_a(0b01000000);
	if (!reg_p.z) { goto alter_2; } // branch if d6 is set
	set_a(pull());
	push(reg_a); // pull and push offset to copy to A
	and_a(0b00001111); // mask out high nybble and store as
	ram[smb_terrain_control] = reg_a; // new terrain height type bits
	set_a(pull());
	and_a(0b00110000); // pull and mask out all but d5 and d4
	reg_a = shr(reg_a); // move bits to lower nybble and store
	reg_a = shr(reg_a); // as new background scenery bits
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	ram[smb_background_scenery] = reg_a; // then leave
	return;
alter_2:;
	set_a(pull());
	and_a(0b00000111); // mask out all but 3 LSB
	cmp_a(0x04); // if four or greater, set color control bits
	if (!reg_p.c) { goto set_fore; } // and nullify foreground scenery bits
	ram[smb_background_color_ctrl] = reg_a;
	set_a(0x00);
set_fore:;
	ram[smb_foreground_scenery] = reg_a; // otherwise set new foreground scenery bits
	return;
	smb_scroll_lock_object_warp(); return;
}

void smb_scroll_lock_object_warp() {
	set_x(0x04); // load value of 4 for game text routine as default
	set_a(ram[smb_world_number]); // warp zone (4-3-2), then check world number
	if (reg_p.z) { goto warp_num; }
	set_x(reg_x+1); // if world number > 1, increment for next warp zone (5)
	set_y(ram[smb_area_type]); // check area type
	set_y(reg_y-1);
	if (!reg_p.z) { goto warp_num; } // if ground area type, increment for last warp zone
	set_x(reg_x+1); // (8-7-6) and move on
warp_num:;
	set_a(reg_x);
	ram[smb_warp_zone_control] = reg_a; // store number here to be used by warp zone routine
	smb_write_game_text(); // print text and warp zone numbers
	set_a((smb_piranha_plant));
	smb_kill_enemies(); // load identifier for piranha plants and do sub
	smb_scroll_lock_object(); return;
}

void smb_scroll_lock_object() {
	set_a(ram[smb_scroll_lock]); // invert scroll lock to turn it on
	eor_a(0b00000001);
	ram[smb_scroll_lock] = reg_a;
	return;
	smb_kill_enemies(); return;
}

void smb_kill_enemies() {
	ram[0x0000] = reg_a; // store identifier here
	set_a(0x00);
	set_x(0x04); // check for identifier in enemy object buffer
kill_e_loop:;
	set_y(ram[smb_enemy_id + reg_x]);
	cmp_y(ram[0x0000]); // if not found, branch
	if (!reg_p.z) { goto no_kill_e; }
	ram[smb_enemy_flag + reg_x] = reg_a;
no_kill_e:;
	set_x(reg_x-1); // do this until all slots are checked
	if (!reg_p.n) { goto kill_e_loop; }
	return;
}

void smb_area_frenzy() {
	set_x(ram[0x0000]); // use area object identifier bit as offset
	set_a(rom[smb_frenzy_id_data-8 + reg_x]); // note that it starts at 8, thus weird address here
	set_y(0x05);
fre_comp_loop:;
	set_y(reg_y-1); // check regular slots of enemy object buffer
	if (reg_p.n) { goto exit_a_frenzy; } // if all slots checked and enemy object not found, branch to store
	cmp_a(ram[smb_enemy_id + reg_y]); // check for enemy object in buffer versus frenzy object
	if (!reg_p.z) { goto fre_comp_loop; }
	set_a(0x00); // if enemy object already present, nullify queue and leave
exit_a_frenzy:;
	ram[smb_enemy_frenzy_queue] = reg_a; // store enemy into frenzy queue
	return;
	smb_area_style_object(); return;
}

void smb_area_style_object() {
	set_a(ram[smb_area_style]); // load level object style and jump to the right sub
	static void(*targets[])() = {
		smb_tree_ledge,
		smb_mushroom_ledge,
		smb_bullet_bill_cannon
	};
	targets[reg_a](); return;
	smb_tree_ledge(); return;
}

void smb_tree_ledge() {
	smb_get_lrg_obj_attrib(); // get row and length of green ledge
	set_a(ram[smb_area_object_length + reg_x]); // check length counter for expiration
	if (reg_p.z) { goto end_tree_l; }
	if (!reg_p.n) { goto mid_tree_l; }
	set_a(reg_y);
	ram[smb_area_object_length + reg_x] = reg_a; // store lower nybble into buffer flag as length of ledge
	set_a(ram[smb_current_page_loc]);
	or_a(ram[smb_current_column_pos]); // are we at the start of the level?
	if (reg_p.z) { goto mid_tree_l; }
	set_a(0x16); // render start of tree ledge
	smb_no_under(); return;
mid_tree_l:;
	set_x(ram[0x0007]);
	set_a(0x17); // render middle of tree ledge
	ram[smb_metatile_buffer + reg_x] = reg_a; // note that this is also used if ledge position is
	set_a(0x4c); // at the start of level for continuous effect
	smb_all_under(); return; // now render the part underneath
end_tree_l:;
	set_a(0x18); // render end of tree ledge
	smb_no_under(); return;
	smb_mushroom_ledge(); return;
}

void smb_mushroom_ledge() {
	smb_chk_lrg_obj_length(); // get shroom dimensions
	ram[0x0006] = reg_y; // store length here for now
	if (!reg_p.c) { goto end_mush_l; }
	set_a(ram[smb_area_object_length + reg_x]); // divide length by 2 and store elsewhere
	reg_a = shr(reg_a);
	ram[smb_mushroom_ledge_half_len + reg_x] = reg_a;
	set_a(0x19); // render start of mushroom
	smb_no_under(); return;
end_mush_l:;
	set_a(0x1b); // if at the end, render end of mushroom
	set_y(ram[smb_area_object_length + reg_x]);
	if (reg_p.z) { smb_no_under(); return; }
	set_a(ram[smb_mushroom_ledge_half_len + reg_x]); // get divided length and store where length
	ram[0x0006] = reg_a; // was stored originally
	set_x(ram[0x0007]);
	set_a(0x1a);
	ram[smb_metatile_buffer + reg_x] = reg_a; // render middle of mushroom
	cmp_y(ram[0x0006]); // are we smack dab in the center?
	if (!reg_p.z) { smb_mush_l_exit(); return; } // if not, branch to leave
	set_x(reg_x+1);
	set_a(0x4f);
	ram[smb_metatile_buffer + reg_x] = reg_a; // render stem top of mushroom underneath the middle
	set_a(0x50);
	smb_all_under(); return;
}

void smb_all_under() {
	set_x(reg_x+1);
	set_y(0x0f); // set $0f to render all way down
	smb_render_under_part(); return; // now render the stem of mushroom
	smb_no_under(); return;
}

void smb_no_under() {
	set_x(ram[0x0007]); // load row of ledge
	set_y(0x00); // set 0 for no bottom on this part
	smb_render_under_part(); return;
}

void smb_pulley_rope_object() {
	smb_chk_lrg_obj_length(); // get length of pulley/rope object
	set_y(0x00); // initialize metatile offset
	if (reg_p.c) { goto render_pul; } // if starting, render left pulley
	set_y(reg_y+1);
	set_a(ram[smb_area_object_length + reg_x]); // if not at the end, render rope
	if (!reg_p.z) { goto render_pul; }
	set_y(reg_y+1); // otherwise render right pulley
render_pul:;
	set_a(rom[smb_pulley_rope_metatiles + reg_y]);
	ram[smb_metatile_buffer] = reg_a; // render at the top of the screen
	smb_mush_l_exit(); return;
}

void smb_mush_l_exit() {
	return; // and leave
}

void smb_castle_object() {
	smb_get_lrg_obj_attrib(); // save lower nybble as starting row
	ram[0x0007] = reg_y; // if starting row is above $0a, game will crash!!!
	set_y(0x04);
	smb_chk_lrg_obj_fixed_length(); // load length of castle if not already loaded
	set_a(reg_x);
	push(reg_a); // save obj buffer offset to stack
	set_y(ram[smb_area_object_length + reg_x]); // use current length as offset for castle data
	set_x(ram[0x0007]); // begin at starting row
	set_a(0x0b);
	ram[0x0006] = reg_a; // load upper limit of number of rows to print
c_rend_loop:;
	set_a(rom[smb_castle_metatiles + reg_y]); // load current byte using offset
	ram[smb_metatile_buffer + reg_x] = reg_a;
	set_x(reg_x+1); // store in buffer and increment buffer offset
	set_a(ram[0x0006]);
	if (reg_p.z) { goto chk_c_floor; } // have we reached upper limit yet?
	set_y(reg_y+1); // if not, increment column-wise
	set_y(reg_y+1); // to byte in next row
	set_y(reg_y+1);
	set_y(reg_y+1);
	set_y(reg_y+1);
	ram[0x0006] = dec(ram[0x0006]); // move closer to upper limit
chk_c_floor:;
	cmp_x(0x0b); // have we reached the row just before floor?
	if (!reg_p.z) { goto c_rend_loop; } // if not, go back and do another row
	set_a(pull());
	set_x(reg_a); // get obj buffer offset from before
	set_a(ram[smb_current_page_loc]);
	if (reg_p.z) { goto exit_castle; } // if we're at page 0, we do not need to do anything else
	set_a(ram[smb_area_object_length + reg_x]); // check length
	cmp_a(0x01); // if length almost about to expire, put brick at floor
	if (reg_p.z) { goto player_stop; }
	set_y(ram[0x0007]); // check starting row for tall castle ($00)
	if (!reg_p.z) { goto not_tall; }
	cmp_a(0x03); // if found, then check to see if we're at the second column
	if (reg_p.z) { goto player_stop; }
not_tall:;
	cmp_a(0x02); // if not tall castle, check to see if we're at the third column
	if (!reg_p.z) { goto exit_castle; } // if we aren't and the castle is tall, don't create flag yet
	smb_get_area_obj_x_position(); // otherwise, obtain and save horizontal pixel coordinate
	push(reg_a);
	smb_find_empty_enemy_slot(); // find an empty place on the enemy object buffer
	set_a(pull());
	ram[smb_enemy_x_position + reg_x] = reg_a; // then write horizontal coordinate for star flag
	set_a(ram[smb_current_page_loc]);
	ram[smb_enemy_page_loc + reg_x] = reg_a; // set page location for star flag
	set_a(0x01);
	ram[smb_enemy_y_high_pos + reg_x] = reg_a; // set vertical high byte
	ram[smb_enemy_flag + reg_x] = reg_a; // set flag for buffer
	set_a(0x90);
	ram[smb_enemy_y_position + reg_x] = reg_a; // set vertical coordinate
	set_a((smb_star_flag_object)); // set star flag value in buffer itself
	ram[smb_enemy_id + reg_x] = reg_a;
	return;
player_stop:;
	set_y(0x52); // put brick at floor to stop player at end of level
	ram[smb_metatile_buffer+0x0a] = reg_y; // this is only done if we're on the second column
exit_castle:;
	return;
	smb_water_pipe(); return;
}

void smb_water_pipe() {
	smb_get_lrg_obj_attrib(); // get row and lower nybble
	set_y(ram[smb_area_object_length + reg_x]); // get length (residual code, water pipe is 1 col thick)
	set_x(ram[0x0007]); // get row
	set_a(0x6b);
	ram[smb_metatile_buffer + reg_x] = reg_a; // draw something here and below it
	set_a(0x6c);
	ram[smb_metatile_buffer+1 + reg_x] = reg_a;
	return;
	smb_intro_pipe(); return;
}

void smb_intro_pipe() {
	set_y(0x03); // check if length set, if not set, set it
	smb_chk_lrg_obj_fixed_length();
	set_y(0x0a); // set fixed value and render the sideways part
	smb_render_sideways_pipe();
	if (reg_p.c) { goto no_blank_p; } // if carry flag set, not time to draw vertical pipe part
	set_x(0x06); // blank everything above the vertical pipe part
v_pipe_sect_loop:;
	set_a(0x00); // all the way to the top of the screen
	ram[smb_metatile_buffer + reg_x] = reg_a; // because otherwise it will look like exit pipe
	set_x(reg_x-1);
	if (!reg_p.n) { goto v_pipe_sect_loop; }
	set_a(rom[smb_vertical_pipe_data + reg_y]); // draw the end of the vertical pipe part
	ram[smb_metatile_buffer+7] = reg_a;
no_blank_p:;
	return;
}

void smb_exit_pipe() {
	set_y(0x03); // check if length set, if not set, set it
	smb_chk_lrg_obj_fixed_length();
	smb_get_lrg_obj_attrib(); // get vertical length, then plow on through smb_render_sideways_pipe
	smb_render_sideways_pipe(); return;
}

void smb_render_sideways_pipe() {
	set_y(reg_y-1); // decrement twice to make room for shaft at bottom
	set_y(reg_y-1); // and store here for now as vertical length
	ram[0x0005] = reg_y;
	set_y(ram[smb_area_object_length + reg_x]); // get length left over and store here
	ram[0x0006] = reg_y;
	set_x(ram[0x0005]); // get vertical length plus one, use as buffer offset
	set_x(reg_x+1);
	set_a(rom[smb_side_pipe_shaft_data + reg_y]); // check for value $00 based on horizontal offset
	cmp_a(0x00);
	if (reg_p.z) { goto draw_side_part; } // if found, do not draw the vertical pipe shaft
	set_x(0x00);
	set_y(ram[0x0005]); // init buffer offset and get vertical length
	smb_render_under_part(); // and render vertical shaft using tile number in A
	reg_p.c = 0; // clear carry flag to be used by smb_intro_pipe
draw_side_part:;
	set_y(ram[0x0006]); // render side pipe part at the bottom
	set_a(rom[smb_side_pipe_top_part + reg_y]);
	ram[smb_metatile_buffer + reg_x] = reg_a; // note that the pipe parts are stored
	set_a(rom[smb_side_pipe_bottom_part + reg_y]); // backwards horizontally
	ram[smb_metatile_buffer+1 + reg_x] = reg_a;
	return;
}

void smb_vertical_pipe() {
	smb_get_pipe_height();
	set_a(ram[0x0000]); // check to see if value was nullified earlier
	if (reg_p.z) { goto warp_pipe; } // (if d3, the usage control bit of second byte, was set)
	set_y(reg_y+1);
	set_y(reg_y+1);
	set_y(reg_y+1);
	set_y(reg_y+1); // add four if usage control bit was not set
warp_pipe:;
	set_a(reg_y); // save value in stack
	push(reg_a);
	set_a(ram[smb_area_number]);
	or_a(ram[smb_world_number]); // if at world 1-1, do not add piranha plant ever
	if (reg_p.z) { goto draw_pipe; }
	set_y(ram[smb_area_object_length + reg_x]); // if on second column of pipe, branch
	if (reg_p.z) { goto draw_pipe; } // (because we only need to do this once)
	smb_find_empty_enemy_slot(); // check for an empty moving data buffer space
	if (reg_p.c) { goto draw_pipe; } // if not found, too many enemies, thus skip
	smb_get_area_obj_x_position(); // get horizontal pixel coordinate
	reg_p.c = 0;
	add_a(0x08); // add eight to put the piranha plant in the center
	ram[smb_enemy_x_position + reg_x] = reg_a; // store as enemy's horizontal coordinate
	set_a(ram[smb_current_page_loc]); // add carry to current page number
	add_a(0x00);
	ram[smb_enemy_page_loc + reg_x] = reg_a; // store as enemy's page coordinate
	set_a(0x01);
	ram[smb_enemy_y_high_pos + reg_x] = reg_a;
	ram[smb_enemy_flag + reg_x] = reg_a; // activate enemy flag
	smb_get_area_obj_y_position(); // get piranha plant's vertical coordinate and store here
	ram[smb_enemy_y_position + reg_x] = reg_a;
	set_a((smb_piranha_plant)); // write piranha plant's value into buffer
	ram[smb_enemy_id + reg_x] = reg_a;
	smb_init_piranha_plant();
draw_pipe:;
	set_a(pull()); // get value saved earlier and use as Y
	set_y(reg_a);
	set_x(ram[0x0007]); // get buffer offset
	set_a(rom[smb_vertical_pipe_data + reg_y]); // draw the appropriate pipe with the Y we loaded earlier
	ram[smb_metatile_buffer + reg_x] = reg_a; // render the top of the pipe
	set_x(reg_x+1);
	set_a(rom[smb_vertical_pipe_data+2 + reg_y]); // render the rest of the pipe
	set_y(ram[0x0006]); // subtract one from length and render the part underneath
	set_y(reg_y-1);
	smb_render_under_part(); return;
	smb_get_pipe_height(); return;
}

void smb_get_pipe_height() {
	set_y(0x01); // check for length loaded, if not, load
	smb_chk_lrg_obj_fixed_length(); // pipe length of 2 (horizontal)
	smb_get_lrg_obj_attrib();
	set_a(reg_y); // get saved lower nybble as height
	and_a(0x07); // save only the three lower bits as
	ram[0x0006] = reg_a; // vertical length, then load Y with
	set_y(ram[smb_area_object_length + reg_x]); // length left over
	return;
	smb_find_empty_enemy_slot(); return;
}

void smb_find_empty_enemy_slot() {
	set_x(0x00); // start at first enemy slot
empty_chk_loop:;
	reg_p.c = 0; // clear carry flag by default
	set_a(ram[smb_enemy_flag + reg_x]); // check enemy buffer for nonzero
	if (reg_p.z) { goto exit_empty_chk; } // if zero, leave
	set_x(reg_x+1);
	cmp_x(0x05); // if nonzero, check next value
	if (!reg_p.z) { goto empty_chk_loop; }
exit_empty_chk:;
	return; // if all values nonzero, carry flag is set
	smb_hole_water(); return;
}

void smb_hole_water() {
	smb_chk_lrg_obj_length(); // get low nybble and save as length
	set_a(0x86); // render waves
	ram[smb_metatile_buffer+0x0a] = reg_a;
	set_x(0x0b);
	set_y(0x01); // now render the water underneath
	set_a(0x87);
	smb_render_under_part(); return;
	smb_question_block_row_high(); return;
}

void smb_question_block_row_high() {
	set_a(0x03); // start on the fourth row
	smb_question_block_row(); return;
}

void smb_question_block_row_low() {
	set_a(0x07); // start on the eighth row
	smb_question_block_row(); return;
}

void smb_question_block_row() {
	push(reg_a); // save whatever row to the stack for now
	smb_chk_lrg_obj_length(); // get low nybble and save as length
	set_a(pull());
	set_x(reg_a); // render question boxes with coins
	set_a(0xc0);
	ram[smb_metatile_buffer + reg_x] = reg_a;
	return;
	smb_bridge_high(); return;
}

void smb_bridge_high() {
	set_a(0x06); // start on the seventh row from top of screen
	smb_bridge(); return;
}

void smb_bridge_middle() {
	set_a(0x07); // start on the eighth row
	smb_bridge(); return;
}

void smb_bridge_low() {
	set_a(0x09); // start on the tenth row
	smb_bridge(); return;
}

void smb_bridge() {
	push(reg_a); // save whatever row to the stack for now
	smb_chk_lrg_obj_length(); // get low nybble and save as length
	set_a(pull());
	set_x(reg_a); // render bridge railing
	set_a(0x0b);
	ram[smb_metatile_buffer + reg_x] = reg_a;
	set_x(reg_x+1);
	set_y(0x00); // now render the bridge itself
	set_a(0x63);
	smb_render_under_part(); return;
	smb_flag_balls_residual(); return;
}

void smb_flag_balls_residual() {
	smb_get_lrg_obj_attrib(); // get low nybble from object byte
	set_x(0x02); // render flag balls on third row from top
	set_a(0x6d); // of screen downwards based on low nybble
	smb_render_under_part(); return;
	smb_flagpole_object(); return;
}

void smb_flagpole_object() {
	set_a(0x24); // render flagpole ball on top
	ram[smb_metatile_buffer] = reg_a;
	set_x(0x01); // now render the flagpole shaft
	set_y(0x08);
	set_a(0x25);
	smb_render_under_part();
	set_a(0x61); // render solid block at the bottom
	ram[smb_metatile_buffer+0x0a] = reg_a;
	smb_get_area_obj_x_position();
	reg_p.c = 1; // get pixel coordinate of where the flagpole is,
	sub_a(0x08); // subtract eight pixels and use as horizontal
	ram[smb_enemy_x_position+5] = reg_a; // coordinate for the flag
	set_a(ram[smb_current_page_loc]);
	sub_a(0x00); // subtract borrow from page location and use as
	ram[smb_enemy_page_loc+5] = reg_a; // page location for the flag
	set_a(0x30);
	ram[smb_enemy_y_position+5] = reg_a; // set vertical coordinate for flag
	set_a(0xb0);
	ram[smb_flagpole_f_num_y_pos] = reg_a; // set initial vertical coordinate for flagpole's floatey number
	set_a((smb_flagpole_flag_object));
	ram[smb_enemy_id+5] = reg_a; // set flag identifier, note that identifier and coordinates
	ram[smb_enemy_flag+5] = inc(ram[smb_enemy_flag+5]); // use last space in enemy object buffer
	return;
	smb_endless_rope(); return;
}

void smb_endless_rope() {
	set_x(0x00); // render rope from the top to the bottom of screen
	set_y(0x0f);
	smb_draw_rope(); return;
	smb_balance_plat_rope(); return;
}

void smb_balance_plat_rope() {
	set_a(reg_x); // save object buffer offset for now
	push(reg_a);
	set_x(0x01); // blank out all from second row to the bottom
	set_y(0x0f); // with blank used for balance platform rope
	set_a(0x44);
	smb_render_under_part();
	set_a(pull()); // get back object buffer offset
	set_x(reg_a);
	smb_get_lrg_obj_attrib(); // get vertical length from lower nybble
	set_x(0x01);
	smb_draw_rope(); return;
}

void smb_draw_rope() {
	set_a(0x40); // render the actual rope
	smb_render_under_part(); return;
}

void smb_row_of_coins() {
	set_y(ram[smb_area_type]); // get area type
	set_a(rom[smb_coin_metatile_data + reg_y]); // load appropriate coin metatile
	smb_get_row(); return;
}

void smb_castle_bridge_obj() {
	set_y(0x0c); // load length of 13 columns
	smb_chk_lrg_obj_fixed_length();
	smb_chain_obj(); return;
	smb_axe_obj(); return;
}

void smb_axe_obj() {
	set_a(0x08); // load bowser's palette into sprite portion of palette
	ram[smb_vram_buffer_addr_ctrl] = reg_a;
	smb_chain_obj(); return;
}

void smb_chain_obj() {
	set_y(ram[0x0000]); // get value loaded earlier from decoder
	set_x(rom[smb_c_object_row-2 + reg_y]); // get appropriate row and metatile for object
	set_a(rom[smb_c_object_metatile-2 + reg_y]);
	smb_col_obj(); return;
	smb_empty_block(); return;
}

void smb_empty_block() {
	smb_get_lrg_obj_attrib(); // get row location
	set_x(ram[0x0007]);
	set_a(0xc4);
	smb_col_obj(); return;
}

void smb_col_obj() {
	set_y(0x00); // column length of 1
	smb_render_under_part(); return;
}

void smb_row_of_bricks() {
	set_y(ram[smb_area_type]); // load area type obtained from area offset pointer
	set_a(ram[smb_cloud_type_override]); // check for cloud type override
	if (reg_p.z) { goto draw_bricks; }
	set_y(0x04); // if cloud type, override area type
draw_bricks:;
	set_a(rom[smb_brick_metatiles + reg_y]); // get appropriate metatile
	smb_get_row(); return; // and go render it
	smb_row_of_solid_blocks(); return;
}

void smb_row_of_solid_blocks() {
	set_y(ram[smb_area_type]); // load area type obtained from area offset pointer
	set_a(rom[smb_solid_block_metatiles + reg_y]); // get metatile
	smb_get_row(); return;
}

void smb_get_row() {
	push(reg_a); // store metatile here
	smb_chk_lrg_obj_length(); // get row number, load length
	smb_draw_row(); return;
}

void smb_draw_row() {
	set_x(ram[0x0007]);
	set_y(0x00); // set vertical height of 1
	set_a(pull());
	smb_render_under_part(); return; // render object
	smb_column_of_bricks(); return;
}

void smb_column_of_bricks() {
	set_y(ram[smb_area_type]); // load area type obtained from area offset
	set_a(rom[smb_brick_metatiles + reg_y]); // get metatile (no cloud override as for row)
	smb_get_row_2(); return;
	smb_column_of_solid_blocks(); return;
}

void smb_column_of_solid_blocks() {
	set_y(ram[smb_area_type]); // load area type obtained from area offset
	set_a(rom[smb_solid_block_metatiles + reg_y]); // get metatile
	smb_get_row_2(); return;
}

void smb_get_row_2() {
	push(reg_a); // save metatile to stack for now
	smb_get_lrg_obj_attrib(); // get length and row
	set_a(pull()); // restore metatile
	set_x(ram[0x0007]); // get starting row
	smb_render_under_part(); return; // now render the column
	smb_bullet_bill_cannon(); return;
}

void smb_bullet_bill_cannon() {
	smb_get_lrg_obj_attrib(); // get row and length of bullet bill cannon
	set_x(ram[0x0007]); // start at first row
	set_a(0x64); // render bullet bill cannon
	ram[smb_metatile_buffer + reg_x] = reg_a;
	set_x(reg_x+1);
	set_y(reg_y-1); // done yet?
	if (reg_p.n) { goto setup_cannon; }
	set_a(0x65); // if not, render middle part
	ram[smb_metatile_buffer + reg_x] = reg_a;
	set_x(reg_x+1);
	set_y(reg_y-1); // done yet?
	if (reg_p.n) { goto setup_cannon; }
	set_a(0x66); // if not, render bottom until length expires
	smb_render_under_part();
setup_cannon:;
	set_x(ram[smb_cannon_offset]); // get offset for data used by cannons and whirlpools
	smb_get_area_obj_y_position(); // get proper vertical coordinate for cannon
	ram[smb_cannon_y_position + reg_x] = reg_a; // and store it here
	set_a(ram[smb_current_page_loc]);
	ram[smb_cannon_page_loc + reg_x] = reg_a; // store page number for cannon here
	smb_get_area_obj_x_position(); // get proper horizontal coordinate for cannon
	ram[smb_cannon_x_position + reg_x] = reg_a; // and store it here
	set_x(reg_x+1);
	cmp_x(0x06); // increment and check offset
	if (!reg_p.c) { goto str_c_offset; } // if not yet reached sixth cannon, branch to save offset
	set_x(0x00); // otherwise initialize it
str_c_offset:;
	ram[smb_cannon_offset] = reg_x; // save new offset and leave
	return;
}

void smb_staircase_object() {
	smb_chk_lrg_obj_length(); // check and load length
	if (!reg_p.c) { goto next_stair; } // if length already loaded, skip init part
	set_a(0x09); // start past the end for the bottom
	ram[smb_staircase_control] = reg_a; // of the staircase
next_stair:;
	ram[smb_staircase_control] = dec(ram[smb_staircase_control]); // move onto next step (or first if starting)
	set_y(ram[smb_staircase_control]);
	set_x(rom[smb_staircase_row_data + reg_y]); // get starting row and height to render
	set_a(rom[smb_staircase_height_data + reg_y]);
	set_y(reg_a);
	set_a(0x61); // now render solid block staircase
	smb_render_under_part(); return;
	smb_jump_spring(); return;
}

void smb_jump_spring() {
	smb_get_lrg_obj_attrib();
	smb_find_empty_enemy_slot(); // find empty space in enemy object buffer
	smb_get_area_obj_x_position(); // get horizontal coordinate for jumpspring
	ram[smb_enemy_x_position + reg_x] = reg_a; // and store
	set_a(ram[smb_current_page_loc]); // store page location of jumpspring
	ram[smb_enemy_page_loc + reg_x] = reg_a;
	smb_get_area_obj_y_position(); // get vertical coordinate for jumpspring
	ram[smb_enemy_y_position + reg_x] = reg_a; // and store
	ram[smb_jumpspring_fixed_y_pos + reg_x] = reg_a; // store as permanent coordinate here
	set_a((smb_jumpspring_object));
	ram[smb_enemy_id + reg_x] = reg_a; // write jumpspring object to enemy object buffer
	set_y(0x01);
	ram[smb_enemy_y_high_pos + reg_x] = reg_y; // store vertical high byte
	ram[smb_enemy_flag + reg_x] = inc(ram[smb_enemy_flag + reg_x]); // set flag for enemy object buffer
	set_x(ram[0x0007]);
	set_a(0x67); // draw metatiles in two rows where jumpspring is
	ram[smb_metatile_buffer + reg_x] = reg_a;
	set_a(0x68);
	ram[smb_metatile_buffer+1 + reg_x] = reg_a;
	return;
	smb_hidden_1_up_block(); return;
}

void smb_hidden_1_up_block() {
	set_a(ram[smb_hidden_1_up_flag]); // if flag not set, do not render object
	if (reg_p.z) { smb_exit_dec_block(); return; }
	set_a(0x00); // if set, init for the next one
	ram[smb_hidden_1_up_flag] = reg_a;
	smb_brick_with_item(); return; // jump to code shared with unbreakable bricks
	smb_question_block(); return;
}

void smb_question_block() {
	smb_get_area_object_id(); // get value from level decoder routine
	smb_draw_q_blk(); return; // go to render it
	smb_brick_with_coins(); return;
}

void smb_brick_with_coins() {
	set_a(0x00); // initialize multi-coin timer flag
	ram[smb_brick_coin_timer_flag] = reg_a;
	smb_brick_with_item(); return;
}

void smb_brick_with_item() {
	smb_get_area_object_id(); // save area object ID
	ram[0x0007] = reg_y;
	set_a(0x00); // load default adder for bricks with lines
	set_y(ram[smb_area_type]); // check level type for ground level
	set_y(reg_y-1);
	if (reg_p.z) { goto b_with_l; } // if ground type, do not start with 5
	set_a(0x05); // otherwise use adder for bricks without lines
b_with_l:;
	reg_p.c = 0; // add object ID to adder
	add_a(ram[0x0007]);
	set_y(reg_a); // use as offset for metatile
	smb_draw_q_blk(); return;
}

void smb_draw_q_blk() {
	set_a(rom[smb_brick_q_block_metatiles + reg_y]); // get appropriate metatile for brick (question block
	push(reg_a); // if branched to here from question block routine)
	smb_get_lrg_obj_attrib(); // get row from location byte
	smb_draw_row(); return; // now render the object
	smb_get_area_object_id(); return;
}

void smb_get_area_object_id() {
	set_a(ram[0x0000]); // get value saved from area parser routine
	reg_p.c = 1;
	sub_a(0x00); // possibly residual code
	set_y(reg_a); // save to Y
	smb_exit_dec_block(); return;
}

void smb_exit_dec_block() {
	return;
}

void smb_hole_empty() {
	smb_chk_lrg_obj_length(); // get lower nybble and save as length
	if (!reg_p.c) { goto no_whirl_p; } // skip this part if length already loaded
	set_a(ram[smb_area_type]); // check for water type level
	if (!reg_p.z) { goto no_whirl_p; } // if not water type, skip this part
	set_x(ram[smb_whirlpool_offset]); // get offset for data used by cannons and whirlpools
	smb_get_area_obj_x_position(); // get proper vertical coordinate of where we're at
	reg_p.c = 1;
	sub_a(0x10); // subtract 16 pixels
	ram[smb_whirlpool_left_extent + reg_x] = reg_a; // store as left extent of whirlpool
	set_a(ram[smb_current_page_loc]); // get page location of where we're at
	sub_a(0x00); // subtract borrow
	ram[smb_whirlpool_page_loc + reg_x] = reg_a; // save as page location of whirlpool
	set_y(reg_y+1);
	set_y(reg_y+1); // increment length by 2
	set_a(reg_y);
	reg_a = shl(reg_a); // multiply by 16 to get size of whirlpool
	reg_a = shl(reg_a); // note that whirlpool will always be
	reg_a = shl(reg_a); // two blocks bigger than actual size of hole
	reg_a = shl(reg_a); // and extend one block beyond each edge
	ram[smb_whirlpool_length + reg_x] = reg_a; // save size of whirlpool here
	set_x(reg_x+1);
	cmp_x(0x05); // increment and check offset
	if (!reg_p.c) { goto str_w_offset; } // if not yet reached fifth whirlpool, branch to save offset
	set_x(0x00); // otherwise initialize it
str_w_offset:;
	ram[smb_whirlpool_offset] = reg_x; // save new offset here
no_whirl_p:;
	set_x(ram[smb_area_type]); // get appropriate metatile, then
	set_a(rom[smb_hole_metatiles + reg_x]); // render the hole proper
	set_x(0x08);
	set_y(0x0f); // start at ninth row and go to bottom, run smb_render_under_part
	smb_render_under_part(); return;
}

void smb_render_under_part() {
	ram[smb_area_object_height] = reg_y; // store vertical length to render
	set_y(ram[smb_metatile_buffer + reg_x]); // check current spot to see if there's something
	if (reg_p.z) { goto draw_this_row; } // we need to keep, if nothing, go ahead
	cmp_y(0x17);
	if (reg_p.z) { goto wait_one_row; } // if middle part (tree ledge), wait until next row
	cmp_y(0x1a);
	if (reg_p.z) { goto wait_one_row; } // if middle part (mushroom ledge), wait until next row
	cmp_y(0xc0);
	if (reg_p.z) { goto draw_this_row; } // if question block w/ coin, overwrite
	cmp_y(0xc0);
	if (reg_p.c) { goto wait_one_row; } // if any other metatile with palette 3, wait until next row
	cmp_y(0x54);
	if (!reg_p.z) { goto draw_this_row; } // if cracked rock terrain, overwrite
	cmp_a(0x50);
	if (reg_p.z) { goto wait_one_row; } // if stem top of mushroom, wait until next row
draw_this_row:;
	ram[smb_metatile_buffer + reg_x] = reg_a; // render contents of A from routine that called this
wait_one_row:;
	set_x(reg_x+1);
	cmp_x(0x0d); // stop rendering if we're at the bottom of the screen
	if (reg_p.c) { goto exit_u_part_r; }
	set_y(ram[smb_area_object_height]); // decrement, and stop rendering if there is no more length
	set_y(reg_y-1);
	if (!reg_p.n) { smb_render_under_part(); return; }
exit_u_part_r:;
	return;
	smb_chk_lrg_obj_length(); return;
}

void smb_chk_lrg_obj_length() {
	smb_get_lrg_obj_attrib(); // get row location and size (length if branched to from here)
	smb_chk_lrg_obj_fixed_length(); return;
}

void smb_chk_lrg_obj_fixed_length() {
	set_a(ram[smb_area_object_length + reg_x]); // check for set length counter
	reg_p.c = 0; // clear carry flag for not just starting
	if (!reg_p.n) { goto len_set; } // if counter not set, load it, otherwise leave alone
	set_a(reg_y); // save length into length counter
	ram[smb_area_object_length + reg_x] = reg_a;
	reg_p.c = 1; // set carry flag if just starting
len_set:;
	return;
	smb_get_lrg_obj_attrib(); return;
}

void smb_get_lrg_obj_attrib() {
	set_y(ram[smb_area_obj_offset_buffer + reg_x]); // get offset saved from area obj decoding routine
	set_a(mem_r(*(uint16_t*)&ram[smb_area_data] + reg_y)); // get first byte of level object
	and_a(0b00001111);
	ram[0x0007] = reg_a; // save row location
	set_y(reg_y+1);
	set_a(mem_r(*(uint16_t*)&ram[smb_area_data] + reg_y)); // get next byte, save lower nybble (length or height)
	and_a(0b00001111); // as Y, then leave
	set_y(reg_a);
	return;
	smb_get_area_obj_x_position(); return;
}

void smb_get_area_obj_x_position() {
	set_a(ram[smb_current_column_pos]); // multiply current offset where we're at by 16
	reg_a = shl(reg_a); // to obtain horizontal pixel coordinate
	reg_a = shl(reg_a);
	reg_a = shl(reg_a);
	reg_a = shl(reg_a);
	return;
	smb_get_area_obj_y_position(); return;
}

void smb_get_area_obj_y_position() {
	set_a(ram[0x0007]); // multiply value by 16
	reg_a = shl(reg_a);
	reg_a = shl(reg_a); // this will give us the proper vertical pixel coordinate
	reg_a = shl(reg_a);
	reg_a = shl(reg_a);
	reg_p.c = 0;
	add_a((32)); // add 32 pixels for the status bar
	return;
}

void smb_get_block_buffer_addr() {
	push(reg_a); // take value of A, save
	reg_a = shr(reg_a); // move high nybble to low
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	set_y(reg_a); // use nybble as pointer to high byte
	set_a(rom[smb_block_buffer_addr+2 + reg_y]); // of indirect here
	ram[0x0007] = reg_a;
	set_a(pull());
	and_a(0b00001111); // pull from stack, mask out high nybble
	reg_p.c = 0;
	add_a(rom[smb_block_buffer_addr + reg_y]); // add to low byte
	ram[0x0006] = reg_a; // store here and leave
	return;
}

void smb_load_area_pointer() {
	smb_find_area_pointer(); // find it and store it here
	ram[smb_area_pointer] = reg_a;
	smb_get_area_type(); return;
}

void smb_get_area_type() {
	and_a(0b01100000); // mask out all but d6 and d5
	reg_a = shl(reg_a);
	reg_a = rol(reg_a);
	reg_a = rol(reg_a);
	reg_a = rol(reg_a); // make %0xx00000 into %000000xx
	ram[smb_area_type] = reg_a; // save 2 MSB as area type
	return;
	smb_find_area_pointer(); return;
}

void smb_find_area_pointer() {
	set_y(ram[smb_world_number]); // load offset from world variable
	set_a(rom[smb_world_addr_offsets + reg_y]);
	reg_p.c = 0; // add area number used to find data
	add_a(ram[smb_area_number]);
	set_y(reg_a);
	set_a(rom[smb_area_addr_offsets + reg_y]); // from there we have our area pointer
	return;
	smb_get_area_data_addrs(); return;
}

void smb_get_area_data_addrs() {
	set_a(ram[smb_area_pointer]); // use 2 MSB for Y
	smb_get_area_type();
	set_y(reg_a);
	set_a(ram[smb_area_pointer]); // mask out all but 5 LSB
	and_a(0b00011111);
	ram[smb_area_addrs_l_offset] = reg_a; // save as low offset
	set_a(rom[smb_enemy_addr_h_offsets + reg_y]); // load base value with 2 altered MSB,
	reg_p.c = 0; // then add base value to 5 LSB, result
	add_a(ram[smb_area_addrs_l_offset]); // becomes offset for level data
	set_y(reg_a);
	set_a(rom[smb_enemy_data_addr_low + reg_y]); // use offset to load pointer
	ram[smb_enemy_data] = reg_a;
	set_a(rom[smb_enemy_data_addr_high + reg_y]);
	ram[smb_enemy_data+1] = reg_a;
	set_y(ram[smb_area_type]); // use area type as offset
	set_a(rom[smb_area_data_h_offsets + reg_y]); // do the same thing but with different base value
	reg_p.c = 0;
	add_a(ram[smb_area_addrs_l_offset]);
	set_y(reg_a);
	set_a(rom[smb_area_data_addr_low + reg_y]); // use this offset to load another pointer
	ram[smb_area_data] = reg_a;
	set_a(rom[smb_area_data_addr_high + reg_y]);
	ram[smb_area_data+1] = reg_a;
	set_y(0x00); // load first byte of header
	set_a(mem_r(*(uint16_t*)&ram[smb_area_data] + reg_y));
	push(reg_a); // save it to the stack for now
	and_a(0b00000111); // save 3 LSB for foreground scenery or bg color control
	cmp_a(0x04);
	if (!reg_p.c) { goto store_fore; }
	ram[smb_background_color_ctrl] = reg_a; // if 4 or greater, save value here as bg color control
	set_a(0x00);
store_fore:;
	ram[smb_foreground_scenery] = reg_a; // if less, save value here as foreground scenery
	set_a(pull()); // pull byte from stack and push it back
	push(reg_a);
	and_a(0b00111000); // save player entrance control bits
	reg_a = shr(reg_a); // shift bits over to LSBs
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	ram[smb_player_entrance_ctrl] = reg_a; // save value here as player entrance control
	set_a(pull()); // pull byte again but do not push it back
	and_a(0b11000000); // save 2 MSB for game timer setting
	reg_p.c = 0;
	reg_a = rol(reg_a); // rotate bits over to LSBs
	reg_a = rol(reg_a);
	reg_a = rol(reg_a);
	ram[smb_game_timer_setting] = reg_a; // save value here as game timer setting
	set_y(reg_y+1);
	set_a(mem_r(*(uint16_t*)&ram[smb_area_data] + reg_y)); // load second byte of header
	push(reg_a); // save to stack
	and_a(0b00001111); // mask out all but lower nybble
	ram[smb_terrain_control] = reg_a;
	set_a(pull()); // pull and push byte to copy it to A
	push(reg_a);
	and_a(0b00110000); // save 2 MSB for background scenery type
	reg_a = shr(reg_a);
	reg_a = shr(reg_a); // shift bits to LSBs
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	ram[smb_background_scenery] = reg_a; // save as background scenery
	set_a(pull());
	and_a(0b11000000);
	reg_p.c = 0;
	reg_a = rol(reg_a); // rotate bits over to LSBs
	reg_a = rol(reg_a);
	reg_a = rol(reg_a);
	cmp_a(0b00000011); // if set to 3, store here
	if (!reg_p.z) { goto store_style; } // and nullify other value
	ram[smb_cloud_type_override] = reg_a; // otherwise store value in other place
	set_a(0x00);
store_style:;
	ram[smb_area_style] = reg_a;
	set_a(ram[smb_area_data]); // increment area data address by 2 bytes
	reg_p.c = 0;
	add_a(0x02);
	ram[smb_area_data] = reg_a;
	set_a(ram[smb_area_data+1]);
	add_a(0x00);
	ram[smb_area_data+1] = reg_a;
	return;
}

void smb_game_mode() {
	set_a(ram[smb_oper_mode_task]);
	static void(*targets[])() = {
		smb_initialize_area,
		smb_screen_routines,
		smb_secondary_game_setup,
		smb_game_core_routine
	};
	targets[reg_a](); return;
	smb_game_core_routine(); return;
}

void smb_game_core_routine() {
	set_x(ram[smb_current_player]); // get which player is on the screen
	set_a(ram[smb_saved_joypad_1_bits + reg_x]); // use appropriate player's controller bits
	ram[smb_saved_joypad_1_bits] = reg_a; // as the master controller bits
	smb_game_routines(); // execute one of many possible subs
	set_a(ram[smb_oper_mode_task]); // check major task of operating mode
	cmp_a(0x03); // if we are supposed to be here,
	if (reg_p.c) { goto game_engine; } // branch to the game engine itself
	return;
game_engine:;
	smb_proc_fireball_bubble(); // process fireballs and air bubbles
	set_x(0x00);
proc_e_loop:;
	ram[smb_object_offset] = reg_x; // put incremented offset in X as enemy object offset
	smb_enemies_and_loops_core(); // process enemy objects
	smb_floatey_numbers_routine(); // process floatey numbers
	set_x(reg_x+1);
	cmp_x(0x06); // do these two subroutines until the whole buffer is done
	if (!reg_p.z) { goto proc_e_loop; }
	smb_get_player_offscreen_bits(); // get offscreen bits for player object
	smb_relative_player_position(); // get relative coordinates for player object
	smb_player_gfx_handler(); // draw the player
	smb_block_obj_mt_updater(); // replace block objects with metatiles if necessary
	set_x(0x01);
	ram[smb_object_offset] = reg_x; // set offset for second
	smb_block_objects_core(); // process second block object
	set_x(reg_x-1);
	ram[smb_object_offset] = reg_x; // set offset for first
	smb_block_objects_core(); // process first block object
	smb_misc_objects_core(); // process misc objects (hammer, jumping coins)
	smb_process_cannons(); // process bullet bill cannons
	smb_process_whirlpools(); // process whirlpools
	smb_flagpole_routine(); // process the flagpole
	smb_run_game_timer(); // count down the game timer
	smb_color_rotation(); // cycle one of the background colors
	set_a(ram[smb_player_y_high_pos]);
	cmp_a(0x02); // if player is below the screen, don't bother with the music
	if (!reg_p.n) { goto no_chg_mus; }
	set_a(ram[smb_star_invincible_timer]); // if star mario invincibility timer at zero,
	if (reg_p.z) { goto clr_plr_pal; } // skip this part
	cmp_a(0x04);
	if (!reg_p.z) { goto no_chg_mus; } // if not yet at a certain point, continue
	set_a(ram[smb_interval_timer_control]); // if interval timer not yet expired,
	if (!reg_p.z) { goto no_chg_mus; } // branch ahead, don't bother with the music
	smb_get_area_music(); // to re-attain appropriate level music
no_chg_mus:;
	set_y(ram[smb_star_invincible_timer]); // get invincibility timer
	set_a(ram[smb_frame_counter]); // get frame counter
	cmp_y(0x08); // if timer still above certain point,
	if (reg_p.c) { goto cycle_two; } // branch to cycle player's palette quickly
	reg_a = shr(reg_a); // otherwise, divide by 8 to cycle every eighth frame
	reg_a = shr(reg_a);
cycle_two:;
	reg_a = shr(reg_a); // if branched here, divide by 2 to cycle every other frame
	smb_cycle_player_palette(); // do sub to cycle the palette (note: shares fire flower code)
	goto save_ab; // then skip this sub to finish up the game engine
clr_plr_pal:;
	smb_reset_pal_star(); // do sub to clear player's palette bits in attributes
save_ab:;
	set_a(ram[smb_a_b_buttons]); // save current A and B button
	ram[smb_previous_a_b_buttons] = reg_a; // into temp variable to be used on next frame
	set_a(0x00);
	ram[smb_left_right_buttons] = reg_a; // nullify left and right buttons temp variable
	smb_upd_scroll_var(); return;
}

void smb_upd_scroll_var() {
	set_a(ram[smb_vram_buffer_addr_ctrl]);
	cmp_a(0x06); // if vram address controller set to 6 (one of two $0341s)
	if (reg_p.z) { goto exit_eng; } // then branch to leave
	set_a(ram[smb_area_parser_task_num]); // otherwise check number of tasks
	if (!reg_p.z) { goto run_parser; }
	set_a(ram[smb_scroll_thirty_two]); // get horizontal scroll in 0-31 or $00-$20 range
	cmp_a(0x20); // check to see if exceeded $21
	if (reg_p.n) { goto exit_eng; } // branch to leave if not
	set_a(ram[smb_scroll_thirty_two]);
	sub_a(0x20); // otherwise subtract $20 to set appropriately
	ram[smb_scroll_thirty_two] = reg_a; // and store
	set_a(0x00); // reset vram buffer offset used in conjunction with
	ram[smb_vram_buffer_2_offset] = reg_a; // level graphics buffer at $0341-$035f
run_parser:;
	smb_area_parser_task_handler(); // update the name table with more level graphics
exit_eng:;
	return; // and after all that, we're finally done!
	smb_scroll_handler(); return;
}

void smb_scroll_handler() {
	set_a(ram[smb_player_x_scroll]); // load value saved here
	reg_p.c = 0;
	add_a(ram[smb_platform_x_scroll]); // add value used by left/right platforms
	ram[smb_player_x_scroll] = reg_a; // save as new value here to impose force on scroll
	set_a(ram[smb_scroll_lock]); // check scroll lock flag
	if (!reg_p.z) { smb_init_scrl_amt(); return; } // skip a bunch of code here if set
	set_a(ram[smb_player_pos_for_scroll]);
	cmp_a(0x50); // check player's horizontal screen position
	if (!reg_p.c) { smb_init_scrl_amt(); return; } // if less than 80 pixels to the right, branch
	set_a(ram[smb_side_collision_timer]); // if timer related to player's side collision
	if (!reg_p.z) { smb_init_scrl_amt(); return; } // not expired, branch
	set_y(ram[smb_player_x_scroll]); // get value and decrement by one
	set_y(reg_y-1); // if value originally set to zero or otherwise
	if (reg_p.n) { smb_init_scrl_amt(); return; } // negative for left movement, branch
	set_y(reg_y+1);
	cmp_y(0x02); // if value $01, branch and do not decrement
	if (!reg_p.c) { goto chk_near_mid; }
	set_y(reg_y-1); // otherwise decrement by one
chk_near_mid:;
	set_a(ram[smb_player_pos_for_scroll]);
	cmp_a(0x70); // check player's horizontal screen position
	if (!reg_p.c) { smb_scroll_screen(); return; } // if less than 112 pixels to the right, branch
	set_y(ram[smb_player_x_scroll]); // otherwise get original value undecremented
	smb_scroll_screen(); return;
}

void smb_scroll_screen() {
	set_a(reg_y);
	ram[smb_scroll_amount] = reg_a; // save value here
	reg_p.c = 0;
	add_a(ram[smb_scroll_thirty_two]); // add to value already set here
	ram[smb_scroll_thirty_two] = reg_a; // save as new value here
	set_a(reg_y);
	reg_p.c = 0;
	add_a(ram[smb_screen_left_x_pos]); // add to left side coordinate
	ram[smb_screen_left_x_pos] = reg_a; // save as new left side coordinate
	ram[smb_horizontal_scroll] = reg_a; // save here also
	set_a(ram[smb_screen_left_page_loc]);
	add_a(0x00); // add carry to page location for left
	ram[smb_screen_left_page_loc] = reg_a; // side of the screen
	and_a(0x01); // get LSB of page location
	ram[0x0000] = reg_a; // save as temp variable for PPU register 1 mirror
	set_a(ram[smb_mirror_ppu_ctrl_reg_1]); // get PPU register 1 mirror
	and_a(0b11111110); // save all bits except d0
	or_a(ram[0x0000]); // get saved bit here and save in PPU register 1
	ram[smb_mirror_ppu_ctrl_reg_1] = reg_a; // mirror to be used to set name table later
	smb_get_screen_position(); // figure out where the right side is
	set_a(0x08);
	ram[smb_scroll_interval_timer] = reg_a; // set scroll timer (residual, not used elsewhere)
	smb_chk_p_offscr(); return; // skip this part
	smb_init_scrl_amt(); return;
}

void smb_init_scrl_amt() {
	set_a(0x00);
	ram[smb_scroll_amount] = reg_a; // initialize value here
	smb_chk_p_offscr(); return;
}

void smb_chk_p_offscr() {
	set_x(0x00); // set X for player offset
	smb_get_x_offscreen_bits(); // get horizontal offscreen bits for player
	ram[0x0000] = reg_a; // save them here
	set_y(0x00); // load default offset (left side)
	reg_a = shl(reg_a); // if d7 of offscreen bits are set,
	if (reg_p.c) { goto keep_onscr; } // branch with default offset
	set_y(reg_y+1); // otherwise use different offset (right side)
	set_a(ram[0x0000]);
	and_a(0b00100000); // check offscreen bits for d5 set
	if (reg_p.z) { goto init_plat_scrl; } // if not set, branch ahead of this part
keep_onscr:;
	set_a(ram[smb_screen_left_x_pos + reg_y]); // get left or right side coordinate based on offset
	reg_p.c = 1;
	sub_a(rom[smb_x_subtracter_data + reg_y]); // subtract amount based on offset
	ram[smb_player_x_position] = reg_a; // store as player position to prevent movement further
	set_a(ram[smb_screen_left_page_loc + reg_y]); // get left or right page location based on offset
	sub_a(0x00); // subtract borrow
	ram[smb_player_page_loc] = reg_a; // save as player's page location
	set_a(ram[smb_left_right_buttons]); // check saved controller bits
	cmp_a(rom[smb_offscr_joypad_bits_data + reg_y]); // against bits based on offset
	if (reg_p.z) { goto init_plat_scrl; } // if not equal, branch
	set_a(0x00);
	ram[smb_player_x_speed] = reg_a; // otherwise nullify horizontal speed of player
init_plat_scrl:;
	set_a(0x00); // nullify platform force imposed on scroll
	ram[smb_platform_x_scroll] = reg_a;
	return;
}

void smb_get_screen_position() {
	set_a(ram[smb_screen_left_x_pos]); // get coordinate of screen's left boundary
	reg_p.c = 0;
	add_a(0xff); // add 255 pixels
	ram[smb_screen_right_x_pos] = reg_a; // store as coordinate of screen's right boundary
	set_a(ram[smb_screen_left_page_loc]); // get page number where left boundary is
	add_a(0x00); // add carry from before
	ram[smb_screen_right_page_loc] = reg_a; // store as page number where right boundary is
	return;
	smb_game_routines(); return;
}

void smb_game_routines() {
	set_a(ram[smb_game_engine_subroutine]); // run routine based on number (a few of these routines are
	static void(*targets[])() = {
		smb_entrance_game_timer_setup,
		smb_vine_auto_climb,
		smb_side_exit_pipe_entry,
		smb_vertical_pipe_entry,
		smb_flagpole_slide,
		smb_player_end_level,
		smb_player_lose_life,
		smb_player_entrance,
		smb_player_ctrl_routine,
		smb_player_change_size,
		smb_player_injury_blink,
		smb_player_death,
		smb_player_fire_flower
	};
	targets[reg_a](); return;
	smb_player_entrance(); return;
}

void smb_player_entrance() {
	set_a(ram[smb_alt_entrance_control]); // check for mode of alternate entry
	cmp_a(0x02);
	if (reg_p.z) { goto entr_mode_2; } // if found, branch to enter from pipe or with vine
	set_a(0x00);
	set_y(ram[smb_player_y_position]); // if vertical position above a certain
	cmp_y(0x30); // point, nullify controller bits and continue
	if (!reg_p.c) { smb_auto_control_player(); return; } // with player movement code, do not return
	set_a(ram[smb_player_entrance_ctrl]); // check player entry bits from header
	cmp_a(0x06);
	if (reg_p.z) { goto chk_beh_pipe; } // if set to 6 or 7, execute pipe intro code
	cmp_a(0x07); // otherwise branch to normal entry
	if (!reg_p.z) { goto player_rdy; }
chk_beh_pipe:;
	set_a(ram[smb_player_spr_attrib]); // check for sprite attributes
	if (!reg_p.z) { goto intro_entr; } // branch if found
	set_a(0x01);
	smb_auto_control_player(); return; // force player to walk to the right
intro_entr:;
	smb_enter_side_pipe(); // execute sub to move player to the right
	ram[smb_change_area_timer] = dec(ram[smb_change_area_timer]); // decrement timer for change of area
	if (!reg_p.z) { goto exit_entr; } // branch to exit if not yet expired
	ram[smb_disable_intermediate] = inc(ram[smb_disable_intermediate]); // set flag to skip world and lives display
	smb_next_area(); return; // jump to increment to next area and set modes
entr_mode_2:;
	set_a(ram[smb_joypad_override]); // if controller override bits set here,
	if (!reg_p.z) { goto vine_entr; } // branch to enter with vine
	set_a(0xff); // otherwise, set value here then execute sub
	smb_move_player_y_axis(); // to move player upwards (note $ff = -1)
	set_a(ram[smb_player_y_position]); // check to see if player is at a specific coordinate
	cmp_a(0x91); // if player risen to a certain point (this requires pipes
	if (!reg_p.c) { goto player_rdy; } // to be at specific height to look/function right) branch
	return; // to the last part, otherwise leave
vine_entr:;
	set_a(ram[smb_vine_height]);
	cmp_a(0x60); // check vine height
	if (!reg_p.z) { goto exit_entr; } // if vine not yet reached maximum height, branch to leave
	set_a(ram[smb_player_y_position]); // get player's vertical coordinate
	cmp_a(0x99); // check player's vertical coordinate against preset value
	set_y(0x00); // load default values to be written to
	set_a(0x01); // this value moves player to the right off the vine
	if (!reg_p.c) { goto off_vine; } // if vertical coordinate < preset value, use defaults
	set_a(0x03);
	ram[smb_player_state] = reg_a; // otherwise set player state to climbing
	set_y(reg_y+1); // increment value in Y
	set_a(0x08); // set block in block buffer to cover hole, then
	ram[smb_block_buffer_1+0xb4] = reg_a; // use same value to force player to climb
off_vine:;
	ram[smb_disable_collision_det] = reg_y; // set collision detection disable flag
	smb_auto_control_player(); // use contents of A to move player up or right, execute sub
	set_a(ram[smb_player_x_position]);
	cmp_a(0x48); // check player's horizontal position
	if (!reg_p.c) { goto exit_entr; } // if not far enough to the right, branch to leave
player_rdy:;
	set_a(0x08); // set routine to be executed by game engine next frame
	ram[smb_game_engine_subroutine] = reg_a;
	set_a(0x01); // set to face player to the right
	ram[smb_player_facing_dir] = reg_a;
	reg_a = shr(reg_a); // init A
	ram[smb_alt_entrance_control] = reg_a; // init mode of entry
	ram[smb_disable_collision_det] = reg_a; // init collision detection disable flag
	ram[smb_joypad_override] = reg_a; // nullify controller override bits
exit_entr:;
	return; // leave!
	smb_auto_control_player(); return;
}

void smb_auto_control_player() {
	ram[smb_saved_joypad_1_bits] = reg_a; // override controller bits with contents of A if executing here
	smb_player_ctrl_routine(); return;
}

void smb_player_ctrl_routine() {
	set_a(ram[smb_game_engine_subroutine]); // check task here
	cmp_a(0x0b); // if certain value is set, branch to skip controller bit
	if (reg_p.z) { goto size_chk; }
	set_a(ram[smb_area_type]); // are we in a water type area?
	if (!reg_p.z) { goto save_joyp; } // if not, branch
	set_y(ram[smb_player_y_high_pos]);
	set_y(reg_y-1); // if not in vertical area between
	if (!reg_p.z) { goto dis_joyp; } // status bar and bottom, branch
	set_a(ram[smb_player_y_position]);
	cmp_a(0xd0); // if nearing the bottom of the screen or
	if (!reg_p.c) { goto save_joyp; } // not in the vertical area between status bar or bottom,
dis_joyp:;
	set_a(0x00); // disable controller bits
	ram[smb_saved_joypad_1_bits] = reg_a;
save_joyp:;
	set_a(ram[smb_saved_joypad_1_bits]); // otherwise store A and B buttons in $0a
	and_a(0b11000000);
	ram[smb_a_b_buttons] = reg_a;
	set_a(ram[smb_saved_joypad_1_bits]); // store left and right buttons in $0c
	and_a(0b00000011);
	ram[smb_left_right_buttons] = reg_a;
	set_a(ram[smb_saved_joypad_1_bits]); // store up and down buttons in $0b
	and_a(0b00001100);
	ram[smb_up_down_buttons] = reg_a;
	and_a(0b00000100); // check for pressing down
	if (reg_p.z) { goto size_chk; } // if not, branch
	set_a(ram[smb_player_state]); // check player's state
	if (!reg_p.z) { goto size_chk; } // if not on the ground, branch
	set_y(ram[smb_left_right_buttons]); // check left and right
	if (reg_p.z) { goto size_chk; } // if neither pressed, branch
	set_a(0x00);
	ram[smb_left_right_buttons] = reg_a; // if pressing down while on the ground,
	ram[smb_up_down_buttons] = reg_a; // nullify directional bits
size_chk:;
	smb_player_movement_subs(); // run movement subroutines
	set_y(0x01); // is player small?
	set_a(ram[smb_player_size]);
	if (!reg_p.z) { goto chk_move_dir; }
	set_y(0x00); // check for if crouching
	set_a(ram[smb_crouching_flag]);
	if (reg_p.z) { goto chk_move_dir; } // if not, branch ahead
	set_y(0x02); // if big and crouching, load y with 2
chk_move_dir:;
	ram[smb_player_bound_box_ctrl] = reg_y; // set contents of Y as player's bounding box size control
	set_a(0x01); // set moving direction to right by default
	set_y(ram[smb_player_x_speed]); // check player's horizontal speed
	if (reg_p.z) { goto player_subs; } // if not moving at all horizontally, skip this part
	if (!reg_p.n) { goto set_move_dir; } // if moving to the right, use default moving direction
	reg_a = shl(reg_a); // otherwise change to move to the left
set_move_dir:;
	ram[smb_player_moving_dir] = reg_a; // set moving direction
player_subs:;
	smb_scroll_handler(); // move the screen if necessary
	smb_get_player_offscreen_bits(); // get player's offscreen bits
	smb_relative_player_position(); // get coordinates relative to the screen
	set_x(0x00); // set offset for player object
	smb_bounding_box_core(); // get player's bounding box coordinates
	smb_player_bg_collision(); // do collision detection and process
	set_a(ram[smb_player_y_position]);
	cmp_a(0x40); // check to see if player is higher than 64th pixel
	if (!reg_p.c) { goto player_hole; } // if so, branch ahead
	set_a(ram[smb_game_engine_subroutine]);
	cmp_a(0x05); // if running end-of-level routine, branch ahead
	if (reg_p.z) { goto player_hole; }
	cmp_a(0x07); // if running player entrance routine, branch ahead
	if (reg_p.z) { goto player_hole; }
	cmp_a(0x04); // if running routines $00-$03, branch ahead
	if (!reg_p.c) { goto player_hole; }
	set_a(ram[smb_player_spr_attrib]);
	and_a(0b11011111); // otherwise nullify player's
	ram[smb_player_spr_attrib] = reg_a; // background priority flag
player_hole:;
	set_a(ram[smb_player_y_high_pos]); // check player's vertical high byte
	cmp_a(0x02); // for below the screen
	if (reg_p.n) { goto exit_ctrl; } // branch to leave if not that far down
	set_x(0x01);
	ram[smb_scroll_lock] = reg_x; // set scroll lock
	set_y(0x04);
	ram[0x0007] = reg_y; // set value here
	set_x(0x00); // use X as flag, and clear for cloud level
	set_y(ram[smb_game_timer_expired_flag]); // check game timer expiration flag
	if (!reg_p.z) { goto hole_die; } // if set, branch
	set_y(ram[smb_cloud_type_override]); // check for cloud type override
	if (!reg_p.z) { goto chk_hole_x; } // skip to last part if found
hole_die:;
	set_x(reg_x+1); // set flag in X for player death
	set_y(ram[smb_game_engine_subroutine]);
	cmp_y(0x0b); // check for some other routine running
	if (reg_p.z) { goto chk_hole_x; } // if so, branch ahead
	set_y(ram[smb_death_music_loaded]); // check value here
	if (!reg_p.z) { goto hole_bottom; } // if already set, branch to next part
	set_y(reg_y+1);
	ram[smb_event_music_queue] = reg_y; // otherwise play death music
	ram[smb_death_music_loaded] = reg_y; // and set value here
hole_bottom:;
	set_y(0x06);
	ram[0x0007] = reg_y; // change value here
chk_hole_x:;
	cmp_a(ram[0x0007]); // compare vertical high byte with value set here
	if (reg_p.n) { goto exit_ctrl; } // if less, branch to leave
	set_x(reg_x-1); // otherwise decrement flag in X
	if (reg_p.n) { goto cloud_exit; } // if flag was clear, branch to set modes and other values
	set_y(ram[smb_event_music_buffer]); // check to see if music is still playing
	if (!reg_p.z) { goto exit_ctrl; } // branch to leave if so
	set_a(0x06); // otherwise set to run lose life routine
	ram[smb_game_engine_subroutine] = reg_a; // on next frame
exit_ctrl:;
	return; // leave
cloud_exit:;
	set_a(0x00);
	ram[smb_joypad_override] = reg_a; // clear controller override bits if any are set
	smb_set_entr(); // do sub to set secondary mode
	ram[smb_alt_entrance_control] = inc(ram[smb_alt_entrance_control]); // set mode of entry to 3
	return;
	smb_vine_auto_climb(); return;
}

void smb_vine_auto_climb() {
	set_a(ram[smb_player_y_high_pos]); // check to see whether player reached position
	if (!reg_p.z) { goto auto_climb; } // above the status bar yet and if so, set modes
	set_a(ram[smb_player_y_position]);
	cmp_a(0xe4);
	if (!reg_p.c) { smb_set_entr(); return; }
auto_climb:;
	set_a(0b00001000); // set controller bits override to up
	ram[smb_joypad_override] = reg_a;
	set_y(0x03); // set player state to climbing
	ram[smb_player_state] = reg_y;
	smb_auto_control_player(); return;
	smb_set_entr(); return;
}

void smb_set_entr() {
	set_a(0x02); // set starting position to override
	ram[smb_alt_entrance_control] = reg_a;
	smb_chg_area_mode(); return; // set modes
	smb_vertical_pipe_entry(); return;
}

void smb_vertical_pipe_entry() {
	set_a(0x01); // set 1 as movement amount
	smb_move_player_y_axis(); // do sub to move player downwards
	smb_scroll_handler(); // do sub to scroll screen with saved force if necessary
	set_y(0x00); // load default mode of entry
	set_a(ram[smb_warp_zone_control]); // check warp zone control variable/flag
	if (!reg_p.z) { smb_chg_area_pipe(); return; } // if set, branch to use mode 0
	set_y(reg_y+1);
	set_a(ram[smb_area_type]); // check for castle level type
	cmp_a(0x03);
	if (!reg_p.z) { smb_chg_area_pipe(); return; } // if not castle type level, use mode 1
	set_y(reg_y+1);
	smb_chg_area_pipe(); return; // otherwise use mode 2
	smb_move_player_y_axis(); return;
}

void smb_move_player_y_axis() {
	reg_p.c = 0;
	add_a(ram[smb_player_y_position]); // add contents of A to player position
	ram[smb_player_y_position] = reg_a;
	return;
	smb_side_exit_pipe_entry(); return;
}

void smb_side_exit_pipe_entry() {
	smb_enter_side_pipe(); // execute sub to move player to the right
	set_y(0x02);
	smb_chg_area_pipe(); return;
}

void smb_chg_area_pipe() {
	ram[smb_change_area_timer] = dec(ram[smb_change_area_timer]); // decrement timer for change of area
	if (!reg_p.z) { smb_exit_ca_pipe(); return; }
	ram[smb_alt_entrance_control] = reg_y; // when timer expires set mode of alternate entry
	smb_chg_area_mode(); return;
}

void smb_chg_area_mode() {
	ram[smb_disable_screen_flag] = inc(ram[smb_disable_screen_flag]); // set flag to disable screen output
	set_a(0x00);
	ram[smb_oper_mode_task] = reg_a; // set secondary mode of operation
	ram[smb_sprite_0_hit_detect_flag] = reg_a; // disable sprite 0 check
	smb_exit_ca_pipe(); return;
}

void smb_exit_ca_pipe() {
	return; // leave
	smb_enter_side_pipe(); return;
}

void smb_enter_side_pipe() {
	set_a(0x08); // set player's horizontal speed
	ram[smb_player_x_speed] = reg_a;
	set_y(0x01); // set controller right button by default
	set_a(ram[smb_player_x_position]); // mask out higher nybble of player's
	and_a(0b00001111); // horizontal position
	if (!reg_p.z) { goto right_pipe; }
	ram[smb_player_x_speed] = reg_a; // if lower nybble = 0, set as horizontal speed
	set_y(reg_a); // and nullify controller bit override here
right_pipe:;
	set_a(reg_y); // use contents of Y to
	smb_auto_control_player(); // execute player control routine with ctrl bits nulled
	return;
	smb_player_change_size(); return;
}

void smb_player_change_size() {
	set_a(ram[smb_timer_control]); // check master timer control
	cmp_a(0xf8); // for specific moment in time
	if (!reg_p.z) { goto end_chg_size; } // branch if before or after that point
	smb_init_change_size(); return; // otherwise run code to get growing/shrinking going
end_chg_size:;
	cmp_a(0xc4); // check again for another specific moment
	if (!reg_p.z) { goto exit_chg_size; } // and branch to leave if before or after that point
	smb_done_player_task(); // otherwise do sub to init timer control and set routine
exit_chg_size:;
	return; // and then leave
	smb_player_injury_blink(); return;
}

void smb_player_injury_blink() {
	set_a(ram[smb_timer_control]); // check master timer control
	cmp_a(0xf0); // for specific moment in time
	if (reg_p.c) { goto exit_blink; } // branch if before that point
	cmp_a(0xc8); // check again for another specific point
	if (reg_p.z) { smb_done_player_task(); return; } // branch if at that point, and not before or after
	smb_player_ctrl_routine(); return; // otherwise run player control routine
exit_blink:;
	if (!reg_p.z) { smb_exit_both(); return; } // do unconditional branch to leave
	smb_init_change_size(); return;
}

void smb_init_change_size() {
	set_y(ram[smb_player_change_size_flag]); // if growing/shrinking flag already set
	if (!reg_p.z) { smb_exit_both(); return; } // then branch to leave
	ram[smb_player_anim_ctrl] = reg_y; // otherwise initialize player's animation frame control
	ram[smb_player_change_size_flag] = inc(ram[smb_player_change_size_flag]); // set growing/shrinking flag
	set_a(ram[smb_player_size]);
	eor_a(0x01); // invert player's size
	ram[smb_player_size] = reg_a;
	smb_exit_both(); return;
}

void smb_exit_both() {
	return; // leave
	smb_player_death(); return;
}

void smb_player_death() {
	set_a(ram[smb_timer_control]); // check master timer control
	cmp_a(0xf0); // for specific moment in time
	if (reg_p.c) { smb_exit_death(); return; } // branch to leave if before that point
	smb_player_ctrl_routine(); return; // otherwise run player control routine
	smb_done_player_task(); return;
}

void smb_done_player_task() {
	set_a(0x00);
	ram[smb_timer_control] = reg_a; // initialize master timer control to continue timers
	set_a(0x08);
	ram[smb_game_engine_subroutine] = reg_a; // set player control routine to run next frame
	return; // leave
	smb_player_fire_flower(); return;
}

void smb_player_fire_flower() {
	set_a(ram[smb_timer_control]); // check master timer control
	cmp_a(0xc0); // for specific moment in time
	if (reg_p.z) { smb_reset_pal_fire_flower(); return; } // branch if at moment, not before or after
	set_a(ram[smb_frame_counter]); // get frame counter
	reg_a = shr(reg_a);
	reg_a = shr(reg_a); // divide by four to change every four frames
	smb_cycle_player_palette(); return;
}

void smb_cycle_player_palette() {
	and_a(0x03); // mask out all but d1-d0 (previously d3-d2)
	ram[0x0000] = reg_a; // store result here to use as palette bits
	set_a(ram[smb_player_spr_attrib]); // get player attributes
	and_a(0b11111100); // save any other bits but palette bits
	or_a(ram[0x0000]); // add palette bits
	ram[smb_player_spr_attrib] = reg_a; // store as new player attributes
	return; // and leave
	smb_reset_pal_fire_flower(); return;
}

void smb_reset_pal_fire_flower() {
	smb_done_player_task(); // do sub to init timer control and run player control routine
	smb_reset_pal_star(); return;
}

void smb_reset_pal_star() {
	set_a(ram[smb_player_spr_attrib]); // get player attributes
	and_a(0b11111100); // mask out palette bits to force palette 0
	ram[smb_player_spr_attrib] = reg_a; // store as new player attributes
	return; // and leave
	smb_exit_death(); return;
}

void smb_exit_death() {
	return; // leave from death routine
	smb_flagpole_slide(); return;
}

void smb_flagpole_slide() {
	set_a(ram[smb_enemy_id+5]); // check special use enemy slot
	cmp_a((smb_flagpole_flag_object)); // for flagpole flag object
	if (!reg_p.z) { goto no_fp_obj; } // if not found, branch to something residual
	set_a(ram[smb_flagpole_sound_queue]); // load flagpole sound
	ram[smb_square_1_sound_queue] = reg_a; // into square 1's sfx queue
	set_a(0x00);
	ram[smb_flagpole_sound_queue] = reg_a; // init flagpole sound queue
	set_y(ram[smb_player_y_position]);
	cmp_y(0x9e); // check to see if player has slid down
	if (reg_p.c) { goto slide_player; } // far enough, and if so, branch with no controller bits set
	set_a(0x04); // otherwise force player to climb down (to slide)
slide_player:;
	smb_auto_control_player(); return; // jump to player control routine
no_fp_obj:;
	ram[smb_game_engine_subroutine] = inc(ram[smb_game_engine_subroutine]); // increment to next routine (this may
	return; // be residual code)
}

void smb_player_end_level() {
	set_a(0x01); // force player to walk to the right
	smb_auto_control_player();
	set_a(ram[smb_player_y_position]); // check player's vertical position
	cmp_a(0xae);
	if (!reg_p.c) { goto chk_stop; } // if player is not yet off the flagpole, skip this part
	set_a(ram[smb_scroll_lock]); // if scroll lock not set, branch ahead to next part
	if (reg_p.z) { goto chk_stop; } // because we only need to do this part once
	set_a((smb_end_of_level_music));
	ram[smb_event_music_queue] = reg_a; // load win level music in event music queue
	set_a(0x00);
	ram[smb_scroll_lock] = reg_a; // turn off scroll lock to skip this part later
chk_stop:;
	set_a(ram[smb_player_collision_bits]); // get player collision bits
	reg_a = shr(reg_a); // check for d0 set
	if (reg_p.c) { goto rdy_next_a; } // if d0 set, skip to next part
	set_a(ram[smb_star_flag_task_control]); // if star flag task control already set,
	if (!reg_p.z) { goto in_castle; } // go ahead with the rest of the code
	ram[smb_star_flag_task_control] = inc(ram[smb_star_flag_task_control]); // otherwise set task control now (this gets ball rolling!)
in_castle:;
	set_a(0b00100000); // set player's background priority bit to
	ram[smb_player_spr_attrib] = reg_a; // give illusion of being inside the castle
rdy_next_a:;
	set_a(ram[smb_star_flag_task_control]);
	cmp_a(0x05); // if star flag task control not yet set
	if (!reg_p.z) { smb_exit_na(); return; } // beyond last valid task number, branch to leave
	ram[smb_level_number] = inc(ram[smb_level_number]); // increment level number used for game logic
	set_a(ram[smb_level_number]);
	cmp_a(0x03); // check to see if we have yet reached level -4
	if (!reg_p.z) { smb_next_area(); return; } // and skip this last part here if not
	set_y(ram[smb_world_number]); // get world number as offset
	set_a(ram[smb_coin_tally_for_1_ups]); // check third area coin tally for bonus 1-ups
	cmp_a(rom[smb_hidden_1_up_coin_amts + reg_y]); // against minimum value, if player has not collected
	if (!reg_p.c) { smb_next_area(); return; } // at least this number of coins, leave flag clear
	ram[smb_hidden_1_up_flag] = inc(ram[smb_hidden_1_up_flag]); // otherwise set hidden 1-up box control flag
	smb_next_area(); return;
}

void smb_next_area() {
	ram[smb_area_number] = inc(ram[smb_area_number]); // increment area number used for address loader
	smb_load_area_pointer(); // get new level pointer
	ram[smb_fetch_new_game_timer_flag] = inc(ram[smb_fetch_new_game_timer_flag]); // set flag to load new game timer
	smb_chg_area_mode(); // do sub to set secondary mode, disable screen and sprite 0
	ram[smb_halfway_page] = reg_a; // reset halfway page to 0 (beginning)
	set_a((smb_silence));
	ram[smb_event_music_queue] = reg_a; // silence music and leave
	smb_exit_na(); return;
}

void smb_exit_na() {
	return;
	smb_player_movement_subs(); return;
}

void smb_player_movement_subs() {
	set_a(0x00); // set A to init crouch flag by default
	set_y(ram[smb_player_size]); // is player small?
	if (!reg_p.z) { goto set_crouch; } // if so, branch
	set_a(ram[smb_player_state]); // check state of player
	if (!reg_p.z) { goto proc_move; } // if not on the ground, branch
	set_a(ram[smb_up_down_buttons]); // load controller bits for up and down
	and_a(0b00000100); // single out bit for down button
set_crouch:;
	ram[smb_crouching_flag] = reg_a; // store value in crouch flag
proc_move:;
	smb_player_physics_sub(); // run sub related to jumping and swimming
	set_a(ram[smb_player_change_size_flag]); // if growing/shrinking flag set,
	if (!reg_p.z) { goto no_move_sub; } // branch to leave
	set_a(ram[smb_player_state]);
	cmp_a(0x03); // get player state
	if (reg_p.z) { goto move_subs; } // if climbing, branch ahead, leave timer unset
	set_y(0x18);
	ram[smb_climb_slide_timer] = reg_y; // otherwise reset timer now
move_subs:;
	static void(*targets[])() = {
		smb_on_ground_state_sub,
		smb_jump_swim_sub,
		smb_falling_sub,
		smb_climbing_sub
	};
	targets[reg_a](); return;
no_move_sub:;
	return;
	smb_on_ground_state_sub(); return;
}

void smb_on_ground_state_sub() {
	smb_get_player_anim_speed(); // do a sub to set animation frame timing
	set_a(ram[smb_left_right_buttons]);
	if (reg_p.z) { goto gnd_move; } // if left/right controller bits not set, skip instruction
	ram[smb_player_facing_dir] = reg_a; // otherwise set new facing direction
gnd_move:;
	smb_impose_friction(); // do a sub to impose friction on player's walk/run
	smb_move_player_horizontally(); // do another sub to move player horizontally
	ram[smb_player_x_scroll] = reg_a; // set returned value as player's movement speed for scroll
	return;
	smb_falling_sub(); return;
}

void smb_falling_sub() {
	set_a(ram[smb_vertical_force_down]);
	ram[smb_vertical_force] = reg_a; // dump vertical movement force for falling into main one
	smb_lr_air(); return; // movement force, then skip ahead to process left/right movement
	smb_jump_swim_sub(); return;
}

void smb_jump_swim_sub() {
	set_y(ram[smb_player_y_speed]); // if player's vertical speed zero
	if (!reg_p.n) { goto dump_fall; } // or moving downwards, branch to falling
	set_a(ram[smb_a_b_buttons]);
	and_a((smb_a_button)); // check to see if A button is being pressed
	and_a(ram[smb_previous_a_b_buttons]); // and was pressed in previous frame
	if (!reg_p.z) { goto proc_swim; } // if so, branch elsewhere
	set_a(ram[smb_jump_origin_y_position]); // get vertical position player jumped from
	reg_p.c = 1;
	sub_a(ram[smb_player_y_position]); // subtract current from original vertical coordinate
	cmp_a(ram[smb_diff_to_halt_jump]); // compare to value set here to see if player is in mid-jump
	if (!reg_p.c) { goto proc_swim; } // or just starting to jump, if just starting, skip ahead
dump_fall:;
	set_a(ram[smb_vertical_force_down]); // otherwise dump falling into main fractional
	ram[smb_vertical_force] = reg_a;
proc_swim:;
	set_a(ram[smb_swimming_flag]); // if swimming flag not set,
	if (reg_p.z) { smb_lr_air(); return; } // branch ahead to last part
	smb_get_player_anim_speed(); // do a sub to get animation frame timing
	set_a(ram[smb_player_y_position]);
	cmp_a(0x14); // check vertical position against preset value
	if (reg_p.c) { goto lr_water; } // if not yet reached a certain position, branch ahead
	set_a(0x18);
	ram[smb_vertical_force] = reg_a; // otherwise set fractional
lr_water:;
	set_a(ram[smb_left_right_buttons]); // check left/right controller bits (check for swimming)
	if (reg_p.z) { smb_lr_air(); return; } // if not pressing any, skip
	ram[smb_player_facing_dir] = reg_a; // otherwise set facing direction accordingly
	smb_lr_air(); return;
}

void smb_lr_air() {
	set_a(ram[smb_left_right_buttons]); // check left/right controller bits (check for jumping/falling)
	if (reg_p.z) { goto js_move; } // if not pressing any, skip
	smb_impose_friction(); // otherwise process horizontal movement
js_move:;
	smb_move_player_horizontally(); // do a sub to move player horizontally
	ram[smb_player_x_scroll] = reg_a; // set player's speed here, to be used for scroll later
	set_a(ram[smb_game_engine_subroutine]);
	cmp_a(0x0b); // check for specific routine selected
	if (!reg_p.z) { goto exit_mov_1; } // branch if not set to run
	set_a(0x28);
	ram[smb_vertical_force] = reg_a; // otherwise set fractional
exit_mov_1:;
	smb_move_player_vertically(); return; // jump to move player vertically, then leave
}

void smb_climbing_sub() {
	set_a(ram[smb_player_ymf_dummy]);
	reg_p.c = 0; // add movement force to dummy variable
	add_a(ram[smb_player_y_move_force]); // save with carry
	ram[smb_player_ymf_dummy] = reg_a;
	set_y(0x00); // set default adder here
	set_a(ram[smb_player_y_speed]); // get player's vertical speed
	if (!reg_p.n) { goto move_on_vine; } // if not moving upwards, branch
	set_y(reg_y-1); // otherwise set adder to $ff
move_on_vine:;
	ram[0x0000] = reg_y; // store adder here
	add_a(ram[smb_player_y_position]); // add carry to player's vertical position
	ram[smb_player_y_position] = reg_a; // and store to move player up or down
	set_a(ram[smb_player_y_high_pos]);
	add_a(ram[0x0000]); // add carry to player's page location
	ram[smb_player_y_high_pos] = reg_a; // and store
	set_a(ram[smb_left_right_buttons]); // compare left/right controller bits
	and_a(ram[smb_player_collision_bits]); // to collision flag
	if (reg_p.z) { goto init_cs_timer; } // if not set, skip to end
	set_y(ram[smb_climb_slide_timer]); // otherwise check timer
	if (!reg_p.z) { goto exit_c_sub; } // if timer not expired, branch to leave
	set_y(0x18);
	ram[smb_climb_slide_timer] = reg_y; // otherwise set timer now
	set_x(0x00); // set default offset here
	set_y(ram[smb_player_facing_dir]); // get facing direction
	reg_a = shr(reg_a); // move right button controller bit to carry
	if (reg_p.c) { goto climb_fd; } // if controller right pressed, branch ahead
	set_x(reg_x+1);
	set_x(reg_x+1); // otherwise increment offset by 2 bytes
climb_fd:;
	set_y(reg_y-1); // check to see if facing right
	if (reg_p.z) { goto c_set_f_dir; } // if so, branch, do not increment
	set_x(reg_x+1); // otherwise increment by 1 byte
c_set_f_dir:;
	set_a(ram[smb_player_x_position]);
	reg_p.c = 0; // add or subtract from player's horizontal position
	add_a(rom[smb_climb_adder_low + reg_x]); // using value here as adder and X as offset
	ram[smb_player_x_position] = reg_a;
	set_a(ram[smb_player_page_loc]); // add or subtract carry or borrow using value here
	add_a(rom[smb_climb_adder_high + reg_x]); // from the player's page location
	ram[smb_player_page_loc] = reg_a;
	set_a(ram[smb_left_right_buttons]); // get left/right controller bits again
	eor_a(0b00000011); // invert them and store them while player
	ram[smb_player_facing_dir] = reg_a; // is on vine to face player in opposite direction
exit_c_sub:;
	return; // then leave
init_cs_timer:;
	ram[smb_climb_slide_timer] = reg_a; // initialize timer here
	return;
}

void smb_player_physics_sub() {
	set_a(ram[smb_player_state]); // check player state
	cmp_a(0x03);
	if (!reg_p.z) { goto check_for_jumping; } // if not climbing, branch
	set_y(0x00);
	set_a(ram[smb_up_down_buttons]); // get controller bits for up/down
	and_a(ram[smb_player_collision_bits]); // check against player's collision detection bits
	if (reg_p.z) { goto proc_climb; } // if not pressing up or down, branch
	set_y(reg_y+1);
	and_a(0b00001000); // check for pressing up
	if (!reg_p.z) { goto proc_climb; }
	set_y(reg_y+1);
proc_climb:;
	set_x(rom[smb_climb_y_m_force_data + reg_y]); // load value here
	ram[smb_player_y_move_force] = reg_x; // store as vertical movement force
	set_a(0x08); // load default animation timing
	set_x(rom[smb_climb_y_speed_data + reg_y]); // load some other value here
	ram[smb_player_y_speed] = reg_x; // store as vertical speed
	if (reg_p.n) { goto set_c_anim; } // if climbing down, use default animation timing value
	reg_a = shr(reg_a); // otherwise divide timer setting by 2
set_c_anim:;
	ram[smb_player_anim_timer_set] = reg_a; // store animation timer setting and leave
	return;
check_for_jumping:;
	set_a(ram[smb_jumpspring_anim_ctrl]); // if jumpspring animating,
	if (!reg_p.z) { goto no_jump; } // skip ahead to something else
	set_a(ram[smb_a_b_buttons]); // check for A button press
	and_a((smb_a_button));
	if (reg_p.z) { goto no_jump; } // if not, branch to something else
	and_a(ram[smb_previous_a_b_buttons]); // if button not pressed in previous frame, branch
	if (reg_p.z) { goto proc_jumping; }
no_jump:;
	goto x_physics; // otherwise, jump to something else
proc_jumping:;
	set_a(ram[smb_player_state]); // check player state
	if (reg_p.z) { goto init_js; } // if on the ground, branch
	set_a(ram[smb_swimming_flag]); // if swimming flag not set, jump to do something else
	if (reg_p.z) { goto no_jump; } // to prevent midair jumping, otherwise continue
	set_a(ram[smb_jump_swim_timer]); // if jump/swim timer nonzero, branch
	if (!reg_p.z) { goto init_js; }
	set_a(ram[smb_player_y_speed]); // check player's vertical speed
	if (!reg_p.n) { goto init_js; } // if player's vertical speed motionless or down, branch
	goto x_physics; // if timer at zero and player still rising, do not swim
init_js:;
	set_a(0x20); // set jump/swim timer
	ram[smb_jump_swim_timer] = reg_a;
	set_y(0x00); // initialize vertical force and dummy variable
	ram[smb_player_ymf_dummy] = reg_y;
	ram[smb_player_y_move_force] = reg_y;
	set_a(ram[smb_player_y_high_pos]); // get vertical high and low bytes of jump origin
	ram[smb_jump_origin_y_high_pos] = reg_a; // and store them next to each other here
	set_a(ram[smb_player_y_position]);
	ram[smb_jump_origin_y_position] = reg_a;
	set_a(0x01); // set player state to jumping/swimming
	ram[smb_player_state] = reg_a;
	set_a(ram[smb_player_x_speed_absolute]); // check value related to walking/running speed
	cmp_a(0x09);
	if (!reg_p.c) { goto chk_wtr; } // branch if below certain values, increment Y
	set_y(reg_y+1); // for each amount equal or exceeded
	cmp_a(0x10);
	if (!reg_p.c) { goto chk_wtr; }
	set_y(reg_y+1);
	cmp_a(0x19);
	if (!reg_p.c) { goto chk_wtr; }
	set_y(reg_y+1);
	cmp_a(0x1c);
	if (!reg_p.c) { goto chk_wtr; } // note that for jumping, range is 0-4 for Y
	set_y(reg_y+1);
chk_wtr:;
	set_a(0x01); // set value here (apparently always set to 1)
	ram[smb_diff_to_halt_jump] = reg_a;
	set_a(ram[smb_swimming_flag]); // if swimming flag disabled, branch
	if (reg_p.z) { goto get_y_phy; }
	set_y(0x05); // otherwise set Y to 5, range is 5-6
	set_a(ram[smb_whirlpool_flag]); // if whirlpool flag not set, branch
	if (reg_p.z) { goto get_y_phy; }
	set_y(reg_y+1); // otherwise increment to 6
get_y_phy:;
	set_a(rom[smb_jump_m_force_data + reg_y]); // store appropriate jump/swim
	ram[smb_vertical_force] = reg_a; // data here
	set_a(rom[smb_fall_m_force_data + reg_y]);
	ram[smb_vertical_force_down] = reg_a;
	set_a(rom[smb_init_m_force_data + reg_y]);
	ram[smb_player_y_move_force] = reg_a;
	set_a(rom[smb_player_y_spd_data + reg_y]);
	ram[smb_player_y_speed] = reg_a;
	set_a(ram[smb_swimming_flag]); // if swimming flag disabled, branch
	if (reg_p.z) { goto p_jump_snd; }
	set_a((smb_sfx_enemy_stomp)); // load swim/goomba stomp sound into
	ram[smb_square_1_sound_queue] = reg_a; // square 1's sfx queue
	set_a(ram[smb_player_y_position]);
	cmp_a(0x14); // check vertical low byte of player position
	if (reg_p.c) { goto x_physics; } // if below a certain point, branch
	set_a(0x00); // otherwise reset player's vertical speed
	ram[smb_player_y_speed] = reg_a; // and jump to something else to keep player
	goto x_physics; // from swimming above water level
p_jump_snd:;
	set_a((smb_sfx_big_jump)); // load big mario's jump sound by default
	set_y(ram[smb_player_size]); // is mario big?
	if (reg_p.z) { goto s_jump_snd; }
	set_a((smb_sfx_small_jump)); // if not, load small mario's jump sound
s_jump_snd:;
	ram[smb_square_1_sound_queue] = reg_a; // store appropriate jump sound in square 1 sfx queue
x_physics:;
	set_y(0x00);
	ram[0x0000] = reg_y; // init value here
	set_a(ram[smb_player_state]); // if mario is on the ground, branch
	if (reg_p.z) { goto proc_p_run; }
	set_a(ram[smb_player_x_speed_absolute]); // check something that seems to be related
	cmp_a(0x19); // to mario's speed
	if (reg_p.c) { goto get_x_phy; } // if =>$19 branch here
	if (!reg_p.c) { goto chk_r_fast; } // if not branch elsewhere
proc_p_run:;
	set_y(reg_y+1); // if mario on the ground, increment Y
	set_a(ram[smb_area_type]); // check area type
	if (reg_p.z) { goto chk_r_fast; } // if water type, branch
	set_y(reg_y-1); // decrement Y by default for non-water type area
	set_a(ram[smb_left_right_buttons]); // get left/right controller bits
	cmp_a(ram[smb_player_moving_dir]); // check against moving direction
	if (!reg_p.z) { goto chk_r_fast; } // if controller bits <> moving direction, skip this part
	set_a(ram[smb_a_b_buttons]); // check for b button pressed
	and_a((smb_b_button));
	if (!reg_p.z) { goto set_r_tmr; } // if pressed, skip ahead to set timer
	set_a(ram[smb_running_timer]); // check for running timer set
	if (!reg_p.z) { goto get_x_phy; } // if set, branch
chk_r_fast:;
	set_y(reg_y+1); // if running timer not set or level type is water,
	ram[0x0000] = inc(ram[0x0000]); // increment Y again and temp variable in memory
	set_a(ram[smb_running_speed]);
	if (!reg_p.z) { goto fast_x_sp; } // if running speed set here, branch
	set_a(ram[smb_player_x_speed_absolute]);
	cmp_a(0x21); // otherwise check player's walking/running speed
	if (!reg_p.c) { goto get_x_phy; } // if less than a certain amount, branch ahead
fast_x_sp:;
	ram[0x0000] = inc(ram[0x0000]); // if running speed set or speed => $21 increment $00
	goto get_x_phy; // and jump ahead
set_r_tmr:;
	set_a(0x0a); // if b button pressed, set running timer
	ram[smb_running_timer] = reg_a;
get_x_phy:;
	set_a(rom[smb_max_left_x_spd_data + reg_y]); // get maximum speed to the left
	ram[smb_maximum_left_speed] = reg_a;
	set_a(ram[smb_game_engine_subroutine]); // check for specific routine running
	cmp_a(0x07); // (player entrance)
	if (!reg_p.z) { goto get_x_phy_2; } // if not running, skip and use old value of Y
	set_y(0x03); // otherwise set Y to 3
get_x_phy_2:;
	set_a(rom[smb_max_right_x_spd_data + reg_y]); // get maximum speed to the right
	ram[smb_maximum_right_speed] = reg_a;
	set_y(ram[0x0000]); // get other value in memory
	set_a(rom[smb_friction_data + reg_y]); // get value using value in memory as offset
	ram[smb_friction_adder_low] = reg_a;
	set_a(0x00);
	ram[smb_friction_adder_high] = reg_a; // init something here
	set_a(ram[smb_player_facing_dir]);
	cmp_a(ram[smb_player_moving_dir]); // check facing direction against moving direction
	if (reg_p.z) { goto exit_phy; } // if the same, branch to leave
	ram[smb_friction_adder_low] = shl(ram[smb_friction_adder_low]); // otherwise shift d7 of friction adder low into carry
	ram[smb_friction_adder_high] = rol(ram[smb_friction_adder_high]); // then rotate carry onto d0 of friction adder high
exit_phy:;
	return; // and then leave
}

void smb_get_player_anim_speed() {
	set_y(0x00); // initialize offset in Y
	set_a(ram[smb_player_x_speed_absolute]); // check player's walking/running speed
	cmp_a(0x1c); // against preset amount
	if (reg_p.c) { goto set_run_spd; } // if greater than a certain amount, branch ahead
	set_y(reg_y+1); // otherwise increment Y
	cmp_a(0x0e); // compare against lower amount
	if (reg_p.c) { goto chk_skid; } // if greater than this but not greater than first, skip increment
	set_y(reg_y+1); // otherwise increment Y again
chk_skid:;
	set_a(ram[smb_saved_joypad_1_bits]); // get controller bits
	and_a(0b01111111); // mask out A button
	if (reg_p.z) { goto set_anim_spd; } // if no other buttons pressed, branch ahead of all this
	and_a(0x03); // mask out all others except left and right
	cmp_a(ram[smb_player_moving_dir]); // check against moving direction
	if (!reg_p.z) { goto proc_skid; } // if left/right controller bits <> moving direction, branch
	set_a(0x00); // otherwise set zero value here
set_run_spd:;
	ram[smb_running_speed] = reg_a; // store zero or running speed here
	goto set_anim_spd;
proc_skid:;
	set_a(ram[smb_player_x_speed_absolute]); // check player's walking/running speed
	cmp_a(0x0b); // against one last amount
	if (reg_p.c) { goto set_anim_spd; } // if greater than this amount, branch
	set_a(ram[smb_player_facing_dir]);
	ram[smb_player_moving_dir] = reg_a; // otherwise use facing direction to set moving direction
	set_a(0x00);
	ram[smb_player_x_speed] = reg_a; // nullify player's horizontal speed
	ram[smb_player_x_move_force] = reg_a; // and dummy variable for player
set_anim_spd:;
	set_a(rom[smb_player_anim_tmr_data + reg_y]); // get animation timer setting using Y as offset
	ram[smb_player_anim_timer_set] = reg_a;
	return;
	smb_impose_friction(); return;
}

void smb_impose_friction() {
	and_a(ram[smb_player_collision_bits]); // perform AND between left/right controller bits and collision flag
	cmp_a(0x00); // then compare to zero (this instruction is redundant)
	if (!reg_p.z) { goto joyp_frict; } // if any bits set, branch to next part
	set_a(ram[smb_player_x_speed]);
	if (reg_p.z) { goto set_abs_spd; } // if player has no horizontal speed, branch ahead to last part
	if (!reg_p.n) { goto rght_frict; } // if player moving to the right, branch to slow
	if (reg_p.n) { goto left_frict; } // otherwise logic dictates player moving left, branch to slow
joyp_frict:;
	reg_a = shr(reg_a); // put right controller bit into carry
	if (!reg_p.c) { goto rght_frict; } // if left button pressed, carry = 0, thus branch
left_frict:;
	set_a(ram[smb_player_x_move_force]); // load value set here
	reg_p.c = 0;
	add_a(ram[smb_friction_adder_low]); // add to it another value set here
	ram[smb_player_x_move_force] = reg_a; // store here
	set_a(ram[smb_player_x_speed]);
	add_a(ram[smb_friction_adder_high]); // add value plus carry to horizontal speed
	ram[smb_player_x_speed] = reg_a; // set as new horizontal speed
	cmp_a(ram[smb_maximum_right_speed]); // compare against maximum value for right movement
	if (reg_p.n) { goto x_spd_sign; } // if horizontal speed greater negatively, branch
	set_a(ram[smb_maximum_right_speed]); // otherwise set preset value as horizontal speed
	ram[smb_player_x_speed] = reg_a; // thus slowing the player's left movement down
	goto set_abs_spd; // skip to the end
rght_frict:;
	set_a(ram[smb_player_x_move_force]); // load value set here
	reg_p.c = 1;
	sub_a(ram[smb_friction_adder_low]); // subtract from it another value set here
	ram[smb_player_x_move_force] = reg_a; // store here
	set_a(ram[smb_player_x_speed]);
	sub_a(ram[smb_friction_adder_high]); // subtract value plus borrow from horizontal speed
	ram[smb_player_x_speed] = reg_a; // set as new horizontal speed
	cmp_a(ram[smb_maximum_left_speed]); // compare against maximum value for left movement
	if (!reg_p.n) { goto x_spd_sign; } // if horizontal speed greater positively, branch
	set_a(ram[smb_maximum_left_speed]); // otherwise set preset value as horizontal speed
	ram[smb_player_x_speed] = reg_a; // thus slowing the player's right movement down
x_spd_sign:;
	cmp_a(0x00); // if player not moving or moving to the right,
	if (!reg_p.n) { goto set_abs_spd; } // branch and leave horizontal speed value unmodified
	eor_a(0xff);
	reg_p.c = 0; // otherwise get two's compliment to get absolute
	add_a(0x01); // unsigned walking/running speed
set_abs_spd:;
	ram[smb_player_x_speed_absolute] = reg_a; // store walking/running speed here and leave
	return;
	smb_proc_fireball_bubble(); return;
}

void smb_proc_fireball_bubble() {
	set_a(ram[smb_player_status]); // check player's status
	cmp_a(0x02);
	if (!reg_p.c) { goto proc_air_bubbles; } // if not fiery, branch
	set_a(ram[smb_a_b_buttons]);
	and_a((smb_b_button)); // check for b button pressed
	if (reg_p.z) { goto proc_fireballs; } // branch if not pressed
	and_a(ram[smb_previous_a_b_buttons]);
	if (!reg_p.z) { goto proc_fireballs; } // if button pressed in previous frame, branch
	set_a(ram[smb_fireball_counter]); // load fireball counter
	and_a(0b00000001); // get LSB and use as offset for buffer
	set_x(reg_a);
	set_a(ram[smb_fireball_state + reg_x]); // load fireball state
	if (!reg_p.z) { goto proc_fireballs; } // if not inactive, branch
	set_y(ram[smb_player_y_high_pos]); // if player too high or too low, branch
	set_y(reg_y-1);
	if (!reg_p.z) { goto proc_fireballs; }
	set_a(ram[smb_crouching_flag]); // if player crouching, branch
	if (!reg_p.z) { goto proc_fireballs; }
	set_a(ram[smb_player_state]); // if player's state = climbing, branch
	cmp_a(0x03);
	if (reg_p.z) { goto proc_fireballs; }
	set_a((smb_sfx_fireball)); // play fireball sound effect
	ram[smb_square_1_sound_queue] = reg_a;
	set_a(0x02); // load state
	ram[smb_fireball_state + reg_x] = reg_a;
	set_y(ram[smb_player_anim_timer_set]); // copy animation frame timer setting
	ram[smb_fireball_throwing_timer] = reg_y; // into fireball throwing timer
	set_y(reg_y-1);
	ram[smb_player_anim_timer] = reg_y; // decrement and store in player's animation timer
	ram[smb_fireball_counter] = inc(ram[smb_fireball_counter]); // increment fireball counter
proc_fireballs:;
	set_x(0x00);
	smb_fireball_obj_core(); // process first fireball object
	set_x(0x01);
	smb_fireball_obj_core(); // process second fireball object, then do air bubbles
proc_air_bubbles:;
	set_a(ram[smb_area_type]); // if not water type level, skip the rest of this
	if (!reg_p.z) { goto bubl_exit; }
	set_x(0x02); // otherwise load counter and use as offset
bubl_loop:;
	ram[smb_object_offset] = reg_x; // store offset
	smb_bubble_check(); // check timers and coordinates, create air bubble
	smb_relative_bubble_position(); // get relative coordinates
	smb_get_bubble_offscreen_bits(); // get offscreen information
	smb_draw_bubble(); // draw the air bubble
	set_x(reg_x-1);
	if (!reg_p.n) { goto bubl_loop; } // do this until all three are handled
bubl_exit:;
	return; // then leave
}

void smb_fireball_obj_core() {
	ram[smb_object_offset] = reg_x; // store offset as current object
	set_a(ram[smb_fireball_state + reg_x]); // check for d7 = 1
	reg_a = shl(reg_a);
	if (reg_p.c) { goto fireball_explosion; } // if so, branch to get relative coordinates and draw explosion
	set_y(ram[smb_fireball_state + reg_x]); // if fireball inactive, branch to leave
	if (reg_p.z) { goto no_f_ball; }
	set_y(reg_y-1); // if fireball state set to 1, skip this part and just run it
	if (reg_p.z) { goto run_fb; }
	set_a(ram[smb_player_x_position]); // get player's horizontal position
	add_a(0x04); // add four pixels and store as fireball's horizontal position
	ram[smb_fireball_x_position + reg_x] = reg_a;
	set_a(ram[smb_player_page_loc]); // get player's page location
	add_a(0x00); // add carry and store as fireball's page location
	ram[smb_fireball_page_loc + reg_x] = reg_a;
	set_a(ram[smb_player_y_position]); // get player's vertical position and store
	ram[smb_fireball_y_position + reg_x] = reg_a;
	set_a(0x01); // set high byte of vertical position
	ram[smb_fireball_y_high_pos + reg_x] = reg_a;
	set_y(ram[smb_player_facing_dir]); // get player's facing direction
	set_y(reg_y-1); // decrement to use as offset here
	set_a(rom[smb_fireball_x_spd_data + reg_y]); // set horizontal speed of fireball accordingly
	ram[smb_fireball_x_speed + reg_x] = reg_a;
	set_a(0x04); // set vertical speed of fireball
	ram[smb_fireball_y_speed + reg_x] = reg_a;
	set_a(0x07);
	ram[smb_fireball_bound_box_ctrl + reg_x] = reg_a; // set bounding box size control for fireball
	ram[smb_fireball_state + reg_x] = dec(ram[smb_fireball_state + reg_x]); // decrement state to 1 to skip this part from now on
run_fb:;
	set_a(reg_x); // add 7 to offset to use
	reg_p.c = 0; // as fireball offset for next routines
	add_a(0x07);
	set_x(reg_a);
	set_a(0x50); // set downward movement force here
	ram[0x0000] = reg_a;
	set_a(0x03); // set maximum speed here
	ram[0x0002] = reg_a;
	set_a(0x00);
	smb_impose_gravity(); // do sub here to impose gravity on fireball and move vertically
	smb_move_object_horizontally(); // do another sub to move it horizontally
	set_x(ram[smb_object_offset]); // return fireball offset to X
	smb_relative_fireball_position(); // get relative coordinates
	smb_get_fireball_offscreen_bits(); // get offscreen information
	smb_get_fireball_bound_box(); // get bounding box coordinates
	smb_fireball_bg_collision(); // do fireball to background collision detection
	set_a(ram[smb_f_ball_offscreen_bits]); // get fireball offscreen bits
	and_a(0b11001100); // mask out certain bits
	if (!reg_p.z) { goto erase_fb; } // if any bits still set, branch to kill fireball
	smb_fireball_enemy_collision(); // do fireball to enemy collision detection and deal with collisions
	smb_draw_fireball(); return; // draw fireball appropriately and leave
erase_fb:;
	set_a(0x00); // erase fireball state
	ram[smb_fireball_state + reg_x] = reg_a;
no_f_ball:;
	return; // leave
fireball_explosion:;
	smb_relative_fireball_position();
	smb_draw_explosion_fireball(); return;
	smb_bubble_check(); return;
}

void smb_bubble_check() {
	set_a(ram[smb_pseudo_random_bit_reg+1 + reg_x]); // get part of LSFR
	and_a(0x01);
	ram[0x0007] = reg_a; // store pseudorandom bit here
	set_a(ram[smb_bubble_y_position + reg_x]); // get vertical coordinate for air bubble
	cmp_a(0xf8); // if offscreen coordinate not set,
	if (!reg_p.z) { smb_move_bubl(); return; } // branch to move air bubble
	set_a(ram[smb_air_bubble_timer]); // if air bubble timer not expired,
	if (!reg_p.z) { smb_exit_bubl(); return; } // branch to leave, otherwise create new air bubble
	smb_setup_bubble(); return;
}

void smb_setup_bubble() {
	set_y(0x00); // load default value here
	set_a(ram[smb_player_facing_dir]); // get player's facing direction
	reg_a = shr(reg_a); // move d0 to carry
	if (!reg_p.c) { goto pos_bubl; } // branch to use default value if facing left
	set_y(0x08); // otherwise load alternate value here
pos_bubl:;
	set_a(reg_y); // use value loaded as adder
	add_a(ram[smb_player_x_position]); // add to player's horizontal position
	ram[smb_bubble_x_position + reg_x] = reg_a; // save as horizontal position for airbubble
	set_a(ram[smb_player_page_loc]);
	add_a(0x00); // add carry to player's page location
	ram[smb_bubble_page_loc + reg_x] = reg_a; // save as page location for airbubble
	set_a(ram[smb_player_y_position]);
	reg_p.c = 0; // add eight pixels to player's vertical position
	add_a(0x08);
	ram[smb_bubble_y_position + reg_x] = reg_a; // save as vertical position for air bubble
	set_a(0x01);
	ram[smb_bubble_y_high_pos + reg_x] = reg_a; // set vertical high byte for air bubble
	set_y(ram[0x0007]); // get pseudorandom bit, use as offset
	set_a(rom[smb_bubble_timer_data + reg_y]); // get data for air bubble timer
	ram[smb_air_bubble_timer] = reg_a; // set air bubble timer
	smb_move_bubl(); return;
}

void smb_move_bubl() {
	set_y(ram[0x0007]); // get pseudorandom bit again, use as offset
	set_a(ram[smb_bubble_ymf_dummy + reg_x]);
	reg_p.c = 1; // subtract pseudorandom amount from dummy variable
	sub_a(rom[smb_bubble_m_force_data + reg_y]);
	ram[smb_bubble_ymf_dummy + reg_x] = reg_a; // save dummy variable
	set_a(ram[smb_bubble_y_position + reg_x]);
	sub_a(0x00); // subtract borrow from airbubble's vertical coordinate
	cmp_a(0x20); // if below the status bar,
	if (reg_p.c) { goto y_bubl; } // branch to go ahead and use to move air bubble upwards
	set_a(0xf8); // otherwise set offscreen coordinate
y_bubl:;
	ram[smb_bubble_y_position + reg_x] = reg_a; // store as new vertical coordinate for air bubble
	smb_exit_bubl(); return;
}

void smb_exit_bubl() {
	return; // leave
}

void smb_run_game_timer() {
	set_a(ram[smb_oper_mode]); // get primary mode of operation
	if (reg_p.z) { smb_ex_g_timer(); return; } // branch to leave if in title screen mode
	set_a(ram[smb_game_engine_subroutine]);
	cmp_a(0x08); // if routine number less than eight running,
	if (!reg_p.c) { smb_ex_g_timer(); return; } // branch to leave
	cmp_a(0x0b); // if running death routine,
	if (reg_p.z) { smb_ex_g_timer(); return; } // branch to leave
	set_a(ram[smb_player_y_high_pos]);
	cmp_a(0x02); // if player below the screen,
	if (reg_p.c) { smb_ex_g_timer(); return; } // branch to leave regardless of level type
	set_a(ram[smb_game_timer_ctrl_timer]); // if game timer control not yet expired,
	if (!reg_p.z) { smb_ex_g_timer(); return; } // branch to leave
	set_a(ram[smb_game_timer_display]);
	or_a(ram[smb_game_timer_display+1]); // otherwise check game timer digits
	or_a(ram[smb_game_timer_display+2]);
	if (reg_p.z) { goto time_up_on; } // if game timer digits at 000, branch to time-up code
	set_y(ram[smb_game_timer_display]); // otherwise check first digit
	set_y(reg_y-1); // if first digit not on 1,
	if (!reg_p.z) { goto res_gt_ctrl; } // branch to reset game timer control
	set_a(ram[smb_game_timer_display+1]); // otherwise check second and third digits
	or_a(ram[smb_game_timer_display+2]);
	if (!reg_p.z) { goto res_gt_ctrl; } // if timer not at 100, branch to reset game timer control
	set_a((smb_time_running_out_music));
	ram[smb_event_music_queue] = reg_a; // otherwise load time running out music
res_gt_ctrl:;
	set_a(0x18); // reset game timer control
	ram[smb_game_timer_ctrl_timer] = reg_a;
	set_y(0x23); // set offset for last digit
	set_a(0xff); // set value to decrement game timer digit
	ram[smb_digit_modifier+5] = reg_a;
	smb_digits_math_routine(); // do sub to decrement game timer slowly
	set_a(0xa4); // set status nybbles to update game timer display
	smb_print_status_bar_numbers(); return; // do sub to update the display
time_up_on:;
	ram[smb_player_status] = reg_a; // init player status (note A will always be zero here)
	smb_force_injury(); // do sub to kill the player (note player is small here)
	ram[smb_game_timer_expired_flag] = inc(ram[smb_game_timer_expired_flag]); // set game timer expiration flag
	smb_ex_g_timer(); return;
}

void smb_ex_g_timer() {
	return; // leave
	smb_warp_zone_object(); return;
}

void smb_warp_zone_object() {
	set_a(ram[smb_scroll_lock]); // check for scroll lock flag
	if (reg_p.z) { smb_ex_g_timer(); return; } // branch if not set to leave
	set_a(ram[smb_player_y_position]); // check to see if player's vertical coordinate has
	and_a(ram[smb_player_y_high_pos]); // same bits set as in vertical high byte (why?)
	if (!reg_p.z) { smb_ex_g_timer(); return; } // if so, branch to leave
	ram[smb_scroll_lock] = reg_a; // otherwise nullify scroll lock flag
	ram[smb_warp_zone_control] = inc(ram[smb_warp_zone_control]); // increment warp zone flag to make warp pipes for warp zone
	smb_erase_enemy_object(); return; // kill this object
	smb_process_whirlpools(); return;
}

void smb_process_whirlpools() {
	set_a(ram[smb_area_type]); // check for water type level
	if (!reg_p.z) { goto exit_wh; } // branch to leave if not found
	ram[smb_whirlpool_flag] = reg_a; // otherwise initialize whirlpool flag
	set_a(ram[smb_timer_control]); // if master timer control set,
	if (!reg_p.z) { goto exit_wh; } // branch to leave
	set_y(0x04); // otherwise start with last whirlpool data
wh_loop:;
	set_a(ram[smb_whirlpool_left_extent + reg_y]); // get left extent of whirlpool
	reg_p.c = 0;
	add_a(ram[smb_whirlpool_length + reg_y]); // add length of whirlpool
	ram[0x0002] = reg_a; // store result as right extent here
	set_a(ram[smb_whirlpool_page_loc + reg_y]); // get page location
	if (reg_p.z) { goto next_wh; } // if none or page 0, branch to get next data
	add_a(0x00); // add carry
	ram[0x0001] = reg_a; // store result as page location of right extent here
	set_a(ram[smb_player_x_position]); // get player's horizontal position
	reg_p.c = 1;
	sub_a(ram[smb_whirlpool_left_extent + reg_y]); // subtract left extent
	set_a(ram[smb_player_page_loc]); // get player's page location
	sub_a(ram[smb_whirlpool_page_loc + reg_y]); // subtract borrow
	if (reg_p.n) { goto next_wh; } // if player too far left, branch to get next data
	set_a(ram[0x0002]); // otherwise get right extent
	reg_p.c = 1;
	sub_a(ram[smb_player_x_position]); // subtract player's horizontal coordinate
	set_a(ram[0x0001]); // get right extent's page location
	sub_a(ram[smb_player_page_loc]); // subtract borrow
	if (!reg_p.n) { goto whirlpool_activate; } // if player within right extent, branch to whirlpool code
next_wh:;
	set_y(reg_y-1); // move onto next whirlpool data
	if (!reg_p.n) { goto wh_loop; } // do this until all whirlpools are checked
exit_wh:;
	return; // leave
whirlpool_activate:;
	set_a(ram[smb_whirlpool_length + reg_y]); // get length of whirlpool
	reg_a = shr(reg_a); // divide by 2
	ram[0x0000] = reg_a; // save here
	set_a(ram[smb_whirlpool_left_extent + reg_y]); // get left extent of whirlpool
	reg_p.c = 0;
	add_a(ram[0x0000]); // add length divided by 2
	ram[0x0001] = reg_a; // save as center of whirlpool
	set_a(ram[smb_whirlpool_page_loc + reg_y]); // get page location
	add_a(0x00); // add carry
	ram[0x0000] = reg_a; // save as page location of whirlpool center
	set_a(ram[smb_frame_counter]); // get frame counter
	reg_a = shr(reg_a); // shift d0 into carry (to run on every other frame)
	if (!reg_p.c) { goto wh_pull; } // if d0 not set, branch to last part of code
	set_a(ram[0x0001]); // get center
	reg_p.c = 1;
	sub_a(ram[smb_player_x_position]); // subtract player's horizontal coordinate
	set_a(ram[0x0000]); // get page location of center
	sub_a(ram[smb_player_page_loc]); // subtract borrow
	if (!reg_p.n) { goto left_wh; } // if player to the left of center, branch
	set_a(ram[smb_player_x_position]); // otherwise slowly pull player left, towards the center
	reg_p.c = 1;
	sub_a(0x01); // subtract one pixel
	ram[smb_player_x_position] = reg_a; // set player's new horizontal coordinate
	set_a(ram[smb_player_page_loc]);
	sub_a(0x00); // subtract borrow
	goto set_p_wh; // jump to set player's new page location
left_wh:;
	set_a(ram[smb_player_collision_bits]); // get player's collision bits
	reg_a = shr(reg_a); // shift d0 into carry
	if (!reg_p.c) { goto wh_pull; } // if d0 not set, branch
	set_a(ram[smb_player_x_position]); // otherwise slowly pull player right, towards the center
	reg_p.c = 0;
	add_a(0x01); // add one pixel
	ram[smb_player_x_position] = reg_a; // set player's new horizontal coordinate
	set_a(ram[smb_player_page_loc]);
	add_a(0x00); // add carry
set_p_wh:;
	ram[smb_player_page_loc] = reg_a; // set player's new page location
wh_pull:;
	set_a(0x10);
	ram[0x0000] = reg_a; // set vertical movement force
	set_a(0x01);
	ram[smb_whirlpool_flag] = reg_a; // set whirlpool flag to be used later
	ram[0x0002] = reg_a; // also set maximum vertical speed
	reg_a = shr(reg_a);
	set_x(reg_a); // set X for player offset
	smb_impose_gravity(); return; // jump to put whirlpool effect on player vertically, do not return
}

void smb_flagpole_routine() {
	set_x(0x05); // set enemy object offset
	ram[smb_object_offset] = reg_x; // to special use slot
	set_a(ram[smb_enemy_id + reg_x]);
	cmp_a((smb_flagpole_flag_object)); // if flagpole flag not found,
	if (!reg_p.z) { goto exit_flag_p; } // branch to leave
	set_a(ram[smb_game_engine_subroutine]);
	cmp_a(0x04); // if flagpole slide routine not running,
	if (!reg_p.z) { goto skip_score; } // branch to near the end of code
	set_a(ram[smb_player_state]);
	cmp_a(0x03); // if player state not climbing,
	if (!reg_p.z) { goto skip_score; } // branch to near the end of code
	set_a(ram[smb_enemy_y_position + reg_x]); // check flagpole flag's vertical coordinate
	cmp_a(0xaa); // if flagpole flag down to a certain point,
	if (reg_p.c) { goto give_fp_scr; } // branch to end the level
	set_a(ram[smb_player_y_position]); // check player's vertical coordinate
	cmp_a(0xa2); // if player down to a certain point,
	if (reg_p.c) { goto give_fp_scr; } // branch to end the level
	set_a(ram[smb_enemy_ymf_dummy + reg_x]);
	add_a(0xff); // add movement amount to dummy variable
	ram[smb_enemy_ymf_dummy + reg_x] = reg_a; // save dummy variable
	set_a(ram[smb_enemy_y_position + reg_x]); // get flag's vertical coordinate
	add_a(0x01); // add 1 plus carry to move flag, and
	ram[smb_enemy_y_position + reg_x] = reg_a; // store vertical coordinate
	set_a(ram[smb_flagpole_f_num_ymf_dummy]);
	reg_p.c = 1; // subtract movement amount from dummy variable
	sub_a(0xff);
	ram[smb_flagpole_f_num_ymf_dummy] = reg_a; // save dummy variable
	set_a(ram[smb_flagpole_f_num_y_pos]);
	sub_a(0x01); // subtract one plus borrow to move floatey number,
	ram[smb_flagpole_f_num_y_pos] = reg_a; // and store vertical coordinate here
skip_score:;
	goto fp_gfx; // jump to skip ahead and draw flag and floatey number
give_fp_scr:;
	set_y(ram[smb_flagpole_score]); // get score offset from earlier (when player touched flagpole)
	set_a(rom[smb_flagpole_score_mods + reg_y]); // get amount to award player points
	set_x(rom[smb_flagpole_score_digits + reg_y]); // get digit with which to award points
	ram[smb_digit_modifier + reg_x] = reg_a; // store in digit modifier
	smb_add_to_score(); // do sub to award player points depending on height of collision
	set_a(0x05);
	ram[smb_game_engine_subroutine] = reg_a; // set to run end-of-level subroutine on next frame
fp_gfx:;
	smb_get_enemy_offscreen_bits(); // get offscreen information
	smb_relative_enemy_position(); // get relative coordinates
	smb_flagpole_gfx_handler(); // draw flagpole flag and floatey number
exit_flag_p:;
	return;
}

void smb_jumpspring_handler() {
	smb_get_enemy_offscreen_bits(); // get offscreen information
	set_a(ram[smb_timer_control]); // check master timer control
	if (!reg_p.z) { goto draw_j_spr; } // branch to last section if set
	set_a(ram[smb_jumpspring_anim_ctrl]); // check jumpspring frame control
	if (reg_p.z) { goto draw_j_spr; } // branch to last section if not set
	set_y(reg_a);
	set_y(reg_y-1); // subtract one from frame control,
	set_a(reg_y); // the only way a poor nmos 6502 can
	and_a(0b00000010); // mask out all but d1, original value still in Y
	if (!reg_p.z) { goto down_j_spr; } // if set, branch to move player up
	ram[smb_player_y_position] = inc(ram[smb_player_y_position]);
	ram[smb_player_y_position] = inc(ram[smb_player_y_position]); // move player's vertical position down two pixels
	goto pos_j_spr; // skip to next part
down_j_spr:;
	ram[smb_player_y_position] = dec(ram[smb_player_y_position]); // move player's vertical position up two pixels
	ram[smb_player_y_position] = dec(ram[smb_player_y_position]);
pos_j_spr:;
	set_a(ram[smb_jumpspring_fixed_y_pos + reg_x]); // get permanent vertical position
	reg_p.c = 0;
	add_a(rom[smb_jumpspring_y_pos_data + reg_y]); // add value using frame control as offset
	ram[smb_enemy_y_position + reg_x] = reg_a; // store as new vertical position
	cmp_y(0x01); // check frame control offset (second frame is $00)
	if (!reg_p.c) { goto bounce_js; } // if offset not yet at third frame ($01), skip to next part
	set_a(ram[smb_a_b_buttons]);
	and_a((smb_a_button)); // check saved controller bits for A button press
	if (reg_p.z) { goto bounce_js; } // skip to next part if A not pressed
	and_a(ram[smb_previous_a_b_buttons]); // check for A button pressed in previous frame
	if (!reg_p.z) { goto bounce_js; } // skip to next part if so
	set_a(0xf4);
	ram[smb_jumpspring_force] = reg_a; // otherwise write new jumpspring force here
bounce_js:;
	cmp_y(0x03); // check frame control offset again
	if (!reg_p.z) { goto draw_j_spr; } // skip to last part if not yet at fifth frame ($03)
	set_a(ram[smb_jumpspring_force]);
	ram[smb_player_y_speed] = reg_a; // store jumpspring force as player's new vertical speed
	set_a(0x00);
	ram[smb_jumpspring_anim_ctrl] = reg_a; // initialize jumpspring frame control
draw_j_spr:;
	smb_relative_enemy_position(); // get jumpspring's relative coordinates
	smb_enemy_gfx_handler(); // draw jumpspring
	smb_offscreen_bounds_check(); // check to see if we need to kill it
	set_a(ram[smb_jumpspring_anim_ctrl]); // if frame control at zero, don't bother
	if (reg_p.z) { goto ex_js_pring; } // trying to animate it, just leave
	set_a(ram[smb_jumpspring_timer]);
	if (!reg_p.z) { goto ex_js_pring; } // if jumpspring timer not expired yet, leave
	set_a(0x04);
	ram[smb_jumpspring_timer] = reg_a; // otherwise initialize jumpspring timer
	ram[smb_jumpspring_anim_ctrl] = inc(ram[smb_jumpspring_anim_ctrl]); // increment frame control to animate jumpspring
ex_js_pring:;
	return; // leave
	smb_setup_vine(); return;
}

void smb_setup_vine() {
	set_a((smb_vine_object)); // load identifier for vine object
	ram[smb_enemy_id + reg_x] = reg_a; // store in buffer
	set_a(0x01);
	ram[smb_enemy_flag + reg_x] = reg_a; // set flag for enemy object buffer
	set_a(ram[smb_block_page_loc + reg_y]);
	ram[smb_enemy_page_loc + reg_x] = reg_a; // copy page location from previous object
	set_a(ram[smb_block_x_position + reg_y]);
	ram[smb_enemy_x_position + reg_x] = reg_a; // copy horizontal coordinate from previous object
	set_a(ram[smb_block_y_position + reg_y]);
	ram[smb_enemy_y_position + reg_x] = reg_a; // copy vertical coordinate from previous object
	set_y(ram[smb_vine_flag_offset]); // load vine flag/offset to next available vine slot
	if (!reg_p.z) { goto next_vo; } // if set at all, don't bother to store vertical
	ram[smb_vine_start_y_position] = reg_a; // otherwise store vertical coordinate here
next_vo:;
	set_a(reg_x); // store object offset to next available vine slot
	ram[smb_vine_obj_offset + reg_y] = reg_a; // using vine flag as offset
	ram[smb_vine_flag_offset] = inc(ram[smb_vine_flag_offset]); // increment vine flag offset
	set_a((smb_sfx_grow_vine));
	ram[smb_square_2_sound_queue] = reg_a; // load vine grow sound
	return;
}

void smb_vine_object_handler() {
	cmp_x(0x05); // check enemy offset for special use slot
	if (!reg_p.z) { goto exit_vh; } // if not in last slot, branch to leave
	set_y(ram[smb_vine_flag_offset]);
	set_y(reg_y-1); // decrement vine flag in Y, use as offset
	set_a(ram[smb_vine_height]);
	cmp_a(rom[smb_vine_height_data + reg_y]); // if vine has reached certain height,
	if (reg_p.z) { goto run_v_subs; } // branch ahead to skip this part
	set_a(ram[smb_frame_counter]); // get frame counter
	reg_a = shr(reg_a); // shift d1 into carry
	reg_a = shr(reg_a);
	if (!reg_p.c) { goto run_v_subs; } // if d1 not set (2 frames every 4) skip this part
	set_a(ram[smb_enemy_y_position+5]);
	sub_a(0x01); // subtract vertical position of vine
	ram[smb_enemy_y_position+5] = reg_a; // one pixel every frame it's time
	ram[smb_vine_height] = inc(ram[smb_vine_height]); // increment vine height
run_v_subs:;
	set_a(ram[smb_vine_height]); // if vine still very small,
	cmp_a(0x08); // branch to leave
	if (!reg_p.c) { goto exit_vh; }
	smb_relative_enemy_position(); // get relative coordinates of vine,
	smb_get_enemy_offscreen_bits(); // and any offscreen bits
	set_y(0x00); // initialize offset used in draw vine sub
v_draw_loop:;
	smb_draw_vine(); // draw vine
	set_y(reg_y+1); // increment offset
	cmp_y(ram[smb_vine_flag_offset]); // if offset in Y and offset here
	if (!reg_p.z) { goto v_draw_loop; } // do not yet match, loop back to draw more vine
	set_a(ram[smb_enemy_offscreen_bits]);
	and_a(0b00001100); // mask offscreen bits
	if (reg_p.z) { goto wr_cm_tile; } // if none of the saved offscreen bits set, skip ahead
	set_y(reg_y-1); // otherwise decrement Y to get proper offset again
kill_vine:;
	set_x(ram[smb_vine_obj_offset + reg_y]); // get enemy object offset for this vine object
	smb_erase_enemy_object(); // kill this vine object
	set_y(reg_y-1); // decrement Y
	if (!reg_p.n) { goto kill_vine; } // if any vine objects left, loop back to kill it
	ram[smb_vine_flag_offset] = reg_a; // initialize vine flag/offset
	ram[smb_vine_height] = reg_a; // initialize vine height
wr_cm_tile:;
	set_a(ram[smb_vine_height]); // check vine height
	cmp_a(0x20); // if vine small (less than 32 pixels tall)
	if (!reg_p.c) { goto exit_vh; } // then branch ahead to leave
	set_x(0x06); // set offset in X to last enemy slot
	set_a(0x01); // set A to obtain horizontal in $04, but we don't care
	set_y(0x1b); // set Y to offset to get block at ($04, $10) of coordinates
	smb_block_buffer_collision(); // do a sub to get block buffer address set, return contents
	set_y(ram[0x0002]);
	cmp_y(0xd0); // if vertical high nybble offset beyond extent of
	if (reg_p.c) { goto exit_vh; } // current block buffer, branch to leave, do not write
	set_a(mem_r(*(uint16_t*)&ram[0x0006] + reg_y)); // otherwise check contents of block buffer at
	if (!reg_p.z) { goto exit_vh; } // current offset, if not empty, branch to leave
	set_a(0x26);
	mem_w(*(uint16_t*)&ram[0x0006] + reg_y, reg_a); // otherwise, write climbing metatile to block buffer
exit_vh:;
	set_x(ram[smb_object_offset]); // get enemy object offset and leave
	return;
}

void smb_process_cannons() {
	set_a(ram[smb_area_type]); // get area type
	if (reg_p.z) { goto ex_cannon; } // if water type area, branch to leave
	set_x(0x02);
three_s_chk:;
	ram[smb_object_offset] = reg_x; // start at third enemy slot
	set_a(ram[smb_enemy_flag + reg_x]); // check enemy buffer flag
	if (!reg_p.z) { goto chk_bb; } // if set, branch to check enemy
	set_a(ram[smb_pseudo_random_bit_reg+1 + reg_x]); // otherwise get part of LSFR
	set_y(ram[smb_secondary_hard_mode]); // get secondary hard mode flag, use as offset
	and_a(rom[smb_cannon_bit_masks + reg_y]); // mask out bits of LSFR as decided by flag
	cmp_a(0x06); // check to see if lower nybble is above certain value
	if (reg_p.c) { goto chk_bb; } // if so, branch to check enemy
	set_y(reg_a); // transfer masked contents of LSFR to Y as pseudorandom offset
	set_a(ram[smb_cannon_page_loc + reg_y]); // get page location
	if (reg_p.z) { goto chk_bb; } // if not set or on page 0, branch to check enemy
	set_a(ram[smb_cannon_timer + reg_y]); // get cannon timer
	if (reg_p.z) { goto fire_cannon; } // if expired, branch to fire cannon
	sub_a(0x00); // otherwise subtract borrow (note carry will always be clear here)
	ram[smb_cannon_timer + reg_y] = reg_a; // to count timer down
	goto chk_bb;
fire_cannon:;
	set_a(ram[smb_timer_control]); // if master timer control set,
	if (!reg_p.z) { goto chk_bb; } // branch to check enemy
	set_a(0x0e); // otherwise we start creating one
	ram[smb_cannon_timer + reg_y] = reg_a; // first, reset cannon timer
	set_a(ram[smb_cannon_page_loc + reg_y]); // get page location of cannon
	ram[smb_enemy_page_loc + reg_x] = reg_a; // save as page location of bullet bill
	set_a(ram[smb_cannon_x_position + reg_y]); // get horizontal coordinate of cannon
	ram[smb_enemy_x_position + reg_x] = reg_a; // save as horizontal coordinate of bullet bill
	set_a(ram[smb_cannon_y_position + reg_y]); // get vertical coordinate of cannon
	reg_p.c = 1;
	sub_a(0x08); // subtract eight pixels (because enemies are 24 pixels tall)
	ram[smb_enemy_y_position + reg_x] = reg_a; // save as vertical coordinate of bullet bill
	set_a(0x01);
	ram[smb_enemy_y_high_pos + reg_x] = reg_a; // set vertical high byte of bullet bill
	ram[smb_enemy_flag + reg_x] = reg_a; // set buffer flag
	reg_a = shr(reg_a); // shift right once to init A
	ram[smb_enemy_state + reg_x] = reg_a; // then initialize enemy's state
	set_a(0x09);
	ram[smb_enemy_bound_box_ctrl + reg_x] = reg_a; // set bounding box size control for bullet bill
	set_a((smb_bullet_bill_cannon_var));
	ram[smb_enemy_id + reg_x] = reg_a; // load identifier for bullet bill (cannon variant)
	goto next_3_slt; // move onto next slot
chk_bb:;
	set_a(ram[smb_enemy_id + reg_x]); // check enemy identifier for bullet bill (cannon variant)
	cmp_a((smb_bullet_bill_cannon_var));
	if (!reg_p.z) { goto next_3_slt; } // if not found, branch to get next slot
	smb_offscreen_bounds_check(); // otherwise, check to see if it went offscreen
	set_a(ram[smb_enemy_flag + reg_x]); // check enemy buffer flag
	if (reg_p.z) { goto next_3_slt; } // if not set, branch to get next slot
	smb_get_enemy_offscreen_bits(); // otherwise, get offscreen information
	smb_bullet_bill_handler(); // then do sub to handle bullet bill
next_3_slt:;
	set_x(reg_x-1); // move onto next slot
	if (!reg_p.n) { goto three_s_chk; } // do this until first three slots are checked
ex_cannon:;
	return; // then leave
}

void smb_bullet_bill_handler() {
	set_a(ram[smb_timer_control]); // if master timer control set,
	if (!reg_p.z) { goto run_bb_subs; } // branch to run subroutines except movement sub
	set_a(ram[smb_enemy_state + reg_x]);
	if (!reg_p.z) { goto chk_d_ste; } // if bullet bill's state set, branch to check defeated state
	set_a(ram[smb_enemy_offscreen_bits]); // otherwise load offscreen bits
	and_a(0b00001100); // mask out bits
	cmp_a(0b00001100); // check to see if all bits are set
	if (reg_p.z) { goto kill_bb; } // if so, branch to kill this object
	set_y(0x01); // set to move right by default
	smb_player_enemy_diff(); // get horizontal difference between player and bullet bill
	if (reg_p.n) { goto setup_bb; } // if enemy to the left of player, branch
	set_y(reg_y+1); // otherwise increment to move left
setup_bb:;
	ram[smb_enemy_moving_dir + reg_x] = reg_y; // set bullet bill's moving direction
	set_y(reg_y-1); // decrement to use as offset
	set_a(rom[smb_bullet_bill_x_spd_data + reg_y]); // get horizontal speed based on moving direction
	ram[smb_blooper_move_speed + reg_x] = reg_a; // and store it
	set_a(ram[0x0000]); // get horizontal difference
	add_a(0x28); // add 40 pixels
	cmp_a(0x50); // if less than a certain amount, player is too close
	if (!reg_p.c) { goto kill_bb; } // to cannon either on left or right side, thus branch
	set_a(0x01);
	ram[smb_enemy_state + reg_x] = reg_a; // otherwise set bullet bill's state
	set_a(0x0a);
	ram[smb_enemy_frame_timer + reg_x] = reg_a; // set enemy frame timer
	set_a(0x08);
	ram[smb_square_2_sound_queue] = reg_a; // play fireworks/gunfire sound
chk_d_ste:;
	set_a(ram[smb_enemy_state + reg_x]); // check enemy state for d5 set
	and_a(0b00100000);
	if (reg_p.z) { goto bb_fly; } // if not set, skip to move horizontally
	smb_move_d_enemy_vertically(); // otherwise do sub to move bullet bill vertically
bb_fly:;
	smb_move_enemy_horizontally(); // do sub to move bullet bill horizontally
run_bb_subs:;
	smb_get_enemy_offscreen_bits(); // get offscreen information
	smb_relative_enemy_position(); // get relative coordinates
	smb_get_enemy_bound_box(); // get bounding box coordinates
	smb_player_enemy_collision(); // handle player to enemy collisions
	smb_enemy_gfx_handler(); return; // draw the bullet bill and leave
kill_bb:;
	smb_erase_enemy_object(); // kill bullet bill and leave
	return;
}

void smb_spawn_hammer_obj() {
	set_a(ram[smb_pseudo_random_bit_reg+1]); // get pseudorandom bits from
	and_a(0x07); // second part of LSFR
	if (!reg_p.z) { goto set_m_ofs; } // if any bits are set, branch and use as offset
	set_a(ram[smb_pseudo_random_bit_reg+1]);
	and_a(0b00001000); // get d3 from same part of LSFR
set_m_ofs:;
	set_y(reg_a); // use either d3 or d2-d0 for offset here
	set_a(ram[smb_misc_state + reg_y]); // if any values loaded in
	if (!reg_p.z) { goto no_hammer; } // $2a-$32 where offset is then leave with carry clear
	set_x(rom[smb_hammer_enemy_ofs_data + reg_y]); // get offset of enemy slot to check using Y as offset
	set_a(ram[smb_enemy_flag + reg_x]); // check enemy buffer flag at offset
	if (!reg_p.z) { goto no_hammer; } // if buffer flag set, branch to leave with carry clear
	set_x(ram[smb_object_offset]); // get original enemy object offset
	set_a(reg_x);
	ram[smb_hammer_enemy_offset + reg_y] = reg_a; // save here
	set_a(0x90);
	ram[smb_misc_state + reg_y] = reg_a; // save hammer's state here
	set_a(0x07);
	ram[smb_misc_bound_box_ctrl + reg_y] = reg_a; // set something else entirely, here
	reg_p.c = 1; // return with carry set
	return;
no_hammer:;
	set_x(ram[smb_object_offset]); // get original enemy object offset
	reg_p.c = 0; // return with carry clear
	return;
	smb_proc_hammer_obj(); return;
}

void smb_proc_hammer_obj() {
	set_a(ram[smb_timer_control]); // if master timer control set
	if (!reg_p.z) { goto run_h_subs; } // skip all of this code and go to last subs at the end
	set_a(ram[smb_misc_state + reg_x]); // otherwise get hammer's state
	and_a(0b01111111); // mask out d7
	set_y(ram[smb_hammer_enemy_offset + reg_x]); // get enemy object offset that spawned this hammer
	cmp_a(0x02); // check hammer's state
	if (reg_p.z) { goto set_h_spd; } // if currently at 2, branch
	if (reg_p.c) { goto set_h_pos; } // if greater than 2, branch elsewhere
	set_a(reg_x);
	reg_p.c = 0; // add 13 bytes to use
	add_a(0x0d); // proper misc object
	set_x(reg_a); // return offset to X
	set_a(0x10);
	ram[0x0000] = reg_a; // set downward movement force
	set_a(0x0f);
	ram[0x0001] = reg_a; // set upward movement force (not used)
	set_a(0x04);
	ram[0x0002] = reg_a; // set maximum vertical speed
	set_a(0x00); // set A to impose gravity on hammer
	smb_impose_gravity(); // do sub to impose gravity on hammer and move vertically
	smb_move_object_horizontally(); // do sub to move it horizontally
	set_x(ram[smb_object_offset]); // get original misc object offset
	goto run_all_h; // branch to essential subroutines
set_h_spd:;
	set_a(0xfe);
	ram[smb_misc_y_speed + reg_x] = reg_a; // set hammer's vertical speed
	set_a(ram[smb_enemy_state + reg_y]); // get enemy object state
	and_a(0b11110111); // mask out d3
	ram[smb_enemy_state + reg_y] = reg_a; // store new state
	set_x(ram[smb_enemy_moving_dir + reg_y]); // get enemy's moving direction
	set_x(reg_x-1); // decrement to use as offset
	set_a(rom[smb_hammer_x_spd_data + reg_x]); // get proper speed to use based on moving direction
	set_x(ram[smb_object_offset]); // reobtain hammer's buffer offset
	ram[smb_misc_x_speed + reg_x] = reg_a; // set hammer's horizontal speed
set_h_pos:;
	ram[smb_misc_state + reg_x] = dec(ram[smb_misc_state + reg_x]); // decrement hammer's state
	set_a(ram[smb_enemy_x_position + reg_y]); // get enemy's horizontal position
	reg_p.c = 0;
	add_a(0x02); // set position 2 pixels to the right
	ram[smb_misc_x_position + reg_x] = reg_a; // store as hammer's horizontal position
	set_a(ram[smb_enemy_page_loc + reg_y]); // get enemy's page location
	add_a(0x00); // add carry
	ram[smb_misc_page_loc + reg_x] = reg_a; // store as hammer's page location
	set_a(ram[smb_enemy_y_position + reg_y]); // get enemy's vertical position
	reg_p.c = 1;
	sub_a(0x0a); // move position 10 pixels upward
	ram[smb_misc_y_position + reg_x] = reg_a; // store as hammer's vertical position
	set_a(0x01);
	ram[smb_misc_y_high_pos + reg_x] = reg_a; // set hammer's vertical high byte
	if (!reg_p.z) { goto run_h_subs; } // unconditional branch to skip first routine
run_all_h:;
	smb_player_hammer_collision(); // handle collisions
run_h_subs:;
	smb_get_misc_offscreen_bits(); // get offscreen information
	smb_relative_misc_position(); // get relative coordinates
	smb_get_misc_bound_box(); // get bounding box coordinates
	smb_draw_hammer(); // draw the hammer
	return; // and we are done here
	smb_coin_block(); return;
}

void smb_coin_block() {
	smb_find_empty_misc_slot(); // set offset for empty or last misc object buffer slot
	set_a(ram[smb_block_page_loc + reg_x]); // get page location of block object
	ram[smb_misc_page_loc + reg_y] = reg_a; // store as page location of misc object
	set_a(ram[smb_block_x_position + reg_x]); // get horizontal coordinate of block object
	or_a(0x05); // add 5 pixels
	ram[smb_misc_x_position + reg_y] = reg_a; // store as horizontal coordinate of misc object
	set_a(ram[smb_block_y_position + reg_x]); // get vertical coordinate of block object
	sub_a(0x10); // subtract 16 pixels
	ram[smb_misc_y_position + reg_y] = reg_a; // store as vertical coordinate of misc object
	smb_j_coin_c(); return; // jump to rest of code as applies to this misc object
	smb_setup_jump_coin(); return;
}

void smb_setup_jump_coin() {
	smb_find_empty_misc_slot(); // set offset for empty or last misc object buffer slot
	set_a(ram[smb_block_page_loc_2 + reg_x]); // get page location saved earlier
	ram[smb_misc_page_loc + reg_y] = reg_a; // and save as page location for misc object
	set_a(ram[0x0006]); // get low byte of block buffer offset
	reg_a = shl(reg_a);
	reg_a = shl(reg_a); // multiply by 16 to use lower nybble
	reg_a = shl(reg_a);
	reg_a = shl(reg_a);
	or_a(0x05); // add five pixels
	ram[smb_misc_x_position + reg_y] = reg_a; // save as horizontal coordinate for misc object
	set_a(ram[0x0002]); // get vertical high nybble offset from earlier
	add_a(0x20); // add 32 pixels for the status bar
	ram[smb_misc_y_position + reg_y] = reg_a; // store as vertical coordinate
	smb_j_coin_c(); return;
}

void smb_j_coin_c() {
	set_a(0xfb);
	ram[smb_misc_y_speed + reg_y] = reg_a; // set vertical speed
	set_a(0x01);
	ram[smb_misc_y_high_pos + reg_y] = reg_a; // set vertical high byte
	ram[smb_misc_state + reg_y] = reg_a; // set state for misc object
	ram[smb_square_2_sound_queue] = reg_a; // load coin grab sound
	ram[smb_object_offset] = reg_x; // store current control bit as misc object offset
	smb_give_one_coin(); // update coin tally on the screen and coin amount variable
	ram[smb_coin_tally_for_1_ups] = inc(ram[smb_coin_tally_for_1_ups]); // increment coin tally used to activate 1-up block flag
	return;
	smb_find_empty_misc_slot(); return;
}

void smb_find_empty_misc_slot() {
	set_y(0x08); // start at end of misc objects buffer
f_misc_loop:;
	set_a(ram[smb_misc_state + reg_y]); // get misc object state
	if (reg_p.z) { goto use_misc_s; } // branch if none found to use current offset
	set_y(reg_y-1); // decrement offset
	cmp_y(0x05); // do this for three slots
	if (!reg_p.z) { goto f_misc_loop; } // do this until all slots are checked
	set_y(0x08); // if no empty slots found, use last slot
use_misc_s:;
	ram[smb_jump_coin_misc_offset] = reg_y; // store offset of misc object buffer here (residual)
	return;
	smb_misc_objects_core(); return;
}

void smb_misc_objects_core() {
	set_x(0x08); // set at end of misc object buffer
misc_loop:;
	ram[smb_object_offset] = reg_x; // store misc object offset here
	set_a(ram[smb_misc_state + reg_x]); // check misc object state
	if (reg_p.z) { goto misc_loop_back; } // branch to check next slot
	reg_a = shl(reg_a); // otherwise shift d7 into carry
	if (!reg_p.c) { goto proc_jump_coin; } // if d7 not set, jumping coin, thus skip to rest of code here
	smb_proc_hammer_obj(); // otherwise go to process hammer,
	goto misc_loop_back; // then check next slot
proc_jump_coin:;
	set_y(ram[smb_misc_state + reg_x]); // check misc object state
	set_y(reg_y-1); // decrement to see if it's set to 1
	if (reg_p.z) { goto j_coin_run; } // if so, branch to handle jumping coin
	ram[smb_misc_state + reg_x] = inc(ram[smb_misc_state + reg_x]); // otherwise increment state to either start off or as timer
	set_a(ram[smb_misc_x_position + reg_x]); // get horizontal coordinate for misc object
	reg_p.c = 0; // whether its jumping coin (state 0 only) or floatey number
	add_a(ram[smb_scroll_amount]); // add current scroll speed
	ram[smb_misc_x_position + reg_x] = reg_a; // store as new horizontal coordinate
	set_a(ram[smb_misc_page_loc + reg_x]); // get page location
	add_a(0x00); // add carry
	ram[smb_misc_page_loc + reg_x] = reg_a; // store as new page location
	set_a(ram[smb_misc_state + reg_x]);
	cmp_a(0x30); // check state of object for preset value
	if (!reg_p.z) { goto run_jc_subs; } // if not yet reached, branch to subroutines
	set_a(0x00);
	ram[smb_misc_state + reg_x] = reg_a; // otherwise nullify object state
	goto misc_loop_back; // and move onto next slot
j_coin_run:;
	set_a(reg_x);
	reg_p.c = 0; // add 13 bytes to offset for next subroutine
	add_a(0x0d);
	set_x(reg_a);
	set_a(0x50); // set downward movement amount
	ram[0x0000] = reg_a;
	set_a(0x06); // set maximum vertical speed
	ram[0x0002] = reg_a;
	reg_a = shr(reg_a); // divide by 2 and set
	ram[0x0001] = reg_a; // as upward movement amount (apparently residual)
	set_a(0x00); // set A to impose gravity on jumping coin
	smb_impose_gravity(); // do sub to move coin vertically and impose gravity on it
	set_x(ram[smb_object_offset]); // get original misc object offset
	set_a(ram[smb_misc_y_speed + reg_x]); // check vertical speed
	cmp_a(0x05);
	if (!reg_p.z) { goto run_jc_subs; } // if not moving downward fast enough, keep state as-is
	ram[smb_misc_state + reg_x] = inc(ram[smb_misc_state + reg_x]); // otherwise increment state to change to floatey number
run_jc_subs:;
	smb_relative_misc_position(); // get relative coordinates
	smb_get_misc_offscreen_bits(); // get offscreen information
	smb_get_misc_bound_box(); // get bounding box coordinates (why?)
	smb_j_coin_gfx_handler(); // draw the coin or floatey number
misc_loop_back:;
	set_x(reg_x-1); // decrement misc object offset
	if (!reg_p.n) { goto misc_loop; } // loop back until all misc objects handled
	return; // then leave
}

void smb_give_one_coin() {
	set_a(0x01); // set digit modifier to add 1 coin
	ram[smb_digit_modifier+5] = reg_a; // to the current player's coin tally
	set_x(ram[smb_current_player]); // get current player on the screen
	set_y(rom[smb_coin_tally_offsets + reg_x]); // get offset for player's coin tally
	smb_digits_math_routine(); // update the coin tally
	ram[smb_coin_tally] = inc(ram[smb_coin_tally]); // increment onscreen player's coin amount
	set_a(ram[smb_coin_tally]);
	cmp_a((100)); // does player have 100 coins yet?
	if (!reg_p.z) { goto coin_points; } // if not, skip all of this
	set_a(0x00);
	ram[smb_coin_tally] = reg_a; // otherwise, reinitialize coin amount
	ram[smb_number_of_lives] = inc(ram[smb_number_of_lives]); // give the player an extra life
	set_a((smb_sfx_extra_life));
	ram[smb_square_2_sound_queue] = reg_a; // play 1-up sound
coin_points:;
	set_a(0x02); // set digit modifier to award
	ram[smb_digit_modifier+4] = reg_a; // 200 points to the player
	smb_add_to_score(); return;
}

void smb_add_to_score() {
	set_x(ram[smb_current_player]); // get current player
	set_y(rom[smb_score_offsets + reg_x]); // get offset for player's score
	smb_digits_math_routine(); // update the score internally with value in digit modifier
	smb_get_sb_nybbles(); return;
}

void smb_get_sb_nybbles() {
	set_y(ram[smb_current_player]); // get current player
	set_a(rom[smb_status_bar_nybbles + reg_y]); // get nybbles based on player, use to update score and coins
	smb_update_number(); return;
}

void smb_update_number() {
	smb_print_status_bar_numbers(); // print status bar numbers based on nybbles, whatever they be
	set_y(ram[smb_vram_buffer_1_offset]);
	set_a(ram[smb_vram_buffer_1-6 + reg_y]); // check highest digit of score
	if (!reg_p.z) { goto no_z_sup; } // if zero, overwrite with space tile for zero suppression
	set_a(0x24);
	ram[smb_vram_buffer_1-6 + reg_y] = reg_a;
no_z_sup:;
	set_x(ram[smb_object_offset]); // get enemy object buffer offset
	return;
	smb_setup_power_up(); return;
}

void smb_setup_power_up() {
	set_a((smb_power_up_object)); // load power-up identifier into
	ram[smb_enemy_id+5] = reg_a; // special use slot of enemy object buffer
	set_a(ram[smb_block_page_loc + reg_x]); // store page location of block object
	ram[smb_enemy_page_loc+5] = reg_a; // as page location of power-up object
	set_a(ram[smb_block_x_position + reg_x]); // store horizontal coordinate of block object
	ram[smb_enemy_x_position+5] = reg_a; // as horizontal coordinate of power-up object
	set_a(0x01);
	ram[smb_enemy_y_high_pos+5] = reg_a; // set vertical high byte of power-up object
	set_a(ram[smb_block_y_position + reg_x]); // get vertical coordinate of block object
	reg_p.c = 1;
	sub_a(0x08); // subtract 8 pixels
	ram[smb_enemy_y_position+5] = reg_a; // and use as vertical coordinate of power-up object
	smb_pwr_up_jmp(); return;
}

void smb_pwr_up_jmp() {
	set_a(0x01); // this is a residual jump point in enemy object jump table
	ram[smb_enemy_state+5] = reg_a; // set power-up object's state
	ram[smb_enemy_flag+5] = reg_a; // set buffer flag
	set_a(0x03);
	ram[smb_enemy_bound_box_ctrl+5] = reg_a; // set bounding box size control for power-up object
	set_a(ram[smb_power_up_type]);
	cmp_a(0x02); // check currently loaded power-up type
	if (reg_p.c) { goto put_behind; } // if star or 1-up, branch ahead
	set_a(ram[smb_player_status]); // otherwise check player's current status
	cmp_a(0x02);
	if (!reg_p.c) { goto str_type; } // if player not fiery, use status as power-up type
	reg_a = shr(reg_a); // otherwise shift right to force fire flower type
str_type:;
	ram[smb_power_up_type] = reg_a; // store type here
put_behind:;
	set_a(0b00100000);
	ram[smb_enemy_spr_attrib+5] = reg_a; // set background priority bit
	set_a((smb_sfx_grow_power_up));
	ram[smb_square_2_sound_queue] = reg_a; // load power-up reveal sound and leave
	return;
	smb_power_up_obj_handler(); return;
}

void smb_power_up_obj_handler() {
	set_x(0x05); // set object offset for last slot in enemy object buffer
	ram[smb_object_offset] = reg_x;
	set_a(ram[smb_enemy_state+5]); // check power-up object's state
	if (reg_p.z) { goto exit_p_up; } // if not set, branch to leave
	reg_a = shl(reg_a); // shift to check if d7 was set in object state
	if (!reg_p.c) { goto grow_the_power_up; } // if not set, branch ahead to skip this part
	set_a(ram[smb_timer_control]); // if master timer control set,
	if (!reg_p.z) { goto run_pu_subs; } // branch ahead to enemy object routines
	set_a(ram[smb_power_up_type]); // check power-up type
	if (reg_p.z) { goto shroom_m; } // if normal mushroom, branch ahead to move it
	cmp_a(0x03);
	if (reg_p.z) { goto shroom_m; } // if 1-up mushroom, branch ahead to move it
	cmp_a(0x02);
	if (!reg_p.z) { goto run_pu_subs; } // if not star, branch elsewhere to skip movement
	smb_move_jumping_enemy(); // otherwise impose gravity on star power-up and make it jump
	smb_enemy_jump(); // note that green paratroopa shares the same code here
	goto run_pu_subs; // then jump to other power-up subroutines
shroom_m:;
	smb_move_normal_enemy(); // do sub to make mushrooms move
	smb_enemy_to_bg_collision_det(); // deal with collisions
	goto run_pu_subs; // run the other subroutines
grow_the_power_up:;
	set_a(ram[smb_frame_counter]); // get frame counter
	and_a(0x03); // mask out all but 2 LSB
	if (!reg_p.z) { goto chk_pu_ste; } // if any bits set here, branch
	ram[smb_enemy_y_position+5] = dec(ram[smb_enemy_y_position+5]); // otherwise decrement vertical coordinate slowly
	set_a(ram[smb_enemy_state+5]); // load power-up object state
	ram[smb_enemy_state+5] = inc(ram[smb_enemy_state+5]); // increment state for next frame (to make power-up rise)
	cmp_a(0x11); // if power-up object state not yet past 16th pixel,
	if (!reg_p.c) { goto chk_pu_ste; } // branch ahead to last part here
	set_a(0x10);
	ram[smb_enemy_x_speed + reg_x] = reg_a; // otherwise set horizontal speed
	set_a(0b10000000);
	ram[smb_enemy_state+5] = reg_a; // and then set d7 in power-up object's state
	reg_a = shl(reg_a); // shift once to init A
	ram[smb_enemy_spr_attrib+5] = reg_a; // initialize background priority bit set here
	reg_a = rol(reg_a); // rotate A to set right moving direction
	ram[smb_enemy_moving_dir + reg_x] = reg_a; // set moving direction
chk_pu_ste:;
	set_a(ram[smb_enemy_state+5]); // check power-up object's state
	cmp_a(0x06); // for if power-up has risen enough
	if (!reg_p.c) { goto exit_p_up; } // if not, don't even bother running these routines
run_pu_subs:;
	smb_relative_enemy_position(); // get coordinates relative to screen
	smb_get_enemy_offscreen_bits(); // get offscreen bits
	smb_get_enemy_bound_box(); // get bounding box coordinates
	smb_draw_power_up(); // draw the power-up object
	smb_player_enemy_collision(); // check for collision with player
	smb_offscreen_bounds_check(); // check to see if it went offscreen
exit_p_up:;
	return; // and we're done
}

void smb_player_head_collision() {
	push(reg_a); // store metatile number to stack
	set_a(0x11); // load unbreakable block object state by default
	set_x(ram[smb_spr_data_offset_ctrl]); // load offset control bit here
	set_y(ram[smb_player_size]); // check player's size
	if (!reg_p.z) { goto d_block_ste; } // if small, branch
	set_a(0x12); // otherwise load breakable block object state
d_block_ste:;
	ram[smb_block_state + reg_x] = reg_a; // store into block object buffer
	smb_destroy_block_metatile(); // store blank metatile in vram buffer to write to name table
	set_x(ram[smb_spr_data_offset_ctrl]); // load offset control bit
	set_a(ram[0x0002]); // get vertical high nybble offset used in block buffer routine
	ram[smb_block_orig_y_pos + reg_x] = reg_a; // set as vertical coordinate for block object
	set_y(reg_a);
	set_a(ram[0x0006]); // get low byte of block buffer address used in same routine
	ram[smb_block_b_buf_low + reg_x] = reg_a; // save as offset here to be used later
	set_a(mem_r(*(uint16_t*)&ram[0x0006] + reg_y)); // get contents of block buffer at old address at $06, $07
	smb_block_bumped_chk(); // do a sub to check which block player bumped head on
	ram[0x0000] = reg_a; // store metatile here
	set_y(ram[smb_player_size]); // check player's size
	if (!reg_p.z) { goto chk_brick; } // if small, use metatile itself as contents of A
	set_a(reg_y); // otherwise init A (note: big = 0)
chk_brick:;
	if (!reg_p.c) { goto put_m_tile_b; } // if no match was found in previous sub, skip ahead
	set_y(0x11); // otherwise load unbreakable state into block object buffer
	ram[smb_block_state + reg_x] = reg_y; // note this applies to both player sizes
	set_a(0xc4); // load empty block metatile into A for now
	set_y(ram[0x0000]); // get metatile from before
	cmp_y(0x58); // is it brick with coins (with line)?
	if (reg_p.z) { goto start_b_tmr; } // if so, branch
	cmp_y(0x5d); // is it brick with coins (without line)?
	if (!reg_p.z) { goto put_m_tile_b; } // if not, branch ahead to store empty block metatile
start_b_tmr:;
	set_a(ram[smb_brick_coin_timer_flag]); // check brick coin timer flag
	if (!reg_p.z) { goto cont_b_tmr; } // if set, timer expired or counting down, thus branch
	set_a(0x0b);
	ram[smb_brick_coin_timer] = reg_a; // if not set, set brick coin timer
	ram[smb_brick_coin_timer_flag] = inc(ram[smb_brick_coin_timer_flag]); // and set flag linked to it
cont_b_tmr:;
	set_a(ram[smb_brick_coin_timer]); // check brick coin timer
	if (!reg_p.z) { goto put_old_mt; } // if not yet expired, branch to use current metatile
	set_y(0xc4); // otherwise use empty block metatile
put_old_mt:;
	set_a(reg_y); // put metatile into A
put_m_tile_b:;
	ram[smb_block_metatile + reg_x] = reg_a; // store whatever metatile be appropriate here
	smb_init_block_xy_pos(); // get block object horizontal coordinates saved
	set_y(ram[0x0002]); // get vertical high nybble offset
	set_a(0x23);
	mem_w(*(uint16_t*)&ram[0x0006] + reg_y, reg_a); // write blank metatile $23 to block buffer
	set_a(0x10);
	ram[smb_block_bounce_timer] = reg_a; // set block bounce timer
	set_a(pull()); // pull original metatile from stack
	ram[0x0005] = reg_a; // and save here
	set_y(0x00); // set default offset
	set_a(ram[smb_crouching_flag]); // is player crouching?
	if (!reg_p.z) { goto small_bp; } // if so, branch to increment offset
	set_a(ram[smb_player_size]); // is player big?
	if (reg_p.z) { goto big_bp; } // if so, branch to use default offset
small_bp:;
	set_y(reg_y+1); // increment for small or big and crouching
big_bp:;
	set_a(ram[smb_player_y_position]); // get player's vertical coordinate
	reg_p.c = 0;
	add_a(rom[smb_block_y_pos_adder_data + reg_y]); // add value determined by size
	and_a(0xf0); // mask out low nybble to get 16-pixel correspondence
	ram[smb_block_y_position + reg_x] = reg_a; // save as vertical coordinate for block object
	set_y(ram[smb_block_state + reg_x]); // get block object state
	cmp_y(0x11);
	if (reg_p.z) { goto unbreak; } // if set to value loaded for unbreakable, branch
	smb_brick_shatter(); // execute code for breakable brick
	goto inv_o_bit; // skip subroutine to do last part of code here
unbreak:;
	smb_bump_block(); // execute code for unbreakable brick or question block
inv_o_bit:;
	set_a(ram[smb_spr_data_offset_ctrl]); // invert control bit used by block objects
	eor_a(0x01); // and floatey numbers
	ram[smb_spr_data_offset_ctrl] = reg_a;
	return; // leave!
	smb_init_block_xy_pos(); return;
}

void smb_init_block_xy_pos() {
	set_a(ram[smb_player_x_position]); // get player's horizontal coordinate
	reg_p.c = 0;
	add_a(0x08); // add eight pixels
	and_a(0xf0); // mask out low nybble to give 16-pixel correspondence
	ram[smb_block_x_position + reg_x] = reg_a; // save as horizontal coordinate for block object
	set_a(ram[smb_player_page_loc]);
	add_a(0x00); // add carry to page location of player
	ram[smb_block_page_loc + reg_x] = reg_a; // save as page location of block object
	ram[smb_block_page_loc_2 + reg_x] = reg_a; // save elsewhere to be used later
	set_a(ram[smb_player_y_high_pos]);
	ram[smb_block_y_high_pos + reg_x] = reg_a; // save vertical high byte of player into
	return; // vertical high byte of block object and leave
	smb_bump_block(); return;
}

void smb_bump_block() {
	smb_check_top_of_block(); // check to see if there's a coin directly above this block
	set_a((smb_sfx_bump));
	ram[smb_square_1_sound_queue] = reg_a; // play bump sound
	set_a(0x00);
	ram[smb_block_x_speed + reg_x] = reg_a; // initialize horizontal speed for block object
	ram[smb_block_y_move_force + reg_x] = reg_a; // init fractional movement force
	ram[smb_player_y_speed] = reg_a; // init player's vertical speed
	set_a(0xfe);
	ram[smb_block_y_speed + reg_x] = reg_a; // set vertical speed for block object
	set_a(ram[0x0005]); // get original metatile from stack
	smb_block_bumped_chk(); // do a sub to check which block player bumped head on
	if (!reg_p.c) { smb_exit_block_chk(); return; } // if no match was found, branch to leave
	set_a(reg_y); // move block number to A
	cmp_a(0x09); // if block number was within 0-8 range,
	if (!reg_p.c) { goto block_code; } // branch to use current number
	sub_a(0x05); // otherwise subtract 5 for second set to get proper number
block_code:;
	static void(*targets[])() = {
		smb_mush_flower_block,
		smb_coin_block,
		smb_coin_block,
		smb_extra_life_mush_block,
		smb_mush_flower_block,
		smb_vine_block,
		smb_star_block,
		smb_coin_block,
		smb_extra_life_mush_block
	};
	targets[reg_a](); return;
	smb_mush_flower_block(); return;
}

void smb_mush_flower_block() {
	set_a(0x00); // load mushroom/fire flower into power-up type
	smb_item_block(); return;
}

void smb_star_block() {
	set_a(0x02); // load star into power-up type
	smb_item_block(); return;
}

void smb_extra_life_mush_block() {
	set_a(0x03); // load 1-up mushroom into power-up type
	smb_item_block(); return;
}

void smb_item_block() {
	ram[smb_power_up_type] = reg_a; // store correct power-up type
	smb_setup_power_up(); return;
	smb_vine_block(); return;
}

void smb_vine_block() {
	set_x(0x05); // load last slot for enemy object buffer
	set_y(ram[smb_spr_data_offset_ctrl]); // get control bit
	smb_setup_vine(); // set up vine object
	smb_exit_block_chk(); return;
}

void smb_exit_block_chk() {
	return; // leave
}

void smb_block_bumped_chk() {
	set_y(0x0d); // start at end of metatile data
bump_chk_loop:;
	cmp_a(rom[smb_brick_q_block_metatiles + reg_y]); // check to see if current metatile matches
	if (reg_p.z) { goto match_bump; } // metatile found in block buffer, branch if so
	set_y(reg_y-1); // otherwise move onto next metatile
	if (!reg_p.n) { goto bump_chk_loop; } // do this until all metatiles are checked
	reg_p.c = 0; // if none match, return with carry clear
match_bump:;
	return; // note carry is set if found match
	smb_brick_shatter(); return;
}

void smb_brick_shatter() {
	smb_check_top_of_block(); // check to see if there's a coin directly above this block
	set_a((smb_sfx_brick_shatter));
	ram[smb_block_rep_flag + reg_x] = reg_a; // set flag for block object to immediately replace metatile
	ram[smb_noise_sound_queue] = reg_a; // load brick shatter sound
	smb_spawn_brick_chunks(); // create brick chunk objects
	set_a(0xfe);
	ram[smb_player_y_speed] = reg_a; // set vertical speed for player
	set_a(0x05);
	ram[smb_digit_modifier+5] = reg_a; // set digit modifier to give player 50 points
	smb_add_to_score(); // do sub to update the score
	set_x(ram[smb_spr_data_offset_ctrl]); // load control bit and leave
	return;
	smb_check_top_of_block(); return;
}

void smb_check_top_of_block() {
	set_x(ram[smb_spr_data_offset_ctrl]); // load control bit
	set_y(ram[0x0002]); // get vertical high nybble offset used in block buffer
	if (reg_p.z) { goto top_ex; } // branch to leave if set to zero, because we're at the top
	set_a(reg_y); // otherwise set to A
	reg_p.c = 1;
	sub_a(0x10); // subtract $10 to move up one row in the block buffer
	ram[0x0002] = reg_a; // store as new vertical high nybble offset
	set_y(reg_a);
	set_a(mem_r(*(uint16_t*)&ram[0x0006] + reg_y)); // get contents of block buffer in same column, one row up
	cmp_a(0xc2); // is it a coin? (not underwater)
	if (!reg_p.z) { goto top_ex; } // if not, branch to leave
	set_a(0x00);
	mem_w(*(uint16_t*)&ram[0x0006] + reg_y, reg_a); // otherwise put blank metatile where coin was
	smb_remove_coin_axe(); // write blank metatile to vram buffer
	set_x(ram[smb_spr_data_offset_ctrl]); // get control bit
	smb_setup_jump_coin(); // create jumping coin object and update coin variables
top_ex:;
	return; // leave!
	smb_spawn_brick_chunks(); return;
}

void smb_spawn_brick_chunks() {
	set_a(ram[smb_block_x_position + reg_x]); // set horizontal coordinate of block object
	ram[smb_block_orig_x_pos + reg_x] = reg_a; // as original horizontal coordinate here
	set_a(0xf0);
	ram[smb_block_x_speed + reg_x] = reg_a; // set horizontal speed for brick chunk objects
	ram[smb_block_x_speed+2 + reg_x] = reg_a;
	set_a(0xfa);
	ram[smb_block_y_speed + reg_x] = reg_a; // set vertical speed for one
	set_a(0xfc);
	ram[smb_block_y_speed+2 + reg_x] = reg_a; // set lower vertical speed for the other
	set_a(0x00);
	ram[smb_block_y_move_force + reg_x] = reg_a; // init fractional movement force for both
	ram[smb_block_y_move_force+2 + reg_x] = reg_a;
	set_a(ram[smb_block_page_loc + reg_x]);
	ram[smb_block_page_loc+2 + reg_x] = reg_a; // copy page location
	set_a(ram[smb_block_x_position + reg_x]);
	ram[smb_block_x_position+2 + reg_x] = reg_a; // copy horizontal coordinate
	set_a(ram[smb_block_y_position + reg_x]);
	reg_p.c = 0; // add 8 pixels to vertical coordinate
	add_a(0x08); // and save as vertical coordinate for one of them
	ram[smb_block_y_position+2 + reg_x] = reg_a;
	set_a(0xfa);
	ram[smb_block_y_speed + reg_x] = reg_a; // set vertical speed...again??? (redundant)
	return;
	smb_block_objects_core(); return;
}

void smb_block_objects_core() {
	set_a(ram[smb_block_state + reg_x]); // get state of block object
	if (reg_p.z) { goto upd_ste; } // if not set, branch to leave
	and_a(0x0f); // mask out high nybble
	push(reg_a); // push to stack
	set_y(reg_a); // put in Y for now
	set_a(reg_x);
	reg_p.c = 0;
	add_a(0x09); // add 9 bytes to offset (note two block objects are created
	set_x(reg_a); // when using brick chunks, but only one offset for both)
	set_y(reg_y-1); // decrement Y to check for solid block state
	if (reg_p.z) { goto bouncing_block_handler; } // branch if found, otherwise continue for brick chunks
	smb_impose_gravity_block(); // do sub to impose gravity on one block object object
	smb_move_object_horizontally(); // do another sub to move horizontally
	set_a(reg_x);
	reg_p.c = 0; // move onto next block object
	add_a(0x02);
	set_x(reg_a);
	smb_impose_gravity_block(); // do sub to impose gravity on other block object
	smb_move_object_horizontally(); // do another sub to move horizontally
	set_x(ram[smb_object_offset]); // get block object offset used for both
	smb_relative_block_position(); // get relative coordinates
	smb_get_block_offscreen_bits(); // get offscreen information
	smb_draw_brick_chunks(); // draw the brick chunks
	set_a(pull()); // get lower nybble of saved state
	set_y(ram[smb_block_y_high_pos + reg_x]); // check vertical high byte of block object
	if (reg_p.z) { goto upd_ste; } // if above the screen, branch to kill it
	push(reg_a); // otherwise save state back into stack
	set_a(0xf0);
	cmp_a(ram[smb_block_y_position+2 + reg_x]); // check to see if bottom block object went
	if (reg_p.c) { goto chk_top; } // to the bottom of the screen, and branch if not
	ram[smb_block_y_position+2 + reg_x] = reg_a; // otherwise set offscreen coordinate
chk_top:;
	set_a(ram[smb_block_y_position + reg_x]); // get top block object's vertical coordinate
	cmp_a(0xf0); // see if it went to the bottom of the screen
	set_a(pull()); // pull block object state from stack
	if (!reg_p.c) { goto upd_ste; } // if not, branch to save state
	if (reg_p.c) { goto kill_block; } // otherwise do unconditional branch to kill it
bouncing_block_handler:;
	smb_impose_gravity_block(); // do sub to impose gravity on block object
	set_x(ram[smb_object_offset]); // get block object offset
	smb_relative_block_position(); // get relative coordinates
	smb_get_block_offscreen_bits(); // get offscreen information
	smb_draw_block(); // draw the block
	set_a(ram[smb_block_y_position + reg_x]); // get vertical coordinate
	and_a(0x0f); // mask out high nybble
	cmp_a(0x05); // check to see if low nybble wrapped around
	set_a(pull()); // pull state from stack
	if (reg_p.c) { goto upd_ste; } // if still above amount, not time to kill block yet, thus branch
	set_a(0x01);
	ram[smb_block_rep_flag + reg_x] = reg_a; // otherwise set flag to replace metatile
kill_block:;
	set_a(0x00); // if branched here, nullify object state
upd_ste:;
	ram[smb_block_state + reg_x] = reg_a; // store contents of A in block object state
	return;
	smb_block_obj_mt_updater(); return;
}

void smb_block_obj_mt_updater() {
	set_x(0x01); // set offset to start with second block object
update_loop:;
	ram[smb_object_offset] = reg_x; // set offset here
	set_a(ram[smb_vram_buffer_1]); // if vram buffer already being used here,
	if (!reg_p.z) { goto next_b_upd; } // branch to move onto next block object
	set_a(ram[smb_block_rep_flag + reg_x]); // if flag for block object already clear,
	if (reg_p.z) { goto next_b_upd; } // branch to move onto next block object
	set_a(ram[smb_block_b_buf_low + reg_x]); // get low byte of block buffer
	ram[0x0006] = reg_a; // store into block buffer address
	set_a(0x05);
	ram[0x0007] = reg_a; // set high byte of block buffer address
	set_a(ram[smb_block_orig_y_pos + reg_x]); // get original vertical coordinate of block object
	ram[0x0002] = reg_a; // store here and use as offset to block buffer
	set_y(reg_a);
	set_a(ram[smb_block_metatile + reg_x]); // get metatile to be written
	mem_w(*(uint16_t*)&ram[0x0006] + reg_y, reg_a); // write it to the block buffer
	smb_replace_block_metatile(); // do sub to replace metatile where block object is
	set_a(0x00);
	ram[smb_block_rep_flag + reg_x] = reg_a; // clear block object flag
next_b_upd:;
	set_x(reg_x-1); // decrement block object offset
	if (!reg_p.n) { goto update_loop; } // do this until both block objects are dealt with
	return; // then leave
	smb_move_enemy_horizontally(); return;
}

void smb_move_enemy_horizontally() {
	set_x(reg_x+1); // increment offset for enemy offset
	smb_move_object_horizontally(); // position object horizontally according to
	set_x(ram[smb_object_offset]); // counters, return with saved value in A,
	return; // put enemy offset back in X and leave
	smb_move_player_horizontally(); return;
}

void smb_move_player_horizontally() {
	set_a(ram[smb_jumpspring_anim_ctrl]); // if jumpspring currently animating,
	if (!reg_p.z) { smb_ex_x_move(); return; } // branch to leave
	set_x(reg_a); // otherwise set zero for offset to use player's stuff
	smb_move_object_horizontally(); return;
}

void smb_move_object_horizontally() {
	set_a(ram[smb_spr_object_x_speed + reg_x]); // get currently saved value (horizontal
	reg_a = shl(reg_a); // speed, secondary counter, whatever)
	reg_a = shl(reg_a); // and move low nybble to high
	reg_a = shl(reg_a);
	reg_a = shl(reg_a);
	ram[0x0001] = reg_a; // store result here
	set_a(ram[smb_spr_object_x_speed + reg_x]); // get saved value again
	reg_a = shr(reg_a); // move high nybble to low
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	cmp_a(0x08); // if < 8, branch, do not change
	if (!reg_p.c) { goto save_x_spd; }
	or_a(0b11110000); // otherwise alter high nybble
save_x_spd:;
	ram[0x0000] = reg_a; // save result here
	set_y(0x00); // load default Y value here
	cmp_a(0x00); // if result positive, leave Y alone
	if (!reg_p.n) { goto use_adder; }
	set_y(reg_y-1); // otherwise decrement Y
use_adder:;
	ram[0x0002] = reg_y; // save Y here
	set_a(ram[smb_spr_object_x_move_force + reg_x]); // get whatever number's here
	reg_p.c = 0;
	add_a(ram[0x0001]); // add low nybble moved to high
	ram[smb_spr_object_x_move_force + reg_x] = reg_a; // store result here
	set_a(0x00); // init A
	reg_a = rol(reg_a); // rotate carry into d0
	push(reg_a); // push onto stack
	reg_a = ror(reg_a); // rotate d0 back onto carry
	set_a(ram[smb_player_x_position + reg_x]);
	add_a(ram[0x0000]); // add carry plus saved value (high nybble moved to low
	ram[smb_player_x_position + reg_x] = reg_a; // plus $f0 if necessary) to object's horizontal position
	set_a(ram[smb_player_page_loc + reg_x]);
	add_a(ram[0x0002]); // add carry plus other saved value to the
	ram[smb_player_page_loc + reg_x] = reg_a; // object's page location and save
	set_a(pull());
	reg_p.c = 0; // pull old carry from stack and add
	add_a(ram[0x0000]); // to high nybble moved to low
	smb_ex_x_move(); return;
}

void smb_ex_x_move() {
	return; // and leave
	smb_move_player_vertically(); return;
}

void smb_move_player_vertically() {
	set_x(0x00); // set X for player offset
	set_a(ram[smb_timer_control]);
	if (!reg_p.z) { goto no_js_chk; } // if master timer control set, branch ahead
	set_a(ram[smb_jumpspring_anim_ctrl]); // otherwise check to see if jumpspring is animating
	if (!reg_p.z) { smb_ex_x_move(); return; } // branch to leave if so
no_js_chk:;
	set_a(ram[smb_vertical_force]); // dump vertical force
	ram[0x0000] = reg_a;
	set_a(0x04); // set maximum vertical speed here
	smb_impose_gravity_spr_obj(); return; // then jump to move player vertically
	smb_move_d_enemy_vertically(); return;
}

void smb_move_d_enemy_vertically() {
	set_y(0x3d); // set quick movement amount downwards
	set_a(ram[smb_enemy_state + reg_x]); // then check enemy state
	cmp_a(0x05); // if not set to unique state for spiny's egg, go ahead
	if (!reg_p.z) { smb_cont_v_move(); return; } // and use, otherwise set different movement amount, continue on
	smb_move_falling_platform(); return;
}

void smb_move_falling_platform() {
	set_y(0x20); // set movement amount
	smb_cont_v_move(); return;
}

void smb_cont_v_move() {
	smb_set_hi_max(); return; // jump to skip the rest of this
	smb_move_red_p_troopa_down(); return;
}

void smb_move_red_p_troopa_down() {
	set_y(0x00); // set Y to move downwards
	smb_move_red_p_troopa(); return; // skip to movement routine
	smb_move_red_p_troopa_up(); return;
}

void smb_move_red_p_troopa_up() {
	set_y(0x01); // set Y to move upwards
	smb_move_red_p_troopa(); return;
}

void smb_move_red_p_troopa() {
	set_x(reg_x+1); // increment X for enemy offset
	set_a(0x03);
	ram[0x0000] = reg_a; // set downward movement amount here
	set_a(0x06);
	ram[0x0001] = reg_a; // set upward movement amount here
	set_a(0x02);
	ram[0x0002] = reg_a; // set maximum speed here
	set_a(reg_y); // set movement direction in A, and
	smb_red_p_troopa_grav(); return; // jump to move this thing
	smb_move_drop_platform(); return;
}

void smb_move_drop_platform() {
	set_y(0x7f); // set movement amount for drop platform
	if (!reg_p.z) { smb_set_md_max(); return; } // skip ahead of other value set here
	smb_move_enemy_slow_vert(); return;
}

void smb_move_enemy_slow_vert() {
	set_y(0x0f); // set movement amount for bowser/other objects
	smb_set_md_max(); return;
}

void smb_set_md_max() {
	set_a(0x02); // set maximum speed in A
	if (!reg_p.z) { smb_set_x_move_amt(); return; } // unconditional branch
	smb_move_j_enemy_vertically(); return;
}

void smb_move_j_enemy_vertically() {
	set_y(0x1c); // set movement amount for podoboo/other objects
	smb_set_hi_max(); return;
}

void smb_set_hi_max() {
	set_a(0x03); // set maximum speed in A
	smb_set_x_move_amt(); return;
}

void smb_set_x_move_amt() {
	ram[0x0000] = reg_y; // set movement amount here
	set_x(reg_x+1); // increment X for enemy offset
	smb_impose_gravity_spr_obj(); // do a sub to move enemy object downwards
	set_x(ram[smb_object_offset]); // get enemy object buffer offset and leave
	return;
}

void smb_impose_gravity_block() {
	set_y(0x01); // set offset for maximum speed
	set_a(0x50); // set movement amount here
	ram[0x0000] = reg_a;
	set_a(rom[smb_max_spd_block_data + reg_y]); // get maximum speed
	smb_impose_gravity_spr_obj(); return;
}

void smb_impose_gravity_spr_obj() {
	ram[0x0002] = reg_a; // set maximum speed here
	set_a(0x00); // set value to move downwards
	smb_impose_gravity(); return; // jump to the code that actually moves it
	smb_move_platform_down(); return;
}

void smb_move_platform_down() {
	set_a(0x00); // save value to stack (if branching here, execute next
	smb_move_platform(); return;
}

void smb_move_platform_up() {
	set_a(0x01); // save value to stack
	smb_move_platform(); return;
}

void smb_move_platform() {
	push(reg_a);
	set_y(ram[smb_enemy_id + reg_x]); // get enemy object identifier
	set_x(reg_x+1); // increment offset for enemy object
	set_a(0x05); // load default value here
	cmp_y(0x29); // residual comparison, object #29 never executes
	if (!reg_p.z) { goto set_dbl_spd; } // this code, thus unconditional branch here
	set_a(0x09); // residual code
set_dbl_spd:;
	ram[0x0000] = reg_a; // save downward movement amount here
	set_a(0x0a); // save upward movement amount here
	ram[0x0001] = reg_a;
	set_a(0x03); // save maximum vertical speed here
	ram[0x0002] = reg_a;
	set_a(pull()); // get value from stack
	set_y(reg_a); // use as Y, then move onto code shared by red koopa
	smb_red_p_troopa_grav(); return;
}

void smb_red_p_troopa_grav() {
	smb_impose_gravity(); // do a sub to move object gradually
	set_x(ram[smb_object_offset]); // get enemy object offset and leave
	return;
	smb_impose_gravity(); return;
}

void smb_impose_gravity() {
	push(reg_a); // push value to stack
	set_a(ram[smb_spr_object_ymf_dummy + reg_x]);
	reg_p.c = 0; // add value in movement force to contents of dummy variable
	add_a(ram[smb_spr_object_y_move_force + reg_x]);
	ram[smb_spr_object_ymf_dummy + reg_x] = reg_a;
	set_y(0x00); // set Y to zero by default
	set_a(ram[smb_spr_object_y_speed + reg_x]); // get current vertical speed
	if (!reg_p.n) { goto alter_yp; } // if currently moving downwards, do not decrement Y
	set_y(reg_y-1); // otherwise decrement Y
alter_yp:;
	ram[0x0007] = reg_y; // store Y here
	add_a(ram[smb_spr_object_y_position + reg_x]); // add vertical position to vertical speed plus carry
	ram[smb_spr_object_y_position + reg_x] = reg_a; // store as new vertical position
	set_a(ram[smb_spr_object_y_high_pos + reg_x]);
	add_a(ram[0x0007]); // add carry plus contents of $07 to vertical high byte
	ram[smb_spr_object_y_high_pos + reg_x] = reg_a; // store as new vertical high byte
	set_a(ram[smb_spr_object_y_move_force + reg_x]);
	reg_p.c = 0;
	add_a(ram[0x0000]); // add downward movement amount to contents of $0433
	ram[smb_spr_object_y_move_force + reg_x] = reg_a;
	set_a(ram[smb_spr_object_y_speed + reg_x]); // add carry to vertical speed and store
	add_a(0x00);
	ram[smb_spr_object_y_speed + reg_x] = reg_a;
	cmp_a(ram[0x0002]); // compare to maximum speed
	if (reg_p.n) { goto chk_up_m; } // if less than preset value, skip this part
	set_a(ram[smb_spr_object_y_move_force + reg_x]);
	cmp_a(0x80); // if less positively than preset maximum, skip this part
	if (!reg_p.c) { goto chk_up_m; }
	set_a(ram[0x0002]);
	ram[smb_spr_object_y_speed + reg_x] = reg_a; // keep vertical speed within maximum value
	set_a(0x00);
	ram[smb_spr_object_y_move_force + reg_x] = reg_a; // clear fractional
chk_up_m:;
	set_a(pull()); // get value from stack
	if (reg_p.z) { goto ex_v_move; } // if set to zero, branch to leave
	set_a(ram[0x0002]);
	eor_a(0b11111111); // otherwise get two's compliment of maximum speed
	set_y(reg_a);
	set_y(reg_y+1);
	ram[0x0007] = reg_y; // store two's compliment here
	set_a(ram[smb_spr_object_y_move_force + reg_x]);
	reg_p.c = 1; // subtract upward movement amount from contents
	sub_a(ram[0x0001]); // of movement force, note that $01 is twice as large as $00,
	ram[smb_spr_object_y_move_force + reg_x] = reg_a; // thus it effectively undoes add we did earlier
	set_a(ram[smb_spr_object_y_speed + reg_x]);
	sub_a(0x00); // subtract borrow from vertical speed and store
	ram[smb_spr_object_y_speed + reg_x] = reg_a;
	cmp_a(ram[0x0007]); // compare vertical speed to two's compliment
	if (!reg_p.n) { goto ex_v_move; } // if less negatively than preset maximum, skip this part
	set_a(ram[smb_spr_object_y_move_force + reg_x]);
	cmp_a(0x80); // check if fractional part is above certain amount,
	if (reg_p.c) { goto ex_v_move; } // and if so, branch to leave
	set_a(ram[0x0007]);
	ram[smb_spr_object_y_speed + reg_x] = reg_a; // keep vertical speed within maximum value
	set_a(0xff);
	ram[smb_spr_object_y_move_force + reg_x] = reg_a; // clear fractional
ex_v_move:;
	return; // leave!
	smb_enemies_and_loops_core(); return;
}

void smb_enemies_and_loops_core() {
	set_a(ram[smb_enemy_flag + reg_x]); // check data here for MSB set
	push(reg_a); // save in stack
	reg_a = shl(reg_a);
	if (reg_p.c) { goto chk_bowser_f; } // if MSB set in enemy flag, branch ahead of jumps
	set_a(pull()); // get from stack
	if (reg_p.z) { goto chk_area_tsk; } // if data zero, branch
	smb_run_enemy_objects_core(); return; // otherwise, jump to run enemy subroutines
chk_area_tsk:;
	set_a(ram[smb_area_parser_task_num]); // check number of tasks to perform
	and_a(0x07);
	cmp_a(0x07); // if at a specific task, jump and leave
	if (reg_p.z) { goto exit_el_core; }
	smb_proc_loop_command(); return; // otherwise, jump to process loop command/load enemies
chk_bowser_f:;
	set_a(pull()); // get data from stack
	and_a(0b00001111); // mask out high nybble
	set_y(reg_a);
	set_a(ram[smb_enemy_flag + reg_y]); // use as pointer and load same place with different offset
	if (!reg_p.z) { goto exit_el_core; }
	ram[smb_enemy_flag + reg_x] = reg_a; // if second enemy flag not set, also clear first one
exit_el_core:;
	return;
}

void smb_exec_game_loopback() {
	set_a(ram[smb_player_page_loc]); // send player back four pages
	reg_p.c = 1;
	sub_a(0x04);
	ram[smb_player_page_loc] = reg_a;
	set_a(ram[smb_current_page_loc]); // send current page back four pages
	reg_p.c = 1;
	sub_a(0x04);
	ram[smb_current_page_loc] = reg_a;
	set_a(ram[smb_screen_left_page_loc]); // subtract four from page location
	reg_p.c = 1; // of screen's left border
	sub_a(0x04);
	ram[smb_screen_left_page_loc] = reg_a;
	set_a(ram[smb_screen_right_page_loc]); // do the same for the page location
	reg_p.c = 1; // of screen's right border
	sub_a(0x04);
	ram[smb_screen_right_page_loc] = reg_a;
	set_a(ram[smb_area_object_page_loc]); // subtract four from page control
	reg_p.c = 1; // for area objects
	sub_a(0x04);
	ram[smb_area_object_page_loc] = reg_a;
	set_a(0x00); // initialize page select for both
	ram[smb_enemy_object_page_sel] = reg_a; // area and enemy objects
	ram[smb_area_object_page_sel] = reg_a;
	ram[smb_enemy_data_offset] = reg_a; // initialize enemy object data offset
	ram[smb_enemy_object_page_loc] = reg_a; // and enemy object page control
	set_a(rom[smb_area_data_ofs_loopback + reg_y]); // adjust area object offset based on
	ram[smb_area_data_offset] = reg_a; // which loop command we encountered
	return;
	smb_proc_loop_command(); return;
}

void smb_proc_loop_command() {
	set_a(ram[smb_loop_command]); // check if loop command was found
	if (reg_p.z) { goto chk_enemy_frenzy; }
	set_a(ram[smb_current_column_pos]); // check to see if we're still on the first page
	if (!reg_p.z) { goto chk_enemy_frenzy; } // if not, do not loop yet
	set_y(0x0b); // start at the end of each set of loop data
find_loop:;
	set_y(reg_y-1);
	if (reg_p.n) { goto chk_enemy_frenzy; } // if all data is checked and not match, do not loop
	set_a(ram[smb_world_number]); // check to see if one of the world numbers
	cmp_a(rom[smb_loop_cmd_world_number + reg_y]); // matches our current world number
	if (!reg_p.z) { goto find_loop; }
	set_a(ram[smb_current_page_loc]); // check to see if one of the page numbers
	cmp_a(rom[smb_loop_cmd_page_number + reg_y]); // matches the page we're currently on
	if (!reg_p.z) { goto find_loop; }
	set_a(ram[smb_player_y_position]); // check to see if the player is at the correct position
	cmp_a(rom[smb_loop_cmd_y_position + reg_y]); // if not, branch to check for world 7
	if (!reg_p.z) { goto wrong_chk; }
	set_a(ram[smb_player_state]); // check to see if the player is
	cmp_a(0x00); // on solid ground (i.e. not jumping or falling)
	if (!reg_p.z) { goto wrong_chk; } // if not, player fails to pass loop, and loopback
	set_a(ram[smb_world_number]); // are we in world 7? (check performed on correct
	cmp_a((smb_world_7)); // vertical position and on solid ground)
	if (!reg_p.z) { goto init_m_lp; } // if not, initialize flags used there, otherwise
	ram[smb_multi_loop_correct_cntr] = inc(ram[smb_multi_loop_correct_cntr]); // increment counter for correct progression
inc_m_loop:;
	ram[smb_multi_loop_pass_cntr] = inc(ram[smb_multi_loop_pass_cntr]); // increment master multi-part counter
	set_a(ram[smb_multi_loop_pass_cntr]); // have we done all three parts?
	cmp_a(0x03);
	if (!reg_p.z) { goto init_l_cmd; } // if not, skip this part
	set_a(ram[smb_multi_loop_correct_cntr]); // if so, have we done them all correctly?
	cmp_a(0x03);
	if (reg_p.z) { goto init_m_lp; } // if so, branch past unnecessary check here
	if (!reg_p.z) { goto do_lp_back; } // unconditional branch if previous branch fails
wrong_chk:;
	set_a(ram[smb_world_number]); // are we in world 7? (check performed on
	cmp_a((smb_world_7)); // incorrect vertical position or not on solid ground)
	if (reg_p.z) { goto inc_m_loop; }
do_lp_back:;
	smb_exec_game_loopback(); // if player is not in right place, loop back
	smb_kill_all_enemies();
init_m_lp:;
	set_a(0x00); // initialize counters used for multi-part loop commands
	ram[smb_multi_loop_pass_cntr] = reg_a;
	ram[smb_multi_loop_correct_cntr] = reg_a;
init_l_cmd:;
	set_a(0x00); // initialize loop command flag
	ram[smb_loop_command] = reg_a;
chk_enemy_frenzy:;
	set_a(ram[smb_enemy_frenzy_queue]); // check for enemy object in frenzy queue
	if (reg_p.z) { goto process_enemy_data; } // if not, skip this part
	ram[smb_enemy_id + reg_x] = reg_a; // store as enemy object identifier here
	set_a(0x01);
	ram[smb_enemy_flag + reg_x] = reg_a; // activate enemy object flag
	set_a(0x00);
	ram[smb_enemy_state + reg_x] = reg_a; // initialize state and frenzy queue
	ram[smb_enemy_frenzy_queue] = reg_a;
	smb_init_enemy_object(); return; // and then jump to deal with this enemy
process_enemy_data:;
	set_y(ram[smb_enemy_data_offset]); // get offset of enemy object data
	set_a(mem_r(*(uint16_t*)&ram[smb_enemy_data] + reg_y)); // load first byte
	cmp_a(0xff); // check for EOD terminator
	if (!reg_p.z) { goto chk_endof_buffer; }
	goto check_frenzy_buffer; // if found, jump to check frenzy buffer, otherwise
chk_endof_buffer:;
	and_a(0b00001111); // check for special row $0e
	cmp_a(0x0e);
	if (reg_p.z) { goto check_right_bounds; } // if found, branch, otherwise
	cmp_x(0x05); // check for end of buffer
	if (!reg_p.c) { goto check_right_bounds; } // if not at end of buffer, branch
	set_y(reg_y+1);
	set_a(mem_r(*(uint16_t*)&ram[smb_enemy_data] + reg_y)); // check for specific value here
	and_a(0b00111111); // not sure what this was intended for, exactly
	cmp_a(0x2e); // this part is quite possibly residual code
	if (reg_p.z) { goto check_right_bounds; } // but it has the effect of keeping enemies out of
	return; // the sixth slot
check_right_bounds:;
	set_a(ram[smb_screen_right_x_pos]); // add 48 to pixel coordinate of right boundary
	reg_p.c = 0;
	add_a(0x30);
	and_a(0b11110000); // store high nybble
	ram[0x0007] = reg_a;
	set_a(ram[smb_screen_right_page_loc]); // add carry to page location of right boundary
	add_a(0x00);
	ram[0x0006] = reg_a; // store page location + carry
	set_y(ram[smb_enemy_data_offset]);
	set_y(reg_y+1);
	set_a(mem_r(*(uint16_t*)&ram[smb_enemy_data] + reg_y)); // if MSB of enemy object is clear, branch to check for row $0f
	reg_a = shl(reg_a);
	if (!reg_p.c) { goto check_page_ctrl_row; }
	set_a(ram[smb_enemy_object_page_sel]); // if page select already set, do not set again
	if (!reg_p.z) { goto check_page_ctrl_row; }
	ram[smb_enemy_object_page_sel] = inc(ram[smb_enemy_object_page_sel]); // otherwise, if MSB is set, set page select
	ram[smb_enemy_object_page_loc] = inc(ram[smb_enemy_object_page_loc]); // and increment page control
check_page_ctrl_row:;
	set_y(reg_y-1);
	set_a(mem_r(*(uint16_t*)&ram[smb_enemy_data] + reg_y)); // reread first byte
	and_a(0x0f);
	cmp_a(0x0f); // check for special row $0f
	if (!reg_p.z) { goto position_enemy_obj; } // if not found, branch to position enemy object
	set_a(ram[smb_enemy_object_page_sel]); // if page select set,
	if (!reg_p.z) { goto position_enemy_obj; } // branch without reading second byte
	set_y(reg_y+1);
	set_a(mem_r(*(uint16_t*)&ram[smb_enemy_data] + reg_y)); // otherwise, get second byte, mask out 2 MSB
	and_a(0b00111111);
	ram[smb_enemy_object_page_loc] = reg_a; // store as page control for enemy object data
	ram[smb_enemy_data_offset] = inc(ram[smb_enemy_data_offset]); // increment enemy object data offset 2 bytes
	ram[smb_enemy_data_offset] = inc(ram[smb_enemy_data_offset]);
	ram[smb_enemy_object_page_sel] = inc(ram[smb_enemy_object_page_sel]); // set page select for enemy object data and
	smb_proc_loop_command(); return; // jump back to process loop commands again
position_enemy_obj:;
	set_a(ram[smb_enemy_object_page_loc]); // store page control as page location
	ram[smb_enemy_page_loc + reg_x] = reg_a; // for enemy object
	set_a(mem_r(*(uint16_t*)&ram[smb_enemy_data] + reg_y)); // get first byte of enemy object
	and_a(0b11110000);
	ram[smb_enemy_x_position + reg_x] = reg_a; // store column position
	cmp_a(ram[smb_screen_right_x_pos]); // check column position against right boundary
	set_a(ram[smb_enemy_page_loc + reg_x]); // without subtracting, then subtract borrow
	sub_a(ram[smb_screen_right_page_loc]); // from page location
	if (reg_p.c) { goto check_right_ext_bounds; } // if enemy object beyond or at boundary, branch
	set_a(mem_r(*(uint16_t*)&ram[smb_enemy_data] + reg_y));
	and_a(0b00001111); // check for special row $0e
	cmp_a(0x0e); // if found, jump elsewhere
	if (reg_p.z) { smb_parse_row_0_e(); return; }
	smb_check_three_bytes(); return; // if not found, unconditional jump
check_right_ext_bounds:;
	set_a(ram[0x0007]); // check right boundary + 48 against
	cmp_a(ram[smb_enemy_x_position + reg_x]); // column position without subtracting,
	set_a(ram[0x0006]); // then subtract borrow from page control temp
	sub_a(ram[smb_enemy_page_loc + reg_x]); // plus carry
	if (!reg_p.c) { goto check_frenzy_buffer; } // if enemy object beyond extended boundary, branch
	set_a(0x01); // store value in vertical high byte
	ram[smb_enemy_y_high_pos + reg_x] = reg_a;
	set_a(mem_r(*(uint16_t*)&ram[smb_enemy_data] + reg_y)); // get first byte again
	reg_a = shl(reg_a); // multiply by four to get the vertical
	reg_a = shl(reg_a); // coordinate
	reg_a = shl(reg_a);
	reg_a = shl(reg_a);
	ram[smb_enemy_y_position + reg_x] = reg_a;
	cmp_a(0xe0); // do one last check for special row $0e
	if (reg_p.z) { smb_parse_row_0_e(); return; } // (necessary if branched to $c1cb)
	set_y(reg_y+1);
	set_a(mem_r(*(uint16_t*)&ram[smb_enemy_data] + reg_y)); // get second byte of object
	and_a(0b01000000); // check to see if hard mode bit is set
	if (reg_p.z) { goto check_for_enemy_group; } // if not, branch to check for group enemy objects
	set_a(ram[smb_secondary_hard_mode]); // if set, check to see if secondary hard mode flag
	if (reg_p.z) { smb_inc_2_b(); return; } // is on, and if not, branch to skip this object completely
check_for_enemy_group:;
	set_a(mem_r(*(uint16_t*)&ram[smb_enemy_data] + reg_y)); // get second byte and mask out 2 MSB
	and_a(0b00111111);
	cmp_a(0x37); // check for value below $37
	if (!reg_p.c) { goto buzzy_beetle_mutate; }
	cmp_a(0x3f); // if $37 or greater, check for value
	if (!reg_p.c) { smb_do_group(); return; } // below $3f, branch if below $3f
buzzy_beetle_mutate:;
	cmp_a((smb_goomba)); // if below $37, check for goomba
	if (!reg_p.z) { goto str_id; } // value ($3f or more always fails)
	set_y(ram[smb_primary_hard_mode]); // check if primary hard mode flag is set
	if (reg_p.z) { goto str_id; } // and if so, change goomba to buzzy beetle
	set_a((smb_buzzy_beetle));
str_id:;
	ram[smb_enemy_id + reg_x] = reg_a; // store enemy object number into buffer
	set_a(0x01);
	ram[smb_enemy_flag + reg_x] = reg_a; // set flag for enemy in buffer
	smb_init_enemy_object();
	set_a(ram[smb_enemy_flag + reg_x]); // check to see if flag is set
	if (!reg_p.z) { smb_inc_2_b(); return; } // if not, leave, otherwise branch
	return;
check_frenzy_buffer:;
	set_a(ram[smb_enemy_frenzy_buffer]); // if enemy object stored in frenzy buffer
	if (!reg_p.z) { goto str_fre; } // then branch ahead to store in enemy object buffer
	set_a(ram[smb_vine_flag_offset]); // otherwise check vine flag offset
	cmp_a(0x01);
	if (!reg_p.z) { smb_ex_e_par(); return; } // if other value <> 1, leave
	set_a((smb_vine_object)); // otherwise put vine in enemy identifier
str_fre:;
	ram[smb_enemy_id + reg_x] = reg_a; // store contents of frenzy buffer into enemy identifier value
	smb_init_enemy_object(); return;
}

void smb_init_enemy_object() {
	set_a(0x00); // initialize enemy state
	ram[smb_enemy_state + reg_x] = reg_a;
	smb_checkpoint_enemy_id(); // jump ahead to run jump engine and subroutines
	smb_ex_e_par(); return;
}

void smb_ex_e_par() {
	return; // then leave
	smb_do_group(); return;
}

void smb_do_group() {
	smb_handle_group_enemies(); return; // handle enemy group objects
	smb_parse_row_0_e(); return;
}

void smb_parse_row_0_e() {
	set_y(reg_y+1); // increment Y to load third byte of object
	set_y(reg_y+1);
	set_a(mem_r(*(uint16_t*)&ram[smb_enemy_data] + reg_y));
	reg_a = shr(reg_a); // move 3 MSB to the bottom, effectively
	reg_a = shr(reg_a); // making %xxx00000 into %00000xxx
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	cmp_a(ram[smb_world_number]); // is it the same world number as we're on?
	if (!reg_p.z) { goto not_use; } // if not, do not use (this allows multiple uses
	set_y(reg_y-1); // of the same area, like the underground bonus areas)
	set_a(mem_r(*(uint16_t*)&ram[smb_enemy_data] + reg_y)); // otherwise, get second byte and use as offset
	ram[smb_area_pointer] = reg_a; // to addresses for level and enemy object data
	set_y(reg_y+1);
	set_a(mem_r(*(uint16_t*)&ram[smb_enemy_data] + reg_y)); // get third byte again, and this time mask out
	and_a(0b00011111); // the 3 MSB from before, save as page number to be
	ram[smb_entrance_page] = reg_a; // used upon entry to area, if area is entered
not_use:;
	smb_inc_3_b(); return;
	smb_check_three_bytes(); return;
}

void smb_check_three_bytes() {
	set_y(ram[smb_enemy_data_offset]);
	set_a(mem_r(*(uint16_t*)&ram[smb_enemy_data] + reg_y));
	and_a(0x0f);
	cmp_a(0x0e);
	if (!reg_p.z) { smb_inc_2_b(); return; }
	smb_inc_3_b(); return;
}

void smb_inc_3_b() {
	ram[smb_enemy_data_offset] = inc(ram[smb_enemy_data_offset]);
	smb_inc_2_b(); return;
}

void smb_inc_2_b() {
	ram[smb_enemy_data_offset] = inc(ram[smb_enemy_data_offset]);
	ram[smb_enemy_data_offset] = inc(ram[smb_enemy_data_offset]);
	set_a(0x00);
	ram[smb_enemy_object_page_sel] = reg_a;
	set_x(ram[smb_object_offset]);
	return;
	smb_checkpoint_enemy_id(); return;
}

void smb_checkpoint_enemy_id() {
	set_a(ram[smb_enemy_id + reg_x]);
	cmp_a(0x15);
	if (reg_p.c) { goto init_enemy_routines; }
	set_y(reg_a);
	set_a(ram[smb_enemy_y_position + reg_x]);
	add_a(0x08);
	ram[smb_enemy_y_position + reg_x] = reg_a;
	set_a(0x01);
	ram[smb_enemy_offscr_bits_masked + reg_x] = reg_a;
	set_a(reg_y);
init_enemy_routines:;
	static void(*targets[])() = {
		smb_init_normal_enemy,
		smb_init_normal_enemy,
		smb_init_normal_enemy,
		smb_init_red_koopa,
		smb_no_init_code,
		smb_init_hammer_bro,
		smb_init_goomba,
		smb_init_bloober,
		smb_init_bullet_bill,
		smb_no_init_code,
		smb_init_cheep_cheep,
		smb_init_cheep_cheep,
		smb_init_podoboo,
		smb_init_piranha_plant,
		smb_init_jump_gp_troopa,
		smb_init_red_p_troopa,
		smb_init_horiz_fly_swim_enemy,
		smb_init_lakitu,
		smb_init_enemy_frenzy,
		smb_no_init_code,
		smb_init_enemy_frenzy,
		smb_init_enemy_frenzy,
		smb_init_enemy_frenzy,
		smb_init_enemy_frenzy,
		smb_end_frenzy,
		smb_no_init_code,
		smb_no_init_code,
		smb_init_short_firebar,
		smb_init_short_firebar,
		smb_init_short_firebar,
		smb_init_short_firebar,
		smb_init_long_firebar,
		smb_no_init_code,
		smb_no_init_code,
		smb_no_init_code,
		smb_no_init_code,
		smb_init_bal_platform,
		smb_init_vert_platform,
		smb_large_lift_up,
		smb_large_lift_down,
		smb_init_hori_platform,
		smb_init_drop_platform,
		smb_init_hori_platform,
		smb_plat_lift_up,
		smb_plat_lift_down,
		smb_init_bowser,
		smb_pwr_up_jmp,
		smb_setup_vine,
		smb_no_init_code,
		smb_no_init_code,
		smb_no_init_code,
		smb_no_init_code,
		smb_no_init_code,
		smb_init_retainer_obj,
		smb_end_of_enemy_init_code
	};
	targets[reg_a](); return;
	smb_no_init_code(); return;
}

void smb_no_init_code() {
	return; // this executed when enemy object has no init code
	smb_init_goomba(); return;
}

void smb_init_goomba() {
	smb_init_normal_enemy(); // set appropriate horizontal speed
	smb_small_b_box(); return; // set $09 as bounding box control, set other values
	smb_init_podoboo(); return;
}

void smb_init_podoboo() {
	set_a(0x02); // set enemy position to below
	ram[smb_enemy_y_high_pos + reg_x] = reg_a; // the bottom of the screen
	ram[smb_enemy_y_position + reg_x] = reg_a;
	reg_a = shr(reg_a);
	ram[smb_enemy_interval_timer + reg_x] = reg_a; // set timer for enemy
	reg_a = shr(reg_a);
	ram[smb_enemy_state + reg_x] = reg_a; // initialize enemy state, then jump to use
	smb_small_b_box(); return; // $09 as bounding box size and set other things
	smb_init_retainer_obj(); return;
}

void smb_init_retainer_obj() {
	set_a(0xb8); // set fixed vertical position for
	ram[smb_enemy_y_position + reg_x] = reg_a; // princess/mushroom retainer object
	return;
}

void smb_init_normal_enemy() {
	set_y(0x01); // load offset of 1 by default
	set_a(ram[smb_primary_hard_mode]); // check for primary hard mode flag set
	if (!reg_p.z) { goto get_e_spd; }
	set_y(reg_y-1); // if not set, decrement offset
get_e_spd:;
	set_a(rom[smb_normal_x_spd_data + reg_y]); // get appropriate horizontal speed
	smb_set_e_spd(); return;
}

void smb_set_e_spd() {
	ram[smb_blooper_move_speed + reg_x] = reg_a; // store as speed for enemy object
	smb_tall_b_box(); return; // branch to set bounding box control and other data
	smb_init_red_koopa(); return;
}

void smb_init_red_koopa() {
	smb_init_normal_enemy(); // load appropriate horizontal speed
	set_a(0x01); // set enemy state for red koopa troopa $03
	ram[smb_enemy_state + reg_x] = reg_a;
	return;
}

void smb_init_hammer_bro() {
	set_a(0x00); // init horizontal speed and timer used by hammer bro
	ram[smb_hammer_throwing_timer + reg_x] = reg_a; // apparently to time hammer throwing
	ram[smb_enemy_x_speed + reg_x] = reg_a;
	set_y(ram[smb_secondary_hard_mode]); // get secondary hard mode flag
	set_a(rom[smb_h_bro_walking_timer_data + reg_y]);
	ram[smb_enemy_interval_timer + reg_x] = reg_a; // set value as delay for hammer bro to walk left
	set_a(0x0b); // set specific value for bounding box size control
	smb_set_b_box(); return;
	smb_init_horiz_fly_swim_enemy(); return;
}

void smb_init_horiz_fly_swim_enemy() {
	set_a(0x00); // initialize horizontal speed
	smb_set_e_spd(); return;
	smb_init_bloober(); return;
}

void smb_init_bloober() {
	set_a(0x00); // initialize horizontal speed
	ram[smb_blooper_move_speed + reg_x] = reg_a;
	smb_small_b_box(); return;
}

void smb_small_b_box() {
	set_a(0x09); // set specific bounding box size control
	if (!reg_p.z) { smb_set_b_box(); return; } // unconditional branch
	smb_init_red_p_troopa(); return;
}

void smb_init_red_p_troopa() {
	set_y(0x30); // load central position adder for 48 pixels down
	set_a(ram[smb_enemy_y_position + reg_x]); // set vertical coordinate into location to
	ram[smb_red_p_troopa_orig_x_pos + reg_x] = reg_a; // be used as original vertical coordinate
	if (!reg_p.n) { goto get_cent; } // if vertical coordinate < $80
	set_y(0xe0); // if => $80, load position adder for 32 pixels up
get_cent:;
	set_a(reg_y); // send central position adder to A
	add_a(ram[smb_enemy_y_position + reg_x]); // add to current vertical coordinate
	ram[smb_red_p_troopa_center_y_pos + reg_x] = reg_a; // store as central vertical coordinate
	smb_tall_b_box(); return;
}

void smb_tall_b_box() {
	set_a(0x03); // set specific bounding box size control
	smb_set_b_box(); return;
}

void smb_set_b_box() {
	ram[smb_enemy_bound_box_ctrl + reg_x] = reg_a; // set bounding box control here
	set_a(0x02); // set moving direction for left
	ram[smb_enemy_moving_dir + reg_x] = reg_a;
	smb_init_v_stf(); return;
}

void smb_init_v_stf() {
	set_a(0x00); // initialize vertical speed
	ram[smb_enemy_y_speed + reg_x] = reg_a; // and movement force
	ram[smb_enemy_y_move_force + reg_x] = reg_a;
	return;
	smb_init_bullet_bill(); return;
}

void smb_init_bullet_bill() {
	set_a(0x02); // set moving direction for left
	ram[smb_enemy_moving_dir + reg_x] = reg_a;
	set_a(0x09); // set bounding box control for $09
	ram[smb_enemy_bound_box_ctrl + reg_x] = reg_a;
	return;
	smb_init_cheep_cheep(); return;
}

void smb_init_cheep_cheep() {
	smb_small_b_box(); // set vertical bounding box, speed, init others
	set_a(ram[smb_pseudo_random_bit_reg + reg_x]); // check one portion of LSFR
	and_a(0b00010000); // get d4 from it
	ram[smb_cheep_cheep_move_m_flag + reg_x] = reg_a; // save as movement flag of some sort
	set_a(ram[smb_enemy_y_position + reg_x]);
	ram[smb_cheep_cheep_orig_y_pos + reg_x] = reg_a; // save original vertical coordinate here
	return;
	smb_init_lakitu(); return;
}

void smb_init_lakitu() {
	set_a(ram[smb_enemy_frenzy_buffer]); // check to see if an enemy is already in
	if (!reg_p.z) { smb_kill_lakitu(); return; } // the frenzy buffer, and branch to kill lakitu if so
	smb_setup_lakitu(); return;
}

void smb_setup_lakitu() {
	set_a(0x00); // erase counter for lakitu's reappearance
	ram[smb_lakitu_reappear_timer] = reg_a;
	smb_init_horiz_fly_swim_enemy(); // set $03 as bounding box, set other attributes
	smb_tall_b_box_2(); return; // set $03 as bounding box again (not necessary) and leave
	smb_kill_lakitu(); return;
}

void smb_kill_lakitu() {
	smb_erase_enemy_object(); return;
}

void smb_lakitu_and_spiny_handler() {
	set_a(ram[smb_frenzy_enemy_timer]); // if timer here not expired, leave
	if (!reg_p.z) { goto ex_ls_hand; }
	cmp_x(0x05); // if we are on the special use slot, leave
	if (reg_p.c) { goto ex_ls_hand; }
	set_a(0x80); // set timer
	ram[smb_frenzy_enemy_timer] = reg_a;
	set_y(0x04); // start with the last enemy slot
chk_lak:;
	set_a(ram[smb_enemy_id + reg_y]); // check all enemy slots to see
	cmp_a((smb_lakitu)); // if lakitu is on one of them
	if (reg_p.z) { goto create_spiny; } // if so, branch out of this loop
	set_y(reg_y-1); // otherwise check another slot
	if (!reg_p.n) { goto chk_lak; } // loop until all slots are checked
	ram[smb_lakitu_reappear_timer] = inc(ram[smb_lakitu_reappear_timer]); // increment reappearance timer
	set_a(ram[smb_lakitu_reappear_timer]);
	cmp_a(0x07); // check to see if we're up to a certain value yet
	if (!reg_p.c) { goto ex_ls_hand; } // if not, leave
	set_x(0x04); // start with the last enemy slot again
chk_no_en:;
	set_a(ram[smb_enemy_flag + reg_x]); // check enemy buffer flag for non-active enemy slot
	if (reg_p.z) { goto create_l; } // branch out of loop if found
	set_x(reg_x-1); // otherwise check next slot
	if (!reg_p.n) { goto chk_no_en; } // branch until all slots are checked
	if (reg_p.n) { goto ret_e_ofs; } // if no empty slots were found, branch to leave
create_l:;
	set_a(0x00); // initialize enemy state
	ram[smb_enemy_state + reg_x] = reg_a;
	set_a((smb_lakitu)); // create lakitu enemy object
	ram[smb_enemy_id + reg_x] = reg_a;
	smb_setup_lakitu(); // do a sub to set up lakitu
	set_a(0x20);
	smb_put_at_right_extent(); // finish setting up lakitu
ret_e_ofs:;
	set_x(ram[smb_object_offset]); // get enemy object buffer offset again and leave
ex_ls_hand:;
	return;
create_spiny:;
	set_a(ram[smb_player_y_position]); // if player above a certain point, branch to leave
	cmp_a(0x2c);
	if (!reg_p.c) { goto ex_ls_hand; }
	set_a(ram[smb_enemy_state + reg_y]); // if lakitu is not in normal state, branch to leave
	if (!reg_p.z) { goto ex_ls_hand; }
	set_a(ram[smb_enemy_page_loc + reg_y]); // store horizontal coordinates (high and low) of lakitu
	ram[smb_enemy_page_loc + reg_x] = reg_a; // into the coordinates of the spiny we're going to create
	set_a(ram[smb_enemy_x_position + reg_y]);
	ram[smb_enemy_x_position + reg_x] = reg_a;
	set_a(0x01); // put spiny within vertical screen unit
	ram[smb_enemy_y_high_pos + reg_x] = reg_a;
	set_a(ram[smb_enemy_y_position + reg_y]); // put spiny eight pixels above where lakitu is
	reg_p.c = 1;
	sub_a(0x08);
	ram[smb_enemy_y_position + reg_x] = reg_a;
	set_a(ram[smb_pseudo_random_bit_reg + reg_x]); // get 2 LSB of LSFR and save to Y
	and_a(0b00000011);
	set_y(reg_a);
	set_x(0x02);
dif_loop:;
	set_a(rom[smb_pr_diff_adjust_data + reg_y]); // get three values and save them
	ram[0x0001 + reg_x] = reg_a; // to $01-$03
	set_y(reg_y+1);
	set_y(reg_y+1); // increment Y four bytes for each value
	set_y(reg_y+1);
	set_y(reg_y+1);
	set_x(reg_x-1); // decrement X for each one
	if (!reg_p.n) { goto dif_loop; } // loop until all three are written
	set_x(ram[smb_object_offset]); // get enemy object buffer offset
	smb_player_lakitu_diff(); // move enemy, change direction, get value - difference
	set_y(ram[smb_player_x_speed]); // check player's horizontal speed
	cmp_y(0x08);
	if (reg_p.c) { goto set_sp_spd; } // if moving faster than a certain amount, branch elsewhere
	set_y(reg_a); // otherwise save value in A to Y for now
	set_a(ram[smb_pseudo_random_bit_reg+1 + reg_x]);
	and_a(0b00000011); // get one of the LSFR parts and save the 2 LSB
	if (reg_p.z) { goto use_posv; } // branch if neither bits are set
	set_a(reg_y);
	eor_a(0b11111111); // otherwise get two's compliment of Y
	set_y(reg_a);
	set_y(reg_y+1);
use_posv:;
	set_a(reg_y); // put value from A in Y back to A (they will be lost anyway)
set_sp_spd:;
	smb_small_b_box(); // set bounding box control, init attributes, lose contents of A
	set_y(0x02);
	ram[smb_enemy_x_speed + reg_x] = reg_a; // set horizontal speed to zero because previous contents
	cmp_a(0x00); // of A were lost...branch here will never be taken for
	if (reg_p.n) { goto spiny_rte; } // the same reason
	set_y(reg_y-1);
spiny_rte:;
	ram[smb_enemy_moving_dir + reg_x] = reg_y; // set moving direction to the right
	set_a(0xfd);
	ram[smb_blooper_move_counter + reg_x] = reg_a; // set vertical speed to move upwards
	set_a(0x01);
	ram[smb_enemy_flag + reg_x] = reg_a; // enable enemy object by setting flag
	set_a(0x05);
	ram[smb_enemy_state + reg_x] = reg_a; // put spiny in egg state and leave
	smb_chp_chp_ex(); return;
}

void smb_chp_chp_ex() {
	return;
}

void smb_init_long_firebar() {
	smb_duplicate_enemy_obj(); // create enemy object for long firebar
	smb_init_short_firebar(); return;
}

void smb_init_short_firebar() {
	set_a(0x00); // initialize low byte of spin state
	ram[smb_firebar_spin_state_low + reg_x] = reg_a;
	set_a(ram[smb_enemy_id + reg_x]); // subtract $1b from enemy identifier
	reg_p.c = 1; // to get proper offset for firebar data
	sub_a(0x1b);
	set_y(reg_a);
	set_a(rom[smb_firebar_spin_spd_data + reg_y]); // get spinning speed of firebar
	ram[smb_firebar_spin_speed + reg_x] = reg_a;
	set_a(rom[smb_firebar_spin_dir_data + reg_y]); // get spinning direction of firebar
	ram[smb_destination_page_loc + reg_x] = reg_a;
	set_a(ram[smb_enemy_y_position + reg_x]);
	reg_p.c = 0; // add four pixels to vertical coordinate
	add_a(0x04);
	ram[smb_enemy_y_position + reg_x] = reg_a;
	set_a(ram[smb_enemy_x_position + reg_x]);
	reg_p.c = 0; // add four pixels to horizontal coordinate
	add_a(0x04);
	ram[smb_enemy_x_position + reg_x] = reg_a;
	set_a(ram[smb_enemy_page_loc + reg_x]);
	add_a(0x00); // add carry to page location
	ram[smb_enemy_page_loc + reg_x] = reg_a;
	smb_tall_b_box_2(); return; // set bounding box control (not used) and leave
}

void smb_init_flying_cheep_cheep() {
	set_a(ram[smb_frenzy_enemy_timer]); // if timer here not expired yet, branch to leave
	if (!reg_p.z) { smb_chp_chp_ex(); return; }
	smb_small_b_box(); // jump to set bounding box size $09 and init other values
	set_a(ram[smb_pseudo_random_bit_reg+1 + reg_x]);
	and_a(0b00000011); // set pseudorandom offset here
	set_y(reg_a);
	set_a(rom[smb_fly_cc_timer_data + reg_y]); // load timer with pseudorandom offset
	ram[smb_frenzy_enemy_timer] = reg_a;
	set_y(0x03); // load Y with default value
	set_a(ram[smb_secondary_hard_mode]);
	if (reg_p.z) { goto max_cc; } // if secondary hard mode flag not set, do not increment Y
	set_y(reg_y+1); // otherwise, increment Y to allow as many as four onscreen
max_cc:;
	ram[0x0000] = reg_y; // store whatever pseudorandom bits are in Y
	cmp_x(ram[0x0000]); // compare enemy object buffer offset with Y
	if (reg_p.c) { smb_chp_chp_ex(); return; } // if X => Y, branch to leave
	set_a(ram[smb_pseudo_random_bit_reg + reg_x]);
	and_a(0b00000011); // get last two bits of LSFR, first part
	ram[0x0000] = reg_a; // and store in two places
	ram[0x0001] = reg_a;
	set_a(0xfb); // set vertical speed for cheep-cheep
	ram[smb_enemy_y_speed + reg_x] = reg_a;
	set_a(0x00); // load default value
	set_y(ram[smb_player_x_speed]); // check player's horizontal speed
	if (reg_p.z) { goto g_seed; } // if player not moving left or right, skip this part
	set_a(0x04);
	cmp_y(0x19); // if moving to the right but not very quickly,
	if (!reg_p.c) { goto g_seed; } // do not change A
	reg_a = shl(reg_a); // otherwise, multiply A by 2
g_seed:;
	push(reg_a); // save to stack
	reg_p.c = 0;
	add_a(ram[0x0000]); // add to last two bits of LSFR we saved earlier
	ram[0x0000] = reg_a; // save it there
	set_a(ram[smb_pseudo_random_bit_reg+1 + reg_x]);
	and_a(0b00000011); // if neither of the last two bits of second LSFR set,
	if (reg_p.z) { goto r_seed; } // skip this part and save contents of $00
	set_a(ram[smb_pseudo_random_bit_reg+2 + reg_x]);
	and_a(0b00001111); // otherwise overwrite with lower nybble of
	ram[0x0000] = reg_a; // third LSFR part
r_seed:;
	set_a(pull()); // get value from stack we saved earlier
	reg_p.c = 0;
	add_a(ram[0x0001]); // add to last two bits of LSFR we saved in other place
	set_y(reg_a); // use as pseudorandom offset here
	set_a(rom[smb_fly_ccx_speed_data + reg_y]); // get horizontal speed using pseudorandom offset
	ram[smb_enemy_x_speed + reg_x] = reg_a;
	set_a(0x01); // set to move towards the right
	ram[smb_enemy_moving_dir + reg_x] = reg_a;
	set_a(ram[smb_player_x_speed]); // if player moving left or right, branch ahead of this part
	if (!reg_p.z) { goto d_2_x_pos_1; }
	set_y(ram[0x0000]); // get first LSFR or third LSFR lower nybble
	set_a(reg_y); // and check for d1 set
	and_a(0b00000010);
	if (reg_p.z) { goto d_2_x_pos_1; } // if d1 not set, branch
	set_a(ram[smb_enemy_x_speed + reg_x]);
	eor_a(0xff); // if d1 set, change horizontal speed
	reg_p.c = 0; // into two's compliment, thus moving in the opposite
	add_a(0x01); // direction
	ram[smb_enemy_x_speed + reg_x] = reg_a;
	ram[smb_enemy_moving_dir + reg_x] = inc(ram[smb_enemy_moving_dir + reg_x]); // increment to move towards the left
d_2_x_pos_1:;
	set_a(reg_y); // get first LSFR or third LSFR lower nybble again
	and_a(0b00000010);
	if (reg_p.z) { goto d_2_x_pos_2; } // check for d1 set again, branch again if not set
	set_a(ram[smb_player_x_position]); // get player's horizontal position
	reg_p.c = 0;
	add_a(rom[smb_fly_ccx_position_data + reg_y]); // if d1 set, add value obtained from pseudorandom offset
	ram[smb_enemy_x_position + reg_x] = reg_a; // and save as enemy's horizontal position
	set_a(ram[smb_player_page_loc]); // get player's page location
	add_a(0x00); // add carry and jump past this part
	goto fin_c_cst;
d_2_x_pos_2:;
	set_a(ram[smb_player_x_position]); // get player's horizontal position
	reg_p.c = 1;
	sub_a(rom[smb_fly_ccx_position_data + reg_y]); // if d1 not set, subtract value obtained from pseudorandom
	ram[smb_enemy_x_position + reg_x] = reg_a; // offset and save as enemy's horizontal position
	set_a(ram[smb_player_page_loc]); // get player's page location
	sub_a(0x00); // subtract borrow
fin_c_cst:;
	ram[smb_enemy_page_loc + reg_x] = reg_a; // save as enemy's page location
	set_a(0x01);
	ram[smb_enemy_flag + reg_x] = reg_a; // set enemy's buffer flag
	ram[smb_enemy_y_high_pos + reg_x] = reg_a; // set enemy's high vertical byte
	set_a(0xf8);
	ram[smb_enemy_y_position + reg_x] = reg_a; // put enemy below the screen, and we are done
	return;
	smb_init_bowser(); return;
}

void smb_init_bowser() {
	smb_duplicate_enemy_obj(); // jump to create another bowser object
	ram[smb_bowser_front_offset] = reg_x; // save offset of first here
	set_a(0x00);
	ram[smb_bowser_body_controls] = reg_a; // initialize bowser's body controls
	ram[smb_bridge_collapse_offset] = reg_a; // and bridge collapse offset
	set_a(ram[smb_enemy_x_position + reg_x]);
	ram[smb_bowser_orig_x_pos] = reg_a; // store original horizontal position here
	set_a(0xdf);
	ram[smb_bowser_fire_breath_timer] = reg_a; // store something here
	ram[smb_enemy_moving_dir + reg_x] = reg_a; // and in moving direction
	set_a(0x20);
	ram[smb_bowser_feet_counter] = reg_a; // set bowser's feet timer and in enemy timer
	ram[smb_enemy_frame_timer + reg_x] = reg_a;
	set_a(0x05);
	ram[smb_bowser_hit_points] = reg_a; // give bowser 5 hit points
	reg_a = shr(reg_a);
	ram[smb_bowser_movement_speed] = reg_a; // set default movement speed here
	return;
	smb_duplicate_enemy_obj(); return;
}

void smb_duplicate_enemy_obj() {
	set_y(0xff); // start at beginning of enemy slots
fs_loop:;
	set_y(reg_y+1); // increment one slot
	set_a(ram[smb_enemy_flag + reg_y]); // check enemy buffer flag for empty slot
	if (!reg_p.z) { goto fs_loop; } // if set, branch and keep checking
	ram[smb_duplicate_obj_offset] = reg_y; // otherwise set offset here
	set_a(reg_x); // transfer original enemy buffer offset
	or_a(0b10000000); // store with d7 set as flag in new enemy
	ram[smb_enemy_flag + reg_y] = reg_a; // slot as well as enemy offset
	set_a(ram[smb_enemy_page_loc + reg_x]);
	ram[smb_enemy_page_loc + reg_y] = reg_a; // copy page location and horizontal coordinates
	set_a(ram[smb_enemy_x_position + reg_x]); // from original enemy to new enemy
	ram[smb_enemy_x_position + reg_y] = reg_a;
	set_a(0x01);
	ram[smb_enemy_flag + reg_x] = reg_a; // set flag as normal for original enemy
	ram[smb_enemy_y_high_pos + reg_y] = reg_a; // set high vertical byte for new enemy
	set_a(ram[smb_enemy_y_position + reg_x]);
	ram[smb_enemy_y_position + reg_y] = reg_a; // copy vertical coordinate from original to new
	smb_flm_ex(); return;
}

void smb_flm_ex() {
	return; // and then leave
}

void smb_init_bowser_flame() {
	set_a(ram[smb_frenzy_enemy_timer]); // if timer not expired yet, branch to leave
	if (!reg_p.z) { smb_flm_ex(); return; }
	ram[smb_enemy_y_move_force + reg_x] = reg_a; // reset something here
	set_a(ram[smb_noise_sound_queue]);
	or_a((smb_sfx_bowser_flame)); // load bowser's flame sound into queue
	ram[smb_noise_sound_queue] = reg_a;
	set_y(ram[smb_bowser_front_offset]); // get bowser's buffer offset
	set_a(ram[smb_enemy_id + reg_y]); // check for bowser
	cmp_a((smb_bowser));
	if (reg_p.z) { smb_spawn_from_mouth(); return; } // branch if found
	smb_set_flame_timer(); // get timer data based on flame counter
	reg_p.c = 0;
	add_a(0x20); // add 32 frames by default
	set_y(ram[smb_secondary_hard_mode]);
	if (reg_p.z) { goto set_fr_t; } // if secondary mode flag not set, use as timer setting
	reg_p.c = 1;
	sub_a(0x10); // otherwise subtract 16 frames for secondary hard mode
set_fr_t:;
	ram[smb_frenzy_enemy_timer] = reg_a; // set timer accordingly
	set_a(ram[smb_pseudo_random_bit_reg + reg_x]);
	and_a(0b00000011); // get 2 LSB from first part of LSFR
	ram[smb_bowser_flame_p_random_ofs + reg_x] = reg_a; // set here
	set_y(reg_a); // use as offset
	set_a(rom[smb_flame_y_pos_data + reg_y]); // load vertical position based on pseudorandom offset
	smb_put_at_right_extent(); return;
}

void smb_put_at_right_extent() {
	ram[smb_enemy_y_position + reg_x] = reg_a; // set vertical position
	set_a(ram[smb_screen_right_x_pos]);
	reg_p.c = 0;
	add_a(0x20); // place enemy 32 pixels beyond right side of screen
	ram[smb_enemy_x_position + reg_x] = reg_a;
	set_a(ram[smb_screen_right_page_loc]);
	add_a(0x00); // add carry
	ram[smb_enemy_page_loc + reg_x] = reg_a;
	smb_finish_flame(); return; // skip this part to finish setting values
	smb_spawn_from_mouth(); return;
}

void smb_spawn_from_mouth() {
	set_a(ram[smb_enemy_x_position + reg_y]); // get bowser's horizontal position
	reg_p.c = 1;
	sub_a(0x0e); // subtract 14 pixels
	ram[smb_enemy_x_position + reg_x] = reg_a; // save as flame's horizontal position
	set_a(ram[smb_enemy_page_loc + reg_y]);
	ram[smb_enemy_page_loc + reg_x] = reg_a; // copy page location from bowser to flame
	set_a(ram[smb_enemy_y_position + reg_y]);
	reg_p.c = 0; // add 8 pixels to bowser's vertical position
	add_a(0x08);
	ram[smb_enemy_y_position + reg_x] = reg_a; // save as flame's vertical position
	set_a(ram[smb_pseudo_random_bit_reg + reg_x]);
	and_a(0b00000011); // get 2 LSB from first part of LSFR
	ram[smb_enemy_ymf_dummy + reg_x] = reg_a; // save here
	set_y(reg_a); // use as offset
	set_a(rom[smb_flame_y_pos_data + reg_y]); // get value here using bits as offset
	set_y(0x00); // load default offset
	cmp_a(ram[smb_enemy_y_position + reg_x]); // compare value to flame's current vertical position
	if (!reg_p.c) { goto set_mf; } // if less, do not increment offset
	set_y(reg_y+1); // otherwise increment now
set_mf:;
	set_a(rom[smb_flame_ymf_adder_data + reg_y]); // get value here and save
	ram[smb_enemy_y_move_force + reg_x] = reg_a; // to vertical movement force
	set_a(0x00);
	ram[smb_enemy_frenzy_buffer] = reg_a; // clear enemy frenzy buffer
	smb_finish_flame(); return;
}

void smb_finish_flame() {
	set_a(0x08); // set $08 for bounding box control
	ram[smb_enemy_bound_box_ctrl + reg_x] = reg_a;
	set_a(0x01); // set high byte of vertical and
	ram[smb_enemy_y_high_pos + reg_x] = reg_a; // enemy buffer flag
	ram[smb_enemy_flag + reg_x] = reg_a;
	reg_a = shr(reg_a);
	ram[smb_enemy_x_move_force + reg_x] = reg_a; // initialize horizontal movement force, and
	ram[smb_enemy_state + reg_x] = reg_a; // enemy state
	return;
}

void smb_init_fireworks() {
	set_a(ram[smb_frenzy_enemy_timer]); // if timer not expired yet, branch to leave
	if (!reg_p.z) { goto exit_f_wk; }
	set_a(0x20); // otherwise reset timer
	ram[smb_frenzy_enemy_timer] = reg_a;
	ram[smb_fireworks_counter] = dec(ram[smb_fireworks_counter]); // decrement for each explosion
	set_y(0x06); // start at last slot
star_f_chk:;
	set_y(reg_y-1);
	set_a(ram[smb_enemy_id + reg_y]); // check for presence of star flag object
	cmp_a((smb_star_flag_object)); // if there isn't a star flag object,
	if (!reg_p.z) { goto star_f_chk; } // routine goes into infinite loop = crash
	set_a(ram[smb_enemy_x_position + reg_y]);
	reg_p.c = 1; // get horizontal coordinate of star flag object, then
	sub_a(0x30); // subtract 48 pixels from it and save to
	push(reg_a); // the stack
	set_a(ram[smb_enemy_page_loc + reg_y]);
	sub_a(0x00); // subtract the carry from the page location
	ram[0x0000] = reg_a; // of the star flag object
	set_a(ram[smb_fireworks_counter]); // get fireworks counter
	reg_p.c = 0;
	add_a(ram[smb_enemy_state + reg_y]); // add state of star flag object (possibly not necessary)
	set_y(reg_a); // use as offset
	set_a(pull()); // get saved horizontal coordinate of star flag - 48 pixels
	reg_p.c = 0;
	add_a(rom[smb_fireworks_x_pos_data + reg_y]); // add number based on offset of fireworks counter
	ram[smb_enemy_x_position + reg_x] = reg_a; // store as the fireworks object horizontal coordinate
	set_a(ram[0x0000]);
	add_a(0x00); // add carry and store as page location for
	ram[smb_enemy_page_loc + reg_x] = reg_a; // the fireworks object
	set_a(rom[smb_fireworks_y_pos_data + reg_y]); // get vertical position using same offset
	ram[smb_enemy_y_position + reg_x] = reg_a; // and store as vertical coordinate for fireworks object
	set_a(0x01);
	ram[smb_enemy_y_high_pos + reg_x] = reg_a; // store in vertical high byte
	ram[smb_enemy_flag + reg_x] = reg_a; // and activate enemy buffer flag
	reg_a = shr(reg_a);
	ram[smb_explosion_gfx_counter + reg_x] = reg_a; // initialize explosion counter
	set_a(0x08);
	ram[smb_explosion_timer_counter + reg_x] = reg_a; // set explosion timing counter
exit_f_wk:;
	return;
}

void smb_bullet_bill_cheep_cheep() {
	set_a(ram[smb_frenzy_enemy_timer]); // if timer not expired yet, branch to leave
	if (!reg_p.z) { goto ex_f_17; }
	set_a(ram[smb_area_type]); // are we in a water-type level?
	if (!reg_p.z) { goto do_bullet_bills; } // if not, branch elsewhere
	cmp_x(0x03); // are we past third enemy slot?
	if (reg_p.c) { goto ex_f_17; } // if so, branch to leave
	set_y(0x00); // load default offset
	set_a(ram[smb_pseudo_random_bit_reg + reg_x]);
	cmp_a(0xaa); // check first part of LSFR against preset value
	if (!reg_p.c) { goto chk_w_2; } // if less than preset, do not increment offset
	set_y(reg_y+1); // otherwise increment
chk_w_2:;
	set_a(ram[smb_world_number]); // check world number
	cmp_a((smb_world_2));
	if (reg_p.z) { goto get_17_id; } // if we're on world 2, do not increment offset
	set_y(reg_y+1); // otherwise increment
get_17_id:;
	set_a(reg_y);
	and_a(0b00000001); // mask out all but last bit of offset
	set_y(reg_a);
	set_a(rom[smb_swim_cc_id_data + reg_y]); // load identifier for cheep-cheeps
set_17_id:;
	ram[smb_enemy_id + reg_x] = reg_a; // store whatever's in A as enemy identifier
	set_a(ram[smb_bit_m_filter]);
	cmp_a(0xff); // if not all bits set, skip init part and compare bits
	if (!reg_p.z) { goto get_r_bit; }
	set_a(0x00); // initialize vertical position filter
	ram[smb_bit_m_filter] = reg_a;
get_r_bit:;
	set_a(ram[smb_pseudo_random_bit_reg + reg_x]); // get first part of LSFR
	and_a(0b00000111); // mask out all but 3 LSB
chk_r_bit:;
	set_y(reg_a); // use as offset
	set_a(rom[smb_bitmasks + reg_y]); // load bitmask
	bit_a(ram[smb_bit_m_filter]); // perform AND on filter without changing it
	if (reg_p.z) { goto add_f_bit; }
	set_y(reg_y+1); // increment offset
	set_a(reg_y);
	and_a(0b00000111); // mask out all but 3 LSB thus keeping it 0-7
	goto chk_r_bit; // do another check
add_f_bit:;
	or_a(ram[smb_bit_m_filter]); // add bit to already set bits in filter
	ram[smb_bit_m_filter] = reg_a; // and store
	set_a(rom[smb_enemy_17_y_pos_data + reg_y]); // load vertical position using offset
	smb_put_at_right_extent(); // set vertical position and other values
	ram[smb_enemy_ymf_dummy + reg_x] = reg_a; // initialize dummy variable
	set_a(0x20); // set timer
	ram[smb_frenzy_enemy_timer] = reg_a;
	smb_checkpoint_enemy_id(); return; // process our new enemy object
do_bullet_bills:;
	set_y(0xff); // start at beginning of enemy slots
bb_s_loop:;
	set_y(reg_y+1); // move onto the next slot
	cmp_y(0x05); // branch to play sound if we've done all slots
	if (reg_p.c) { goto fire_bullet_bill; }
	set_a(ram[smb_enemy_flag + reg_y]); // if enemy buffer flag not set,
	if (reg_p.z) { goto bb_s_loop; } // loop back and check another slot
	set_a(ram[smb_enemy_id + reg_y]);
	cmp_a((smb_bullet_bill_frenzy_var)); // check enemy identifier for
	if (!reg_p.z) { goto bb_s_loop; } // bullet bill object (frenzy variant)
ex_f_17:;
	return; // if found, leave
fire_bullet_bill:;
	set_a(ram[smb_square_2_sound_queue]);
	or_a((smb_sfx_blast)); // play fireworks/gunfire sound
	ram[smb_square_2_sound_queue] = reg_a;
	set_a((smb_bullet_bill_frenzy_var)); // load identifier for bullet bill object
	if (!reg_p.z) { goto set_17_id; } // unconditional branch
	smb_handle_group_enemies(); return;
}

void smb_handle_group_enemies() {
	set_y(0x00); // load value for green koopa troopa
	reg_p.c = 1;
	sub_a(0x37); // subtract $37 from second byte read
	push(reg_a); // save result in stack for now
	cmp_a(0x04); // was byte in $3b-$3e range?
	if (reg_p.c) { goto sngl_id; } // if so, branch
	push(reg_a); // save another copy to stack
	set_y((smb_goomba)); // load value for goomba enemy
	set_a(ram[smb_primary_hard_mode]); // if primary hard mode flag not set,
	if (reg_p.z) { goto pull_id; } // branch, otherwise change to value
	set_y((smb_buzzy_beetle)); // for buzzy beetle
pull_id:;
	set_a(pull()); // get second copy from stack
sngl_id:;
	ram[0x0001] = reg_y; // save enemy id here
	set_y(0xb0); // load default y coordinate
	and_a(0x02); // check to see if d1 was set
	if (reg_p.z) { goto set_y_gp; } // if so, move y coordinate up,
	set_y(0x70); // otherwise branch and use default
set_y_gp:;
	ram[0x0000] = reg_y; // save y coordinate here
	set_a(ram[smb_screen_right_page_loc]); // get page number of right edge of screen
	ram[0x0002] = reg_a; // save here
	set_a(ram[smb_screen_right_x_pos]); // get pixel coordinate of right edge
	ram[0x0003] = reg_a; // save here
	set_y(0x02); // load two enemies by default
	set_a(pull()); // get first copy from stack
	reg_a = shr(reg_a); // check to see if d0 was set
	if (!reg_p.c) { goto cnt_grp; } // if not, use default value
	set_y(reg_y+1); // otherwise increment to three enemies
cnt_grp:;
	ram[smb_numberof_group_enemies] = reg_y; // save number of enemies here
gr_loop:;
	set_x(0xff); // start at beginning of enemy buffers
g_slt_lp:;
	set_x(reg_x+1); // increment and branch if past
	cmp_x(0x05); // end of buffers
	if (reg_p.c) { goto next_ed; }
	set_a(ram[smb_enemy_flag + reg_x]); // check to see if enemy is already
	if (!reg_p.z) { goto g_slt_lp; } // stored in buffer, and branch if so
	set_a(ram[0x0001]);
	ram[smb_enemy_id + reg_x] = reg_a; // store enemy object identifier
	set_a(ram[0x0002]);
	ram[smb_enemy_page_loc + reg_x] = reg_a; // store page location for enemy object
	set_a(ram[0x0003]);
	ram[smb_enemy_x_position + reg_x] = reg_a; // store x coordinate for enemy object
	reg_p.c = 0;
	add_a(0x18); // add 24 pixels for next enemy
	ram[0x0003] = reg_a;
	set_a(ram[0x0002]); // add carry to page location for
	add_a(0x00); // next enemy
	ram[0x0002] = reg_a;
	set_a(ram[0x0000]); // store y coordinate for enemy object
	ram[smb_enemy_y_position + reg_x] = reg_a;
	set_a(0x01); // activate flag for buffer, and
	ram[smb_enemy_y_high_pos + reg_x] = reg_a; // put enemy within the screen vertically
	ram[smb_enemy_flag + reg_x] = reg_a;
	smb_checkpoint_enemy_id(); // process each enemy object separately
	ram[smb_numberof_group_enemies] = dec(ram[smb_numberof_group_enemies]); // do this until we run out of enemy objects
	if (!reg_p.z) { goto gr_loop; }
next_ed:;
	smb_inc_2_b(); return; // jump to increment data offset and leave
	smb_init_piranha_plant(); return;
}

void smb_init_piranha_plant() {
	set_a(0x01); // set initial speed
	ram[smb_piranha_plant_y_speed + reg_x] = reg_a;
	reg_a = shr(reg_a);
	ram[smb_enemy_state + reg_x] = reg_a; // initialize enemy state and what would normally
	ram[smb_piranha_plant_move_flag + reg_x] = reg_a; // be used as vertical speed, but not in this case
	set_a(ram[smb_enemy_y_position + reg_x]);
	ram[smb_piranha_plant_down_y_pos + reg_x] = reg_a; // save original vertical coordinate here
	reg_p.c = 1;
	sub_a(0x18);
	ram[smb_piranha_plant_up_y_pos + reg_x] = reg_a; // save original vertical coordinate - 24 pixels here
	set_a(0x09);
	smb_set_b_box_2(); return; // set specific value for bounding box control
	smb_init_enemy_frenzy(); return;
}

void smb_init_enemy_frenzy() {
	set_a(ram[smb_enemy_id + reg_x]); // load enemy identifier
	ram[smb_enemy_frenzy_buffer] = reg_a; // save in enemy frenzy buffer
	reg_p.c = 1;
	sub_a(0x12); // subtract 12 and use as offset for jump engine
	static void(*targets[])() = {
		smb_lakitu_and_spiny_handler,
		smb_no_frenzy_code,
		smb_init_flying_cheep_cheep,
		smb_init_bowser_flame,
		smb_init_fireworks,
		smb_bullet_bill_cheep_cheep
	};
	targets[reg_a](); return;
	smb_no_frenzy_code(); return;
}

void smb_no_frenzy_code() {
	return;
	smb_end_frenzy(); return;
}

void smb_end_frenzy() {
	set_y(0x05); // start at last slot
lakitu_chk:;
	set_a(ram[smb_enemy_id + reg_y]); // check enemy identifiers
	cmp_a((smb_lakitu)); // for lakitu
	if (!reg_p.z) { goto next_f_slot; }
	set_a(0x01); // if found, set state
	ram[smb_enemy_state + reg_y] = reg_a;
next_f_slot:;
	set_y(reg_y-1); // move onto the next slot
	if (!reg_p.n) { goto lakitu_chk; } // do this until all slots are checked
	set_a(0x00);
	ram[smb_enemy_frenzy_buffer] = reg_a; // empty enemy frenzy buffer
	ram[smb_enemy_flag + reg_x] = reg_a; // disable enemy buffer flag for this object
	return;
	smb_init_jump_gp_troopa(); return;
}

void smb_init_jump_gp_troopa() {
	set_a(0x02); // set for movement to the left
	ram[smb_enemy_moving_dir + reg_x] = reg_a;
	set_a(0xf8); // set horizontal speed
	ram[smb_enemy_x_speed + reg_x] = reg_a;
	smb_tall_b_box_2(); return;
}

void smb_tall_b_box_2() {
	set_a(0x03); // set specific value for bounding box control
	smb_set_b_box_2(); return;
}

void smb_set_b_box_2() {
	ram[smb_enemy_bound_box_ctrl + reg_x] = reg_a; // set bounding box control then leave
	return;
	smb_init_bal_platform(); return;
}

void smb_init_bal_platform() {
	ram[smb_enemy_y_position + reg_x] = dec(ram[smb_enemy_y_position + reg_x]); // raise vertical position by two pixels
	ram[smb_enemy_y_position + reg_x] = dec(ram[smb_enemy_y_position + reg_x]);
	set_y(ram[smb_secondary_hard_mode]); // if secondary hard mode flag not set,
	if (!reg_p.z) { goto align_p; } // branch ahead
	set_y(0x02); // otherwise set value here
	smb_pos_platform(); // do a sub to add or subtract pixels
align_p:;
	set_y(0xff); // set default value here for now
	set_a(ram[smb_bal_platform_alignment]); // get current balance platform alignment
	ram[smb_enemy_state + reg_x] = reg_a; // set platform alignment to object state here
	if (!reg_p.n) { goto set_bpa; } // if old alignment $ff, put $ff as alignment for negative
	set_a(reg_x); // if old contents already $ff, put
	set_y(reg_a); // object offset as alignment to make next positive
set_bpa:;
	ram[smb_bal_platform_alignment] = reg_y; // store whatever value's in Y here
	set_a(0x00);
	ram[smb_enemy_moving_dir + reg_x] = reg_a; // init moving direction
	set_y(reg_a); // init Y
	smb_pos_platform(); // do a sub to add 8 pixels, then run shared code here
	smb_init_drop_platform(); return;
}

void smb_init_drop_platform() {
	set_a(0xff);
	ram[smb_platform_collision_flag + reg_x] = reg_a; // set some value here
	smb_common_plat_code(); return; // then jump ahead to execute more code
	smb_init_hori_platform(); return;
}

void smb_init_hori_platform() {
	set_a(0x00);
	ram[smb_x_move_secondary_counter + reg_x] = reg_a; // init one of the moving counters
	smb_common_plat_code(); return; // jump ahead to execute more code
	smb_init_vert_platform(); return;
}

void smb_init_vert_platform() {
	set_y(0x40); // set default value here
	set_a(ram[smb_enemy_y_position + reg_x]); // check vertical position
	if (!reg_p.n) { goto set_yo; } // if above a certain point, skip this part
	eor_a(0xff);
	reg_p.c = 0; // otherwise get two's compliment
	add_a(0x01);
	set_y(0xc0); // get alternate value to add to vertical position
set_yo:;
	ram[smb_y_platform_top_y_pos + reg_x] = reg_a; // save as top vertical position
	set_a(reg_y);
	reg_p.c = 0; // load value from earlier, add number of pixels
	add_a(ram[smb_enemy_y_position + reg_x]); // to vertical position
	ram[smb_y_platform_center_y_pos + reg_x] = reg_a; // save result as central vertical position
	smb_common_plat_code(); return;
}

void smb_common_plat_code() {
	smb_init_v_stf(); // do a sub to init certain other values
	smb_spb_box(); return;
}

void smb_spb_box() {
	set_a(0x05); // set default bounding box size control
	set_y(ram[smb_area_type]);
	cmp_y(0x03); // check for castle-type level
	if (reg_p.z) { goto cas_pbb; } // use default value if found
	set_y(ram[smb_secondary_hard_mode]); // otherwise check for secondary hard mode flag
	if (!reg_p.z) { goto cas_pbb; } // if set, use default value
	set_a(0x06); // use alternate value if not castle or secondary not set
cas_pbb:;
	ram[smb_enemy_bound_box_ctrl + reg_x] = reg_a; // set bounding box size control here and leave
	return;
	smb_large_lift_up(); return;
}

void smb_large_lift_up() {
	smb_plat_lift_up(); // execute code for platforms going up
	smb_large_lift_b_box(); return; // overwrite bounding box for large platforms
	smb_large_lift_down(); return;
}

void smb_large_lift_down() {
	smb_plat_lift_down(); // execute code for platforms going down
	smb_large_lift_b_box(); return;
}

void smb_large_lift_b_box() {
	smb_spb_box(); return; // jump to overwrite bounding box size control
	smb_plat_lift_up(); return;
}

void smb_plat_lift_up() {
	set_a(0x10); // set movement amount here
	ram[smb_enemy_y_move_force + reg_x] = reg_a;
	set_a(0xff); // set moving speed for platforms going up
	ram[smb_blooper_move_counter + reg_x] = reg_a;
	smb_common_small_lift(); return; // skip ahead to part we should be executing
	smb_plat_lift_down(); return;
}

void smb_plat_lift_down() {
	set_a(0xf0); // set movement amount here
	ram[smb_enemy_y_move_force + reg_x] = reg_a;
	set_a(0x00); // set moving speed for platforms going down
	ram[smb_enemy_y_speed + reg_x] = reg_a;
	smb_common_small_lift(); return;
}

void smb_common_small_lift() {
	set_y(0x01);
	smb_pos_platform(); // do a sub to add 12 pixels due to preset value
	set_a(0x04);
	ram[smb_enemy_bound_box_ctrl + reg_x] = reg_a; // set bounding box control for small platforms
	return;
}

void smb_pos_platform() {
	set_a(ram[smb_enemy_x_position + reg_x]); // get horizontal coordinate
	reg_p.c = 0;
	add_a(rom[smb_plat_pos_data_low + reg_y]); // add or subtract pixels depending on offset
	ram[smb_enemy_x_position + reg_x] = reg_a; // store as new horizontal coordinate
	set_a(ram[smb_enemy_page_loc + reg_x]);
	add_a(rom[smb_plat_pos_data_high + reg_y]); // add or subtract page location depending on offset
	ram[smb_enemy_page_loc + reg_x] = reg_a; // store as new page location
	return; // and go back
	smb_end_of_enemy_init_code(); return;
}

void smb_end_of_enemy_init_code() {
	return;
	smb_run_enemy_objects_core(); return;
}

void smb_run_enemy_objects_core() {
	set_x(ram[smb_object_offset]); // get offset for enemy object buffer
	set_a(0x00); // load value 0 for jump engine by default
	set_y(ram[smb_enemy_id + reg_x]);
	cmp_y(0x15); // if enemy object < $15, use default value
	if (!reg_p.c) { goto jmp_eo; }
	set_a(reg_y); // otherwise subtract $14 from the value and use
	sub_a(0x14); // as value for jump engine
jmp_eo:;
	static void(*targets[])() = {
		smb_run_normal_enemies,
		smb_run_bowser_flame,
		smb_run_fireworks,
		smb_no_run_code,
		smb_no_run_code,
		smb_no_run_code,
		smb_no_run_code,
		smb_run_firebar_obj,
		smb_run_firebar_obj,
		smb_run_firebar_obj,
		smb_run_firebar_obj,
		smb_run_firebar_obj,
		smb_run_firebar_obj,
		smb_run_firebar_obj,
		smb_run_firebar_obj,
		smb_no_run_code,
		smb_run_large_platform,
		smb_run_large_platform,
		smb_run_large_platform,
		smb_run_large_platform,
		smb_run_large_platform,
		smb_run_large_platform,
		smb_run_large_platform,
		smb_run_small_platform,
		smb_run_small_platform,
		smb_run_bowser,
		smb_power_up_obj_handler,
		smb_vine_object_handler,
		smb_no_run_code,
		smb_run_star_flag_obj,
		smb_jumpspring_handler,
		smb_no_run_code,
		smb_warp_zone_object,
		smb_run_retainer_obj
	};
	targets[reg_a](); return;
	smb_no_run_code(); return;
}

void smb_no_run_code() {
	return;
	smb_run_retainer_obj(); return;
}

void smb_run_retainer_obj() {
	smb_get_enemy_offscreen_bits();
	smb_relative_enemy_position();
	smb_enemy_gfx_handler(); return;
	smb_run_normal_enemies(); return;
}

void smb_run_normal_enemies() {
	set_a(0x00); // init sprite attributes
	ram[smb_enemy_spr_attrib + reg_x] = reg_a;
	smb_get_enemy_offscreen_bits();
	smb_relative_enemy_position();
	smb_enemy_gfx_handler();
	smb_get_enemy_bound_box();
	smb_enemy_to_bg_collision_det();
	smb_enemies_collision();
	smb_player_enemy_collision();
	set_y(ram[smb_timer_control]); // if master timer control set, skip to last routine
	if (!reg_p.z) { goto skip_move; }
	smb_enemy_movement_subs();
skip_move:;
	smb_offscreen_bounds_check(); return;
	smb_enemy_movement_subs(); return;
}

void smb_enemy_movement_subs() {
	set_a(ram[smb_enemy_id + reg_x]);
	static void(*targets[])() = {
		smb_move_normal_enemy,
		smb_move_normal_enemy,
		smb_move_normal_enemy,
		smb_move_normal_enemy,
		smb_move_normal_enemy,
		smb_proc_hammer_bro,
		smb_move_normal_enemy,
		smb_move_bloober,
		smb_move_bullet_bill,
		smb_no_move_code,
		smb_move_swimming_cheep_cheep,
		smb_move_swimming_cheep_cheep,
		smb_move_podoboo,
		smb_move_piranha_plant,
		smb_move_jumping_enemy,
		smb_proc_move_red_p_troopa,
		smb_move_fly_green_p_troopa,
		smb_move_lakitu,
		smb_move_normal_enemy,
		smb_no_move_code,
		smb_move_flying_cheep_cheep
	};
	targets[reg_a](); return;
	smb_no_move_code(); return;
}

void smb_no_move_code() {
	return;
	smb_run_bowser_flame(); return;
}

void smb_run_bowser_flame() {
	smb_proc_bowser_flame();
	smb_get_enemy_offscreen_bits();
	smb_relative_enemy_position();
	smb_get_enemy_bound_box();
	smb_player_enemy_collision();
	smb_offscreen_bounds_check(); return;
	smb_run_firebar_obj(); return;
}

void smb_run_firebar_obj() {
	smb_proc_firebar();
	smb_offscreen_bounds_check(); return;
	smb_run_small_platform(); return;
}

void smb_run_small_platform() {
	smb_get_enemy_offscreen_bits();
	smb_relative_enemy_position();
	smb_small_platform_bound_box();
	smb_small_platform_collision();
	smb_relative_enemy_position();
	smb_draw_small_platform();
	smb_move_small_platform();
	smb_offscreen_bounds_check(); return;
	smb_run_large_platform(); return;
}

void smb_run_large_platform() {
	smb_get_enemy_offscreen_bits();
	smb_relative_enemy_position();
	smb_large_platform_bound_box();
	smb_large_platform_collision();
	set_a(ram[smb_timer_control]); // if master timer control set,
	if (!reg_p.z) { goto skip_pt; } // skip subroutine tree
	smb_large_platform_subroutines();
skip_pt:;
	smb_relative_enemy_position();
	smb_draw_large_platform();
	smb_offscreen_bounds_check(); return;
	smb_large_platform_subroutines(); return;
}

void smb_large_platform_subroutines() {
	set_a(ram[smb_enemy_id + reg_x]); // subtract $24 to get proper offset for jump table
	reg_p.c = 1;
	sub_a(0x24);
	static void(*targets[])() = {
		smb_balance_platform,
		smb_y_moving_platform,
		smb_move_large_lift_plat,
		smb_move_large_lift_plat,
		smb_x_moving_platform,
		smb_drop_platform,
		smb_right_platform
	};
	targets[reg_a](); return;
	smb_erase_enemy_object(); return;
}

void smb_erase_enemy_object() {
	set_a(0x00); // clear all enemy object variables
	ram[smb_enemy_flag + reg_x] = reg_a;
	ram[smb_enemy_id + reg_x] = reg_a;
	ram[smb_enemy_state + reg_x] = reg_a;
	ram[smb_floatey_num_control + reg_x] = reg_a;
	ram[smb_enemy_interval_timer + reg_x] = reg_a;
	ram[smb_shell_chain_counter + reg_x] = reg_a;
	ram[smb_enemy_spr_attrib + reg_x] = reg_a;
	ram[smb_enemy_frame_timer + reg_x] = reg_a;
	return;
	smb_move_podoboo(); return;
}

void smb_move_podoboo() {
	set_a(ram[smb_enemy_interval_timer + reg_x]); // check enemy timer
	if (!reg_p.z) { goto pdb_m; } // branch to move enemy if not expired
	smb_init_podoboo(); // otherwise set up podoboo again
	set_a(ram[smb_pseudo_random_bit_reg+1 + reg_x]); // get part of LSFR
	or_a(0b10000000); // set d7
	ram[smb_enemy_y_move_force + reg_x] = reg_a; // store as movement force
	and_a(0b00001111); // mask out high nybble
	or_a(0x06); // set for at least six intervals
	ram[smb_enemy_interval_timer + reg_x] = reg_a; // store as new enemy timer
	set_a(0xf9);
	ram[smb_blooper_move_counter + reg_x] = reg_a; // set vertical speed to move podoboo upwards
pdb_m:;
	smb_move_j_enemy_vertically(); return; // branch to impose gravity on podoboo
}

void smb_proc_hammer_bro() {
	set_a(ram[smb_enemy_state + reg_x]); // check hammer bro's enemy state for d5 set
	and_a(0b00100000);
	if (reg_p.z) { goto chk_jh; } // if not set, go ahead with code
	smb_move_defeated_enemy(); return; // otherwise jump to something else
chk_jh:;
	set_a(ram[smb_hammer_bro_jump_timer + reg_x]); // check jump timer
	if (reg_p.z) { smb_hammer_bro_jump_code(); return; } // if expired, branch to jump
	ram[smb_hammer_bro_jump_timer + reg_x] = dec(ram[smb_hammer_bro_jump_timer + reg_x]); // otherwise decrement jump timer
	set_a(ram[smb_enemy_offscreen_bits]);
	and_a(0b00001100); // check offscreen bits
	if (!reg_p.z) { smb_move_hammer_bro_x_dir(); return; } // if hammer bro a little offscreen, skip to movement code
	set_a(ram[smb_hammer_throwing_timer + reg_x]); // check hammer throwing timer
	if (!reg_p.z) { goto dec_ht; } // if not expired, skip ahead, do not throw hammer
	set_y(ram[smb_secondary_hard_mode]); // otherwise get secondary hard mode flag
	set_a(rom[smb_hammer_throw_tmr_data + reg_y]); // get timer data using flag as offset
	ram[smb_hammer_throwing_timer + reg_x] = reg_a; // set as new timer
	smb_spawn_hammer_obj(); // do a sub here to spawn hammer object
	if (!reg_p.c) { goto dec_ht; } // if carry clear, hammer not spawned, skip to decrement timer
	set_a(ram[smb_enemy_state + reg_x]);
	or_a(0b00001000); // set d3 in enemy state for hammer throw
	ram[smb_enemy_state + reg_x] = reg_a;
	smb_move_hammer_bro_x_dir(); return; // jump to move hammer bro
dec_ht:;
	ram[smb_hammer_throwing_timer + reg_x] = dec(ram[smb_hammer_throwing_timer + reg_x]); // decrement timer
	smb_move_hammer_bro_x_dir(); return; // jump to move hammer bro
}

void smb_hammer_bro_jump_code() {
	set_a(ram[smb_enemy_state + reg_x]); // get hammer bro's enemy state
	and_a(0b00000111); // mask out all but 3 LSB
	cmp_a(0x01); // check for d0 set (for jumping)
	if (reg_p.z) { smb_move_hammer_bro_x_dir(); return; } // if set, branch ahead to moving code
	set_a(0x00); // load default value here
	ram[0x0000] = reg_a; // save into temp variable for now
	set_y(0xfa); // set default vertical speed
	set_a(ram[smb_enemy_y_position + reg_x]); // check hammer bro's vertical coordinate
	if (reg_p.n) { smb_set_hj(); return; } // if on the bottom half of the screen, use current speed
	set_y(0xfd); // otherwise set alternate vertical speed
	cmp_a(0x70); // check to see if hammer bro is above the middle of screen
	ram[0x0000] = inc(ram[0x0000]); // increment preset value to $01
	if (!reg_p.c) { smb_set_hj(); return; } // if above the middle of the screen, use current speed and $01
	ram[0x0000] = dec(ram[0x0000]); // otherwise return value to $00
	set_a(ram[smb_pseudo_random_bit_reg+1 + reg_x]); // get part of LSFR, mask out all but LSB
	and_a(0x01);
	if (!reg_p.z) { smb_set_hj(); return; } // if d0 of LSFR set, branch and use current speed and $00
	set_y(0xfa); // otherwise reset to default vertical speed
	smb_set_hj(); return;
}

void smb_set_hj() {
	ram[smb_enemy_y_speed + reg_x] = reg_y; // set vertical speed for jumping
	set_a(ram[smb_enemy_state + reg_x]); // set d0 in enemy state for jumping
	or_a(0x01);
	ram[smb_enemy_state + reg_x] = reg_a;
	set_a(ram[0x0000]); // load preset value here to use as bitmask
	and_a(ram[smb_pseudo_random_bit_reg+2 + reg_x]); // and do bit-wise comparison with part of LSFR
	set_y(reg_a); // then use as offset
	set_a(ram[smb_secondary_hard_mode]); // check secondary hard mode flag
	if (!reg_p.z) { goto h_jump; }
	set_y(reg_a); // if secondary hard mode flag clear, set offset to 0
h_jump:;
	set_a(rom[smb_hammer_bro_jump_l_data + reg_y]); // get jump length timer data using offset from before
	ram[smb_enemy_frame_timer + reg_x] = reg_a; // save in enemy timer
	set_a(ram[smb_pseudo_random_bit_reg+1 + reg_x]);
	or_a(0b11000000); // get contents of part of LSFR, set d7 and d6, then
	ram[smb_hammer_bro_jump_timer + reg_x] = reg_a; // store in jump timer
	smb_move_hammer_bro_x_dir(); return;
}

void smb_move_hammer_bro_x_dir() {
	set_y(0xfc); // move hammer bro a little to the left
	set_a(ram[smb_frame_counter]);
	and_a(0b01000000); // change hammer bro's direction every 64 frames
	if (!reg_p.z) { goto shimmy; }
	set_y(0x04); // if d6 set in counter, move him a little to the right
shimmy:;
	ram[smb_enemy_x_speed + reg_x] = reg_y; // store horizontal speed
	set_y(0x01); // set to face right by default
	smb_player_enemy_diff(); // get horizontal difference between player and hammer bro
	if (reg_p.n) { goto set_shim; } // if enemy to the left of player, skip this part
	set_y(reg_y+1); // set to face left
	set_a(ram[smb_enemy_interval_timer + reg_x]); // check walking timer
	if (!reg_p.z) { goto set_shim; } // if not yet expired, skip to set moving direction
	set_a(0xf8);
	ram[smb_enemy_x_speed + reg_x] = reg_a; // otherwise, make the hammer bro walk left towards player
set_shim:;
	ram[smb_enemy_moving_dir + reg_x] = reg_y; // set moving direction
	smb_move_normal_enemy(); return;
}

void smb_move_normal_enemy() {
	set_y(0x00); // init Y to leave horizontal movement as-is
	set_a(ram[smb_enemy_state + reg_x]);
	and_a(0b01000000); // check enemy state for d6 set, if set skip
	if (!reg_p.z) { goto fall_e; } // to move enemy vertically, then horizontally if necessary
	set_a(ram[smb_enemy_state + reg_x]);
	reg_a = shl(reg_a); // check enemy state for d7 set
	if (reg_p.c) { goto stead_m; } // if set, branch to move enemy horizontally
	set_a(ram[smb_enemy_state + reg_x]);
	and_a(0b00100000); // check enemy state for d5 set
	if (!reg_p.z) { smb_move_defeated_enemy(); return; } // if set, branch to move defeated enemy object
	set_a(ram[smb_enemy_state + reg_x]);
	and_a(0b00000111); // check d2-d0 of enemy state for any set bits
	if (reg_p.z) { goto stead_m; } // if enemy in normal state, branch to move enemy horizontally
	cmp_a(0x05);
	if (reg_p.z) { goto fall_e; } // if enemy in state used by spiny's egg, go ahead here
	cmp_a(0x03);
	if (reg_p.c) { goto revive_stunned; } // if enemy in states $03 or $04, skip ahead to yet another part
fall_e:;
	smb_move_d_enemy_vertically(); // do a sub here to move enemy downwards
	set_y(0x00);
	set_a(ram[smb_enemy_state + reg_x]); // check for enemy state $02
	cmp_a(0x02);
	if (reg_p.z) { goto me_hor; } // if found, branch to move enemy horizontally
	and_a(0b01000000); // check for d6 set
	if (reg_p.z) { goto stead_m; } // if not set, branch to something else
	set_a(ram[smb_enemy_id + reg_x]);
	cmp_a((smb_power_up_object)); // check for power-up object
	if (reg_p.z) { goto stead_m; }
	if (!reg_p.z) { goto slow_m; } // if any other object where d6 set, jump to set Y
me_hor:;
	smb_move_enemy_horizontally(); return; // jump here to move enemy horizontally for <> $2e and d6 set
slow_m:;
	set_y(0x01); // if branched here, increment Y to slow horizontal movement
stead_m:;
	set_a(ram[smb_enemy_x_speed + reg_x]); // get current horizontal speed
	push(reg_a); // save to stack
	if (!reg_p.n) { goto add_hs; } // if not moving or moving right, skip, leave Y alone
	set_y(reg_y+1);
	set_y(reg_y+1); // otherwise increment Y to next data
add_hs:;
	reg_p.c = 0;
	add_a(rom[smb_x_speed_adder_data + reg_y]); // add value here to slow enemy down if necessary
	ram[smb_enemy_x_speed + reg_x] = reg_a; // save as horizontal speed temporarily
	smb_move_enemy_horizontally(); // then do a sub to move horizontally
	set_a(pull());
	ram[smb_enemy_x_speed + reg_x] = reg_a; // get old horizontal speed from stack and return to
	return; // original memory location, then leave
revive_stunned:;
	set_a(ram[smb_enemy_interval_timer + reg_x]); // if enemy timer not expired yet,
	if (!reg_p.z) { smb_chk_kill_goomba(); return; } // skip ahead to something else
	ram[smb_enemy_state + reg_x] = reg_a; // otherwise initialize enemy state to normal
	set_a(ram[smb_frame_counter]);
	and_a(0x01); // get d0 of frame counter
	set_y(reg_a); // use as Y and increment for movement direction
	set_y(reg_y+1);
	ram[smb_enemy_moving_dir + reg_x] = reg_y; // store as pseudorandom movement direction
	set_y(reg_y-1); // decrement for use as pointer
	set_a(ram[smb_primary_hard_mode]); // check primary hard mode flag
	if (reg_p.z) { goto set_r_spd; } // if not set, use pointer as-is
	set_y(reg_y+1);
	set_y(reg_y+1); // otherwise increment 2 bytes to next data
set_r_spd:;
	set_a(rom[smb_revived_x_speed + reg_y]); // load and store new horizontal speed
	ram[smb_enemy_x_speed + reg_x] = reg_a; // and leave
	return;
	smb_move_defeated_enemy(); return;
}

void smb_move_defeated_enemy() {
	smb_move_d_enemy_vertically(); // execute sub to move defeated enemy downwards
	smb_move_enemy_horizontally(); return; // now move defeated enemy horizontally
	smb_chk_kill_goomba(); return;
}

void smb_chk_kill_goomba() {
	cmp_a(0x0e); // check to see if enemy timer has reached
	if (!reg_p.z) { goto hk_gmba; } // a certain point, and branch to leave if not
	set_a(ram[smb_enemy_id + reg_x]);
	cmp_a((smb_goomba)); // check for goomba object
	if (!reg_p.z) { goto hk_gmba; } // branch if not found
	smb_erase_enemy_object(); // otherwise, kill this goomba object
hk_gmba:;
	return; // leave!
	smb_move_jumping_enemy(); return;
}

void smb_move_jumping_enemy() {
	smb_move_j_enemy_vertically(); // do a sub to impose gravity on green paratroopa
	smb_move_enemy_horizontally(); return; // jump to move enemy horizontally
	smb_proc_move_red_p_troopa(); return;
}

void smb_proc_move_red_p_troopa() {
	set_a(ram[smb_enemy_y_speed + reg_x]);
	or_a(ram[smb_enemy_y_move_force + reg_x]); // check for any vertical force or speed
	if (!reg_p.z) { goto move_red_pt_up_or_down; } // branch if any found
	ram[smb_enemy_ymf_dummy + reg_x] = reg_a; // initialize something here
	set_a(ram[smb_enemy_y_position + reg_x]); // check current vs. original vertical coordinate
	cmp_a(ram[smb_red_p_troopa_orig_x_pos + reg_x]);
	if (reg_p.c) { goto move_red_pt_up_or_down; } // if current => original, skip ahead to more code
	set_a(ram[smb_frame_counter]); // get frame counter
	and_a(0b00000111); // mask out all but 3 LSB
	if (!reg_p.z) { goto no_inc_pt; } // if any bits set, branch to leave
	ram[smb_enemy_y_position + reg_x] = inc(ram[smb_enemy_y_position + reg_x]); // otherwise increment red paratroopa's vertical position
no_inc_pt:;
	return; // leave
move_red_pt_up_or_down:;
	set_a(ram[smb_enemy_y_position + reg_x]); // check current vs. central vertical coordinate
	cmp_a(ram[smb_red_p_troopa_center_y_pos + reg_x]);
	if (!reg_p.c) { goto mov_pt_dwn; } // if current < central, jump to move downwards
	smb_move_red_p_troopa_up(); return; // otherwise jump to move upwards
mov_pt_dwn:;
	smb_move_red_p_troopa_down(); return; // move downwards
	smb_move_fly_green_p_troopa(); return;
}

void smb_move_fly_green_p_troopa() {
	smb_x_move_cntr_green_p_troopa(); // do sub to increment primary and secondary counters
	smb_move_with_xm_cntrs(); // do sub to move green paratroopa accordingly, and horizontally
	set_y(0x01); // set Y to move green paratroopa down
	set_a(ram[smb_frame_counter]);
	and_a(0b00000011); // check frame counter 2 LSB for any bits set
	if (!reg_p.z) { goto no_mgpt; } // branch to leave if set to move up/down every fourth frame
	set_a(ram[smb_frame_counter]);
	and_a(0b01000000); // check frame counter for d6 set
	if (!reg_p.z) { goto y_sway; } // branch to move green paratroopa down if set
	set_y(0xff); // otherwise set Y to move green paratroopa up
y_sway:;
	ram[0x0000] = reg_y; // store adder here
	set_a(ram[smb_enemy_y_position + reg_x]);
	reg_p.c = 0; // add or subtract from vertical position
	add_a(ram[0x0000]); // to give green paratroopa a wavy flight
	ram[smb_enemy_y_position + reg_x] = reg_a;
no_mgpt:;
	return; // leave!
	smb_x_move_cntr_green_p_troopa(); return;
}

void smb_x_move_cntr_green_p_troopa() {
	set_a(0x13); // load preset maximum value for secondary counter
	smb_x_move_cntr_platform(); return;
}

void smb_x_move_cntr_platform() {
	ram[0x0001] = reg_a; // store value here
	set_a(ram[smb_frame_counter]);
	and_a(0b00000011); // branch to leave if not on
	if (!reg_p.z) { goto no_inc_xm; } // every fourth frame
	set_y(ram[smb_x_move_secondary_counter + reg_x]); // get secondary counter
	set_a(ram[smb_x_move_primary_counter + reg_x]); // get primary counter
	reg_a = shr(reg_a);
	if (reg_p.c) { goto dec_se_xm; } // if d0 of primary counter set, branch elsewhere
	cmp_y(ram[0x0001]); // compare secondary counter to preset maximum value
	if (reg_p.z) { goto inc_pxm; } // if equal, branch ahead of this part
	ram[smb_x_move_secondary_counter + reg_x] = inc(ram[smb_x_move_secondary_counter + reg_x]); // increment secondary counter and leave
no_inc_xm:;
	return;
inc_pxm:;
	ram[smb_x_move_primary_counter + reg_x] = inc(ram[smb_x_move_primary_counter + reg_x]); // increment primary counter and leave
	return;
dec_se_xm:;
	set_a(reg_y); // put secondary counter in A
	if (reg_p.z) { goto inc_pxm; } // if secondary counter at zero, branch back
	ram[smb_x_move_secondary_counter + reg_x] = dec(ram[smb_x_move_secondary_counter + reg_x]); // otherwise decrement secondary counter and leave
	return;
	smb_move_with_xm_cntrs(); return;
}

void smb_move_with_xm_cntrs() {
	set_a(ram[smb_x_move_secondary_counter + reg_x]); // save secondary counter to stack
	push(reg_a);
	set_y(0x01); // set value here by default
	set_a(ram[smb_x_move_primary_counter + reg_x]);
	and_a(0b00000010); // if d1 of primary counter is
	if (!reg_p.z) { goto xm_right; } // set, branch ahead of this part here
	set_a(ram[smb_x_move_secondary_counter + reg_x]);
	eor_a(0xff); // otherwise change secondary
	reg_p.c = 0; // counter to two's compliment
	add_a(0x01);
	ram[smb_x_move_secondary_counter + reg_x] = reg_a;
	set_y(0x02); // load alternate value here
xm_right:;
	ram[smb_enemy_moving_dir + reg_x] = reg_y; // store as moving direction
	smb_move_enemy_horizontally();
	ram[0x0000] = reg_a; // save value obtained from sub here
	set_a(pull()); // get secondary counter from stack
	ram[smb_x_move_secondary_counter + reg_x] = reg_a; // and return to original place
	return;
}

void smb_move_bloober() {
	set_a(ram[smb_enemy_state + reg_x]);
	and_a(0b00100000); // check enemy state for d5 set
	if (!reg_p.z) { goto move_defeated_bloober; } // branch if set to move defeated bloober
	set_y(ram[smb_secondary_hard_mode]); // use secondary hard mode flag as offset
	set_a(ram[smb_pseudo_random_bit_reg+1 + reg_x]); // get LSFR
	and_a(rom[smb_bloober_bitmasks + reg_y]); // mask out bits in LSFR using bitmask loaded with offset
	if (!reg_p.z) { goto bloober_swim; } // if any bits set, skip ahead to make swim
	set_a(reg_x);
	reg_a = shr(reg_a); // check to see if on second or fourth slot (1 or 3)
	if (!reg_p.c) { goto fb_left; } // if not, branch to figure out moving direction
	set_y(ram[smb_player_moving_dir]); // otherwise, load player's moving direction and
	if (reg_p.c) { goto sbm_dir; } // do an unconditional branch to set
fb_left:;
	set_y(0x02); // set left moving direction by default
	smb_player_enemy_diff(); // get horizontal difference between player and bloober
	if (!reg_p.n) { goto sbm_dir; } // if enemy to the right of player, keep left
	set_y(reg_y-1); // otherwise decrement to set right moving direction
sbm_dir:;
	ram[smb_enemy_moving_dir + reg_x] = reg_y; // set moving direction of bloober, then continue on here
bloober_swim:;
	smb_proc_swimming_b(); // execute sub to make bloober swim characteristically
	set_a(ram[smb_enemy_y_position + reg_x]); // get vertical coordinate
	reg_p.c = 1;
	sub_a(ram[smb_enemy_y_move_force + reg_x]); // subtract movement force
	cmp_a(0x20); // check to see if position is above edge of status bar
	if (!reg_p.c) { goto swim_x; } // if so, don't do it
	ram[smb_enemy_y_position + reg_x] = reg_a; // otherwise, set new vertical position, make bloober swim
swim_x:;
	set_y(ram[smb_enemy_moving_dir + reg_x]); // check moving direction
	set_y(reg_y-1);
	if (!reg_p.z) { goto left_swim; } // if moving to the left, branch to second part
	set_a(ram[smb_enemy_x_position + reg_x]);
	reg_p.c = 0; // add movement speed to horizontal coordinate
	add_a(ram[smb_blooper_move_speed + reg_x]);
	ram[smb_enemy_x_position + reg_x] = reg_a; // store result as new horizontal coordinate
	set_a(ram[smb_enemy_page_loc + reg_x]);
	add_a(0x00); // add carry to page location
	ram[smb_enemy_page_loc + reg_x] = reg_a; // store as new page location and leave
	return;
left_swim:;
	set_a(ram[smb_enemy_x_position + reg_x]);
	reg_p.c = 1; // subtract movement speed from horizontal coordinate
	sub_a(ram[smb_blooper_move_speed + reg_x]);
	ram[smb_enemy_x_position + reg_x] = reg_a; // store result as new horizontal coordinate
	set_a(ram[smb_enemy_page_loc + reg_x]);
	sub_a(0x00); // subtract borrow from page location
	ram[smb_enemy_page_loc + reg_x] = reg_a; // store as new page location and leave
	return;
move_defeated_bloober:;
	smb_move_enemy_slow_vert(); return; // jump to move defeated bloober downwards
	smb_proc_swimming_b(); return;
}

void smb_proc_swimming_b() {
	set_a(ram[smb_blooper_move_counter + reg_x]); // get enemy's movement counter
	and_a(0b00000010); // check for d1 set
	if (!reg_p.z) { goto chk_for_floatdown; } // branch if set
	set_a(ram[smb_frame_counter]);
	and_a(0b00000111); // get 3 LSB of frame counter
	push(reg_a); // get 3 LSB of frame counter
	set_a(ram[smb_blooper_move_counter + reg_x]); // get enemy's movement counter
	reg_a = shr(reg_a); // check for d0 set
	if (reg_p.c) { goto slow_swim; } // branch if set
	set_a(pull()); // pull 3 LSB of frame counter from the stack
	if (!reg_p.z) { goto b_swim_e; } // branch to leave, execute code only every eighth frame
	set_a(ram[smb_enemy_y_move_force + reg_x]);
	reg_p.c = 0; // add to movement force to speed up swim
	add_a(0x01);
	ram[smb_enemy_y_move_force + reg_x] = reg_a; // set movement force
	ram[smb_blooper_move_speed + reg_x] = reg_a; // set as movement speed
	cmp_a(0x02);
	if (!reg_p.z) { goto b_swim_e; } // if certain horizontal speed, branch to leave
	ram[smb_blooper_move_counter + reg_x] = inc(ram[smb_blooper_move_counter + reg_x]); // otherwise increment movement counter
b_swim_e:;
	return;
slow_swim:;
	set_a(pull()); // pull 3 LSB of frame counter from the stack
	if (!reg_p.z) { goto no_s_sw; } // branch to leave, execute code only every eighth frame
	set_a(ram[smb_enemy_y_move_force + reg_x]);
	reg_p.c = 1; // subtract from movement force to slow swim
	sub_a(0x01);
	ram[smb_enemy_y_move_force + reg_x] = reg_a; // set movement force
	ram[smb_blooper_move_speed + reg_x] = reg_a; // set as movement speed
	if (!reg_p.z) { goto no_s_sw; } // if any speed, branch to leave
	ram[smb_blooper_move_counter + reg_x] = inc(ram[smb_blooper_move_counter + reg_x]); // otherwise increment movement counter
	set_a(0x02);
	ram[smb_enemy_interval_timer + reg_x] = reg_a; // set enemy's timer
no_s_sw:;
	return; // leave
chk_for_floatdown:;
	set_a(ram[smb_enemy_interval_timer + reg_x]); // get enemy timer
	if (reg_p.z) { goto chk_neear_player; } // branch if expired
floatdown:;
	set_a(ram[smb_frame_counter]); // get frame counter
	reg_a = shr(reg_a); // check for d0 set
	if (reg_p.c) { goto no_fd; } // branch to leave on every other frame
	ram[smb_enemy_y_position + reg_x] = inc(ram[smb_enemy_y_position + reg_x]); // otherwise increment vertical coordinate
no_fd:;
	return; // leave
chk_neear_player:;
	set_a(ram[smb_enemy_y_position + reg_x]); // get vertical coordinate
	add_a(0x10); // add sixteen pixels
	cmp_a(ram[smb_player_y_position]); // compare result with player's vertical coordinate
	if (!reg_p.c) { goto floatdown; } // if modified vertical less than player's, branch
	set_a(0x00);
	ram[smb_blooper_move_counter + reg_x] = reg_a; // otherwise nullify movement counter
	return;
	smb_move_bullet_bill(); return;
}

void smb_move_bullet_bill() {
	set_a(ram[smb_enemy_state + reg_x]); // check bullet bill's enemy object state for d5 set
	and_a(0b00100000);
	if (reg_p.z) { goto not_def_b; } // if not set, continue with movement code
	smb_move_j_enemy_vertically(); return; // otherwise jump to move defeated bullet bill downwards
not_def_b:;
	set_a(0xe8); // set bullet bill's horizontal speed
	ram[smb_enemy_x_speed + reg_x] = reg_a; // and move it accordingly (note: this bullet bill
	smb_move_enemy_horizontally(); return; // object occurs in frenzy object $17, not from cannons)
}

void smb_move_swimming_cheep_cheep() {
	set_a(ram[smb_enemy_state + reg_x]); // check cheep-cheep's enemy object state
	and_a(0b00100000); // for d5 set
	if (reg_p.z) { goto cc_swim; } // if not set, continue with movement code
	smb_move_enemy_slow_vert(); return; // otherwise jump to move defeated cheep-cheep downwards
cc_swim:;
	ram[0x0003] = reg_a; // save enemy state in $03
	set_a(ram[smb_enemy_id + reg_x]); // get enemy identifier
	reg_p.c = 1;
	sub_a(0x0a); // subtract ten for cheep-cheep identifiers
	set_y(reg_a); // use as offset
	set_a(rom[smb_swim_ccx_move_data + reg_y]); // load value here
	ram[0x0002] = reg_a;
	set_a(ram[smb_enemy_x_move_force + reg_x]); // load horizontal force
	reg_p.c = 1;
	sub_a(ram[0x0002]); // subtract preset value from horizontal force
	ram[smb_enemy_x_move_force + reg_x] = reg_a; // store as new horizontal force
	set_a(ram[smb_enemy_x_position + reg_x]); // get horizontal coordinate
	sub_a(0x00); // subtract borrow (thus moving it slowly)
	ram[smb_enemy_x_position + reg_x] = reg_a; // and save as new horizontal coordinate
	set_a(ram[smb_enemy_page_loc + reg_x]);
	sub_a(0x00); // subtract borrow again, this time from the
	ram[smb_enemy_page_loc + reg_x] = reg_a; // page location, then save
	set_a(0x20);
	ram[0x0002] = reg_a; // save new value here
	cmp_x(0x02); // check enemy object offset
	if (!reg_p.c) { goto ex_sw_cc; } // if in first or second slot, branch to leave
	set_a(ram[smb_cheep_cheep_move_m_flag + reg_x]); // check movement flag
	cmp_a(0x10); // if movement speed set to $00,
	if (!reg_p.c) { goto cc_swim_upwards; } // branch to move upwards
	set_a(ram[smb_enemy_ymf_dummy + reg_x]);
	reg_p.c = 0;
	add_a(ram[0x0002]); // add preset value to dummy variable to get carry
	ram[smb_enemy_ymf_dummy + reg_x] = reg_a; // and save dummy
	set_a(ram[smb_enemy_y_position + reg_x]); // get vertical coordinate
	add_a(ram[0x0003]); // add carry to it plus enemy state to slowly move it downwards
	ram[smb_enemy_y_position + reg_x] = reg_a; // save as new vertical coordinate
	set_a(ram[smb_enemy_y_high_pos + reg_x]);
	add_a(0x00); // add carry to page location and
	goto chk_swim_y_pos; // jump to end of movement code
cc_swim_upwards:;
	set_a(ram[smb_enemy_ymf_dummy + reg_x]);
	reg_p.c = 1;
	sub_a(ram[0x0002]); // subtract preset value to dummy variable to get borrow
	ram[smb_enemy_ymf_dummy + reg_x] = reg_a; // and save dummy
	set_a(ram[smb_enemy_y_position + reg_x]); // get vertical coordinate
	sub_a(ram[0x0003]); // subtract borrow to it plus enemy state to slowly move it upwards
	ram[smb_enemy_y_position + reg_x] = reg_a; // save as new vertical coordinate
	set_a(ram[smb_enemy_y_high_pos + reg_x]);
	sub_a(0x00); // subtract borrow from page location
chk_swim_y_pos:;
	ram[smb_enemy_y_high_pos + reg_x] = reg_a; // save new page location here
	set_y(0x00); // load movement speed to upwards by default
	set_a(ram[smb_enemy_y_position + reg_x]); // get vertical coordinate
	reg_p.c = 1;
	sub_a(ram[smb_cheep_cheep_orig_y_pos + reg_x]); // subtract original coordinate from current
	if (!reg_p.n) { goto yp_diff; } // if result positive, skip to next part
	set_y(0x10); // otherwise load movement speed to downwards
	eor_a(0xff);
	reg_p.c = 0; // get two's compliment of result
	add_a(0x01); // to obtain total difference of original vs. current
yp_diff:;
	cmp_a(0x0f); // if difference between original vs. current vertical
	if (!reg_p.c) { goto ex_sw_cc; } // coordinates < 15 pixels, leave movement speed alone
	set_a(reg_y);
	ram[smb_cheep_cheep_move_m_flag + reg_x] = reg_a; // otherwise change movement speed
ex_sw_cc:;
	return; // leave
}

void smb_proc_firebar() {
	smb_get_enemy_offscreen_bits(); // get offscreen information
	set_a(ram[smb_enemy_offscreen_bits]); // check for d3 set
	and_a(0b00001000); // if so, branch to leave
	if (!reg_p.z) { goto skip_f_bar; }
	set_a(ram[smb_timer_control]); // if master timer control set, branch
	if (!reg_p.z) { goto sus_fbar; } // ahead of this part
	set_a(ram[smb_firebar_spin_speed + reg_x]); // load spinning speed of firebar
	smb_firebar_spin(); // modify current spinstate
	and_a(0b00011111); // mask out all but 5 LSB
	ram[smb_firebar_spin_state_high + reg_x] = reg_a; // and store as new high byte of spinstate
sus_fbar:;
	set_a(ram[smb_firebar_spin_state_high + reg_x]); // get high byte of spinstate
	set_y(ram[smb_enemy_id + reg_x]); // check enemy identifier
	cmp_y(0x1f);
	if (!reg_p.c) { goto setup_gfb; } // if < $1f (long firebar), branch
	cmp_a(0x08); // check high byte of spinstate
	if (reg_p.z) { goto skp_f_ste; } // if eight, branch to change
	cmp_a(0x18);
	if (!reg_p.z) { goto setup_gfb; } // if not at twenty-four branch to not change
skp_f_ste:;
	reg_p.c = 0;
	add_a(0x01); // add one to spinning thing to avoid horizontal state
	ram[smb_firebar_spin_state_high + reg_x] = reg_a;
setup_gfb:;
	ram[0x00ef] = reg_a; // save high byte of spinning thing, modified or otherwise
	smb_relative_enemy_position(); // get relative coordinates to screen
	smb_get_firebar_position(); // do a sub here (residual, too early to be used now)
	set_y(ram[smb_enemy_spr_data_offset + reg_x]); // get OAM data offset
	set_a(ram[smb_enemy_rel_y_pos]); // get relative vertical coordinate
	ram[smb_sprite_y_position + reg_y] = reg_a; // store as Y in OAM data
	ram[0x0007] = reg_a; // also save here
	set_a(ram[smb_enemy_rel_x_pos]); // get relative horizontal coordinate
	ram[smb_sprite_x_position + reg_y] = reg_a; // store as X in OAM data
	ram[0x0006] = reg_a; // also save here
	set_a(0x01);
	ram[0x0000] = reg_a; // set $01 value here (not necessary)
	smb_firebar_collision(); // draw fireball part and do collision detection
	set_y(0x05); // load value for short firebars by default
	set_a(ram[smb_enemy_id + reg_x]);
	cmp_a(0x1f); // are we doing a long firebar?
	if (!reg_p.c) { goto set_m_fbar; } // no, branch then
	set_y(0x0b); // otherwise load value for long firebars
set_m_fbar:;
	ram[0x00ed] = reg_y; // store maximum value for length of firebars
	set_a(0x00);
	ram[0x0000] = reg_a; // initialize counter here
draw_fbar:;
	set_a(ram[0x00ef]); // load high byte of spinstate
	smb_get_firebar_position(); // get fireball position data depending on firebar part
	smb_draw_firebar_collision(); // position it properly, draw it and do collision detection
	set_a(ram[0x0000]); // check which firebar part
	cmp_a(0x04);
	if (!reg_p.z) { goto next_fbar; }
	set_y(ram[smb_duplicate_obj_offset]); // if we arrive at fifth firebar part,
	set_a(ram[smb_enemy_spr_data_offset + reg_y]); // get offset from long firebar and load OAM data offset
	ram[0x0006] = reg_a; // using long firebar offset, then store as new one here
next_fbar:;
	ram[0x0000] = inc(ram[0x0000]); // move onto the next firebar part
	set_a(ram[0x0000]);
	cmp_a(ram[0x00ed]); // if we end up at the maximum part, go on and leave
	if (!reg_p.c) { goto draw_fbar; } // otherwise go back and do another
skip_f_bar:;
	return;
	smb_draw_firebar_collision(); return;
}

void smb_draw_firebar_collision() {
	set_a(ram[0x0003]); // store mirror data elsewhere
	ram[0x0005] = reg_a;
	set_y(ram[0x0006]); // load OAM data offset for firebar
	set_a(ram[0x0001]); // load horizontal adder we got from position loader
	ram[0x0005] = shr(ram[0x0005]); // shift LSB of mirror data
	if (reg_p.c) { goto add_ha; } // if carry was set, skip this part
	eor_a(0xff);
	add_a(0x01); // otherwise get two's compliment of horizontal adder
add_ha:;
	reg_p.c = 0; // add horizontal coordinate relative to screen to
	add_a(ram[smb_enemy_rel_x_pos]); // horizontal adder, modified or otherwise
	ram[smb_sprite_x_position + reg_y] = reg_a; // store as X coordinate here
	ram[0x0006] = reg_a; // store here for now, note offset is saved in Y still
	cmp_a(ram[smb_enemy_rel_x_pos]); // compare X coordinate of sprite to original X of firebar
	if (reg_p.c) { goto subt_r_1; } // if sprite coordinate => original coordinate, branch
	set_a(ram[smb_enemy_rel_x_pos]);
	reg_p.c = 1; // otherwise subtract sprite X from the
	sub_a(ram[0x0006]); // original one and skip this part
	goto chk_f_ofs;
subt_r_1:;
	reg_p.c = 1; // subtract original X from the
	sub_a(ram[smb_enemy_rel_x_pos]); // current sprite X
chk_f_ofs:;
	cmp_a(0x59); // if difference of coordinates within a certain range,
	if (!reg_p.c) { goto va_handl; } // continue by handling vertical adder
	set_a(0xf8); // otherwise, load offscreen Y coordinate
	if (!reg_p.z) { goto set_v_fbr; } // and unconditionally branch to move sprite offscreen
va_handl:;
	set_a(ram[smb_enemy_rel_y_pos]); // if vertical relative coordinate offscreen,
	cmp_a(0xf8); // skip ahead of this part and write into sprite Y coordinate
	if (reg_p.z) { goto set_v_fbr; }
	set_a(ram[0x0002]); // load vertical adder we got from position loader
	ram[0x0005] = shr(ram[0x0005]); // shift LSB of mirror data one more time
	if (reg_p.c) { goto add_va; } // if carry was set, skip this part
	eor_a(0xff);
	add_a(0x01); // otherwise get two's compliment of second part
add_va:;
	reg_p.c = 0; // add vertical coordinate relative to screen to
	add_a(ram[smb_enemy_rel_y_pos]); // the second data, modified or otherwise
set_v_fbr:;
	ram[smb_sprite_y_position + reg_y] = reg_a; // store as Y coordinate here
	ram[0x0007] = reg_a; // also store here for now
	smb_firebar_collision(); return;
}

void smb_firebar_collision() {
	smb_draw_firebar(); // run sub here to draw current tile of firebar
	set_a(reg_y); // return OAM data offset and save
	push(reg_a); // to the stack for now
	set_a(ram[smb_star_invincible_timer]); // if star mario invincibility timer
	or_a(ram[smb_timer_control]); // or master timer controls set
	if (!reg_p.z) { goto no_col_fb; } // then skip all of this
	ram[0x0005] = reg_a; // otherwise initialize counter
	set_y(ram[smb_player_y_high_pos]);
	set_y(reg_y-1); // if player's vertical high byte offscreen,
	if (!reg_p.z) { goto no_col_fb; } // skip all of this
	set_y(ram[smb_player_y_position]); // get player's vertical position
	set_a(ram[smb_player_size]); // get player's size
	if (!reg_p.z) { goto adj_sm; } // if player small, branch to alter variables
	set_a(ram[smb_crouching_flag]);
	if (reg_p.z) { goto big_jp; } // if player big and not crouching, jump ahead
adj_sm:;
	ram[0x0005] = inc(ram[0x0005]); // if small or big but crouching, execute this part
	ram[0x0005] = inc(ram[0x0005]); // first increment our counter twice (setting $02 as flag)
	set_a(reg_y);
	reg_p.c = 0; // then add 24 pixels to the player's
	add_a(0x18); // vertical coordinate
	set_y(reg_a);
big_jp:;
	set_a(reg_y); // get vertical coordinate, altered or otherwise, from Y
fbc_loop:;
	reg_p.c = 1; // subtract vertical position of firebar
	sub_a(ram[0x0007]); // from the vertical coordinate of the player
	if (!reg_p.n) { goto chk_vfbd; } // if player lower on the screen than firebar,
	eor_a(0xff); // skip two's compliment part
	reg_p.c = 0; // otherwise get two's compliment
	add_a(0x01);
chk_vfbd:;
	cmp_a(0x08); // if difference => 8 pixels, skip ahead of this part
	if (reg_p.c) { goto chk_20_fs; }
	set_a(ram[0x0006]); // if firebar on far right on the screen, skip this,
	cmp_a(0xf0); // because, really, what's the point?
	if (reg_p.c) { goto chk_20_fs; }
	set_a(ram[smb_sprite_x_position+4]); // get OAM X coordinate for sprite #1
	reg_p.c = 0;
	add_a(0x04); // add four pixels
	ram[0x0004] = reg_a; // store here
	reg_p.c = 1; // subtract horizontal coordinate of firebar
	sub_a(ram[0x0006]); // from the X coordinate of player's sprite 1
	if (!reg_p.n) { goto chk_fb_cl; } // if modded X coordinate to the right of firebar
	eor_a(0xff); // skip two's compliment part
	reg_p.c = 0; // otherwise get two's compliment
	add_a(0x01);
chk_fb_cl:;
	cmp_a(0x08); // if difference < 8 pixels, collision, thus branch
	if (!reg_p.c) { goto chg_s_dir; } // to process
chk_20_fs:;
	set_a(ram[0x0005]); // if value of $02 was set earlier for whatever reason,
	cmp_a(0x02); // branch to increment OAM offset and leave, no collision
	if (reg_p.z) { goto no_col_fb; }
	set_y(ram[0x0005]); // otherwise get temp here and use as offset
	set_a(ram[smb_player_y_position]);
	reg_p.c = 0;
	add_a(rom[smb_firebar_y_pos + reg_y]); // add value loaded with offset to player's vertical coordinate
	ram[0x0005] = inc(ram[0x0005]); // then increment temp and jump back
	goto fbc_loop;
chg_s_dir:;
	set_x(0x01); // set movement direction by default
	set_a(ram[0x0004]); // if OAM X coordinate of player's sprite 1
	cmp_a(ram[0x0006]); // is greater than horizontal coordinate of firebar
	if (reg_p.c) { goto set_s_dir; } // then do not alter movement direction
	set_x(reg_x+1); // otherwise increment it
set_s_dir:;
	ram[smb_enemy_moving_dir] = reg_x; // store movement direction here
	set_x(0x00);
	set_a(ram[0x0000]); // save value written to $00 to stack
	push(reg_a);
	smb_injure_player(); // perform sub to hurt or kill player
	set_a(pull());
	ram[0x0000] = reg_a; // get value of $00 from stack
no_col_fb:;
	set_a(pull()); // get OAM data offset
	reg_p.c = 0; // add four to it and save
	add_a(0x04);
	ram[0x0006] = reg_a;
	set_x(ram[smb_object_offset]); // get enemy object buffer offset and leave
	return;
	smb_get_firebar_position(); return;
}

void smb_get_firebar_position() {
	push(reg_a); // save high byte of spinstate to the stack
	and_a(0b00001111); // mask out low nybble
	cmp_a(0x09);
	if (!reg_p.c) { goto get_h_adder; } // if lower than $09, branch ahead
	eor_a(0b00001111); // otherwise get two's compliment to oscillate
	reg_p.c = 0;
	add_a(0x01);
get_h_adder:;
	ram[0x0001] = reg_a; // store result, modified or not, here
	set_y(ram[0x0000]); // load number of firebar ball where we're at
	set_a(rom[smb_firebar_tbl_offsets + reg_y]); // load offset to firebar position data
	reg_p.c = 0;
	add_a(ram[0x0001]); // add oscillated high byte of spinstate
	set_y(reg_a); // to offset here and use as new offset
	set_a(rom[smb_firebar_pos_lookup_tbl + reg_y]); // get data here and store as horizontal adder
	ram[0x0001] = reg_a;
	set_a(pull()); // pull whatever was in A from the stack
	push(reg_a); // save it again because we still need it
	reg_p.c = 0;
	add_a(0x08); // add eight this time, to get vertical adder
	and_a(0b00001111); // mask out high nybble
	cmp_a(0x09); // if lower than $09, branch ahead
	if (!reg_p.c) { goto get_v_adder; }
	eor_a(0b00001111); // otherwise get two's compliment
	reg_p.c = 0;
	add_a(0x01);
get_v_adder:;
	ram[0x0002] = reg_a; // store result here
	set_y(ram[0x0000]);
	set_a(rom[smb_firebar_tbl_offsets + reg_y]); // load offset to firebar position data again
	reg_p.c = 0;
	add_a(ram[0x0002]); // this time add value in $02 to offset here and use as offset
	set_y(reg_a);
	set_a(rom[smb_firebar_pos_lookup_tbl + reg_y]); // get data here and store as vertica adder
	ram[0x0002] = reg_a;
	set_a(pull()); // pull out whatever was in A one last time
	reg_a = shr(reg_a); // divide by eight or shift three to the right
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	set_y(reg_a); // use as offset
	set_a(rom[smb_firebar_mirror_data + reg_y]); // load mirroring data here
	ram[0x0003] = reg_a; // store
	return;
}

void smb_move_flying_cheep_cheep() {
	set_a(ram[smb_enemy_state + reg_x]); // check cheep-cheep's enemy state
	and_a(0b00100000); // for d5 set
	if (reg_p.z) { goto fly_cc; } // branch to continue code if not set
	set_a(0x00);
	ram[smb_enemy_spr_attrib + reg_x] = reg_a; // otherwise clear sprite attributes
	smb_move_j_enemy_vertically(); return; // and jump to move defeated cheep-cheep downwards
fly_cc:;
	smb_move_enemy_horizontally(); // move cheep-cheep horizontally based on speed and force
	set_y(0x0d); // set vertical movement amount
	set_a(0x05); // set maximum speed
	smb_set_x_move_amt(); // branch to impose gravity on flying cheep-cheep
	set_a(ram[smb_enemy_y_move_force + reg_x]);
	reg_a = shr(reg_a); // get vertical movement force and
	reg_a = shr(reg_a); // move high nybble to low
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	set_y(reg_a); // save as offset (note this tends to go into reach of code)
	set_a(ram[smb_enemy_y_position + reg_x]); // get vertical position
	reg_p.c = 1; // subtract pseudorandom value based on offset from position
	sub_a(rom[smb_p_random_subtracter + reg_y]);
	if (!reg_p.n) { goto add_ccf; } // if result within top half of screen, skip this part
	eor_a(0xff);
	reg_p.c = 0; // otherwise get two's compliment
	add_a(0x01);
add_ccf:;
	cmp_a(0x08); // if result or two's compliment greater than eight,
	if (reg_p.c) { goto bp_get; } // skip to the end without changing movement force
	set_a(ram[smb_enemy_y_move_force + reg_x]);
	reg_p.c = 0;
	add_a(0x10); // otherwise add to it
	ram[smb_enemy_y_move_force + reg_x] = reg_a;
	reg_a = shr(reg_a); // move high nybble to low again
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	set_y(reg_a);
bp_get:;
	set_a(rom[smb_fly_ccb_priority + reg_y]); // load bg priority data and store (this is very likely
	ram[smb_enemy_spr_attrib + reg_x] = reg_a; // broken or residual code, value is overwritten before
	return; // drawing it next frame), then leave
}

void smb_move_lakitu() {
	set_a(ram[smb_enemy_state + reg_x]); // check lakitu's enemy state
	and_a(0b00100000); // for d5 set
	if (reg_p.z) { goto chk_ls; } // if not set, continue with code
	smb_move_d_enemy_vertically(); return; // otherwise jump to move defeated lakitu downwards
chk_ls:;
	set_a(ram[smb_enemy_state + reg_x]); // if lakitu's enemy state not set at all,
	if (reg_p.z) { goto fr_12_s; } // go ahead and continue with code
	set_a(0x00);
	ram[smb_lakitu_move_direction + reg_x] = reg_a; // otherwise initialize moving direction to move to left
	ram[smb_enemy_frenzy_buffer] = reg_a; // initialize frenzy buffer
	set_a(0x10);
	if (!reg_p.z) { goto set_l_spd; } // load horizontal speed and do unconditional branch
fr_12_s:;
	set_a((smb_spiny));
	ram[smb_enemy_frenzy_buffer] = reg_a; // set spiny identifier in frenzy buffer
	set_y(0x02);
ld_l_da:;
	set_a(rom[smb_lakitu_diff_adj + reg_y]); // load values
	ram[0x0001 + reg_y] = reg_a; // store in zero page
	set_y(reg_y-1);
	if (!reg_p.n) { goto ld_l_da; } // do this until all values are stired
	smb_player_lakitu_diff(); // execute sub to set speed and create spinys
set_l_spd:;
	ram[smb_lakitu_move_speed + reg_x] = reg_a; // set movement speed returned from sub
	set_y(0x01); // set moving direction to right by default
	set_a(ram[smb_lakitu_move_direction + reg_x]);
	and_a(0x01); // get LSB of moving direction
	if (!reg_p.z) { goto set_l_mov; } // if set, branch to the end to use moving direction
	set_a(ram[smb_lakitu_move_speed + reg_x]);
	eor_a(0xff); // get two's compliment of moving speed
	reg_p.c = 0;
	add_a(0x01);
	ram[smb_lakitu_move_speed + reg_x] = reg_a; // store as new moving speed
	set_y(reg_y+1); // increment moving direction to left
set_l_mov:;
	ram[smb_enemy_moving_dir + reg_x] = reg_y; // store moving direction
	smb_move_enemy_horizontally(); return; // move lakitu horizontally
	smb_player_lakitu_diff(); return;
}

void smb_player_lakitu_diff() {
	set_y(0x00); // set Y for default value
	smb_player_enemy_diff(); // get horizontal difference between enemy and player
	if (!reg_p.n) { goto chk_lak_dif; } // branch if enemy is to the right of the player
	set_y(reg_y+1); // increment Y for left of player
	set_a(ram[0x0000]);
	eor_a(0xff); // get two's compliment of low byte of horizontal difference
	reg_p.c = 0;
	add_a(0x01); // store two's compliment as horizontal difference
	ram[0x0000] = reg_a;
chk_lak_dif:;
	set_a(ram[0x0000]); // get low byte of horizontal difference
	cmp_a(0x3c); // if within a certain distance of player, branch
	if (!reg_p.c) { goto chk_p_speed; }
	set_a(0x3c); // otherwise set maximum distance
	ram[0x0000] = reg_a;
	set_a(ram[smb_enemy_id + reg_x]); // check if lakitu is in our current enemy slot
	cmp_a((smb_lakitu));
	if (!reg_p.z) { goto chk_p_speed; } // if not, branch elsewhere
	set_a(reg_y); // compare contents of Y, now in A
	cmp_a(ram[smb_lakitu_move_direction + reg_x]); // to what is being used as horizontal movement direction
	if (reg_p.z) { goto chk_p_speed; } // if moving toward the player, branch, do not alter
	set_a(ram[smb_lakitu_move_direction + reg_x]); // if moving to the left beyond maximum distance,
	if (reg_p.z) { goto set_l_mov_d; } // branch and alter without delay
	ram[smb_lakitu_move_speed + reg_x] = dec(ram[smb_lakitu_move_speed + reg_x]); // decrement horizontal speed
	set_a(ram[smb_lakitu_move_speed + reg_x]); // if horizontal speed not yet at zero, branch to leave
	if (!reg_p.z) { goto ex_move_lak; }
set_l_mov_d:;
	set_a(reg_y); // set horizontal direction depending on horizontal
	ram[smb_lakitu_move_direction + reg_x] = reg_a; // difference between enemy and player if necessary
chk_p_speed:;
	set_a(ram[0x0000]);
	and_a(0b00111100); // mask out all but four bits in the middle
	reg_a = shr(reg_a); // divide masked difference by four
	reg_a = shr(reg_a);
	ram[0x0000] = reg_a; // store as new value
	set_y(0x00); // init offset
	set_a(ram[smb_player_x_speed]);
	if (reg_p.z) { goto sub_dif_adj; } // if player not moving horizontally, branch
	set_a(ram[smb_scroll_amount]);
	if (reg_p.z) { goto sub_dif_adj; } // if scroll speed not set, branch to same place
	set_y(reg_y+1); // otherwise increment offset
	set_a(ram[smb_player_x_speed]);
lcfb_5:;
	cmp_a(0x19); // if player not running, branch
	if (!reg_p.c) { goto chk_spiny_o; }
	set_a(ram[smb_scroll_amount]);
	cmp_a(0x02); // if scroll speed below a certain amount, branch
	if (!reg_p.c) { goto chk_spiny_o; } // to same place
	set_y(reg_y+1); // otherwise increment once more
chk_spiny_o:;
	set_a(ram[smb_enemy_id + reg_x]); // check for spiny object
	cmp_a((smb_spiny));
	if (!reg_p.z) { goto chk_emy_spd; } // branch if not found
	set_a(ram[smb_player_x_speed]); // if player not moving, skip this part
	if (!reg_p.z) { goto sub_dif_adj; }
chk_emy_spd:;
	set_a(ram[smb_lakitu_move_direction + reg_x]); // check vertical speed
	if (!reg_p.z) { goto sub_dif_adj; } // branch if nonzero
	set_y(0x00); // otherwise reinit offset
sub_dif_adj:;
	set_a(ram[0x0001 + reg_y]); // get one of three saved values from earlier
	set_y(ram[0x0000]); // get saved horizontal difference
s_pixel_lak:;
	reg_p.c = 1; // subtract one for each pixel of horizontal difference
	sub_a(0x01); // from one of three saved values
	set_y(reg_y-1);
	if (!reg_p.n) { goto s_pixel_lak; } // branch until all pixels are subtracted, to adjust difference
ex_move_lak:;
	return; // leave!!!
}

void smb_bridge_collapse() {
	set_x(ram[smb_bowser_front_offset]); // get enemy offset for bowser
	set_a(ram[smb_enemy_id + reg_x]); // check enemy object identifier for bowser
	cmp_a((smb_bowser)); // if not found, branch ahead,
	if (!reg_p.z) { goto set_m_2; } // metatile removal not necessary
	ram[smb_object_offset] = reg_x; // store as enemy offset here
	set_a(ram[smb_enemy_state + reg_x]); // if bowser in normal state, skip all of this
	if (reg_p.z) { smb_remove_bridge(); return; }
	and_a(0b01000000); // if bowser's state has d6 clear, skip to silence music
	if (reg_p.z) { goto set_m_2; }
	set_a(ram[smb_enemy_y_position + reg_x]); // check bowser's vertical coordinate
	cmp_a(0xe0); // if bowser not yet low enough, skip this part ahead
	if (!reg_p.c) { smb_move_d_bowser(); return; }
set_m_2:;
	set_a((smb_silence)); // silence music
	ram[smb_event_music_queue] = reg_a;
	ram[smb_oper_mode_task] = inc(ram[smb_oper_mode_task]); // move onto next secondary mode in autoctrl mode
	smb_kill_all_enemies(); return; // jump to empty all enemy slots and then leave
	smb_move_d_bowser(); return;
}

void smb_move_d_bowser() {
	smb_move_enemy_slow_vert(); // do a sub to move bowser downwards
	smb_bowser_gfx_handler(); return; // jump to draw bowser's front and rear, then leave
	smb_remove_bridge(); return;
}

void smb_remove_bridge() {
	ram[smb_bowser_feet_counter] = dec(ram[smb_bowser_feet_counter]); // decrement timer to control bowser's feet
	if (!reg_p.z) { goto no_b_fall; } // if not expired, skip all of this
	set_a(0x04);
	ram[smb_bowser_feet_counter] = reg_a; // otherwise, set timer now
	set_a(ram[smb_bowser_body_controls]);
	eor_a(0x01); // invert bit to control bowser's feet
	ram[smb_bowser_body_controls] = reg_a;
	set_a(0x22); // put high byte of name table address here for now
	ram[0x0005] = reg_a;
	set_y(ram[smb_bridge_collapse_offset]); // get bridge collapse offset here
	set_a(rom[smb_bridge_collapse_data + reg_y]); // load low byte of name table address and store here
	ram[0x0004] = reg_a;
	set_y(ram[smb_vram_buffer_1_offset]); // increment vram buffer offset
	set_y(reg_y+1);
	set_x(0x0c); // set offset for tile data for sub to draw blank metatile
	smb_rem_bridge(); // do sub here to remove bowser's bridge metatiles
	set_x(ram[smb_object_offset]); // get enemy offset
	smb_move_v_offset(); // set new vram buffer offset
	set_a((smb_sfx_blast)); // load the fireworks/gunfire sound into the square 2 sfx
	ram[smb_square_2_sound_queue] = reg_a; // queue while at the same time loading the brick
	set_a((smb_sfx_brick_shatter)); // shatter sound into the noise sfx queue thus
	ram[smb_noise_sound_queue] = reg_a; // producing the unique sound of the bridge collapsing
	ram[smb_bridge_collapse_offset] = inc(ram[smb_bridge_collapse_offset]); // increment bridge collapse offset
	set_a(ram[smb_bridge_collapse_offset]);
	cmp_a(0x0f); // if bridge collapse offset has not yet reached
	if (!reg_p.z) { goto no_b_fall; } // the end, go ahead and skip this part
	smb_init_v_stf(); // initialize whatever vertical speed bowser has
	set_a(0b01000000);
	ram[smb_enemy_state + reg_x] = reg_a; // set bowser's state to one of defeated states (d6 set)
	set_a((smb_sfx_bowser_fall));
	ram[smb_square_2_sound_queue] = reg_a; // play bowser defeat sound
no_b_fall:;
	smb_bowser_gfx_handler(); return; // jump to code that draws bowser
}

void smb_run_bowser() {
	set_a(ram[smb_enemy_state + reg_x]); // if d5 in enemy state is not set
	and_a(0b00100000); // then branch elsewhere to run bowser
	if (reg_p.z) { smb_bowser_control(); return; }
	set_a(ram[smb_enemy_y_position + reg_x]); // otherwise check vertical position
	cmp_a(0xe0); // if above a certain point, branch to move defeated bowser
	if (!reg_p.c) { smb_move_d_bowser(); return; } // otherwise proceed to smb_kill_all_enemies
	smb_kill_all_enemies(); return;
}

void smb_kill_all_enemies() {
	set_x(0x04); // start with last enemy slot
kill_loop:;
	smb_erase_enemy_object(); // branch to kill enemy objects
	set_x(reg_x-1); // move onto next enemy slot
	if (!reg_p.n) { goto kill_loop; } // do this until all slots are emptied
	ram[smb_enemy_frenzy_buffer] = reg_a; // empty frenzy buffer
	set_x(ram[smb_object_offset]); // get enemy object offset and leave
	return;
	smb_bowser_control(); return;
}

void smb_bowser_control() {
	set_a(0x00);
	ram[smb_enemy_frenzy_buffer] = reg_a; // empty frenzy buffer
	set_a(ram[smb_timer_control]); // if master timer control not set,
	if (reg_p.z) { goto chk_mouth; } // skip jump and execute code here
	goto skip_to_fb; // otherwise, jump over a bunch of code
chk_mouth:;
	set_a(ram[smb_bowser_body_controls]); // check bowser's mouth
	if (!reg_p.n) { goto feet_tmr; } // if bit clear, go ahead with code here
	goto hammer_chk; // otherwise skip a whole section starting here
feet_tmr:;
	ram[smb_bowser_feet_counter] = dec(ram[smb_bowser_feet_counter]); // decrement timer to control bowser's feet
	if (!reg_p.z) { goto reset_m_dr; } // if not expired, skip this part
	set_a(0x20); // otherwise, reset timer
	ram[smb_bowser_feet_counter] = reg_a;
	set_a(ram[smb_bowser_body_controls]); // and invert bit used
	eor_a(0b00000001); // to control bowser's feet
	ram[smb_bowser_body_controls] = reg_a;
reset_m_dr:;
	set_a(ram[smb_frame_counter]); // check frame counter
	and_a(0b00001111); // if not on every sixteenth frame, skip
	if (!reg_p.z) { goto b_face_p; } // ahead to continue code
	set_a(0x02); // otherwise reset moving/facing direction every
	ram[smb_enemy_moving_dir + reg_x] = reg_a; // sixteen frames
b_face_p:;
	set_a(ram[smb_enemy_frame_timer + reg_x]); // if timer set here expired,
	if (reg_p.z) { goto get_pr_cmp; } // branch to next section
	smb_player_enemy_diff(); // get horizontal difference between player and bowser,
	if (!reg_p.n) { goto get_pr_cmp; } // and branch if bowser to the right of the player
	set_a(0x01);
	ram[smb_enemy_moving_dir + reg_x] = reg_a; // set bowser to move and face to the right
	set_a(0x02);
	ram[smb_bowser_movement_speed] = reg_a; // set movement speed
	set_a(0x20);
	ram[smb_enemy_frame_timer + reg_x] = reg_a; // set timer here
	ram[smb_bowser_fire_breath_timer] = reg_a; // set timer used for bowser's flame
	set_a(ram[smb_enemy_x_position + reg_x]);
	cmp_a(0xc8); // if bowser to the right past a certain point,
	if (reg_p.c) { goto hammer_chk; } // skip ahead to some other section
get_pr_cmp:;
	set_a(ram[smb_frame_counter]); // get frame counter
	and_a(0b00000011);
	if (!reg_p.z) { goto hammer_chk; } // execute this code every fourth frame, otherwise branch
	set_a(ram[smb_enemy_x_position + reg_x]);
	cmp_a(ram[smb_bowser_orig_x_pos]); // if bowser not at original horizontal position,
	if (!reg_p.z) { goto get_d_to_o; } // branch to skip this part
	set_a(ram[smb_pseudo_random_bit_reg + reg_x]);
	and_a(0b00000011); // get pseudorandom offset
	set_y(reg_a);
	set_a(rom[smb_p_random_range + reg_y]); // load value using pseudorandom offset
	ram[smb_max_range_from_origin] = reg_a; // and store here
get_d_to_o:;
	set_a(ram[smb_enemy_x_position + reg_x]);
	reg_p.c = 0; // add movement speed to bowser's horizontal
	add_a(ram[smb_bowser_movement_speed]); // coordinate and save as new horizontal position
	ram[smb_enemy_x_position + reg_x] = reg_a;
	set_y(ram[smb_enemy_moving_dir + reg_x]);
	cmp_y(0x01); // if bowser moving and facing to the right, skip ahead
	if (reg_p.z) { goto hammer_chk; }
	set_y(0xff); // set default movement speed here (move left)
	reg_p.c = 1; // get difference of current vs. original
	sub_a(ram[smb_bowser_orig_x_pos]); // horizontal position
	if (!reg_p.n) { goto comp_dto_o; } // if current position to the right of original, skip ahead
	eor_a(0xff);
	reg_p.c = 0; // get two's compliment
	add_a(0x01);
	set_y(0x01); // set alternate movement speed here (move right)
comp_dto_o:;
	cmp_a(ram[smb_max_range_from_origin]); // compare difference with pseudorandom value
	if (!reg_p.c) { goto hammer_chk; } // if difference < pseudorandom value, leave speed alone
	ram[smb_bowser_movement_speed] = reg_y; // otherwise change bowser's movement speed
hammer_chk:;
	set_a(ram[smb_enemy_frame_timer + reg_x]); // if timer set here not expired yet, skip ahead to
	if (!reg_p.z) { goto make_b_jump; } // some other section of code
	smb_move_enemy_slow_vert(); // otherwise start by moving bowser downwards
	set_a(ram[smb_world_number]); // check world number
	cmp_a((smb_world_6));
	if (!reg_p.c) { goto set_hmr_tmr; } // if world 1-5, skip this part (not time to throw hammers yet)
	set_a(ram[smb_frame_counter]);
	and_a(0b00000011); // check to see if it's time to execute sub
	if (!reg_p.z) { goto set_hmr_tmr; } // if not, skip sub, otherwise
	smb_spawn_hammer_obj(); // execute sub on every fourth frame to spawn misc object (hammer)
set_hmr_tmr:;
	set_a(ram[smb_enemy_y_position + reg_x]); // get current vertical position
	cmp_a(0x80); // if still above a certain point
	if (!reg_p.c) { goto chk_fire_b; } // then skip to world number check for flames
	set_a(ram[smb_pseudo_random_bit_reg + reg_x]);
	and_a(0b00000011); // get pseudorandom offset
	set_y(reg_a);
	set_a(rom[smb_p_random_range + reg_y]); // get value using pseudorandom offset
	ram[smb_enemy_frame_timer + reg_x] = reg_a; // set for timer here
skip_to_fb:;
	goto chk_fire_b; // jump to execute flames code
make_b_jump:;
	cmp_a(0x01); // if timer not yet about to expire,
	if (!reg_p.z) { goto chk_fire_b; } // skip ahead to next part
	ram[smb_enemy_y_position + reg_x] = dec(ram[smb_enemy_y_position + reg_x]); // otherwise decrement vertical coordinate
	smb_init_v_stf(); // initialize movement amount
	set_a(0xfe);
	ram[smb_enemy_y_speed + reg_x] = reg_a; // set vertical speed to move bowser upwards
chk_fire_b:;
	set_a(ram[smb_world_number]); // check world number here
	cmp_a((smb_world_8)); // world 8?
	if (reg_p.z) { goto spawn_f_br; } // if so, execute this part here
	cmp_a((smb_world_6)); // world 6-7?
	if (reg_p.c) { smb_bowser_gfx_handler(); return; } // if so, skip this part here
spawn_f_br:;
	set_a(ram[smb_bowser_fire_breath_timer]); // check timer here
	if (!reg_p.z) { smb_bowser_gfx_handler(); return; } // if not expired yet, skip all of this
	set_a(0x20);
	ram[smb_bowser_fire_breath_timer] = reg_a; // set timer here
	set_a(ram[smb_bowser_body_controls]);
	eor_a(0b10000000); // invert bowser's mouth bit to open
	ram[smb_bowser_body_controls] = reg_a; // and close bowser's mouth
	if (reg_p.n) { goto chk_fire_b; } // if bowser's mouth open, loop back
	smb_set_flame_timer(); // get timing for bowser's flame
	set_y(ram[smb_secondary_hard_mode]);
	if (reg_p.z) { goto set_fb_tmr; } // if secondary hard mode flag not set, skip this
	reg_p.c = 1;
	sub_a(0x10); // otherwise subtract from value in A
set_fb_tmr:;
	ram[smb_bowser_fire_breath_timer] = reg_a; // set value as timer here
	set_a(0x15); // put bowser's flame identifier
	ram[smb_enemy_frenzy_buffer] = reg_a; // in enemy frenzy buffer
	smb_bowser_gfx_handler(); return;
}

void smb_bowser_gfx_handler() {
	smb_process_bowser_half(); // do a sub here to process bowser's front
	set_y(0x10); // load default value here to position bowser's rear
	set_a(ram[smb_enemy_moving_dir + reg_x]); // check moving direction
	reg_a = shr(reg_a);
	if (!reg_p.c) { goto copy_f_to_r; } // if moving left, use default
	set_y(0xf0); // otherwise load alternate positioning value here
copy_f_to_r:;
	set_a(reg_y); // move bowser's rear object position value to A
	reg_p.c = 0;
	add_a(ram[smb_enemy_x_position + reg_x]); // add to bowser's front object horizontal coordinate
	set_y(ram[smb_duplicate_obj_offset]); // get bowser's rear object offset
	ram[smb_enemy_x_position + reg_y] = reg_a; // store A as bowser's rear horizontal coordinate
	set_a(ram[smb_enemy_y_position + reg_x]);
	reg_p.c = 0; // add eight pixels to bowser's front object
	add_a(0x08); // vertical coordinate and store as vertical coordinate
	ram[smb_enemy_y_position + reg_y] = reg_a; // for bowser's rear
	set_a(ram[smb_enemy_state + reg_x]);
	ram[smb_enemy_state + reg_y] = reg_a; // copy enemy state directly from front to rear
	set_a(ram[smb_enemy_moving_dir + reg_x]);
	ram[smb_enemy_moving_dir + reg_y] = reg_a; // copy moving direction also
	set_a(ram[smb_object_offset]); // save enemy object offset of front to stack
	push(reg_a);
	set_x(ram[smb_duplicate_obj_offset]); // put enemy object offset of rear as current
	ram[smb_object_offset] = reg_x;
	set_a((smb_bowser)); // set bowser's enemy identifier
	ram[smb_enemy_id + reg_x] = reg_a; // store in bowser's rear object
	smb_process_bowser_half(); // do a sub here to process bowser's rear
	set_a(pull());
	ram[smb_object_offset] = reg_a; // get original enemy object offset
	set_x(reg_a);
	set_a(0x00); // nullify bowser's front/rear graphics flag
	ram[smb_bowser_gfx_flag] = reg_a;
	smb_ex_bgfx_h(); return;
}

void smb_ex_bgfx_h() {
	return; // leave!
	smb_process_bowser_half(); return;
}

void smb_process_bowser_half() {
	ram[smb_bowser_gfx_flag] = inc(ram[smb_bowser_gfx_flag]); // increment bowser's graphics flag, then run subroutines
	smb_run_retainer_obj(); // to get offscreen bits, relative position and draw bowser (finally!)
	set_a(ram[smb_enemy_state + reg_x]);
	if (!reg_p.z) { smb_ex_bgfx_h(); return; } // if either enemy object not in normal state, branch to leave
	set_a(0x0a);
	ram[smb_enemy_bound_box_ctrl + reg_x] = reg_a; // set bounding box size control
	smb_get_enemy_bound_box(); // get bounding box coordinates
	smb_player_enemy_collision(); return; // do player-to-enemy collision detection
}

void smb_set_flame_timer() {
	set_y(ram[smb_bowser_flame_timer_ctrl]); // load counter as offset
	ram[smb_bowser_flame_timer_ctrl] = inc(ram[smb_bowser_flame_timer_ctrl]); // increment
	set_a(ram[smb_bowser_flame_timer_ctrl]); // mask out all but 3 LSB
	and_a(0b00000111); // to keep in range of 0-7
	ram[smb_bowser_flame_timer_ctrl] = reg_a;
	set_a(rom[smb_flame_timer_data + reg_y]); // load value to be used then leave
	smb_ex_fl(); return;
}

void smb_ex_fl() {
	return;
	smb_proc_bowser_flame(); return;
}

void smb_proc_bowser_flame() {
	set_a(ram[smb_timer_control]); // if master timer control flag set,
	if (!reg_p.z) { goto set_gfx_f; } // skip all of this
	set_a(0x40); // load default movement force
	set_y(ram[smb_secondary_hard_mode]);
	if (reg_p.z) { goto s_flm_x; } // if secondary hard mode flag not set, use default
	set_a(0x60); // otherwise load alternate movement force to go faster
s_flm_x:;
	ram[0x0000] = reg_a; // store value here
	set_a(ram[smb_enemy_x_move_force + reg_x]);
	reg_p.c = 1; // subtract value from movement force
	sub_a(ram[0x0000]);
	ram[smb_enemy_x_move_force + reg_x] = reg_a; // save new value
	set_a(ram[smb_enemy_x_position + reg_x]);
	sub_a(0x01); // subtract one from horizontal position to move
	ram[smb_enemy_x_position + reg_x] = reg_a; // to the left
	set_a(ram[smb_enemy_page_loc + reg_x]);
	sub_a(0x00); // subtract borrow from page location
	ram[smb_enemy_page_loc + reg_x] = reg_a;
	set_y(ram[smb_bowser_flame_p_random_ofs + reg_x]); // get some value here and use as offset
	set_a(ram[smb_enemy_y_position + reg_x]); // load vertical coordinate
	cmp_a(rom[smb_flame_y_pos_data + reg_y]); // compare against coordinate data using $0417,x as offset
	if (reg_p.z) { goto set_gfx_f; } // if equal, branch and do not modify coordinate
	reg_p.c = 0;
	add_a(ram[smb_enemy_y_move_force + reg_x]); // otherwise add value here to coordinate and store
	ram[smb_enemy_y_position + reg_x] = reg_a; // as new vertical coordinate
set_gfx_f:;
	smb_relative_enemy_position(); // get new relative coordinates
	set_a(ram[smb_enemy_state + reg_x]); // if bowser's flame not in normal state,
	if (!reg_p.z) { smb_ex_fl(); return; } // branch to leave
	set_a(0x51); // otherwise, continue
	ram[0x0000] = reg_a; // write first tile number
	set_y(0x02); // load attributes without vertical flip by default
	set_a(ram[smb_frame_counter]);
	and_a(0b00000010); // invert vertical flip bit every 2 frames
	if (reg_p.z) { goto flme_at; } // if d1 not set, write default value
	set_y(0x82); // otherwise write value with vertical flip bit set
flme_at:;
	ram[0x0001] = reg_y; // set bowser's flame sprite attributes here
	set_y(ram[smb_enemy_spr_data_offset + reg_x]); // get OAM data offset
	set_x(0x00);
draw_flame_loop:;
	set_a(ram[smb_enemy_rel_y_pos]); // get Y relative coordinate of current enemy object
	ram[smb_sprite_y_position + reg_y] = reg_a; // write into Y coordinate of OAM data
	set_a(ram[0x0000]);
	ram[smb_sprite_tilenumber + reg_y] = reg_a; // write current tile number into OAM data
	ram[0x0000] = inc(ram[0x0000]); // increment tile number to draw more bowser's flame
	set_a(ram[0x0001]);
	ram[smb_sprite_attributes + reg_y] = reg_a; // write saved attributes into OAM data
	set_a(ram[smb_enemy_rel_x_pos]);
	ram[smb_sprite_x_position + reg_y] = reg_a; // write X relative coordinate of current enemy object
	reg_p.c = 0;
	add_a(0x08);
	ram[smb_enemy_rel_x_pos] = reg_a; // then add eight to it and store
	set_y(reg_y+1);
	set_y(reg_y+1);
	set_y(reg_y+1);
	set_y(reg_y+1); // increment Y four times to move onto the next OAM
	set_x(reg_x+1); // move onto the next OAM, and branch if three
	cmp_x(0x03); // have not yet been done
	if (!reg_p.c) { goto draw_flame_loop; }
	set_x(ram[smb_object_offset]); // reload original enemy offset
	smb_get_enemy_offscreen_bits(); // get offscreen information
	set_y(ram[smb_enemy_spr_data_offset + reg_x]); // get OAM data offset
	set_a(ram[smb_enemy_offscreen_bits]); // get enemy object offscreen bits
	reg_a = shr(reg_a); // move d0 to carry and result to stack
	push(reg_a);
	if (!reg_p.c) { goto m_3_f_ofs; } // branch if carry not set
	set_a(0xf8); // otherwise move sprite offscreen, this part likely
	ram[smb_sprite_y_position+0x0c + reg_y] = reg_a; // residual since flame is only made of three sprites
m_3_f_ofs:;
	set_a(pull()); // get bits from stack
	reg_a = shr(reg_a); // move d1 to carry and move bits back to stack
	push(reg_a);
	if (!reg_p.c) { goto m_2_f_ofs; } // branch if carry not set again
	set_a(0xf8); // otherwise move third sprite offscreen
	ram[smb_sprite_y_position+8 + reg_y] = reg_a;
m_2_f_ofs:;
	set_a(pull()); // get bits from stack again
	reg_a = shr(reg_a); // move d2 to carry and move bits back to stack again
	push(reg_a);
	if (!reg_p.c) { goto m_1_f_ofs; } // branch if carry not set yet again
	set_a(0xf8); // otherwise move second sprite offscreen
	ram[smb_sprite_y_position+4 + reg_y] = reg_a;
m_1_f_ofs:;
	set_a(pull()); // get bits from stack one last time
	reg_a = shr(reg_a); // move d3 to carry
	if (!reg_p.c) { goto ex_flme_d; } // branch if carry not set one last time
	set_a(0xf8);
	ram[smb_sprite_y_position + reg_y] = reg_a; // otherwise move first sprite offscreen
ex_flme_d:;
	return; // leave
	smb_run_fireworks(); return;
}

void smb_run_fireworks() {
	ram[smb_explosion_timer_counter + reg_x] = dec(ram[smb_explosion_timer_counter + reg_x]); // decrement explosion timing counter here
	if (!reg_p.z) { goto setup_expl; } // if not expired, skip this part
	set_a(0x08);
	ram[smb_explosion_timer_counter + reg_x] = reg_a; // reset counter
	ram[smb_explosion_gfx_counter + reg_x] = inc(ram[smb_explosion_gfx_counter + reg_x]); // increment explosion graphics counter
	set_a(ram[smb_explosion_gfx_counter + reg_x]);
	cmp_a(0x03); // check explosion graphics counter
	if (reg_p.c) { goto fireworks_sound_score; } // if at a certain point, branch to kill this object
setup_expl:;
	smb_relative_enemy_position(); // get relative coordinates of explosion
	set_a(ram[smb_enemy_rel_y_pos]); // copy relative coordinates
	ram[smb_fireball_rel_y_pos] = reg_a; // from the enemy object to the fireball object
	set_a(ram[smb_enemy_rel_x_pos]); // first vertical, then horizontal
	ram[smb_fireball_rel_x_pos] = reg_a;
	set_y(ram[smb_enemy_spr_data_offset + reg_x]); // get OAM data offset
	set_a(ram[smb_explosion_gfx_counter + reg_x]); // get explosion graphics counter
	smb_draw_explosion_fireworks(); // do a sub to draw the explosion then leave
	return;
fireworks_sound_score:;
	set_a(0x00); // disable enemy buffer flag
	ram[smb_enemy_flag + reg_x] = reg_a;
	set_a((smb_sfx_blast)); // play fireworks/gunfire sound
	ram[smb_square_2_sound_queue] = reg_a;
	set_a(0x05); // set part of score modifier for 500 points
	ram[smb_digit_modifier+4] = reg_a;
	smb_end_area_points(); return; // jump to award points accordingly then leave
}

void smb_run_star_flag_obj() {
	set_a(0x00); // initialize enemy frenzy buffer
	ram[smb_enemy_frenzy_buffer] = reg_a;
	set_a(ram[smb_star_flag_task_control]); // check star flag object task number here
	cmp_a(0x05); // if greater than 5, branch to exit
	if (reg_p.c) { smb_star_flag_exit(); return; }
	static void(*targets[])() = {
		smb_star_flag_exit,
		smb_game_timer_fireworks,
		smb_award_game_timer_points,
		smb_raise_flag_setoff_fworks,
		smb_delay_to_area_end
	};
	targets[reg_a](); return;
	smb_game_timer_fireworks(); return;
}

void smb_game_timer_fireworks() {
	set_y(0x05); // set default state for star flag object
	set_a(ram[smb_game_timer_display+2]); // get game timer's last digit
	cmp_a(0x01);
	if (reg_p.z) { goto set_fwc; } // if last digit of game timer set to 1, skip ahead
	set_y(0x03); // otherwise load new value for state
	cmp_a(0x03);
	if (reg_p.z) { goto set_fwc; } // if last digit of game timer set to 3, skip ahead
	set_y(0x00); // otherwise load one more potential value for state
	cmp_a(0x06);
	if (reg_p.z) { goto set_fwc; } // if last digit of game timer set to 6, skip ahead
	set_a(0xff); // otherwise set value for no fireworks
set_fwc:;
	ram[smb_fireworks_counter] = reg_a; // set fireworks counter here
	ram[smb_enemy_state + reg_x] = reg_y; // set whatever state we have in star flag object
	smb_increment_sf_task_1(); return;
}

void smb_increment_sf_task_1() {
	ram[smb_star_flag_task_control] = inc(ram[smb_star_flag_task_control]); // increment star flag object task number
	smb_star_flag_exit(); return;
}

void smb_star_flag_exit() {
	return; // leave
	smb_award_game_timer_points(); return;
}

void smb_award_game_timer_points() {
	set_a(ram[smb_game_timer_display]); // check all game timer digits for any intervals left
	or_a(ram[smb_game_timer_display+1]);
	or_a(ram[smb_game_timer_display+2]);
	if (reg_p.z) { smb_increment_sf_task_1(); return; } // if no time left on game timer at all, branch to next task
	set_a(ram[smb_frame_counter]);
	and_a(0b00000100); // check frame counter for d2 set (skip ahead
	if (reg_p.z) { goto no_t_tick; } // for four frames every four frames) branch if not set
	set_a((smb_sfx_timer_tick));
	ram[smb_square_2_sound_queue] = reg_a; // load timer tick sound
no_t_tick:;
	set_y(0x23); // set offset here to subtract from game timer's last digit
	set_a(0xff); // set adder here to $ff, or -1, to subtract one
	ram[smb_digit_modifier+5] = reg_a; // from the last digit of the game timer
	smb_digits_math_routine(); // subtract digit
	set_a(0x05); // set now to add 50 points
	ram[smb_digit_modifier+5] = reg_a; // per game timer interval subtracted
	smb_end_area_points(); return;
}

void smb_end_area_points() {
	set_y(0x0b); // load offset for mario's score by default
	set_a(ram[smb_current_player]); // check player on the screen
	if (reg_p.z) { goto elp_give; } // if mario, do not change
	set_y(0x11); // otherwise load offset for luigi's score
elp_give:;
	smb_digits_math_routine(); // award 50 points per game timer interval
	set_a(ram[smb_current_player]); // get player on the screen (or 500 points per
	reg_a = shl(reg_a); // fireworks explosion if branched here from there)
	reg_a = shl(reg_a); // shift to high nybble
	reg_a = shl(reg_a);
	reg_a = shl(reg_a);
	or_a(0b00000100); // add four to set nybble for game timer
	smb_update_number(); return; // jump to print the new score and game timer
	smb_raise_flag_setoff_fworks(); return;
}

void smb_raise_flag_setoff_fworks() {
	set_a(ram[smb_enemy_y_position + reg_x]); // check star flag's vertical position
	cmp_a(0x72); // against preset value
	if (!reg_p.c) { goto setoff_f; } // if star flag higher vertically, branch to other code
	ram[smb_enemy_y_position + reg_x] = dec(ram[smb_enemy_y_position + reg_x]); // otherwise, raise star flag by one pixel
	smb_draw_star_flag(); return; // and skip this part here
setoff_f:;
	set_a(ram[smb_fireworks_counter]); // check fireworks counter
	if (reg_p.z) { smb_draw_flag_set_timer(); return; } // if no fireworks left to go off, skip this part
	if (reg_p.n) { smb_draw_flag_set_timer(); return; } // if no fireworks set to go off, skip this part
	set_a((smb_fireworks));
	ram[smb_enemy_frenzy_buffer] = reg_a; // otherwise set fireworks object in frenzy queue
	smb_draw_star_flag(); return;
}

void smb_draw_star_flag() {
	smb_relative_enemy_position(); // get relative coordinates of star flag
	set_y(ram[smb_enemy_spr_data_offset + reg_x]); // get OAM data offset
	set_x(0x03); // do four sprites
ds_floop:;
	set_a(ram[smb_enemy_rel_y_pos]); // get relative vertical coordinate
	reg_p.c = 0;
	add_a(rom[smb_star_flag_y_pos_adder + reg_x]); // add Y coordinate adder data
	ram[smb_sprite_y_position + reg_y] = reg_a; // store as Y coordinate
	set_a(rom[smb_star_flag_tile_data + reg_x]); // get tile number
	ram[smb_sprite_tilenumber + reg_y] = reg_a; // store as tile number
	set_a(0x22); // set palette and background priority bits
	ram[smb_sprite_attributes + reg_y] = reg_a; // store as attributes
	set_a(ram[smb_enemy_rel_x_pos]); // get relative horizontal coordinate
	reg_p.c = 0;
	add_a(rom[smb_star_flag_x_pos_adder + reg_x]); // add X coordinate adder data
	ram[smb_sprite_x_position + reg_y] = reg_a; // store as X coordinate
	set_y(reg_y+1);
	set_y(reg_y+1); // increment OAM data offset four bytes
	set_y(reg_y+1); // for next sprite
	set_y(reg_y+1);
	set_x(reg_x-1); // move onto next sprite
	if (!reg_p.n) { goto ds_floop; } // do this until all sprites are done
	set_x(ram[smb_object_offset]); // get enemy object offset and leave
	return;
	smb_draw_flag_set_timer(); return;
}

void smb_draw_flag_set_timer() {
	smb_draw_star_flag(); // do sub to draw star flag
	set_a(0x06);
	ram[smb_enemy_interval_timer + reg_x] = reg_a; // set interval timer here
	smb_increment_sf_task_2(); return;
}

void smb_increment_sf_task_2() {
	ram[smb_star_flag_task_control] = inc(ram[smb_star_flag_task_control]); // move onto next task
	return;
	smb_delay_to_area_end(); return;
}

void smb_delay_to_area_end() {
	smb_draw_star_flag(); // do sub to draw star flag
	set_a(ram[smb_enemy_interval_timer + reg_x]); // if interval timer set in previous task
	if (!reg_p.z) { goto star_flag_exit_2; } // not yet expired, branch to leave
	set_a(ram[smb_event_music_buffer]); // if event music buffer empty,
	if (reg_p.z) { smb_increment_sf_task_2(); return; } // branch to increment task
star_flag_exit_2:;
	return; // otherwise leave
	smb_move_piranha_plant(); return;
}

void smb_move_piranha_plant() {
	set_a(ram[smb_enemy_state + reg_x]); // check enemy state
	if (!reg_p.z) { goto putin_pipe; } // if set at all, branch to leave
	set_a(ram[smb_enemy_frame_timer + reg_x]); // check enemy's timer here
	if (!reg_p.z) { goto putin_pipe; } // branch to end if not yet expired
	set_a(ram[smb_piranha_plant_move_flag + reg_x]); // check movement flag
	if (!reg_p.z) { goto setup_to_move_p_plant; } // if moving, skip to part ahead
	set_a(ram[smb_piranha_plant_y_speed + reg_x]); // if currently rising, branch
	if (reg_p.n) { goto reverse_plant_speed; } // to move enemy upwards out of pipe
	smb_player_enemy_diff(); // get horizontal difference between player and
	if (!reg_p.n) { goto chk_player_near_pipe; } // piranha plant, and branch if enemy to right of player
	set_a(ram[0x0000]); // otherwise get saved horizontal difference
	eor_a(0xff);
	reg_p.c = 0; // and change to two's compliment
	add_a(0x01);
	ram[0x0000] = reg_a; // save as new horizontal difference
chk_player_near_pipe:;
	set_a(ram[0x0000]); // get saved horizontal difference
	cmp_a(0x21);
	if (!reg_p.c) { goto putin_pipe; } // if player within a certain distance, branch to leave
reverse_plant_speed:;
	set_a(ram[smb_piranha_plant_y_speed + reg_x]); // get vertical speed
	eor_a(0xff);
	reg_p.c = 0; // change to two's compliment
	add_a(0x01);
	ram[smb_piranha_plant_y_speed + reg_x] = reg_a; // save as new vertical speed
	ram[smb_piranha_plant_move_flag + reg_x] = inc(ram[smb_piranha_plant_move_flag + reg_x]); // increment to set movement flag
setup_to_move_p_plant:;
	set_a(ram[smb_piranha_plant_down_y_pos + reg_x]); // get original vertical coordinate (lowest point)
	set_y(ram[smb_piranha_plant_y_speed + reg_x]); // get vertical speed
	if (!reg_p.n) { goto rise_fall_piranha_plant; } // branch if moving downwards
	set_a(ram[smb_piranha_plant_up_y_pos + reg_x]); // otherwise get other vertical coordinate (highest point)
rise_fall_piranha_plant:;
	ram[0x0000] = reg_a; // save vertical coordinate here
	set_a(ram[smb_frame_counter]); // get frame counter
	reg_a = shr(reg_a);
	if (!reg_p.c) { goto putin_pipe; } // branch to leave if d0 set (execute code every other frame)
	set_a(ram[smb_timer_control]); // get master timer control
	if (!reg_p.z) { goto putin_pipe; } // branch to leave if set (likely not necessary)
	set_a(ram[smb_enemy_y_position + reg_x]); // get current vertical coordinate
	reg_p.c = 0;
	add_a(ram[smb_piranha_plant_y_speed + reg_x]); // add vertical speed to move up or down
	ram[smb_enemy_y_position + reg_x] = reg_a; // save as new vertical coordinate
	cmp_a(ram[0x0000]); // compare against low or high coordinate
	if (!reg_p.z) { goto putin_pipe; } // branch to leave if not yet reached
	set_a(0x00);
	ram[smb_piranha_plant_move_flag + reg_x] = reg_a; // otherwise clear movement flag
	set_a(0x40);
	ram[smb_enemy_frame_timer + reg_x] = reg_a; // set timer to delay piranha plant movement
putin_pipe:;
	set_a(0b00100000); // set background priority bit in sprite
	ram[smb_enemy_spr_attrib + reg_x] = reg_a; // attributes to give illusion of being inside pipe
	return; // then leave
	smb_firebar_spin(); return;
}

void smb_firebar_spin() {
	ram[0x0007] = reg_a; // save spinning speed here
	set_a(ram[smb_firebar_spin_direction + reg_x]); // check spinning direction
	if (!reg_p.z) { goto spin_counter_clockwise; } // if moving counter-clockwise, branch to other part
	set_y(0x18); // possibly residual ldy
	set_a(ram[smb_firebar_spin_state_low + reg_x]);
	reg_p.c = 0; // add spinning speed to what would normally be
	add_a(ram[0x0007]); // the horizontal speed
	ram[smb_firebar_spin_state_low + reg_x] = reg_a;
	set_a(ram[smb_firebar_spin_state_high + reg_x]); // add carry to what would normally be the vertical speed
	add_a(0x00);
	return;
spin_counter_clockwise:;
	set_y(0x08); // possibly residual ldy
	set_a(ram[smb_firebar_spin_state_low + reg_x]);
	reg_p.c = 1; // subtract spinning speed to what would normally be
	sub_a(ram[0x0007]); // the horizontal speed
	ram[smb_firebar_spin_state_low + reg_x] = reg_a;
	set_a(ram[smb_firebar_spin_state_high + reg_x]); // add carry to what would normally be the vertical speed
	sub_a(0x00);
	return;
	smb_balance_platform(); return;
}

void smb_balance_platform() {
	set_a(ram[smb_enemy_y_high_pos + reg_x]); // check high byte of vertical position
	cmp_a(0x03);
	if (!reg_p.z) { goto do_b_pl; }
	smb_erase_enemy_object(); return; // if far below screen, kill the object
do_b_pl:;
	set_a(ram[smb_enemy_state + reg_x]); // get object's state (set to $ff or other platform offset)
	if (!reg_p.n) { goto check_bal_platform; } // if doing other balance platform, branch to leave
	return;
check_bal_platform:;
	set_y(reg_a); // save offset from state as Y
	set_a(ram[smb_platform_collision_flag + reg_x]); // get collision flag of platform
	ram[0x0000] = reg_a; // store here
	set_a(ram[smb_enemy_moving_dir + reg_x]); // get moving direction
	if (reg_p.z) { goto chk_for_fall; }
	smb_platform_fall(); return; // if set, jump here
chk_for_fall:;
	set_a(0x2d); // check if platform is above a certain point
	cmp_a(ram[smb_enemy_y_position + reg_x]);
	if (!reg_p.c) { goto chk_other_for_fall; } // if not, branch elsewhere
	cmp_y(ram[0x0000]); // if collision flag is set to same value as
	if (reg_p.z) { goto make_platform_fall; } // enemy state, branch to make platforms fall
	reg_p.c = 0;
	add_a(0x02); // otherwise add 2 pixels to vertical position
	ram[smb_enemy_y_position + reg_x] = reg_a; // of current platform and branch elsewhere
	smb_stop_platforms(); return; // to make platforms stop
make_platform_fall:;
	smb_init_platform_fall(); return; // make platforms fall
chk_other_for_fall:;
	cmp_a(ram[smb_enemy_y_position + reg_y]); // check if other platform is above a certain point
	if (!reg_p.c) { goto chk_to_move_bal_plat; } // if not, branch elsewhere
	cmp_x(ram[0x0000]); // if collision flag is set to same value as
	if (reg_p.z) { goto make_platform_fall; } // enemy state, branch to make platforms fall
	reg_p.c = 0;
	add_a(0x02); // otherwise add 2 pixels to vertical position
	ram[smb_enemy_y_position + reg_y] = reg_a; // of other platform and branch elsewhere
	smb_stop_platforms(); return; // jump to stop movement and do not return
chk_to_move_bal_plat:;
	set_a(ram[smb_enemy_y_position + reg_x]); // save vertical position to stack
	push(reg_a);
	set_a(ram[smb_platform_collision_flag + reg_x]); // get collision flag
	if (!reg_p.n) { goto col_flg; } // branch if collision
	set_a(ram[smb_enemy_y_move_force + reg_x]);
	reg_p.c = 0; // add $05 to contents of moveforce, whatever they be
	add_a(0x05);
	ram[0x0000] = reg_a; // store here
	set_a(ram[smb_enemy_y_speed + reg_x]);
	add_a(0x00); // add carry to vertical speed
	if (reg_p.n) { goto plat_dn; } // branch if moving downwards
	if (!reg_p.z) { goto plat_up; } // branch elsewhere if moving upwards
	set_a(ram[0x0000]);
	cmp_a(0x0b); // check if there's still a little force left
	if (!reg_p.c) { goto plat_st; } // if not enough, branch to stop movement
	if (reg_p.c) { goto plat_up; } // otherwise keep branch to move upwards
col_flg:;
	cmp_a(ram[smb_object_offset]); // if collision flag matches
	if (reg_p.z) { goto plat_dn; } // current enemy object offset, branch
plat_up:;
	smb_move_platform_up(); // do a sub to move upwards
	goto do_other_platform; // jump ahead to remaining code
plat_st:;
	smb_stop_platforms(); // do a sub to stop movement
	goto do_other_platform; // jump ahead to remaining code
plat_dn:;
	smb_move_platform_down(); // do a sub to move downwards
do_other_platform:;
	set_y(ram[smb_enemy_state + reg_x]); // get offset of other platform
	set_a(pull()); // get old vertical coordinate from stack
	reg_p.c = 1;
	sub_a(ram[smb_enemy_y_position + reg_x]); // get difference of old vs. new coordinate
	reg_p.c = 0;
	add_a(ram[smb_enemy_y_position + reg_y]); // add difference to vertical coordinate of other
	ram[smb_enemy_y_position + reg_y] = reg_a; // platform to move it in the opposite direction
	set_a(ram[smb_platform_collision_flag + reg_x]); // if no collision, skip this part here
	if (reg_p.n) { goto draw_erase_rope; }
	set_x(reg_a); // put offset which collision occurred here
	smb_position_player_on_v_plat(); // and use it to position player accordingly
draw_erase_rope:;
	set_y(ram[smb_object_offset]); // get enemy object offset
	set_a(ram[smb_enemy_y_speed + reg_y]); // check to see if current platform is
	or_a(ram[smb_enemy_y_move_force + reg_y]); // moving at all
	if (reg_p.z) { goto exit_rp; } // if not, skip all of this and branch to leave
	set_x(ram[smb_vram_buffer_1_offset]); // get vram buffer offset
	cmp_x(0x20); // if offset beyond a certain point, go ahead
	if (reg_p.c) { goto exit_rp; } // and skip this, branch to leave
	set_a(ram[smb_enemy_y_speed + reg_y]);
	push(reg_a); // save two copies of vertical speed to stack
	push(reg_a);
	smb_setup_platform_rope(); // do a sub to figure out where to put new bg tiles
	set_a(ram[0x0001]); // write name table address to vram buffer
	ram[smb_vram_buffer_1 + reg_x] = reg_a; // first the high byte, then the low
	set_a(ram[0x0000]);
	ram[smb_vram_buffer_1+1 + reg_x] = reg_a;
	set_a(0x02); // set length for 2 bytes
	ram[smb_vram_buffer_1+2 + reg_x] = reg_a;
	set_a(ram[smb_enemy_y_speed + reg_y]); // if platform moving upwards, branch
	if (reg_p.n) { goto erase_r_1; } // to do something else
	set_a(0xa2);
	ram[smb_vram_buffer_1+3 + reg_x] = reg_a; // otherwise put tile numbers for left
	set_a(0xa3); // and right sides of rope in vram buffer
	ram[smb_vram_buffer_1+4 + reg_x] = reg_a;
	goto other_rope; // jump to skip this part
erase_r_1:;
	set_a(0x24); // put blank tiles in vram buffer
	ram[smb_vram_buffer_1+3 + reg_x] = reg_a; // to erase rope
	ram[smb_vram_buffer_1+4 + reg_x] = reg_a;
other_rope:;
	set_a(ram[smb_enemy_state + reg_y]); // get offset of other platform from state
	set_y(reg_a); // use as Y here
	set_a(pull()); // pull second copy of vertical speed from stack
	eor_a(0xff); // invert bits to reverse speed
	smb_setup_platform_rope(); // do sub again to figure out where to put bg tiles
	set_a(ram[0x0001]); // write name table address to vram buffer
	ram[smb_vram_buffer_1+5 + reg_x] = reg_a; // this time we're doing putting tiles for
	set_a(ram[0x0000]); // the other platform
	ram[smb_vram_buffer_1+6 + reg_x] = reg_a;
	set_a(0x02);
	ram[smb_vram_buffer_1+7 + reg_x] = reg_a; // set length again for 2 bytes
	set_a(pull()); // pull first copy of vertical speed from stack
	if (!reg_p.n) { goto erase_r_2; } // if moving upwards (note inversion earlier), skip this
	set_a(0xa2);
	ram[smb_vram_buffer_1+8 + reg_x] = reg_a; // otherwise put tile numbers for left
	set_a(0xa3); // and right sides of rope in vram
	ram[smb_vram_buffer_1+0x09 + reg_x] = reg_a; // transfer buffer
	goto end_rp; // jump to skip this part
erase_r_2:;
	set_a(0x24); // put blank tiles in vram buffer
	ram[smb_vram_buffer_1+8 + reg_x] = reg_a; // to erase rope
	ram[smb_vram_buffer_1+0x09 + reg_x] = reg_a;
end_rp:;
	set_a(0x00); // put null terminator at the end
	ram[smb_vram_buffer_1+0x0a + reg_x] = reg_a;
	set_a(ram[smb_vram_buffer_1_offset]); // add ten bytes to the vram buffer offset
	reg_p.c = 0; // and store
	add_a(0x0a);
	ram[smb_vram_buffer_1_offset] = reg_a;
exit_rp:;
	set_x(ram[smb_object_offset]); // get enemy object buffer offset and leave
	return;
	smb_setup_platform_rope(); return;
}

void smb_setup_platform_rope() {
	push(reg_a); // save second/third copy to stack
	set_a(ram[smb_enemy_x_position + reg_y]); // get horizontal coordinate
	reg_p.c = 0;
	add_a(0x08); // add eight pixels
	set_x(ram[smb_secondary_hard_mode]); // if secondary hard mode flag set,
	if (!reg_p.z) { goto get_l_rp; } // use coordinate as-is
	reg_p.c = 0;
	add_a(0x10); // otherwise add sixteen more pixels
get_l_rp:;
	push(reg_a); // save modified horizontal coordinate to stack
	set_a(ram[smb_enemy_page_loc + reg_y]);
	add_a(0x00); // add carry to page location
	ram[0x0002] = reg_a; // and save here
	set_a(pull()); // pull modified horizontal coordinate
	and_a(0b11110000); // from the stack, mask out low nybble
	reg_a = shr(reg_a); // and shift three bits to the right
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	ram[0x0000] = reg_a; // store result here as part of name table low byte
	set_x(ram[smb_enemy_y_position + reg_y]); // get vertical coordinate
	set_a(pull()); // get second/third copy of vertical speed from stack
	if (!reg_p.n) { goto get_h_rp; } // skip this part if moving downwards or not at all
	set_a(reg_x);
	reg_p.c = 0;
	add_a(0x08); // add eight to vertical coordinate and
	set_x(reg_a); // save as X
get_h_rp:;
	set_a(reg_x); // move vertical coordinate to A
	set_x(ram[smb_vram_buffer_1_offset]); // get vram buffer offset
	reg_a = shl(reg_a);
	reg_a = rol(reg_a); // rotate d7 to d0 and d6 into carry
	push(reg_a); // save modified vertical coordinate to stack
	reg_a = rol(reg_a); // rotate carry to d0, thus d7 and d6 are at 2 LSB
	and_a(0b00000011); // mask out all bits but d7 and d6, then set
	or_a(0b00100000); // d5 to get appropriate high byte of name table
	ram[0x0001] = reg_a; // address, then store
	set_a(ram[0x0002]); // get saved page location from earlier
	and_a(0x01); // mask out all but LSB
	reg_a = shl(reg_a);
	reg_a = shl(reg_a); // shift twice to the left and save with the
	or_a(ram[0x0001]); // rest of the bits of the high byte, to get
	ram[0x0001] = reg_a; // the proper name table and the right place on it
	set_a(pull()); // get modified vertical coordinate from stack
	and_a(0b11100000); // mask out low nybble and LSB of high nybble
	reg_p.c = 0;
	add_a(ram[0x0000]); // add to horizontal part saved here
	ram[0x0000] = reg_a; // save as name table low byte
	set_a(ram[smb_enemy_y_position + reg_y]);
	cmp_a(0xe8); // if vertical position not below the
	if (!reg_p.c) { goto ex_p_rp; } // bottom of the screen, we're done, branch to leave
	set_a(ram[0x0000]);
	and_a(0b10111111); // mask out d6 of low byte of name table address
	ram[0x0000] = reg_a;
ex_p_rp:;
	return; // leave!
	smb_init_platform_fall(); return;
}

void smb_init_platform_fall() {
	set_a(reg_y); // move offset of other platform from Y to X
	set_x(reg_a);
	smb_get_enemy_offscreen_bits(); // get offscreen bits
	set_a(0x06);
	smb_setup_floatey_number(); // award 1000 points to player
	set_a(ram[smb_player_rel_x_pos]);
	ram[smb_floatey_num_x_pos + reg_x] = reg_a; // put floatey number coordinates where player is
	set_a(ram[smb_player_y_position]);
	ram[smb_floatey_num_y_pos + reg_x] = reg_a;
	set_a(0x01); // set moving direction as flag for
	ram[smb_enemy_moving_dir + reg_x] = reg_a; // falling platforms
	smb_stop_platforms(); return;
}

void smb_stop_platforms() {
	smb_init_v_stf(); // initialize vertical speed and low byte
	ram[smb_enemy_y_speed + reg_y] = reg_a; // for both platforms and leave
	ram[smb_enemy_y_move_force + reg_y] = reg_a;
	return;
	smb_platform_fall(); return;
}

void smb_platform_fall() {
	set_a(reg_y); // save offset for other platform to stack
	push(reg_a);
	smb_move_falling_platform(); // make current platform fall
	set_a(pull());
	set_x(reg_a); // pull offset from stack and save to X
	smb_move_falling_platform(); // make other platform fall
	set_x(ram[smb_object_offset]);
	set_a(ram[smb_platform_collision_flag + reg_x]); // if player not standing on either platform,
	if (reg_p.n) { goto ex_pf; } // skip this part
	set_x(reg_a); // transfer collision flag offset as offset to X
	smb_position_player_on_v_plat(); // and position player appropriately
ex_pf:;
	set_x(ram[smb_object_offset]); // get enemy object buffer offset and leave
	return;
	smb_y_moving_platform(); return;
}

void smb_y_moving_platform() {
	set_a(ram[smb_enemy_y_speed + reg_x]); // if platform moving up or down, skip ahead to
	or_a(ram[smb_enemy_y_move_force + reg_x]); // check on other position
	if (!reg_p.z) { goto chk_y_center_pos; }
	ram[smb_enemy_ymf_dummy + reg_x] = reg_a; // initialize dummy variable
	set_a(ram[smb_enemy_y_position + reg_x]);
	cmp_a(ram[smb_y_platform_top_y_pos + reg_x]); // if current vertical position => top position, branch
	if (reg_p.c) { goto chk_y_center_pos; } // ahead of all this
	set_a(ram[smb_frame_counter]);
	and_a(0b00000111); // check for every eighth frame
	if (!reg_p.z) { goto skip_iy; }
	ram[smb_enemy_y_position + reg_x] = inc(ram[smb_enemy_y_position + reg_x]); // increase vertical position every eighth frame
skip_iy:;
	smb_chk_yp_collision(); return; // skip ahead to last part
chk_y_center_pos:;
	set_a(ram[smb_enemy_y_position + reg_x]); // if current vertical position < central position, branch
	cmp_a(ram[smb_y_platform_center_y_pos + reg_x]); // to slow ascent/move downwards
	if (!reg_p.c) { goto ym_down; }
	smb_move_platform_up(); // otherwise start slowing descent/moving upwards
	smb_chk_yp_collision(); return;
ym_down:;
	smb_move_platform_down(); // start slowing ascent/moving downwards
	smb_chk_yp_collision(); return;
}

void smb_chk_yp_collision() {
	set_a(ram[smb_hammer_throwing_timer + reg_x]); // if collision flag not set here, branch
	if (reg_p.n) { goto ex_y_pl; } // to leave
	smb_position_player_on_v_plat(); // otherwise position player appropriately
ex_y_pl:;
	return; // leave
	smb_x_moving_platform(); return;
}

void smb_x_moving_platform() {
	set_a(0x0e); // load preset maximum value for secondary counter
	smb_x_move_cntr_platform(); // do a sub to increment counters for movement
	smb_move_with_xm_cntrs(); // do a sub to move platform accordingly, and return value
	set_a(ram[smb_platform_collision_flag + reg_x]); // if no collision with player,
	if (reg_p.n) { smb_ex_xmp(); return; } // branch ahead to leave
	smb_position_player_on_h_plat(); return;
}

void smb_position_player_on_h_plat() {
	set_a(ram[smb_player_x_position]);
	reg_p.c = 0; // add saved value from second subroutine to
	add_a(ram[0x0000]); // current player's position to position
	ram[smb_player_x_position] = reg_a; // player accordingly in horizontal position
	set_a(ram[smb_player_page_loc]); // get player's page location
	set_y(ram[0x0000]); // check to see if saved value here is positive or negative
	if (reg_p.n) { goto pph_subt; } // if negative, branch to subtract
	add_a(0x00); // otherwise add carry to page location
	goto set_p_var; // jump to skip subtraction
pph_subt:;
	sub_a(0x00); // subtract borrow from page location
set_p_var:;
	ram[smb_player_page_loc] = reg_a; // save result to player's page location
	ram[smb_platform_x_scroll] = reg_y; // put saved value from second sub here to be used later
	smb_position_player_on_v_plat(); // position player vertically and appropriately
	smb_ex_xmp(); return;
}

void smb_ex_xmp() {
	return; // and we are done here
	smb_drop_platform(); return;
}

void smb_drop_platform() {
	set_a(ram[smb_platform_collision_flag + reg_x]); // if no collision between platform and player
	if (reg_p.n) { goto ex_d_pl; } // occurred, just leave without moving anything
	smb_move_drop_platform(); // otherwise do a sub to move platform down very quickly
	smb_position_player_on_v_plat(); // do a sub to position player appropriately
ex_d_pl:;
	return; // leave
	smb_right_platform(); return;
}

void smb_right_platform() {
	smb_move_enemy_horizontally(); // move platform with current horizontal speed, if any
	ram[0x0000] = reg_a; // store saved value here (residual code)
	set_a(ram[smb_platform_collision_flag + reg_x]); // check collision flag, if no collision between player
	if (reg_p.n) { goto ex_r_pl; } // and platform, branch ahead, leave speed unaltered
	set_a(0x10);
	ram[smb_enemy_x_speed + reg_x] = reg_a; // otherwise set new speed (gets moving if motionless)
	smb_position_player_on_h_plat(); // use saved value from earlier sub to position player
ex_r_pl:;
	return; // then leave
	smb_move_large_lift_plat(); return;
}

void smb_move_large_lift_plat() {
	smb_move_lift_platforms(); // execute common to all large and small lift platforms
	smb_chk_yp_collision(); return; // branch to position player correctly
	smb_move_small_platform(); return;
}

void smb_move_small_platform() {
	smb_move_lift_platforms(); // execute common to all large and small lift platforms
	smb_chk_small_plat_collision(); return; // branch to position player correctly
	smb_move_lift_platforms(); return;
}

void smb_move_lift_platforms() {
	set_a(ram[smb_timer_control]); // if master timer control set, skip all of this
	if (!reg_p.z) { smb_ex_lift_p(); return; } // and branch to leave
	set_a(ram[smb_enemy_ymf_dummy + reg_x]);
	reg_p.c = 0; // add contents of movement amount to whatever's here
	add_a(ram[smb_enemy_y_move_force + reg_x]);
	ram[smb_enemy_ymf_dummy + reg_x] = reg_a;
	set_a(ram[smb_enemy_y_position + reg_x]); // add whatever vertical speed is set to current
	add_a(ram[smb_enemy_y_speed + reg_x]); // vertical position plus carry to move up or down
	ram[smb_enemy_y_position + reg_x] = reg_a; // and then leave
	return;
	smb_chk_small_plat_collision(); return;
}

void smb_chk_small_plat_collision() {
	set_a(ram[smb_platform_collision_flag + reg_x]); // get bounding box counter saved in collision flag
	if (reg_p.z) { smb_ex_lift_p(); return; } // if none found, leave player position alone
	smb_position_player_on_s_plat(); // use to position player correctly
	smb_ex_lift_p(); return;
}

void smb_ex_lift_p() {
	return; // then leave
	smb_offscreen_bounds_check(); return;
}

void smb_offscreen_bounds_check() {
	set_a(ram[smb_enemy_id + reg_x]); // check for cheep-cheep object
	cmp_a((smb_flying_cheep_cheep)); // branch to leave if found
	if (reg_p.z) { goto ex_scrn_bd; }
	set_a(ram[smb_screen_left_x_pos]); // get horizontal coordinate for left side of screen
	set_y(ram[smb_enemy_id + reg_x]);
	cmp_y((smb_hammer_bro)); // check for hammer bro object
	if (reg_p.z) { goto limit_b; }
	cmp_y((smb_piranha_plant)); // check for piranha plant object
	if (!reg_p.z) { goto extend_lb; } // these two will be erased sooner than others if too far left
limit_b:;
	add_a((56)); // add 56 pixels to coordinate if hammer bro or piranha plant
extend_lb:;
	sub_a((72)); // subtract 72 pixels regardless of enemy object
	ram[0x0001] = reg_a; // store result here
	set_a(ram[smb_screen_left_page_loc]);
	sub_a(0x00); // subtract borrow from page location of left side
	ram[0x0000] = reg_a; // store result here
	set_a(ram[smb_screen_right_x_pos]); // add 72 pixels to the right side horizontal coordinate
	add_a((72));
	ram[0x0003] = reg_a; // store result here
	set_a(ram[smb_screen_right_page_loc]);
	add_a(0x00); // then add the carry to the page location
	ram[0x0002] = reg_a; // and store result here
	set_a(ram[smb_enemy_x_position + reg_x]); // compare horizontal coordinate of the enemy object
	cmp_a(ram[0x0001]); // to modified horizontal left edge coordinate to get carry
	set_a(ram[smb_enemy_page_loc + reg_x]);
	sub_a(ram[0x0000]); // then subtract it from the page coordinate of the enemy object
	if (reg_p.n) { goto too_far; } // if enemy object is too far left, branch to erase it
	set_a(ram[smb_enemy_x_position + reg_x]); // compare horizontal coordinate of the enemy object
	cmp_a(ram[0x0003]); // to modified horizontal right edge coordinate to get carry
	set_a(ram[smb_enemy_page_loc + reg_x]);
	sub_a(ram[0x0002]); // then subtract it from the page coordinate of the enemy object
	if (reg_p.n) { goto ex_scrn_bd; } // if enemy object is on the screen, leave, do not erase enemy
	set_a(ram[smb_enemy_state + reg_x]); // if at this point, enemy is offscreen to the right, so check
	cmp_a((smb_hammer_bro)); // if in state used by spiny's egg, do not erase
	if (reg_p.z) { goto ex_scrn_bd; }
	cmp_y((smb_piranha_plant)); // if piranha plant, do not erase
	if (reg_p.z) { goto ex_scrn_bd; }
	cmp_y((smb_flagpole_flag_object)); // if flagpole flag, do not erase
	if (reg_p.z) { goto ex_scrn_bd; }
	cmp_y((smb_star_flag_object)); // if star flag, do not erase
	if (reg_p.z) { goto ex_scrn_bd; }
	cmp_y((smb_jumpspring_object)); // if jumpspring, do not erase
	if (reg_p.z) { goto ex_scrn_bd; } // erase all others too far to the right
too_far:;
	smb_erase_enemy_object(); // erase object if necessary
ex_scrn_bd:;
	return; // leave
}

void smb_fireball_enemy_collision() {
	set_a(ram[smb_fireball_state + reg_x]); // check to see if fireball state is set at all
	if (reg_p.z) { goto exit_f_ball_enemy; } // branch to leave if not
	reg_a = shl(reg_a);
	if (reg_p.c) { goto exit_f_ball_enemy; } // branch to leave also if d7 in state is set
	set_a(ram[smb_frame_counter]);
	reg_a = shr(reg_a); // get LSB of frame counter
	if (reg_p.c) { goto exit_f_ball_enemy; } // branch to leave if set (do routine every other frame)
	set_a(reg_x);
	reg_a = shl(reg_a); // multiply fireball offset by four
	reg_a = shl(reg_a);
	reg_p.c = 0;
	add_a(0x1c); // then add $1c or 28 bytes to it
	set_y(reg_a); // to use fireball's bounding box coordinates
	set_x(0x04);
fireball_enemy_cd_loop:;
	ram[0x0001] = reg_x; // store enemy object offset here
	set_a(reg_y);
	push(reg_a); // push fireball offset to the stack
	set_a(ram[smb_enemy_state + reg_x]);
	and_a(0b00100000); // check to see if d5 is set in enemy state
	if (!reg_p.z) { goto no_f_to_e_col; } // if so, skip to next enemy slot
	set_a(ram[smb_enemy_flag + reg_x]); // check to see if buffer flag is set
	if (reg_p.z) { goto no_f_to_e_col; } // if not, skip to next enemy slot
	set_a(ram[smb_enemy_id + reg_x]); // check enemy identifier
	cmp_a(0x24);
	if (!reg_p.c) { goto goomba_die; } // if < $24, branch to check further
	cmp_a(0x2b);
	if (!reg_p.c) { goto no_f_to_e_col; } // if in range $24-$2a, skip to next enemy slot
goomba_die:;
	cmp_a((smb_goomba)); // check for goomba identifier
	if (!reg_p.z) { goto no_goomba; } // if not found, continue with code
	set_a(ram[smb_enemy_state + reg_x]); // otherwise check for defeated state
	cmp_a(0x02); // if stomped or otherwise defeated,
	if (reg_p.c) { goto no_f_to_e_col; } // skip to next enemy slot
no_goomba:;
	set_a(ram[smb_enemy_offscr_bits_masked + reg_x]); // if any masked offscreen bits set,
	if (!reg_p.z) { goto no_f_to_e_col; } // skip to next enemy slot
	set_a(reg_x);
	reg_a = shl(reg_a); // otherwise multiply enemy offset by four
	reg_a = shl(reg_a);
	reg_p.c = 0;
	add_a(0x04); // add 4 bytes to it
	set_x(reg_a); // to use enemy's bounding box coordinates
	smb_spr_object_collision_core(); // do fireball-to-enemy collision detection
	set_x(ram[smb_object_offset]); // return fireball's original offset
	if (!reg_p.c) { goto no_f_to_e_col; } // if carry clear, no collision, thus do next enemy slot
	set_a(0b10000000);
	ram[smb_fireball_state + reg_x] = reg_a; // set d7 in enemy state
	set_x(ram[0x0001]); // get enemy offset
	smb_handle_enemy_f_ball_col(); // jump to handle fireball to enemy collision
no_f_to_e_col:;
	set_a(pull()); // pull fireball offset from stack
	set_y(reg_a); // put it in Y
	set_x(ram[0x0001]); // get enemy object offset
	set_x(reg_x-1); // decrement it
	if (!reg_p.n) { goto fireball_enemy_cd_loop; } // loop back until collision detection done on all enemies
exit_f_ball_enemy:;
	set_x(ram[smb_object_offset]); // get original fireball offset and leave
	return;
}

void smb_handle_enemy_f_ball_col() {
	smb_relative_enemy_position(); // get relative coordinate of enemy
	set_x(ram[0x0001]); // get current enemy object offset
	set_a(ram[smb_enemy_flag + reg_x]); // check buffer flag for d7 set
	if (!reg_p.n) { goto chk_buzzy_beetle; } // branch if not set to continue
	and_a(0b00001111); // otherwise mask out high nybble and
	set_x(reg_a); // use low nybble as enemy offset
	set_a(ram[smb_enemy_id + reg_x]);
	cmp_a((smb_bowser)); // check enemy identifier for bowser
	if (reg_p.z) { goto hurt_bowser; } // branch if found
	set_x(ram[0x0001]); // otherwise retrieve current enemy offset
chk_buzzy_beetle:;
	set_a(ram[smb_enemy_id + reg_x]);
	cmp_a((smb_buzzy_beetle)); // check for buzzy beetle
	if (reg_p.z) { smb_ex_hcf(); return; } // branch if found to leave (buzzy beetles fireproof)
	cmp_a((smb_bowser)); // check for bowser one more time (necessary if d7 of flag was clear)
	if (!reg_p.z) { goto chk_other_enemies; } // if not found, branch to check other enemies
hurt_bowser:;
	ram[smb_bowser_hit_points] = dec(ram[smb_bowser_hit_points]); // decrement bowser's hit points
	if (!reg_p.z) { smb_ex_hcf(); return; } // if bowser still has hit points, branch to leave
	smb_init_v_stf(); // otherwise do sub to init vertical speed and movement force
	ram[smb_enemy_x_speed + reg_x] = reg_a; // initialize horizontal speed
	ram[smb_enemy_frenzy_buffer] = reg_a; // init enemy frenzy buffer
	set_a(0xfe);
	ram[smb_enemy_y_speed + reg_x] = reg_a; // set vertical speed to make defeated bowser jump a little
	set_y(ram[smb_world_number]); // use world number as offset
	set_a(rom[smb_bowser_identities + reg_y]); // get enemy identifier to replace bowser with
	ram[smb_enemy_id + reg_x] = reg_a; // set as new enemy identifier
	set_a(0x20); // set A to use starting value for state
	cmp_y(0x03); // check to see if using offset of 3 or more
	if (reg_p.c) { goto set_db_ste; } // branch if so
	or_a(0x03); // otherwise add 3 to enemy state
set_db_ste:;
	ram[smb_enemy_state + reg_x] = reg_a; // set defeated enemy state
	set_a((smb_sfx_bowser_fall));
	ram[smb_square_2_sound_queue] = reg_a; // load bowser defeat sound
	set_x(ram[0x0001]); // get enemy offset
	set_a(0x09); // award 5000 points to player for defeating bowser
	if (!reg_p.z) { smb_enemy_smack_core(); return; } // unconditional branch to award points
chk_other_enemies:;
	cmp_a((smb_bullet_bill_frenzy_var));
	if (reg_p.z) { smb_ex_hcf(); return; } // branch to leave if bullet bill (frenzy variant)
	cmp_a((smb_podoboo));
	if (reg_p.z) { smb_ex_hcf(); return; } // branch to leave if podoboo
	cmp_a(0x15);
	if (reg_p.c) { smb_ex_hcf(); return; } // branch to leave if identifier => $15
	smb_shell_or_block_defeat(); return;
}

void smb_shell_or_block_defeat() {
	set_a(ram[smb_enemy_id + reg_x]); // check for piranha plant
	cmp_a((smb_piranha_plant));
	if (!reg_p.z) { goto stn_e; } // branch if not found
	set_a(ram[smb_enemy_y_position + reg_x]);
	add_a(0x18); // add 24 pixels to enemy object's vertical position
	ram[smb_enemy_y_position + reg_x] = reg_a;
stn_e:;
	smb_chk_to_stun_enemies(); // do yet another sub
	set_a(ram[smb_enemy_state + reg_x]);
	and_a(0b00011111); // mask out 2 MSB of enemy object's state
	or_a(0b00100000); // set d5 to defeat enemy and save as new state
	ram[smb_enemy_state + reg_x] = reg_a;
	set_a(0x02); // award 200 points by default
	set_y(ram[smb_enemy_id + reg_x]); // check for hammer bro
	cmp_y((smb_hammer_bro));
	if (!reg_p.z) { goto goomba_points; } // branch if not found
	set_a(0x06); // award 1000 points for hammer bro
goomba_points:;
	cmp_y((smb_goomba)); // check for goomba
	if (!reg_p.z) { smb_enemy_smack_core(); return; } // branch if not found
	set_a(0x01); // award 100 points for goomba
	smb_enemy_smack_core(); return;
}

void smb_enemy_smack_core() {
	smb_setup_floatey_number(); // update necessary score variables
	set_a((smb_sfx_enemy_smack)); // play smack enemy sound
	ram[smb_square_1_sound_queue] = reg_a;
	smb_ex_hcf(); return;
}

void smb_ex_hcf() {
	return; // and now let's leave
	smb_player_hammer_collision(); return;
}

void smb_player_hammer_collision() {
	set_a(ram[smb_frame_counter]); // get frame counter
	reg_a = shr(reg_a); // shift d0 into carry
	if (!reg_p.c) { goto ex_phc; } // branch to leave if d0 not set to execute every other frame
	set_a(ram[smb_timer_control]); // if either master timer control
	or_a(ram[smb_misc_offscreen_bits]); // or any offscreen bits for hammer are set,
	if (!reg_p.z) { goto ex_phc; } // branch to leave
	set_a(reg_x);
	reg_a = shl(reg_a); // multiply misc object offset by four
	reg_a = shl(reg_a);
	reg_p.c = 0;
	add_a(0x24); // add 36 or $24 bytes to get proper offset
	set_y(reg_a); // for misc object bounding box coordinates
	smb_player_collision_core(); // do player-to-hammer collision detection
	set_x(ram[smb_object_offset]); // get misc object offset
	if (!reg_p.c) { goto cl_h_col; } // if no collision, then branch
	set_a(ram[smb_misc_collision_flag + reg_x]); // otherwise read collision flag
	if (!reg_p.z) { goto ex_phc; } // if collision flag already set, branch to leave
	set_a(0x01);
	ram[smb_misc_collision_flag + reg_x] = reg_a; // otherwise set collision flag now
	set_a(ram[smb_misc_x_speed + reg_x]);
	eor_a(0xff); // get two's compliment of
	reg_p.c = 0; // hammer's horizontal speed
	add_a(0x01);
	ram[smb_misc_x_speed + reg_x] = reg_a; // set to send hammer flying the opposite direction
	set_a(ram[smb_star_invincible_timer]); // if star mario invincibility timer set,
	if (!reg_p.z) { goto ex_phc; } // branch to leave
	smb_injure_player(); return; // otherwise jump to hurt player, do not return
cl_h_col:;
	set_a(0x00); // clear collision flag
	ram[smb_misc_collision_flag + reg_x] = reg_a;
ex_phc:;
	return;
	smb_handle_power_up_collision(); return;
}

void smb_handle_power_up_collision() {
	smb_erase_enemy_object(); // erase the power-up object
	set_a(0x06);
	smb_setup_floatey_number(); // award 1000 points to player by default
	set_a((smb_sfx_power_up_grab));
	ram[smb_square_2_sound_queue] = reg_a; // play the power-up sound
	set_a(ram[smb_power_up_type]); // check power-up type
	cmp_a(0x02);
	if (!reg_p.c) { goto shroom_flower_p_up; } // if mushroom or fire flower, branch
	cmp_a(0x03);
	if (reg_p.z) { goto set_for_1_up; } // if 1-up mushroom, branch
	set_a(0x23); // otherwise set star mario invincibility
	ram[smb_star_invincible_timer] = reg_a; // timer, and load the star mario music
	set_a((smb_star_power_music)); // into the area music queue, then leave
	ram[smb_area_music_queue] = reg_a;
	return;
shroom_flower_p_up:;
	set_a(ram[smb_player_status]); // if player status = small, branch
	if (reg_p.z) { goto up_to_super; }
	cmp_a(0x01); // if player status not super, leave
	if (!reg_p.z) { smb_no_p_up(); return; }
	set_x(ram[smb_object_offset]); // get enemy offset, not necessary
	set_a(0x02); // set player status to fiery
	ram[smb_player_status] = reg_a;
	smb_get_player_colors(); // run sub to change colors of player
	set_x(ram[smb_object_offset]); // get enemy offset again, and again not necessary
	set_a(0x0c); // set value to be used by subroutine tree (fiery)
	goto up_to_fiery; // jump to set values accordingly
set_for_1_up:;
	set_a(0x0b); // change 1000 points into 1-up instead
	ram[smb_floatey_num_control + reg_x] = reg_a; // and then leave
	return;
up_to_super:;
	set_a(0x01); // set player status to super
	ram[smb_player_status] = reg_a;
	set_a(0x09); // set value to be used by subroutine tree (super)
up_to_fiery:;
	set_y(0x00); // set value to be used as new player state
	smb_set_p_rout(); // set values to stop certain things in motion
	smb_no_p_up(); return;
}

void smb_no_p_up() {
	return;
}

void smb_player_enemy_collision() {
	set_a(ram[smb_frame_counter]); // check counter for d0 set
	reg_a = shr(reg_a);
	if (reg_p.c) { smb_no_p_up(); return; } // if set, branch to leave
	smb_check_player_vertical(); // if player object is completely offscreen or
	if (reg_p.c) { goto no_pe_col; } // if down past 224th pixel row, branch to leave
	set_a(ram[smb_enemy_offscr_bits_masked + reg_x]); // if current enemy is offscreen by any amount,
	if (!reg_p.z) { goto no_pe_col; } // go ahead and branch to leave
	set_a(ram[smb_game_engine_subroutine]);
	cmp_a(0x08); // if not set to run player control routine
	if (!reg_p.z) { goto no_pe_col; } // on next frame, branch to leave
	set_a(ram[smb_enemy_state + reg_x]);
	and_a(0b00100000); // if enemy state has d5 set, branch to leave
	if (!reg_p.z) { goto no_pe_col; }
	smb_get_enemy_bound_box_ofs(); // get bounding box offset for current enemy object
	smb_player_collision_core(); // do collision detection on player vs. enemy
	set_x(ram[smb_object_offset]); // get enemy object buffer offset
	if (reg_p.c) { goto chk_for_p_up_collision; } // if collision, branch past this part here
	set_a(ram[smb_enemy_collision_bits + reg_x]);
	and_a(0b11111110); // otherwise, clear d0 of current enemy object's
	ram[smb_enemy_collision_bits + reg_x] = reg_a; // collision bit
no_pe_col:;
	return;
chk_for_p_up_collision:;
	set_y(ram[smb_enemy_id + reg_x]);
	cmp_y((smb_power_up_object)); // check for power-up object
	if (!reg_p.z) { goto e_coll; } // if not found, branch to next part
	smb_handle_power_up_collision(); return; // otherwise, unconditional jump backwards
e_coll:;
	set_a(ram[smb_star_invincible_timer]); // if star mario invincibility timer expired,
	if (reg_p.z) { smb_handle_pe_collisions(); return; } // perform task here, otherwise kill enemy like
	smb_shell_or_block_defeat(); return; // hit with a shell, or from beneath
}

void smb_handle_pe_collisions() {
	set_a(ram[smb_enemy_collision_bits + reg_x]); // check enemy collision bits for d0 set
	and_a(0b00000001); // or for being offscreen at all
	or_a(ram[smb_enemy_offscr_bits_masked + reg_x]);
	if (!reg_p.z) { goto ex_pec; } // branch to leave if either is true
	set_a(0x01);
	or_a(ram[smb_enemy_collision_bits + reg_x]); // otherwise set d0 now
	ram[smb_enemy_collision_bits + reg_x] = reg_a;
	cmp_y((smb_spiny)); // branch if spiny
	if (reg_p.z) { goto chk_for_player_injury; }
	cmp_y((smb_piranha_plant)); // branch if piranha plant
	if (reg_p.z) { smb_injure_player(); return; }
	cmp_y((smb_podoboo)); // branch if podoboo
	if (reg_p.z) { smb_injure_player(); return; }
	cmp_y((smb_bullet_bill_cannon_var)); // branch if bullet bill
	if (reg_p.z) { goto chk_for_player_injury; }
	cmp_y(0x15); // branch if object => $15
	if (reg_p.c) { smb_injure_player(); return; }
	set_a(ram[smb_area_type]); // branch if water type level
	if (reg_p.z) { smb_injure_player(); return; }
	set_a(ram[smb_enemy_state + reg_x]); // branch if d7 of enemy state was set
	reg_a = shl(reg_a);
	if (reg_p.c) { goto chk_for_player_injury; }
	set_a(ram[smb_enemy_state + reg_x]); // mask out all but 3 LSB of enemy state
	and_a(0b00000111);
	cmp_a(0x02); // branch if enemy is in normal or falling state
	if (!reg_p.c) { goto chk_for_player_injury; }
	set_a(ram[smb_enemy_id + reg_x]); // branch to leave if goomba in defeated state
	cmp_a(0x06);
	if (reg_p.z) { goto ex_pec; }
	set_a((smb_sfx_enemy_smack)); // play smack enemy sound
	ram[smb_square_1_sound_queue] = reg_a;
	set_a(ram[smb_enemy_state + reg_x]); // set d7 in enemy state, thus become moving shell
	or_a(0b10000000);
	ram[smb_enemy_state + reg_x] = reg_a;
	smb_enemy_face_player(); // set moving direction and get offset
	set_a(rom[smb_kicked_shell_x_spd_data + reg_y]); // load and set horizontal speed data with offset
	ram[smb_enemy_x_speed + reg_x] = reg_a;
	set_a(0x03); // add three to whatever the stomp counter contains
	reg_p.c = 0; // to give points for kicking the shell
	add_a(ram[smb_stomp_chain_counter]);
	set_y(ram[smb_enemy_interval_timer + reg_x]); // check shell enemy's timer
	cmp_y(0x03); // if above a certain point, branch using the points
	if (reg_p.c) { goto ks_pts; } // data obtained from the stomp counter + 3
	set_a(rom[smb_kicked_shell_pts_data + reg_y]); // otherwise, set points based on proximity to timer expiration
ks_pts:;
	smb_setup_floatey_number(); // set values for floatey number now
ex_pec:;
	return; // leave!!!
chk_for_player_injury:;
	set_a(ram[smb_player_y_speed]); // check player's vertical speed
	if (reg_p.n) { goto chk_inj; } // perform procedure below if player moving upwards
	if (!reg_p.z) { smb_enemy_stomped(); return; } // or not at all, and branch elsewhere if moving downwards
chk_inj:;
	set_a(ram[smb_enemy_id + reg_x]); // branch if enemy object < $07
	cmp_a((smb_bloober));
	if (!reg_p.c) { goto chk_e_tmrs; }
	set_a(ram[smb_player_y_position]); // add 12 pixels to player's vertical position
	reg_p.c = 0;
	add_a(0x0c);
	cmp_a(ram[smb_enemy_y_position + reg_x]); // compare modified player's position to enemy's position
	if (!reg_p.c) { smb_enemy_stomped(); return; } // branch if this player's position above (less than) enemy's
chk_e_tmrs:;
	set_a(ram[smb_stomp_timer]); // check stomp timer
	if (!reg_p.z) { smb_enemy_stomped(); return; } // branch if set
	set_a(ram[smb_injury_timer]); // check to see if injured invincibility timer still
	if (!reg_p.z) { smb_ex_inj_col_routines(); return; } // counting down, and branch elsewhere to leave if so
	set_a(ram[smb_player_rel_x_pos]);
	cmp_a(ram[smb_enemy_rel_x_pos]); // if player's relative position to the left of enemy's
	if (!reg_p.c) { goto t_inj_e; } // relative position, branch here
	smb_chk_enemy_face_right(); return; // otherwise do a jump here
t_inj_e:;
	set_a(ram[smb_enemy_moving_dir + reg_x]); // if enemy moving towards the left,
	cmp_a(0x01); // branch, otherwise do a jump here
	if (!reg_p.z) { smb_injure_player(); return; } // to turn the enemy around
	smb_l_inj(); return;
	smb_injure_player(); return;
}

void smb_injure_player() {
	set_a(ram[smb_injury_timer]); // check again to see if injured invincibility timer is
	if (!reg_p.z) { smb_ex_inj_col_routines(); return; } // at zero, and branch to leave if so
	smb_force_injury(); return;
}

void smb_force_injury() {
	set_x(ram[smb_player_status]); // check player's status
	if (reg_p.z) { smb_kill_player(); return; } // branch if small
	ram[smb_player_status] = reg_a; // otherwise set player's status to small
	set_a(0x08);
	ram[smb_injury_timer] = reg_a; // set injured invincibility timer
	reg_a = shl(reg_a);
	ram[smb_square_1_sound_queue] = reg_a; // play pipedown/injury sound
	smb_get_player_colors(); // change player's palette if necessary
	set_a(0x0a); // set subroutine to run on next frame
	smb_set_k_rout(); return;
}

void smb_set_k_rout() {
	set_y(0x01); // set new player state
	smb_set_p_rout(); return;
}

void smb_set_p_rout() {
	ram[smb_game_engine_subroutine] = reg_a; // load new value to run subroutine on next frame
	ram[smb_player_state] = reg_y; // store new player state
	set_y(0xff);
	ram[smb_timer_control] = reg_y; // set master timer control flag to halt timers
	set_y(reg_y+1);
	ram[smb_scroll_amount] = reg_y; // initialize scroll speed
	smb_ex_inj_col_routines(); return;
}

void smb_ex_inj_col_routines() {
	set_x(ram[smb_object_offset]); // get enemy offset and leave
	return;
	smb_kill_player(); return;
}

void smb_kill_player() {
	ram[smb_player_x_speed] = reg_x; // halt player's horizontal movement by initializing speed
	set_x(reg_x+1);
	ram[smb_event_music_queue] = reg_x; // set event music queue to death music
	set_a(0xfc);
	ram[smb_player_y_speed] = reg_a; // set new vertical speed
	set_a(0x0b); // set subroutine to run on next frame
	if (!reg_p.z) { smb_set_k_rout(); return; } // branch to set player's state and other things
}

void smb_enemy_stomped() {
	set_a(ram[smb_enemy_id + reg_x]); // check for spiny, branch to hurt player
	cmp_a((smb_spiny)); // if found
	if (reg_p.z) { smb_injure_player(); return; }
	set_a((smb_sfx_enemy_stomp)); // otherwise play stomp/swim sound
	ram[smb_square_1_sound_queue] = reg_a;
	set_a(ram[smb_enemy_id + reg_x]);
	set_y(0x00); // initialize points data offset for stomped enemies
	cmp_a((smb_flying_cheep_cheep)); // branch for cheep-cheep
	if (reg_p.z) { goto enemy_stomped_pts; }
	cmp_a((smb_bullet_bill_frenzy_var)); // branch for either bullet bill object
	if (reg_p.z) { goto enemy_stomped_pts; }
	cmp_a((smb_bullet_bill_cannon_var));
	if (reg_p.z) { goto enemy_stomped_pts; }
	cmp_a((smb_podoboo)); // branch for podoboo (this branch is logically impossible
	if (reg_p.z) { goto enemy_stomped_pts; } // for cpu to take due to earlier checking of podoboo)
	set_y(reg_y+1); // increment points data offset
	cmp_a((smb_hammer_bro)); // branch for hammer bro
	if (reg_p.z) { goto enemy_stomped_pts; }
	set_y(reg_y+1); // increment points data offset
	cmp_a((smb_lakitu)); // branch for lakitu
	if (reg_p.z) { goto enemy_stomped_pts; }
	set_y(reg_y+1); // increment points data offset
	cmp_a((smb_bloober)); // branch if NOT bloober
	if (!reg_p.z) { goto chk_for_demote_koopa; }
enemy_stomped_pts:;
	set_a(rom[smb_stomped_enemy_pts_data + reg_y]); // load points data using offset in Y
	smb_setup_floatey_number(); // run sub to set floatey number controls
	set_a(ram[smb_enemy_moving_dir + reg_x]);
	push(reg_a); // save enemy movement direction to stack
	smb_set_stun(); // run sub to kill enemy
	set_a(pull());
	ram[smb_enemy_moving_dir + reg_x] = reg_a; // return enemy movement direction from stack
	set_a(0b00100000);
	ram[smb_enemy_state + reg_x] = reg_a; // set d5 in enemy state
	smb_init_v_stf(); // nullify vertical speed, physics-related thing,
	ram[smb_enemy_x_speed + reg_x] = reg_a; // and horizontal speed
	set_a(0xfd); // set player's vertical speed, to give bounce
	ram[smb_player_y_speed] = reg_a;
	return;
chk_for_demote_koopa:;
	cmp_a(0x09); // branch elsewhere if enemy object < $09
	if (!reg_p.c) { smb_handle_stomped_shell_e(); return; }
	and_a(0b00000001); // demote koopa paratroopas to ordinary troopas
	ram[smb_enemy_id + reg_x] = reg_a;
	set_y(0x00); // return enemy to normal state
	ram[smb_enemy_state + reg_x] = reg_y;
	set_a(0x03); // award 400 points to the player
	smb_setup_floatey_number();
	smb_init_v_stf(); // nullify physics-related thing and vertical speed
	smb_enemy_face_player(); // turn enemy around if necessary
	set_a(rom[smb_demoted_koopa_x_spd_data + reg_y]);
	ram[smb_enemy_x_speed + reg_x] = reg_a; // set appropriate moving speed based on direction
	smb_s_bnce(); return; // then move onto something else
}

void smb_handle_stomped_shell_e() {
	set_a(0x04); // set defeated state for enemy
	ram[smb_enemy_state + reg_x] = reg_a;
	ram[smb_stomp_chain_counter] = inc(ram[smb_stomp_chain_counter]); // increment the stomp counter
	set_a(ram[smb_stomp_chain_counter]); // add whatever is in the stomp counter
	reg_p.c = 0; // to whatever is in the stomp timer
	add_a(ram[smb_stomp_timer]);
	smb_setup_floatey_number(); // award points accordingly
	ram[smb_stomp_timer] = inc(ram[smb_stomp_timer]); // increment stomp timer of some sort
	set_y(ram[smb_primary_hard_mode]); // check primary hard mode flag
	set_a(rom[smb_revival_rate_data + reg_y]); // load timer setting according to flag
	ram[smb_enemy_interval_timer + reg_x] = reg_a; // set as enemy timer to revive stomped enemy
	smb_s_bnce(); return;
}

void smb_s_bnce() {
	set_a(0xfc); // set player's vertical speed for bounce
	ram[smb_player_y_speed] = reg_a; // and then leave!!!
	return;
	smb_chk_enemy_face_right(); return;
}

void smb_chk_enemy_face_right() {
	set_a(ram[smb_enemy_moving_dir + reg_x]); // check to see if enemy is moving to the right
	cmp_a(0x01);
	if (!reg_p.z) { smb_l_inj(); return; } // if not, branch
	smb_injure_player(); return; // otherwise go back to hurt player
	smb_l_inj(); return;
}

void smb_l_inj() {
	smb_enemy_turn_around(); // turn the enemy around, if necessary
	smb_injure_player(); return; // go back to hurt player
	smb_enemy_face_player(); return;
}

void smb_enemy_face_player() {
	set_y(0x01); // set to move right by default
	smb_player_enemy_diff(); // get horizontal difference between player and enemy
	if (!reg_p.n) { goto s_fc_rt; } // if enemy is to the right of player, do not increment
	set_y(reg_y+1); // otherwise, increment to set to move to the left
s_fc_rt:;
	ram[smb_enemy_moving_dir + reg_x] = reg_y; // set moving direction here
	set_y(reg_y-1); // then decrement to use as a proper offset
	return;
	smb_setup_floatey_number(); return;
}

void smb_setup_floatey_number() {
	ram[smb_floatey_num_control + reg_x] = reg_a; // set number of points control for floatey numbers
	set_a(0x30);
	ram[smb_floatey_num_timer + reg_x] = reg_a; // set timer for floatey numbers
	set_a(ram[smb_enemy_y_position + reg_x]);
	ram[smb_floatey_num_y_pos + reg_x] = reg_a; // set vertical coordinate
	set_a(ram[smb_enemy_rel_x_pos]);
	ram[smb_floatey_num_x_pos + reg_x] = reg_a; // set horizontal coordinate and leave
	smb_ex_sfn(); return;
}

void smb_ex_sfn() {
	return;
}

void smb_enemies_collision() {
	set_a(ram[smb_frame_counter]); // check counter for d0 set
	reg_a = shr(reg_a);
	if (!reg_p.c) { smb_ex_sfn(); return; } // if d0 not set, leave
	set_a(ram[smb_area_type]);
	if (reg_p.z) { smb_ex_sfn(); return; } // if water area type, leave
	set_a(ram[smb_enemy_id + reg_x]);
	cmp_a(0x15); // if enemy object => $15, branch to leave
	if (reg_p.c) { goto exit_ec_routine; }
	cmp_a((smb_lakitu)); // if lakitu, branch to leave
	if (reg_p.z) { goto exit_ec_routine; }
	cmp_a((smb_piranha_plant)); // if piranha plant, branch to leave
	if (reg_p.z) { goto exit_ec_routine; }
	set_a(ram[smb_enemy_offscr_bits_masked + reg_x]); // if masked offscreen bits nonzero, branch to leave
	if (!reg_p.z) { goto exit_ec_routine; }
	smb_get_enemy_bound_box_ofs(); // otherwise, do sub, get appropriate bounding box offset for
	set_x(reg_x-1); // first enemy we're going to compare, then decrement for second
	if (reg_p.n) { goto exit_ec_routine; } // branch to leave if there are no other enemies
ec_loop:;
	ram[0x0001] = reg_x; // save enemy object buffer offset for second enemy here
	set_a(reg_y); // save first enemy's bounding box offset to stack
	push(reg_a);
	set_a(ram[smb_enemy_flag + reg_x]); // check enemy object enable flag
	if (reg_p.z) { goto ready_next_enemy; } // branch if flag not set
	set_a(ram[smb_enemy_id + reg_x]);
	cmp_a(0x15); // check for enemy object => $15
	if (reg_p.c) { goto ready_next_enemy; } // branch if true
	cmp_a((smb_lakitu));
	if (reg_p.z) { goto ready_next_enemy; } // branch if enemy object is lakitu
	cmp_a((smb_piranha_plant));
	if (reg_p.z) { goto ready_next_enemy; } // branch if enemy object is piranha plant
	set_a(ram[smb_enemy_offscr_bits_masked + reg_x]);
	if (!reg_p.z) { goto ready_next_enemy; } // branch if masked offscreen bits set
	set_a(reg_x); // get second enemy object's bounding box offset
	reg_a = shl(reg_a); // multiply by four, then add four
	reg_a = shl(reg_a);
	reg_p.c = 0;
	add_a(0x04);
	set_x(reg_a); // use as new contents of X
	smb_spr_object_collision_core(); // do collision detection using the two enemies here
	set_x(ram[smb_object_offset]); // use first enemy offset for X
	set_y(ram[0x0001]); // use second enemy offset for Y
	if (!reg_p.c) { goto no_enemy_collision; } // if carry clear, no collision, branch ahead of this
	set_a(ram[smb_enemy_state + reg_x]);
	or_a(ram[smb_enemy_state + reg_y]); // check both enemy states for d7 set
	and_a(0b10000000);
	if (!reg_p.z) { goto yes_ec; } // branch if at least one of them is set
	set_a(ram[smb_enemy_collision_bits + reg_y]); // load first enemy's collision-related bits
	and_a(rom[smb_set_bits_mask + reg_x]); // check to see if bit connected to second enemy is
	if (!reg_p.z) { goto ready_next_enemy; } // already set, and move onto next enemy slot if set
	set_a(ram[smb_enemy_collision_bits + reg_y]);
	or_a(rom[smb_set_bits_mask + reg_x]); // if the bit is not set, set it now
	ram[smb_enemy_collision_bits + reg_y] = reg_a;
yes_ec:;
	smb_proc_enemy_collisions(); // react according to the nature of collision
	goto ready_next_enemy; // move onto next enemy slot
no_enemy_collision:;
	set_a(ram[smb_enemy_collision_bits + reg_y]); // load first enemy's collision-related bits
	and_a(rom[smb_clear_bits_mask + reg_x]); // clear bit connected to second enemy
	ram[smb_enemy_collision_bits + reg_y] = reg_a; // then move onto next enemy slot
ready_next_enemy:;
	set_a(pull()); // get first enemy's bounding box offset from the stack
	set_y(reg_a); // use as Y again
	set_x(ram[0x0001]); // get and decrement second enemy's object buffer offset
	set_x(reg_x-1);
	if (!reg_p.n) { goto ec_loop; } // loop until all enemy slots have been checked
exit_ec_routine:;
	set_x(ram[smb_object_offset]); // get enemy object buffer offset
	return; // leave
	smb_proc_enemy_collisions(); return;
}

void smb_proc_enemy_collisions() {
	set_a(ram[smb_enemy_state + reg_y]); // check both enemy states for d5 set
	or_a(ram[smb_enemy_state + reg_x]);
	and_a(0b00100000); // if d5 is set in either state, or both, branch
	if (!reg_p.z) { goto exit_process_e_coll; } // to leave and do nothing else at this point
	set_a(ram[smb_enemy_state + reg_x]);
	cmp_a(0x06); // if second enemy state < $06, branch elsewhere
	if (!reg_p.c) { goto proc_second_enemy_coll; }
	set_a(ram[smb_enemy_id + reg_x]); // check second enemy identifier for hammer bro
	cmp_a((smb_hammer_bro)); // if hammer bro found in alt state, branch to leave
	if (reg_p.z) { goto exit_process_e_coll; }
	set_a(ram[smb_enemy_state + reg_y]); // check first enemy state for d7 set
	reg_a = shl(reg_a);
	if (!reg_p.c) { goto shell_collisions; } // branch if d7 is clear
	set_a(0x06);
	smb_setup_floatey_number(); // award 1000 points for killing enemy
	smb_shell_or_block_defeat(); // then kill enemy, then load
	set_y(ram[0x0001]); // original offset of second enemy
shell_collisions:;
	set_a(reg_y); // move Y to X
	set_x(reg_a);
	smb_shell_or_block_defeat(); // kill second enemy
	set_x(ram[smb_object_offset]);
	set_a(ram[smb_shell_chain_counter + reg_x]); // get chain counter for shell
	reg_p.c = 0;
	add_a(0x04); // add four to get appropriate point offset
	set_x(ram[0x0001]);
	smb_setup_floatey_number(); // award appropriate number of points for second enemy
	set_x(ram[smb_object_offset]); // load original offset of first enemy
	ram[smb_shell_chain_counter + reg_x] = inc(ram[smb_shell_chain_counter + reg_x]); // increment chain counter for additional enemies
exit_process_e_coll:;
	return; // leave!!!
proc_second_enemy_coll:;
	set_a(ram[smb_enemy_state + reg_y]); // if first enemy state < $06, branch elsewhere
	cmp_a(0x06);
	if (!reg_p.c) { goto move_e_ofs; }
	set_a(ram[smb_enemy_id + reg_y]); // check first enemy identifier for hammer bro
	cmp_a((smb_hammer_bro)); // if hammer bro found in alt state, branch to leave
	if (reg_p.z) { goto exit_process_e_coll; }
	smb_shell_or_block_defeat(); // otherwise, kill first enemy
	set_y(ram[0x0001]);
	set_a(ram[smb_shell_chain_counter + reg_y]); // get chain counter for shell
	reg_p.c = 0;
	add_a(0x04); // add four to get appropriate point offset
	set_x(ram[smb_object_offset]);
	smb_setup_floatey_number(); // award appropriate number of points for first enemy
	set_x(ram[0x0001]); // load original offset of second enemy
	ram[smb_shell_chain_counter + reg_x] = inc(ram[smb_shell_chain_counter + reg_x]); // increment chain counter for additional enemies
	return; // leave!!!
move_e_ofs:;
	set_a(reg_y); // move Y ($01) to X
	set_x(reg_a);
	smb_enemy_turn_around(); // do the sub here using value from $01
	set_x(ram[smb_object_offset]); // then do it again using value from $08
	smb_enemy_turn_around(); return;
}

void smb_enemy_turn_around() {
	set_a(ram[smb_enemy_id + reg_x]); // check for specific enemies
	cmp_a((smb_piranha_plant));
	if (reg_p.z) { smb_ex_ta(); return; } // if piranha plant, leave
	cmp_a((smb_lakitu));
	if (reg_p.z) { smb_ex_ta(); return; } // if lakitu, leave
	cmp_a((smb_hammer_bro));
	if (reg_p.z) { smb_ex_ta(); return; } // if hammer bro, leave
	cmp_a((smb_spiny));
	if (reg_p.z) { smb_rx_spd(); return; } // if spiny, turn it around
	cmp_a((smb_green_paratroopa_jump));
	if (reg_p.z) { smb_rx_spd(); return; } // if green paratroopa, turn it around
	cmp_a(0x07);
	if (reg_p.c) { smb_ex_ta(); return; } // if any OTHER enemy object => $07, leave
	smb_rx_spd(); return;
}

void smb_rx_spd() {
	set_a(ram[smb_enemy_x_speed + reg_x]); // load horizontal speed
	eor_a(0xff); // get two's compliment for horizontal speed
	set_y(reg_a);
	set_y(reg_y+1);
	ram[smb_enemy_x_speed + reg_x] = reg_y; // store as new horizontal speed
	set_a(ram[smb_enemy_moving_dir + reg_x]);
	eor_a(0b00000011); // invert moving direction and store, then leave
	ram[smb_enemy_moving_dir + reg_x] = reg_a; // thus effectively turning the enemy around
	smb_ex_ta(); return;
}

void smb_ex_ta() {
	return; // leave!!!
	smb_large_platform_collision(); return;
}

void smb_large_platform_collision() {
	set_a(0xff); // save value here
	ram[smb_hammer_throwing_timer + reg_x] = reg_a;
	set_a(ram[smb_timer_control]); // check master timer control
	if (!reg_p.z) { smb_ex_lpc(); return; } // if set, branch to leave
	set_a(ram[smb_enemy_state + reg_x]); // if d7 set in object state,
	if (reg_p.n) { smb_ex_lpc(); return; } // branch to leave
	set_a(ram[smb_enemy_id + reg_x]);
	cmp_a(0x24); // check enemy object identifier for
	if (!reg_p.z) { smb_chk_for_player_c_large_p(); return; } // balance platform, branch if not found
	set_a(ram[smb_enemy_state + reg_x]);
	set_x(reg_a); // set state as enemy offset here
	smb_chk_for_player_c_large_p(); // perform code with state offset, then original offset, in X
	smb_chk_for_player_c_large_p(); return;
}

void smb_chk_for_player_c_large_p() {
	smb_check_player_vertical(); // figure out if player is below a certain point
	if (reg_p.c) { smb_ex_lpc(); return; } // or offscreen, branch to leave if true
	set_a(reg_x);
	smb_get_enemy_bound_box_ofs_arg(); // get bounding box offset in Y
	set_a(ram[smb_enemy_y_position + reg_x]); // store vertical coordinate in
	ram[0x0000] = reg_a; // temp variable for now
	set_a(reg_x); // send offset we're on to the stack
	push(reg_a);
	smb_player_collision_core(); // do player-to-platform collision detection
	set_a(pull()); // retrieve offset from the stack
	set_x(reg_a);
	if (!reg_p.c) { smb_ex_lpc(); return; } // if no collision, branch to leave
	smb_proc_l_plat_collisions(); // otherwise collision, perform sub
	smb_ex_lpc(); return;
}

void smb_ex_lpc() {
	set_x(ram[smb_object_offset]); // get enemy object buffer offset and leave
	return;
	smb_small_platform_collision(); return;
}

void smb_small_platform_collision() {
	set_a(ram[smb_timer_control]); // if master timer control set,
	if (!reg_p.z) { goto ex_spc; } // branch to leave
	ram[smb_platform_collision_flag + reg_x] = reg_a; // otherwise initialize collision flag
	smb_check_player_vertical(); // do a sub to see if player is below a certain point
	if (reg_p.c) { goto ex_spc; } // or entirely offscreen, and branch to leave if true
	set_a(0x02);
	ram[0x0000] = reg_a; // load counter here for 2 bounding boxes
chk_small_plat_loop:;
	set_x(ram[smb_object_offset]); // get enemy object offset
	smb_get_enemy_bound_box_ofs(); // get bounding box offset in Y
	and_a(0b00000010); // if d1 of offscreen lower nybble bits was set
	if (!reg_p.z) { goto ex_spc; } // then branch to leave
	set_a(ram[smb_bounding_box_ul_y_pos + reg_y]); // check top of platform's bounding box for being
	cmp_a(0x20); // above a specific point
	if (!reg_p.c) { goto move_bound_box; } // if so, branch, don't do collision detection
	smb_player_collision_core(); // otherwise, perform player-to-platform collision detection
	if (reg_p.c) { goto proc_s_plat_collisions; } // skip ahead if collision
move_bound_box:;
	set_a(ram[smb_bounding_box_ul_y_pos + reg_y]); // move bounding box vertical coordinates
	reg_p.c = 0; // 128 pixels downwards
	add_a(0x80);
	ram[smb_bounding_box_ul_y_pos + reg_y] = reg_a;
	set_a(ram[smb_bounding_box_dr_y_pos + reg_y]);
	reg_p.c = 0;
	add_a(0x80);
	ram[smb_bounding_box_dr_y_pos + reg_y] = reg_a;
	ram[0x0000] = dec(ram[0x0000]); // decrement counter we set earlier
	if (!reg_p.z) { goto chk_small_plat_loop; } // loop back until both bounding boxes are checked
ex_spc:;
	set_x(ram[smb_object_offset]); // get enemy object buffer offset, then leave
	return;
proc_s_plat_collisions:;
	set_x(ram[smb_object_offset]); // return enemy object buffer offset to X, then continue
	smb_proc_l_plat_collisions(); return;
}

void smb_proc_l_plat_collisions() {
	set_a(ram[smb_bounding_box_dr_y_pos + reg_y]); // get difference by subtracting the top
	reg_p.c = 1; // of the player's bounding box from the bottom
	sub_a(ram[smb_bounding_box_ul_y_pos]); // of the platform's bounding box
	cmp_a(0x04); // if difference too large or negative,
	if (reg_p.c) { goto chk_for_top_collision; } // branch, do not alter vertical speed of player
	set_a(ram[smb_player_y_speed]); // check to see if player's vertical speed is moving down
	if (!reg_p.n) { goto chk_for_top_collision; } // if so, don't mess with it
	set_a(0x01); // otherwise, set vertical
	ram[smb_player_y_speed] = reg_a; // speed of player to kill jump
chk_for_top_collision:;
	set_a(ram[smb_bounding_box_dr_y_pos]); // get difference by subtracting the top
	reg_p.c = 1; // of the platform's bounding box from the bottom
	sub_a(ram[smb_bounding_box_ul_y_pos + reg_y]); // of the player's bounding box
	cmp_a(0x06);
	if (reg_p.c) { goto platform_side_collisions; } // if difference not close enough, skip all of this
	set_a(ram[smb_player_y_speed]);
	if (reg_p.n) { goto platform_side_collisions; } // if player's vertical speed moving upwards, skip this
	set_a(ram[0x0000]); // get saved bounding box counter from earlier
	set_y(ram[smb_enemy_id + reg_x]);
	cmp_y(0x2b); // if either of the two small platform objects are found,
	if (reg_p.z) { goto set_collision_flag; } // regardless of which one, branch to use bounding box counter
	cmp_y(0x2c); // as contents of collision flag
	if (reg_p.z) { goto set_collision_flag; }
	set_a(reg_x); // otherwise use enemy object buffer offset
set_collision_flag:;
	set_x(ram[smb_object_offset]); // get enemy object buffer offset
	ram[smb_platform_collision_flag + reg_x] = reg_a; // save either bounding box counter or enemy offset here
	set_a(0x00);
	ram[smb_player_state] = reg_a; // set player state to normal then leave
	return;
platform_side_collisions:;
	set_a(0x01); // set value here to indicate possible horizontal
	ram[0x0000] = reg_a; // collision on left side of platform
	set_a(ram[smb_bounding_box_dr_x_pos]); // get difference by subtracting platform's left edge
	reg_p.c = 1; // from player's right edge
	sub_a(ram[smb_bounding_box_ul_x_pos + reg_y]);
	cmp_a(0x08); // if difference close enough, skip all of this
	if (!reg_p.c) { goto side_c; }
	ram[0x0000] = inc(ram[0x0000]); // otherwise increment value set here for right side collision
	set_a(ram[smb_bounding_box_dr_x_pos + reg_y]); // get difference by subtracting player's left edge
	reg_p.c = 0; // from platform's right edge
	sub_a(ram[smb_bounding_box_ul_x_pos]);
	cmp_a(0x09); // if difference not close enough, skip subroutine
	if (reg_p.c) { goto no_side_c; } // and instead branch to leave (no collision)
side_c:;
	smb_impede_player_move(); // deal with horizontal collision
no_side_c:;
	set_x(ram[smb_object_offset]); // return with enemy object buffer offset
	return;
}

void smb_position_player_on_s_plat() {
	set_y(reg_a); // use bounding box counter saved in collision flag
	set_a(ram[smb_enemy_y_position + reg_x]); // for offset
	reg_p.c = 0; // add positioning data using offset to the vertical
	add_a(rom[smb_player_pos_s_plat_data-1 + reg_y]); // coordinate
	smb_position_player_on_plat(); return;
}

void smb_position_player_on_v_plat() {
	set_a(ram[smb_enemy_y_position + reg_x]); // get vertical coordinate
	smb_position_player_on_plat(); return;
}

void smb_position_player_on_plat() {
	set_y(ram[smb_game_engine_subroutine]);
	cmp_y(0x0b); // if certain routine being executed on this frame,
	if (reg_p.z) { goto ex_pl_pos; } // skip all of this
	set_y(ram[smb_enemy_y_high_pos + reg_x]);
	cmp_y(0x01); // if vertical high byte offscreen, skip this
	if (!reg_p.z) { goto ex_pl_pos; }
	reg_p.c = 1; // subtract 32 pixels from vertical coordinate
	sub_a(0x20); // for the player object's height
	ram[smb_player_y_position] = reg_a; // save as player's new vertical coordinate
	set_a(reg_y);
	sub_a(0x00); // subtract borrow and store as player's
	ram[smb_player_y_high_pos] = reg_a; // new vertical high byte
	set_a(0x00);
	ram[smb_player_y_speed] = reg_a; // initialize vertical speed and low byte of force
	ram[smb_player_y_move_force] = reg_a; // and then leave
ex_pl_pos:;
	return;
	smb_check_player_vertical(); return;
}

void smb_check_player_vertical() {
	set_a(ram[smb_player_offscreen_bits]); // if player object is completely offscreen
	cmp_a(0xf0); // vertically, leave this routine
	if (reg_p.c) { goto ex_cpv; }
	set_y(ram[smb_player_y_high_pos]); // if player high vertical byte is not
	set_y(reg_y-1); // within the screen, leave this routine
	if (!reg_p.z) { goto ex_cpv; }
	set_a(ram[smb_player_y_position]); // if on the screen, check to see how far down
	cmp_a(0xd0); // the player is vertically
ex_cpv:;
	return;
	smb_get_enemy_bound_box_ofs(); return;
}

void smb_get_enemy_bound_box_ofs() {
	set_a(ram[smb_object_offset]); // get enemy object buffer offset
	smb_get_enemy_bound_box_ofs_arg(); return;
}

void smb_get_enemy_bound_box_ofs_arg() {
	reg_a = shl(reg_a); // multiply A by four, then add four
	reg_a = shl(reg_a); // to skip player's bounding box
	reg_p.c = 0;
	add_a(0x04);
	set_y(reg_a); // send to Y
	set_a(ram[smb_enemy_offscreen_bits]); // get offscreen bits for enemy object
	and_a(0b00001111); // save low nybble
	cmp_a(0b00001111); // check for all bits set
	return;
}

void smb_player_bg_collision() {
	set_a(ram[smb_disable_collision_det]); // if collision detection disabled flag set,
	if (!reg_p.z) { goto ex_pbg_col; } // branch to leave
	set_a(ram[smb_game_engine_subroutine]);
	cmp_a(0x0b); // if running routine #11 or $0b
	if (reg_p.z) { goto ex_pbg_col; } // branch to leave
	cmp_a(0x04);
	if (!reg_p.c) { goto ex_pbg_col; } // if running routines $00-$03 branch to leave
	set_a(0x01); // load default player state for swimming
	set_y(ram[smb_swimming_flag]); // if swimming flag set,
	if (!reg_p.z) { goto set_p_ste; } // branch ahead to set default state
	set_a(ram[smb_player_state]); // if player in normal state,
	if (reg_p.z) { goto set_fall_s; } // branch to set default state for falling
	cmp_a(0x03);
	if (!reg_p.z) { goto chk_on_scr; } // if in any other state besides climbing, skip to next part
set_fall_s:;
	set_a(0x02); // load default player state for falling
set_p_ste:;
	ram[smb_player_state] = reg_a; // set whatever player state is appropriate
chk_on_scr:;
	set_a(ram[smb_player_y_high_pos]);
	cmp_a(0x01); // check player's vertical high byte for still on the screen
	if (!reg_p.z) { goto ex_pbg_col; } // branch to leave if not
	set_a(0xff);
	ram[smb_player_collision_bits] = reg_a; // initialize player's collision flag
	set_a(ram[smb_player_y_position]);
	cmp_a(0xcf); // check player's vertical coordinate
	if (!reg_p.c) { goto chk_coll_size; } // if not too close to the bottom of screen, continue
ex_pbg_col:;
	return; // otherwise leave
chk_coll_size:;
	set_y(0x02); // load default offset
	set_a(ram[smb_crouching_flag]);
	if (!reg_p.z) { goto gbb_adr; } // if player crouching, skip ahead
	set_a(ram[smb_player_size]);
	if (!reg_p.z) { goto gbb_adr; } // if player small, skip ahead
	set_y(reg_y-1); // otherwise decrement offset for big player not crouching
	set_a(ram[smb_swimming_flag]);
	if (!reg_p.z) { goto gbb_adr; } // if swimming flag set, skip ahead
	set_y(reg_y-1); // otherwise decrement offset
gbb_adr:;
	set_a(rom[smb_block_buffer_adder_data + reg_y]); // get value using offset
	ram[0x00eb] = reg_a; // store value here
	set_y(reg_a); // put value into Y, as offset for block buffer routine
	set_x(ram[smb_player_size]); // get player's size as offset
	set_a(ram[smb_crouching_flag]);
	if (reg_p.z) { goto head_chk; } // if player not crouching, branch ahead
	set_x(reg_x+1); // otherwise increment size as offset
head_chk:;
	set_a(ram[smb_player_y_position]); // get player's vertical coordinate
	cmp_a(rom[smb_player_bg_upper_extent + reg_x]); // compare with upper extent value based on offset
	if (!reg_p.c) { goto do_foot_check; } // if player is too high, skip this part
	smb_block_buffer_colli_head(); // do player-to-bg collision detection on top of
	if (reg_p.z) { goto do_foot_check; } // player, and branch if nothing above player's head
	smb_check_for_coin_m_tiles(); // check to see if player touched coin with their head
	if (reg_p.c) { goto award_touched_coin; } // if so, branch to some other part of code
	set_y(ram[smb_player_y_speed]); // check player's vertical speed
	if (!reg_p.n) { goto do_foot_check; } // if player not moving upwards, branch elsewhere
	set_y(ram[0x0004]); // check lower nybble of vertical coordinate returned
	cmp_y(0x04); // from collision detection routine
	if (!reg_p.c) { goto do_foot_check; } // if low nybble < 4, branch
	smb_check_for_solid_m_tiles(); // check to see what player's head bumped on
	if (reg_p.c) { goto solid_or_climb; } // if player collided with solid metatile, branch
	set_y(ram[smb_area_type]); // otherwise check area type
	if (reg_p.z) { goto my_spd; } // if water level, branch ahead
	set_y(ram[smb_block_bounce_timer]); // if block bounce timer not expired,
	if (!reg_p.z) { goto my_spd; } // branch ahead, do not process collision
	smb_player_head_collision(); // otherwise do a sub to process collision
	goto do_foot_check; // jump ahead to skip these other parts here
solid_or_climb:;
	cmp_a(0x26); // if climbing metatile,
	if (reg_p.z) { goto my_spd; } // branch ahead and do not play sound
	set_a((smb_sfx_bump));
	ram[smb_square_1_sound_queue] = reg_a; // otherwise load bump sound
my_spd:;
	set_a(0x01); // set player's vertical speed to nullify
	ram[smb_player_y_speed] = reg_a; // jump or swim
do_foot_check:;
	set_y(ram[0x00eb]); // get block buffer adder offset
	set_a(ram[smb_player_y_position]);
	cmp_a(0xcf); // check to see how low player is
	if (reg_p.c) { goto do_player_side_check; } // if player is too far down on screen, skip all of this
	smb_block_buffer_colli_feet(); // do player-to-bg collision detection on bottom left of player
	smb_check_for_coin_m_tiles(); // check to see if player touched coin with their left foot
	if (reg_p.c) { goto award_touched_coin; } // if so, branch to some other part of code
	push(reg_a); // save bottom left metatile to stack
	smb_block_buffer_colli_feet(); // do player-to-bg collision detection on bottom right of player
	ram[0x0000] = reg_a; // save bottom right metatile here
	set_a(pull());
	ram[0x0001] = reg_a; // pull bottom left metatile and save here
	if (!reg_p.z) { goto chk_foot_m_tile; } // if anything here, skip this part
	set_a(ram[0x0000]); // otherwise check for anything in bottom right metatile
	if (reg_p.z) { goto do_player_side_check; } // and skip ahead if not
	smb_check_for_coin_m_tiles(); // check to see if player touched coin with their right foot
	if (!reg_p.c) { goto chk_foot_m_tile; } // if not, skip unconditional jump and continue code
award_touched_coin:;
	smb_handle_coin_metatile(); return; // follow the code to erase coin and award to player 1 coin
chk_foot_m_tile:;
	smb_check_for_climb_m_tiles(); // check to see if player landed on climbable metatiles
	if (reg_p.c) { goto do_player_side_check; } // if so, branch
	set_y(ram[smb_player_y_speed]); // check player's vertical speed
	if (reg_p.n) { goto do_player_side_check; } // if player moving upwards, branch
	cmp_a(0xc5);
	if (!reg_p.z) { goto cont_chk; } // if player did not touch axe, skip ahead
	smb_handle_axe_metatile(); return; // otherwise jump to set modes of operation
cont_chk:;
	smb_chk_invisible_m_tiles(); // do sub to check for hidden coin or 1-up blocks
	if (reg_p.z) { goto do_player_side_check; } // if either found, branch
	set_y(ram[smb_jumpspring_anim_ctrl]); // if jumpspring animating right now,
	if (!reg_p.z) { goto init_ste_p; } // branch ahead
	set_y(ram[0x0004]); // check lower nybble of vertical coordinate returned
	cmp_y(0x05); // from collision detection routine
	if (!reg_p.c) { goto land_plyr; } // if lower nybble < 5, branch
	set_a(ram[smb_player_moving_dir]);
	ram[0x0000] = reg_a; // use player's moving direction as temp variable
	smb_impede_player_move(); return; // jump to impede player's movement in that direction
land_plyr:;
	smb_chk_for_land_jump_spring(); // do sub to check for jumpspring metatiles and deal with it
	set_a(0xf0);
	and_a(ram[smb_player_y_position]); // mask out lower nybble of player's vertical position
	ram[smb_player_y_position] = reg_a; // and store as new vertical position to land player properly
	smb_handle_pipe_entry(); // do sub to process potential pipe entry
	set_a(0x00);
	ram[smb_player_y_speed] = reg_a; // initialize vertical speed and fractional
	ram[smb_player_y_move_force] = reg_a; // movement force to stop player's vertical movement
	ram[smb_stomp_chain_counter] = reg_a; // initialize enemy stomp counter
init_ste_p:;
	set_a(0x00);
	ram[smb_player_state] = reg_a; // set player's state to normal
do_player_side_check:;
	set_y(ram[0x00eb]); // get block buffer adder offset
	set_y(reg_y+1);
	set_y(reg_y+1); // increment offset 2 bytes to use adders for side collisions
	set_a(0x02); // set value here to be used as counter
	ram[0x0000] = reg_a;
side_check_loop:;
	set_y(reg_y+1); // move onto the next one
	ram[0x00eb] = reg_y; // store it
	set_a(ram[smb_player_y_position]);
	cmp_a(0x20); // check player's vertical position
	if (!reg_p.c) { goto b_half; } // if player is in status bar area, branch ahead to skip this part
	cmp_a(0xe4);
	if (reg_p.c) { goto ex_sch; } // branch to leave if player is too far down
	smb_block_buffer_colli_side(); // do player-to-bg collision detection on one half of player
	if (reg_p.z) { goto b_half; } // branch ahead if nothing found
	cmp_a(0x1c); // otherwise check for pipe metatiles
	if (reg_p.z) { goto b_half; } // if collided with sideways pipe (top), branch ahead
	cmp_a(0x6b);
	if (reg_p.z) { goto b_half; } // if collided with water pipe (top), branch ahead
	smb_check_for_climb_m_tiles(); // do sub to see if player bumped into anything climbable
	if (!reg_p.c) { goto chk_side_m_tiles; } // if not, branch to alternate section of code
b_half:;
	set_y(ram[0x00eb]); // load block adder offset
	set_y(reg_y+1); // increment it
	set_a(ram[smb_player_y_position]); // get player's vertical position
	cmp_a(0x08);
	if (!reg_p.c) { goto ex_sch; } // if too high, branch to leave
	cmp_a(0xd0);
	if (reg_p.c) { goto ex_sch; } // if too low, branch to leave
	smb_block_buffer_colli_side(); // do player-to-bg collision detection on other half of player
	if (!reg_p.z) { goto chk_side_m_tiles; } // if something found, branch
	ram[0x0000] = dec(ram[0x0000]); // otherwise decrement counter
	if (!reg_p.z) { goto side_check_loop; } // run code until both sides of player are checked
ex_sch:;
	return; // leave
chk_side_m_tiles:;
	smb_chk_invisible_m_tiles(); // check for hidden or coin 1-up blocks
	if (reg_p.z) { goto ex_csm; } // branch to leave if either found
	smb_check_for_climb_m_tiles(); // check for climbable metatiles
	if (!reg_p.c) { goto cont_s_chk; } // if not found, skip and continue with code
	smb_handle_climbing(); return; // otherwise jump to handle climbing
cont_s_chk:;
	smb_check_for_coin_m_tiles(); // check to see if player touched coin
	if (reg_p.c) { smb_handle_coin_metatile(); return; } // if so, execute code to erase coin and award to player 1 coin
	smb_chk_jumpspring_metatiles(); // check for jumpspring metatiles
	if (!reg_p.c) { goto chk_p_btm; } // if not found, branch ahead to continue cude
	set_a(ram[smb_jumpspring_anim_ctrl]); // otherwise check jumpspring animation control
	if (!reg_p.z) { goto ex_csm; } // branch to leave if set
	goto stop_player_move; // otherwise jump to impede player's movement
chk_p_btm:;
	set_y(ram[smb_player_state]); // get player's state
	cmp_y(0x00); // check for player's state set to normal
	if (!reg_p.z) { goto stop_player_move; } // if not, branch to impede player's movement
	set_y(ram[smb_player_facing_dir]); // get player's facing direction
	set_y(reg_y-1);
	if (!reg_p.z) { goto stop_player_move; } // if facing left, branch to impede movement
	cmp_a(0x6c); // otherwise check for pipe metatiles
	if (reg_p.z) { goto pipe_dwn_s; } // if collided with sideways pipe (bottom), branch
	cmp_a(0x1f); // if collided with water pipe (bottom), continue
	if (!reg_p.z) { goto stop_player_move; } // otherwise branch to impede player's movement
pipe_dwn_s:;
	set_a(ram[smb_player_spr_attrib]); // check player's attributes
	if (!reg_p.z) { goto plyr_pipe; } // if already set, branch, do not play sound again
	set_y((smb_sfx_pipe_down_injury));
	ram[smb_square_1_sound_queue] = reg_y; // otherwise load pipedown/injury sound
plyr_pipe:;
	or_a(0b00100000);
	ram[smb_player_spr_attrib] = reg_a; // set background priority bit in player attributes
	set_a(ram[smb_player_x_position]);
	and_a(0b00001111); // get lower nybble of player's horizontal coordinate
	if (reg_p.z) { goto chk_ge_rtn; } // if at zero, branch ahead to skip this part
	set_y(0x00); // set default offset for timer setting data
	set_a(ram[smb_screen_left_page_loc]); // load page location for left side of screen
	if (reg_p.z) { goto set_ca_tmr; } // if at page zero, use default offset
	set_y(reg_y+1); // otherwise increment offset
set_ca_tmr:;
	set_a(rom[smb_area_change_timer_data + reg_y]); // set timer for change of area as appropriate
	ram[smb_change_area_timer] = reg_a;
chk_ge_rtn:;
	set_a(ram[smb_game_engine_subroutine]); // get number of game engine routine running
	cmp_a(0x07);
	if (reg_p.z) { goto ex_csm; } // if running player entrance routine or
	cmp_a(0x08); // player control routine, go ahead and branch to leave
	if (!reg_p.z) { goto ex_csm; }
	set_a(0x02);
	ram[smb_game_engine_subroutine] = reg_a; // otherwise set sideways pipe entry routine to run
	return; // and leave
stop_player_move:;
	smb_impede_player_move(); // stop player's movement
ex_csm:;
	return; // leave
}

void smb_handle_coin_metatile() {
	smb_er_acm(); // do sub to erase coin metatile from block buffer
	ram[smb_coin_tally_for_1_ups] = inc(ram[smb_coin_tally_for_1_ups]); // increment coin tally used for 1-up blocks
	smb_give_one_coin(); return; // update coin amount and tally on the screen
	smb_handle_axe_metatile(); return;
}

void smb_handle_axe_metatile() {
	set_a(0x00);
	ram[smb_oper_mode_task] = reg_a; // reset secondary mode
	set_a(0x02);
	ram[smb_oper_mode] = reg_a; // set primary mode to autoctrl mode
	set_a(0x18);
	ram[smb_player_x_speed] = reg_a; // set horizontal speed and continue to erase axe metatile
	smb_er_acm(); return;
}

void smb_er_acm() {
	set_y(ram[0x0002]); // load vertical high nybble offset for block buffer
	set_a(0x00); // load blank metatile
	mem_w(*(uint16_t*)&ram[0x0006] + reg_y, reg_a); // store to remove old contents from block buffer
	smb_remove_coin_axe(); return; // update the screen accordingly
}

void smb_handle_climbing() {
	set_y(ram[0x0004]); // check low nybble of horizontal coordinate returned from
	cmp_y(0x06); // collision detection routine against certain values, this
	if (!reg_p.c) { goto ex_hc; } // makes actual physical part of vine or flagpole thinner
	cmp_y(0x0a); // than 16 pixels
	if (!reg_p.c) { goto chk_for_flagpole; }
ex_hc:;
	return; // leave if too far left or too far right
chk_for_flagpole:;
	cmp_a(0x24); // check climbing metatiles
	if (reg_p.z) { goto flagpole_collision; } // branch if flagpole ball found
	cmp_a(0x25);
	if (!reg_p.z) { goto vine_collision; } // branch to alternate code if flagpole shaft not found
flagpole_collision:;
	set_a(ram[smb_game_engine_subroutine]);
	cmp_a(0x05); // check for end-of-level routine running
	if (reg_p.z) { goto put_player_on_vine; } // if running, branch to end of climbing code
	set_a(0x01);
	ram[smb_player_facing_dir] = reg_a; // set player's facing direction to right
	ram[smb_scroll_lock] = inc(ram[smb_scroll_lock]); // set scroll lock flag
	set_a(ram[smb_game_engine_subroutine]);
	cmp_a(0x04); // check for flagpole slide routine running
	if (reg_p.z) { goto run_fr; } // if running, branch to end of flagpole code here
	set_a((smb_bullet_bill_cannon_var)); // load identifier for bullet bills (cannon variant)
	smb_kill_enemies(); // get rid of them
	set_a((smb_silence));
	ram[smb_event_music_queue] = reg_a; // silence music
	reg_a = shr(reg_a);
	ram[smb_flagpole_sound_queue] = reg_a; // load flagpole sound into flagpole sound queue
	set_x(0x04); // start at end of vertical coordinate data
	set_a(ram[smb_player_y_position]);
	ram[smb_flagpole_collision_y_pos] = reg_a; // store player's vertical coordinate here to be used later
chk_flagpole_y_pos_loop:;
	cmp_a(rom[smb_flagpole_y_pos_data + reg_x]); // compare with current vertical coordinate data
	if (reg_p.c) { goto mtch_f; } // if player's => current, branch to use current offset
	set_x(reg_x-1); // otherwise decrement offset to use
	if (!reg_p.z) { goto chk_flagpole_y_pos_loop; } // do this until all data is checked (use last one if all checked)
mtch_f:;
	ram[smb_flagpole_score] = reg_x; // store offset here to be used later
run_fr:;
	set_a(0x04);
	ram[smb_game_engine_subroutine] = reg_a; // set value to run flagpole slide routine
	goto put_player_on_vine; // jump to end of climbing code
vine_collision:;
	cmp_a(0x26); // check for climbing metatile used on vines
	if (!reg_p.z) { goto put_player_on_vine; }
	set_a(ram[smb_player_y_position]); // check player's vertical coordinate
	cmp_a(0x20); // for being in status bar area
	if (reg_p.c) { goto put_player_on_vine; } // branch if not that far up
	set_a(0x01);
	ram[smb_game_engine_subroutine] = reg_a; // otherwise set to run autoclimb routine next frame
put_player_on_vine:;
	set_a(0x03); // set player state to climbing
	ram[smb_player_state] = reg_a;
	set_a(0x00); // nullify player's horizontal speed
	ram[smb_player_x_speed] = reg_a; // and fractional horizontal movement force
	ram[smb_player_x_move_force] = reg_a;
	set_a(ram[smb_player_x_position]); // get player's horizontal coordinate
	reg_p.c = 1;
	sub_a(ram[smb_screen_left_x_pos]); // subtract from left side horizontal coordinate
	cmp_a(0x10);
	if (reg_p.c) { goto set_vx_pl; } // if 16 or more pixels difference, do not alter facing direction
	set_a(0x02);
	ram[smb_player_facing_dir] = reg_a; // otherwise force player to face left
set_vx_pl:;
	set_y(ram[smb_player_facing_dir]); // get current facing direction, use as offset
	set_a(ram[0x0006]); // get low byte of block buffer address
	reg_a = shl(reg_a);
	reg_a = shl(reg_a); // move low nybble to high
	reg_a = shl(reg_a);
	reg_a = shl(reg_a);
	reg_p.c = 0;
	add_a(rom[smb_climb_x_pos_adder-1 + reg_y]); // add pixels depending on facing direction
	ram[smb_player_x_position] = reg_a; // store as player's horizontal coordinate
	set_a(ram[0x0006]); // get low byte of block buffer address again
	if (!reg_p.z) { goto ex_p_vne; } // if not zero, branch
	set_a(ram[smb_screen_right_page_loc]); // load page location of right side of screen
	reg_p.c = 0;
	add_a(rom[smb_climb_p_loc_adder-1 + reg_y]); // add depending on facing location
	ram[smb_player_page_loc] = reg_a; // store as player's page location
ex_p_vne:;
	return; // finally, we're done!
	smb_chk_invisible_m_tiles(); return;
}

void smb_chk_invisible_m_tiles() {
	cmp_a(0x5f); // check for hidden coin block
	if (reg_p.z) { goto ex_c_inv_t; } // branch to leave if found
	cmp_a(0x60); // check for hidden 1-up block
ex_c_inv_t:;
	return; // leave with zero flag set if either found
	smb_chk_for_land_jump_spring(); return;
}

void smb_chk_for_land_jump_spring() {
	smb_chk_jumpspring_metatiles(); // do sub to check if player landed on jumpspring
	if (!reg_p.c) { goto ex_cj_sp; } // if carry not set, jumpspring not found, therefore leave
	set_a(0x70);
	ram[smb_vertical_force] = reg_a; // otherwise set vertical movement force for player
	set_a(0xf9);
	ram[smb_jumpspring_force] = reg_a; // set default jumpspring force
	set_a(0x03);
	ram[smb_jumpspring_timer] = reg_a; // set jumpspring timer to be used later
	reg_a = shr(reg_a);
	ram[smb_jumpspring_anim_ctrl] = reg_a; // set jumpspring animation control to start animating
ex_cj_sp:;
	return; // and leave
	smb_chk_jumpspring_metatiles(); return;
}

void smb_chk_jumpspring_metatiles() {
	cmp_a(0x67); // check for top jumpspring metatile
	if (reg_p.z) { goto js_fnd; } // branch to set carry if found
	cmp_a(0x68); // check for bottom jumpspring metatile
	reg_p.c = 0; // clear carry flag
	if (!reg_p.z) { goto no_js_fnd; } // branch to use cleared carry if not found
js_fnd:;
	reg_p.c = 1; // set carry if found
no_js_fnd:;
	return; // leave
	smb_handle_pipe_entry(); return;
}

void smb_handle_pipe_entry() {
	set_a(ram[smb_up_down_buttons]); // check saved controller bits from earlier
	and_a(0b00000100); // for pressing down
	if (reg_p.z) { goto ex_pipe_e; } // if not pressing down, branch to leave
	set_a(ram[0x0000]);
	cmp_a(0x11); // check right foot metatile for warp pipe right metatile
	if (!reg_p.z) { goto ex_pipe_e; } // branch to leave if not found
	set_a(ram[0x0001]);
	cmp_a(0x10); // check left foot metatile for warp pipe left metatile
	if (!reg_p.z) { goto ex_pipe_e; } // branch to leave if not found
	set_a(0x30);
	ram[smb_change_area_timer] = reg_a; // set timer for change of area
	set_a(0x03);
	ram[smb_game_engine_subroutine] = reg_a; // set to run vertical pipe entry routine on next frame
	set_a((smb_sfx_pipe_down_injury));
	ram[smb_square_1_sound_queue] = reg_a; // load pipedown/injury sound
	set_a(0b00100000);
	ram[smb_player_spr_attrib] = reg_a; // set background priority bit in player's attributes
	set_a(ram[smb_warp_zone_control]); // check warp zone control
	if (reg_p.z) { goto ex_pipe_e; } // branch to leave if none found
	and_a(0b00000011); // mask out all but 2 LSB
	reg_a = shl(reg_a);
	reg_a = shl(reg_a); // multiply by four
	set_x(reg_a); // save as offset to warp zone numbers (starts at left pipe)
	set_a(ram[smb_player_x_position]); // get player's horizontal position
	cmp_a(0x60);
	if (!reg_p.c) { goto get_w_num; } // if player at left, not near middle, use offset and skip ahead
	set_x(reg_x+1); // otherwise increment for middle pipe
	cmp_a(0xa0);
	if (!reg_p.c) { goto get_w_num; } // if player at middle, but not too far right, use offset and skip
	set_x(reg_x+1); // otherwise increment for last pipe
get_w_num:;
	set_y(rom[smb_warp_zone_numbers + reg_x]); // get warp zone numbers
	set_y(reg_y-1); // decrement for use as world number
	ram[smb_world_number] = reg_y; // store as world number and offset
	set_x(rom[smb_world_addr_offsets + reg_y]); // get offset to where this world's area offsets are
	set_a(rom[smb_area_addr_offsets + reg_x]); // get area offset based on world offset
	ram[smb_area_pointer] = reg_a; // store area offset here to be used to change areas
	set_a(0x80);
	ram[smb_event_music_queue] = reg_a; // silence music
	set_a(0x00);
	ram[smb_entrance_page] = reg_a; // initialize starting page number
	ram[smb_area_number] = reg_a; // initialize area number used for area address offset
	ram[smb_level_number] = reg_a; // initialize level number used for world display
	ram[smb_alt_entrance_control] = reg_a; // initialize mode of entry
	ram[smb_hidden_1_up_flag] = inc(ram[smb_hidden_1_up_flag]); // set flag for hidden 1-up blocks
	ram[smb_fetch_new_game_timer_flag] = inc(ram[smb_fetch_new_game_timer_flag]); // set flag to load new game timer
ex_pipe_e:;
	return; // leave!!!
	smb_impede_player_move(); return;
}

void smb_impede_player_move() {
	set_a(0x00); // initialize value here
	set_y(ram[smb_player_x_speed]); // get player's horizontal speed
	set_x(ram[0x0000]); // check value set earlier for
	set_x(reg_x-1); // left side collision
	if (!reg_p.z) { goto r_impd; } // if right side collision, skip this part
	set_x(reg_x+1); // return value to X
	cmp_y(0x00); // if player moving to the left,
	if (reg_p.n) { goto ex_ipm; } // branch to invert bit and leave
	set_a(0xff); // otherwise load A with value to be used later
	goto nx_spd; // and jump to affect movement
r_impd:;
	set_x(0x02); // return $02 to X
	cmp_y(0x01); // if player moving to the right,
	if (!reg_p.n) { goto ex_ipm; } // branch to invert bit and leave
	set_a(0x01); // otherwise load A with value to be used here
nx_spd:;
	set_y(0x10);
	ram[smb_side_collision_timer] = reg_y; // set timer of some sort
	set_y(0x00);
	ram[smb_player_x_speed] = reg_y; // nullify player's horizontal speed
	cmp_a(0x00); // if value set in A not set to $ff,
	if (!reg_p.n) { goto plat_f; } // branch ahead, do not decrement Y
	set_y(reg_y-1); // otherwise decrement Y now
plat_f:;
	ram[0x0000] = reg_y; // store Y as high bits of horizontal adder
	reg_p.c = 0;
	add_a(ram[smb_player_x_position]); // add contents of A to player's horizontal
	ram[smb_player_x_position] = reg_a; // position to move player left or right
	set_a(ram[smb_player_page_loc]);
	add_a(ram[0x0000]); // add high bits and carry to
	ram[smb_player_page_loc] = reg_a; // page location if necessary
ex_ipm:;
	set_a(reg_x); // invert contents of X
	eor_a(0xff);
	and_a(ram[smb_player_collision_bits]); // mask out bit that was set here
	ram[smb_player_collision_bits] = reg_a; // store to clear bit
	return;
}

void smb_check_for_solid_m_tiles() {
	smb_get_m_tile_attrib(); // find appropriate offset based on metatile's 2 MSB
	cmp_a(rom[smb_solid_m_tile_upper_ext + reg_x]); // compare current metatile with solid metatiles
	return;
}

void smb_check_for_climb_m_tiles() {
	smb_get_m_tile_attrib(); // find appropriate offset based on metatile's 2 MSB
	cmp_a(rom[smb_climb_m_tile_upper_ext + reg_x]); // compare current metatile with climbable metatiles
	return;
	smb_check_for_coin_m_tiles(); return;
}

void smb_check_for_coin_m_tiles() {
	cmp_a(0xc2); // check for regular coin
	if (reg_p.z) { goto coin_sd; } // branch if found
	cmp_a(0xc3); // check for underwater coin
	if (reg_p.z) { goto coin_sd; } // branch if found
	reg_p.c = 0; // otherwise clear carry and leave
	return;
coin_sd:;
	set_a((smb_sfx_coin_grab));
	ram[smb_square_2_sound_queue] = reg_a; // load coin grab sound and leave
	return;
	smb_get_m_tile_attrib(); return;
}

void smb_get_m_tile_attrib() {
	set_y(reg_a); // save metatile value into Y
	and_a(0b11000000); // mask out all but 2 MSB
	reg_a = shl(reg_a);
	reg_a = rol(reg_a); // shift and rotate d7-d6 to d1-d0
	reg_a = rol(reg_a);
	set_x(reg_a); // use as offset for metatile data
	set_a(reg_y); // get original metatile value back
	smb_ex_ebg(); return;
}

void smb_ex_ebg() {
	return; // leave
}

void smb_enemy_to_bg_collision_det() {
	set_a(ram[smb_enemy_state + reg_x]); // check enemy state for d6 set
	and_a(0b00100000);
	if (!reg_p.z) { smb_ex_ebg(); return; } // if set, branch to leave
	smb_subt_enemy_y_pos(); // otherwise, do a subroutine here
	if (!reg_p.c) { smb_ex_ebg(); return; } // if enemy vertical coord + 62 < 68, branch to leave
	set_y(ram[smb_enemy_id + reg_x]);
	cmp_y((smb_spiny)); // if enemy object is not spiny, branch elsewhere
	if (!reg_p.z) { goto do_id_check_bg_coll; }
	set_a(ram[smb_enemy_y_position + reg_x]);
	cmp_a(0x25); // if enemy vertical coordinate < 36 branch to leave
	if (!reg_p.c) { smb_ex_ebg(); return; }
do_id_check_bg_coll:;
	cmp_y((smb_green_paratroopa_jump)); // check for some other enemy object
	if (!reg_p.z) { goto hb_chk; } // branch if not found
	smb_enemy_jump(); return; // otherwise jump elsewhere
hb_chk:;
	cmp_y((smb_hammer_bro)); // check for hammer bro
	if (!reg_p.z) { goto c_invu; } // branch if not found
	smb_hammer_bro_bg_coll(); return; // otherwise jump elsewhere
c_invu:;
	cmp_y((smb_spiny)); // if enemy object is spiny, branch
	if (reg_p.z) { goto yes_in; }
	cmp_y((smb_power_up_object)); // if special power-up object, branch
	if (reg_p.z) { goto yes_in; }
	cmp_y(0x07); // if enemy object =>$07, branch to leave
	if (reg_p.c) { smb_ex_ebg_chk(); return; }
yes_in:;
	smb_chk_under_enemy(); // if enemy object < $07, or = $12 or $2e, do this sub
	if (!reg_p.z) { goto handle_e_to_bg_collision; } // if block underneath enemy, branch
no_e_to_bg_collision:;
	smb_chk_for_red_koopa(); return; // otherwise skip and do something else
handle_e_to_bg_collision:;
	smb_chk_for_non_solids(); // if something is underneath enemy, find out what
	if (reg_p.z) { goto no_e_to_bg_collision; } // if blank $26, coins, or hidden blocks, jump, enemy falls through
	cmp_a(0x23);
	if (!reg_p.z) { smb_land_enemy_properly(); return; } // check for blank metatile $23 and branch if not found
	set_y(ram[0x0002]); // get vertical coordinate used to find block
	set_a(0x00); // store default blank metatile in that spot so we won't
	mem_w(*(uint16_t*)&ram[0x0006] + reg_y, reg_a); // trigger this routine accidentally again
	set_a(ram[smb_enemy_id + reg_x]);
	cmp_a(0x15); // if enemy object => $15, branch ahead
	if (reg_p.c) { smb_chk_to_stun_enemies(); return; }
	cmp_a((smb_goomba)); // if enemy object not goomba, branch ahead of this routine
	if (!reg_p.z) { goto give_oe_points; }
	smb_kill_enemy_above_block(); // if enemy object IS goomba, do this sub
give_oe_points:;
	set_a(0x01); // award 100 points for hitting block beneath enemy
	smb_setup_floatey_number();
	smb_chk_to_stun_enemies(); return;
}

void smb_chk_to_stun_enemies() {
	cmp_a(0x09); // perform many comparisons on enemy object identifier
	if (!reg_p.c) { smb_set_stun(); return; }
	cmp_a(0x11); // if the enemy object identifier is equal to the values
	if (reg_p.c) { smb_set_stun(); return; } // $09, $0e, $0f or $10, it will be modified, and not
	cmp_a(0x0a); // modified if not any of those values, note that piranha plant will
	if (!reg_p.c) { goto demote; } // always fail this test because A will still have vertical
	cmp_a((smb_piranha_plant)); // coordinate from previous addition, also these comparisons
	if (!reg_p.c) { smb_set_stun(); return; } // are only necessary if branching from $d7a1
demote:;
	and_a(0b00000001); // erase all but LSB, essentially turning enemy object
	ram[smb_enemy_id + reg_x] = reg_a; // into green or red koopa troopa to demote them
	smb_set_stun(); return;
}

void smb_set_stun() {
	set_a(ram[smb_enemy_state + reg_x]); // load enemy state
	and_a(0b11110000); // save high nybble
	or_a(0b00000010);
	ram[smb_enemy_state + reg_x] = reg_a; // set d1 of enemy state
	ram[smb_enemy_y_position + reg_x] = dec(ram[smb_enemy_y_position + reg_x]);
	ram[smb_enemy_y_position + reg_x] = dec(ram[smb_enemy_y_position + reg_x]); // subtract two pixels from enemy's vertical position
	set_a(ram[smb_enemy_id + reg_x]);
	cmp_a((smb_bloober)); // check for bloober object
	if (reg_p.z) { goto set_wy_spd; }
	set_a(0xfd); // set default vertical speed
	set_y(ram[smb_area_type]);
	if (!reg_p.z) { goto set_not_w; } // if area type not water, set as speed, otherwise
set_wy_spd:;
	set_a(0xff); // change the vertical speed
set_not_w:;
	ram[smb_enemy_y_speed + reg_x] = reg_a; // set vertical speed now
	set_y(0x01);
	smb_player_enemy_diff(); // get horizontal difference between player and enemy object
	if (!reg_p.n) { goto chk_b_bill; } // branch if enemy is to the right of player
	set_y(reg_y+1); // increment Y if not
chk_b_bill:;
	set_a(ram[smb_enemy_id + reg_x]);
	cmp_a((smb_bullet_bill_cannon_var)); // check for bullet bill (cannon variant)
	if (reg_p.z) { goto no_c_dir_f; }
	cmp_a((smb_bullet_bill_frenzy_var)); // check for bullet bill (frenzy variant)
	if (reg_p.z) { goto no_c_dir_f; } // branch if either found, direction does not change
	ram[smb_enemy_moving_dir + reg_x] = reg_y; // store as moving direction
no_c_dir_f:;
	set_y(reg_y-1); // decrement and use as offset
	set_a(rom[smb_enemy_bgcx_spd_data + reg_y]); // get proper horizontal speed
	ram[smb_enemy_x_speed + reg_x] = reg_a; // and store, then leave
	smb_ex_ebg_chk(); return;
}

void smb_ex_ebg_chk() {
	return;
	smb_land_enemy_properly(); return;
}

void smb_land_enemy_properly() {
	set_a(ram[0x0004]); // check lower nybble of vertical coordinate saved earlier
	reg_p.c = 1;
	sub_a(0x08); // subtract eight pixels
	cmp_a(0x05); // used to determine whether enemy landed from falling
	if (reg_p.c) { smb_chk_for_red_koopa(); return; } // branch if lower nybble in range of $0d-$0f before subtract
	set_a(ram[smb_enemy_state + reg_x]);
	and_a(0b01000000); // branch if d6 in enemy state is set
	if (!reg_p.z) { goto land_enemy_init_state; }
	set_a(ram[smb_enemy_state + reg_x]);
	reg_a = shl(reg_a); // branch if d7 in enemy state is not set
	if (!reg_p.c) { goto chk_landed_enemy_state; }
s_chk_a:;
	smb_do_enemy_side_check(); return; // if lower nybble < $0d, d7 set but d6 not set, jump here
chk_landed_enemy_state:;
	set_a(ram[smb_enemy_state + reg_x]); // if enemy in normal state, branch back to jump here
	if (reg_p.z) { goto s_chk_a; }
	cmp_a(0x05); // if in state used by spiny's egg
	if (reg_p.z) { goto proc_enemy_direction; } // then branch elsewhere
	cmp_a(0x03); // if already in state used by koopas and buzzy beetles
	if (reg_p.c) { goto ex_ste_chk; } // or in higher numbered state, branch to leave
	set_a(ram[smb_enemy_state + reg_x]); // load enemy state again (why?)
	cmp_a(0x02); // if not in $02 state (used by koopas and buzzy beetles)
	if (!reg_p.z) { goto proc_enemy_direction; } // then branch elsewhere
	set_a(0x10); // load default timer here
	set_y(ram[smb_enemy_id + reg_x]); // check enemy identifier for spiny
	cmp_y((smb_spiny));
	if (!reg_p.z) { goto set_for_stn; } // branch if not found
	set_a(0x00); // set timer for $00 if spiny
set_for_stn:;
	ram[smb_enemy_interval_timer + reg_x] = reg_a; // set timer here
	set_a(0x03); // set state here, apparently used to render
	ram[smb_enemy_state + reg_x] = reg_a; // upside-down koopas and buzzy beetles
	smb_enemy_landing(); // then land it properly
ex_ste_chk:;
	return; // then leave
proc_enemy_direction:;
	set_a(ram[smb_enemy_id + reg_x]); // check enemy identifier for goomba
	cmp_a((smb_goomba)); // branch if found
	if (reg_p.z) { goto land_enemy_init_state; }
	cmp_a((smb_spiny)); // check for spiny
	if (!reg_p.z) { goto invt_d; } // branch if not found
	set_a(0x01);
	ram[smb_enemy_moving_dir + reg_x] = reg_a; // send enemy moving to the right by default
	set_a(0x08);
	ram[smb_enemy_x_speed + reg_x] = reg_a; // set horizontal speed accordingly
	set_a(ram[smb_frame_counter]);
	and_a(0b00000111); // if timed appropriately, spiny will skip over
	if (reg_p.z) { goto land_enemy_init_state; } // trying to face the player
invt_d:;
	set_y(0x01); // load 1 for enemy to face the left (inverted here)
	smb_player_enemy_diff(); // get horizontal difference between player and enemy
	if (!reg_p.n) { goto c_nw_c_dir; } // if enemy to the right of player, branch
	set_y(reg_y+1); // if to the left, increment by one for enemy to face right (inverted)
c_nw_c_dir:;
	set_a(reg_y);
	cmp_a(ram[smb_enemy_moving_dir + reg_x]); // compare direction in A with current direction in memory
	if (!reg_p.z) { goto land_enemy_init_state; }
	smb_chk_for_bump_hammer_bro_j(); // if equal, not facing in correct dir, do sub to turn around
land_enemy_init_state:;
	smb_enemy_landing(); // land enemy properly
	set_a(ram[smb_enemy_state + reg_x]);
	and_a(0b10000000); // if d7 of enemy state is set, branch
	if (!reg_p.z) { goto n_mov_shell_fall_bit; }
	set_a(0x00); // otherwise initialize enemy state and leave
	ram[smb_enemy_state + reg_x] = reg_a; // note this will also turn spiny's egg into spiny
	return;
n_mov_shell_fall_bit:;
	set_a(ram[smb_enemy_state + reg_x]); // nullify d6 of enemy state, save other bits
	and_a(0b10111111); // and store, then leave
	ram[smb_enemy_state + reg_x] = reg_a;
	return;
	smb_chk_for_red_koopa(); return;
}

void smb_chk_for_red_koopa() {
	set_a(ram[smb_enemy_id + reg_x]); // check for red koopa troopa $03
	cmp_a((smb_red_koopa));
	if (!reg_p.z) { goto chk_2_msb_st; } // branch if not found
	set_a(ram[smb_enemy_state + reg_x]);
	if (reg_p.z) { smb_chk_for_bump_hammer_bro_j(); return; } // if enemy found and in normal state, branch
chk_2_msb_st:;
	set_a(ram[smb_enemy_state + reg_x]); // save enemy state into Y
	set_y(reg_a);
	reg_a = shl(reg_a); // check for d7 set
	if (!reg_p.c) { goto get_ste_from_d; } // branch if not set
	set_a(ram[smb_enemy_state + reg_x]);
	or_a(0b01000000); // set d6
	goto set_d_6_ste; // jump ahead of this part
get_ste_from_d:;
	set_a(rom[smb_enemy_bgc_state_data + reg_y]); // load new enemy state with old as offset
set_d_6_ste:;
	ram[smb_enemy_state + reg_x] = reg_a; // set as new state
	smb_do_enemy_side_check(); return;
}

void smb_do_enemy_side_check() {
	set_a(ram[smb_enemy_y_position + reg_x]); // if enemy within status bar, branch to leave
	cmp_a(0x20); // because there's nothing there that impedes movement
	if (!reg_p.c) { goto ex_e_sde_c; }
	set_y(0x16); // start by finding block to the left of enemy ($00,$14)
	set_a(0x02); // set value here in what is also used as
	ram[0x00eb] = reg_a; // OAM data offset
sde_c_loop:;
	set_a(ram[0x00eb]); // check value
	cmp_a(ram[smb_enemy_moving_dir + reg_x]); // compare value against moving direction
	if (!reg_p.z) { goto next_sde_c; } // branch if different and do not seek block there
	set_a(0x01); // set flag in A for save horizontal coordinate
	smb_block_buffer_chk_enemy(); // find block to left or right of enemy object
	if (reg_p.z) { goto next_sde_c; } // if nothing found, branch
	smb_chk_for_non_solids(); // check for non-solid blocks
	if (!reg_p.z) { smb_chk_for_bump_hammer_bro_j(); return; } // branch if not found
next_sde_c:;
	ram[0x00eb] = dec(ram[0x00eb]); // move to the next direction
	set_y(reg_y+1);
	cmp_y(0x18); // increment Y, loop only if Y < $18, thus we check
	if (!reg_p.c) { goto sde_c_loop; } // enemy ($00, $14) and ($10, $14) pixel coordinates
ex_e_sde_c:;
	return;
	smb_chk_for_bump_hammer_bro_j(); return;
}

void smb_chk_for_bump_hammer_bro_j() {
	cmp_x(0x05); // check if we're on the special use slot
	if (reg_p.z) { goto no_bump; } // and if so, branch ahead and do not play sound
	set_a(ram[smb_enemy_state + reg_x]); // if enemy state d7 not set, branch
	reg_a = shl(reg_a); // ahead and do not play sound
	if (!reg_p.c) { goto no_bump; }
	set_a((smb_sfx_bump)); // otherwise, play bump sound
	ram[smb_square_1_sound_queue] = reg_a; // sound will never be played if branching from smb_chk_for_red_koopa
no_bump:;
	set_a(ram[smb_enemy_id + reg_x]); // check for hammer bro
	cmp_a((smb_hammer_bro));
	if (!reg_p.z) { goto inv_enemy_dir; } // branch if not found
	set_a(0x00);
	ram[0x0000] = reg_a; // initialize value here for bitmask
	set_y(0xfa); // load default vertical speed for jumping
	smb_set_hj(); return; // jump to code that makes hammer bro jump
inv_enemy_dir:;
	smb_rx_spd(); return; // jump to turn the enemy around
	smb_player_enemy_diff(); return;
}

void smb_player_enemy_diff() {
	set_a(ram[smb_enemy_x_position + reg_x]); // get distance between enemy object's
	reg_p.c = 1; // horizontal coordinate and the player's
	sub_a(ram[smb_player_x_position]); // horizontal coordinate
	ram[0x0000] = reg_a; // and store here
	set_a(ram[smb_enemy_page_loc + reg_x]);
	sub_a(ram[smb_player_page_loc]); // subtract borrow, then leave
	return;
	smb_enemy_landing(); return;
}

void smb_enemy_landing() {
	smb_init_v_stf(); // do something here to vertical speed and something else
	set_a(ram[smb_enemy_y_position + reg_x]);
	and_a(0b11110000); // save high nybble of vertical coordinate, and
	or_a(0b00001000); // set d3, then store, probably used to set enemy object
	ram[smb_enemy_y_position + reg_x] = reg_a; // neatly on whatever it's landing on
	return;
	smb_subt_enemy_y_pos(); return;
}

void smb_subt_enemy_y_pos() {
	set_a(ram[smb_enemy_y_position + reg_x]); // add 62 pixels to enemy object's
	reg_p.c = 0; // vertical coordinate
	add_a(0x3e);
	cmp_a(0x44); // compare against a certain range
	return; // and leave with flags set for conditional branch
	smb_enemy_jump(); return;
}

void smb_enemy_jump() {
	smb_subt_enemy_y_pos(); // do a sub here
	if (!reg_p.c) { goto do_side; } // if enemy vertical coord + 62 < 68, branch to leave
	set_a(ram[smb_enemy_y_speed + reg_x]);
	reg_p.c = 0; // add two to vertical speed
	add_a(0x02);
	cmp_a(0x03); // if green paratroopa not falling, branch ahead
	if (!reg_p.c) { goto do_side; }
	smb_chk_under_enemy(); // otherwise, check to see if green paratroopa is
	if (reg_p.z) { goto do_side; } // standing on anything, then branch to same place if not
	smb_chk_for_non_solids(); // check for non-solid blocks
	if (reg_p.z) { goto do_side; } // branch if found
	smb_enemy_landing(); // change vertical coordinate and speed
	set_a(0xfd);
	ram[smb_enemy_y_speed + reg_x] = reg_a; // make the paratroopa jump again
do_side:;
	smb_do_enemy_side_check(); return; // check for horizontal blockage, then leave
	smb_hammer_bro_bg_coll(); return;
}

void smb_hammer_bro_bg_coll() {
	smb_chk_under_enemy(); // check to see if hammer bro is standing on anything
	if (reg_p.z) { smb_no_under_hammer_bro(); return; }
	cmp_a(0x23); // check for blank metatile $23 and branch if not found
	if (!reg_p.z) { smb_under_hammer_bro(); return; }
	smb_kill_enemy_above_block(); return;
}

void smb_kill_enemy_above_block() {
	smb_shell_or_block_defeat(); // do this sub to kill enemy
	set_a(0xfc); // alter vertical speed of enemy and leave
	ram[smb_enemy_y_speed + reg_x] = reg_a;
	return;
	smb_under_hammer_bro(); return;
}

void smb_under_hammer_bro() {
	set_a(ram[smb_enemy_frame_timer + reg_x]); // check timer used by hammer bro
	if (!reg_p.z) { smb_no_under_hammer_bro(); return; } // branch if not expired
	set_a(ram[smb_enemy_state + reg_x]);
	and_a(0b10001000); // save d7 and d3 from enemy state, nullify other bits
	ram[smb_enemy_state + reg_x] = reg_a; // and store
	smb_enemy_landing(); // modify vertical coordinate, speed and something else
	smb_do_enemy_side_check(); return; // then check for horizontal blockage and leave
	smb_no_under_hammer_bro(); return;
}

void smb_no_under_hammer_bro() {
	set_a(ram[smb_enemy_state + reg_x]); // if hammer bro is not standing on anything, set d0
	or_a(0x01); // in the enemy state to indicate jumping or falling, then leave
	ram[smb_enemy_state + reg_x] = reg_a;
	return;
	smb_chk_under_enemy(); return;
}

void smb_chk_under_enemy() {
	set_a(0x00); // set flag in A for save vertical coordinate
	set_y(0x15); // set Y to check the bottom middle (8,18) of enemy object
	smb_block_buffer_chk_enemy(); return; // hop to it!
	smb_chk_for_non_solids(); return;
}

void smb_chk_for_non_solids() {
	cmp_a(0x26); // blank metatile used for vines?
	if (reg_p.z) { goto ns_fnd; }
	cmp_a(0xc2); // regular coin?
	if (reg_p.z) { goto ns_fnd; }
	cmp_a(0xc3); // underwater coin?
	if (reg_p.z) { goto ns_fnd; }
	cmp_a(0x5f); // hidden coin block?
	if (reg_p.z) { goto ns_fnd; }
	cmp_a(0x60); // hidden 1-up block?
ns_fnd:;
	return;
	smb_fireball_bg_collision(); return;
}

void smb_fireball_bg_collision() {
	set_a(ram[smb_fireball_y_position + reg_x]); // check fireball's vertical coordinate
	cmp_a(0x18);
	if (!reg_p.c) { goto clear_bounce_flag; } // if within the status bar area of the screen, branch ahead
	smb_block_buffer_chk_f_ball(); // do fireball to background collision detection on bottom of it
	if (reg_p.z) { goto clear_bounce_flag; } // if nothing underneath fireball, branch
	smb_chk_for_non_solids(); // check for non-solid metatiles
	if (reg_p.z) { goto clear_bounce_flag; } // branch if any found
	set_a(ram[smb_fireball_y_speed + reg_x]); // if fireball's vertical speed set to move upwards,
	if (reg_p.n) { goto init_fireball_explode; } // branch to set exploding bit in fireball's state
	set_a(ram[smb_fireball_bouncing_flag + reg_x]); // if bouncing flag already set,
	if (!reg_p.z) { goto init_fireball_explode; } // branch to set exploding bit in fireball's state
	set_a(0xfd);
	ram[smb_fireball_y_speed + reg_x] = reg_a; // otherwise set vertical speed to move upwards (give it bounce)
	set_a(0x01);
	ram[smb_fireball_bouncing_flag + reg_x] = reg_a; // set bouncing flag
	set_a(ram[smb_fireball_y_position + reg_x]);
	and_a(0xf8); // modify vertical coordinate to land it properly
	ram[smb_fireball_y_position + reg_x] = reg_a; // store as new vertical coordinate
	return; // leave
clear_bounce_flag:;
	set_a(0x00);
	ram[smb_fireball_bouncing_flag + reg_x] = reg_a; // clear bouncing flag by default
	return; // leave
init_fireball_explode:;
	set_a(0x80);
	ram[smb_fireball_state + reg_x] = reg_a; // set exploding flag in fireball's state
	set_a((smb_sfx_bump));
	ram[smb_square_1_sound_queue] = reg_a; // load bump sound
	return; // leave
}

void smb_get_fireball_bound_box() {
	set_a(reg_x); // add seven bytes to offset
	reg_p.c = 0; // to use in routines as offset for fireball
	add_a(0x07);
	set_x(reg_a);
	set_y(0x02); // set offset for relative coordinates
	if (!reg_p.z) { smb_f_ball_b(); return; } // unconditional branch
	smb_get_misc_bound_box(); return;
}

void smb_get_misc_bound_box() {
	set_a(reg_x); // add nine bytes to offset
	reg_p.c = 0; // to use in routines as offset for misc object
	add_a(0x09);
	set_x(reg_a);
	set_y(0x06); // set offset for relative coordinates
	smb_f_ball_b(); return;
}

void smb_f_ball_b() {
	smb_bounding_box_core(); // get bounding box coordinates
	smb_check_right_screen_b_box(); return; // jump to handle any offscreen coordinates
	smb_get_enemy_bound_box(); return;
}

void smb_get_enemy_bound_box() {
	set_y(0x48); // store bitmask here for now
	ram[0x0000] = reg_y;
	set_y(0x44); // store another bitmask here for now and jump
	smb_get_masked_off_scr_bits(); return;
	smb_small_platform_bound_box(); return;
}

void smb_small_platform_bound_box() {
	set_y(0x08); // store bitmask here for now
	ram[0x0000] = reg_y;
	set_y(0x04); // store another bitmask here for now
	smb_get_masked_off_scr_bits(); return;
}

void smb_get_masked_off_scr_bits() {
	set_a(ram[smb_enemy_x_position + reg_x]); // get enemy object position relative
	reg_p.c = 1; // to the left side of the screen
	sub_a(ram[smb_screen_left_x_pos]);
	ram[0x0001] = reg_a; // store here
	set_a(ram[smb_enemy_page_loc + reg_x]); // subtract borrow from current page location
	sub_a(ram[smb_screen_left_page_loc]); // of left side
	if (reg_p.n) { goto cm_bits; } // if enemy object is beyond left edge, branch
	or_a(ram[0x0001]);
	if (reg_p.z) { goto cm_bits; } // if precisely at the left edge, branch
	set_y(ram[0x0000]); // if to the right of left edge, use value in $00 for A
cm_bits:;
	set_a(reg_y); // otherwise use contents of Y
	and_a(ram[smb_enemy_offscreen_bits]); // preserve bitwise whatever's in here
	ram[smb_enemy_offscr_bits_masked + reg_x] = reg_a; // save masked offscreen bits here
	if (!reg_p.z) { smb_move_bound_box_offscreen(); return; } // if anything set here, branch
	smb_setup_e_offset_fb_box(); return; // otherwise, do something else
	smb_large_platform_bound_box(); return;
}

void smb_large_platform_bound_box() {
	set_x(reg_x+1); // increment X to get the proper offset
	smb_get_x_offscreen_bits(); // then jump directly to the sub for horizontal offscreen bits
	set_x(reg_x-1); // decrement to return to original offset
	cmp_a(0xfe); // if completely offscreen, branch to put entire bounding
	if (reg_p.c) { smb_move_bound_box_offscreen(); return; } // box offscreen, otherwise start getting coordinates
	smb_setup_e_offset_fb_box(); return;
}

void smb_setup_e_offset_fb_box() {
	set_a(reg_x); // add 1 to offset to properly address
	reg_p.c = 0; // the enemy object memory locations
	add_a(0x01);
	set_x(reg_a);
	set_y(0x01); // load 1 as offset here, same reason
	smb_bounding_box_core(); // do a sub to get the coordinates of the bounding box
	smb_check_right_screen_b_box(); return; // jump to handle offscreen coordinates of bounding box
	smb_move_bound_box_offscreen(); return;
}

void smb_move_bound_box_offscreen() {
	set_a(reg_x); // multiply offset by 4
	reg_a = shl(reg_a);
	reg_a = shl(reg_a);
	set_y(reg_a); // use as offset here
	set_a(0xff);
	ram[smb_enemy_bounding_box_coord + reg_y] = reg_a; // load value into four locations here and leave
	ram[smb_enemy_bounding_box_coord+1 + reg_y] = reg_a;
	ram[smb_enemy_bounding_box_coord+2 + reg_y] = reg_a;
	ram[smb_enemy_bounding_box_coord+3 + reg_y] = reg_a;
	return;
	smb_bounding_box_core(); return;
}

void smb_bounding_box_core() {
	ram[0x0000] = reg_x; // save offset here
	set_a(ram[smb_spr_object_rel_y_pos + reg_y]); // store object coordinates relative to screen
	ram[0x0002] = reg_a; // vertically and horizontally, respectively
	set_a(ram[smb_spr_object_rel_x_pos + reg_y]);
	ram[0x0001] = reg_a;
	set_a(reg_x); // multiply offset by four and save to stack
	reg_a = shl(reg_a);
	reg_a = shl(reg_a);
	push(reg_a);
	set_y(reg_a); // use as offset for Y, X is left alone
	set_a(ram[smb_spr_obj_bound_box_ctrl + reg_x]); // load value here to be used as offset for X
	reg_a = shl(reg_a); // multiply that by four and use as X
	reg_a = shl(reg_a);
	set_x(reg_a);
	set_a(ram[0x0001]); // add the first number in the bounding box data to the
	reg_p.c = 0; // relative horizontal coordinate using enemy object offset
	add_a(rom[smb_bound_box_ctrl_data + reg_x]); // and store somewhere using same offset * 4
	ram[smb_bounding_box_ul_x_pos + reg_y] = reg_a; // store here
	set_a(ram[0x0001]);
	reg_p.c = 0;
	add_a(rom[smb_bound_box_ctrl_data+2 + reg_x]); // add the third number in the bounding box data to the
	ram[smb_bounding_box_dr_x_pos + reg_y] = reg_a; // relative horizontal coordinate and store
	set_x(reg_x+1); // increment both offsets
	set_y(reg_y+1);
	set_a(ram[0x0002]); // add the second number to the relative vertical coordinate
	reg_p.c = 0; // using incremented offset and store using the other
	add_a(rom[smb_bound_box_ctrl_data + reg_x]); // incremented offset
	ram[smb_bounding_box_ul_x_pos + reg_y] = reg_a;
	set_a(ram[0x0002]);
	reg_p.c = 0;
	add_a(rom[smb_bound_box_ctrl_data+2 + reg_x]); // add the fourth number to the relative vertical coordinate
	ram[smb_bounding_box_dr_x_pos + reg_y] = reg_a; // and store
	set_a(pull()); // get original offset loaded into $00 * y from stack
	set_y(reg_a); // use as Y
	set_x(ram[0x0000]); // get original offset and use as X again
	return;
	smb_check_right_screen_b_box(); return;
}

void smb_check_right_screen_b_box() {
	set_a(ram[smb_screen_left_x_pos]); // add 128 pixels to left side of screen
	reg_p.c = 0; // and store as horizontal coordinate of middle
	add_a(0x80);
	ram[0x0002] = reg_a;
	set_a(ram[smb_screen_left_page_loc]); // add carry to page location of left side of screen
	add_a(0x00); // and store as page location of middle
	ram[0x0001] = reg_a;
	set_a(ram[smb_spr_object_x_position + reg_x]); // get horizontal coordinate
	cmp_a(ram[0x0002]); // compare against middle horizontal coordinate
	set_a(ram[smb_spr_object_page_loc + reg_x]); // get page location
	sub_a(ram[0x0001]); // subtract from middle page location
	if (!reg_p.c) { goto check_left_screen_b_box; } // if object is on the left side of the screen, branch
	set_a(ram[smb_bounding_box_dr_x_pos + reg_y]); // check right-side edge of bounding box for offscreen
	if (reg_p.n) { goto no_ofs; } // coordinates, branch if still on the screen
	set_a(0xff); // load offscreen value here to use on one or both horizontal sides
	set_x(ram[smb_bounding_box_ul_x_pos + reg_y]); // check left-side edge of bounding box for offscreen
	if (reg_p.n) { goto so_rte; } // coordinates, and branch if still on the screen
	ram[smb_bounding_box_ul_x_pos + reg_y] = reg_a; // store offscreen value for left side
so_rte:;
	ram[smb_bounding_box_dr_x_pos + reg_y] = reg_a; // store offscreen value for right side
no_ofs:;
	set_x(ram[smb_object_offset]); // get object offset and leave
	return;
check_left_screen_b_box:;
	set_a(ram[smb_bounding_box_ul_x_pos + reg_y]); // check left-side edge of bounding box for offscreen
	if (!reg_p.n) { goto no_ofs_2; } // coordinates, and branch if still on the screen
	cmp_a(0xa0); // check to see if left-side edge is in the middle of the
	if (!reg_p.c) { goto no_ofs_2; } // screen or really offscreen, and branch if still on
	set_a(0x00);
	set_x(ram[smb_bounding_box_dr_x_pos + reg_y]); // check right-side edge of bounding box for offscreen
	if (!reg_p.n) { goto so_lft; } // coordinates, branch if still onscreen
	ram[smb_bounding_box_dr_x_pos + reg_y] = reg_a; // store offscreen value for right side
so_lft:;
	ram[smb_bounding_box_ul_x_pos + reg_y] = reg_a; // store offscreen value for left side
no_ofs_2:;
	set_x(ram[smb_object_offset]); // get object offset and leave
	return;
	smb_player_collision_core(); return;
}

void smb_player_collision_core() {
	set_x(0x00); // initialize X to use player's bounding box for comparison
	smb_spr_object_collision_core(); return;
}

void smb_spr_object_collision_core() {
	ram[0x0006] = reg_y; // save contents of Y here
	set_a(0x01);
	ram[0x0007] = reg_a; // save value 1 here as counter, compare horizontal coordinates first
collision_core_loop:;
	set_a(ram[smb_bounding_box_ul_x_pos + reg_y]); // compare left/top coordinates
	cmp_a(ram[smb_bounding_box_ul_x_pos + reg_x]); // of first and second objects' bounding boxes
	if (reg_p.c) { goto first_box_greater; } // if first left/top => second, branch
	cmp_a(ram[smb_bounding_box_dr_x_pos + reg_x]); // otherwise compare to right/bottom of second
	if (!reg_p.c) { goto second_box_vertical_chk; } // if first left/top < second right/bottom, branch elsewhere
	if (reg_p.z) { goto collision_found; } // if somehow equal, collision, thus branch
	set_a(ram[smb_bounding_box_dr_x_pos + reg_y]); // if somehow greater, check to see if bottom of
	cmp_a(ram[smb_bounding_box_ul_x_pos + reg_y]); // first object's bounding box is greater than its top
	if (!reg_p.c) { goto collision_found; } // if somehow less, vertical wrap collision, thus branch
	cmp_a(ram[smb_bounding_box_ul_x_pos + reg_x]); // otherwise compare bottom of first bounding box to the top
	if (reg_p.c) { goto collision_found; } // of second box, and if equal or greater, collision, thus branch
	set_y(ram[0x0006]); // otherwise return with carry clear and Y = $0006
	return; // note horizontal wrapping never occurs
second_box_vertical_chk:;
	set_a(ram[smb_bounding_box_dr_x_pos + reg_x]); // check to see if the vertical bottom of the box
	cmp_a(ram[smb_bounding_box_ul_x_pos + reg_x]); // is greater than the vertical top
	if (!reg_p.c) { goto collision_found; } // if somehow less, vertical wrap collision, thus branch
	set_a(ram[smb_bounding_box_dr_x_pos + reg_y]); // otherwise compare horizontal right or vertical bottom
	cmp_a(ram[smb_bounding_box_ul_x_pos + reg_x]); // of first box with horizontal left or vertical top of second box
	if (reg_p.c) { goto collision_found; } // if equal or greater, collision, thus branch
	set_y(ram[0x0006]); // otherwise return with carry clear and Y = $0006
	return;
first_box_greater:;
	cmp_a(ram[smb_bounding_box_ul_x_pos + reg_x]); // compare first and second box horizontal left/vertical top again
	if (reg_p.z) { goto collision_found; } // if first coordinate = second, collision, thus branch
	cmp_a(ram[smb_bounding_box_dr_x_pos + reg_x]); // if not, compare with second object right or bottom edge
	if (!reg_p.c) { goto collision_found; } // if left/top of first less than or equal to right/bottom of second
	if (reg_p.z) { goto collision_found; } // then collision, thus branch
	cmp_a(ram[smb_bounding_box_dr_x_pos + reg_y]); // otherwise check to see if top of first box is greater than bottom
	if (!reg_p.c) { goto no_collision_found; } // if less than or equal, no collision, branch to end
	if (reg_p.z) { goto no_collision_found; }
	set_a(ram[smb_bounding_box_dr_x_pos + reg_y]); // otherwise compare bottom of first to top of second
	cmp_a(ram[smb_bounding_box_ul_x_pos + reg_x]); // if bottom of first is greater than top of second, vertical wrap
	if (reg_p.c) { goto collision_found; } // collision, and branch, otherwise, proceed onwards here
no_collision_found:;
	reg_p.c = 0; // clear carry, then load value set earlier, then leave
	set_y(ram[0x0006]); // like previous ones, if horizontal coordinates do not collide, we do
	return; // not bother checking vertical ones, because what's the point?
collision_found:;
	set_x(reg_x+1); // increment offsets on both objects to check
	set_y(reg_y+1); // the vertical coordinates
	ram[0x0007] = dec(ram[0x0007]); // decrement counter to reflect this
	if (!reg_p.n) { goto collision_core_loop; } // if counter not expired, branch to loop
	reg_p.c = 1; // otherwise we already did both sets, therefore collision, so set carry
	set_y(ram[0x0006]); // load original value set here earlier, then leave
	return;
	smb_block_buffer_chk_enemy(); return;
}

void smb_block_buffer_chk_enemy() {
	push(reg_a); // save contents of A to stack
	set_a(reg_x);
	reg_p.c = 0; // add 1 to X to run sub with enemy offset in mind
	add_a(0x01);
	set_x(reg_a);
	set_a(pull()); // pull A from stack and jump elsewhere
	smb_bb_chk_e(); return;
unref_e_392:;
	set_a(reg_x);
	reg_p.c = 0; // supposedly used once to set offset for
	add_a(0x0d); // miscellaneous objects
	set_x(reg_a);
	set_y(0x1b); // supposedly used once to set offset for block buffer data
	smb_res_jmp_m(); return; // probably used in early stages to do misc to bg collision detection
	smb_block_buffer_chk_f_ball(); return;
}

void smb_block_buffer_chk_f_ball() {
	set_y(0x1a); // set offset for block buffer adder data
	set_a(reg_x);
	reg_p.c = 0;
	add_a(0x07); // add seven bytes to use
	set_x(reg_a);
	smb_res_jmp_m(); return;
}

void smb_res_jmp_m() {
	set_a(0x00); // set A to return vertical coordinate
	smb_bb_chk_e(); return;
}

void smb_bb_chk_e() {
	smb_block_buffer_collision(); // do collision detection subroutine for sprite object
	set_x(ram[smb_object_offset]); // get object offset
	cmp_a(0x00); // check to see if object bumped into anything
	return;
}

void smb_block_buffer_colli_feet() {
	set_y(reg_y+1); // if branched here, increment to next set of adders
	smb_block_buffer_colli_head(); return;
}

void smb_block_buffer_colli_head() {
	set_a(0x00); // set flag to return vertical coordinate
	smb_block_buffer_colli(); return;
}

void smb_block_buffer_colli_side() {
	set_a(0x01); // set flag to return horizontal coordinate
	smb_block_buffer_colli(); return;
}

void smb_block_buffer_colli() {
	set_x(0x00); // set offset for player object
	smb_block_buffer_collision(); return;
}

void smb_block_buffer_collision() {
	push(reg_a); // save contents of A to stack
	ram[0x0004] = reg_y; // save contents of Y here
	set_a(rom[smb_block_buffer_x_adder + reg_y]); // add horizontal coordinate
	reg_p.c = 0; // of object to value obtained using Y as offset
	add_a(ram[smb_spr_object_x_position + reg_x]);
	ram[0x0005] = reg_a; // store here
	set_a(ram[smb_spr_object_page_loc + reg_x]);
	add_a(0x00); // add carry to page location
	and_a(0x01); // get LSB, mask out all other bits
	reg_a = shr(reg_a); // move to carry
	or_a(ram[0x0005]); // get stored value
	reg_a = ror(reg_a); // rotate carry to MSB of A
	reg_a = shr(reg_a); // and effectively move high nybble to
	reg_a = shr(reg_a); // lower, LSB which became MSB will be
	reg_a = shr(reg_a); // d4 at this point
	smb_get_block_buffer_addr(); // get address of block buffer into $06, $07
	set_y(ram[0x0004]); // get old contents of Y
	set_a(ram[smb_spr_object_y_position + reg_x]); // get vertical coordinate of object
	reg_p.c = 0;
	add_a(rom[smb_block_buffer_y_adder + reg_y]); // add it to value obtained using Y as offset
	and_a(0b11110000); // mask out low nybble
	reg_p.c = 1;
	sub_a(0x20); // subtract 32 pixels for the status ba
	ram[0x0002] = reg_a; // store result here
	set_y(reg_a); // use as offset for block buffer
	set_a(mem_r(*(uint16_t*)&ram[0x0006] + reg_y)); // check current content of block buffer
	ram[0x0003] = reg_a; // and store here
	set_y(ram[0x0004]); // get old contents of Y again
	set_a(pull()); // pull A from stack
	if (!reg_p.z) { goto ret_xc; } // if A = 1, branch
	set_a(ram[smb_spr_object_y_position + reg_x]); // if A = 0, load vertical coordinate
	goto ret_yc; // and jump
ret_xc:;
	set_a(ram[smb_spr_object_x_position + reg_x]); // otherwise load horizontal coordinate
ret_yc:;
	and_a(0b00001111); // and mask out high nybble
	ram[0x0004] = reg_a; // store masked out result here
	set_a(ram[0x0003]); // get saved content of block buffer
	return; // and leave
}

void smb_draw_vine() {
	ram[0x0000] = reg_y; // save offset here
	set_a(ram[smb_enemy_rel_y_pos]); // get relative vertical coordinate
	reg_p.c = 0;
	add_a(rom[smb_vine_y_pos_adder + reg_y]); // add value using offset in Y to get value
	set_x(ram[smb_vine_obj_offset + reg_y]); // get offset to vine
	set_y(ram[smb_enemy_spr_data_offset + reg_x]); // get sprite data offset
	ram[0x0002] = reg_y; // store sprite data offset here
	smb_six_sprite_stacker(); // stack six sprites on top of each other vertically
	set_a(ram[smb_enemy_rel_x_pos]); // get relative horizontal coordinate
	ram[smb_sprite_x_position + reg_y] = reg_a; // store in first, third and fifth sprites
	ram[smb_sprite_x_position+8 + reg_y] = reg_a;
	ram[smb_sprite_x_position+0x10 + reg_y] = reg_a;
	reg_p.c = 0;
	add_a(0x06); // add six pixels to second, fourth and sixth sprites
	ram[smb_sprite_x_position+4 + reg_y] = reg_a; // to give characteristic staggered vine shape to
	ram[smb_sprite_x_position+0x0c + reg_y] = reg_a; // our vertical stack of sprites
	ram[smb_sprite_x_position+0x14 + reg_y] = reg_a;
	set_a(0b00100001); // set bg priority and palette attribute bits
	ram[smb_sprite_attributes + reg_y] = reg_a; // set in first, third and fifth sprites
	ram[smb_sprite_attributes+8 + reg_y] = reg_a;
	ram[smb_sprite_attributes+0x10 + reg_y] = reg_a;
	or_a(0b01000000); // additionally, set horizontal flip bit
	ram[smb_sprite_attributes+4 + reg_y] = reg_a; // for second, fourth and sixth sprites
	ram[smb_sprite_attributes+0x0c + reg_y] = reg_a;
	ram[smb_sprite_attributes+0x14 + reg_y] = reg_a;
	set_x(0x05); // set tiles for six sprites
vine_tl:;
	set_a(0xe1); // set tile number for sprite
	ram[smb_sprite_tilenumber + reg_y] = reg_a;
	set_y(reg_y+1); // move offset to next sprite data
	set_y(reg_y+1);
	set_y(reg_y+1);
	set_y(reg_y+1);
	set_x(reg_x-1); // move onto next sprite
	if (!reg_p.n) { goto vine_tl; } // loop until all sprites are done
	set_y(ram[0x0002]); // get original offset
	set_a(ram[0x0000]); // get offset to vine adding data
	if (!reg_p.z) { goto skp_v_top; } // if offset not zero, skip this part
	set_a(0xe0);
	ram[smb_sprite_tilenumber + reg_y] = reg_a; // set other tile number for top of vine
skp_v_top:;
	set_x(0x00); // start with the first sprite again
chk_f_top:;
	set_a(ram[smb_vine_start_y_position]); // get original starting vertical coordinate
	reg_p.c = 1;
	sub_a(ram[smb_sprite_y_position + reg_y]); // subtract top-most sprite's Y coordinate
	cmp_a(0x64); // if two coordinates are less than 100/$64 pixels
	if (!reg_p.c) { goto next_v_sp; } // apart, skip this to leave sprite alone
	set_a(0xf8);
	ram[smb_sprite_y_position + reg_y] = reg_a; // otherwise move sprite offscreen
next_v_sp:;
	set_y(reg_y+1); // move offset to next OAM data
	set_y(reg_y+1);
	set_y(reg_y+1);
	set_y(reg_y+1);
	set_x(reg_x+1); // move onto next sprite
	cmp_x(0x06); // do this until all sprites are checked
	if (!reg_p.z) { goto chk_f_top; }
	set_y(ram[0x0000]); // return offset set earlier
	return;
	smb_six_sprite_stacker(); return;
}

void smb_six_sprite_stacker() {
	set_x(0x06); // do six sprites
stk_lp:;
	ram[smb_sprite_data + reg_y] = reg_a; // store X or Y coordinate into OAM data
	reg_p.c = 0;
	add_a(0x08); // add eight pixels
	set_y(reg_y+1);
	set_y(reg_y+1); // move offset four bytes forward
	set_y(reg_y+1);
	set_y(reg_y+1);
	set_x(reg_x-1); // do another sprite
	if (!reg_p.z) { goto stk_lp; } // do this until all sprites are done
	set_y(ram[0x0002]); // get saved OAM data offset and leave
	return;
}

void smb_draw_hammer() {
	set_y(ram[smb_misc_spr_data_offset + reg_x]); // get misc object OAM data offset
	set_a(ram[smb_timer_control]);
	if (!reg_p.z) { goto force_h_pose; } // if master timer control set, skip this part
	set_a(ram[smb_misc_state + reg_x]); // otherwise get hammer's state
	and_a(0b01111111); // mask out d7
	cmp_a(0x01); // check to see if set to 1 yet
	if (reg_p.z) { goto get_h_pose; } // if so, branch
force_h_pose:;
	set_x(0x00); // reset offset here
	if (reg_p.z) { goto rdner_h; } // do unconditional branch to rendering part
get_h_pose:;
	set_a(ram[smb_frame_counter]); // get frame counter
	reg_a = shr(reg_a); // move d3-d2 to d1-d0
	reg_a = shr(reg_a);
	and_a(0b00000011); // mask out all but d1-d0 (changes every four frames)
	set_x(reg_a); // use as timing offset
rdner_h:;
	set_a(ram[smb_misc_rel_y_pos]); // get relative vertical coordinate
	reg_p.c = 0;
	add_a(rom[smb_first_spr_y_pos + reg_x]); // add first sprite vertical adder based on offset
	ram[smb_sprite_y_position + reg_y] = reg_a; // store as sprite Y coordinate for first sprite
	reg_p.c = 0;
	add_a(rom[smb_second_spr_y_pos + reg_x]); // add second sprite vertical adder based on offset
	ram[smb_sprite_y_position+4 + reg_y] = reg_a; // store as sprite Y coordinate for second sprite
	set_a(ram[smb_misc_rel_x_pos]); // get relative horizontal coordinate
	reg_p.c = 0;
	add_a(rom[smb_first_spr_x_pos + reg_x]); // add first sprite horizontal adder based on offset
	ram[smb_sprite_x_position + reg_y] = reg_a; // store as sprite X coordinate for first sprite
	reg_p.c = 0;
	add_a(rom[smb_second_spr_x_pos + reg_x]); // add second sprite horizontal adder based on offset
	ram[smb_sprite_x_position+4 + reg_y] = reg_a; // store as sprite X coordinate for second sprite
	set_a(rom[smb_first_spr_tilenum + reg_x]);
	ram[smb_sprite_tilenumber + reg_y] = reg_a; // get and store tile number of first sprite
	set_a(rom[smb_second_spr_tilenum + reg_x]);
	ram[smb_sprite_tilenumber+4 + reg_y] = reg_a; // get and store tile number of second sprite
	set_a(rom[smb_hammer_spr_attrib + reg_x]);
	ram[smb_sprite_attributes + reg_y] = reg_a; // get and store attribute bytes for both
	ram[smb_sprite_attributes+4 + reg_y] = reg_a; // note in this case they use the same data
	set_x(ram[smb_object_offset]); // get misc object offset
	set_a(ram[smb_misc_offscreen_bits]);
	and_a(0b11111100); // check offscreen bits
	if (reg_p.z) { goto no_h_offscr; } // if all bits clear, leave object alone
	set_a(0x00);
	ram[smb_misc_state + reg_x] = reg_a; // otherwise nullify misc object state
	set_a(0xf8);
	smb_dump_two_spr(); // do sub to move hammer sprites offscreen
no_h_offscr:;
	return; // leave
}

void smb_flagpole_gfx_handler() {
	set_y(ram[smb_enemy_spr_data_offset + reg_x]); // get sprite data offset for flagpole flag
	set_a(ram[smb_enemy_rel_x_pos]); // get relative horizontal coordinate
	ram[smb_sprite_x_position + reg_y] = reg_a; // store as X coordinate for first sprite
	reg_p.c = 0;
	add_a(0x08); // add eight pixels and store
	ram[smb_sprite_x_position+4 + reg_y] = reg_a; // as X coordinate for second and third sprites
	ram[smb_sprite_x_position+8 + reg_y] = reg_a;
	reg_p.c = 0;
	add_a(0x0c); // add twelve more pixels and
	ram[0x0005] = reg_a; // store here to be used later by floatey number
	set_a(ram[smb_enemy_y_position + reg_x]); // get vertical coordinate
	smb_dump_two_spr(); // and do sub to dump into first and second sprites
	add_a(0x08); // add eight pixels
	ram[smb_sprite_y_position+8 + reg_y] = reg_a; // and store into third sprite
	set_a(ram[smb_flagpole_f_num_y_pos]); // get vertical coordinate for floatey number
	ram[0x0002] = reg_a; // store it here
	set_a(0x01);
	ram[0x0003] = reg_a; // set value for flip which will not be used, and
	ram[0x0004] = reg_a; // attribute byte for floatey number
	ram[smb_sprite_attributes + reg_y] = reg_a; // set attribute bytes for all three sprites
	ram[smb_sprite_attributes+4 + reg_y] = reg_a;
	ram[smb_sprite_attributes+8 + reg_y] = reg_a;
	set_a(0x7e);
	ram[smb_sprite_tilenumber + reg_y] = reg_a; // put triangle shaped tile
	ram[smb_sprite_tilenumber+8 + reg_y] = reg_a; // into first and third sprites
	set_a(0x7f);
	ram[smb_sprite_tilenumber+4 + reg_y] = reg_a; // put skull tile into second sprite
	set_a(ram[smb_flagpole_collision_y_pos]); // get vertical coordinate at time of collision
	if (reg_p.z) { goto chk_flag_offscreen; } // if zero, branch ahead
	set_a(reg_y);
	reg_p.c = 0; // add 12 bytes to sprite data offset
	add_a(0x0c);
	set_y(reg_a); // put back in Y
	set_a(ram[smb_flagpole_score]); // get offset used to award points for touching flagpole
	reg_a = shl(reg_a); // multiply by 2 to get proper offset here
	set_x(reg_a);
	set_a(rom[smb_flagpole_score_num_tiles + reg_x]); // get appropriate tile data
	ram[0x0000] = reg_a;
	set_a(rom[smb_flagpole_score_num_tiles+1 + reg_x]);
	smb_draw_one_sprite_row(); // use it to render floatey number
chk_flag_offscreen:;
	set_x(ram[smb_object_offset]); // get object offset for flag
	set_y(ram[smb_enemy_spr_data_offset + reg_x]); // get OAM data offset
	set_a(ram[smb_enemy_offscreen_bits]); // get offscreen bits
	and_a(0b00001110); // mask out all but d3-d1
	if (reg_p.z) { smb_exit_dump_spr(); return; } // if none of these bits set, branch to leave
	smb_move_six_sprites_offscreen(); return;
}

void smb_move_six_sprites_offscreen() {
	set_a(0xf8); // set offscreen coordinate if jumping here
	smb_dump_six_spr(); return;
}

void smb_dump_six_spr() {
	ram[smb_sprite_data+0x14 + reg_y] = reg_a; // dump A contents
	ram[smb_sprite_data+0x10 + reg_y] = reg_a; // into third row sprites
	smb_dump_four_spr(); return;
}

void smb_dump_four_spr() {
	ram[smb_sprite_data+0x0c + reg_y] = reg_a; // into second row sprites
	smb_dump_three_spr(); return;
}

void smb_dump_three_spr() {
	ram[smb_sprite_data+8 + reg_y] = reg_a;
	smb_dump_two_spr(); return;
}

void smb_dump_two_spr() {
	ram[smb_sprite_data+4 + reg_y] = reg_a; // and into first row sprites
	ram[smb_sprite_data + reg_y] = reg_a;
	smb_exit_dump_spr(); return;
}

void smb_exit_dump_spr() {
	return;
	smb_draw_large_platform(); return;
}

void smb_draw_large_platform() {
	set_y(ram[smb_enemy_spr_data_offset + reg_x]); // get OAM data offset
	ram[0x0002] = reg_y; // store here
	set_y(reg_y+1); // add 3 to it for offset
	set_y(reg_y+1); // to X coordinate
	set_y(reg_y+1);
	set_a(ram[smb_enemy_rel_x_pos]); // get horizontal relative coordinate
	smb_six_sprite_stacker(); // store X coordinates using A as base, stack horizontally
	set_x(ram[smb_object_offset]);
	set_a(ram[smb_enemy_y_position + reg_x]); // get vertical coordinate
	smb_dump_four_spr(); // dump into first four sprites as Y coordinate
	set_y(ram[smb_area_type]);
	cmp_y(0x03); // check for castle-type level
	if (reg_p.z) { goto shrink_platform; }
	set_y(ram[smb_secondary_hard_mode]); // check for secondary hard mode flag set
	if (reg_p.z) { goto set_last_2_platform; } // branch if not set elsewhere
shrink_platform:;
	set_a(0xf8); // load offscreen coordinate if flag set or castle-type level
set_last_2_platform:;
	set_y(ram[smb_enemy_spr_data_offset + reg_x]); // get OAM data offset
	ram[smb_sprite_y_position+0x10 + reg_y] = reg_a; // store vertical coordinate or offscreen
	ram[smb_sprite_y_position+0x14 + reg_y] = reg_a; // coordinate into last two sprites as Y coordinate
	set_a(0x5b); // load default tile for platform (girder)
	set_x(ram[smb_cloud_type_override]);
	if (reg_p.z) { goto set_platform_tilenum; } // if cloud level override flag not set, use
	set_a(0x75); // otherwise load other tile for platform (puff)
set_platform_tilenum:;
	set_x(ram[smb_object_offset]); // get enemy object buffer offset
	set_y(reg_y+1); // increment Y for tile offset
	smb_dump_six_spr(); // dump tile number into all six sprites
	set_a(0x02); // set palette controls
	set_y(reg_y+1); // increment Y for sprite attributes
	smb_dump_six_spr(); // dump attributes into all six sprites
	set_x(reg_x+1); // increment X for enemy objects
	smb_get_x_offscreen_bits(); // get offscreen bits again
	set_x(reg_x-1);
	set_y(ram[smb_enemy_spr_data_offset + reg_x]); // get OAM data offset
	reg_a = shl(reg_a); // rotate d7 into carry, save remaining
	push(reg_a); // bits to the stack
	if (!reg_p.c) { goto s_chk_2; }
	set_a(0xf8); // if d7 was set, move first sprite offscreen
	ram[smb_sprite_y_position + reg_y] = reg_a;
s_chk_2:;
	set_a(pull()); // get bits from stack
	reg_a = shl(reg_a); // rotate d6 into carry
	push(reg_a); // save to stack
	if (!reg_p.c) { goto s_chk_3; }
	set_a(0xf8); // if d6 was set, move second sprite offscreen
	ram[smb_sprite_y_position+4 + reg_y] = reg_a;
s_chk_3:;
	set_a(pull()); // get bits from stack
	reg_a = shl(reg_a); // rotate d5 into carry
	push(reg_a); // save to stack
	if (!reg_p.c) { goto s_chk_4; }
	set_a(0xf8); // if d5 was set, move third sprite offscreen
	ram[smb_sprite_y_position+8 + reg_y] = reg_a;
s_chk_4:;
	set_a(pull()); // get bits from stack
	reg_a = shl(reg_a); // rotate d4 into carry
	push(reg_a); // save to stack
	if (!reg_p.c) { goto s_chk_5; }
	set_a(0xf8); // if d4 was set, move fourth sprite offscreen
	ram[smb_sprite_y_position+0x0c + reg_y] = reg_a;
s_chk_5:;
	set_a(pull()); // get bits from stack
	reg_a = shl(reg_a); // rotate d3 into carry
	push(reg_a); // save to stack
	if (!reg_p.c) { goto s_chk_6; }
	set_a(0xf8); // if d3 was set, move fifth sprite offscreen
	ram[smb_sprite_y_position+0x10 + reg_y] = reg_a;
s_chk_6:;
	set_a(pull()); // get bits from stack
	reg_a = shl(reg_a); // rotate d2 into carry
	if (!reg_p.c) { goto sl_chk; } // save to stack
	set_a(0xf8);
	ram[smb_sprite_y_position+0x14 + reg_y] = reg_a; // if d2 was set, move sixth sprite offscreen
sl_chk:;
	set_a(ram[smb_enemy_offscreen_bits]); // check d7 of offscreen bits
	reg_a = shl(reg_a); // and if d7 is not set, skip sub
	if (!reg_p.c) { goto ex_dl_pl; }
	smb_move_six_sprites_offscreen(); // otherwise branch to move all sprites offscreen
ex_dl_pl:;
	return;
	smb_draw_floatey_number_coin(); return;
}

void smb_draw_floatey_number_coin() {
	set_a(ram[smb_frame_counter]); // get frame counter
	reg_a = shr(reg_a); // divide by 2
	if (reg_p.c) { goto not_rs_num; } // branch if d0 not set to raise number every other frame
	ram[smb_misc_y_position + reg_x] = dec(ram[smb_misc_y_position + reg_x]); // otherwise, decrement vertical coordinate
not_rs_num:;
	set_a(ram[smb_misc_y_position + reg_x]); // get vertical coordinate
	smb_dump_two_spr(); // dump into both sprites
	set_a(ram[smb_misc_rel_x_pos]); // get relative horizontal coordinate
	ram[smb_sprite_x_position + reg_y] = reg_a; // store as X coordinate for first sprite
	reg_p.c = 0;
	add_a(0x08); // add eight pixels
	ram[smb_sprite_x_position+4 + reg_y] = reg_a; // store as X coordinate for second sprite
	set_a(0x02);
	ram[smb_sprite_attributes + reg_y] = reg_a; // store attribute byte in both sprites
	ram[smb_sprite_attributes+4 + reg_y] = reg_a;
	set_a(0xf7);
	ram[smb_sprite_tilenumber + reg_y] = reg_a; // put tile numbers into both sprites
	set_a(0xfb); // that resemble "200"
	ram[smb_sprite_tilenumber+4 + reg_y] = reg_a;
	smb_ex_jc_gfx(); return; // then jump to leave (why not an rts here instead?)
}

void smb_j_coin_gfx_handler() {
	set_y(ram[smb_misc_spr_data_offset + reg_x]); // get coin/floatey number's OAM data offset
	set_a(ram[smb_misc_state + reg_x]); // get state of misc object
	cmp_a(0x02); // if 2 or greater,
	if (reg_p.c) { smb_draw_floatey_number_coin(); return; } // branch to draw floatey number
	set_a(ram[smb_misc_y_position + reg_x]); // store vertical coordinate as
	ram[smb_sprite_y_position + reg_y] = reg_a; // Y coordinate for first sprite
	reg_p.c = 0;
	add_a(0x08); // add eight pixels
	ram[smb_sprite_y_position+4 + reg_y] = reg_a; // store as Y coordinate for second sprite
	set_a(ram[smb_misc_rel_x_pos]); // get relative horizontal coordinate
	ram[smb_sprite_x_position + reg_y] = reg_a;
	ram[smb_sprite_x_position+4 + reg_y] = reg_a; // store as X coordinate for first and second sprites
	set_a(ram[smb_frame_counter]); // get frame counter
	reg_a = shr(reg_a); // divide by 2 to alter every other frame
	and_a(0b00000011); // mask out d2-d1
	set_x(reg_a); // use as graphical offset
	set_a(rom[smb_jumping_coin_tiles + reg_x]); // load tile number
	set_y(reg_y+1); // increment OAM data offset to write tile numbers
	smb_dump_two_spr(); // do sub to dump tile number into both sprites
	set_y(reg_y-1); // decrement to get old offset
	set_a(0x02);
	ram[smb_sprite_attributes + reg_y] = reg_a; // set attribute byte in first sprite
	set_a(0x82);
	ram[smb_sprite_attributes+4 + reg_y] = reg_a; // set attribute byte with vertical flip in second sprite
	set_x(ram[smb_object_offset]); // get misc object offset
	smb_ex_jc_gfx(); return;
}

void smb_ex_jc_gfx() {
	return; // leave
}

void smb_draw_power_up() {
	set_y(ram[smb_enemy_spr_data_offset+5]); // get power-up's sprite data offset
	set_a(ram[smb_enemy_rel_y_pos]); // get relative vertical coordinate
	reg_p.c = 0;
	add_a(0x08); // add eight pixels
	ram[0x0002] = reg_a; // store result here
	set_a(ram[smb_enemy_rel_x_pos]); // get relative horizontal coordinate
	ram[0x0005] = reg_a; // store here
	set_x(ram[smb_power_up_type]); // get power-up type
	set_a(rom[smb_power_up_attributes + reg_x]); // get attribute data for power-up type
	or_a(ram[smb_enemy_spr_attrib+5]); // add background priority bit if set
	ram[0x0004] = reg_a; // store attributes here
	set_a(reg_x);
	push(reg_a); // save power-up type to the stack
	reg_a = shl(reg_a);
	reg_a = shl(reg_a); // multiply by four to get proper offset
	set_x(reg_a); // use as X
	set_a(0x01);
	ram[0x0007] = reg_a; // set counter here to draw two rows of sprite object
	ram[0x0003] = reg_a; // init d1 of flip control
p_up_draw_loop:;
	set_a(rom[smb_power_up_gfx_table + reg_x]); // load left tile of power-up object
	ram[0x0000] = reg_a;
	set_a(rom[smb_power_up_gfx_table+1 + reg_x]); // load right tile
	smb_draw_one_sprite_row(); // branch to draw one row of our power-up object
	ram[0x0007] = dec(ram[0x0007]); // decrement counter
	if (!reg_p.n) { goto p_up_draw_loop; } // branch until two rows are drawn
	set_y(ram[smb_enemy_spr_data_offset+5]); // get sprite data offset again
	set_a(pull()); // pull saved power-up type from the stack
	if (reg_p.z) { goto p_up_ofs; } // if regular mushroom, branch, do not change colors or flip
	cmp_a(0x03);
	if (reg_p.z) { goto p_up_ofs; } // if 1-up mushroom, branch, do not change colors or flip
	ram[0x0000] = reg_a; // store power-up type here now
	set_a(ram[smb_frame_counter]); // get frame counter
	reg_a = shr(reg_a); // divide by 2 to change colors every two frames
	and_a(0b00000011); // mask out all but d1 and d0 (previously d2 and d1)
	or_a(ram[smb_enemy_spr_attrib+5]); // add background priority bit if any set
	ram[smb_sprite_attributes + reg_y] = reg_a; // set as new palette bits for top left and
	ram[smb_sprite_attributes+4 + reg_y] = reg_a; // top right sprites for fire flower and star
	set_x(ram[0x0000]);
	set_x(reg_x-1); // check power-up type for fire flower
	if (reg_p.z) { goto flip_p_up_right_side; } // if found, skip this part
	ram[smb_sprite_attributes+8 + reg_y] = reg_a; // otherwise set new palette bits  for bottom left
	ram[smb_sprite_attributes+0x0c + reg_y] = reg_a; // and bottom right sprites as well for star only
flip_p_up_right_side:;
	set_a(ram[smb_sprite_attributes+4 + reg_y]);
	or_a(0b01000000); // set horizontal flip bit for top right sprite
	ram[smb_sprite_attributes+4 + reg_y] = reg_a;
	set_a(ram[smb_sprite_attributes+0x0c + reg_y]);
	or_a(0b01000000); // set horizontal flip bit for bottom right sprite
	ram[smb_sprite_attributes+0x0c + reg_y] = reg_a; // note these are only done for fire flower and star power-ups
p_up_ofs:;
	smb_spr_object_offscr_chk(); return; // jump to check to see if power-up is offscreen at all, then leave
}

void smb_enemy_gfx_handler() {
	set_a(ram[smb_enemy_y_position + reg_x]); // get enemy object vertical position
	ram[0x0002] = reg_a;
	set_a(ram[smb_enemy_rel_x_pos]); // get enemy object horizontal position
	ram[0x0005] = reg_a; // relative to screen
	set_y(ram[smb_enemy_spr_data_offset + reg_x]);
	ram[0x00eb] = reg_y; // get sprite data offset
	set_a(0x00);
	ram[smb_vertical_flip_flag] = reg_a; // initialize vertical flip flag by default
	set_a(ram[smb_enemy_moving_dir + reg_x]);
	ram[0x0003] = reg_a; // get enemy object moving direction
	set_a(ram[smb_enemy_spr_attrib + reg_x]);
	ram[0x0004] = reg_a; // get enemy object sprite attributes
	set_a(ram[smb_enemy_id + reg_x]);
	cmp_a((smb_piranha_plant)); // is enemy object piranha plant?
	if (!reg_p.z) { goto check_for_retainer_obj; } // if not, branch
	set_y(ram[smb_piranha_plant_y_speed + reg_x]);
	if (reg_p.n) { goto check_for_retainer_obj; } // if piranha plant moving upwards, branch
	set_y(ram[smb_enemy_frame_timer + reg_x]);
	if (reg_p.z) { goto check_for_retainer_obj; } // if timer for movement expired, branch
	return; // if all conditions fail, leave
check_for_retainer_obj:;
	set_a(ram[smb_enemy_state + reg_x]); // store enemy state
	ram[0x00ed] = reg_a;
	and_a(0b00011111); // nullify all but 5 LSB and use as Y
	set_y(reg_a);
	set_a(ram[smb_enemy_id + reg_x]); // check for mushroom retainer/princess object
	cmp_a(0x35);
	if (!reg_p.z) { goto check_for_bullet_bill_cv; } // if not found, branch
	set_y(0x00); // if found, nullify saved state in Y
	set_a(0x01); // set value that will not be used
	ram[0x0003] = reg_a;
	set_a(0x15); // set value $15 as code for mushroom retainer/princess object
check_for_bullet_bill_cv:;
	cmp_a((smb_bullet_bill_cannon_var)); // otherwise check for bullet bill object
	if (!reg_p.z) { goto check_for_jumpspring; } // if not found, branch again
	ram[0x0002] = dec(ram[0x0002]); // decrement saved vertical position
	set_a(0x03);
	set_y(ram[smb_enemy_frame_timer + reg_x]); // get timer for enemy object
	if (reg_p.z) { goto sbb_at; } // if expired, do not set priority bit
	or_a(0b00100000); // otherwise do so
sbb_at:;
	ram[0x0004] = reg_a; // set new sprite attributes
	set_y(0x00); // nullify saved enemy state both in Y and in
	ram[0x00ed] = reg_y; // memory location here
	set_a(0x08); // set specific value to unconditionally branch once
check_for_jumpspring:;
	cmp_a((smb_jumpspring_object)); // check for jumpspring object
	if (!reg_p.z) { goto check_for_podoboo; }
	set_y(0x03); // set enemy state -2 MSB here for jumpspring object
	set_x(ram[smb_jumpspring_anim_ctrl]); // get current frame number for jumpspring object
	set_a(rom[smb_jumpspring_frame_offsets + reg_x]); // load data using frame number as offset
check_for_podoboo:;
	ram[0x00ef] = reg_a; // store saved enemy object value here
	ram[0x00ec] = reg_y; // and Y here (enemy state -2 MSB if not changed)
	set_x(ram[smb_object_offset]); // get enemy object offset
	cmp_a(0x0c); // check for podoboo object
	if (!reg_p.z) { goto check_bowser_gfx_flag; } // branch if not found
	set_a(ram[smb_enemy_y_speed + reg_x]); // if moving upwards, branch
	if (reg_p.n) { goto check_bowser_gfx_flag; }
	ram[smb_vertical_flip_flag] = inc(ram[smb_vertical_flip_flag]); // otherwise, set flag for vertical flip
check_bowser_gfx_flag:;
	set_a(ram[smb_bowser_gfx_flag]); // if not drawing bowser at all, skip to something else
	if (reg_p.z) { goto check_for_goomba; }
	set_y(0x16); // if set to 1, draw bowser's front
	cmp_a(0x01);
	if (reg_p.z) { goto s_bwsr_gfx_ofs; }
	set_y(reg_y+1); // otherwise draw bowser's rear
s_bwsr_gfx_ofs:;
	ram[0x00ef] = reg_y;
check_for_goomba:;
	set_y(ram[0x00ef]); // check value for goomba object
	cmp_y(0x06);
	if (!reg_p.z) { goto check_bowser_front; } // branch if not found
	set_a(ram[smb_enemy_state + reg_x]);
	cmp_a(0x02); // check for defeated state
	if (!reg_p.c) { goto gmba_anim; } // if not defeated, go ahead and animate
	set_x(0x04); // if defeated, write new value here
	ram[0x00ec] = reg_x;
gmba_anim:;
	and_a(0b00100000); // check for d5 set in enemy object state
	or_a(ram[smb_timer_control]); // or timer disable flag set
	if (!reg_p.z) { goto check_bowser_front; } // if either condition true, do not animate goomba
	set_a(ram[smb_frame_counter]);
	and_a(0b00001000); // check for every eighth frame
	if (!reg_p.z) { goto check_bowser_front; }
	set_a(ram[0x0003]);
	eor_a(0b00000011); // invert bits to flip horizontally every eight frames
	ram[0x0003] = reg_a; // leave alone otherwise
check_bowser_front:;
	set_a(rom[smb_enemy_attribute_data + reg_y]); // load sprite attribute using enemy object
	or_a(ram[0x0004]); // as offset, and add to bits already loaded
	ram[0x0004] = reg_a;
	set_a(rom[smb_enemy_gfx_table_offsets + reg_y]); // load value based on enemy object as offset
	set_x(reg_a); // save as X
	set_y(ram[0x00ec]); // get previously saved value
	set_a(ram[smb_bowser_gfx_flag]);
	if (reg_p.z) { goto check_for_spiny; } // if not drawing bowser object at all, skip all of this
	cmp_a(0x01);
	if (!reg_p.z) { goto check_bowser_rear; } // if not drawing front part, branch to draw the rear part
	set_a(ram[smb_bowser_body_controls]); // check bowser's body control bits
	if (!reg_p.n) { goto check_front_ste; } // branch if d7 not set (control's bowser's mouth)
	set_x(0xde); // otherwise load offset for second frame
check_front_ste:;
	set_a(ram[0x00ed]); // check saved enemy state
	and_a(0b00100000); // if bowser not defeated, do not set flag
	if (reg_p.z) { goto draw_bowser; }
flip_bowser_over:;
	ram[smb_vertical_flip_flag] = reg_x; // set vertical flip flag to nonzero
draw_bowser:;
	goto draw_enemy_object; // draw bowser's graphics now
check_bowser_rear:;
	set_a(ram[smb_bowser_body_controls]); // check bowser's body control bits
	and_a(0x01);
	if (reg_p.z) { goto chk_rear_ste; } // branch if d0 not set (control's bowser's feet)
	set_x(0xe4); // otherwise load offset for second frame
chk_rear_ste:;
	set_a(ram[0x00ed]); // check saved enemy state
	and_a(0b00100000); // if bowser not defeated, do not set flag
	if (reg_p.z) { goto draw_bowser; }
	set_a(ram[0x0002]); // subtract 16 pixels from
	reg_p.c = 1; // saved vertical coordinate
	sub_a(0x10);
	ram[0x0002] = reg_a;
	goto flip_bowser_over; // jump to set vertical flip flag
check_for_spiny:;
	cmp_x(0x24); // check if value loaded is for spiny
	if (!reg_p.z) { goto check_for_lakitu; } // if not found, branch
	cmp_y(0x05); // if enemy state set to $05, do this,
	if (!reg_p.z) { goto not_egg; } // otherwise branch
	set_x(0x30); // set to spiny egg offset
	set_a(0x02);
	ram[0x0003] = reg_a; // set enemy direction to reverse sprites horizontally
	set_a(0x05);
	ram[0x00ec] = reg_a; // set enemy state
not_egg:;
	goto check_for_hammer_bro; // skip a big chunk of this if we found spiny but not in egg
check_for_lakitu:;
	cmp_x(0x90); // check value for lakitu's offset loaded
	if (!reg_p.z) { goto check_upside_down_shell; } // branch if not loaded
	set_a(ram[0x00ed]);
	and_a(0b00100000); // check for d5 set in enemy state
	if (!reg_p.z) { goto no_la_fr; } // branch if set
	set_a(ram[smb_frenzy_enemy_timer]);
	cmp_a(0x10); // check timer to see if we've reached a certain range
	if (reg_p.c) { goto no_la_fr; } // branch if not
	set_x(0x96); // if d6 not set and timer in range, load alt frame for lakitu
no_la_fr:;
	goto check_defeated_state; // skip this next part if we found lakitu but alt frame not needed
check_upside_down_shell:;
	set_a(ram[0x00ef]); // check for enemy object => $04
	cmp_a(0x04);
	if (reg_p.c) { goto check_right_side_up_shell; } // branch if true
	cmp_y(0x02);
	if (!reg_p.c) { goto check_right_side_up_shell; } // branch if enemy state < $02
	set_x(0x5a); // set for upside-down koopa shell by default
	set_y(ram[0x00ef]);
	cmp_y((smb_buzzy_beetle)); // check for buzzy beetle object
	if (!reg_p.z) { goto check_right_side_up_shell; }
	set_x(0x7e); // set for upside-down buzzy beetle shell if found
	ram[0x0002] = inc(ram[0x0002]); // increment vertical position by one pixel
check_right_side_up_shell:;
	set_a(ram[0x00ec]); // check for value set here
	cmp_a(0x04); // if enemy state < $02, do not change to shell, if
	if (!reg_p.z) { goto check_for_hammer_bro; } // enemy state => $02 but not = $04, leave shell upside-down
	set_x(0x72); // set right-side up buzzy beetle shell by default
	ram[0x0002] = inc(ram[0x0002]); // increment saved vertical position by one pixel
	set_y(ram[0x00ef]);
	cmp_y((smb_buzzy_beetle)); // check for buzzy beetle object
	if (reg_p.z) { goto check_for_defd_goomba; } // branch if found
	set_x(0x66); // change to right-side up koopa shell if not found
	ram[0x0002] = inc(ram[0x0002]); // and increment saved vertical position again
check_for_defd_goomba:;
	cmp_y((smb_goomba)); // check for goomba object (necessary if previously
	if (!reg_p.z) { goto check_for_hammer_bro; } // failed buzzy beetle object test)
	set_x(0x54); // load for regular goomba
	set_a(ram[0x00ed]); // note that this only gets performed if enemy state => $02
	and_a(0b00100000); // check saved enemy state for d5 set
	if (!reg_p.z) { goto check_for_hammer_bro; } // branch if set
	set_x(0x8a); // load offset for defeated goomba
	ram[0x0002] = dec(ram[0x0002]); // set different value and decrement saved vertical position
check_for_hammer_bro:;
	set_y(ram[smb_object_offset]);
	set_a(ram[0x00ef]); // check for hammer bro object
	cmp_a((smb_hammer_bro));
	if (!reg_p.z) { goto check_for_bloober; } // branch if not found
	set_a(ram[0x00ed]);
	if (reg_p.z) { goto check_to_animate_enemy; } // branch if not in normal enemy state
	and_a(0b00001000);
	if (reg_p.z) { goto check_defeated_state; } // if d3 not set, branch further away
	set_x(0xb4); // otherwise load offset for different frame
	if (!reg_p.z) { goto check_to_animate_enemy; } // unconditional branch
check_for_bloober:;
	cmp_x(0x48); // check for cheep-cheep offset loaded
	if (reg_p.z) { goto check_to_animate_enemy; } // branch if found
	set_a(ram[smb_enemy_interval_timer + reg_y]);
	cmp_a(0x05);
	if (reg_p.c) { goto check_defeated_state; } // branch if some timer is above a certain point
	cmp_x(0x3c); // check for bloober offset loaded
	if (!reg_p.z) { goto check_to_animate_enemy; } // branch if not found this time
	cmp_a(0x01);
	if (reg_p.z) { goto check_defeated_state; } // branch if timer is set to certain point
	ram[0x0002] = inc(ram[0x0002]); // increment saved vertical coordinate three pixels
	ram[0x0002] = inc(ram[0x0002]);
	ram[0x0002] = inc(ram[0x0002]);
	goto check_animation_stop; // and do something else
check_to_animate_enemy:;
	set_a(ram[0x00ef]); // check for specific enemy objects
	cmp_a(0x06);
	if (reg_p.z) { goto check_defeated_state; } // branch if goomba
	cmp_a(0x08);
	if (reg_p.z) { goto check_defeated_state; } // branch if bullet bill (note both variants use $08 here)
	cmp_a(0x0c);
	if (reg_p.z) { goto check_defeated_state; } // branch if podoboo
	cmp_a(0x18); // branch if => $18
	if (reg_p.c) { goto check_defeated_state; }
	set_y(0x00);
	cmp_a(0x15); // check for mushroom retainer/princess object
	if (!reg_p.z) { goto check_for_second_frame; } // which uses different code here, branch if not found
	set_y(reg_y+1); // residual instruction
	set_a(ram[smb_world_number]); // are we on world 8?
	cmp_a((smb_world_8));
	if (reg_p.c) { goto check_defeated_state; } // if so, leave the offset alone (use princess)
	set_x(0xa2); // otherwise, set for mushroom retainer object instead
	set_a(0x03); // set alternate state here
	ram[0x00ec] = reg_a;
	if (!reg_p.z) { goto check_defeated_state; } // unconditional branch
check_for_second_frame:;
	set_a(ram[smb_frame_counter]); // load frame counter
	and_a(rom[smb_enemy_anim_timing_b_mask + reg_y]); // mask it (partly residual, one byte not ever used)
	if (!reg_p.z) { goto check_defeated_state; } // branch if timing is off
check_animation_stop:;
	set_a(ram[0x00ed]); // check saved enemy state
	and_a(0b10100000); // for d7 or d5, or check for timers stopped
	or_a(ram[smb_timer_control]);
	if (!reg_p.z) { goto check_defeated_state; } // if either condition true, branch
	set_a(reg_x);
	reg_p.c = 0;
	add_a(0x06); // add $06 to current enemy offset
	set_x(reg_a); // to animate various enemy objects
check_defeated_state:;
	set_a(ram[0x00ed]); // check saved enemy state
	and_a(0b00100000); // for d5 set
	if (reg_p.z) { goto draw_enemy_object; } // branch if not set
	set_a(ram[0x00ef]);
	cmp_a(0x04); // check for saved enemy object => $04
	if (!reg_p.c) { goto draw_enemy_object; } // branch if less
	set_y(0x01);
	ram[smb_vertical_flip_flag] = reg_y; // set vertical flip flag
	set_y(reg_y-1);
	ram[0x00ec] = reg_y; // init saved value here
draw_enemy_object:;
	set_y(ram[0x00eb]); // load sprite data offset
	smb_draw_enemy_obj_row(); // draw six tiles of data
	smb_draw_enemy_obj_row(); // into sprite data
	smb_draw_enemy_obj_row();
	set_x(ram[smb_object_offset]); // get enemy object offset
	set_y(ram[smb_enemy_spr_data_offset + reg_x]); // get sprite data offset
	set_a(ram[0x00ef]);
	cmp_a(0x08); // get saved enemy object and check
	if (!reg_p.z) { goto check_for_vertical_flip; } // for bullet bill, branch if not found
skip_to_off_scr_chk:;
	smb_spr_object_offscr_chk(); return; // jump if found
check_for_vertical_flip:;
	set_a(ram[smb_vertical_flip_flag]); // check if vertical flip flag is set here
	if (reg_p.z) { goto check_for_e_symmetry; } // branch if not
	set_a(ram[smb_sprite_attributes + reg_y]); // get attributes of first sprite we dealt with
	or_a(0b10000000); // set bit for vertical flip
	set_y(reg_y+1);
	set_y(reg_y+1); // increment two bytes so that we store the vertical flip
	smb_dump_six_spr(); // in attribute bytes of enemy obj sprite data
	set_y(reg_y-1);
	set_y(reg_y-1); // now go back to the Y coordinate offset
	set_a(reg_y);
	set_x(reg_a); // give offset to X
	set_a(ram[0x00ef]);
	cmp_a((smb_hammer_bro)); // check saved enemy object for hammer bro
	if (reg_p.z) { goto flip_enemy_vertically; }
	cmp_a((smb_lakitu)); // check saved enemy object for lakitu
	if (reg_p.z) { goto flip_enemy_vertically; } // branch for hammer bro or lakitu
	cmp_a(0x15);
	if (reg_p.c) { goto flip_enemy_vertically; } // also branch if enemy object => $15
	set_a(reg_x);
	reg_p.c = 0;
	add_a(0x08); // if not selected objects or => $15, set
	set_x(reg_a); // offset in X for next row
flip_enemy_vertically:;
	set_a(ram[smb_sprite_tilenumber + reg_x]); // load first or second row tiles
	push(reg_a); // and save tiles to the stack
	set_a(ram[smb_sprite_tilenumber+4 + reg_x]);
	push(reg_a);
	set_a(ram[smb_sprite_tilenumber+0x10 + reg_y]); // exchange third row tiles
	ram[smb_sprite_tilenumber + reg_x] = reg_a; // with first or second row tiles
	set_a(ram[smb_sprite_tilenumber+0x14 + reg_y]);
	ram[smb_sprite_tilenumber+4 + reg_x] = reg_a;
	set_a(pull()); // pull first or second row tiles from stack
	ram[smb_sprite_tilenumber+0x14 + reg_y] = reg_a; // and save in third row
	set_a(pull());
	ram[smb_sprite_tilenumber+0x10 + reg_y] = reg_a;
check_for_e_symmetry:;
	set_a(ram[smb_bowser_gfx_flag]); // are we drawing bowser at all?
	if (!reg_p.z) { goto skip_to_off_scr_chk; } // branch if so
	set_a(ram[0x00ef]);
	set_x(ram[0x00ec]); // get alternate enemy state
	cmp_a(0x05); // check for hammer bro object
	if (!reg_p.z) { goto cont_es; }
	smb_spr_object_offscr_chk(); return; // jump if found
cont_es:;
	cmp_a((smb_bloober)); // check for bloober object
	if (reg_p.z) { goto mirror_enemy_gfx; }
	cmp_a((smb_piranha_plant)); // check for piranha plant object
	if (reg_p.z) { goto mirror_enemy_gfx; }
	cmp_a((smb_podoboo)); // check for podoboo object
	if (reg_p.z) { goto mirror_enemy_gfx; } // branch if either of three are found
	cmp_a((smb_spiny)); // check for spiny object
	if (!reg_p.z) { goto es_rtnr; } // branch closer if not found
	cmp_x(0x05); // check spiny's state
	if (!reg_p.z) { goto check_to_mirror_lakitu; } // branch if not an egg, otherwise
es_rtnr:;
	cmp_a(0x15); // check for princess/mushroom retainer object
	if (!reg_p.z) { goto spny_sc; }
	set_a(0x42); // set horizontal flip on bottom right sprite
	ram[smb_sprite_attributes+0x14 + reg_y] = reg_a; // note that palette bits were already set earlier
spny_sc:;
	cmp_x(0x02); // if alternate enemy state set to 1 or 0, branch
	if (!reg_p.c) { goto check_to_mirror_lakitu; }
mirror_enemy_gfx:;
	set_a(ram[smb_bowser_gfx_flag]); // if enemy object is bowser, skip all of this
	if (!reg_p.z) { goto check_to_mirror_lakitu; }
	set_a(ram[smb_sprite_attributes + reg_y]); // load attribute bits of first sprite
	and_a(0b10100011);
	ram[smb_sprite_attributes + reg_y] = reg_a; // save vertical flip, priority, and palette bits
	ram[smb_sprite_attributes+8 + reg_y] = reg_a; // in left sprite column of enemy object OAM data
	ram[smb_sprite_attributes+0x10 + reg_y] = reg_a;
	or_a(0b01000000); // set horizontal flip
	cmp_x(0x05); // check for state used by spiny's egg
	if (!reg_p.z) { goto egg_exc; } // if alternate state not set to $05, branch
	or_a(0b10000000); // otherwise set vertical flip
egg_exc:;
	ram[smb_sprite_attributes+4 + reg_y] = reg_a; // set bits of right sprite column
	ram[smb_sprite_attributes+0x0c + reg_y] = reg_a; // of enemy object sprite data
	ram[smb_sprite_attributes+0x14 + reg_y] = reg_a;
	cmp_x(0x04); // check alternate enemy state
	if (!reg_p.z) { goto check_to_mirror_lakitu; } // branch if not $04
	set_a(ram[smb_sprite_attributes+8 + reg_y]); // get second row left sprite attributes
	or_a(0x80);
	ram[smb_sprite_attributes+8 + reg_y] = reg_a; // store bits with vertical flip in
	ram[smb_sprite_attributes+0x10 + reg_y] = reg_a; // second and third row left sprites
	or_a(0x40);
	ram[smb_sprite_attributes+0x0c + reg_y] = reg_a; // store with horizontal and vertical flip in
	ram[smb_sprite_attributes+0x14 + reg_y] = reg_a; // second and third row right sprites
check_to_mirror_lakitu:;
	set_a(ram[0x00ef]); // check for lakitu enemy object
	cmp_a(0x11);
	if (!reg_p.z) { goto check_to_mirror_j_spring; } // branch if not found
	set_a(ram[smb_vertical_flip_flag]);
	if (!reg_p.z) { goto nv_flak; } // branch if vertical flip flag not set
	set_a(ram[smb_sprite_attributes+0x10 + reg_y]); // save vertical flip and palette bits
	and_a(0b10000001); // in third row left sprite
	ram[smb_sprite_attributes+0x10 + reg_y] = reg_a;
	set_a(ram[smb_sprite_attributes+0x14 + reg_y]); // set horizontal flip and palette bits
	or_a(0b01000001); // in third row right sprite
	ram[smb_sprite_attributes+0x14 + reg_y] = reg_a;
	set_x(ram[smb_frenzy_enemy_timer]); // check timer
	cmp_x(0x10);
	if (reg_p.c) { smb_spr_object_offscr_chk(); return; } // branch if timer has not reached a certain range
	ram[smb_sprite_attributes+0x0c + reg_y] = reg_a; // otherwise set same for second row right sprite
	and_a(0b10000001);
	ram[smb_sprite_attributes+8 + reg_y] = reg_a; // preserve vertical flip and palette bits for left sprite
	if (!reg_p.c) { smb_spr_object_offscr_chk(); return; } // unconditional branch
nv_flak:;
	set_a(ram[smb_sprite_attributes + reg_y]); // get first row left sprite attributes
	and_a(0b10000001);
	ram[smb_sprite_attributes + reg_y] = reg_a; // save vertical flip and palette bits
	set_a(ram[smb_sprite_attributes+4 + reg_y]); // get first row right sprite attributes
	or_a(0b01000001); // set horizontal flip and palette bits
	ram[smb_sprite_attributes+4 + reg_y] = reg_a; // note that vertical flip is left as-is
check_to_mirror_j_spring:;
	set_a(ram[0x00ef]); // check for jumpspring object (any frame)
	cmp_a(0x18);
	if (!reg_p.c) { smb_spr_object_offscr_chk(); return; } // branch if not jumpspring object at all
	set_a(0x82);
	ram[smb_sprite_attributes+8 + reg_y] = reg_a; // set vertical flip and palette bits of
	ram[smb_sprite_attributes+0x10 + reg_y] = reg_a; // second and third row left sprites
	or_a(0x40);
	ram[smb_sprite_attributes+0x0c + reg_y] = reg_a; // set, in addition to those, horizontal flip
	ram[smb_sprite_attributes+0x14 + reg_y] = reg_a; // for second and third row right sprites
	smb_spr_object_offscr_chk(); return;
}

void smb_spr_object_offscr_chk() {
	set_x(ram[smb_object_offset]); // get enemy buffer offset
	set_a(ram[smb_enemy_offscreen_bits]); // check offscreen information
	reg_a = shr(reg_a);
	reg_a = shr(reg_a); // shift three times to the right
	reg_a = shr(reg_a); // which puts d2 into carry
	push(reg_a); // save to stack
	if (!reg_p.c) { goto lc_chk; } // branch if not set
	set_a(0x04); // set for right column sprites
	smb_move_e_spr_col_offscreen(); // and move them offscreen
lc_chk:;
	set_a(pull()); // get from stack
	reg_a = shr(reg_a); // move d3 to carry
	push(reg_a); // ;save to stack
	if (!reg_p.c) { goto row_3_c; } // branch if not set
	set_a(0x00); // set for left column sprites,
	smb_move_e_spr_col_offscreen(); // move them offscreen
row_3_c:;
	set_a(pull()); // get from stack again
	reg_a = shr(reg_a); // move d5 to carry this time
	reg_a = shr(reg_a);
	push(reg_a); // save to stack again
	if (!reg_p.c) { goto row_23_c; } // branch if carry not set
	set_a(0x10); // set for third row of sprites
	smb_move_e_spr_row_offscreen(); // and move them offscreen
row_23_c:;
	set_a(pull()); // get from stack
	reg_a = shr(reg_a); // move d6 into carry
	push(reg_a); // save to stack
	if (!reg_p.c) { goto all_row_c; }
	set_a(0x08); // set for second and third rows
	smb_move_e_spr_row_offscreen(); // move them offscreen
all_row_c:;
	set_a(pull()); // get from stack once more
	reg_a = shr(reg_a); // move d7 into carry
	if (!reg_p.c) { goto ex_eg_handler; }
	smb_move_e_spr_row_offscreen(); // move all sprites offscreen (A should be 0 by now)
	set_a(ram[smb_enemy_id + reg_x]);
	cmp_a((smb_podoboo)); // check enemy identifier for podoboo
	if (reg_p.z) { goto ex_eg_handler; } // skip this part if found, we do not want to erase podoboo!
	set_a(ram[smb_enemy_y_high_pos + reg_x]); // check high byte of vertical position
	cmp_a(0x02); // if not yet past the bottom of the screen, branch
	if (!reg_p.z) { goto ex_eg_handler; }
	smb_erase_enemy_object(); // what it says
ex_eg_handler:;
	return;
	smb_draw_enemy_obj_row(); return;
}

void smb_draw_enemy_obj_row() {
	set_a(rom[smb_enemy_graphics_table + reg_x]); // load two tiles of enemy graphics
	ram[0x0000] = reg_a;
	set_a(rom[smb_enemy_graphics_table+1 + reg_x]);
	smb_draw_one_sprite_row(); return;
}

void smb_draw_one_sprite_row() {
	ram[0x0001] = reg_a;
	smb_draw_sprite_object(); return; // draw them
	smb_move_e_spr_row_offscreen(); return;
}

void smb_move_e_spr_row_offscreen() {
	reg_p.c = 0; // add A to enemy object OAM data offset
	add_a(ram[smb_enemy_spr_data_offset + reg_x]);
	set_y(reg_a); // use as offset
	set_a(0xf8);
	smb_dump_two_spr(); return; // move first row of sprites offscreen
	smb_move_e_spr_col_offscreen(); return;
}

void smb_move_e_spr_col_offscreen() {
	reg_p.c = 0; // add A to enemy object OAM data offset
	add_a(ram[smb_enemy_spr_data_offset + reg_x]);
	set_y(reg_a); // use as offset
	smb_move_col_offscreen(); // move first and second row sprites in column offscreen
	ram[smb_sprite_data+0x10 + reg_y] = reg_a; // move third row sprite in column offscreen
	return;
}

void smb_draw_block() {
	set_a(ram[smb_block_rel_y_pos]); // get relative vertical coordinate of block object
	ram[0x0002] = reg_a; // store here
	set_a(ram[smb_block_rel_x_pos]); // get relative horizontal coordinate of block object
	ram[0x0005] = reg_a; // store here
	set_a(0x03);
	ram[0x0004] = reg_a; // set attribute byte here
	reg_a = shr(reg_a);
	ram[0x0003] = reg_a; // set horizontal flip bit here (will not be used)
	set_y(ram[smb_block_spr_data_offset + reg_x]); // get sprite data offset
	set_x(0x00); // reset X for use as offset to tile data
d_blk_loop:;
	set_a(rom[smb_default_block_obj_tiles + reg_x]); // get left tile number
	ram[0x0000] = reg_a; // set here
	set_a(rom[smb_default_block_obj_tiles+1 + reg_x]); // get right tile number
	smb_draw_one_sprite_row(); // do sub to write tile numbers to first row of sprites
	cmp_x(0x04); // check incremented offset
	if (!reg_p.z) { goto d_blk_loop; } // and loop back until all four sprites are done
	set_x(ram[smb_object_offset]); // get block object offset
	set_y(ram[smb_block_spr_data_offset + reg_x]); // get sprite data offset
	set_a(ram[smb_area_type]);
	cmp_a(0x01); // check for ground level type area
	if (reg_p.z) { goto chk_rep; } // if found, branch to next part
	set_a(0x86);
	ram[smb_sprite_tilenumber + reg_y] = reg_a; // otherwise remove brick tiles with lines
	ram[smb_sprite_tilenumber+4 + reg_y] = reg_a; // and replace then with lineless brick tiles
chk_rep:;
	set_a(ram[smb_block_metatile + reg_x]); // check replacement metatile
	cmp_a(0xc4); // if not used block metatile, then
	if (!reg_p.z) { goto blk_offscr; } // branch ahead to use current graphics
	set_a(0x87); // set A for used block tile
	set_y(reg_y+1); // increment Y to write to tile bytes
	smb_dump_four_spr(); // do sub to dump into all four sprites
	set_y(reg_y-1); // return Y to original offset
	set_a(0x03); // set palette bits
	set_x(ram[smb_area_type]);
	set_x(reg_x-1); // check for ground level type area again
	if (reg_p.z) { goto set_b_flip; } // if found, use current palette bits
	reg_a = shr(reg_a); // otherwise set to $01
set_b_flip:;
	set_x(ram[smb_object_offset]); // put block object offset back in X
	ram[smb_sprite_attributes + reg_y] = reg_a; // store attribute byte as-is in first sprite
	or_a(0x40);
	ram[smb_sprite_attributes+4 + reg_y] = reg_a; // set horizontal flip bit for second sprite
	or_a(0x80);
	ram[smb_sprite_attributes+0x0c + reg_y] = reg_a; // set both flip bits for fourth sprite
	and_a(0x83);
	ram[smb_sprite_attributes+8 + reg_y] = reg_a; // set vertical flip bit for third sprite
blk_offscr:;
	set_a(ram[smb_block_offscreen_bits]); // get offscreen bits for block object
	push(reg_a); // save to stack
	and_a(0b00000100); // check to see if d2 in offscreen bits are set
	if (reg_p.z) { goto pull_ofs_b; } // if not set, branch, otherwise move sprites offscreen
	set_a(0xf8); // move offscreen two OAMs
	ram[smb_sprite_y_position+4 + reg_y] = reg_a; // on the right side
	ram[smb_sprite_y_position+0x0c + reg_y] = reg_a;
pull_ofs_b:;
	set_a(pull()); // pull offscreen bits from stack
	smb_chk_left_co(); return;
}

void smb_chk_left_co() {
	and_a(0b00001000); // check to see if d3 in offscreen bits are set
	if (reg_p.z) { smb_ex_d_blk(); return; } // if not set, branch, otherwise move sprites offscreen
	smb_move_col_offscreen(); return;
}

void smb_move_col_offscreen() {
	set_a(0xf8); // move offscreen two OAMs
	ram[smb_sprite_y_position + reg_y] = reg_a; // on the left side (or two rows of enemy on either side
	ram[smb_sprite_y_position+8 + reg_y] = reg_a; // if branched here from enemy graphics handler)
	smb_ex_d_blk(); return;
}

void smb_ex_d_blk() {
	return;
	smb_draw_brick_chunks(); return;
}

void smb_draw_brick_chunks() {
	set_a(0x02); // set palette bits here
	ram[0x0000] = reg_a;
	set_a(0x75); // set tile number for ball (something residual, likely)
	set_y(ram[smb_game_engine_subroutine]);
	cmp_y(0x05); // if end-of-level routine running,
	if (reg_p.z) { goto d_chunks; } // use palette and tile number assigned
	set_a(0x03); // otherwise set different palette bits
	ram[0x0000] = reg_a;
	set_a(0x84); // and set tile number for brick chunks
d_chunks:;
	set_y(ram[smb_block_spr_data_offset + reg_x]); // get OAM data offset
	set_y(reg_y+1); // increment to start with tile bytes in OAM
	smb_dump_four_spr(); // do sub to dump tile number into all four sprites
	set_a(ram[smb_frame_counter]); // get frame counter
	reg_a = shl(reg_a);
	reg_a = shl(reg_a);
	reg_a = shl(reg_a); // move low nybble to high
	reg_a = shl(reg_a);
	and_a(0xc0); // get what was originally d3-d2 of low nybble
	or_a(ram[0x0000]); // add palette bits
	set_y(reg_y+1); // increment offset for attribute bytes
	smb_dump_four_spr(); // do sub to dump attribute data into all four sprites
	set_y(reg_y-1);
	set_y(reg_y-1); // decrement offset to Y coordinate
	set_a(ram[smb_block_rel_y_pos]); // get first block object's relative vertical coordinate
	smb_dump_two_spr(); // do sub to dump current Y coordinate into two sprites
	set_a(ram[smb_block_rel_x_pos]); // get first block object's relative horizontal coordinate
	ram[smb_sprite_x_position + reg_y] = reg_a; // save into X coordinate of first sprite
	set_a(ram[smb_block_orig_x_pos + reg_x]); // get original horizontal coordinate
	reg_p.c = 1;
	sub_a(ram[smb_screen_left_x_pos]); // subtract coordinate of left side from original coordinate
	ram[0x0000] = reg_a; // store result as relative horizontal coordinate of original
	reg_p.c = 1;
	sub_a(ram[smb_block_rel_x_pos]); // get difference of relative positions of original - current
	add_a(ram[0x0000]); // add original relative position to result
	add_a(0x06); // plus 6 pixels to position second brick chunk correctly
	ram[smb_sprite_x_position+4 + reg_y] = reg_a; // save into X coordinate of second sprite
	set_a(ram[smb_block_rel_y_pos+1]); // get second block object's relative vertical coordinate
	ram[smb_sprite_y_position+8 + reg_y] = reg_a;
	ram[smb_sprite_y_position+0x0c + reg_y] = reg_a; // dump into Y coordinates of third and fourth sprites
	set_a(ram[smb_block_rel_x_pos+1]); // get second block object's relative horizontal coordinate
	ram[smb_sprite_x_position+8 + reg_y] = reg_a; // save into X coordinate of third sprite
	set_a(ram[0x0000]); // use original relative horizontal position
	reg_p.c = 1;
	sub_a(ram[smb_block_rel_x_pos+1]); // get difference of relative positions of original - current
	add_a(ram[0x0000]); // add original relative position to result
	add_a(0x06); // plus 6 pixels to position fourth brick chunk correctly
	ram[smb_sprite_x_position+0x0c + reg_y] = reg_a; // save into X coordinate of fourth sprite
	set_a(ram[smb_block_offscreen_bits]); // get offscreen bits for block object
	smb_chk_left_co(); // do sub to move left half of sprites offscreen if necessary
	set_a(ram[smb_block_offscreen_bits]); // get offscreen bits again
	reg_a = shl(reg_a); // shift d7 into carry
	if (!reg_p.c) { goto chnk_ofs; } // if d7 not set, branch to last part
	set_a(0xf8);
	smb_dump_two_spr(); // otherwise move top sprites offscreen
chnk_ofs:;
	set_a(ram[0x0000]); // if relative position on left side of screen,
	if (!reg_p.n) { goto ex_bc_dr; } // go ahead and leave
	set_a(ram[smb_sprite_x_position + reg_y]); // otherwise compare left-side X coordinate
	cmp_a(ram[smb_sprite_x_position+4 + reg_y]); // to right-side X coordinate
	if (!reg_p.c) { goto ex_bc_dr; } // branch to leave if less
	set_a(0xf8); // otherwise move right half of sprites offscreen
	ram[smb_sprite_y_position+4 + reg_y] = reg_a;
	ram[smb_sprite_y_position+0x0c + reg_y] = reg_a;
ex_bc_dr:;
	return; // leave
	smb_draw_fireball(); return;
}

void smb_draw_fireball() {
	set_y(ram[smb_f_ball_spr_data_offset + reg_x]); // get fireball's sprite data offset
	set_a(ram[smb_fireball_rel_y_pos]); // get relative vertical coordinate
	ram[smb_sprite_y_position + reg_y] = reg_a; // store as sprite Y coordinate
	set_a(ram[smb_fireball_rel_x_pos]); // get relative horizontal coordinate
	ram[smb_sprite_x_position + reg_y] = reg_a; // store as sprite X coordinate, then do shared code
	smb_draw_firebar(); return;
}

void smb_draw_firebar() {
	set_a(ram[smb_frame_counter]); // get frame counter
	reg_a = shr(reg_a); // divide by four
	reg_a = shr(reg_a);
	push(reg_a); // save result to stack
	and_a(0x01); // mask out all but last bit
	eor_a(0x64); // set either tile $64 or $65 as fireball tile
	ram[smb_sprite_tilenumber + reg_y] = reg_a; // thus tile changes every four frames
	set_a(pull()); // get from stack
	reg_a = shr(reg_a); // divide by four again
	reg_a = shr(reg_a);
	set_a(0x02); // load value $02 to set palette in attrib byte
	if (!reg_p.c) { goto fire_a; } // if last bit shifted out was not set, skip this
	or_a(0b11000000); // otherwise flip both ways every eight frames
fire_a:;
	ram[smb_sprite_attributes + reg_y] = reg_a; // store attribute byte and leave
	return;
}

void smb_draw_explosion_fireball() {
	set_y(ram[smb_alt_spr_data_offset + reg_x]); // get OAM data offset of alternate sort for fireball's explosion
	set_a(ram[smb_fireball_state + reg_x]); // load fireball state
	ram[smb_fireball_state + reg_x] = inc(ram[smb_fireball_state + reg_x]); // increment state for next frame
	reg_a = shr(reg_a); // divide by 2
	and_a(0b00000111); // mask out all but d3-d1
	cmp_a(0x03); // check to see if time to kill fireball
	if (reg_p.c) { smb_kill_fire_ball(); return; } // branch if so, otherwise continue to draw explosion
	smb_draw_explosion_fireworks(); return;
}

void smb_draw_explosion_fireworks() {
	set_x(reg_a); // use whatever's in A for offset
	set_a(rom[smb_explosion_tiles + reg_x]); // get tile number using offset
	set_y(reg_y+1); // increment Y (contains sprite data offset)
	smb_dump_four_spr(); // and dump into tile number part of sprite data
	set_y(reg_y-1); // decrement Y so we have the proper offset again
	set_x(ram[smb_object_offset]); // return enemy object buffer offset to X
	set_a(ram[smb_fireball_rel_y_pos]); // get relative vertical coordinate
	reg_p.c = 1; // subtract four pixels vertically
	sub_a(0x04); // for first and third sprites
	ram[smb_sprite_y_position + reg_y] = reg_a;
	ram[smb_sprite_y_position+8 + reg_y] = reg_a;
	reg_p.c = 0; // add eight pixels vertically
	add_a(0x08); // for second and fourth sprites
	ram[smb_sprite_y_position+4 + reg_y] = reg_a;
	ram[smb_sprite_y_position+0x0c + reg_y] = reg_a;
	set_a(ram[smb_fireball_rel_x_pos]); // get relative horizontal coordinate
	reg_p.c = 1; // subtract four pixels horizontally
	sub_a(0x04); // for first and second sprites
	ram[smb_sprite_x_position + reg_y] = reg_a;
	ram[smb_sprite_x_position+4 + reg_y] = reg_a;
	reg_p.c = 0; // add eight pixels horizontally
	add_a(0x08); // for third and fourth sprites
	ram[smb_sprite_x_position+8 + reg_y] = reg_a;
	ram[smb_sprite_x_position+0x0c + reg_y] = reg_a;
	set_a(0x02); // set palette attributes for all sprites, but
	ram[smb_sprite_attributes + reg_y] = reg_a; // set no flip at all for first sprite
	set_a(0x82);
	ram[smb_sprite_attributes+4 + reg_y] = reg_a; // set vertical flip for second sprite
	set_a(0x42);
	ram[smb_sprite_attributes+8 + reg_y] = reg_a; // set horizontal flip for third sprite
	set_a(0xc2);
	ram[smb_sprite_attributes+0x0c + reg_y] = reg_a; // set both flips for fourth sprite
	return; // we are done
	smb_kill_fire_ball(); return;
}

void smb_kill_fire_ball() {
	set_a(0x00); // clear fireball state to kill it
	ram[smb_fireball_state + reg_x] = reg_a;
	return;
	smb_draw_small_platform(); return;
}

void smb_draw_small_platform() {
	set_y(ram[smb_enemy_spr_data_offset + reg_x]); // get OAM data offset
	set_a(0x5b); // load tile number for small platforms
	set_y(reg_y+1); // increment offset for tile numbers
	smb_dump_six_spr(); // dump tile number into all six sprites
	set_y(reg_y+1); // increment offset for attributes
	set_a(0x02); // load palette controls
	smb_dump_six_spr(); // dump attributes into all six sprites
	set_y(reg_y-1); // decrement for original offset
	set_y(reg_y-1);
	set_a(ram[smb_enemy_rel_x_pos]); // get relative horizontal coordinate
	ram[smb_sprite_x_position + reg_y] = reg_a;
	ram[smb_sprite_x_position+0x0c + reg_y] = reg_a; // dump as X coordinate into first and fourth sprites
	reg_p.c = 0;
	add_a(0x08); // add eight pixels
	ram[smb_sprite_x_position+4 + reg_y] = reg_a; // dump into second and fifth sprites
	ram[smb_sprite_x_position+0x10 + reg_y] = reg_a;
	reg_p.c = 0;
	add_a(0x08); // add eight more pixels
	ram[smb_sprite_x_position+8 + reg_y] = reg_a; // dump into third and sixth sprites
	ram[smb_sprite_x_position+0x14 + reg_y] = reg_a;
	set_a(ram[smb_enemy_y_position + reg_x]); // get vertical coordinate
	set_x(reg_a);
	push(reg_a); // save to stack
	cmp_x(0x20); // if vertical coordinate below status bar,
	if (reg_p.c) { goto to_sp; } // do not mess with it
	set_a(0xf8); // otherwise move first three sprites offscreen
to_sp:;
	smb_dump_three_spr(); // dump vertical coordinate into Y coordinates
	set_a(pull()); // pull from stack
	reg_p.c = 0;
	add_a(0x80); // add 128 pixels
	set_x(reg_a);
	cmp_x(0x20); // if below status bar (taking wrap into account)
	if (reg_p.c) { goto bot_sp; } // then do not change altered coordinate
	set_a(0xf8); // otherwise move last three sprites offscreen
bot_sp:;
	ram[smb_sprite_y_position+0x0c + reg_y] = reg_a; // dump vertical coordinate + 128 pixels
	ram[smb_sprite_y_position+0x10 + reg_y] = reg_a; // into Y coordinates
	ram[smb_sprite_y_position+0x14 + reg_y] = reg_a;
	set_a(ram[smb_enemy_offscreen_bits]); // get offscreen bits
	push(reg_a); // save to stack
	and_a(0b00001000); // check d3
	if (reg_p.z) { goto s_ofs; }
	set_a(0xf8); // if d3 was set, move first and
	ram[smb_sprite_y_position + reg_y] = reg_a; // fourth sprites offscreen
	ram[smb_sprite_y_position+0x0c + reg_y] = reg_a;
s_ofs:;
	set_a(pull()); // move out and back into stack
	push(reg_a);
	and_a(0b00000100); // check d2
	if (reg_p.z) { goto s_ofs_2; }
	set_a(0xf8); // if d2 was set, move second and
	ram[smb_sprite_y_position+4 + reg_y] = reg_a; // fifth sprites offscreen
	ram[smb_sprite_y_position+0x10 + reg_y] = reg_a;
s_ofs_2:;
	set_a(pull()); // get from stack
	and_a(0b00000010); // check d1
	if (reg_p.z) { goto ex_s_pl; }
	set_a(0xf8); // if d1 was set, move third and
	ram[smb_sprite_y_position+8 + reg_y] = reg_a; // sixth sprites offscreen
	ram[smb_sprite_y_position+0x14 + reg_y] = reg_a;
ex_s_pl:;
	set_x(ram[smb_object_offset]); // get enemy object offset and leave
	return;
	smb_draw_bubble(); return;
}

void smb_draw_bubble() {
	set_y(ram[smb_player_y_high_pos]); // if player's vertical high position
	set_y(reg_y-1); // not within screen, skip all of this
	if (!reg_p.z) { goto ex_d_bub; }
	set_a(ram[smb_bubble_offscreen_bits]); // check air bubble's offscreen bits
	and_a(0b00001000);
	if (!reg_p.z) { goto ex_d_bub; } // if bit set, branch to leave
	set_y(ram[smb_bubble_spr_data_offset + reg_x]); // get air bubble's OAM data offset
	set_a(ram[smb_bubble_rel_x_pos]); // get relative horizontal coordinate
	ram[smb_sprite_x_position + reg_y] = reg_a; // store as X coordinate here
	set_a(ram[smb_bubble_rel_y_pos]); // get relative vertical coordinate
	ram[smb_sprite_y_position + reg_y] = reg_a; // store as Y coordinate here
	set_a(0x74);
	ram[smb_sprite_tilenumber + reg_y] = reg_a; // put air bubble tile into OAM data
	set_a(0x02);
	ram[smb_sprite_attributes + reg_y] = reg_a; // set attribute byte
ex_d_bub:;
	return; // leave
}

void smb_player_gfx_handler() {
	set_a(ram[smb_injury_timer]); // if player's injured invincibility timer
	if (reg_p.z) { goto cnt_pl; } // not set, skip checkpoint and continue code
	set_a(ram[smb_frame_counter]);
	reg_a = shr(reg_a); // otherwise check frame counter and branch
	if (reg_p.c) { goto ex_pgh; } // to leave on every other frame (when d0 is set)
cnt_pl:;
	set_a(ram[smb_game_engine_subroutine]); // if executing specific game engine routine,
	cmp_a(0x0b); // branch ahead to some other part
	if (reg_p.z) { smb_player_killed(); return; }
	set_a(ram[smb_player_change_size_flag]); // if grow/shrink flag set
	if (!reg_p.z) { smb_do_change_size(); return; } // then branch to some other code
	set_y(ram[smb_swimming_flag]); // if swimming flag set, branch to
	if (reg_p.z) { smb_find_player_action(); return; } // different part, do not return
	set_a(ram[smb_player_state]);
	cmp_a(0x00); // if player status normal,
	if (reg_p.z) { smb_find_player_action(); return; } // branch and do not return
	smb_find_player_action(); // otherwise jump and return
	set_a(ram[smb_frame_counter]);
	and_a(0b00000100); // check frame counter for d2 set (8 frames every
	if (!reg_p.z) { goto ex_pgh; } // eighth frame), and branch if set to leave
	set_x(reg_a); // initialize X to zero
	set_y(ram[smb_spr_data_offset]); // get player sprite data offset
	set_a(ram[smb_player_facing_dir]); // get player's facing direction
	reg_a = shr(reg_a);
	if (reg_p.c) { goto swim_kt; } // if player facing to the right, use current offset
	set_y(reg_y+1);
	set_y(reg_y+1); // otherwise move to next OAM data
	set_y(reg_y+1);
	set_y(reg_y+1);
swim_kt:;
	set_a(ram[smb_player_size]); // check player's size
	if (reg_p.z) { goto big_kts; } // if big, use first tile
	set_a(ram[smb_sprite_tilenumber+0x18 + reg_y]); // check tile number of seventh/eighth sprite
	cmp_a(rom[smb_swim_tiles+0x16]); // against tile number in player graphics table
	if (reg_p.z) { goto ex_pgh; } // if spr7/spr8 tile number = value, branch to leave
	set_x(reg_x+1); // otherwise increment X for second tile
big_kts:;
	set_a(rom[smb_swim_kick_tile_num + reg_x]); // overwrite tile number in sprite 7/8
	ram[smb_sprite_tilenumber+0x18 + reg_y] = reg_a; // to animate player's feet when swimming
ex_pgh:;
	return; // then leave
	smb_find_player_action(); return;
}

void smb_find_player_action() {
	smb_process_player_action(); // find proper offset to graphics table by player's actions
	smb_player_gfx_processing(); return; // draw player, then process for fireball throwing
	smb_do_change_size(); return;
}

void smb_do_change_size() {
	smb_handle_change_size(); // find proper offset to graphics table for grow/shrink
	smb_player_gfx_processing(); return; // draw player, then process for fireball throwing
	smb_player_killed(); return;
}

void smb_player_killed() {
	set_y(0x0e); // load offset for player killed
	set_a(rom[smb_player_gfx_tbl_offsets + reg_y]); // get offset to graphics table
	smb_player_gfx_processing(); return;
}

void smb_player_gfx_processing() {
	ram[smb_player_gfx_offset] = reg_a; // store offset to graphics table here
	set_a(0x04);
	smb_render_player_sub(); // draw player based on offset loaded
	smb_chk_for_player_attrib(); // set horizontal flip bits as necessary
	set_a(ram[smb_fireball_throwing_timer]);
	if (reg_p.z) { goto player_offscreen_chk; } // if fireball throw timer not set, skip to the end
	set_y(0x00); // set value to initialize by default
	set_a(ram[smb_player_anim_timer]); // get animation frame timer
	cmp_a(ram[smb_fireball_throwing_timer]); // compare to fireball throw timer
	ram[smb_fireball_throwing_timer] = reg_y; // initialize fireball throw timer
	if (reg_p.c) { goto player_offscreen_chk; } // if animation frame timer => fireball throw timer skip to end
	ram[smb_fireball_throwing_timer] = reg_a; // otherwise store animation timer into fireball throw timer
	set_y(0x07); // load offset for throwing
	set_a(rom[smb_player_gfx_tbl_offsets + reg_y]); // get offset to graphics table
	ram[smb_player_gfx_offset] = reg_a; // store it for use later
	set_y(0x04); // set to update four sprite rows by default
	set_a(ram[smb_player_x_speed]);
	or_a(ram[smb_left_right_buttons]); // check for horizontal speed or left/right button press
	if (reg_p.z) { goto s_upd_r; } // if no speed or button press, branch using set value in Y
	set_y(reg_y-1); // otherwise set to update only three sprite rows
s_upd_r:;
	set_a(reg_y); // save in A for use
	smb_render_player_sub(); // in sub, draw player object again
player_offscreen_chk:;
	set_a(ram[smb_player_offscreen_bits]); // get player's offscreen bits
	reg_a = shr(reg_a);
	reg_a = shr(reg_a); // move vertical bits to low nybble
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	ram[0x0000] = reg_a; // store here
	set_x(0x03); // check all four rows of player sprites
	set_a(ram[smb_spr_data_offset]); // get player's sprite data offset
	reg_p.c = 0;
	add_a(0x18); // add 24 bytes to start at bottom row
	set_y(reg_a); // set as offset here
pr_ofs_loop:;
	set_a(0xf8); // load offscreen Y coordinate just in case
	ram[0x0000] = shr(ram[0x0000]); // shift bit into carry
	if (!reg_p.c) { goto npr_offscr; } // if bit not set, skip, do not move sprites
	smb_dump_two_spr(); // otherwise dump offscreen Y coordinate into sprite data
npr_offscr:;
	set_a(reg_y);
	reg_p.c = 1; // subtract eight bytes to do
	sub_a(0x08); // next row up
	set_y(reg_a);
	set_x(reg_x-1); // decrement row counter
	if (!reg_p.n) { goto pr_ofs_loop; } // do this until all sprite rows are checked
	return; // then we are done!
}

void smb_draw_player_intermediate() {
	set_x(0x05); // store data into zero page memory
p_int_loop:;
	set_a(rom[smb_intermediate_player_data + reg_x]); // load data to display player as he always
	ram[0x0002 + reg_x] = reg_a; // appears on world/lives display
	set_x(reg_x-1);
	if (!reg_p.n) { goto p_int_loop; } // do this until all data is loaded
	set_x(0xb8); // load offset for small standing
	set_y(0x04); // load sprite data offset
	smb_draw_player_loop(); // draw player accordingly
	set_a(ram[smb_sprite_attributes+0x24]); // get empty sprite attributes
	or_a(0b01000000); // set horizontal flip bit for bottom-right sprite
	ram[smb_sprite_attributes+0x20] = reg_a; // store and leave
	return;
	smb_render_player_sub(); return;
}

void smb_render_player_sub() {
	ram[0x0007] = reg_a; // store number of rows of sprites to draw
	set_a(ram[smb_player_rel_x_pos]);
	ram[smb_player_pos_for_scroll] = reg_a; // store player's relative horizontal position
	ram[0x0005] = reg_a; // store it here also
	set_a(ram[smb_player_rel_y_pos]);
	ram[0x0002] = reg_a; // store player's vertical position
	set_a(ram[smb_player_facing_dir]);
	ram[0x0003] = reg_a; // store player's facing direction
	set_a(ram[smb_player_spr_attrib]);
	ram[0x0004] = reg_a; // store player's sprite attributes
	set_x(ram[smb_player_gfx_offset]); // load graphics table offset
	set_y(ram[smb_spr_data_offset]); // get player's sprite data offset
	smb_draw_player_loop(); return;
}

void smb_draw_player_loop() {
	set_a(rom[smb_player_graphics_table + reg_x]); // load player's left side
	ram[0x0000] = reg_a;
	set_a(rom[smb_player_graphics_table+1 + reg_x]); // now load right side
	smb_draw_one_sprite_row();
	ram[0x0007] = dec(ram[0x0007]); // decrement rows of sprites to draw
	if (!reg_p.z) { smb_draw_player_loop(); return; } // do this until all rows are drawn
	return;
	smb_process_player_action(); return;
}

void smb_process_player_action() {
	set_a(ram[smb_player_state]); // get player's state
	cmp_a(0x03);
	if (reg_p.z) { goto action_climbing; } // if climbing, branch here
	cmp_a(0x02);
	if (reg_p.z) { goto action_falling; } // if falling, branch here
	cmp_a(0x01);
	if (!reg_p.z) { goto proc_on_ground_acts; } // if not jumping, branch here
	set_a(ram[smb_swimming_flag]);
	if (!reg_p.z) { goto action_swimming; } // if swimming flag set, branch elsewhere
	set_y(0x06); // load offset for crouching
	set_a(ram[smb_crouching_flag]); // get crouching flag
	if (!reg_p.z) { goto non_animated_acts; } // if set, branch to get offset for graphics table
	set_y(0x00); // otherwise load offset for jumping
	goto non_animated_acts; // go to get offset to graphics table
proc_on_ground_acts:;
	set_y(0x06); // load offset for crouching
	set_a(ram[smb_crouching_flag]); // get crouching flag
	if (!reg_p.z) { goto non_animated_acts; } // if set, branch to get offset for graphics table
	set_y(0x02); // load offset for standing
	set_a(ram[smb_player_x_speed]); // check player's horizontal speed
	or_a(ram[smb_left_right_buttons]); // and left/right controller bits
	if (reg_p.z) { goto non_animated_acts; } // if no speed or buttons pressed, use standing offset
	set_a(ram[smb_player_x_speed_absolute]); // load walking/running speed
	cmp_a(0x09);
	if (!reg_p.c) { goto action_walk_run; } // if less than a certain amount, branch, too slow to skid
	set_a(ram[smb_player_moving_dir]); // otherwise check to see if moving direction
	and_a(ram[smb_player_facing_dir]); // and facing direction are the same
	if (!reg_p.z) { goto action_walk_run; } // if moving direction = facing direction, branch, don't skid
	set_y(reg_y+1); // otherwise increment to skid offset ($03)
non_animated_acts:;
	smb_get_gfx_offset_adder(); // do a sub here to get offset adder for graphics table
	set_a(0x00);
	ram[smb_player_anim_ctrl] = reg_a; // initialize animation frame control
	set_a(rom[smb_player_gfx_tbl_offsets + reg_y]); // load offset to graphics table using size as offset
	return;
action_falling:;
	set_y(0x04); // load offset for walking/running
	smb_get_gfx_offset_adder(); // get offset to graphics table
	smb_get_current_anim_offset(); return; // execute instructions for falling state
action_walk_run:;
	set_y(0x04); // load offset for walking/running
	smb_get_gfx_offset_adder(); // get offset to graphics table
	smb_four_frame_extent(); return; // execute instructions for normal state
action_climbing:;
	set_y(0x05); // load offset for climbing
	set_a(ram[smb_player_y_speed]); // check player's vertical speed
	if (reg_p.z) { goto non_animated_acts; } // if no speed, branch, use offset as-is
	smb_get_gfx_offset_adder(); // otherwise get offset for graphics table
	smb_three_frame_extent(); return; // then skip ahead to more code
action_swimming:;
	set_y(0x01); // load offset for swimming
	smb_get_gfx_offset_adder();
	set_a(ram[smb_jump_swim_timer]); // check jump/swim timer
	or_a(ram[smb_player_anim_ctrl]); // and animation frame control
	if (!reg_p.z) { smb_four_frame_extent(); return; } // if any one of these set, branch ahead
	set_a(ram[smb_a_b_buttons]);
	reg_a = shl(reg_a); // check for A button pressed
	if (reg_p.c) { smb_four_frame_extent(); return; } // branch to same place if A button pressed
	smb_get_current_anim_offset(); return;
}

void smb_get_current_anim_offset() {
	set_a(ram[smb_player_anim_ctrl]); // get animation frame control
	smb_get_offset_from_anim_ctrl(); return; // jump to get proper offset to graphics table
	smb_four_frame_extent(); return;
}

void smb_four_frame_extent() {
	set_a(0x03); // load upper extent for frame control
	smb_animation_control(); return; // jump to get offset and animate player object
	smb_three_frame_extent(); return;
}

void smb_three_frame_extent() {
	set_a(0x02); // load upper extent for frame control for climbing
	smb_animation_control(); return;
}

void smb_animation_control() {
	ram[0x0000] = reg_a; // store upper extent here
	smb_get_current_anim_offset(); // get proper offset to graphics table
	push(reg_a); // save offset to stack
	set_a(ram[smb_player_anim_timer]); // load animation frame timer
	if (!reg_p.z) { goto ex_anim_c; } // branch if not expired
	set_a(ram[smb_player_anim_timer_set]); // get animation frame timer amount
	ram[smb_player_anim_timer] = reg_a; // and set timer accordingly
	set_a(ram[smb_player_anim_ctrl]);
	reg_p.c = 0; // add one to animation frame control
	add_a(0x01);
	cmp_a(ram[0x0000]); // compare to upper extent
	if (!reg_p.c) { goto set_anim_c; } // if frame control + 1 < upper extent, use as next
	set_a(0x00); // otherwise initialize frame control
set_anim_c:;
	ram[smb_player_anim_ctrl] = reg_a; // store as new animation frame control
ex_anim_c:;
	set_a(pull()); // get offset to graphics table from stack and leave
	return;
	smb_get_gfx_offset_adder(); return;
}

void smb_get_gfx_offset_adder() {
	set_a(ram[smb_player_size]); // get player's size
	if (reg_p.z) { goto sz_ofs; } // if player big, use current offset as-is
	set_a(reg_y); // for big player
	reg_p.c = 0; // otherwise add eight bytes to offset
	add_a(0x08); // for small player
	set_y(reg_a); // go back
sz_ofs:;
	return;
}

void smb_handle_change_size() {
	set_y(ram[smb_player_anim_ctrl]); // get animation frame control
	set_a(ram[smb_frame_counter]);
	and_a(0b00000011); // get frame counter and execute this code every
	if (!reg_p.z) { goto gor_s_log; } // fourth frame, otherwise branch ahead
	set_y(reg_y+1); // increment frame control
	cmp_y(0x0a); // check for preset upper extent
	if (!reg_p.c) { goto csz_next; } // if not there yet, skip ahead to use
	set_y(0x00); // otherwise initialize both grow/shrink flag
	ram[smb_player_change_size_flag] = reg_y; // and animation frame control
csz_next:;
	ram[smb_player_anim_ctrl] = reg_y; // store proper frame control
gor_s_log:;
	set_a(ram[smb_player_size]); // get player's size
	if (!reg_p.z) { smb_shrink_player(); return; } // if player small, skip ahead to next part
	set_a(rom[smb_change_size_offset_adder + reg_y]); // get offset adder based on frame control as offset
	set_y(0x0f); // load offset for player growing
	smb_get_offset_from_anim_ctrl(); return;
}

void smb_get_offset_from_anim_ctrl() {
	reg_a = shl(reg_a); // multiply animation frame control
	reg_a = shl(reg_a); // by eight to get proper amount
	reg_a = shl(reg_a); // to add to our offset
	add_a(rom[smb_player_gfx_tbl_offsets + reg_y]); // add to offset to graphics table
	return; // and return with result in A
	smb_shrink_player(); return;
}

void smb_shrink_player() {
	set_a(reg_y); // add ten bytes to frame control as offset
	reg_p.c = 0;
	add_a(0x0a); // this thing apparently uses two of the swimming frames
	set_x(reg_a); // to draw the player shrinking
	set_y(0x09); // load offset for small player swimming
	set_a(rom[smb_change_size_offset_adder + reg_x]); // get what would normally be offset adder
	if (!reg_p.z) { goto shr_pl_f; } // and branch to use offset if nonzero
	set_y(0x01); // otherwise load offset for big player swimming
shr_pl_f:;
	set_a(rom[smb_player_gfx_tbl_offsets + reg_y]); // get offset to graphics table based on offset loaded
	return; // and leave
	smb_chk_for_player_attrib(); return;
}

void smb_chk_for_player_attrib() {
	set_y(ram[smb_spr_data_offset]); // get sprite data offset
	set_a(ram[smb_game_engine_subroutine]);
	cmp_a(0x0b); // if executing specific game engine routine,
	if (reg_p.z) { goto killed_att; } // branch to change third and fourth row OAM attributes
	set_a(ram[smb_player_gfx_offset]); // get graphics table offset
	cmp_a(0x50);
	if (reg_p.z) { goto c_s_ig_att; } // if crouch offset, either standing offset,
	cmp_a(0xb8); // or intermediate growing offset,
	if (reg_p.z) { goto c_s_ig_att; } // go ahead and execute code to change
	cmp_a(0xc0); // fourth row OAM attributes only
	if (reg_p.z) { goto c_s_ig_att; }
	cmp_a(0xc8);
	if (!reg_p.z) { goto ex_plyr_at; } // if none of these, branch to leave
killed_att:;
	set_a(ram[smb_sprite_attributes+0x10 + reg_y]);
	and_a(0b00111111); // mask out horizontal and vertical flip bits
	ram[smb_sprite_attributes+0x10 + reg_y] = reg_a; // for third row sprites and save
	set_a(ram[smb_sprite_attributes+0x14 + reg_y]);
	and_a(0b00111111);
	or_a(0b01000000); // set horizontal flip bit for second
	ram[smb_sprite_attributes+0x14 + reg_y] = reg_a; // sprite in the third row
c_s_ig_att:;
	set_a(ram[smb_sprite_attributes+0x18 + reg_y]);
	and_a(0b00111111); // mask out horizontal and vertical flip bits
	ram[smb_sprite_attributes+0x18 + reg_y] = reg_a; // for fourth row sprites and save
	set_a(ram[smb_sprite_attributes+0x1c + reg_y]);
	and_a(0b00111111);
	or_a(0b01000000); // set horizontal flip bit for second
	ram[smb_sprite_attributes+0x1c + reg_y] = reg_a; // sprite in the fourth row
ex_plyr_at:;
	return; // leave
	smb_relative_player_position(); return;
}

void smb_relative_player_position() {
	set_x(0x00); // set offsets for relative cooordinates
	set_y(0x00); // routine to correspond to player object
	smb_rel_w_ofs(); return; // get the coordinates
	smb_relative_bubble_position(); return;
}

void smb_relative_bubble_position() {
	set_y(0x01); // set for air bubble offsets
	smb_get_proper_obj_offset(); // modify X to get proper air bubble offset
	set_y(0x03);
	smb_rel_w_ofs(); return; // get the coordinates
	smb_relative_fireball_position(); return;
}

void smb_relative_fireball_position() {
	set_y(0x00); // set for fireball offsets
	smb_get_proper_obj_offset(); // modify X to get proper fireball offset
	set_y(0x02);
	smb_rel_w_ofs(); return;
}

void smb_rel_w_ofs() {
	smb_get_obj_relative_position(); // get the coordinates
	set_x(ram[smb_object_offset]); // return original offset
	return; // leave
	smb_relative_misc_position(); return;
}

void smb_relative_misc_position() {
	set_y(0x02); // set for misc object offsets
	smb_get_proper_obj_offset(); // modify X to get proper misc object offset
	set_y(0x06);
	smb_rel_w_ofs(); return; // get the coordinates
	smb_relative_enemy_position(); return;
}

void smb_relative_enemy_position() {
	set_a(0x01); // get coordinates of enemy object
	set_y(0x01); // relative to the screen
	smb_variable_obj_ofs_rel_pos(); return;
	smb_relative_block_position(); return;
}

void smb_relative_block_position() {
	set_a(0x09); // get coordinates of one block object
	set_y(0x04); // relative to the screen
	smb_variable_obj_ofs_rel_pos();
	set_x(reg_x+1); // adjust offset for other block object if any
	set_x(reg_x+1);
	set_a(0x09);
	set_y(reg_y+1); // adjust other and get coordinates for other one
	smb_variable_obj_ofs_rel_pos(); return;
}

void smb_variable_obj_ofs_rel_pos() {
	ram[0x0000] = reg_x; // store value to add to A here
	reg_p.c = 0;
	add_a(ram[0x0000]); // add A to value stored
	set_x(reg_a); // use as enemy offset
	smb_get_obj_relative_position();
	set_x(ram[smb_object_offset]); // reload old object offset and leave
	return;
	smb_get_obj_relative_position(); return;
}

void smb_get_obj_relative_position() {
	set_a(ram[smb_spr_object_y_position + reg_x]); // load vertical coordinate low
	ram[smb_spr_object_rel_y_pos + reg_y] = reg_a; // store here
	set_a(ram[smb_spr_object_x_position + reg_x]); // load horizontal coordinate
	reg_p.c = 1; // subtract left edge coordinate
	sub_a(ram[smb_screen_left_x_pos]);
	ram[smb_spr_object_rel_x_pos + reg_y] = reg_a; // store result here
	return;
	smb_get_player_offscreen_bits(); return;
}

void smb_get_player_offscreen_bits() {
	set_x(0x00); // set offsets for player-specific variables
	set_y(0x00); // and get offscreen information about player
	smb_get_off_screen_bits_set(); return;
	smb_get_fireball_offscreen_bits(); return;
}

void smb_get_fireball_offscreen_bits() {
	set_y(0x00); // set for fireball offsets
	smb_get_proper_obj_offset(); // modify X to get proper fireball offset
	set_y(0x02); // set other offset for fireball's offscreen bits
	smb_get_off_screen_bits_set(); return; // and get offscreen information about fireball
	smb_get_bubble_offscreen_bits(); return;
}

void smb_get_bubble_offscreen_bits() {
	set_y(0x01); // set for air bubble offsets
	smb_get_proper_obj_offset(); // modify X to get proper air bubble offset
	set_y(0x03); // set other offset for airbubble's offscreen bits
	smb_get_off_screen_bits_set(); return; // and get offscreen information about air bubble
	smb_get_misc_offscreen_bits(); return;
}

void smb_get_misc_offscreen_bits() {
	set_y(0x02); // set for misc object offsets
	smb_get_proper_obj_offset(); // modify X to get proper misc object offset
	set_y(0x06); // set other offset for misc object's offscreen bits
	smb_get_off_screen_bits_set(); return; // and get offscreen information about misc object
}

void smb_get_proper_obj_offset() {
	set_a(reg_x); // move offset to A
	reg_p.c = 0;
	add_a(rom[smb_obj_offset_data + reg_y]); // add amount of bytes to offset depending on setting in Y
	set_x(reg_a); // put back in X and leave
	return;
	smb_get_enemy_offscreen_bits(); return;
}

void smb_get_enemy_offscreen_bits() {
	set_a(0x01); // set A to add 1 byte in order to get enemy offset
	set_y(0x01); // set Y to put offscreen bits in smb_enemy_offscreen_bits
	smb_set_offscr_bits_offset(); return;
	smb_get_block_offscreen_bits(); return;
}

void smb_get_block_offscreen_bits() {
	set_a(0x09); // set A to add 9 bytes in order to get block obj offset
	set_y(0x04); // set Y to put offscreen bits in smb_block_offscreen_bits
	smb_set_offscr_bits_offset(); return;
}

void smb_set_offscr_bits_offset() {
	ram[0x0000] = reg_x;
	reg_p.c = 0; // add contents of X to A to get
	add_a(ram[0x0000]); // appropriate offset, then give back to X
	set_x(reg_a);
	smb_get_off_screen_bits_set(); return;
}

void smb_get_off_screen_bits_set() {
	set_a(reg_y); // save offscreen bits offset to stack for now
	push(reg_a);
	smb_run_offscr_bits_subs();
	reg_a = shl(reg_a); // move low nybble to high nybble
	reg_a = shl(reg_a);
	reg_a = shl(reg_a);
	reg_a = shl(reg_a);
	or_a(ram[0x0000]); // mask together with previously saved low nybble
	ram[0x0000] = reg_a; // store both here
	set_a(pull()); // get offscreen bits offset from stack
	set_y(reg_a);
	set_a(ram[0x0000]); // get value here and store elsewhere
	ram[smb_spr_object_offscr_bits + reg_y] = reg_a;
	set_x(ram[smb_object_offset]);
	return;
	smb_run_offscr_bits_subs(); return;
}

void smb_run_offscr_bits_subs() {
	smb_get_x_offscreen_bits(); // do subroutine here
	reg_a = shr(reg_a); // move high nybble to low
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	ram[0x0000] = reg_a; // store here
	smb_get_y_offscreen_bits(); return;
}

void smb_get_x_offscreen_bits() {
	ram[0x0004] = reg_x; // save position in buffer to here
	set_y(0x01); // start with right side of screen
x_ofs_loop:;
	set_a(ram[smb_screen_left_x_pos + reg_y]); // get pixel coordinate of edge
	reg_p.c = 1; // get difference between pixel coordinate of edge
	sub_a(ram[smb_spr_object_x_position + reg_x]); // and pixel coordinate of object position
	ram[0x0007] = reg_a; // store here
	set_a(ram[smb_screen_left_page_loc + reg_y]); // get page location of edge
	sub_a(ram[smb_spr_object_page_loc + reg_x]); // subtract from page location of object position
	set_x(rom[smb_default_x_onscreen_ofs + reg_y]); // load offset value here
	cmp_a(0x00);
	if (reg_p.n) { goto x_ld_b_data; } // if beyond right edge or in front of left edge, branch
	set_x(rom[smb_default_x_onscreen_ofs+1 + reg_y]); // if not, load alternate offset value here
	cmp_a(0x01);
	if (!reg_p.n) { goto x_ld_b_data; } // if one page or more to the left of either edge, branch
	set_a(0x38); // if no branching, load value here and store
	ram[0x0006] = reg_a;
	set_a(0x08); // load some other value and execute subroutine
	smb_divide_p_diff();
x_ld_b_data:;
	set_a(rom[smb_x_offscreen_bits_data + reg_x]); // get bits here
	set_x(ram[0x0004]); // reobtain position in buffer
	cmp_a(0x00); // if bits not zero, branch to leave
	if (!reg_p.z) { goto ex_xofs_bs; }
	set_y(reg_y-1); // otherwise, do left side of screen now
	if (!reg_p.n) { goto x_ofs_loop; } // branch if not already done with left side
ex_xofs_bs:;
	return;
}

void smb_get_y_offscreen_bits() {
	ram[0x0004] = reg_x; // save position in buffer to here
	set_y(0x01); // start with top of screen
y_ofs_loop:;
	set_a(rom[smb_high_pos_unit_data + reg_y]); // load coordinate for edge of vertical unit
	reg_p.c = 1;
	sub_a(ram[smb_spr_object_y_position + reg_x]); // subtract from vertical coordinate of object
	ram[0x0007] = reg_a; // store here
	set_a(0x01); // subtract one from vertical high byte of object
	sub_a(ram[smb_spr_object_y_high_pos + reg_x]);
	set_x(rom[smb_default_y_onscreen_ofs + reg_y]); // load offset value here
	cmp_a(0x00);
	if (reg_p.n) { goto y_ld_b_data; } // if under top of the screen or beyond bottom, branch
	set_x(rom[smb_default_y_onscreen_ofs+1 + reg_y]); // if not, load alternate offset value here
	cmp_a(0x01);
	if (!reg_p.n) { goto y_ld_b_data; } // if one vertical unit or more above the screen, branch
	set_a(0x20); // if no branching, load value here and store
	ram[0x0006] = reg_a;
	set_a(0x04); // load some other value and execute subroutine
	smb_divide_p_diff();
y_ld_b_data:;
	set_a(rom[smb_y_offscreen_bits_data + reg_x]); // get offscreen data bits using offset
	set_x(ram[0x0004]); // reobtain position in buffer
	cmp_a(0x00);
	if (!reg_p.z) { goto ex_y_ofs_bs; } // if bits not zero, branch to leave
	set_y(reg_y-1); // otherwise, do bottom of the screen now
	if (!reg_p.n) { goto y_ofs_loop; }
ex_y_ofs_bs:;
	return;
	smb_divide_p_diff(); return;
}

void smb_divide_p_diff() {
	ram[0x0005] = reg_a; // store current value in A here
	set_a(ram[0x0007]); // get pixel difference
	cmp_a(ram[0x0006]); // compare to preset value
	if (reg_p.c) { goto ex_div_pd; } // if pixel difference >= preset value, branch
	reg_a = shr(reg_a); // divide by eight
	reg_a = shr(reg_a);
	reg_a = shr(reg_a);
	and_a(0x07); // mask out all but 3 LSB
	cmp_y(0x01); // right side of the screen or top?
	if (reg_p.c) { goto set_oscr_o; } // if so, branch, use difference / 8 as offset
	add_a(ram[0x0005]); // if not, add value to difference / 8
set_oscr_o:;
	set_x(reg_a); // use as offset
ex_div_pd:;
	return; // leave
	smb_draw_sprite_object(); return;
}

void smb_draw_sprite_object() {
	set_a(ram[0x0003]); // get saved flip control bits
	reg_a = shr(reg_a);
	reg_a = shr(reg_a); // move d1 into carry
	set_a(ram[0x0000]);
	if (!reg_p.c) { goto no_h_flip; } // if d1 not set, branch
	ram[smb_sprite_tilenumber+4 + reg_y] = reg_a; // store first tile into second sprite
	set_a(ram[0x0001]); // and second into first sprite
	ram[smb_sprite_tilenumber + reg_y] = reg_a;
	set_a(0x40); // activate horizontal flip OAM attribute
	if (!reg_p.z) { goto set_hf_at; } // and unconditionally branch
no_h_flip:;
	ram[smb_sprite_tilenumber + reg_y] = reg_a; // store first tile into first sprite
	set_a(ram[0x0001]); // and second into second sprite
	ram[smb_sprite_tilenumber+4 + reg_y] = reg_a;
	set_a(0x00); // clear bit for horizontal flip
set_hf_at:;
	or_a(ram[0x0004]); // add other OAM attributes if necessary
	ram[smb_sprite_attributes + reg_y] = reg_a; // store sprite attributes
	ram[smb_sprite_attributes+4 + reg_y] = reg_a;
	set_a(ram[0x0002]); // now the y coordinates
	ram[smb_sprite_y_position + reg_y] = reg_a; // note because they are
	ram[smb_sprite_y_position+4 + reg_y] = reg_a; // side by side, they are the same
	set_a(ram[0x0005]);
	ram[smb_sprite_x_position + reg_y] = reg_a; // store x coordinate, then
	reg_p.c = 0; // add 8 pixels and store another to
	add_a(0x08); // put them side by side
	ram[smb_sprite_x_position+4 + reg_y] = reg_a;
	set_a(ram[0x0002]); // add eight pixels to the next y
	reg_p.c = 0; // coordinate
	add_a(0x08);
	ram[0x0002] = reg_a;
	set_a(reg_y); // add eight to the offset in Y to
	reg_p.c = 0; // move to the next two sprites
	add_a(0x08);
	set_y(reg_a);
	set_x(reg_x+1); // increment offset to return it to the
	set_x(reg_x+1); // routine that called this subroutine
	return;
}

void smb_sound_engine() {
	set_a(ram[smb_oper_mode]); // are we in title screen mode?
	if (!reg_p.z) { goto snd_on; }
	env_snd_chn_w(reg_a); // if so, disable sound and leave
	return;
snd_on:;
	set_a(0xff);
	env_joy2_w(reg_a); // disable irqs and set frame counter mode???
	set_a(0x0f);
	env_snd_chn_w(reg_a); // enable first four channels
	set_a(ram[smb_pause_mode_flag]); // is sound already in pause mode?
	if (!reg_p.z) { goto in_pause; }
	set_a(ram[smb_pause_sound_queue]); // if not, check pause sfx queue
	cmp_a(0x01);
	if (!reg_p.z) { goto run_sound_subroutines; } // if queue is empty, skip pause mode routine
in_pause:;
	set_a(ram[smb_pause_sound_buffer]); // check pause sfx buffer
	if (!reg_p.z) { goto cont_pau; }
	set_a(ram[smb_pause_sound_queue]); // check pause queue
	if (reg_p.z) { goto skip_sound_subroutines; }
	ram[smb_pause_sound_buffer] = reg_a; // if queue full, store in buffer and activate
	ram[smb_pause_mode_flag] = reg_a; // pause mode to interrupt game sounds
	set_a(0x00); // disable sound and clear sfx buffers
	env_snd_chn_w(reg_a);
	ram[smb_square_1_sound_buffer] = reg_a;
	ram[smb_square_2_sound_buffer] = reg_a;
	ram[smb_noise_sound_buffer] = reg_a;
	set_a(0x0f);
	env_snd_chn_w(reg_a); // enable sound again
	set_a(0x2a); // store length of sound in pause counter
	ram[smb_squ_1_sfx_len_counter] = reg_a;
p_tone_1_f:;
	set_a(0x44); // play first tone
	if (!reg_p.z) { goto pt_reg_c; } // unconditional branch
cont_pau:;
	set_a(ram[smb_squ_1_sfx_len_counter]); // check pause length left
	cmp_a(0x24); // time to play second?
	if (reg_p.z) { goto p_tone_2_f; }
	cmp_a(0x1e); // time to play first again?
	if (reg_p.z) { goto p_tone_1_f; }
	cmp_a(0x18); // time to play second again?
	if (!reg_p.z) { goto dec_pau_c; } // only load regs during times, otherwise skip
p_tone_2_f:;
	set_a(0x64); // store reg contents and play the pause sfx
pt_reg_c:;
	set_x(0x84);
	set_y(0x7f);
	smb_play_squ_1_sfx();
dec_pau_c:;
	ram[smb_squ_1_sfx_len_counter] = dec(ram[smb_squ_1_sfx_len_counter]); // decrement pause sfx counter
	if (!reg_p.z) { goto skip_sound_subroutines; }
	set_a(0x00); // disable sound if in pause mode and
	env_snd_chn_w(reg_a); // not currently playing the pause sfx
	set_a(ram[smb_pause_sound_buffer]); // if no longer playing pause sfx, check to see
	cmp_a(0x02); // if we need to be playing sound again
	if (!reg_p.z) { goto skip_p_in; }
	set_a(0x00); // clear pause mode to allow game sounds again
	ram[smb_pause_mode_flag] = reg_a;
skip_p_in:;
	set_a(0x00); // clear pause sfx buffer
	ram[smb_pause_sound_buffer] = reg_a;
	if (reg_p.z) { goto skip_sound_subroutines; }
run_sound_subroutines:;
	smb_square_1_sfx_handler(); // play sfx on square channel 1
	smb_square_2_sfx_handler(); // ''  ''  '' square channel 2
	smb_noise_sfx_handler(); // ''  ''  '' noise channel
	smb_music_handler(); // play music on all channels
	set_a(0x00); // clear the music queues
	ram[smb_area_music_queue] = reg_a;
	ram[smb_event_music_queue] = reg_a;
skip_sound_subroutines:;
	set_a(0x00); // clear the sound effects queues
	ram[smb_square_1_sound_queue] = reg_a;
	ram[smb_square_2_sound_queue] = reg_a;
	ram[smb_noise_sound_queue] = reg_a;
	ram[smb_pause_sound_queue] = reg_a;
	set_y(ram[smb_dac_counter]); // load some sort of counter
	set_a(ram[smb_area_music_buffer]);
	and_a(0b00000011); // check for specific music
	if (reg_p.z) { goto no_inc_dac; }
	ram[smb_dac_counter] = inc(ram[smb_dac_counter]); // increment and check counter
	cmp_y(0x30);
	if (!reg_p.c) { goto str_wave; } // if not there yet, just store it
no_inc_dac:;
	set_a(reg_y);
	if (reg_p.z) { goto str_wave; } // if we are at zero, do not decrement
	ram[smb_dac_counter] = dec(ram[smb_dac_counter]); // decrement counter
str_wave:;
	env_dmc_raw_w(reg_y); // store into DMC load register (??)
	return; // we are done here
	smb_dump_squ_1_regs(); return;
}

void smb_dump_squ_1_regs() {
	env_sq1_sweep_w(reg_y); // dump the contents of X and Y into square 1's control regs
	env_sq1_vol_w(reg_x);
	return;
	smb_play_squ_1_sfx(); return;
}

void smb_play_squ_1_sfx() {
	smb_dump_squ_1_regs(); // do sub to set ctrl regs for square 1, then set frequency regs
	smb_set_freq_squ_1(); return;
}

void smb_set_freq_squ_1() {
	set_x(0x00); // set frequency reg offset for square 1 sound channel
	smb_dump_freq_regs(); return;
}

void smb_dump_freq_regs() {
	set_y(reg_a);
	set_a(rom[smb_freq_reg_lookup_tbl+1 + reg_y]); // use previous contents of A for sound reg offset
	if (reg_p.z) { goto no_tone; } // if zero, then do not load
	if (reg_x==0) { env_sq1_lo_w(reg_a); } else if (reg_x==4) { env_sq2_lo_w(reg_a); } else if (reg_x==8) { env_tri_lo_w(reg_a); } // first byte goes into LSB of frequency divider
	set_a(rom[smb_freq_reg_lookup_tbl + reg_y]); // second byte goes into 3 MSB plus extra bit for
	or_a(0b00001000); // length counter
	if (reg_x==0) { env_sq1_hi_w(reg_a); } else if (reg_x==4) { env_sq2_hi_w(reg_a); } else if (reg_x==8) { env_tri_hi_w(reg_a); }
no_tone:;
	return;
	smb_dump_sq_2_regs(); return;
}

void smb_dump_sq_2_regs() {
	env_sq2_vol_w(reg_x); // dump the contents of X and Y into square 2's control regs
	env_sq2_sweep_w(reg_y);
	return;
	smb_play_squ_2_sfx(); return;
}

void smb_play_squ_2_sfx() {
	smb_dump_sq_2_regs(); // do sub to set ctrl regs for square 2, then set frequency regs
	smb_set_freq_squ_2(); return;
}

void smb_set_freq_squ_2() {
	set_x(0x04); // set frequency reg offset for square 2 sound channel
	if (!reg_p.z) { smb_dump_freq_regs(); return; } // unconditional branch
	smb_set_freq_tri(); return;
}

void smb_set_freq_tri() {
	set_x(0x08); // set frequency reg offset for triangle sound channel
	if (!reg_p.z) { smb_dump_freq_regs(); return; } // unconditional branch
}

void smb_play_flagpole_slide() {
	set_a(0x40); // store length of flagpole sound
	ram[smb_squ_1_sfx_len_counter] = reg_a;
	set_a(0x62); // load part of reg contents for flagpole sound
	smb_set_freq_squ_1();
	set_x(0x99); // now load the rest
	if (!reg_p.z) { smb_fps_2_nd(); return; }
	smb_play_small_jump(); return;
}

void smb_play_small_jump() {
	set_a(0x26); // branch here for small mario jumping sound
	if (!reg_p.z) { smb_jump_reg_contents(); return; }
	smb_play_big_jump(); return;
}

void smb_play_big_jump() {
	set_a(0x18); // branch here for big mario jumping sound
	smb_jump_reg_contents(); return;
}

void smb_jump_reg_contents() {
	set_x(0x82); // note that small and big jump borrow each others' reg contents
	set_y(0xa7); // anyway, this loads the first part of mario's jumping sound
	smb_play_squ_1_sfx();
	set_a(0x28); // store length of sfx for both jumping sounds
	ram[smb_squ_1_sfx_len_counter] = reg_a; // then continue on here
	smb_continue_snd_jump(); return;
}

void smb_continue_snd_jump() {
	set_a(ram[smb_squ_1_sfx_len_counter]); // jumping sounds seem to be composed of three parts
	cmp_a(0x25); // check for time to play second part yet
	if (!reg_p.z) { goto n_2_prt; }
	set_x(0x5f); // load second part
	set_y(0xf6);
	if (!reg_p.z) { smb_dmp_jp_fps(); return; } // unconditional branch
n_2_prt:;
	cmp_a(0x20); // check for third part
	if (!reg_p.z) { smb_dec_jp_fps(); return; }
	set_x(0x48); // load third part
	smb_fps_2_nd(); return;
}

void smb_fps_2_nd() {
	set_y(0xbc); // the flagpole slide sound shares part of third part
	smb_dmp_jp_fps(); return;
}

void smb_dmp_jp_fps() {
	smb_dump_squ_1_regs();
	if (!reg_p.z) { smb_dec_jp_fps(); return; } // unconditional branch outta here
	smb_play_fireball_throw(); return;
}

void smb_play_fireball_throw() {
	set_a(0x05);
	set_y(0x99); // load reg contents for fireball throw sound
	if (!reg_p.z) { smb_fthrow(); return; } // unconditional branch
	smb_play_bump(); return;
}

void smb_play_bump() {
	set_a(0x0a); // load length of sfx and reg contents for bump sound
	set_y(0x93);
	smb_fthrow(); return;
}

void smb_fthrow() {
	set_x(0x9e); // the fireball sound shares reg contents with the bump sound
	ram[smb_squ_1_sfx_len_counter] = reg_a;
	set_a(0x0c); // load offset for bump sound
	smb_play_squ_1_sfx();
	smb_continue_bump_throw(); return;
}

void smb_continue_bump_throw() {
	set_a(ram[smb_squ_1_sfx_len_counter]); // check for second part of bump sound
	cmp_a(0x06);
	if (!reg_p.z) { smb_dec_jp_fps(); return; }
	set_a(0xbb); // load second part directly
	env_sq1_sweep_w(reg_a);
	smb_dec_jp_fps(); return;
}

void smb_dec_jp_fps() {
	if (!reg_p.z) { smb_branch_to_dec_length_1(); return; } // unconditional branch
	smb_square_1_sfx_handler(); return;
}

void smb_square_1_sfx_handler() {
	set_y(ram[smb_square_1_sound_queue]); // check for sfx in queue
	if (reg_p.z) { goto check_sfx_1_buffer; }
	ram[smb_square_1_sound_buffer] = reg_y; // if found, put in buffer
	if (reg_p.n) { smb_play_small_jump(); return; } // small jump
	ram[smb_square_1_sound_queue] = shr(ram[smb_square_1_sound_queue]);
	if (reg_p.c) { smb_play_big_jump(); return; } // big jump
	ram[smb_square_1_sound_queue] = shr(ram[smb_square_1_sound_queue]);
	if (reg_p.c) { smb_play_bump(); return; } // bump
	ram[smb_square_1_sound_queue] = shr(ram[smb_square_1_sound_queue]);
	if (reg_p.c) { goto play_swim_stomp; } // swim/stomp
	ram[smb_square_1_sound_queue] = shr(ram[smb_square_1_sound_queue]);
	if (reg_p.c) { smb_play_smack_enemy(); return; } // smack enemy
	ram[smb_square_1_sound_queue] = shr(ram[smb_square_1_sound_queue]);
	if (reg_p.c) { smb_play_pipe_down_inj(); return; } // pipedown/injury
	ram[smb_square_1_sound_queue] = shr(ram[smb_square_1_sound_queue]);
	if (reg_p.c) { smb_play_fireball_throw(); return; } // fireball throw
	ram[smb_square_1_sound_queue] = shr(ram[smb_square_1_sound_queue]);
	if (reg_p.c) { smb_play_flagpole_slide(); return; } // slide flagpole
check_sfx_1_buffer:;
	set_a(ram[smb_square_1_sound_buffer]); // check for sfx in buffer
	if (reg_p.z) { goto ex_s_1_h; } // if not found, exit sub
	if (reg_p.n) { smb_continue_snd_jump(); return; } // small mario jump
	reg_a = shr(reg_a);
	if (reg_p.c) { smb_continue_snd_jump(); return; } // big mario jump
	reg_a = shr(reg_a);
	if (reg_p.c) { smb_continue_bump_throw(); return; } // bump
	reg_a = shr(reg_a);
	if (reg_p.c) { goto continue_swim_stomp; } // swim/stomp
	reg_a = shr(reg_a);
	if (reg_p.c) { smb_continue_smack_enemy(); return; } // smack enemy
	reg_a = shr(reg_a);
	if (reg_p.c) { smb_continue_pipe_down_inj(); return; } // pipedown/injury
	reg_a = shr(reg_a);
	if (reg_p.c) { smb_continue_bump_throw(); return; } // fireball throw
	reg_a = shr(reg_a);
	if (reg_p.c) { smb_decrement_sfx_1_length(); return; } // slide flagpole
ex_s_1_h:;
	return;
play_swim_stomp:;
	set_a(0x0e); // store length of swim/stomp sound
	ram[smb_squ_1_sfx_len_counter] = reg_a;
	set_y(0x9c); // store reg contents for swim/stomp sound
	set_x(0x9e);
	set_a(0x26);
	smb_play_squ_1_sfx();
continue_swim_stomp:;
	set_y(ram[smb_squ_1_sfx_len_counter]); // look up reg contents in data section based on
	set_a(rom[smb_swim_stomp_envelope_data-1 + reg_y]); // length of sound left, used to control sound's
	env_sq1_vol_w(reg_a); // envelope
	cmp_y(0x06);
	if (!reg_p.z) { smb_branch_to_dec_length_1(); return; }
	set_a(0x9e); // when the length counts down to a certain point, put this
	env_sq1_lo_w(reg_a); // directly into the LSB of square 1's frequency divider
	smb_branch_to_dec_length_1(); return;
}

void smb_branch_to_dec_length_1() {
	if (!reg_p.z) { smb_decrement_sfx_1_length(); return; } // unconditional branch (regardless of how we got here)
	smb_play_smack_enemy(); return;
}

void smb_play_smack_enemy() {
	set_a(0x0e); // store length of smack enemy sound
	set_y(0xcb);
	set_x(0x9f);
	ram[smb_squ_1_sfx_len_counter] = reg_a;
	set_a(0x28); // store reg contents for smack enemy sound
	smb_play_squ_1_sfx();
	if (!reg_p.z) { smb_decrement_sfx_1_length(); return; } // unconditional branch
	smb_continue_smack_enemy(); return;
}

void smb_continue_smack_enemy() {
	set_y(ram[smb_squ_1_sfx_len_counter]); // check about halfway through
	cmp_y(0x08);
	if (!reg_p.z) { goto sm_spc; }
	set_a(0xa0); // if we're at the about-halfway point, make the second tone
	env_sq1_lo_w(reg_a); // in the smack enemy sound
	set_a(0x9f);
	if (!reg_p.z) { goto sm_tick; }
sm_spc:;
	set_a(0x90); // this creates spaces in the sound, giving it its distinct noise
sm_tick:;
	env_sq1_vol_w(reg_a);
	smb_decrement_sfx_1_length(); return;
}

void smb_decrement_sfx_1_length() {
	ram[smb_squ_1_sfx_len_counter] = dec(ram[smb_squ_1_sfx_len_counter]); // decrement length of sfx
	if (!reg_p.z) { smb_ex_sfx_1(); return; }
	smb_stop_square_1_sfx(); return;
}

void smb_stop_square_1_sfx() {
	set_x(0x00); // if end of sfx reached, clear buffer
	ram[smb_square_1_sound_buffer] = reg_x; // and stop making the sfx
	set_x(0x0e);
	env_snd_chn_w(reg_x);
	set_x(0x0f);
	env_snd_chn_w(reg_x);
	smb_ex_sfx_1(); return;
}

void smb_ex_sfx_1() {
	return;
	smb_play_pipe_down_inj(); return;
}

void smb_play_pipe_down_inj() {
	set_a(0x2f); // load length of pipedown sound
	ram[smb_squ_1_sfx_len_counter] = reg_a;
	smb_continue_pipe_down_inj(); return;
}

void smb_continue_pipe_down_inj() {
	set_a(ram[smb_squ_1_sfx_len_counter]); // some bitwise logic, forces the regs
	reg_a = shr(reg_a); // to be written to only during six specific times
	if (reg_p.c) { goto no_p_dwn_l; } // during which d3 must be set and d1-0 must be clear
	reg_a = shr(reg_a);
	if (reg_p.c) { goto no_p_dwn_l; }
	and_a(0b00000010);
	if (reg_p.z) { goto no_p_dwn_l; }
	set_y(0x91); // and this is where it actually gets written in
	set_x(0x9a);
	set_a(0x44);
	smb_play_squ_1_sfx();
no_p_dwn_l:;
	smb_decrement_sfx_1_length(); return;
}

void smb_play_coin_grab() {
	set_a(0x35); // load length of coin grab sound
	set_x(0x8d); // and part of reg contents
	if (!reg_p.z) { smb_c_grab_t_tick_reg_l(); return; }
	smb_play_timer_tick(); return;
}

void smb_play_timer_tick() {
	set_a(0x06); // load length of timer tick sound
	set_x(0x98); // and part of reg contents
	smb_c_grab_t_tick_reg_l(); return;
}

void smb_c_grab_t_tick_reg_l() {
	ram[smb_squ_2_sfx_len_counter] = reg_a;
	set_y(0x7f); // load the rest of reg contents
	set_a(0x42); // of coin grab and timer tick sound
	smb_play_squ_2_sfx();
	smb_continue_c_grab_t_tick(); return;
}

void smb_continue_c_grab_t_tick() {
	set_a(ram[smb_squ_2_sfx_len_counter]); // check for time to play second tone yet
	cmp_a(0x30); // timer tick sound also executes this, not sure why
	if (!reg_p.z) { goto n_2_tone; }
	set_a(0x54); // if so, load the tone directly into the reg
	env_sq2_lo_w(reg_a);
n_2_tone:;
	if (!reg_p.z) { smb_decrement_sfx_2_length(); return; }
	smb_play_blast(); return;
}

void smb_play_blast() {
	set_a(0x20); // load length of fireworks/gunfire sound
	ram[smb_squ_2_sfx_len_counter] = reg_a;
	set_y(0x94); // load reg contents of fireworks/gunfire sound
	set_a(0x5e);
	if (!reg_p.z) { smb_s_blas_j(); return; }
	smb_continue_blast(); return;
}

void smb_continue_blast() {
	set_a(ram[smb_squ_2_sfx_len_counter]); // check for time to play second part
	cmp_a(0x18);
	if (!reg_p.z) { smb_decrement_sfx_2_length(); return; }
	set_y(0x93); // load second part reg contents then
	set_a(0x18);
	smb_s_blas_j(); return;
}

void smb_s_blas_j() {
	if (!reg_p.z) { smb_blst_s_jp(); return; } // unconditional branch to load rest of reg contents
	smb_play_power_up_grab(); return;
}

void smb_play_power_up_grab() {
	set_a(0x36); // load length of power-up grab sound
	ram[smb_squ_2_sfx_len_counter] = reg_a;
	smb_continue_power_up_grab(); return;
}

void smb_continue_power_up_grab() {
	set_a(ram[smb_squ_2_sfx_len_counter]); // load frequency reg based on length left over
	reg_a = shr(reg_a); // divide by 2
	if (reg_p.c) { smb_decrement_sfx_2_length(); return; } // alter frequency every other frame
	set_y(reg_a);
	set_a(rom[smb_power_up_grab_freq_data-1 + reg_y]); // use length left over / 2 for frequency offset
	set_x(0x5d); // store reg contents of power-up grab sound
	set_y(0x7f);
	smb_load_squ_2_regs(); return;
}

void smb_load_squ_2_regs() {
	smb_play_squ_2_sfx();
	smb_decrement_sfx_2_length(); return;
}

void smb_decrement_sfx_2_length() {
	ram[smb_squ_2_sfx_len_counter] = dec(ram[smb_squ_2_sfx_len_counter]); // decrement length of sfx
	if (!reg_p.z) { smb_es_sfx_2(); return; }
	smb_empty_sfx_2_buffer(); return;
}

void smb_empty_sfx_2_buffer() {
	set_x(0x00); // initialize square 2's sound effects buffer
	ram[smb_square_2_sound_buffer] = reg_x;
	smb_stop_square_2_sfx(); return;
}

void smb_stop_square_2_sfx() {
	set_x(0x0d); // stop playing the sfx
	env_snd_chn_w(reg_x);
	set_x(0x0f);
	env_snd_chn_w(reg_x);
	smb_es_sfx_2(); return;
}

void smb_es_sfx_2() {
	return;
	smb_square_2_sfx_handler(); return;
}

void smb_square_2_sfx_handler() {
	set_a(ram[smb_square_2_sound_buffer]); // special handling for the 1-up sound to keep it
	and_a((smb_sfx_extra_life)); // from being interrupted by other sounds on square 2
	if (!reg_p.z) { smb_continue_extra_life(); return; }
	set_y(ram[smb_square_2_sound_queue]); // check for sfx in queue
	if (reg_p.z) { goto check_sfx_2_buffer; }
	ram[smb_square_2_sound_buffer] = reg_y; // if found, put in buffer and check for the following
	if (reg_p.n) { smb_play_bowser_fall(); return; } // bowser fall
	ram[smb_square_2_sound_queue] = shr(ram[smb_square_2_sound_queue]);
	if (reg_p.c) { smb_play_coin_grab(); return; } // coin grab
	ram[smb_square_2_sound_queue] = shr(ram[smb_square_2_sound_queue]);
	if (reg_p.c) { smb_play_grow_power_up(); return; } // power-up reveal
	ram[smb_square_2_sound_queue] = shr(ram[smb_square_2_sound_queue]);
	if (reg_p.c) { smb_play_grow_vine(); return; } // vine grow
	ram[smb_square_2_sound_queue] = shr(ram[smb_square_2_sound_queue]);
	if (reg_p.c) { smb_play_blast(); return; } // fireworks/gunfire
	ram[smb_square_2_sound_queue] = shr(ram[smb_square_2_sound_queue]);
	if (reg_p.c) { smb_play_timer_tick(); return; } // timer tick
	ram[smb_square_2_sound_queue] = shr(ram[smb_square_2_sound_queue]);
	if (reg_p.c) { smb_play_power_up_grab(); return; } // power-up grab
	ram[smb_square_2_sound_queue] = shr(ram[smb_square_2_sound_queue]);
	if (reg_p.c) { smb_play_extra_life(); return; } // 1-up
check_sfx_2_buffer:;
	set_a(ram[smb_square_2_sound_buffer]); // check for sfx in buffer
	if (reg_p.z) { goto ex_s_2_h; } // if not found, exit sub
	if (reg_p.n) { smb_continue_bowser_fall(); return; } // bowser fall
	reg_a = shr(reg_a);
	if (reg_p.c) { goto cont_c_grab_t_tick; } // coin grab
	reg_a = shr(reg_a);
	if (reg_p.c) { smb_continue_grow_items(); return; } // power-up reveal
	reg_a = shr(reg_a);
	if (reg_p.c) { smb_continue_grow_items(); return; } // vine grow
	reg_a = shr(reg_a);
	if (reg_p.c) { smb_continue_blast(); return; } // fireworks/gunfire
	reg_a = shr(reg_a);
	if (reg_p.c) { goto cont_c_grab_t_tick; } // timer tick
	reg_a = shr(reg_a);
	if (reg_p.c) { smb_continue_power_up_grab(); return; } // power-up grab
	reg_a = shr(reg_a);
	if (reg_p.c) { smb_continue_extra_life(); return; } // 1-up
ex_s_2_h:;
	return;
cont_c_grab_t_tick:;
	smb_continue_c_grab_t_tick(); return;
	smb_jump_to_dec_length_2(); return;
}

void smb_jump_to_dec_length_2() {
	smb_decrement_sfx_2_length(); return;
	smb_play_bowser_fall(); return;
}

void smb_play_bowser_fall() {
	set_a(0x38); // load length of bowser defeat sound
	ram[smb_squ_2_sfx_len_counter] = reg_a;
	set_y(0xc4); // load contents of reg for bowser defeat sound
	set_a(0x18);
	smb_blst_s_jp(); return;
}

void smb_blst_s_jp() {
	if (!reg_p.z) { smb_pbf_regs(); return; }
	smb_continue_bowser_fall(); return;
}

void smb_continue_bowser_fall() {
	set_a(ram[smb_squ_2_sfx_len_counter]); // check for almost near the end
	cmp_a(0x08);
	if (!reg_p.z) { smb_decrement_sfx_2_length(); return; }
	set_y(0xa4); // if so, load the rest of reg contents for bowser defeat sound
	set_a(0x5a);
	smb_pbf_regs(); return;
}

void smb_pbf_regs() {
	set_x(0x9f); // the fireworks/gunfire sound shares part of reg contents here
	smb_el_l_regs(); return;
}

void smb_el_l_regs() {
	if (!reg_p.z) { smb_load_squ_2_regs(); return; } // this is an unconditional branch outta here
	smb_play_extra_life(); return;
}

void smb_play_extra_life() {
	set_a(0x30); // load length of 1-up sound
	ram[smb_squ_2_sfx_len_counter] = reg_a;
	smb_continue_extra_life(); return;
}

void smb_continue_extra_life() {
	set_a(ram[smb_squ_2_sfx_len_counter]);
	set_x(0x03); // load new tones only every eight frames
div_l_loop:;
	reg_a = shr(reg_a);
	if (reg_p.c) { smb_jump_to_dec_length_2(); return; } // if any bits set here, branch to dec the length
	set_x(reg_x-1);
	if (!reg_p.z) { goto div_l_loop; } // do this until all bits checked, if none set, continue
	set_y(reg_a);
	set_a(rom[smb_extra_life_freq_data-1 + reg_y]); // load our reg contents
	set_x(0x82);
	set_y(0x7f);
	if (!reg_p.z) { smb_el_l_regs(); return; } // unconditional branch
	smb_play_grow_power_up(); return;
}

void smb_play_grow_power_up() {
	set_a(0x10); // load length of power-up reveal sound
	if (!reg_p.z) { smb_grow_item_regs(); return; }
	smb_play_grow_vine(); return;
}

void smb_play_grow_vine() {
	set_a(0x20); // load length of vine grow sound
	smb_grow_item_regs(); return;
}

void smb_grow_item_regs() {
	ram[smb_squ_2_sfx_len_counter] = reg_a;
	set_a(0x7f); // load contents of reg for both sounds directly
	env_sq2_sweep_w(reg_a);
	set_a(0x00); // start secondary counter for both sounds
	ram[smb_sfx_secondary_counter] = reg_a;
	smb_continue_grow_items(); return;
}

void smb_continue_grow_items() {
	ram[smb_sfx_secondary_counter] = inc(ram[smb_sfx_secondary_counter]); // increment secondary counter for both sounds
	set_a(ram[smb_sfx_secondary_counter]); // this sound doesn't decrement the usual counter
	reg_a = shr(reg_a); // divide by 2 to get the offset
	set_y(reg_a);
	cmp_y(ram[smb_squ_2_sfx_len_counter]); // have we reached the end yet?
	if (reg_p.z) { goto stop_grow_items; } // if so, branch to jump, and stop playing sounds
	set_a(0x9d); // load contents of other reg directly
	env_sq2_vol_w(reg_a);
	set_a(rom[smb_p_up_v_grow_freq_data + reg_y]); // use secondary counter / 2 as offset for frequency regs
	smb_set_freq_squ_2();
	return;
stop_grow_items:;
	smb_empty_sfx_2_buffer(); return; // branch to stop playing sounds
}

void smb_play_brick_shatter() {
	set_a(0x20); // load length of brick shatter sound
	ram[smb_noise_sfx_len_counter] = reg_a;
	smb_continue_brick_shatter(); return;
}

void smb_continue_brick_shatter() {
	set_a(ram[smb_noise_sfx_len_counter]);
	reg_a = shr(reg_a); // divide by 2 and check for bit set to use offset
	if (!reg_p.c) { smb_decrement_sfx_3_length(); return; }
	set_y(reg_a);
	set_x(rom[smb_brick_shatter_freq_data + reg_y]); // load reg contents of brick shatter sound
	set_a(rom[smb_brick_shatter_env_data + reg_y]);
	smb_play_noise_sfx(); return;
}

void smb_play_noise_sfx() {
	env_noise_vol_w(reg_a); // play the sfx
	env_noise_lo_w(reg_x);
	set_a(0x18);
	env_noise_hi_w(reg_a);
	smb_decrement_sfx_3_length(); return;
}

void smb_decrement_sfx_3_length() {
	ram[smb_noise_sfx_len_counter] = dec(ram[smb_noise_sfx_len_counter]); // decrement length of sfx
	if (!reg_p.z) { goto ex_sfx_3; }
	set_a(0xf0); // if done, stop playing the sfx
	env_noise_vol_w(reg_a);
	set_a(0x00);
	ram[smb_noise_sound_buffer] = reg_a;
ex_sfx_3:;
	return;
	smb_noise_sfx_handler(); return;
}

void smb_noise_sfx_handler() {
	set_y(ram[smb_noise_sound_queue]); // check for sfx in queue
	if (reg_p.z) { goto check_noise_buffer; }
	ram[smb_noise_sound_buffer] = reg_y; // if found, put in buffer
	ram[smb_noise_sound_queue] = shr(ram[smb_noise_sound_queue]);
	if (reg_p.c) { smb_play_brick_shatter(); return; } // brick shatter
	ram[smb_noise_sound_queue] = shr(ram[smb_noise_sound_queue]);
	if (reg_p.c) { goto play_bowser_flame; } // bowser flame
check_noise_buffer:;
	set_a(ram[smb_noise_sound_buffer]); // check for sfx in buffer
	if (reg_p.z) { goto ex_nh; } // if not found, exit sub
	reg_a = shr(reg_a);
	if (reg_p.c) { smb_continue_brick_shatter(); return; } // brick shatter
	reg_a = shr(reg_a);
	if (reg_p.c) { goto continue_bowser_flame; } // bowser flame
ex_nh:;
	return;
play_bowser_flame:;
	set_a(0x40); // load length of bowser flame sound
	ram[smb_noise_sfx_len_counter] = reg_a;
continue_bowser_flame:;
	set_a(ram[smb_noise_sfx_len_counter]);
	reg_a = shr(reg_a);
	set_y(reg_a);
	set_x(0x0f); // load reg contents of bowser flame sound
	set_a(rom[smb_bowser_flame_env_data-1 + reg_y]);
	if (!reg_p.z) { smb_play_noise_sfx(); return; } // unconditional branch here
	smb_continue_music(); return;
}

void smb_continue_music() {
	smb_handle_square_2_music(); return; // if we have music, start with square 2 channel
	smb_music_handler(); return;
}

void smb_music_handler() {
	set_a(ram[smb_event_music_queue]); // check event music queue
	if (!reg_p.z) { smb_load_event_music(); return; }
	set_a(ram[smb_area_music_queue]); // check area music queue
	if (!reg_p.z) { smb_load_area_music(); return; }
	set_a(ram[smb_event_music_buffer]); // check both buffers
	or_a(ram[smb_area_music_buffer]);
	if (!reg_p.z) { smb_continue_music(); return; }
	return; // no music, then leave
	smb_load_event_music(); return;
}

void smb_load_event_music() {
	ram[smb_event_music_buffer] = reg_a; // copy event music queue contents to buffer
	cmp_a((smb_death_music)); // is it death music?
	if (!reg_p.z) { goto no_stop_sfx; } // if not, jump elsewhere
	smb_stop_square_1_sfx(); // stop sfx in square 1 and 2
	smb_stop_square_2_sfx(); // but clear only square 1's sfx buffer
no_stop_sfx:;
	set_x(ram[smb_area_music_buffer]);
	ram[smb_area_music_buffer_alt] = reg_x; // save current area music buffer to be re-obtained later
	set_y(0x00);
	ram[smb_note_length_tbl_adder] = reg_y; // default value for additional length byte offset
	ram[smb_area_music_buffer] = reg_y; // clear area music buffer
	cmp_a((smb_time_running_out_music)); // is it time running out music?
	if (!reg_p.z) { smb_find_event_music_header(); return; }
	set_x(0x08); // load offset to be added to length byte of header
	ram[smb_note_length_tbl_adder] = reg_x;
	if (!reg_p.z) { smb_find_event_music_header(); return; } // unconditional branch
	smb_load_area_music(); return;
}

void smb_load_area_music() {
	cmp_a(0x04); // is it underground music?
	if (!reg_p.z) { goto no_stop_1; } // no, do not stop square 1 sfx
	smb_stop_square_1_sfx();
no_stop_1:;
	set_y(0x10); // start counter used only by ground level music
	smb_gm_loop_b(); return;
}

void smb_gm_loop_b() {
	ram[smb_ground_music_header_ofs] = reg_y;
	smb_handle_area_music_loop_b(); return;
}

void smb_handle_area_music_loop_b() {
	set_y(0x00); // clear event music buffer
	ram[smb_event_music_buffer] = reg_y;
	ram[smb_area_music_buffer] = reg_a; // copy area music queue contents to buffer
	cmp_a(0x01); // is it ground level music?
	if (!reg_p.z) { goto find_area_music_header; }
	ram[smb_ground_music_header_ofs] = inc(ram[smb_ground_music_header_ofs]); // increment but only if playing ground level music
	set_y(ram[smb_ground_music_header_ofs]); // is it time to loopback ground level music?
	cmp_y(0x32);
	if (!reg_p.z) { smb_load_header(); return; } // branch ahead with alternate offset
	set_y(0x11);
	if (!reg_p.z) { smb_gm_loop_b(); return; } // unconditional branch
find_area_music_header:;
	set_y(0x08); // load Y for offset of area music
	ram[smb_music_offset_square_2] = reg_y; // residual instruction here
	smb_find_event_music_header(); return;
}

void smb_find_event_music_header() {
	set_y(reg_y+1); // increment Y pointer based on previously loaded queue contents
	reg_a = shr(reg_a); // bit shift and increment until we find a set bit for music
	if (!reg_p.c) { smb_find_event_music_header(); return; }
	smb_load_header(); return;
}

void smb_load_header() {
	set_a(rom[smb_music_header_data-1 + reg_y]); // load offset for header
	set_y(reg_a);
	set_a(rom[smb_music_header_data + reg_y]); // now load the header
	ram[smb_note_len_lookup_tbl_ofs] = reg_a;
	set_a(rom[smb_music_header_data+1 + reg_y]);
	ram[smb_music_data] = reg_a;
	set_a(rom[smb_music_header_data+2 + reg_y]);
	ram[smb_music_data+1] = reg_a;
	set_a(rom[smb_music_header_data+3 + reg_y]);
	ram[smb_music_offset_triangle] = reg_a;
	set_a(rom[smb_music_header_data+4 + reg_y]);
	ram[smb_music_offset_square_1] = reg_a;
	set_a(rom[smb_music_header_data+5 + reg_y]);
	ram[smb_music_offset_noise] = reg_a;
	ram[smb_noise_data_loopback_ofs] = reg_a;
	set_a(0x01); // initialize music note counters
	ram[smb_squ_2_note_len_counter] = reg_a;
	ram[smb_squ_1_note_len_counter] = reg_a;
	ram[smb_tri_note_len_counter] = reg_a;
	ram[smb_noise_beat_len_counter] = reg_a;
	set_a(0x00); // initialize music data offset for square 2
	ram[smb_music_offset_square_2] = reg_a;
	ram[smb_alt_reg_content_flag] = reg_a; // initialize alternate control reg data used by square 1
	set_a(0x0b); // disable triangle channel and reenable it
	env_snd_chn_w(reg_a);
	set_a(0x0f);
	env_snd_chn_w(reg_a);
	smb_handle_square_2_music(); return;
}

void smb_handle_square_2_music() {
	ram[smb_squ_2_note_len_counter] = dec(ram[smb_squ_2_note_len_counter]); // decrement square 2 note length
	if (!reg_p.z) { goto misc_squ_2_music_tasks; } // is it time for more data?  if not, branch to end tasks
	set_y(ram[smb_music_offset_square_2]); // increment square 2 music offset and fetch data
	ram[smb_music_offset_square_2] = inc(ram[smb_music_offset_square_2]);
	set_a(mem_r(*(uint16_t*)&ram[smb_music_data] + reg_y));
	if (reg_p.z) { goto end_of_music_data; } // if zero, the data is a null terminator
	if (!reg_p.n) { goto squ_2_note_handler; } // if non-negative, data is a note
	if (!reg_p.z) { goto squ_2_length_handler; } // otherwise it is length data
end_of_music_data:;
	set_a(ram[smb_event_music_buffer]); // check secondary buffer for time running out music
	cmp_a((smb_time_running_out_music));
	if (!reg_p.z) { goto not_tro; }
	set_a(ram[smb_area_music_buffer_alt]); // load previously saved contents of primary buffer
	if (!reg_p.z) { goto music_loop_back; } // and start playing the song again if there is one
not_tro:;
	and_a((smb_victory_music)); // check for victory music (the only secondary that loops)
	if (!reg_p.z) { goto victory_m_loop_back; }
	set_a(ram[smb_area_music_buffer]); // check primary buffer for any music except pipe intro
	and_a(0b01011111);
	if (!reg_p.z) { goto music_loop_back; } // if any area music except pipe intro, music loops
	set_a(0x00); // clear primary and secondary buffers and initialize
	ram[smb_area_music_buffer] = reg_a; // control regs of square and triangle channels
	ram[smb_event_music_buffer] = reg_a;
	env_tri_linear_w(reg_a);
	set_a(0x90);
	env_sq1_vol_w(reg_a);
	env_sq2_vol_w(reg_a);
	return;
music_loop_back:;
	smb_handle_area_music_loop_b(); return;
victory_m_loop_back:;
	smb_load_event_music(); return;
squ_2_length_handler:;
	smb_process_length_data(); // store length of note
	ram[smb_squ_2_note_len_buffer] = reg_a;
	set_y(ram[smb_music_offset_square_2]); // fetch another byte (MUST NOT BE LENGTH BYTE!)
	ram[smb_music_offset_square_2] = inc(ram[smb_music_offset_square_2]);
	set_a(mem_r(*(uint16_t*)&ram[smb_music_data] + reg_y));
squ_2_note_handler:;
	set_x(ram[smb_square_2_sound_buffer]); // is there a sound playing on this channel?
	if (!reg_p.z) { goto skip_fq_l_1; }
	smb_set_freq_squ_2(); // no, then play the note
	if (reg_p.z) { goto rest; } // check to see if note is rest
	smb_load_control_regs(); // if not, load control regs for square 2
rest:;
	ram[smb_squ_2_envelope_data_ctrl] = reg_a; // save contents of A
	smb_dump_sq_2_regs(); // dump X and Y into square 2 control regs
skip_fq_l_1:;
	set_a(ram[smb_squ_2_note_len_buffer]); // save length in square 2 note counter
	ram[smb_squ_2_note_len_counter] = reg_a;
misc_squ_2_music_tasks:;
	set_a(ram[smb_square_2_sound_buffer]); // is there a sound playing on square 2?
	if (!reg_p.z) { goto handle_square_1_music; }
	set_a(ram[smb_event_music_buffer]); // check for death music or d4 set on secondary buffer
	and_a(0b10010001); // note that regs for death music or d4 are loaded by default
	if (!reg_p.z) { goto handle_square_1_music; }
	set_y(ram[smb_squ_2_envelope_data_ctrl]); // check for contents saved from smb_load_control_regs
	if (reg_p.z) { goto no_dec_env_1; }
	ram[smb_squ_2_envelope_data_ctrl] = dec(ram[smb_squ_2_envelope_data_ctrl]); // decrement unless already zero
no_dec_env_1:;
	smb_load_envelope_data(); // do a load of envelope data to replace default
	env_sq2_vol_w(reg_a); // based on offset set by first load unless playing
	set_x(0x7f); // death music or d4 set on secondary buffer
	env_sq2_sweep_w(reg_x);
handle_square_1_music:;
	set_y(ram[smb_music_offset_square_1]); // is there a nonzero offset here?
	if (reg_p.z) { goto handle_triangle_music; } // if not, skip ahead to the triangle channel
	ram[smb_squ_1_note_len_counter] = dec(ram[smb_squ_1_note_len_counter]); // decrement square 1 note length
	if (!reg_p.z) { goto misc_squ_1_music_tasks; } // is it time for more data?
fetch_squ_1_music_data:;
	set_y(ram[smb_music_offset_square_1]); // increment square 1 music offset and fetch data
	ram[smb_music_offset_square_1] = inc(ram[smb_music_offset_square_1]);
	set_a(mem_r(*(uint16_t*)&ram[smb_music_data] + reg_y));
	if (!reg_p.z) { goto squ_1_note_handler; } // if nonzero, then skip this part
	set_a(0x83);
	env_sq1_vol_w(reg_a); // store some data into control regs for square 1
	set_a(0x94); // and fetch another byte of data, used to give
	env_sq1_sweep_w(reg_a); // death music its unique sound
	ram[smb_alt_reg_content_flag] = reg_a;
	if (!reg_p.z) { goto fetch_squ_1_music_data; } // unconditional branch
squ_1_note_handler:;
	smb_alternate_length_nalder();
	ram[smb_squ_1_note_len_counter] = reg_a; // save contents of A in square 1 note counter
	set_y(ram[smb_square_1_sound_buffer]); // is there a sound playing on square 1?
	if (!reg_p.z) { goto handle_triangle_music; }
	set_a(reg_x);
	and_a(0b00111110); // change saved data to appropriate note format
	smb_set_freq_squ_1(); // play the note
	if (reg_p.z) { goto skip_ctrl_l; }
	smb_load_control_regs();
skip_ctrl_l:;
	ram[smb_squ_1_envelope_data_ctrl] = reg_a; // save envelope offset
	smb_dump_squ_1_regs();
misc_squ_1_music_tasks:;
	set_a(ram[smb_square_1_sound_buffer]); // is there a sound playing on square 1?
	if (!reg_p.z) { goto handle_triangle_music; }
	set_a(ram[smb_event_music_buffer]); // check for death music or d4 set on secondary buffer
	and_a(0b10010001);
	if (!reg_p.z) { goto death_m_alt_reg; }
	set_y(ram[smb_squ_1_envelope_data_ctrl]); // check saved envelope offset
	if (reg_p.z) { goto no_dec_env_2; }
	ram[smb_squ_1_envelope_data_ctrl] = dec(ram[smb_squ_1_envelope_data_ctrl]); // decrement unless already zero
no_dec_env_2:;
	smb_load_envelope_data(); // do a load of envelope data
	env_sq1_vol_w(reg_a); // based on offset set by first load
death_m_alt_reg:;
	set_a(ram[smb_alt_reg_content_flag]); // check for alternate control reg data
	if (!reg_p.z) { goto do_alt_load; }
	set_a(0x7f); // load this value if zero, the alternate value
do_alt_load:;
	env_sq1_sweep_w(reg_a); // if nonzero, and let's move on
handle_triangle_music:;
	set_a(ram[smb_music_offset_triangle]);
	ram[smb_tri_note_len_counter] = dec(ram[smb_tri_note_len_counter]); // decrement triangle note length
	if (!reg_p.z) { goto handle_noise_music; } // is it time for more data?
	set_y(ram[smb_music_offset_triangle]); // increment square 1 music offset and fetch data
	ram[smb_music_offset_triangle] = inc(ram[smb_music_offset_triangle]);
	set_a(mem_r(*(uint16_t*)&ram[smb_music_data] + reg_y));
	if (reg_p.z) { goto load_tri_ctrl_reg; } // if zero, skip all this and move on to noise
	if (!reg_p.n) { goto tri_note_handler; } // if non-negative, data is note
	smb_process_length_data(); // otherwise, it is length data
	ram[smb_tri_note_len_buffer] = reg_a; // save contents of A
	set_a(0x1f);
	env_tri_linear_w(reg_a); // load some default data for triangle control reg
	set_y(ram[smb_music_offset_triangle]); // fetch another byte
	ram[smb_music_offset_triangle] = inc(ram[smb_music_offset_triangle]);
	set_a(mem_r(*(uint16_t*)&ram[smb_music_data] + reg_y));
	if (reg_p.z) { goto load_tri_ctrl_reg; } // check once more for nonzero data
tri_note_handler:;
	smb_set_freq_tri();
	set_x(ram[smb_tri_note_len_buffer]); // save length in triangle note counter
	ram[smb_tri_note_len_counter] = reg_x;
	set_a(ram[smb_event_music_buffer]);
	and_a(0b01101110); // check for death music or d4 set on secondary buffer
	if (!reg_p.z) { goto not_d_or_d_4; } // if playing any other secondary, skip primary buffer check
	set_a(ram[smb_area_music_buffer]); // check primary buffer for water or castle level music
	and_a(0b00001010);
	if (reg_p.z) { goto handle_noise_music; } // if playing any other primary, or death or d4, go on to noise routine
not_d_or_d_4:;
	set_a(reg_x); // if playing water or castle music or any secondary
	cmp_a(0x12); // besides death music or d4 set, check length of note
	if (reg_p.c) { goto long_n; }
	set_a(ram[smb_event_music_buffer]); // check for win castle music again if not playing a long note
	and_a((smb_end_of_castle_music));
	if (reg_p.z) { goto medi_n; }
	set_a(0x0f); // load value $0f if playing the win castle music and playing a short
	if (!reg_p.z) { goto load_tri_ctrl_reg; } // note, load value $1f if playing water or castle level music or any
medi_n:;
	set_a(0x1f); // secondary besides death and d4 except win castle or win castle and playing
	if (!reg_p.z) { goto load_tri_ctrl_reg; } // a short note, and load value $ff if playing a long note on water, castle
long_n:;
	set_a(0xff); // or any secondary (including win castle) except death and d4
load_tri_ctrl_reg:;
	env_tri_linear_w(reg_a); // save final contents of A into control reg for triangle
handle_noise_music:;
	set_a(ram[smb_area_music_buffer]); // check if playing underground or castle music
	and_a(0b11110011);
	if (reg_p.z) { goto exit_music_handler; } // if so, skip the noise routine
	ram[smb_noise_beat_len_counter] = dec(ram[smb_noise_beat_len_counter]); // decrement noise beat length
	if (!reg_p.z) { goto exit_music_handler; } // is it time for more data?
fetch_noise_beat_data:;
	set_y(ram[smb_music_offset_noise]); // increment noise beat offset and fetch data
	ram[smb_music_offset_noise] = inc(ram[smb_music_offset_noise]);
	set_a(mem_r(*(uint16_t*)&ram[smb_music_data] + reg_y)); // get noise beat data, if nonzero, branch to handle
	if (!reg_p.z) { goto noise_beat_handler; }
	set_a(ram[smb_noise_data_loopback_ofs]); // if data is zero, reload original noise beat offset
	ram[smb_music_offset_noise] = reg_a; // and loopback next time around
	if (!reg_p.z) { goto fetch_noise_beat_data; } // unconditional branch
noise_beat_handler:;
	smb_alternate_length_nalder();
	ram[smb_noise_beat_len_counter] = reg_a; // store length in noise beat counter
	set_a(reg_x);
	and_a(0b00111110); // reload data and erase length bits
	if (reg_p.z) { goto silent_beat; } // if no beat data, silence
	cmp_a(0x30); // check the beat data and play the appropriate
	if (reg_p.z) { goto long_beat; } // noise accordingly
	cmp_a(0x20);
	if (reg_p.z) { goto strong_beat; }
	and_a(0x10);
	if (reg_p.z) { goto silent_beat; }
	set_a(0x1c); // short beat data
	set_x(0x03);
	set_y(0x18);
	if (!reg_p.z) { goto play_beat; }
strong_beat:;
	set_a(0x1c); // strong beat data
	set_x(0x0c);
	set_y(0x18);
	if (!reg_p.z) { goto play_beat; }
long_beat:;
	set_a(0x1c); // long beat data
	set_x(0x03);
	set_y(0x58);
	if (!reg_p.z) { goto play_beat; }
silent_beat:;
	set_a(0x10); // silence
play_beat:;
	env_noise_vol_w(reg_a); // load beat data into noise regs
	env_noise_lo_w(reg_x);
	env_noise_hi_w(reg_y);
exit_music_handler:;
	return;
	smb_alternate_length_nalder(); return;
}

void smb_alternate_length_nalder() {
	set_x(reg_a); // save a copy of original byte into X
	reg_a = ror(reg_a); // save LSB from original byte into carry
	set_a(reg_x); // reload original byte and rotate three times
	reg_a = rol(reg_a); // turning xx00000x into 00000xxx, with the
	reg_a = rol(reg_a); // bit in carry as the MSB here
	reg_a = rol(reg_a);
	smb_process_length_data(); return;
}

void smb_process_length_data() {
	and_a(0b00000111); // clear all but the three LSBs
	reg_p.c = 0;
	add_a(ram[smb_note_len_lookup_tbl_ofs]); // add offset loaded from first header byte
	add_a(ram[smb_note_length_tbl_adder]); // add extra if time running out music
	set_y(reg_a);
	set_a(rom[smb_music_length_lookup_tbl + reg_y]); // load length
	return;
	smb_load_control_regs(); return;
}

void smb_load_control_regs() {
	set_a(ram[smb_event_music_buffer]); // check secondary buffer for win castle music
	and_a((smb_end_of_castle_music));
	if (reg_p.z) { goto not_e_cstl_m; }
	set_a(0x04); // this value is only used for win castle music
	if (!reg_p.z) { goto all_mus; } // unconditional branch
not_e_cstl_m:;
	set_a(ram[smb_area_music_buffer]);
	and_a(0b01111101); // check primary buffer for water music
	if (reg_p.z) { goto water_mus; }
	set_a(0x08); // this is the default value for all other music
	if (!reg_p.z) { goto all_mus; }
water_mus:;
	set_a(0x28); // this value is used for water music and all other event music
all_mus:;
	set_x(0x82); // load contents of other sound regs for square 2
	set_y(0x7f);
	return;
	smb_load_envelope_data(); return;
}

void smb_load_envelope_data() {
	set_a(ram[smb_event_music_buffer]); // check secondary buffer for win castle music
	and_a((smb_end_of_castle_music));
	if (reg_p.z) { goto load_usual_env_data; }
	set_a(rom[smb_end_of_castle_music_env_data + reg_y]); // load data from offset for win castle music
	return;
load_usual_env_data:;
	set_a(ram[smb_area_music_buffer]); // check primary buffer for water music
	and_a(0b01111101);
	if (reg_p.z) { goto load_water_event_mus_env_data; }
	set_a(rom[smb_area_music_env_data + reg_y]); // load default data from offset for all other music
	return;
load_water_event_mus_env_data:;
	set_a(rom[smb_water_event_mus_env_data + reg_y]); // load data from offset for water music and all other event music
	return;
}
