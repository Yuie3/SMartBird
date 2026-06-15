#pragma once

bool hitCircle(float x, float y, float cx, float cy, float r);
bool hitRect(float x, float y, float rx, float ry, float rw, float rh);
int connectFieldAtPoint(float x, float y);
int listIndexAtPoint(float x, float y, int listTop, int count);
bool handlePlayerTouch(bool touching, float x, float y);
bool pressed(unsigned int current, unsigned int previous, unsigned int button);
bool repeatButton(unsigned int current, unsigned int previous, unsigned int button, int* holdFrames);
float analogAxis(unsigned char value);
