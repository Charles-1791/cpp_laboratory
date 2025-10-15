//
// Created by Charles Green on 5/21/25.
//

#ifndef NICE_PRINTER_H
#define NICE_PRINTER_H

#include <iostream>
#include <vector>

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& vec) {
    for(const T& ele: vec) {
        os << ele << "\t";
    }
    os << std::endl;
    return os;
}

#endif //NICE_PRINTER_H
