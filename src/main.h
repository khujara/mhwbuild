struct SkillRank {
	std::string name;
	u32 rank;
};

// NOTE(flo): if one day MAX_SKILL_PER_PIECE is too big use dynamic array or linked list here instead of fixed size array
// atm we do not care since the max is only 2...
#define MAX_SKILL_PER_PIECE 2
#define MAX_DECORATIONS_LEVEL 3
#define MAX_DECORATIONS_PER_LEVEL 16
#define MAX_BONUS_PER_SET 2
struct Piece {
	// NOTE(flo): 00 = no gem -> break, 01 = gem level 1, 10 = gem level 2, 11 = gem level 3 
	u32 gem_bitfield;
	u32 skill_count;
	SkillRank skills[MAX_SKILL_PER_PIECE];
};

enum PieceType {
	PieceType_head_alpha,
	PieceType_chest_alpha,
	PieceType_hands_alpha,
	PieceType_belt_alpha,
	PieceType_legs_alpha,
	PieceType_head_beta,
	PieceType_chest_beta,
	PieceType_hands_beta,
	PieceType_belt_beta,
	PieceType_legs_beta,
	PieceType_count,
};

enum ArmorSlot {
	ArmorSlot_head,
	ArmorSlot_chest,
	ArmorSlot_hands,
	ArmorSlot_belt,
	ArmorSlot_legs,
	ArmorSlot_count,
};

struct MHSet {
	b32 available;
	std::string name;
	Piece pieces[PieceType_count];
};

struct PieceIndex {
	u32 set;
	PieceType type;
};

struct PieceList {
	PieceIndex piece_index;
	PieceList *next;
};

struct CharacterArmor {
	u32 set_type;
	u32 set_index;
	Piece *piece;
};

struct Charm {
	std::string name;
	i32 level;
};

struct Skill {
	u32 max;
	u32 decoration_slot;
	u32 decoration_rarity;
	PieceList *piece_head;
};

struct DecorationLevel {
	u32 count;
	std::string skill_names[MAX_DECORATIONS_PER_LEVEL];
};

struct Character {
	CharacterArmor armors[ArmorSlot_count];
	std::unordered_map<std::string, i32> skills;
	Charm charm;
	DecorationLevel decoration_levels[MAX_DECORATIONS_LEVEL];
	std::unordered_map<std::string, i32> bonuses;
};

struct SetBonuses {
	u32 count;
	std::string name[MAX_BONUS_PER_SET];
	u32 needed[MAX_BONUS_PER_SET];
};

struct ChooseDecoration {
	b32 show;
	u32 level;
	u32 slot_in_level;
};

struct Application {
	Character character;
	std::vector<MHSet> sets;
	std::unordered_map<std::string, u32> sets_map;
	std::unordered_map<std::string, Skill> skills;
	std::unordered_map<u32, SetBonuses> bonuses_per_set;
	ChooseDecoration choose_deco;
};

struct FuzzySkill {
	u32 score;
	std::string name;
	PieceList *first;
};

char *slot_names[ArmorSlot_count] = {
	"Head ", "Chest", "Hands", "Belt ", "Legs ",
};

b32 slot_search[ArmorSlot_count] = {
	true, true, true, true, true,	
};

char *set_types[2] = {
	"Alpha", "Beta",
};

static void unequip_armor(Application *app, u32 slot_index);
static void equip_armor(Application *app, u32 set_index, Piece *piece, PieceType type, u32 slot_index);
static void equip_charm(Character *chara, std::string skill, u32 level);
static void add_skill_rank(Character *chara, std::string skill, u32 rank);
static void substract_skill_rank(Character *chara, std::string skill, u32 rank);