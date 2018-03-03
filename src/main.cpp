#include "kh_types.h"
#include <windows.h>
#include <gl/gl.h>
#include "kh_ogl_ext.h"

#include "ext/imgui.h"
#include "ext/imgui.cpp"
#include "ext/imgui_draw.cpp"

#define STB_SPRINTF_IMPLEMENTATION
#include "ext/stb_sprintf.h"
#define kh_printf stbsp_sprintf

#define FTS_FUZZY_MATCH_IMPLEMENTATION
#include "ext/fts_fuzzy_match.h"


static b32 global_isrunning = false;
#ifdef KH_DEBUG
#define kh_assert(expression) if(!(expression)) { *(int *)0 = 0;}
#else
#define kh_assert(...)
#endif
#define array_count(arr) (sizeof(arr) / sizeof(arr)[0])
#define KH_OFFSETOF(type, member) (umm)&(((type*)0)->member)

#include "ogl_debug.h"
#include "kh_tokenizer.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include "main.h"
#include "intrin.h"
#include "database.h"
#include "importer.h"

#include "database.cpp"

static int 
imgui_filter_save_file_letters(ImGuiTextEditCallbackData* data) { 
	b32 min = ((data->EventChar >= 'a') && (data->EventChar <= 'z')); 
	b32 max = ((data->EventChar >= 'A') && (data->EventChar <= 'Z')); 
	b32 dec = ((data->EventChar >= '0') && (data->EventChar <= '9'));
	b32 und = (data->EventChar == '_');
	if(min || max || dec || und) {
		return(0);
	}
	return(1);
} 

static bool 
imgui_is_any_mouse_button_down() {
    ImGuiIO& io = ImGui::GetIO();
    for (int n = 0; n < ARRAYSIZE(io.MouseDown); n++)
        if (io.MouseDown[n])
            return true;
    return false;
}

LRESULT WINAPI
win32_window_callback(HWND wnd, UINT msg, WPARAM w_param, LPARAM l_param) {
	ImGuiIO& io = ImGui::GetIO();
	LRESULT res = 0;
	switch(msg)	{
		case WM_CLOSE : {
			global_isrunning = false;
		} break;
		case WM_DESTROY : {
			global_isrunning = false;
		} break;
		case WM_QUIT : {
			global_isrunning = false;
		};
		case WM_LBUTTONDOWN :
		case WM_RBUTTONDOWN :
		case WM_MBUTTONDOWN : {
			int button = 0;
			if (msg == WM_LBUTTONDOWN) button = 0;
			if (msg == WM_RBUTTONDOWN) button = 1;
			if (msg == WM_MBUTTONDOWN) button = 2;
			if (!imgui_is_any_mouse_button_down() && GetCapture() == NULL)
				SetCapture(wnd);
			io.MouseDown[button] = true;
		} break;
		case WM_LBUTTONUP :
		case WM_RBUTTONUP :
		case WM_MBUTTONUP : {
			int button = 0;
			if (msg == WM_LBUTTONUP) button = 0;
			if (msg == WM_RBUTTONUP) button = 1;
			if (msg == WM_MBUTTONUP) button = 2;
			io.MouseDown[button] = false;
			if (!imgui_is_any_mouse_button_down() && GetCapture() == wnd)
				ReleaseCapture();
		} break;
		case WM_MOUSEWHEEL : {
			io.MouseWheel += GET_WHEEL_DELTA_WPARAM(w_param) > 0 ? +1.0f : -1.0f;
		} break;
		case WM_MOUSEMOVE : {
			io.MousePos.x = (signed short)(l_param);
			io.MousePos.y = (signed short)(l_param >> 16);
		} break;
		case WM_CHAR : {
			if (w_param > 0 && w_param < 0x10000) {
				io.AddInputCharacter((unsigned short)w_param);
			}
		} break;
		case WM_KEYDOWN :
		case WM_SYSKEYDOWN : {
			if(w_param == VK_ESCAPE) {
				global_isrunning = false;				
			}
			if (w_param < 256) {
				io.KeysDown[w_param] = 1;
			}
		} break;
		case WM_KEYUP :
		case WM_SYSKEYUP : {
			if (w_param < 256) {
				io.KeysDown[w_param] = 0;
			}
		} break;
		default : {
			res = DefWindowProcA(wnd, msg, w_param, l_param);
		} break;
	}
	return(res);
}

inline b32
best_fuzzy_match(FuzzySkill a, FuzzySkill b) {
	b32 res = (a.score > b.score);
	if(a.score == b.score) {
		res = (a.name < b.name);
	}
	return res;
}

