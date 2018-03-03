static u32
import_skills(Application *app) {
	u32 res = 1;
	FILE *file_handle = fopen("mh_world_skills.csv", "r");
	kh_assert(file_handle);
	fseek(file_handle, 0, SEEK_END);
	umm file_size = ftell(file_handle);
	fseek(file_handle, 0, SEEK_SET);
	char *file_contents = (char *)calloc(1, sizeof(char) * file_size);
	kh_assert(file_contents);
	umm result_size = fread(file_contents, 1, file_size, file_handle);
	fclose(file_handle);

	StringTokenizer str_tok = {file_contents, TokenizerFlags_dont_ignored_white_space};
	Token tok = get_token_and_next(&str_tok);


	while(!token_fit(tok, Token_end_of_file)) {
		if(token_fit(tok, Token_eol)) {
			res++;
		}
		tok = get_token_and_next(&str_tok);
	}
	str_tok.pos = file_contents;
	tok = get_token_and_next(&str_tok);
	app->skills.reserve(res);

	while(!token_fit(tok, Token_end_of_file)) {
		char *name = tok.text;
		umm name_len = tok.text_length;
		tok = get_token_and_next(&str_tok);
		while(!token_fit(tok, Token_comma)) {
			kh_assert(!token_fit(tok, Token_end_of_file));
			name_len += tok.text_length;
			tok = get_token_and_next(&str_tok);
		}
		std::string str(name, name_len);
		auto search = app->skills.find(str);
		kh_assert(search == app->skills.end());

		tok = get_token_and_next(&str_tok);
		u32 max = token_to_u32(tok);

		tok = get_token_and_next(&str_tok);
		kh_assert(token_fit(tok, Token_comma));

		tok = get_token_and_next(&str_tok);
		u32 deco_slot = token_to_u32(tok);

		tok = get_token_and_next(&str_tok);
		kh_assert(token_fit(tok, Token_comma));

		tok = get_token_and_next(&str_tok);
		u32 deco_rarity = token_to_u32(tok);

		tok = get_token_and_next(&str_tok);
		kh_assert(token_fit(tok, Token_eol) || token_fit(tok, Token_end_of_file));

		if(token_fit(tok, Token_eol)) {
			tok = get_token_and_next(&str_tok);
		}
		Skill skill = {};
		skill.max = max;
		skill.decoration_slot = deco_slot;
		skill.decoration_rarity = deco_rarity;
		app->skills.insert(std::make_pair(str, skill));
	}
	free(file_contents);
	return(res);
}

inline b32 
parsing_piece(MHWParser parse_state) {
	b32 res = false;
	if(parse_state >= MHWParser_alpha_piece_1 && parse_state <= MHWParser_beta_piece_5) {
		res = true;
	}
	return(res);
}

inline MHWParser
advance_parse_state(MHWParser parse_state) {
	MHWParser res = (MHWParser)((u32)parse_state + 1);
	return(res);
}

