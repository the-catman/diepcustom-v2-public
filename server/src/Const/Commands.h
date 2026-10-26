#pragma once

#include <string>
#include <vector>

class Client;

void executeCommand(Client* client, const std::string& cmd, const std::vector<std::string>& args);
std::string commandsJson();
