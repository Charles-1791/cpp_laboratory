//
// Created by Charles Green on 5/28/25.
//

#ifndef RANGE_MODULE_H
#define RANGE_MODULE_H

#include <iostream>
#include <list>
#include <map>

using namespace std;
struct RangeModule {
public:
    RangeModule() {

    }

    void addRange(int left, int right) {
        if(lookup_table_.empty()) {
            ranges_.emplace_back(left, right);
            lookup_table_.emplace(left, --ranges_.end());
            return;
        }
        // check the very last element
        auto strict_larger = lookup_table_.upper_bound(left);
        auto equal_less = strict_larger;
        auto new_it = ranges_.end();
        // handle left first
        if(equal_less == lookup_table_.begin()) {
            new_it = ranges_.emplace(ranges_.begin(), pair<int,int>{left, right});
            lookup_table_.emplace(left, new_it);
        } else {
            --equal_less;
            if(equal_less->second->second < left) {
                //       [------------------]
                // new                         [------------------]
                auto tmp = equal_less->second;
                ++tmp;
                new_it = ranges_.emplace(tmp, pair<int,int>{left, right});
                lookup_table_.emplace(left, new_it);
            } else {
                //       [-------------------]
                // new                   [------------------------]
                new_it = equal_less->second;
                equal_less->second->second = max(equal_less->second->second, right);
            }
        }
        // handle right
        auto next_it = new_it;
        ++next_it;
        while(next_it != ranges_.end() && next_it->first <= new_it->second) {
            //          [----------]  [-----------]            [---------------]
            //    [-----------------------]
            new_it->second = max(new_it->second, next_it->second);
            lookup_table_.erase(next_it->first);
            auto copy = next_it;
            ++next_it;
            ranges_.erase(copy);
        }

    }

    bool queryRange(int left, int right) {
        if(lookup_table_.empty()) {
            return false;
        }
        auto strict_larger = lookup_table_.upper_bound(left);
        if(strict_larger == lookup_table_.begin()) {
            return false;
        }
        auto equal_less = strict_larger;
        --equal_less;
        return equal_less->first <= left && equal_less->second->second >= right;
    }

    void removeRange(int left, int right) {
        if(lookup_table_.empty()) {
            return;
        }
        auto strict_larger = lookup_table_.upper_bound(left);
        auto equal_less = strict_larger;
        if(strict_larger != lookup_table_.begin()) {
            --equal_less;
        } else {
            equal_less = lookup_table_.end();
        }
        auto copy = strict_larger;
        while(copy != lookup_table_.end() && copy->first < right) {
            if(copy->second->second <= right) {
                // delete the whole
                ranges_.erase(copy->second);
                auto tmp = copy;
                ++copy;
                // delete element from a map won't invalidate other iterator, curious
                lookup_table_.erase(tmp);
            } else {
                // partial delete
                auto new_it = ranges_.insert(copy->second, pair<int,int>(right, copy->second->second));
                ranges_.erase(copy->second);
                lookup_table_.erase(copy);
                lookup_table_.emplace(new_it->first, new_it);
                break;
            }
        }
        // handle left
        if(equal_less == lookup_table_.end()) {
            return;
        }
        if(equal_less->second->second <= left) {
            return;
        }
        if(equal_less->first == left) {
            if(equal_less->second->second <= right) {
                ranges_.erase(equal_less->second);
                lookup_table_.erase(equal_less);
            } else {
                int right_bound = equal_less->second->second;
                equal_less->second->first = right;
                auto it = equal_less->second;
                lookup_table_.erase(equal_less);
                lookup_table_.emplace(it->first, it);
            }
            return;
        }
        int right_bound = equal_less->second->second;
        equal_less->second->second = left;
        if(right_bound > right) {
            auto copy = equal_less->second;
            ++copy;
            auto it = ranges_.insert(copy, pair<int,int>(right, right_bound));
            lookup_table_.emplace(it->first, it);
        }
    }
    void clear() {
        ranges_.clear();
        lookup_table_.clear();
    }
    list<pair<int, int>> ranges_;
    map<int, list<pair<int, int>>::iterator> lookup_table_;
};
/**
 * Your RangeModule object will be instantiated and called as such:
 * RangeModule* obj = new RangeModule();
 * obj->addRange(left,right);
 * bool param_2 = obj->queryRange(left,right);
 * obj->removeRange(left,right);
 */

class RangeModule2 {
public:
    RangeModule2() {

    }

    void addRange(int left, int right) {
        if(lookup_table_.empty()) {
            lookup_table_.emplace(left, right);
            return;
        }
        // check the very last element
        auto equal_less = lookup_table_.upper_bound(left);
        auto new_it = lookup_table_.end();
        // handle left first
        if(equal_less == lookup_table_.begin()) {
            auto ret = lookup_table_.insert({left, right});
            new_it = ret.first;
        } else {
            --equal_less;
            if(equal_less->second < left) {
                //       [------------------]
                // new                         [------------------]
                auto ret = lookup_table_.insert({left, right});
                new_it = ret.first;
            } else {
                //       [-------------------]
                // new                   [------------------------]
                new_it = equal_less;
                equal_less->second = max(equal_less->second, right);
            }
        }
        // handle right
        auto next_it = new_it;
        ++next_it;
        while(next_it != lookup_table_.end() && next_it->first <= new_it->second) {
            //          [----------]  [-----------]            [---------------]
            //    [-----------------------]
            new_it->second = max(new_it->second, next_it->second);
            auto copy = next_it;
            ++next_it;
            lookup_table_.erase(copy);
        }
    }

    bool queryRange(int left, int right) {
        if(lookup_table_.empty()) {
            return false;
        }
        auto strict_larger = lookup_table_.upper_bound(left);
        if(strict_larger == lookup_table_.begin()) {
            return false;
        }
        auto equal_less = strict_larger;
        --equal_less;
        return equal_less->first <= left && equal_less->second >= right;
    }

    void removeRight(int right, map<int,int>::iterator strict_larger) {
        while(strict_larger != lookup_table_.end() && strict_larger->first < right) {
            if(strict_larger->second > right) {
                lookup_table_.insert({right, strict_larger->second});
            }
            auto tmp = strict_larger;
            ++strict_larger;
            lookup_table_.erase(tmp);
        }
    }

    void removeLeft(int left, int right, map<int,int>::iterator equal_less) {
        // handle left
        if(equal_less->second <= left) {
            return;
        }
        int right_bound = equal_less->second;
        if(equal_less->first == left) {
            lookup_table_.erase(equal_less);
        } else {
            equal_less->second = left;
        }
        if(right_bound > right) {
            lookup_table_.emplace(right, right_bound);
        }
    }

    void removeRange(int left, int right) {
        if(lookup_table_.empty()) {
            return;
        }
        auto strict_larger = lookup_table_.upper_bound(left);
        auto equal_less = strict_larger;
        if(equal_less != lookup_table_.begin()) {
            removeLeft(left, right, --equal_less);
        }
        removeRight(right, strict_larger);
    }

    map<int, int> lookup_table_;
};




#endif //RANGE_MODULE_H
