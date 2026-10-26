#pragma once

class CameraEntity;
class Writer;
class Entity;

void compileCreation(CameraEntity* camera, Writer& w, Entity* entity);
void compileUpdate(CameraEntity* camera, Writer& w, Entity* entity);
