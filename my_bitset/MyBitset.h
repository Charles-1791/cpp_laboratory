//
// Created by Charles Green on 5/30/25.
//

#ifndef MYBITSET_H
#define MYBITSET_H
#include <cstddef>
#include <utility>

class MyBitset {
public:
    using uint = unsigned int;
    class Iterator;
    // copy control
    MyBitset();
    ~MyBitset();
    MyBitset(const MyBitset&);
    MyBitset(MyBitset&&);
    MyBitset& operator=(const MyBitset&);
    MyBitset& operator=(MyBitset&&);
    // functions
    void reserve(size_t size);
    void setBit(size_t index);
    void unsetBit(size_t index);
    bool checkBit(size_t index) const;
    void clear();
    size_t size() const;
    size_t capacity() const;
    Iterator begin();
    Iterator end();
protected:
    uint* internal_mem_;
    size_t size_;
    size_t capacity_; // [0, 1, 2, .... capacity - 1]
    friend MyBitset operator+(const MyBitset&, const MyBitset&);
    friend bool operator==(const MyBitset&, const MyBitset&);
    friend class Iterator;
private:
    constexpr static size_t UINT_SIZE = sizeof(uint) * 8;
    std::pair<int, int> getPosition(size_t) const;
};

MyBitset operator+(const MyBitset&, const MyBitset&);
bool operator==(const MyBitset&, const MyBitset&);

class MyBitset::Iterator {
public:
    friend class MyBitset;
    // void unset();
    Iterator(const Iterator&);
    size_t operator*() const;
    Iterator& operator++();
    Iterator operator++(int);
    Iterator& operator--();
    Iterator operator--(int);
private:
    Iterator(MyBitset* mbs): mbs_(mbs) {}
    void setToFirst();
    void advance();
    void retreat();
    MyBitset* mbs_;
    size_t uint_index_;
    size_t offset_in_uint_;
    friend bool operator==(const Iterator&, const Iterator&);
    friend bool operator!=(const Iterator&, const Iterator&);
};

bool operator==(const MyBitset::Iterator&, const MyBitset::Iterator&);
bool operator!=(const MyBitset::Iterator&, const MyBitset::Iterator&);


#endif //MYBITSET_H
