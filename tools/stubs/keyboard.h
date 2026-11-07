#pragma once
#include <cstdint>
#include <vector>
#define ORBIS_KEYBOARD_MOD_LEFT_SHIFT 2
#define ORBIS_KEYBOARD_MOD_RIGHT_SHIFT 32
#define ORBIS_KEYBOARD_MOD_LEFT_CTRL 1
#define ORBIS_KEYBOARD_MOD_RIGHT_CTRL 16
#define ORBIS_KEYBOARD_MOD_LEFT_ALT 4
#define ORBIS_KEYBOARD_MOD_RIGHT_ALT 64
struct Keyboard { enum Mod { SHIFT, CTRL, ALT }; bool Init(int); bool CheckModifierKey(int); const std::vector<uint16_t>& Keys(); bool Key(int); int Key2Char(int); };
