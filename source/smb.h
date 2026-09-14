#include "compat.h"
#include "env.h"

#define smb_green_koopa 0x00
#define smb_death_music 0b00000001
#define smb_game_mode_value 1
#define smb_ground_music 0b00000001
#define smb_sfx_big_jump 0b00000001
#define smb_sfx_brick_shatter 0b00000001
#define smb_sfx_coin_grab 0b00000001
#define smb_world_2 1
#define smb_buzzy_beetle 0x02
#define smb_game_over_music 0b00000010
#define smb_level_3 2
#define smb_sfx_bowser_flame 0b00000010
#define smb_sfx_bump 0b00000010
#define smb_sfx_grow_power_up 0b00000010
#define smb_victory_mode_value 2
#define smb_water_music 0b00000010
#define smb_game_over_mode_value 3
#define smb_red_koopa 0x03
#define smb_sfx_enemy_stomp 0b00000100
#define smb_sfx_grow_vine 0b00000100
#define smb_underground_music 0b00000100
#define smb_victory_music 0b00000100
#define smb_world_5 4
#define smb_hammer_bro 0x05
#define smb_world_6 5
#define smb_goomba 0x06
#define smb_world_7 6
#define smb_bloober 0x07
#define smb_world_8 7
#define smb_bullet_bill_frenzy_var 0x08
#define smb_castle_music 0b00001000
#define smb_end_of_castle_music 0b00001000
#define smb_sfx_blast 0b00001000
#define smb_sfx_enemy_smack 0b00001000
#define smb_tall_enemy 0x09
#define smb_grey_cheep_cheep 0x0a
#define smb_red_cheep_cheep 0x0b
#define smb_podoboo 0x0c
#define smb_piranha_plant 0x0d
#define smb_green_paratroopa_jump 0x0e
#define smb_cloud_music 0b00010000
#define smb_sfx_pipe_down_injury 0b00010000
#define smb_sfx_timer_tick 0b00010000
#define smb_start_button 0b00010000
#define smb_lakitu 0x11
#define smb_spiny 0x12
#define smb_fly_cheep_cheep_frenzy 0x14
#define smb_flying_cheep_cheep 0x14
#define smb_fireworks 0x16
#define smb_b_bill_c_cheep_frenzy 0x17
#define smb_stop_frenzy 0x18
#define smb_end_of_level_music 0b00100000
#define smb_pipe_intro_music 0b00100000
#define smb_select_button 0b00100000
#define smb_sfx_fireball 0b00100000
#define smb_sfx_power_up_grab 0b00100000
#define smb_bowser 0x2d
#define smb_power_up_object 0x2e
#define smb_vine_object 0x2f
#define smb_flagpole_flag_object 0x30
#define smb_star_flag_object 0x31
#define smb_jumpspring_object 0x32
#define smb_bullet_bill_cannon_var 0x33
#define smb_b_button 0b01000000
#define smb_sfx_extra_life 0b01000000
#define smb_star_power_music 0b01000000
#define smb_time_running_out_music 0b01000000
#define smb_a_button 0b10000000
#define smb_sfx_bowser_fall 0b10000000
#define smb_sfx_small_jump 0b10000000
#define smb_silence 0b10000000
#define smb_title_screen_data_offset 0x1ec0