static void
add_skill_rank(Character *chara, std::string skill, u32 rank) {
	auto search = chara->skills.find(skill);
	if(search == chara->skills.end()) {
		chara->skills.insert(std::make_pair(skill, rank));
	} else {
		search->second += rank;
	}
}

static void
substract_skill_rank(Character *chara, std::string skill, u32 rank) {
	auto search = chara->skills.find(skill);
	if(search != chara->skills.end()) {
		search->second -= rank;
		kh_assert(search->second >= 0);
		if(search->second == 0) {
			chara->skills.erase(skill);
		}
	} else {
		kh_assert(skill.size() == 0);
	}
}

static void
unequip_armor(Application *app, u32 slot_index) {
	const Piece *armor = app->character.armors[slot_index].piece;
		// TODO(flo): count gem by level!
	if(armor) {
		for(u32 i = 0; i < armor->skill_count; ++i) {
			substract_skill_rank(&app->character, armor->skills[i].name, armor->skills[i].rank);
		}
		u32 gem = armor->gem_bitfield;
		u32 count = 0;
		u32 mask = 3;
		while(gem != 0 && count <= 2) {
			u32 cur_gem = gem & mask;  
			kh_assert(cur_gem - 1 < MAX_DECORATIONS_LEVEL);
			DecorationLevel *deco = app->character.decoration_levels + (cur_gem - 1);
			kh_assert(deco->count > 0);
			std::string name = deco->skill_names[--deco->count];
			substract_skill_rank(&app->character, name, 1);
			gem = gem >> 2;
			count++;
		}
		auto search = app->bonuses_per_set.find(app->character.armors[slot_index].set_index);
		if(search != app->bonuses_per_set.end()) {
			for(u32 i = 0; i < search->second.count; ++i) {
				std::string name = search->second.name[i];
				auto bonus = app->character.bonuses.find(name);
				if(bonus != app->character.bonuses.end()) {
					bonus->second -= 1;	
					kh_assert(bonus->second >= 0);
					if((u32)bonus->second == search->second.needed[i] - 1) {
						substract_skill_rank(&app->character, name, 1);
					}
				}
			}
		}
	}
	app->character.armors[slot_index].set_type = 0;
	app->character.armors[slot_index].set_index = 0;
	app->character.armors[slot_index].piece = 0;
}

static void
equip_armor(Application *app, u32 set_index, Piece *piece, PieceType type, u32 slot_index) {
	unequip_armor(app, slot_index);
	auto bonus_search = app->bonuses_per_set.find(set_index);
	if(bonus_search != app->bonuses_per_set.end()) {
		for(u32 i = 0; i < bonus_search->second.count; ++i) {
			std::string name = bonus_search->second.name[i];
			auto bonus = app->character.bonuses.find(name);
			if(bonus == app->character.bonuses.end()) {
				app->character.bonuses.insert(std::make_pair(name, 1));
			} else {
				bonus->second += 1;	
				if((u32)bonus->second == bonus_search->second.needed[i]) {
					add_skill_rank(&app->character, name, 1);
				}
			}
		}
	}

	for(u32 i = 0; i < piece->skill_count; ++i) {
		std::string name = piece->skills[i].name;
		u32 rank = piece->skills[i].rank;
		add_skill_rank(&app->character, name, rank);
	}
	u32 gem = piece->gem_bitfield;
	u32 count = 0;
	u32 mask = 3;
	while(gem != 0 && count <= 2) {
		u32 cur_gem = gem & mask;  
		kh_assert(cur_gem - 1 < MAX_DECORATIONS_LEVEL);
		kh_assert(app->character.decoration_levels[cur_gem - 1].count < MAX_DECORATIONS_PER_LEVEL)
		DecorationLevel *deco = app->character.decoration_levels + (cur_gem - 1);
		std::string name = deco->skill_names[deco->count++];
		if(name.size() > 0) {
			add_skill_rank(&app->character, name, 1);
		}
		gem = gem >> 2;
		count++;
	}
	kh_assert(gem == 0);
	app->character.armors[slot_index].set_type = (type < PieceType_head_beta) ? 0 : 1;
	app->character.armors[slot_index].set_index = set_index;
	app->character.armors[slot_index].piece = piece;
}