static void
import_armors(Application *app) {
	FILE *file_handle = fopen("mhworld_armor_hr.csv", "r");
	kh_assert(file_handle);			
	fseek(file_handle, 0, SEEK_END);
	umm file_size = ftell(file_handle);
	fseek(file_handle, 0, SEEK_SET);
	char *file_contents = (char *)calloc(1, sizeof(char) * file_size);
	kh_assert(file_contents);
	umm result_size = fread(file_contents, 1, file_size, file_handle);
			// kh_assert(result_size == file_size);
	fclose(file_handle);

	StringTokenizer str_tok = {file_contents, TokenizerFlags_dont_ignored_white_space};
	Token tok = get_token_and_next(&str_tok);
	u32 total_mhset_count = 1;
	while(!token_fit(tok, Token_end_of_file)) {
		if(token_fit(tok, Token_eol)) {
			total_mhset_count++;
		}
		tok = get_token_and_next(&str_tok);
	}
	str_tok.pos = file_contents;
	tok = get_token_and_next(&str_tok);


	const u32 list_allocator_max_count = 1024;
	u32 list_allocator_count = 0;
	PieceList *list_allocator = (PieceList *)calloc(1, sizeof(PieceList) * list_allocator_max_count);

	app->sets.resize(total_mhset_count);

	MHWParser parse_state = MHWParser_set_name;
	u32 cur_piece = PieceType_head_alpha;
	u32 mhset_count = 0;
	while(!token_fit(tok, Token_end_of_file)) {
		if(parse_state == MHWParser_set_name) {
			kh_assert(mhset_count < total_mhset_count);
			char *name = tok.text;
			umm name_len = tok.text_length;
			tok = get_token_and_next(&str_tok);
			while(!token_fit(tok, Token_comma)) {
				kh_assert(!token_fit(tok, Token_end_of_file));
				name_len += tok.text_length;
				tok = get_token_and_next(&str_tok);
			}
			app->sets[mhset_count].name.assign(name, name_len);
			app->sets[mhset_count].available = true;
			auto search = app->sets_map.find(app->sets[mhset_count].name);
			kh_assert(search == app->sets_map.end());
			app->sets_map.insert(std::make_pair(app->sets[mhset_count].name, mhset_count));
			cur_piece = PieceType_head_alpha;
			parse_state = advance_parse_state(parse_state);
		} else if(parsing_piece(parse_state)) {
			Piece *piece = app->sets[mhset_count].pieces + cur_piece;
			piece->skill_count = 0;
			piece->gem_bitfield = 0;
			u32 gem_count = 0;
			while(!token_fit(tok, Token_comma)) {
				kh_assert(!token_fit(tok, Token_end_of_file));
				if(token_fit(tok, Token_pipe)) {
					tok = get_token_and_next(&str_tok, true);
				}
				if(token_fit(tok, Token_open_parenthesis)) {
							// NOTE(flo): parse gem
					tok = get_token_and_next(&str_tok);
					u32 gem_rank = token_to_u32(tok);
					tok = get_token_and_next(&str_tok);
					kh_assert(token_fit(tok, Token_close_parenthesis));
					u32 gem_shift = gem_count * 2;
					u32 gem_add = gem_rank << gem_shift;
					piece->gem_bitfield |= gem_add;
					kh_assert(gem_count < 3);
					gem_count++;
				} else {
							// NOTE(flo): parse skill
					kh_assert(token_fit(tok, Token_word));
					char *name = tok.text;
					umm name_len = tok.text_length; 
					tok = get_token_and_next(&str_tok);
					while(!token_fit(tok, Token_open_parenthesis)) {
						kh_assert(!token_fit(tok, Token_eol) && !token_fit(tok, Token_end_of_file));
						name_len += tok.text_length;
						tok = get_token_and_next(&str_tok);
					}
					kh_assert(piece->skill_count < MAX_SKILL_PER_PIECE);
					SkillRank *skill_rank = piece->skills + piece->skill_count++;
							// NOTE(flo): for now we assume there is a space between skill name and the open parenthesis 
							// 	(ie : Hunger Resistance (1))
					skill_rank->name.assign(name, name_len - 1);

					auto search = app->skills.find(skill_rank->name);
					kh_assert(search != app->skills.end());
					kh_assert(list_allocator_count < list_allocator_max_count);

					PieceList *add = list_allocator + list_allocator_count++;
					add->piece_index.set = mhset_count;
					add->piece_index.type = (PieceType)cur_piece;
					add->next = search->second.piece_head;
					search->second.piece_head = add;

					tok = get_token_and_next(&str_tok);
					kh_assert(token_fit(tok, Token_numeric));
					skill_rank->rank = token_to_u32(tok);
					tok = get_token_and_next(&str_tok);
					kh_assert(token_fit(tok, Token_close_parenthesis));
				}
				tok = get_token_and_next(&str_tok, true);
			}
			parse_state = advance_parse_state(parse_state);
			cur_piece++;
		} else {
			kh_assert(parse_state < MHWParser_count && parse_state >= MHWParser_bonus_2p);
			u32 bonus_index = parse_state - MHWParser_bonus_2p;
			b32 has_bonus = (!token_fit(tok, Token_comma) && !token_fit(tok, Token_eol));
			char *name = tok.text;
			umm name_len = tok.text_length; 
			if(has_bonus) {
				tok = get_token_and_next(&str_tok);
			}
			while(!token_fit(tok, Token_comma) && !token_fit(tok, Token_eol)) {
				kh_assert(!token_fit(tok, Token_end_of_file));
				name_len += tok.text_length;
				tok = get_token_and_next(&str_tok);	
			}
			if(has_bonus) {
				auto search = app->bonuses_per_set.find(mhset_count);
				if(search == app->bonuses_per_set.end()) {
					SetBonuses bonus = {};
					bonus.count = 1;
					bonus.name[0].assign(name, name_len); 
					bonus.needed[0] = bonus_index + 2;
					app->bonuses_per_set.insert(std::make_pair(mhset_count, bonus));
				} else {
					kh_assert(search->second.count < MAX_BONUS_PER_SET);
					u32 index = search->second.count++;
					search->second.name[index].assign(name, name_len);
					search->second.needed[index] = bonus_index + 2;
				}
				std::string bonus_name(name, name_len);
				auto skill_search = app->skills.find(bonus_name);
				kh_assert(skill_search != app->skills.end());
				kh_assert(skill_search->second.max == 1);
			}

			parse_state = advance_parse_state(parse_state);
			if(token_fit(tok, Token_eol)) {
				kh_assert(parse_state == MHWParser_count);
				parse_state = MHWParser_set_name;
				mhset_count++;
			}
		}
		tok = get_token_and_next(&str_tok);
	}
	free(file_contents);
}