#define smb_object_offset 0x08
#define smb_frame_counter 0x09
#define smb_a_b_buttons 0x0a
#define smb_up_down_buttons 0x0b
#define smb_left_right_buttons 0x0c
#define smb_previous_a_b_buttons 0x0d
#define smb_game_engine_subroutine 0x0e
#define smb_enemy_flag 0x0f
#define smb_enemy_id 0x16
#define smb_player_state 0x1d
#define smb_enemy_state 0x1e
#define smb_fireball_state 0x24
#define smb_block_state 0x26
#define smb_misc_state 0x2a
#define smb_player_facing_dir 0x33
#define smb_destination_page_loc 0x34
#define smb_firebar_spin_direction 0x34
#define smb_victory_walk_control 0x35
#define smb_power_up_type 0x39
#define smb_fireball_bouncing_flag 0x3a
#define smb_hammer_bro_jump_timer 0x3c
#define smb_player_moving_dir 0x45
#define smb_enemy_moving_dir 0x46
#define smb_player_x_speed 0x57
#define smb_spr_object_x_speed 0x57
#define smb_blooper_move_speed 0x58
#define smb_cheep_cheep_move_m_flag 0x58
#define smb_enemy_x_speed 0x58
#define smb_explosion_gfx_counter 0x58
#define smb_firebar_spin_state_low 0x58
#define smb_jumpspring_fixed_y_pos 0x58
#define smb_lakitu_move_speed 0x58
#define smb_piranha_plant_y_speed 0x58
#define smb_red_p_troopa_center_y_pos 0x58
#define smb_x_move_secondary_counter 0x58
#define smb_y_platform_center_y_pos 0x58
#define smb_fireball_x_speed 0x5e
#define smb_block_x_speed 0x60
#define smb_misc_x_speed 0x64
#define smb_player_page_loc 0x6d
#define smb_spr_object_page_loc 0x6d
#define smb_enemy_page_loc 0x6e
#define smb_fireball_page_loc 0x74
#define smb_block_page_loc 0x76
#define smb_misc_page_loc 0x7a
#define smb_bubble_page_loc 0x83
#define smb_player_x_position 0x86
#define smb_spr_object_x_position 0x86
#define smb_enemy_x_position 0x87
#define smb_fireball_x_position 0x8d
#define smb_block_x_position 0x8f
#define smb_misc_x_position 0x93
#define smb_bubble_x_position 0x9c
#define smb_player_y_speed 0x9f
#define smb_spr_object_y_speed 0x9f
#define smb_blooper_move_counter 0xa0
#define smb_enemy_y_speed 0xa0
#define smb_explosion_timer_counter 0xa0
#define smb_firebar_spin_state_high 0xa0
#define smb_lakitu_move_direction 0xa0
#define smb_piranha_plant_move_flag 0xa0
#define smb_x_move_primary_counter 0xa0
#define smb_fireball_y_speed 0xa6
#define smb_block_y_speed 0xa8
#define smb_misc_y_speed 0xac
#define smb_player_y_high_pos 0xb5
#define smb_spr_object_y_high_pos 0xb5
#define smb_enemy_y_high_pos 0xb6
#define smb_fireball_y_high_pos 0xbc
#define smb_block_y_high_pos 0xbe
#define smb_misc_y_high_pos 0xc2
#define smb_bubble_y_high_pos 0xcb
#define smb_player_y_position 0xce
#define smb_spr_object_y_position 0xce
#define smb_enemy_y_position 0xcf
#define smb_fireball_y_position 0xd5
#define smb_block_y_position 0xd7
#define smb_misc_y_position 0xdb
#define smb_bubble_y_position 0xe4
#define smb_area_data 0xe7
#define smb_enemy_data 0xe9
#define smb_note_len_lookup_tbl_ofs 0xf0
#define smb_square_1_sound_buffer 0xf1
#define smb_square_2_sound_buffer 0xf2
#define smb_noise_sound_buffer 0xf3
#define smb_area_music_buffer 0xf4
#define smb_music_data 0xf5
#define smb_music_offset_square_2 0xf7
#define smb_music_offset_square_1 0xf8
#define smb_music_offset_triangle 0xf9
#define smb_pause_sound_queue 0xfa
#define smb_area_music_queue 0xfb
#define smb_event_music_queue 0xfc
#define smb_noise_sound_queue 0xfd
#define smb_square_2_sound_queue 0xfe
#define smb_square_1_sound_queue 0xff
#define smb_vertical_flip_flag 0x0109
#define smb_flagpole_f_num_y_pos 0x010d
#define smb_flagpole_f_num_ymf_dummy 0x010e
#define smb_flagpole_score 0x010f
#define smb_floatey_num_control 0x0110
#define smb_floatey_num_x_pos 0x0117
#define smb_floatey_num_y_pos 0x011e
#define smb_shell_chain_counter 0x0125
#define smb_floatey_num_timer 0x012c
#define smb_digit_modifier 0x0134
#define smb_sprite_data 0x0200
#define smb_sprite_y_position 0x0200
#define smb_sprite_tilenumber 0x0201
#define smb_sprite_attributes 0x0202
#define smb_sprite_x_position 0x0203
#define smb_vram_buffer_1_offset 0x0300
#define smb_vram_buffer_1 0x0301
#define smb_vram_buffer_2_offset 0x0340
#define smb_vram_buffer_2 0x0341
#define smb_bowser_body_controls 0x0363
#define smb_bowser_feet_counter 0x0364
#define smb_bowser_movement_speed 0x0365
#define smb_bowser_orig_x_pos 0x0366
#define smb_bowser_flame_timer_ctrl 0x0367
#define smb_bowser_front_offset 0x0368
#define smb_bridge_collapse_offset 0x0369
#define smb_bowser_gfx_flag 0x036a
#define smb_firebar_spin_speed 0x0388
#define smb_vine_flag_offset 0x0398
#define smb_vine_height 0x0399
#define smb_vine_obj_offset 0x039a
#define smb_vine_start_y_position 0x039d
#define smb_bal_platform_alignment 0x03a0
#define smb_platform_x_scroll 0x03a1
#define smb_hammer_throwing_timer 0x03a2
#define smb_platform_collision_flag 0x03a2
#define smb_player_rel_x_pos 0x03ad
#define smb_spr_object_rel_x_pos 0x03ad
#define smb_enemy_rel_x_pos 0x03ae
#define smb_fireball_rel_x_pos 0x03af
#define smb_bubble_rel_x_pos 0x03b0
#define smb_block_rel_x_pos 0x03b1
#define smb_misc_rel_x_pos 0x03b3
#define smb_player_rel_y_pos 0x03b8
#define smb_spr_object_rel_y_pos 0x03b8
#define smb_enemy_rel_y_pos 0x03b9
#define smb_fireball_rel_y_pos 0x03ba
#define smb_bubble_rel_y_pos 0x03bb
#define smb_block_rel_y_pos 0x03bc
#define smb_misc_rel_y_pos 0x03be
#define smb_player_spr_attrib 0x03c4
#define smb_enemy_spr_attrib 0x03c5
#define smb_player_offscreen_bits 0x03d0
#define smb_spr_object_offscr_bits 0x03d0
#define smb_enemy_offscreen_bits 0x03d1
#define smb_f_ball_offscreen_bits 0x03d2
#define smb_bubble_offscreen_bits 0x03d3
#define smb_block_offscreen_bits 0x03d4
#define smb_misc_offscreen_bits 0x03d6
#define smb_enemy_offscr_bits_masked 0x03d8
#define smb_block_orig_y_pos 0x03e4
#define smb_block_b_buf_low 0x03e6
#define smb_block_metatile 0x03e8
#define smb_block_page_loc_2 0x03ea
#define smb_block_rep_flag 0x03ec
#define smb_spr_data_offset_ctrl 0x03ee
#define smb_block_residual_counter 0x03f0
#define smb_block_orig_x_pos 0x03f1
#define smb_attribute_buffer 0x03f9
#define smb_spr_object_x_move_force 0x0400
#define smb_enemy_x_move_force 0x0401
#define smb_red_p_troopa_orig_x_pos 0x0401
#define smb_y_platform_top_y_pos 0x0401
#define smb_player_ymf_dummy 0x0416
#define smb_spr_object_ymf_dummy 0x0416
#define smb_bowser_flame_p_random_ofs 0x0417
#define smb_enemy_ymf_dummy 0x0417
#define smb_piranha_plant_up_y_pos 0x0417
#define smb_bubble_ymf_dummy 0x042c
#define smb_player_y_move_force 0x0433
#define smb_spr_object_y_move_force 0x0433
#define smb_cheep_cheep_orig_y_pos 0x0434
#define smb_enemy_y_move_force 0x0434
#define smb_piranha_plant_down_y_pos 0x0434
#define smb_block_y_move_force 0x043c
#define smb_maximum_left_speed 0x0450
#define smb_maximum_right_speed 0x0456
#define smb_cannon_offset 0x046a
#define smb_whirlpool_offset 0x046a
#define smb_cannon_page_loc 0x046b
#define smb_whirlpool_page_loc 0x046b
#define smb_cannon_x_position 0x0471
#define smb_whirlpool_left_extent 0x0471
#define smb_cannon_y_position 0x0477
#define smb_whirlpool_length 0x0477
#define smb_cannon_timer 0x047d
#define smb_whirlpool_flag 0x047d
#define smb_bowser_hit_points 0x0483
#define smb_stomp_chain_counter 0x0484
#define smb_player_collision_bits 0x0490
#define smb_enemy_collision_bits 0x0491
#define smb_player_bound_box_ctrl 0x0499
#define smb_spr_obj_bound_box_ctrl 0x0499
#define smb_enemy_bound_box_ctrl 0x049a
#define smb_fireball_bound_box_ctrl 0x04a0
#define smb_misc_bound_box_ctrl 0x04a2
#define smb_bounding_box_ul_x_pos 0x04ac
#define smb_bounding_box_ul_y_pos 0x04ad
#define smb_bounding_box_dr_x_pos 0x04ae
#define smb_bounding_box_dr_y_pos 0x04af
#define smb_enemy_bounding_box_coord 0x04b0
#define smb_block_buffer_1 0x0500
#define smb_block_buffer_2 0x05d0
#define smb_block_buffer_column_pos 0x06a0
#define smb_metatile_buffer 0x06a1
#define smb_hammer_enemy_offset 0x06ae
#define smb_jump_coin_misc_offset 0x06b7
#define smb_brick_coin_timer_flag 0x06bc
#define smb_misc_collision_flag 0x06be
#define smb_unused_06_c_9 0x06c9
#define smb_enemy_frenzy_buffer 0x06cb
#define smb_secondary_hard_mode 0x06cc
#define smb_enemy_frenzy_queue 0x06cd
#define smb_fireball_counter 0x06ce
#define smb_duplicate_obj_offset 0x06cf
#define smb_lakitu_reappear_timer 0x06d1
#define smb_numberof_group_enemies 0x06d3
#define smb_color_rotate_offset 0x06d4
#define smb_player_gfx_offset 0x06d5
#define smb_warp_zone_control 0x06d6
#define smb_fireworks_counter 0x06d7
#define smb_multi_loop_correct_cntr 0x06d9
#define smb_multi_loop_pass_cntr 0x06da
#define smb_jumpspring_force 0x06db
#define smb_max_range_from_origin 0x06dc
#define smb_bit_m_filter 0x06dd
#define smb_change_area_timer 0x06de
#define smb_spr_shuffle_amt_offset 0x06e0
#define smb_spr_shuffle_amt 0x06e1
#define smb_spr_data_offset 0x06e4
#define smb_enemy_spr_data_offset 0x06e5
#define smb_alt_spr_data_offset 0x06ec
#define smb_block_spr_data_offset 0x06ec
#define smb_bubble_spr_data_offset 0x06ee
#define smb_f_ball_spr_data_offset 0x06f1
#define smb_misc_spr_data_offset 0x06f3
#define smb_saved_joypad_1_bits 0x06fc
#define smb_saved_joypad_2_bits 0x06fd
#define smb_player_x_scroll 0x06ff
#define smb_player_x_speed_absolute 0x0700
#define smb_friction_adder_high 0x0701
#define smb_friction_adder_low 0x0702
#define smb_running_speed 0x0703
#define smb_swimming_flag 0x0704
#define smb_player_x_move_force 0x0705
#define smb_diff_to_halt_jump 0x0706
#define smb_jump_origin_y_high_pos 0x0707
#define smb_jump_origin_y_position 0x0708
#define smb_vertical_force 0x0709
#define smb_vertical_force_down 0x070a
#define smb_player_change_size_flag 0x070b
#define smb_player_anim_timer_set 0x070c
#define smb_player_anim_ctrl 0x070d
#define smb_jumpspring_anim_ctrl 0x070e
#define smb_flagpole_collision_y_pos 0x070f
#define smb_player_entrance_ctrl 0x0710
#define smb_fireball_throwing_timer 0x0711
#define smb_death_music_loaded 0x0712
#define smb_flagpole_sound_queue 0x0713
#define smb_crouching_flag 0x0714
#define smb_game_timer_setting 0x0715
#define smb_disable_collision_det 0x0716
#define smb_demo_action 0x0717
#define smb_demo_action_timer 0x0718
#define smb_primary_msg_counter 0x0719
#define smb_screen_left_page_loc 0x071a
#define smb_screen_right_page_loc 0x071b
#define smb_screen_left_x_pos 0x071c
#define smb_screen_right_x_pos 0x071d
#define smb_column_sets 0x071e
#define smb_area_parser_task_num 0x071f
#define smb_current_nt_addr_high 0x0720
#define smb_current_nt_addr_low 0x0721
#define smb_sprite_0_hit_detect_flag 0x0722
#define smb_scroll_lock 0x0723
#define smb_current_page_loc 0x0725
#define smb_current_column_pos 0x0726
#define smb_terrain_control 0x0727
#define smb_backloading_flag 0x0728
#define smb_behind_area_parser_flag 0x0729
#define smb_area_object_page_loc 0x072a
#define smb_area_object_page_sel 0x072b
#define smb_area_data_offset 0x072c
#define smb_area_obj_offset_buffer 0x072d
#define smb_area_object_length 0x0730
#define smb_area_style 0x0733
#define smb_staircase_control 0x0734
#define smb_area_object_height 0x0735
#define smb_mushroom_ledge_half_len 0x0736
#define smb_enemy_data_offset 0x0739
#define smb_enemy_object_page_loc 0x073a
#define smb_enemy_object_page_sel 0x073b
#define smb_screen_routine_task 0x073c
#define smb_scroll_thirty_two 0x073d
#define smb_horizontal_scroll 0x073f
#define smb_vertical_scroll 0x0740
#define smb_foreground_scenery 0x0741
#define smb_background_scenery 0x0742
#define smb_cloud_type_override 0x0743
#define smb_background_color_ctrl 0x0744
#define smb_loop_command 0x0745
#define smb_star_flag_task_control 0x0746
#define smb_timer_control 0x0747
#define smb_coin_tally_for_1_ups 0x0748
#define smb_secondary_msg_counter 0x0749
#define smb_joypad_bit_mask 0x074a
#define smb_area_type 0x074e
#define smb_area_addrs_l_offset 0x074f
#define smb_area_pointer 0x0750
#define smb_entrance_page 0x0751
#define smb_alt_entrance_control 0x0752
#define smb_current_player 0x0753
#define smb_player_size 0x0754
#define smb_player_pos_for_scroll 0x0755
#define smb_player_status 0x0756
#define smb_fetch_new_game_timer_flag 0x0757
#define smb_joypad_override 0x0758
#define smb_game_timer_expired_flag 0x0759
#define smb_onscreen_player_info 0x075a
#define smb_number_of_lives 0x075a
#define smb_halfway_page 0x075b
#define smb_level_number 0x075c
#define smb_hidden_1_up_flag 0x075d
#define smb_coin_tally 0x075e
#define smb_world_number 0x075f
#define smb_area_number 0x0760
#define smb_offscreen_player_info 0x0761
#define smb_off_scr_numberof_lives 0x0761
#define smb_off_scr_hidden_1_up_flag 0x0764
#define smb_off_scr_world_number 0x0766
#define smb_off_scr_area_number 0x0767
#define smb_scroll_fractional 0x0768
#define smb_disable_intermediate 0x0769
#define smb_primary_hard_mode 0x076a
#define smb_world_select_number 0x076b
#define smb_oper_mode 0x0770
#define smb_oper_mode_task 0x0772
#define smb_vram_buffer_addr_ctrl 0x0773
#define smb_disable_screen_flag 0x0774
#define smb_scroll_amount 0x0775
#define smb_game_pause_status 0x0776
#define smb_game_pause_timer 0x0777
#define smb_mirror_ppu_ctrl_reg_1 0x0778
#define smb_mirror_ppu_ctrl_reg_2 0x0779
#define smb_number_of_players 0x077a
#define smb_interval_timer_control 0x077f
#define smb_timers 0x0780
#define smb_select_timer 0x0780
#define smb_player_anim_timer 0x0781
#define smb_jump_swim_timer 0x0782
#define smb_running_timer 0x0783
#define smb_block_bounce_timer 0x0784
#define smb_side_collision_timer 0x0785
#define smb_jumpspring_timer 0x0786
#define smb_game_timer_ctrl_timer 0x0787
#define smb_climb_slide_timer 0x0789
#define smb_enemy_frame_timer 0x078a
#define smb_frenzy_enemy_timer 0x078f
#define smb_bowser_fire_breath_timer 0x0790
#define smb_stomp_timer 0x0791
#define smb_air_bubble_timer 0x0792
#define smb_scroll_interval_timer 0x0795
#define smb_enemy_interval_timer 0x0796
#define smb_brick_coin_timer 0x079d
#define smb_injury_timer 0x079e
#define smb_star_invincible_timer 0x079f
#define smb_screen_timer 0x07a0
#define smb_world_end_timer 0x07a1
#define smb_demo_timer 0x07a2
#define smb_pseudo_random_bit_reg 0x07a7
#define smb_sound_memory 0x07b0
#define smb_music_offset_noise 0x07b0
#define smb_event_music_buffer 0x07b1
#define smb_pause_sound_buffer 0x07b2
#define smb_squ_2_note_len_buffer 0x07b3
#define smb_squ_2_note_len_counter 0x07b4
#define smb_squ_2_envelope_data_ctrl 0x07b5
#define smb_squ_1_note_len_counter 0x07b6
#define smb_squ_1_envelope_data_ctrl 0x07b7
#define smb_tri_note_len_buffer 0x07b8
#define smb_tri_note_len_counter 0x07b9
#define smb_noise_beat_len_counter 0x07ba
#define smb_squ_1_sfx_len_counter 0x07bb
#define smb_squ_2_sfx_len_counter 0x07bd
#define smb_sfx_secondary_counter 0x07be
#define smb_noise_sfx_len_counter 0x07bf
#define smb_dac_counter 0x07c0
#define smb_noise_data_loopback_ofs 0x07c1
#define smb_note_length_tbl_adder 0x07c4
#define smb_area_music_buffer_alt 0x07c5
#define smb_pause_mode_flag 0x07c6
#define smb_ground_music_header_ofs 0x07c7
#define smb_alt_reg_content_flag 0x07ca
#define smb_warm_boot_offset 0x07d6
#define smb_top_score_display 0x07d7
#define smb_score_and_coin_display 0x07dd
#define smb_game_timer_display 0x07f8
#define smb_world_select_enable_flag 0x07fc
#define smb_continue_world 0x07fd
#define smb_cold_boot_offset 0x07fe
#define smb_warm_boot_validation 0x07ff

