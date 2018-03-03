struct FileResult {
	std::string filename;
	umm size;

	FileResult(std::string p_filename, umm p_size) {
		filename = p_filename;
		size = p_size;
	}
};

struct FilesOfType {
	std::vector<FileResult> files;
};


static FilesOfType 
get_text_files() {
	FilesOfType res;
	WIN32_FIND_DATA find_data;
	HANDLE find = FindFirstFile("*.txt", &find_data);
	if(!find_data.nFileSizeHigh) {
		res.files.emplace_back(find_data.cFileName, find_data.nFileSizeLow);
	}
	const u32 max = 128;
	u32 count = 0;
	while(FindNextFile(find, &find_data) || count >= max) {
		if(!find_data.nFileSizeHigh) {
			res.files.emplace_back(find_data.cFileName, find_data.nFileSizeLow);
			count++;
		}
	}
	return(res);
}

static b32
import_from_file(Application *app, FileResult *file, char *error) { 
	b32 res = true;
	FILE *f_hdl = fopen(file->filename.c_str(), "r");
	char *file_contents = (char *)calloc(1, sizeof(char) * file->size);
	kh_assert(file_contents);
	umm result_size = fread(file_contents, 1, file->size, f_hdl);
	fclose(f_hdl);

	u32 deco_count[3] = {0,0,0};

	enum ImportParser {
		ImportParser_head,
		ImportParser_chest,
		ImportParser_hands,
		ImportParser_belt,
		ImportParser_legs,
		ImportParser_charm,
		ImportParser_decoration_title,
		ImportParser_decoration_1,
		ImportParser_decoration_2,
		ImportParser_decoration_3,
	};

	// NOTE(flo): validation process
	// TODO(flo): add sets decoration per level for each piece  and test if it is consistent with the number of decorations
	// TODO(flo): and test if we have less or equal number of skill for each decoration level
	StringTokenizer str_tok = {file_contents, TokenizerFlags_dont_ignored_white_space};
	Token tok = get_token_and_next(&str_tok);
	u32 parser = ImportParser_head;
	u32 line = 1;
	while(!token_fit(tok, Token_end_of_file) && res && !word_fit(tok, "Skills")) {
		if(parser <= ImportParser_legs) {
			char *name = tok.text;
			umm len = tok.text_length;
			tok = get_token_and_next(&str_tok);
			while(!word_fit(tok, "Alpha") && !word_fit(tok, "Beta")) {
				if(token_fit(tok, Token_end_of_file) || token_fit(tok, Token_eol)) {
					kh_printf(error, "parsing error, line %u", line);
					res = false;
					break;
				}
				len += tok.text_length;
				tok = get_token_and_next(&str_tok);
			}
			if(res) {
				std::string set_name(name, len - 1);
				auto search = app->sets_map.find(set_name);
				if(search == app->sets_map.end()) {
					kh_printf(error, "could not find set, line %u", line);
					res = false;
				} else {
					b32 is_beta = word_fit(tok, "Beta");
					u32 piece_type = parser + (is_beta * ArmorSlot_count);
					kh_assert(piece_type < PieceType_count);
					MHSet *mhset = &app->sets[search->second];
					Piece *piece = mhset->pieces + piece_type;
					u32 gem = piece->gem_bitfield;
					u32 count = 0;
					u32 mask = 3;
					while(gem != 0 && count <= 2) {
						u32 cur_gem = gem & mask;
						deco_count[cur_gem - 1] += 1;	
						gem = gem >> 2;
						count++;
					}
					tok = get_token_and_next(&str_tok);
					parser++;
				}
			}
		}else if(parser == ImportParser_charm) {
			char *name = tok.text;
			umm len = tok.text_length;
			tok = get_token_and_next(&str_tok);
			while(!token_fit(tok, Token_plus)) {
				if(token_fit(tok, Token_end_of_file) || token_fit(tok, Token_eol)) {
					kh_printf(error, "parsing error, line %u", line);
					res = false;
					break;
				}
				len += tok.text_length;
				tok = get_token_and_next(&str_tok);
			}
			if(res) {
				std::string skill_name(name, len - 1);
				auto search = app->skills.find(skill_name);
				if(search == app->skills.end()) {
					kh_printf(error, "could not find skill, line %u", line);
					res = false;
				} else {
					tok = get_token_and_next(&str_tok);
					if(!token_fit(tok, Token_numeric) && token_to_u32(tok) > 3) {
						kh_printf(error, "could not find charm level or charm level invalid, line %u", line);
						res = false;
					}
					parser++;
					tok = get_token_and_next(&str_tok);
				}
			}
		} else if(parser == ImportParser_decoration_title) {
			if(token_fit(tok, Token_eol)) {
				line++;
				tok = get_token_and_next(&str_tok);
				if(word_fit(tok, "Decorations")) {
					while(!token_fit(tok, Token_eol)) {
						if(token_fit(tok, Token_end_of_file)) {
							kh_printf(error, "parsing error missing blank line, line %u", line);
							res = false;
							break;
						}
						tok = get_token_and_next(&str_tok);
					}
					parser++;
				} else {
					kh_printf(error, "parsing error missing \"Decorations\", line %u", line);
					res = false;
				}
			} else {
				kh_printf(error, "parsing error missing blank line, line %u", line);
				res = false;
			}
		} else if(parser <= ImportParser_decoration_3) {
			if(word_fit(tok, "Level")) {
				tok = get_token_and_next(&str_tok, true);
				if(token_fit(tok, Token_numeric)) {
					tok = get_token_and_next(&str_tok, true);
					if(token_fit(tok, Token_open_parenthesis)) {
						tok = get_token_and_next(&str_tok);
						if(token_fit(tok, Token_numeric) && token_to_u32(tok) == deco_count[parser - ImportParser_decoration_1]) {
							tok = get_token_and_next(&str_tok);
							if(token_fit(tok, Token_close_parenthesis)) {
								while(!token_fit(tok, Token_eol)) {
									if(token_fit(tok, Token_end_of_file)) {
										kh_printf(error, "parsing error missing blank line, line %u", line);
										res = false;
										break;
									}
									tok = get_token_and_next(&str_tok);
								}
								if(res) {
									line++;
									tok = get_token_and_next(&str_tok);
									u32 count = 0;
									u32 max_deco = deco_count[parser - ImportParser_decoration_1];
									while(!token_fit(tok, Token_eol)) {
										char *name = tok.text;
										umm len = tok.text_length;
										if(!token_fit(tok, Token_comma)) {
											tok = get_token_and_next(&str_tok);
											while(!token_fit(tok, Token_comma)) {
												if(token_fit(tok, Token_eol) || token_fit(tok, Token_end_of_file)) {
													kh_printf(error, "parsing error in skill decoration, line %u", line);
													res = false;
													break;
												}
												len += tok.text_length;
												tok = get_token_and_next(&str_tok);;
											}
										} else {
											name = 0;
											len = 0;
										}
										if(++count > max_deco) {
											kh_printf(error, "too much skill in decoration level %u, line %u", 
											          parser - ImportParser_decoration_1 + 1, line);
											res = false;
											break;
										}

										std::string skill_name(name, len);
										auto search = app->skills.find(skill_name);
										if(search == app->skills.end() && len > 0) {
											kh_printf(error, "parsing error in skill name, line %u", line);
											res = false;
											break;
										} else {
											tok = get_token_and_next(&str_tok);
											if(!token_fit(tok, Token_space)) {
												kh_printf(error, "parsing error in skill decoration, line %u", line);
												res = false;
												break;
											}
											tok = get_token_and_next(&str_tok);
										}
									}
									parser++;
								}
							} else {
								kh_printf(error, "parsing error missing open paran, line %u", line);
								res = false;
							}
						} else {
							kh_printf(error, "parsing error missing or invalid decoration count, line %u", line);
							res = false;
						}
					} else {
						kh_printf(error, "parsing error missing open paran, line %u", line);
						res = false;
					}
				} else {
					kh_printf(error, "parsing error missing decoration level, line %u", line);
					res = false;
				}
			} else {
				kh_printf(error, "parsing error missing \"Level\", line %u", line);
				res = false;
			}
		}
		if(res) {
			if(token_fit(tok, Token_eol)) {
				line++;
			} else {
				kh_printf(error, "parsing error, line %u", line);
				res = false;
	
			}
		}
		tok = get_token_and_next(&str_tok);
	}

	// NOTE(flo): import process
	if(res) {
		str_tok.pos = file_contents;
		tok = get_token_and_next(&str_tok);
		parser = ImportParser_head;
		for(u32 i = 0; i < ArmorSlot_count; ++i) {
			unequip_armor(app, i);
		}
		for(u32 i = 0; i < array_count(app->character.decoration_levels); ++i) {
			DecorationLevel *deco = app->character.decoration_levels + i;
			kh_assert(deco->count == 0);
		}
		while(!token_fit(tok, Token_end_of_file) && res && !word_fit(tok, "Skills")) {
			if(parser <= ImportParser_legs) {
				char *name = tok.text;
				umm len = tok.text_length;
				tok = get_token_and_next(&str_tok);
				while(!word_fit(tok, "Alpha") && !word_fit(tok, "Beta")) {
					len += tok.text_length;
					tok = get_token_and_next(&str_tok);
				}
				std::string set_name(name, len - 1);
				auto search = app->sets_map.find(set_name);
				b32 is_beta = word_fit(tok, "Beta");
				u32 slot_index = parser;
				kh_assert(slot_index < ArmorSlot_count);
				u32 piece_type = slot_index + (is_beta * ArmorSlot_count);
				kh_assert(piece_type < PieceType_count);
				MHSet *mhset = &app->sets[search->second];
				Piece *piece = mhset->pieces + piece_type;
				equip_armor(app, search->second, piece, (PieceType)piece_type, slot_index);	
				tok = get_token_and_next(&str_tok);
				parser++;
			}else if(parser == ImportParser_charm) {
				char *name = tok.text;
				umm len = tok.text_length;
				tok = get_token_and_next(&str_tok);
				while(!token_fit(tok, Token_plus)) {
					len += tok.text_length;
					tok = get_token_and_next(&str_tok);
				}
				std::string skill_name(name, len - 1);
				auto search = app->skills.find(skill_name);
				tok = get_token_and_next(&str_tok);
				u32 charm_level = token_to_u32(tok);
				equip_charm(&app->character, skill_name, charm_level - 1);	
				parser++;
				tok = get_token_and_next(&str_tok);
			} else if(parser == ImportParser_decoration_title) {
				line++;
				tok = get_token_and_next(&str_tok);
				while(!token_fit(tok, Token_eol)) {
					tok = get_token_and_next(&str_tok);
				}
				parser++;
			} else if(parser <= ImportParser_decoration_3) {
				tok = get_token_and_next(&str_tok, true);
				tok = get_token_and_next(&str_tok, true);
				tok = get_token_and_next(&str_tok);
				u32 dec_count = token_to_u32(tok);
				tok = get_token_and_next(&str_tok);
				while(!token_fit(tok, Token_eol)) {
					tok = get_token_and_next(&str_tok);
				}
				line++;
				tok = get_token_and_next(&str_tok);
				u32 deco_level = parser - ImportParser_decoration_1;
				DecorationLevel *deco = app->character.decoration_levels + deco_level;
				u32 deco_slot = 0;
				while(!token_fit(tok, Token_eol)) {
					char *name = tok.text;
					umm len = tok.text_length;
					if(!token_fit(tok, Token_comma)) {
						tok = get_token_and_next(&str_tok);
						while(!token_fit(tok, Token_comma)) {
							len += tok.text_length;
							tok = get_token_and_next(&str_tok);;
						}
						std::string skill_name(name, len);
						auto search = app->skills.find(skill_name);
						substract_skill_rank(&app->character, deco->skill_names[deco_slot], 1);
						add_skill_rank(&app->character, skill_name, 1);
						deco->skill_names[deco_slot].assign(skill_name);
					}
					deco_slot++;
					tok = get_token_and_next(&str_tok);
					tok = get_token_and_next(&str_tok);
				}
				parser++;
			}
			tok = get_token_and_next(&str_tok);
		}
	}

	free(file_contents);
	return(res);
}


