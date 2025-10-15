//
// Created by Charles Green on 5/30/25.
//

#include "MyBitset.h"
#include <algorithm>
#include <stdexcept>

MyBitset::MyBitset(): size_(0), capacity_(0), internal_mem_(nullptr) {}

MyBitset::~MyBitset() {
    delete []internal_mem_;
}

MyBitset::MyBitset(const MyBitset& mbs): size_(mbs.size_), capacity_(mbs.capacity_) {
    internal_mem_ = new uint[mbs.capacity_ / UINT_SIZE]();
    if(mbs.internal_mem_) {
      std::copy(mbs.internal_mem_, mbs.internal_mem_+capacity_ / UINT_SIZE, internal_mem_);
    }
}

MyBitset::MyBitset(MyBitset&& mbs): size_(mbs.size_), capacity_(mbs.capacity_) {
    internal_mem_ = mbs.internal_mem_;
    mbs.internal_mem_ = nullptr;
    mbs.capacity_ = 0;
    mbs.size_ = 0;
}

MyBitset &MyBitset::operator=(const MyBitset& mbs) {
    // avoid self-assign
    if(this == &mbs) {
        return *this;
    }
    // free the current memory
    delete []internal_mem_;
    size_ = mbs.size_;
    capacity_ = mbs.capacity_;
    internal_mem_ = new uint[mbs.capacity_ / UINT_SIZE]();
    std::copy(mbs.internal_mem_, mbs.internal_mem_+capacity_/UINT_SIZE, internal_mem_);
    return *this;
}

MyBitset &MyBitset::operator=(MyBitset&& mbs) {
    // avoid self-assign
    if(this == &mbs) {
        return *this;
    }
    // free the current memory
    delete []internal_mem_;
    size_ = mbs.size_;
    mbs.size_ = 0;
    capacity_ = mbs.capacity_;
    mbs.capacity_ = 0;
    internal_mem_ = mbs.internal_mem_;
    mbs.internal_mem_ = nullptr;
    return *this;
}

void MyBitset::reserve(size_t size) {
    if(size < capacity_) {
        return;
    }
    std::pair<int, int> pos = getPosition(size);
    uint* new_mem_ptr = new uint[pos.first+1]();
    if(internal_mem_) {
        std::copy(internal_mem_, internal_mem_+capacity_/UINT_SIZE, new_mem_ptr);
        delete []internal_mem_;
    }
    internal_mem_ = new_mem_ptr;
    capacity_ = (pos.first + 1) * UINT_SIZE;
}


void MyBitset::setBit(size_t index) {
    if(index >= capacity_) {
        reserve(index+1);
    }
    std::pair<int,int> pos = getPosition(index);
    if((internal_mem_[pos.first] & (1<<pos.second)) == 0) {
        internal_mem_[pos.first] |= (1<<pos.second);
        ++size_;
    }
}

void MyBitset::unsetBit(size_t index) {
    if(index >= capacity_) {
        return;
    }
    std::pair<int,int> pos = getPosition(index);
    if((internal_mem_[pos.first] & (1<<pos.second)) != 0) {
        internal_mem_[pos.first] &= ~(1<<pos.second);
        --size_;
    }
}


bool MyBitset::checkBit(size_t index) const {
    if(index >= capacity_) {
        return false;
    }
    std::pair<int, int> pos = getPosition(index);
    return internal_mem_[pos.first] & (1<<pos.second);
}

void MyBitset::clear() {
    if(internal_mem_ == nullptr) {
        return;
    }
    std::fill_n(internal_mem_, capacity_ / UINT_SIZE, 0);
    size_ = 0;
}

size_t MyBitset::size() const {
    return size_;
}

size_t MyBitset::capacity() const {
    return capacity_;
}

// return the index into internal_mem_ and the bit position
std::pair<int, int> MyBitset::getPosition(size_t index) const {
    return {index / UINT_SIZE, index % UINT_SIZE};
}

MyBitset::Iterator MyBitset::begin() {
    Iterator it(this);
    it.setToFirst();
    return it;
}

MyBitset::Iterator MyBitset::end() {
    Iterator it(this);
    it.uint_index_ = capacity_ / UINT_SIZE;
    it.offset_in_uint_ = 0;
    return it;
}

MyBitset operator+(const MyBitset& mbs1, const MyBitset& mbs2) {
    size_t larger_uint_count = std::max<size_t>(mbs1.capacity_, mbs2.capacity_) / MyBitset::UINT_SIZE;
    size_t smaller_uint_count = std::min<size_t>(mbs1.capacity_, mbs2.capacity_) / MyBitset::UINT_SIZE;
    MyBitset::uint* mem_ptr = new MyBitset::uint[larger_uint_count]();
    int i = 0;
    int new_size = 0;
    for(;i<smaller_uint_count; ++i) {
        mem_ptr[i] = mbs1.internal_mem_[i] | mbs2.internal_mem_[i];
        for(int offset = 0; offset < MyBitset::UINT_SIZE; ++offset) {
            int pivot = 1<<offset;
            if(pivot & mem_ptr[i]) {
                ++new_size;
            }
        }
    }
    const MyBitset::uint* larger_intermal_mem = (mbs1.capacity_ >= mbs2.capacity_)? mbs1.internal_mem_ : mbs2.internal_mem_;
    for(;i<larger_uint_count;++i) {
        mem_ptr[i] = larger_intermal_mem[i];
        for(int offset = 0; offset < MyBitset::UINT_SIZE; ++offset) {
            int pivot = 1 << offset;
            if(pivot & mem_ptr[i]) {
                ++new_size;
            }
        }
    }
    MyBitset ret;
    ret.capacity_ = larger_uint_count * MyBitset::UINT_SIZE;
    ret.size_ = new_size;
    ret.internal_mem_ = mem_ptr;
    return ret;
}

