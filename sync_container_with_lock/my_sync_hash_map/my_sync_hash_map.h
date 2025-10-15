//
// Created by Charles Green on 6/28/25.
//

#ifndef MY_SYNC_HASH_MAP_H
#define MY_SYNC_HASH_MAP_H
#include <vector>
#include <list>
#include <algorithm>
#include "../my_sync_forward_list/my_sync_forward_list.h"




// a sync map should not expose reference to a value
template <typename KT, typename VT, typename Hash=std::hash<KT>>
class MySyncHashMap {
    struct KVPair  {
        KT key_;
        std::unique_ptr<VT> value_;
        KVPair() = default;
        template <typename KK, typename VV>
        KVPair(KK&& key, VV&& value): key_(std::forward<KK>(key)), value_(std::make_unique<VT>(std::forward<VV>(value))) {}
        KVPair(const KVPair&) = delete;
        KVPair(KVPair&&) = default;
        KVPair& operator=(const KVPair&) = delete;
        KVPair& operator=(KVPair&&) noexcept = default;
    };


    std::vector<std::unique_ptr<MySyncForwardList<KVPair>>> buckets_;
    const int BUCKET_NUM;
    Hash hasher_;
    MySyncForwardList<KVPair>& getBucket(const KT& key) {
        size_t idx = hasher_(key) % buckets_.size();
        return *buckets_[idx];
    }
public:
    MySyncHashMap(size_t bucket_num = 19, const Hash& hasher = Hash()) : BUCKET_NUM(bucket_num), hasher_(hasher),
    buckets_(std::vector<std::unique_ptr<MySyncForwardList<KVPair>>>(bucket_num)) {
        for(int i = 0; i<bucket_num; ++i) {
            buckets_[i] = std::make_unique<MySyncForwardList<KVPair>>();
        }
    }

    MySyncHashMap(const MySyncHashMap&) = delete;
    MySyncHashMap& operator=(const MySyncHashMap&) = delete;

    bool getValue(const KT& key, VT& placeholder) {
        MySyncForwardList<KVPair>& bkt = getBucket(key);
        std::shared_ptr<KVPair> ret = bkt.find_first_if([&key](const KVPair& p) {
            return p.key_ == key;
        });
        if(ret == nullptr) {
            return false;
        }
        placeholder = *ret->value_;
        return true;
    }

    template<typename KK, typename VV>
        bool insertOrUpdate(KK&& key, VV&& value) {
        MySyncForwardList<KVPair>& bkt = getBucket(key);
        return bkt.insert_or_update([&key](const KVPair& p) {
            return key == p.key_;
        }, KVPair(std::forward<KK>(key), std::forward<VV>(value)));
    }

    // true if key exists
    bool eraseEntry(const KT& key) {
        MySyncForwardList<KVPair>& bkt = getBucket(key);
        return bkt.remove_first_if([&key](const KVPair& p) {
            return key == p.key_;
        });
    }


};

#endif //MY_SYNC_HASH_MAP_H