static void
show_piece(Application *app, u32 set_index, Piece *piece, PieceType type, u32 slot_index) {
	kh_assert(slot_index < array_count(slot_names));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
	ImGui::Text(slot_names[slot_index]);
	ImGui::PopStyleColor(1);
	if(piece->skill_count) {
		ImGui::SameLine();
		ImGui::Text("  ");
		ImGui::SameLine();
		ImGui::Text(piece->skills[0].name.c_str());
		ImGui::SameLine();
		ImGui::Text(" +");
		ImGui::SameLine();
		char buff[4];
		kh_printf(buff, "%u", piece->skills[0].rank);
		ImGui::Text(buff);

		if(piece->skill_count == 2) {
			ImGui::SameLine();
			ImGui::SameLine();
			ImVec2 pos = ImGui::GetCursorPos();
			ImGui::SetCursorPos(ImVec2(400.0f, pos.y));
			ImGui::Text(piece->skills[1].name.c_str());
			ImGui::SameLine();
			ImGui::Text(" +");
			ImGui::SameLine();
			kh_printf(buff, "%u", piece->skills[1].rank);
			ImGui::Text(buff);
		}
	}
	if(piece->gem_bitfield) {
		ImGui::SameLine();
		ImVec2 pos = ImGui::GetCursorPos();
		ImGui::SetCursorPos(ImVec2(700.0f, pos.y));
		ImGui::Text(" ");
		u32 gem = piece->gem_bitfield;
		u32 count = 0;
		u32 mask = 3;
		char buf[4];
		while(gem != 0 && count <= 2) {

			ImGui::SameLine();
			u32 cur_gem = gem & mask;  
			kh_printf(buf, "%u  ", cur_gem);
			ImGui::Text(buf);

			gem = gem >> 2;
			count++;
		}
		kh_assert(gem == 0);
	}

	ImGui::SameLine();
	ImGui::PushID(set_index * PieceType_count + type);
	ImVec2 pos = ImGui::GetCursorPos();
	ImGui::SetCursorPos(ImVec2(825.0f, pos.y));
	if(ImGui::Button("equip")) {
		equip_armor(app, set_index, piece, type, slot_index);
	}
	ImGui::PopID();
}

static void
equip_charm(Character *chara, std::string skill, u32 level) {
	substract_skill_rank(chara, chara->charm.name, chara->charm.level + 1);
	add_skill_rank(chara, skill, level + 1);
	chara->charm.name = skill;
	chara->charm.level = level;
}

