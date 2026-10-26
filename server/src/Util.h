#pragma once
#include <cstdint>

#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Physics/Vector.h"

class ObjectEntity;

namespace util {

double randomFloat();

template <typename... Args>
void log(Args&&... args) {
    std::cout << "[server] ";
    (std::cout << ... << args);
    std::cout << std::endl;
}

template <typename... Args>
void warn(Args&&... args) {
    std::cout << "[server] WARNING: ";
    (std::cout << ... << args);
    std::cout << std::endl;
}

template <typename T>
void removeFast(std::vector<T>& array, std::ptrdiff_t index) {
    if (index < 0 || (size_t)index >= array.size()) throw std::out_of_range("Index out of range. In removeFast");
    if ((size_t)index == array.size() - 1) array.pop_back();
    else { array[index] = array.back(); array.pop_back(); }
}

template <typename T>
void shuffleArray(std::vector<T>& array) {
    for (std::ptrdiff_t i = (std::ptrdiff_t)array.size() - 1; i >= 1; --i) {
        std::ptrdiff_t j = (std::ptrdiff_t)(randomFloat() * (i + 1));
        std::swap(array[i], array[j]);
    }
}

template <typename T>
T randomFrom(const std::vector<T>& array) {
    return array[(size_t)(randomFloat() * array.size())];
}

inline double constrain(double value, double min, double max) {
    return std::max(min, std::min(max, value));
}

constexpr double PI2 = M_PI * 2;

inline double normalizeAngle(double angle) {
    return std::fmod(std::fmod(angle, PI2) + PI2, PI2);
}

std::string truncateUtf8(const std::string& s, size_t maxCodepoints);
std::string trim(const std::string& s);

VectorAbstract getRandomPosition(ObjectEntity* entity);

void saveToLog(const std::string& title, const std::string& description, uint32_t color);
void saveToVLog(const std::string& text);

}