#define smb_vram_addr_table_low (32858 - 0x8000)
#define smb_vram_addr_table_high (32877 - 0x8000)
#define smb_vram_buffer_offset (32896 - 0x8000)
#define smb_w_select_buffer_template (33343 - 0x8000)
#define smb_mushroom_icon_data (33565 - 0x8000)
#define smb_demo_action_data (33600 - 0x8000)
#define smb_demo_timing_data (33621 - 0x8000)
#define smb_floatey_num_tile_data (33951 - 0x8000)
#define smb_score_update_data (33975 - 0x8000)
#define smb_area_palette (34235 - 0x8000)
#define smb_bg_color_ctrl_addr (34251 - 0x8000)
#define smb_background_colors (34255 - 0x8000)
#define smb_player_colors (34263 - 0x8000)
#define smb_game_text (34642 - 0x8000)
#define smb_world_lives_display (34681 - 0x8000)
#define smb_two_player_time_up (34712 - 0x8000)
#define smb_two_player_game_over (34731 - 0x8000)
#define smb_one_player_game_over (34739 - 0x8000)
#define smb_warp_zone_welcome (34752 - 0x8000)
#define smb_luigi_name (34797 - 0x8000)
#define smb_warp_zone_numbers (34802 - 0x8000)
#define smb_game_text_offsets (34814 - 0x8000)
#define smb_color_rotate_palette (35267 - 0x8000)
#define smb_blank_palette (35273 - 0x8000)
#define smb_palette_3_data (35281 - 0x8000)
#define smb_block_gfx_data (35385 - 0x8000)
#define smb_metatile_graphics_low (35592 - 0x8000)
#define smb_metatile_graphics_high (35596 - 0x8000)
#define smb_palette_0_m_tiles (35600 - 0x8000)
#define smb_palette_1_m_tiles (35756 - 0x8000)
#define smb_palette_2_m_tiles (35940 - 0x8000)
#define smb_palette_3_m_tiles (35980 - 0x8000)
#define smb_water_palette_data (36004 - 0x8000)
#define smb_ground_palette_data (36040 - 0x8000)
#define smb_underground_palette_data (36076 - 0x8000)
#define smb_castle_palette_data (36112 - 0x8000)
#define smb_day_snow_palette_data (36148 - 0x8000)
#define smb_night_snow_palette_data (36156 - 0x8000)
#define smb_mushroom_palette_data (36164 - 0x8000)
#define smb_bowser_palette_data (36172 - 0x8000)
#define smb_mario_thanks_message (36180 - 0x8000)
#define smb_luigi_thanks_message (36200 - 0x8000)
#define smb_mushroom_retainer_saved (36220 - 0x8000)
#define smb_princess_saved_1 (36264 - 0x8000)
#define smb_princess_saved_2 (36287 - 0x8000)
#define smb_world_select_message_1 (36318 - 0x8000)
#define smb_world_select_message_2 (36335 - 0x8000)
#define smb_status_bar_data (36596 - 0x8000)
#define smb_status_bar_offset (36608 - 0x8000)
#define smb_default_spr_offsets (36796 - 0x8000)
#define smb_sprite_0_data (36811 - 0x8000)
#define smb_music_select_data (37095 - 0x8000)
#define smb_player_starting_x_pos (37142 - 0x8000)
#define smb_alt_y_pos_offset (37146 - 0x8000)
#define smb_player_starting_y_pos (37148 - 0x8000)
#define smb_player_bg_priority_data (37157 - 0x8000)
#define smb_game_timer_data (37165 - 0x8000)
#define smb_halfway_page_nybbles (37309 - 0x8000)
#define smb_b_scene_data_offsets (37623 - 0x8000)
#define smb_back_scenery_data (37626 - 0x8000)
#define smb_back_scenery_metatiles (37770 - 0x8000)
#define smb_f_scene_data_offsets (37806 - 0x8000)
#define smb_fore_scenery_data (37809 - 0x8000)
#define smb_terrain_metatiles (37848 - 0x8000)
#define smb_terrain_render_bits (37852 - 0x8000)
#define smb_block_buff_low_bounds (38148 - 0x8000)
#define smb_frenzy_id_data (38696 - 0x8000)
#define smb_pulley_rope_metatiles (38839 - 0x8000)
#define smb_castle_metatiles (38863 - 0x8000)
#define smb_side_pipe_shaft_data (39071 - 0x8000)
#define smb_side_pipe_top_part (39075 - 0x8000)
#define smb_side_pipe_bottom_part (39079 - 0x8000)
#define smb_vertical_pipe_data (39133 - 0x8000)
#define smb_coin_metatile_data (39406 - 0x8000)
#define smb_c_object_row (39419 - 0x8000)
#define smb_c_object_metatile (39422 - 0x8000)
#define smb_solid_block_metatiles (39461 - 0x8000)
#define smb_brick_metatiles (39465 - 0x8000)
#define smb_staircase_height_data (39589 - 0x8000)
#define smb_staircase_row_data (39598 - 0x8000)
#define smb_hole_metatiles (39741 - 0x8000)
#define smb_block_buffer_addr (39901 - 0x8000)
#define smb_area_data_ofs_loopback (39928 - 0x8000)
#define smb_world_addr_offsets (40116 - 0x8000)
#define smb_area_addr_offsets (40124 - 0x8000)
#define smb_world_2_areas (40129 - 0x8000)
#define smb_world_3_areas (40134 - 0x8000)
#define smb_world_4_areas (40138 - 0x8000)
#define smb_world_5_areas (40143 - 0x8000)
#define smb_world_6_areas (40147 - 0x8000)
#define smb_world_7_areas (40151 - 0x8000)
#define smb_world_8_areas (40156 - 0x8000)
#define smb_enemy_addr_h_offsets (40160 - 0x8000)
#define smb_enemy_data_addr_low (40164 - 0x8000)
#define smb_enemy_data_addr_high (40198 - 0x8000)
#define smb_area_data_h_offsets (40232 - 0x8000)
#define smb_area_data_addr_low (40236 - 0x8000)
#define smb_area_data_addr_high (40270 - 0x8000)
#define smb_e_castle_area_1 (40304 - 0x8000)
#define smb_e_castle_area_2 (40343 - 0x8000)
#define smb_e_castle_area_3 (40368 - 0x8000)
#define smb_e_castle_area_4 (40415 - 0x8000)
#define smb_e_castle_area_5 (40458 - 0x8000)
#define smb_e_castle_area_6 (40479 - 0x8000)
#define smb_e_ground_area_1 (40537 - 0x8000)
#define smb_e_ground_area_2 (40574 - 0x8000)
#define smb_e_ground_area_3 (40603 - 0x8000)
#define smb_e_ground_area_4 (40617 - 0x8000)
#define smb_e_ground_area_5 (40656 - 0x8000)
#define smb_e_ground_area_6 (40705 - 0x8000)
#define smb_e_ground_area_7 (40735 - 0x8000)
#define smb_e_ground_area_8 (40764 - 0x8000)
#define smb_e_ground_area_9 (40785 - 0x8000)
#define smb_e_ground_area_10 (40827 - 0x8000)
#define smb_e_ground_area_11 (40828 - 0x8000)
#define smb_e_ground_area_12 (40864 - 0x8000)
#define smb_e_ground_area_13 (40873 - 0x8000)
#define smb_e_ground_area_14 (40910 - 0x8000)
#define smb_e_ground_area_15 (40945 - 0x8000)
#define smb_e_ground_area_16 (40954 - 0x8000)
#define smb_e_ground_area_17 (40955 - 0x8000)
#define smb_e_ground_area_18 (41013 - 0x8000)
#define smb_e_ground_area_19 (41056 - 0x8000)
#define smb_e_ground_area_20 (41102 - 0x8000)
#define smb_e_ground_area_21 (41130 - 0x8000)
#define smb_e_ground_area_22 (41139 - 0x8000)
#define smb_e_underground_area_1 (41176 - 0x8000)
#define smb_e_underground_area_2 (41221 - 0x8000)
#define smb_e_underground_area_3 (41267 - 0x8000)
#define smb_e_water_area_1 (41312 - 0x8000)
#define smb_e_water_area_2 (41329 - 0x8000)
#define smb_e_water_area_3 (41371 - 0x8000)
#define smb_l_castle_area_1 (41391 - 0x8000)
#define smb_l_castle_area_2 (41488 - 0x8000)
#define smb_l_castle_area_3 (41615 - 0x8000)
#define smb_l_castle_area_4 (41730 - 0x8000)
#define smb_l_castle_area_5 (41839 - 0x8000)
#define smb_l_castle_area_6 (41978 - 0x8000)
#define smb_l_ground_area_1 (42091 - 0x8000)
#define smb_l_ground_area_2 (42190 - 0x8000)
#define smb_l_ground_area_3 (42295 - 0x8000)
#define smb_l_ground_area_4 (42378 - 0x8000)
#define smb_l_ground_area_5 (42521 - 0x8000)
#define smb_l_ground_area_6 (42638 - 0x8000)
#define smb_l_ground_area_7 (42739 - 0x8000)
#define smb_l_ground_area_8 (42824 - 0x8000)
#define smb_l_ground_area_9 (42957 - 0x8000)
#define smb_l_ground_area_10 (43058 - 0x8000)
#define smb_l_ground_area_11 (43067 - 0x8000)
#define smb_l_ground_area_12 (43130 - 0x8000)
#define smb_l_ground_area_13 (43151 - 0x8000)
#define smb_l_ground_area_14 (43254 - 0x8000)
#define smb_l_ground_area_15 (43355 - 0x8000)
#define smb_l_ground_area_16 (43470 - 0x8000)
#define smb_l_ground_area_17 (43519 - 0x8000)
#define smb_l_ground_area_18 (43666 - 0x8000)
#define smb_l_ground_area_19 (43781 - 0x8000)
#define smb_l_ground_area_20 (43902 - 0x8000)
#define smb_l_ground_area_21 (43991 - 0x8000)
#define smb_l_ground_area_22 (44034 - 0x8000)
#define smb_l_underground_area_1 (44085 - 0x8000)
#define smb_l_underground_area_2 (44248 - 0x8000)
#define smb_l_underground_area_3 (44409 - 0x8000)
#define smb_l_water_area_1 (44550 - 0x8000)
#define smb_l_water_area_2 (44613 - 0x8000)
#define smb_l_water_area_3 (44736 - 0x8000)
#define smb_x_subtracter_data (45108 - 0x8000)
#define smb_offscr_joypad_bits_data (45110 - 0x8000)
#define smb_hidden_1_up_coin_amts (45762 - 0x8000)
#define smb_climb_adder_low (46023 - 0x8000)
#define smb_climb_adder_high (46027 - 0x8000)
#define smb_jump_m_force_data (46116 - 0x8000)
#define smb_fall_m_force_data (46123 - 0x8000)
#define smb_player_y_spd_data (46130 - 0x8000)
#define smb_init_m_force_data (46137 - 0x8000)
#define smb_max_left_x_spd_data (46144 - 0x8000)
#define smb_max_right_x_spd_data (46147 - 0x8000)
#define smb_friction_data (46151 - 0x8000)
#define smb_climb_y_speed_data (46154 - 0x8000)
#define smb_climb_y_m_force_data (46157 - 0x8000)
#define smb_player_anim_tmr_data (46476 - 0x8000)
#define smb_fireball_x_spd_data (46727 - 0x8000)
#define smb_bubble_m_force_data (46923 - 0x8000)
#define smb_bubble_timer_data (46925 - 0x8000)
#define smb_flagpole_score_mods (47179 - 0x8000)
#define smb_flagpole_score_digits (47184 - 0x8000)
#define smb_jumpspring_y_pos_data (47286 - 0x8000)
#define smb_vine_height_data (47433 - 0x8000)
#define smb_cannon_bit_masks (47546 - 0x8000)
#define smb_bullet_bill_x_spd_data (47665 - 0x8000)
#define smb_hammer_enemy_ofs_data (47753 - 0x8000)
#define smb_hammer_x_spd_data (47762 - 0x8000)
#define smb_coin_tally_offsets (48120 - 0x8000)
#define smb_score_offsets (48122 - 0x8000)
#define smb_status_bar_nybbles (48124 - 0x8000)
#define smb_block_y_pos_adder_data (48363 - 0x8000)
#define smb_brick_q_block_metatiles (48616 - 0x8000)
#define smb_max_spd_block_data (49055 - 0x8000)
#define smb_loop_cmd_world_number (49259 - 0x8000)
#define smb_loop_cmd_page_number (49270 - 0x8000)
#define smb_loop_cmd_y_position (49281 - 0x8000)
#define smb_normal_x_spd_data (49932 - 0x8000)
#define smb_h_bro_walking_timer_data (49958 - 0x8000)
#define smb_pr_diff_adjust_data (50072 - 0x8000)
#define smb_firebar_spin_spd_data (50255 - 0x8000)
#define smb_firebar_spin_dir_data (50260 - 0x8000)
#define smb_fly_ccx_position_data (50312 - 0x8000)
#define smb_fly_ccx_speed_data (50328 - 0x8000)
#define smb_fly_cc_timer_data (50340 - 0x8000)
#define smb_flame_y_pos_data (50589 - 0x8000)
#define smb_flame_ymf_adder_data (50593 - 0x8000)
#define smb_fireworks_x_pos_data (50737 - 0x8000)
#define smb_fireworks_y_pos_data (50743 - 0x8000)
#define smb_bitmasks (50826 - 0x8000)
#define smb_enemy_17_y_pos_data (50834 - 0x8000)
#define smb_swim_cc_id_data (50842 - 0x8000)
#define smb_plat_pos_data_low (51307 - 0x8000)
#define smb_plat_pos_data_high (51310 - 0x8000)
#define smb_hammer_throw_tmr_data (51662 - 0x8000)
#define smb_x_speed_adder_data (51664 - 0x8000)
#define smb_revived_x_speed (51668 - 0x8000)
#define smb_hammer_bro_jump_l_data (51728 - 0x8000)
#define smb_bloober_bitmasks (52103 - 0x8000)
#define smb_swim_ccx_move_data (52294 - 0x8000)
#define smb_firebar_pos_lookup_tbl (52423 - 0x8000)
#define smb_firebar_mirror_data (52522 - 0x8000)
#define smb_firebar_tbl_offsets (52526 - 0x8000)
#define smb_firebar_y_pos (52538 - 0x8000)
#define smb_p_random_subtracter (52949 - 0x8000)
#define smb_fly_ccb_priority (52954 - 0x8000)
#define smb_lakitu_diff_adj (53029 - 0x8000)
#define smb_bridge_collapse_data (53213 - 0x8000)
#define smb_p_random_range (53345 - 0x8000)
#define smb_flame_timer_data (53713 - 0x8000)
#define smb_star_flag_y_pos_adder (53965 - 0x8000)
#define smb_star_flag_x_pos_adder (53969 - 0x8000)
#define smb_star_flag_tile_data (53973 - 0x8000)
#define smb_bowser_identities (55094 - 0x8000)
#define smb_residual_x_spd_data (55373 - 0x8000)
#define smb_kicked_shell_x_spd_data (55375 - 0x8000)
#define smb_demoted_koopa_x_spd_data (55377 - 0x8000)
#define smb_kicked_shell_pts_data (55442 - 0x8000)
#define smb_stomped_enemy_pts_data (55653 - 0x8000)
#define smb_revival_rate_data (55762 - 0x8000)
#define smb_set_bits_mask (55845 - 0x8000)
#define smb_clear_bits_mask (55852 - 0x8000)
#define smb_player_pos_s_plat_data (56343 - 0x8000)
#define smb_player_bg_upper_extent (56418 - 0x8000)
#define smb_area_change_timer_data (56835 - 0x8000)
#define smb_climb_x_pos_adder (56869 - 0x8000)
#define smb_climb_p_loc_adder (56871 - 0x8000)
#define smb_flagpole_y_pos_data (56873 - 0x8000)
#define smb_solid_m_tile_upper_ext (57227 - 0x8000)
#define smb_climb_m_tile_upper_ext (57238 - 0x8000)
#define smb_enemy_bgc_state_data (57273 - 0x8000)
#define smb_enemy_bgcx_spd_data (57279 - 0x8000)
#define smb_bound_box_ctrl_data (57853 - 0x8000)
#define smb_block_buffer_adder_data (58285 - 0x8000)
#define smb_block_buffer_x_adder (58288 - 0x8000)
#define smb_block_buffer_y_adder (58316 - 0x8000)
#define smb_vine_y_pos_adder (58419 - 0x8000)
#define smb_first_spr_x_pos (58560 - 0x8000)
#define smb_first_spr_y_pos (58564 - 0x8000)
#define smb_second_spr_x_pos (58568 - 0x8000)
#define smb_second_spr_y_pos (58572 - 0x8000)
#define smb_first_spr_tilenum (58576 - 0x8000)
#define smb_second_spr_tilenum (58580 - 0x8000)
#define smb_hammer_spr_attrib (58584 - 0x8000)
#define smb_flagpole_score_num_tiles (58689 - 0x8000)
#define smb_jumping_coin_tiles (59010 - 0x8000)
#define smb_power_up_gfx_table (59070 - 0x8000)
#define smb_power_up_attributes (59086 - 0x8000)
#define smb_enemy_graphics_table (59198 - 0x8000)
#define smb_enemy_gfx_table_offsets (59456 - 0x8000)
#define smb_enemy_attribute_data (59483 - 0x8000)
#define smb_enemy_anim_timing_b_mask (59510 - 0x8000)
#define smb_jumpspring_frame_offsets (59512 - 0x8000)
#define smb_default_block_obj_tiles (60365 - 0x8000)
#define smb_explosion_tiles (60678 - 0x8000)
#define smb_player_gfx_tbl_offsets (60935 - 0x8000)
#define smb_player_graphics_table (60951 - 0x8000)
#define smb_swim_tiles (61087 - 0x8000)
#define smb_swim_kick_tile_num (61159 - 0x8000)
#define smb_intermediate_player_data (61342 - 0x8000)
#define smb_change_size_offset_adder (61596 - 0x8000)
#define smb_obj_offset_data (61861 - 0x8000)
#define smb_x_offscreen_bits_data (61923 - 0x8000)
#define smb_default_x_onscreen_ofs (61939 - 0x8000)
#define smb_y_offscreen_bits_data (61995 - 0x8000)
#define smb_default_y_onscreen_ofs (62004 - 0x8000)
#define smb_high_pos_unit_data (62007 - 0x8000)
#define smb_swim_stomp_envelope_data (62385 - 0x8000)
#define smb_extra_life_freq_data (62676 - 0x8000)
#define smb_power_up_grab_freq_data (62682 - 0x8000)
#define smb_p_up_v_grow_freq_data (62712 - 0x8000)
#define smb_brick_shatter_freq_data (63019 - 0x8000)
#define smb_music_header_data (63757 - 0x8000)
#define smb_time_running_out_hdr (63806 - 0x8000)
#define smb_str_cloud_hdr (63811 - 0x8000)
#define smb_end_of_level_mus_hdr (63817 - 0x8000)
#define smb_residual_header_data (63822 - 0x8000)
#define smb_underground_mus_hdr (63827 - 0x8000)
#define smb_silence_hdr (63832 - 0x8000)
#define smb_castle_mus_hdr (63836 - 0x8000)
#define smb_victory_mus_hdr (63841 - 0x8000)
#define smb_game_over_mus_hdr (63846 - 0x8000)
#define smb_water_mus_hdr (63851 - 0x8000)
#define smb_win_castle_mus_hdr (63857 - 0x8000)
#define smb_ground_level_part_1_hdr (63862 - 0x8000)
#define smb_ground_level_part_2_a_hdr (63868 - 0x8000)
#define smb_ground_level_part_2_b_hdr (63874 - 0x8000)
#define smb_ground_level_part_2_c_hdr (63880 - 0x8000)
#define smb_ground_level_part_3_a_hdr (63886 - 0x8000)
#define smb_ground_level_part_3_b_hdr (63892 - 0x8000)
#define smb_ground_level_lead_in_hdr (63898 - 0x8000)
#define smb_ground_level_part_4_a_hdr (63904 - 0x8000)
#define smb_ground_level_part_4_b_hdr (63910 - 0x8000)
#define smb_ground_level_part_4_c_hdr (63916 - 0x8000)
#define smb_death_mus_hdr (63922 - 0x8000)
#define smb_star_cloud_m_data (63928 - 0x8000)
#define smb_ground_m_p_1_data (64001 - 0x8000)
#define smb_silence_data (64028 - 0x8000)
#define smb_ground_m_p_2_a_data (64073 - 0x8000)
#define smb_ground_m_p_2_b_data (64117 - 0x8000)
#define smb_ground_m_p_2_c_data (64157 - 0x8000)
#define smb_ground_m_p_3_a_data (64194 - 0x8000)
#define smb_ground_m_p_3_b_data (64219 - 0x8000)
#define smb_ground_m_ld_in_data (64249 - 0x8000)
#define smb_ground_m_p_4_a_data (64293 - 0x8000)
#define smb_ground_m_p_4_b_data (64331 - 0x8000)
#define smb_death_mus_data (64370 - 0x8000)
#define smb_ground_m_p_4_c_data (64372 - 0x8000)
#define smb_castle_mus_data (64420 - 0x8000)
#define smb_game_over_mus_data (64581 - 0x8000)
#define smb_time_run_out_mus_data (64626 - 0x8000)
#define smb_win_level_mus_data (64688 - 0x8000)
#define smb_underground_mus_data (64785 - 0x8000)
#define smb_water_mus_data (64850 - 0x8000)
#define smb_end_of_castle_mus_data (65105 - 0x8000)
#define smb_victory_mus_data (65224 - 0x8000)
#define smb_freq_reg_lookup_tbl (65280 - 0x8000)
#define smb_music_length_lookup_tbl (65382 - 0x8000)
#define smb_end_of_castle_music_env_data (65430 - 0x8000)
#define smb_area_music_env_data (65434 - 0x8000)
#define smb_water_event_mus_env_data (65442 - 0x8000)
#define smb_bowser_flame_env_data (65482 - 0x8000)
#define smb_brick_shatter_env_data (65514 - 0x8000)