static b32
save_to_file(Application *app, std::string filename) {
	b32 res = false;
	if(filename.size() > 0) {
		char buf[64];
		std::string filebuf;
		for(u32 i = 0; i < array_count(app->character.armors); ++i) {
			if(app->character.armors[i].piece) {
				filebuf.append(app->sets[app->character.armors[i].set_index].name);
				filebuf.append(" ");
				filebuf.append(set_types[app->character.armors[i].set_type]);
			}
			filebuf.append("\n");
		}

		filebuf.append(app->character.charm.name);
		kh_printf(buf, " +%u", app->character.charm.level + 1);
		filebuf.append(buf);

		filebuf.append("\n\nDecorations : \n");
		for(u32 i = 0; i < array_count(app->character.decoration_levels); ++i) {
			DecorationLevel *deco = app->character.decoration_levels + i;
			kh_printf(buf, "Level %u (%u) : ", i + 1, deco->count);
			filebuf.append(buf);
			filebuf.append("\n");
			for(u32 name_i = 0; name_i < deco->count; ++name_i) {
				filebuf.append(deco->skill_names[name_i]);
				filebuf.append(", ");
			}
			filebuf.append("\n");
		}

		filebuf.append("\nSkills : \n");
		for(std::pair<std::string, u32> it : app->character.skills) {
			filebuf.append(it.first);
			kh_printf(buf, " %i", it.second);
			filebuf.append(buf);
			filebuf.append("\n");
		}

		filename.append(".txt");
		FILE *file = fopen(filename.c_str(), "w");
		fwrite(filebuf.c_str(), filebuf.size(), 1, file);
		fclose(file);
	}
	return(res);
}