int CALLBACK
WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmd_line, int cmd_show) {
	WNDCLASS wc = {};
	// wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.style = CS_CLASSDC;
	wc.lpfnWndProc = win32_window_callback;
	wc.hInstance = instance;
	wc.hIcon = LoadIcon(0, IDI_APPLICATION);
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	// wc.lpszMenuName =  MAKEINTRESOURCE(3); 
	wc.lpszMenuName =  NULL; 
	wc.lpszClassName = "MHW Build";
	const int start_w = 1600;
	const int start_h = 900;
	if(RegisterClassA(&wc)) {
		HWND wnd = CreateWindowExA(0, wc.lpszClassName, "MHW Build", 
		                              WS_OVERLAPPEDWINDOW ,
		                              CW_USEDEFAULT, CW_USEDEFAULT, start_w, start_h, 0, 0, wc.hInstance, 0);
		if(wnd) {

			ShowWindow(wnd, SW_SHOWDEFAULT);
			HDC wnd_dc = GetDC(wnd);
			PIXELFORMATDESCRIPTOR pf_desc = {};
			pf_desc.nSize = sizeof(PIXELFORMATDESCRIPTOR);
			pf_desc.nVersion = 1;
			pf_desc.dwFlags = PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
			pf_desc.iPixelType = PFD_TYPE_RGBA;
			pf_desc.cColorBits = 32;
			pf_desc.cAlphaBits = 8;
			pf_desc.iLayerType = PFD_MAIN_PLANE;
			int win32_suggested_pf_ind = ChoosePixelFormat(wnd_dc, &pf_desc);
			PIXELFORMATDESCRIPTOR win32_pf_desc;
			DescribePixelFormat(wnd_dc, win32_suggested_pf_ind, sizeof(win32_pf_desc), &win32_pf_desc);
			SetPixelFormat(wnd_dc, win32_suggested_pf_ind, &pf_desc);

			HGLRC ogl_rc = wglCreateContext(wnd_dc);
			b32 ogl_cur = wglMakeCurrent(wnd_dc, ogl_rc);
			load_gl_extensions();

			Application app = {};

			u32 skill_count = import_skills(&app);
			import_armors(&app);

			ImGuiIO &io = ImGui::GetIO();
			io.FontGlobalScale = 1.0f;
			ImFont *font = io.Fonts->AddFontFromFileTTF("LiberationMono-Regular.ttf", 20.0f);
			u8 *font_pixels;
			i32 font_w, font_h;
			io.Fonts->GetTexDataAsAlpha8(&font_pixels, &font_w, &font_h);
			io.KeyMap[ImGuiKey_Tab] = VK_TAB;
			io.KeyMap[ImGuiKey_LeftArrow] = VK_LEFT;
			io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
			io.KeyMap[ImGuiKey_UpArrow] = VK_UP;
			io.KeyMap[ImGuiKey_DownArrow] = VK_DOWN;
			io.KeyMap[ImGuiKey_PageUp] = VK_PRIOR;
			io.KeyMap[ImGuiKey_PageDown] = VK_NEXT;
			io.KeyMap[ImGuiKey_Home] = VK_HOME;
			io.KeyMap[ImGuiKey_End] = VK_END;
			io.KeyMap[ImGuiKey_Delete] = VK_DELETE;
			io.KeyMap[ImGuiKey_Backspace] = VK_BACK;
			io.KeyMap[ImGuiKey_Enter] = VK_RETURN;
			io.KeyMap[ImGuiKey_Escape] = VK_ESCAPE;
			io.KeyMap[ImGuiKey_A] = 'A';
			io.KeyMap[ImGuiKey_C] = 'C';
			io.KeyMap[ImGuiKey_V] = 'V';
			io.KeyMap[ImGuiKey_X] = 'X';
			io.KeyMap[ImGuiKey_Y] = 'Y';
			io.KeyMap[ImGuiKey_Z] = 'Z';

			char *vert_src = 
				"#version 150 core\n"
				"in vec2 in_pos;\n"
				"in vec2 in_uv;\n"
				"in vec4 in_color;\n"
				"out vec2 out_uv;\n"
				"out vec4 out_color;\n"
				"uniform mat4 proj_mat;\n"
				"void main() {\n"
				"	out_uv = in_uv;\n"
				"	out_color = in_color;\n"
				"	gl_Position = proj_mat * vec4(in_pos, 0.0, 1.0);	\n"
				"}\n";

			char *frag_src = 
				"#version 150 core\n"
				"in vec2 out_uv;\n"
				"in vec4 out_color;\n"
				"out vec4 final_color;\n"
				"uniform sampler2D tex_sampler;\n"
				"void main() {\n"
				"	final_color = out_color * texture(tex_sampler, out_uv).r;\n"
				"}\n";


			GLenum gl_error = 0;
			glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
			glDebugMessageCallback(ogl_debug_callback, 0);

			GLuint prog = glCreateProgram();
			GLuint vert_shader = glCreateShader(GL_VERTEX_SHADER);
			glShaderSource(vert_shader, 1, &vert_src, NULL);
			glCompileShader(vert_shader);
			glAttachShader(prog, vert_shader);
			DEBUG_ogl_get_shader_log(vert_shader);

			GLuint frag_shader = glCreateShader(GL_FRAGMENT_SHADER);
			glShaderSource(frag_shader, 1, &frag_src, NULL);
			glCompileShader(frag_shader);
			glAttachShader(prog, frag_shader);
			DEBUG_ogl_get_shader_log(frag_shader);

			glLinkProgram(prog);
			DEBUG_ogl_get_prog_log(prog);
			glDeleteShader(vert_shader);
			glDeleteShader(frag_shader);

			GLuint tex_loc = glGetUniformLocation(prog, "tex_sampler");
			GLuint projmat_loc = glGetUniformLocation(prog, "proj_mat");
			GLuint pos_loc = glGetAttribLocation(prog, "in_pos");
			GLuint uv_loc = glGetAttribLocation(prog, "in_uv");
			GLuint col_loc = glGetAttribLocation(prog, "in_color");

			GLuint vao;
			glGenVertexArrays(1, &vao);
			glBindVertexArray(vao);

			GLuint vbo, ibo;
			glGenBuffers(1, &vbo);
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glGenBuffers(1, &ibo);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

			glVertexAttribPointer(pos_loc, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (void *)KH_OFFSETOF(ImDrawVert,pos));
			glVertexAttribPointer(uv_loc, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (void *)KH_OFFSETOF(ImDrawVert,uv));
			glVertexAttribPointer(col_loc, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (void *)KH_OFFSETOF(ImDrawVert,col));

			glEnableVertexAttribArray(pos_loc);
			glEnableVertexAttribArray(uv_loc);
			glEnableVertexAttribArray(col_loc);

			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable(GL_CULL_FACE);
			glDisable(GL_DEPTH_TEST);
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
			glEnable(GL_SCISSOR_TEST);

			GLuint font_id;
			glActiveTexture(GL_TEXTURE0);
			glGenTextures(1, &font_id);
			glBindTexture(GL_TEXTURE_2D, font_id);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, font_w, font_h, 0, GL_RED, GL_UNSIGNED_BYTE, font_pixels);

			glUseProgram(prog);


			char skill_buffer[256] = {0};
			char charm_buffer[256] = {0};
			char monster_buffer[256] = {0};
			char input_file_buf[256] = {0};
			char filename[1024] = {0};
			char import_error[1024] = {0};

			std::vector<u32> file_matches;
			std::vector<FuzzySkill> skill_matches(skill_count);
			std::vector<FuzzySkill> charm_matches(skill_count);

			bool show_charm_wnd = false;
			bool show_save_wnd = false;
			bool show_import_wnd = false;

			global_isrunning = true;
			while(global_isrunning) {
				MSG win32_mes;
				for(;;)	{
					BOOL got_mes = PeekMessage(&win32_mes, 0, 0, 0, PM_REMOVE);
					if(!got_mes) break;
					switch(win32_mes.message) {
						case WM_QUIT : {
							global_isrunning = false;
						} break;
						default : {
							TranslateMessage(&win32_mes);
							DispatchMessage(&win32_mes);
						} break;
					}
				}

				RECT client_rect;
				GetClientRect(wnd, &client_rect);
				i32 dim_w = client_rect.right - client_rect.left;
				i32 dim_h = client_rect.bottom - client_rect.top;
				u32 render_w = (u32)dim_w;
				u32 render_h = (u32)dim_h;
				io.DisplaySize = ImVec2((f32)render_w, (f32)render_h);
				io.DeltaTime = 1.0f / 60.0f;
				io.Fonts->TexID = (void *)(umm)font_id;
				io.KeyCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
				io.KeyShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
				io.KeyAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
				bool open = true;

				ImGui::NewFrame();

				// NOTE(flo): SKILL WINDOW
				ImGui::Begin("Search By Skill", &open, ImGuiWindowFlags_ShowBorders|ImGuiWindowFlags_NoTitleBar);
				ImGui::Columns(6, NULL, false);
				for(u32 i = 0; i < ArmorSlot_count; ++i) {
					ImGui::Checkbox(slot_names[i], (bool *)&slot_search[i]);
					ImGui::NextColumn();
				}
				ImGui::Columns(1);
				ImGui::InputText("Search Skill", skill_buffer, sizeof(skill_buffer));
				if(skill_buffer[0]) {
					skill_matches.clear();
					for(std::pair<std::string, Skill> it : app.skills) {
						int score;
						if(fts::fuzzy_match(skill_buffer, it.first.c_str(), score)) {
							FuzzySkill fskill;
							fskill.score = score;
							if(tolower(it.first[0]) == tolower(skill_buffer[0])) {
								fskill.score += 10;
							}
							fskill.name = it.first;
							fskill.first = it.second.piece_head;
							skill_matches.push_back(fskill);
						}	
					}
					std::sort(skill_matches.begin(), skill_matches.end(), best_fuzzy_match);
					for(auto it = skill_matches.begin(); it != skill_matches.end(); ++it) {
						// ImGui::Text(it->name.c_str());
						if(it->first) {
							if(ImGui::CollapsingHeader(it->name.c_str())) {
								u32 old_type = 0;
								std::string old_string("");
								for(PieceList *plist = it->first; plist; plist = plist->next) {
									if(app.sets[plist->piece_index.set].available) {
										kh_assert(plist->piece_index.type < PieceType_count);
										std::string set_name = app.sets[plist->piece_index.set].name;
										Piece *piece = app.sets[plist->piece_index.set].pieces + plist->piece_index.type;
										u32 piece_index = plist->piece_index.type;
										u32 cur_type = 0;
										if(plist->piece_index.type >= PieceType_head_beta) {
											piece_index = piece_index - (u32)PieceType_head_beta;
											cur_type = 1;
										}

										if(slot_search[piece_index]) {
											if(old_string.compare(set_name) != 0 || old_type != cur_type) {
												ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f,0.6f,1.0f,1));
												ImGui::Text("[");
												ImGui::SameLine();
												ImGui::Text(set_name.c_str());
												ImGui::SameLine();
												ImGui::Text(set_types[cur_type]);
												ImGui::SameLine();
												ImGui::Text("]");
												ImGui::PopStyleColor(1);
											}
											old_type = cur_type;
											old_string = set_name;
											show_piece(&app, plist->piece_index.set, piece, plist->piece_index.type, piece_index);
										}
									}
								}
							}
						}
					}
				}
				ImGui::End();

				// NOTE(flo): ARMOR WINDOW
				ImGui::Begin("Search by Set", &open, ImGuiWindowFlags_ShowBorders);
				ImGui::InputText("Search Set", monster_buffer, sizeof(monster_buffer));
				u32 set_count = 0;
				for(auto it = app.sets.begin(); it != app.sets.end(); ++it) {
					if(it->available) {
						if(fts::fuzzy_match_simple(monster_buffer, it->name.c_str())) {
						// ImGui::Text(it->name.c_str());
							if(ImGui::CollapsingHeader(it->name.c_str())) {
								ImGui::PushID(it->name.c_str());
								if(ImGui::TreeNode("Alpha")) {
									for(u32 i = (u32)PieceType_head_alpha; i < (u32)PieceType_head_beta; ++i) {
										Piece *piece = it->pieces + i;
										show_piece(&app, set_count, piece,(PieceType)i, i);
									}
									ImGui::TreePop();	
								}
								if(ImGui::TreeNode("Beta")) {
									for(u32 i = (u32)PieceType_head_beta; i < (u32)PieceType_count; ++i) {
										Piece *piece = it->pieces + i;
										show_piece(&app, set_count, piece,(PieceType)i, i - PieceType_head_beta);
									}
									ImGui::TreePop();	
								}
								ImGui::PopID();
							}
						}		
					}
					set_count++;
				}
				ImGui::End();

				// NOTE(flo): SAVE WINDOW
				if(show_save_wnd) {
					ImGui::Begin("Save Window", &open, ImGuiWindowFlags_ShowBorders|ImGuiWindowFlags_NoTitleBar); 
					ImGui::InputText("file name", filename, sizeof(filename), ImGuiInputTextFlags_CallbackCharFilter, 
					                 imgui_filter_save_file_letters);
					if(ImGui::Button("Save")) {
						std::string str(filename);	
						if(save_to_file(&app, str)) {
							show_save_wnd = false;
						}
					}
					ImGui::SameLine();
					if(ImGui::Button("Cancel")) {
						show_save_wnd = false;

					}
					ImGui::End();
				}

				// NOTE(flo): IMPORT WINDOW
				if(show_import_wnd) {
					ImGui::Begin("Import Window", &show_import_wnd, ImGuiWindowFlags_ShowBorders); 
					FilesOfType res = get_text_files();
					ImGui::InputText("Files", input_file_buf, sizeof(input_file_buf));
					ImGui::Text(import_error);
					file_matches.clear();	
					for(auto it = res.files.begin(); it != res.files.end(); ++it) {
						if(fts::fuzzy_match_simple(input_file_buf, it->filename.c_str())) {
							file_matches.push_back(it - res.files.begin());
						}	
					}
					for(auto it = file_matches.begin(); it != file_matches.end(); ++it) {
						FileResult *file = &res.files[*it];
						if(ImGui::Button(file->filename.c_str())) {
							b32 import = import_from_file(&app, file, import_error);	
							if(import) {
								kh_printf(import_error, "");
								show_import_wnd = false;
							}
						}
					}
					ImGui::End();
				}

				// NOTE(flo): CHARM WINDOW
				if(show_charm_wnd) {
					ImGui::Begin("Charm Window", &show_charm_wnd, ImGuiWindowFlags_ShowBorders);
					ImGui::InputText("Search Charm", charm_buffer, sizeof(charm_buffer));
					if(charm_buffer[0]) {
						charm_matches.clear();
						for(std::pair<std::string, Skill> it : app.skills) {
							int score;
							if(fts::fuzzy_match(charm_buffer, it.first.c_str(), score)) {
								FuzzySkill fskill;
								fskill.score = score;
								if(tolower(it.first[0]) == tolower(charm_buffer[0])) {
									fskill.score += 10;
								}
								fskill.name = it.first;
								fskill.first = it.second.piece_head;
								charm_matches.push_back(fskill);
							}	
						}
						std::sort(charm_matches.begin(), charm_matches.end(), best_fuzzy_match);
						for(auto it = charm_matches.begin(); it != charm_matches.end(); ++it) {
							if(ImGui::Button(it->name.c_str())) {
								equip_charm(&app.character, it->name, app.character.charm.level);
								ZeroMemory(charm_buffer, sizeof(charm_buffer));
								show_charm_wnd = false;
							}
						}
					}
					ImGui::End();
				}

				// NOTE(flo): DECORATION WINDOW
				if(app.choose_deco.show) {
					ImGui::Begin("Decoration window", (bool *)&app.choose_deco.show, ImGuiWindowFlags_ShowBorders);
					ImGui::Columns(4, NULL, false);
					std::vector<std::string> deco_skills;
					for(std::pair<std::string, Skill> it : app.skills) {
						if(it.second.decoration_slot > 0 && it.second.decoration_slot <= app.choose_deco.level + 1) {
							deco_skills.emplace_back(it.first);
						}
					}
					std::sort(deco_skills.begin(), deco_skills.end());
					for(auto it = deco_skills.begin(); it != deco_skills.end(); ++it) {
						char buf[64];
						auto search = app.skills.find(*it);
						kh_assert(search != app.skills.end());
						kh_printf(buf, "%s (R%u)", it->c_str(), search->second.decoration_rarity);
						if(ImGui::Button(buf)) {
							DecorationLevel *deco = app.character.decoration_levels + app.choose_deco.level;
							std::string old_name = deco->skill_names[app.choose_deco.slot_in_level];

							substract_skill_rank(&app.character, old_name, 1);
							add_skill_rank(&app.character, *it, 1);
							app.choose_deco.show = false;
							deco->skill_names[app.choose_deco.slot_in_level].assign(*it);
						}
						ImGui::NextColumn();
					}
					ImGui::Columns(1);
					ImGui::End();
				}

				// NOTE(flo): CHARACTER WINDOW
				ImGui::Begin("Character Window", &open, ImGuiWindowFlags_ShowBorders|ImGuiWindowFlags_MenuBar|
				             ImGuiWindowFlags_NoTitleBar);
				char buf[8];
				if(ImGui::BeginMenuBar()) {
					if(ImGui::BeginMenu("File")) {
						if(ImGui::MenuItem("Save")) {
							show_save_wnd = true;	
						}
						if(ImGui::MenuItem("Import")) {
							kh_printf(import_error, "");
							show_import_wnd = true;
						}
						ImGui::EndMenu();	
					}
					ImGui::EndMenuBar();
				}
				if(ImGui::CollapsingHeader("Armor")) {
					for(u32 i = 0; i < array_count(app.character.armors); ++i) {
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
						ImGui::Text(slot_names[i]);
						ImGui::PopStyleColor(1);

						if(app.character.armors[i].piece) {
							ImGui::PushID(i);
							ImGui::SameLine();
							if(ImGui::Button("unequip")) {
								unequip_armor(&app, i);
							}
							ImGui::PopID();
							if(app.character.armors[i].piece) {
								ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f,0.6f,1.0f,1));
								ImGui::Text("[");
								ImGui::SameLine();
								ImGui::Text(app.sets[app.character.armors[i].set_index].name.c_str());
								ImGui::SameLine();
								ImGui::Text(set_types[app.character.armors[i].set_type]);
								ImGui::SameLine();
								ImGui::Text("]");
								ImGui::PopStyleColor(1);
								const Piece *piece = app.character.armors[i].piece;
								for(u32 piece_i = 0; piece_i < piece->skill_count; ++piece_i) {
									const SkillRank *skill = piece->skills + piece_i;
									ImGui::Text("  ");
									ImGui::SameLine();
									ImGui::Text(skill->name.c_str());
									kh_printf(buf, " +%u", skill->rank);
									ImGui::SameLine();
									ImGui::Text(buf);
								}
								if(piece->gem_bitfield) {
									u32 gem = piece->gem_bitfield;
									u32 count = 0;
									u32 mask = 3;
									while(gem != 0 && count <= 2) {
										u32 cur_gem = gem & mask;  
										ImGui::Text("  ");
										ImGui::SameLine();
										ImGui::Text("Decorations");
										kh_printf(buf, "#%u: ", count);
										ImGui::SameLine();
										ImGui::Text(buf);
										ImGui::SameLine();
										kh_printf(buf, "%u  ", cur_gem);
										ImGui::Text(buf);
										gem = gem >> 2;
										count++;
									}
									kh_assert(gem == 0);
								}
							}
						} else {
						}
					}
				}
				if(ImGui::CollapsingHeader("Charm")) {
					ImGui::Text(app.character.charm.name.c_str());
					ImGui::SameLine();
					if(ImGui::Button("Choose")) {
						show_charm_wnd = true;
					}
					const char *levels[] = {
						"1", "2", "3",
					};
					int old_level = app.character.charm.level;
					ImGui::Combo("Level", &app.character.charm.level, levels, array_count(levels));
					if(app.character.charm.level != old_level) {
						auto search = app.character.skills.find(app.character.charm.name);
						if(search != app.character.skills.end()) {
							search->second -= old_level + 1;
							search->second += app.character.charm.level + 1;
							kh_assert(search->second > 0);
						}
					}
					if(ImGui::Button("Remove")) {
						auto search = app.character.skills.find(app.character.charm.name);
						if(search != app.character.skills.end()) {
							search->second -= app.character.charm.level + 1;
							if(search->second == 0) {
								app.character.skills.erase(app.character.charm.name);
							}
						}
						app.character.charm.name = "";
					}
				}
				if(ImGui::CollapsingHeader("Decorations")) {
					for(u32 lvl_i = 0; lvl_i < array_count(app.character.decoration_levels); ++lvl_i) {
						ImGui::Text("Level");
						kh_printf(buf, "%u: ", lvl_i + 1);
						ImGui::SameLine();
						ImGui::Text(buf);
						DecorationLevel *deco = app.character.decoration_levels + lvl_i; 
						u32 count = deco->count;
						kh_assert(count < MAX_DECORATIONS_PER_LEVEL);
						ImGui::Columns(4, NULL, false);
						for(u32 deco_i = 0; deco_i < count; ++deco_i) {
							ImGui::PushID(lvl_i * MAX_DECORATIONS_PER_LEVEL + deco_i);	
							if(ImGui::Button(deco->skill_names[deco_i].c_str())) {
								app.choose_deco.show = true;
								app.choose_deco.level = lvl_i;
								app.choose_deco.slot_in_level = deco_i;
							}
							ImGui::PopID();
							ImGui::NextColumn();
						}
						ImGui::Columns(1);
						ImGui::Text("");
					}
				}

				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
				ImGui::Text("Skills : ");
				ImGui::PopStyleColor(1);
				for(std::pair<std::string, u32> it : app.character.skills) {
					auto search = app.skills.find(it.first);
					kh_assert(search != app.skills.end());
					u32 count = it.second;
					if(it.second > search->second.max) {
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
						count = search->second.max;
					}
					ImGui::Text("  ");
					ImGui::SameLine();
					ImGui::Text(it.first.c_str());
					kh_printf(buf, " %i", count);
					ImGui::SameLine();
					ImGui::Text(buf);
					if(it.second > search->second.max) {
						ImGui::PopStyleColor(1);
					}

				}
				ImGui::End();

				// NOTE(flo): AVAILABLE ARMOR WINDOW
				ImGui::Begin("Available Sets", &open, ImGuiWindowFlags_ShowBorders);
				ImGui::Columns(5, NULL, false);
				for(auto it = app.sets.begin(); it != app.sets.end(); ++it) {
					if(ImGui::Selectable(it->name.c_str(), it->available)) {
						it->available = !it->available;
					}
					ImGui::NextColumn();
				}
				ImGui::Columns(1);
				ImGui::End();

				ImGui::Render();
				ImDrawData *draw_data = ImGui::GetDrawData(); 
				glViewport(0, 0, render_w, render_h);
				glClearColor(0.25f, 0.25f, 0.25f, 1.0f);
				glClear(GL_COLOR_BUFFER_BIT);
				glEnable(GL_SCISSOR_TEST);
				int fb_width = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
				int fb_height = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
				if(draw_data) {
					const float ortho_projection[4][4] = {
						{ 2.0f/render_w, 	0.0f,                   	0.0f, 0.0f },
						{ 0.0f,                  	2.0f/-(f32)render_h, 	0.0f, 0.0f },
						{ 0.0f,                  	0.0f,                       -1.0f, 0.0f },
						{-1.0f,                  	1.0f,                   	0.0f, 1.0f },
					};
					glUniformMatrix4fv(projmat_loc, 1, GL_FALSE, (GLfloat *)ortho_projection);
					for(i32 i = 0; i < draw_data->CmdListsCount; ++i) {
						const ImDrawList *cmd_list = draw_data->CmdLists[i];
						const ImDrawIdx *idx_buffer_offset = 0;
						glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), 
						             (const GLvoid *)cmd_list->VtxBuffer.Data, GL_STREAM_DRAW);
						glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx), 
						             (const GLvoid*)cmd_list->IdxBuffer.Data, GL_STREAM_DRAW);
						for(i32 cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; ++cmd_i) {
							const ImDrawCmd *pcmd = &cmd_list->CmdBuffer[cmd_i];
							glBindTexture(GL_TEXTURE_2D, (GLuint)(umm)pcmd->TextureId);
							glScissor((int)pcmd->ClipRect.x, (int)(fb_height - pcmd->ClipRect.w), (int)(pcmd->ClipRect.z - pcmd->ClipRect.x), (int)(pcmd->ClipRect.w - pcmd->ClipRect.y));
							glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, idx_buffer_offset);
							idx_buffer_offset += pcmd->ElemCount;
						}
					}
				}
				glDisable(GL_SCISSOR_TEST);
				SwapBuffers(wnd_dc);
			}
			ReleaseDC(wnd, wnd_dc);
		}
	}
	return(0);
}