bool operator==(const MyBitset& mbs1, const MyBitset& mbs2) {
    if(mbs1.size_ != mbs2.size_) {
        return false;
    }
    size_t larger_uint_count = std::max<size_t>(mbs1.capacity_, mbs2.capacity_) / MyBitset::UINT_SIZE;
    size_t smaller_uint_count = std::min<size_t>(mbs1.capacity_, mbs2.capacity_) / MyBitset::UINT_SIZE;
    int i = 0;
    for(;i<smaller_uint_count;++i) {
        if(mbs1.internal_mem_[i] != mbs2.internal_mem_[i]) {
            return false;
        }
    }
    const MyBitset::uint* larger_intermal_mem = (mbs1.capacity_ >= mbs2.capacity_)? mbs1.internal_mem_ : mbs2.internal_mem_;
    for(;i<larger_uint_count;++i) {
        if(larger_intermal_mem[i] != 0) {
            return false;
        }
    }
    return true;
}

MyBitset::Iterator::Iterator(const MyBitset::Iterator& it):mbs_(it.mbs_), uint_index_(it.uint_index_), offset_in_uint_(it.offset_in_uint_) {}

// void MyBitset::Iterator::unset() {
//     mbs_->internal_mem_[uint_index_] &= ~(1<<offset_in_uint_);
// }

size_t MyBitset::Iterator::operator*() const {
    // if(uint_index_ == mbs_->capacity_ / UINT_SIZE) {
    //     throw std::runtime_error("try to dereference an end iterator");
    // }
    return uint_index_ * UINT_SIZE + offset_in_uint_;
}

MyBitset::Iterator& MyBitset::Iterator::operator++() {
    advance();
    return *this;
}

MyBitset::Iterator MyBitset::Iterator::operator++(int) {
    Iterator current(*this);
    advance();
    return current;
}

MyBitset::Iterator &MyBitset::Iterator::operator--() {
    retreat();
    return *this;
}

MyBitset::Iterator MyBitset::Iterator::operator--(int) {
    Iterator current(*this);
    retreat();
    return current;
}



void MyBitset::Iterator::setToFirst() {
    if(mbs_->size_ == 0) {
        uint_index_ = mbs_->capacity_ / UINT_SIZE;
        offset_in_uint_ = 0;
        return;
    }
    uint_index_ = 0;
    offset_in_uint_ = 0;
    while(uint_index_ < mbs_->capacity_ / UINT_SIZE) {
        while(offset_in_uint_ < UINT_SIZE && (mbs_->internal_mem_[uint_index_] & (1 << offset_in_uint_)) == 0) {
            ++offset_in_uint_;
        }
        if(offset_in_uint_ < UINT_SIZE) {
            return;
        }
        offset_in_uint_ = 0;
        ++uint_index_;
    }
    advance();
}

void MyBitset::Iterator::advance() {
    if (uint_index_ == mbs_->capacity_ / UINT_SIZE) {
        throw std::runtime_error("cannot advance the end iterator");
    }
    ++offset_in_uint_;
    while(uint_index_ < mbs_->capacity_ / UINT_SIZE) {
        while(offset_in_uint_ < UINT_SIZE && (mbs_->internal_mem_[uint_index_] & (1 << offset_in_uint_)) == 0) {
            ++offset_in_uint_;
        }
        if(offset_in_uint_ < UINT_SIZE) {
            return;
        }
        offset_in_uint_ = 0;
        ++uint_index_;
    }
    // if no further element, the two value would be set to end()
}

void MyBitset::Iterator::retreat() {
    if(offset_in_uint_ == 0) {
        if(uint_index_ == 0) {
            throw std::runtime_error("cannot retreat the begin iterator");
        }
        offset_in_uint_ = UINT_SIZE - 1;
        --uint_index_;
    } else {
        --offset_in_uint_;
    }
    int signed_uint_index = uint_index_;
    int signed_offset_in_uint = offset_in_uint_;
    while (signed_uint_index >= 0) {
        while (signed_offset_in_uint >= 0 && (mbs_->internal_mem_[signed_uint_index] & (1 << signed_offset_in_uint)) == 0) {
            --signed_offset_in_uint;
        }
        if(signed_offset_in_uint >= 0) {
            offset_in_uint_ = signed_offset_in_uint;
            uint_index_ = signed_uint_index;
            return;
        }
        signed_offset_in_uint = UINT_SIZE - 1;
        --signed_uint_index;
    }
    // no match
    throw std::runtime_error("cannot retreat the begin iterator");
}

bool operator==(const MyBitset::Iterator& it1, const MyBitset::Iterator& it2) {
    return it1.mbs_ == it2.mbs_ && it1.offset_in_uint_ == it2.offset_in_uint_ && it1.uint_index_ == it2.uint_index_;
}

bool operator!=(const MyBitset::Iterator& it1, const MyBitset::Iterator& it2) {
    return !operator==(it1, it2);
}



