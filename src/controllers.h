// UniEmu - mapeamentos de controle (banco de dados SDL + mapeamentos do usuário + genéricos)
#pragma once
#include <SDL.h>
#include <string>

// Carrega gamecontrollerdb.txt e controles.cfg (mapeamentos feitos no assistente).
void controllersLoadMappings();
// Para um joystick sem mapeamento (controle genérico USB/DirectInput), cria um mapeamento padrão
// para que ele funcione como gamepad. Retorna true se o dispositivo passou a ser um GameController.
bool controllersEnsureMapping(int deviceIndex);
// Salva um mapeamento feito pelo usuário (linha no formato SDL) em controles.cfg e aplica.
bool controllersSaveUserMapping(const std::string& mapping);
std::string controllersUserFile();
std::string guidString(SDL_Joystick* js);