void smb_start();
void smb_non_maskable_interrupt();
void smb_pause_routine();
void smb_sprite_shuffler();
void smb_oper_mode_execution_tree();
void smb_move_all_sprites_offscreen();
void smb_move_sprites_except_0_offscreen();
void smb_move_sprites_offscreen();
void smb_title_screen_mode();
void smb_game_menu_routine();
void smb_go_continue();
void smb_draw_mushroom_icon();
void smb_demo_engine();
void smb_victory_mode();
void smb_victory_mode_subroutines();
void smb_setup_victory_mode();
void smb_player_victory_walk();
void smb_print_victory_messages();
void smb_inc_mode_task_a();
void smb_exit_msgs();
void smb_player_end_world();
void smb_end_exit_one();
void smb_end_chk_b_button();
void smb_floatey_numbers_routine();
void smb_screen_routines();
void smb_init_screen();
void smb_setup_intermediate();
void smb_get_area_palette();
void smb_set_vram_addr_a();
void smb_next_subtask();
void smb_get_background_color();
void smb_get_player_colors();
void smb_set_vram_offset();
void smb_get_alternate_palette_1();
void smb_set_vram_addr_b();
void smb_no_alt_pal();
void smb_write_top_status_line();
void smb_write_bottom_status_line();
void smb_display_time_up();
void smb_display_intermediate();
void smb_output_inter();
void smb_game_over_inter();
void smb_no_inter();
void smb_area_parser_task_control();
void smb_draw_title_screen();
void smb_clear_buffers_draw_icon();
void smb_inc_subtask();
void smb_write_top_score();
void smb_inc_mode_task_b();
void smb_write_game_text();
void smb_reset_sprites_and_screen_timer();
void smb_reset_screen_timer();
void smb_no_reset();
void smb_render_area_graphics();
void smb_render_attribute_tables();
void smb_set_vram_ctrl();
void smb_color_rotation();
void smb_remove_coin_axe();
void smb_replace_block_metatile();
void smb_destroy_block_metatile();
void smb_write_block_metatile();
void smb_move_v_offset();
void smb_put_block_metatile();
void smb_rem_bridge();
void smb_jump_engine();
void smb_initialize_name_tables();
void smb_write_nt_addr();
void smb_read_joypads();
void smb_read_port_bits();
void smb_write_buffer_to_screen();
void smb_update_screen();
void smb_init_scroll();
void smb_write_ppu_reg_1();
void smb_print_status_bar_numbers();
void smb_output_numbers();
void smb_digits_math_routine();
void smb_update_top_score();
void smb_top_score_check();
void smb_initialize_game();
void smb_initialize_area();
void smb_primary_game_setup();
void smb_secondary_game_setup();
void smb_initialize_memory();
void smb_get_area_music();
void smb_entrance_game_timer_setup();
void smb_player_lose_life();
void smb_game_over_mode();
void smb_setup_game_over();
void smb_run_game_over();
void smb_terminate_game();
void smb_continue_game();
void smb_game_is_on();
void smb_transpose_players();
void smb_do_nothing_1();
void smb_do_nothing_2();
void smb_area_parser_task_handler();
void smb_area_parser_tasks();
void smb_increment_column_pos();
void smb_area_parser_core();
void smb_process_area_data();
void smb_end_a_parse();
void smb_inc_area_obj_offset();
void smb_decode_area_data();
void smb_leave_par();
void smb_init_rear();
void smb_loop_cmd_e();
void smb_back_col_c();
void smb_star_a_obj();
void smb_run_a_obj();
void smb_alter_area_attributes();
void smb_scroll_lock_object_warp();
void smb_scroll_lock_object();
void smb_kill_enemies();
void smb_area_frenzy();
void smb_area_style_object();
void smb_tree_ledge();
void smb_mushroom_ledge();
void smb_all_under();
void smb_no_under();
void smb_pulley_rope_object();
void smb_mush_l_exit();
void smb_castle_object();
void smb_water_pipe();
void smb_intro_pipe();
void smb_exit_pipe();
void smb_render_sideways_pipe();
void smb_vertical_pipe();
void smb_get_pipe_height();
void smb_find_empty_enemy_slot();
void smb_hole_water();
void smb_question_block_row_high();
void smb_question_block_row_low();
void smb_question_block_row();
void smb_bridge_high();
void smb_bridge_middle();
void smb_bridge_low();
void smb_bridge();
void smb_flag_balls_residual();
void smb_flagpole_object();
void smb_endless_rope();
void smb_balance_plat_rope();
void smb_draw_rope();
void smb_row_of_coins();
void smb_castle_bridge_obj();
void smb_axe_obj();
void smb_chain_obj();
void smb_empty_block();
void smb_col_obj();
void smb_row_of_bricks();
void smb_row_of_solid_blocks();
void smb_get_row();
void smb_draw_row();
void smb_column_of_bricks();
void smb_column_of_solid_blocks();
void smb_get_row_2();
void smb_bullet_bill_cannon();
void smb_staircase_object();
void smb_jump_spring();
void smb_hidden_1_up_block();
void smb_question_block();
void smb_brick_with_coins();
void smb_brick_with_item();
void smb_draw_q_blk();
void smb_get_area_object_id();
void smb_exit_dec_block();
void smb_hole_empty();
void smb_render_under_part();
void smb_chk_lrg_obj_length();
void smb_chk_lrg_obj_fixed_length();
void smb_get_lrg_obj_attrib();
void smb_get_area_obj_x_position();
void smb_get_area_obj_y_position();
void smb_get_block_buffer_addr();
void smb_load_area_pointer();
void smb_get_area_type();
void smb_find_area_pointer();
void smb_get_area_data_addrs();
void smb_game_mode();
void smb_game_core_routine();
void smb_upd_scroll_var();
void smb_scroll_handler();
void smb_scroll_screen();
void smb_init_scrl_amt();
void smb_chk_p_offscr();
void smb_get_screen_position();
void smb_game_routines();
void smb_player_entrance();
void smb_auto_control_player();
void smb_player_ctrl_routine();
void smb_vine_auto_climb();
void smb_set_entr();
void smb_vertical_pipe_entry();
void smb_move_player_y_axis();
void smb_side_exit_pipe_entry();
void smb_chg_area_pipe();
void smb_chg_area_mode();
void smb_exit_ca_pipe();
void smb_enter_side_pipe();
void smb_player_change_size();
void smb_player_injury_blink();
void smb_init_change_size();
void smb_exit_both();
void smb_player_death();
void smb_done_player_task();
void smb_player_fire_flower();
void smb_cycle_player_palette();
void smb_reset_pal_fire_flower();
void smb_reset_pal_star();
void smb_exit_death();
void smb_flagpole_slide();
void smb_player_end_level();
void smb_next_area();
void smb_exit_na();
void smb_player_movement_subs();
void smb_on_ground_state_sub();
void smb_falling_sub();
void smb_jump_swim_sub();
void smb_lr_air();
void smb_climbing_sub();
void smb_player_physics_sub();
void smb_get_player_anim_speed();
void smb_impose_friction();
void smb_proc_fireball_bubble();
void smb_fireball_obj_core();
void smb_bubble_check();
void smb_setup_bubble();
void smb_move_bubl();
void smb_exit_bubl();
void smb_run_game_timer();
void smb_ex_g_timer();
void smb_warp_zone_object();
void smb_process_whirlpools();
void smb_flagpole_routine();
void smb_jumpspring_handler();
void smb_setup_vine();
void smb_vine_object_handler();
void smb_process_cannons();
void smb_bullet_bill_handler();
void smb_spawn_hammer_obj();
void smb_proc_hammer_obj();
void smb_coin_block();
void smb_setup_jump_coin();
void smb_j_coin_c();
void smb_find_empty_misc_slot();
void smb_misc_objects_core();
void smb_give_one_coin();
void smb_add_to_score();
void smb_get_sb_nybbles();
void smb_update_number();
void smb_setup_power_up();
void smb_pwr_up_jmp();
void smb_power_up_obj_handler();
void smb_player_head_collision();
void smb_init_block_xy_pos();
void smb_bump_block();
void smb_mush_flower_block();
void smb_star_block();
void smb_extra_life_mush_block();
void smb_item_block();
void smb_vine_block();
void smb_exit_block_chk();
void smb_block_bumped_chk();
void smb_brick_shatter();
void smb_check_top_of_block();
void smb_spawn_brick_chunks();
void smb_block_objects_core();
void smb_block_obj_mt_updater();
void smb_move_enemy_horizontally();
void smb_move_player_horizontally();
void smb_move_object_horizontally();
void smb_ex_x_move();
void smb_move_player_vertically();
void smb_move_d_enemy_vertically();
void smb_move_falling_platform();
void smb_cont_v_move();
void smb_move_red_p_troopa_down();
void smb_move_red_p_troopa_up();
void smb_move_red_p_troopa();
void smb_move_drop_platform();
void smb_move_enemy_slow_vert();
void smb_set_md_max();
void smb_move_j_enemy_vertically();
void smb_set_hi_max();
void smb_set_x_move_amt();
void smb_impose_gravity_block();
void smb_impose_gravity_spr_obj();
void smb_move_platform_down();
void smb_move_platform_up();
void smb_move_platform();
void smb_red_p_troopa_grav();
void smb_impose_gravity();
void smb_enemies_and_loops_core();
void smb_exec_game_loopback();
void smb_proc_loop_command();
void smb_init_enemy_object();
void smb_ex_e_par();
void smb_do_group();
void smb_parse_row_0_e();
void smb_check_three_bytes();
void smb_inc_3_b();
void smb_inc_2_b();
void smb_checkpoint_enemy_id();
void smb_no_init_code();
void smb_init_goomba();
void smb_init_podoboo();
void smb_init_retainer_obj();
void smb_init_normal_enemy();
void smb_set_e_spd();
void smb_init_red_koopa();
void smb_init_hammer_bro();
void smb_init_horiz_fly_swim_enemy();
void smb_init_bloober();
void smb_small_b_box();
void smb_init_red_p_troopa();
void smb_tall_b_box();
void smb_set_b_box();
void smb_init_v_stf();
void smb_init_bullet_bill();
void smb_init_cheep_cheep();
void smb_init_lakitu();
void smb_setup_lakitu();
void smb_kill_lakitu();
void smb_lakitu_and_spiny_handler();
void smb_chp_chp_ex();
void smb_init_long_firebar();
void smb_init_short_firebar();
void smb_init_flying_cheep_cheep();
void smb_init_bowser();
void smb_duplicate_enemy_obj();
void smb_flm_ex();
void smb_init_bowser_flame();
void smb_put_at_right_extent();
void smb_spawn_from_mouth();
void smb_finish_flame();
void smb_init_fireworks();
void smb_bullet_bill_cheep_cheep();
void smb_handle_group_enemies();
void smb_init_piranha_plant();
void smb_init_enemy_frenzy();
void smb_no_frenzy_code();
void smb_end_frenzy();
void smb_init_jump_gp_troopa();
void smb_tall_b_box_2();
void smb_set_b_box_2();
void smb_init_bal_platform();
void smb_init_drop_platform();
void smb_init_hori_platform();
void smb_init_vert_platform();
void smb_common_plat_code();
void smb_spb_box();
void smb_large_lift_up();
void smb_large_lift_down();
void smb_large_lift_b_box();
void smb_plat_lift_up();
void smb_plat_lift_down();
void smb_common_small_lift();
void smb_pos_platform();
void smb_end_of_enemy_init_code();
void smb_run_enemy_objects_core();
void smb_no_run_code();
void smb_run_retainer_obj();
void smb_run_normal_enemies();
void smb_enemy_movement_subs();
void smb_no_move_code();
void smb_run_bowser_flame();
void smb_run_firebar_obj();
void smb_run_small_platform();
void smb_run_large_platform();
void smb_large_platform_subroutines();
void smb_erase_enemy_object();
void smb_move_podoboo();
void smb_proc_hammer_bro();
void smb_hammer_bro_jump_code();
void smb_set_hj();
void smb_move_hammer_bro_x_dir();
void smb_move_normal_enemy();
void smb_move_defeated_enemy();
void smb_chk_kill_goomba();
void smb_move_jumping_enemy();
void smb_proc_move_red_p_troopa();
void smb_move_fly_green_p_troopa();
void smb_x_move_cntr_green_p_troopa();
void smb_x_move_cntr_platform();
void smb_move_with_xm_cntrs();
void smb_move_bloober();
void smb_proc_swimming_b();
void smb_move_bullet_bill();
void smb_move_swimming_cheep_cheep();
void smb_proc_firebar();
void smb_draw_firebar_collision();
void smb_firebar_collision();
void smb_get_firebar_position();
void smb_move_flying_cheep_cheep();
void smb_move_lakitu();
void smb_player_lakitu_diff();
void smb_bridge_collapse();
void smb_move_d_bowser();
void smb_remove_bridge();
void smb_run_bowser();
void smb_kill_all_enemies();
void smb_bowser_control();
void smb_bowser_gfx_handler();
void smb_ex_bgfx_h();
void smb_process_bowser_half();
void smb_set_flame_timer();
void smb_ex_fl();
void smb_proc_bowser_flame();
void smb_run_fireworks();
void smb_run_star_flag_obj();
void smb_game_timer_fireworks();
void smb_increment_sf_task_1();
void smb_star_flag_exit();
void smb_award_game_timer_points();
void smb_end_area_points();
void smb_raise_flag_setoff_fworks();
void smb_draw_star_flag();
void smb_draw_flag_set_timer();
void smb_increment_sf_task_2();
void smb_delay_to_area_end();
void smb_move_piranha_plant();
void smb_firebar_spin();
void smb_balance_platform();
void smb_setup_platform_rope();
void smb_init_platform_fall();
void smb_stop_platforms();
void smb_platform_fall();
void smb_y_moving_platform();
void smb_chk_yp_collision();
void smb_x_moving_platform();
void smb_position_player_on_h_plat();
void smb_ex_xmp();
void smb_drop_platform();
void smb_right_platform();
void smb_move_large_lift_plat();
void smb_move_small_platform();
void smb_move_lift_platforms();
void smb_chk_small_plat_collision();
void smb_ex_lift_p();
void smb_offscreen_bounds_check();
void smb_fireball_enemy_collision();
void smb_handle_enemy_f_ball_col();
void smb_shell_or_block_defeat();
void smb_enemy_smack_core();
void smb_ex_hcf();
void smb_player_hammer_collision();
void smb_handle_power_up_collision();
void smb_no_p_up();
void smb_player_enemy_collision();
void smb_handle_pe_collisions();
void smb_injure_player();
void smb_force_injury();
void smb_set_k_rout();
void smb_set_p_rout();
void smb_ex_inj_col_routines();
void smb_kill_player();
void smb_enemy_stomped();
void smb_handle_stomped_shell_e();
void smb_s_bnce();
void smb_chk_enemy_face_right();
void smb_l_inj();
void smb_enemy_face_player();
void smb_setup_floatey_number();
void smb_ex_sfn();
void smb_enemies_collision();
void smb_proc_enemy_collisions();
void smb_enemy_turn_around();
void smb_rx_spd();
void smb_ex_ta();
void smb_large_platform_collision();
void smb_chk_for_player_c_large_p();
void smb_ex_lpc();
void smb_small_platform_collision();
void smb_proc_l_plat_collisions();
void smb_position_player_on_s_plat();
void smb_position_player_on_v_plat();
void smb_position_player_on_plat();
void smb_check_player_vertical();
void smb_get_enemy_bound_box_ofs();
void smb_get_enemy_bound_box_ofs_arg();
void smb_player_bg_collision();
void smb_handle_coin_metatile();
void smb_handle_axe_metatile();
void smb_er_acm();
void smb_handle_climbing();
void smb_chk_invisible_m_tiles();
void smb_chk_for_land_jump_spring();
void smb_chk_jumpspring_metatiles();
void smb_handle_pipe_entry();
void smb_impede_player_move();
void smb_check_for_solid_m_tiles();
void smb_check_for_climb_m_tiles();
void smb_check_for_coin_m_tiles();
void smb_get_m_tile_attrib();
void smb_ex_ebg();
void smb_enemy_to_bg_collision_det();
void smb_chk_to_stun_enemies();
void smb_set_stun();
void smb_ex_ebg_chk();
void smb_land_enemy_properly();
void smb_chk_for_red_koopa();
void smb_do_enemy_side_check();
void smb_chk_for_bump_hammer_bro_j();
void smb_player_enemy_diff();
void smb_enemy_landing();
void smb_subt_enemy_y_pos();
void smb_enemy_jump();
void smb_hammer_bro_bg_coll();
void smb_kill_enemy_above_block();
void smb_under_hammer_bro();
void smb_no_under_hammer_bro();
void smb_chk_under_enemy();
void smb_chk_for_non_solids();
void smb_fireball_bg_collision();
void smb_get_fireball_bound_box();
void smb_get_misc_bound_box();
void smb_f_ball_b();
void smb_get_enemy_bound_box();
void smb_small_platform_bound_box();
void smb_get_masked_off_scr_bits();
void smb_large_platform_bound_box();
void smb_setup_e_offset_fb_box();
void smb_move_bound_box_offscreen();
void smb_bounding_box_core();
void smb_check_right_screen_b_box();
void smb_player_collision_core();
void smb_spr_object_collision_core();
void smb_block_buffer_chk_enemy();
void smb_block_buffer_chk_f_ball();
void smb_res_jmp_m();
void smb_bb_chk_e();
void smb_block_buffer_colli_feet();
void smb_block_buffer_colli_head();
void smb_block_buffer_colli_side();
void smb_block_buffer_colli();
void smb_block_buffer_collision();
void smb_draw_vine();
void smb_six_sprite_stacker();
void smb_draw_hammer();
void smb_flagpole_gfx_handler();
void smb_move_six_sprites_offscreen();
void smb_dump_six_spr();
void smb_dump_four_spr();
void smb_dump_three_spr();
void smb_dump_two_spr();
void smb_exit_dump_spr();
void smb_draw_large_platform();
void smb_draw_floatey_number_coin();
void smb_j_coin_gfx_handler();
void smb_ex_jc_gfx();
void smb_draw_power_up();
void smb_enemy_gfx_handler();
void smb_spr_object_offscr_chk();
void smb_draw_enemy_obj_row();
void smb_draw_one_sprite_row();
void smb_move_e_spr_row_offscreen();
void smb_move_e_spr_col_offscreen();
void smb_draw_block();
void smb_chk_left_co();
void smb_move_col_offscreen();
void smb_ex_d_blk();
void smb_draw_brick_chunks();
void smb_draw_fireball();
void smb_draw_firebar();
void smb_draw_explosion_fireball();
void smb_draw_explosion_fireworks();
void smb_kill_fire_ball();
void smb_draw_small_platform();
void smb_draw_bubble();
void smb_player_gfx_handler();
void smb_find_player_action();
void smb_do_change_size();
void smb_player_killed();
void smb_player_gfx_processing();
void smb_draw_player_intermediate();
void smb_render_player_sub();
void smb_draw_player_loop();
void smb_process_player_action();
void smb_get_current_anim_offset();
void smb_four_frame_extent();
void smb_three_frame_extent();
void smb_animation_control();
void smb_get_gfx_offset_adder();
void smb_handle_change_size();
void smb_get_offset_from_anim_ctrl();
void smb_shrink_player();
void smb_chk_for_player_attrib();
void smb_relative_player_position();
void smb_relative_bubble_position();
void smb_relative_fireball_position();
void smb_rel_w_ofs();
void smb_relative_misc_position();
void smb_relative_enemy_position();
void smb_relative_block_position();
void smb_variable_obj_ofs_rel_pos();
void smb_get_obj_relative_position();
void smb_get_player_offscreen_bits();
void smb_get_fireball_offscreen_bits();
void smb_get_bubble_offscreen_bits();
void smb_get_misc_offscreen_bits();
void smb_get_proper_obj_offset();
void smb_get_enemy_offscreen_bits();
void smb_get_block_offscreen_bits();
void smb_set_offscr_bits_offset();
void smb_get_off_screen_bits_set();
void smb_run_offscr_bits_subs();
void smb_get_x_offscreen_bits();
void smb_get_y_offscreen_bits();
void smb_divide_p_diff();
void smb_draw_sprite_object();
void smb_sound_engine();
void smb_dump_squ_1_regs();
void smb_play_squ_1_sfx();
void smb_set_freq_squ_1();
void smb_dump_freq_regs();
void smb_dump_sq_2_regs();
void smb_play_squ_2_sfx();
void smb_set_freq_squ_2();
void smb_set_freq_tri();
void smb_play_flagpole_slide();
void smb_play_small_jump();
void smb_play_big_jump();
void smb_jump_reg_contents();
void smb_continue_snd_jump();
void smb_fps_2_nd();
void smb_dmp_jp_fps();
void smb_play_fireball_throw();
void smb_play_bump();
void smb_fthrow();
void smb_continue_bump_throw();
void smb_dec_jp_fps();
void smb_square_1_sfx_handler();
void smb_branch_to_dec_length_1();
void smb_play_smack_enemy();
void smb_continue_smack_enemy();
void smb_decrement_sfx_1_length();
void smb_stop_square_1_sfx();
void smb_ex_sfx_1();
void smb_play_pipe_down_inj();
void smb_continue_pipe_down_inj();
void smb_play_coin_grab();
void smb_play_timer_tick();
void smb_c_grab_t_tick_reg_l();
void smb_continue_c_grab_t_tick();
void smb_play_blast();
void smb_continue_blast();
void smb_s_blas_j();
void smb_play_power_up_grab();
void smb_continue_power_up_grab();
void smb_load_squ_2_regs();
void smb_decrement_sfx_2_length();
void smb_empty_sfx_2_buffer();
void smb_stop_square_2_sfx();
void smb_es_sfx_2();
void smb_square_2_sfx_handler();
void smb_jump_to_dec_length_2();
void smb_play_bowser_fall();
void smb_blst_s_jp();
void smb_continue_bowser_fall();
void smb_pbf_regs();
void smb_el_l_regs();
void smb_play_extra_life();
void smb_continue_extra_life();
void smb_play_grow_power_up();
void smb_play_grow_vine();
void smb_grow_item_regs();
void smb_continue_grow_items();
void smb_play_brick_shatter();
void smb_continue_brick_shatter();
void smb_play_noise_sfx();
void smb_decrement_sfx_3_length();
void smb_noise_sfx_handler();
void smb_continue_music();
void smb_music_handler();
void smb_load_event_music();
void smb_load_area_music();
void smb_gm_loop_b();
void smb_handle_area_music_loop_b();
void smb_find_event_music_header();
void smb_load_header();
void smb_handle_square_2_music();
void smb_alternate_length_nalder();
void smb_process_length_data();
void smb_load_control_regs();
void smb_load_envelope_data();

