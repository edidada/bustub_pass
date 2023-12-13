//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.cpp
//
// Identification: src/buffer/lru_k_replacer.cpp
//
// Copyright (c) 2015-2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/lru_k_replacer.h"
#include <cstddef>
#include <cstdio>
#include <mutex>
#include "common/exception.h"

namespace bustub {

LRUKNode::LRUKNode(size_t k, frame_id_t fid, bool is_evictable) : k_(k), fid_(fid), is_evictable_(is_evictable) {}

void LRUKNode::AddHistory(size_t timestamp) {
  if (history_.size() >= k_) {
    history_.pop_back();
  }
  history_.push_front(timestamp);
}

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : replacer_size_(num_frames), k_(k) {}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  // TODO(assassin): finish
  // debug
  // printf("evict\n");
  {
    LockGuard lock_guard(latch_);
    if (curr_size_ <= 0) {
      return false;
    }
    // 在k次以下链表中寻找
    if (!node_list_less_k_.empty()) {
      auto evict_iter = node_list_less_k_.rbegin();
      while (evict_iter != node_list_less_k_.rend() && !evict_iter->GetEvictable()) {
        ++evict_iter;
      }
      if (evict_iter != node_list_less_k_.rend()) {
        auto fid = evict_iter->GetFrameId();
        *frame_id = fid;
        node_store_.erase(fid);
        node_list_less_k_.erase((++evict_iter).base());
        --curr_size_;
        // debug
        // printf("evict frame_id: %d\n", fid);
        return true;
      }
    }
    // 如果k次以下链表不为空，在k次以上的链表中寻找
    if (!node_list_more_k_.empty()) {
      auto evict_iter = node_list_more_k_.begin();
      // find the oldest k history
      for (auto iter = node_list_more_k_.begin(); iter != node_list_more_k_.end(); ++iter) {
        if (iter->GetEvictable() && (iter->GetKHistory() < evict_iter->GetKHistory() || !evict_iter->GetEvictable())) {
          evict_iter = iter;
        }
      }
      if (evict_iter->GetEvictable()) {
        auto fid = evict_iter->GetFrameId();
        *frame_id = fid;
        node_store_.erase(fid);
        node_list_more_k_.erase(evict_iter);
        --curr_size_;
        // debug
        // printf("evict frame_id: %d\n", fid);
        return true;
      }
    }
    // 如果都没有找到，返回false
    // debug
    // printf("evict failed\n");
    return false;
  }
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id, [[maybe_unused]] AccessType access_type) {
  // debug
  // printf("access frame_id: %d\n", frame_id);
  BUSTUB_ASSERT(static_cast<size_t>(frame_id) < replacer_size_, "invalid frame_id in RecordAccess()\n");
  {
    LockGuard lock_guard(latch_);
    RecordAccessLF(frame_id);
  }
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  // debug
  // if (set_evictable) {
  //   printf("set frame_id: %d evictable: true\n", frame_id);
  // } else {
  //   printf("set frame_id: %d evictable: false\n", frame_id);
  // }
  BUSTUB_ASSERT(static_cast<size_t>(frame_id) < replacer_size_, "invalid frame_id in SetEvicetable()");
  {
    LockGuard lock_guard(latch_);
    SetEvictableLF(frame_id, set_evictable);
  }
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  // debug
  // printf("remove frame_id: %d\n", frame_id);
  {
    LockGuard lock_guard(latch_);
    BUSTUB_ASSERT(static_cast<size_t>(frame_id) < replacer_size_, "invalid frame_id in SetEvicetable()");
    // find frame_id in map
    auto found = node_store_.find(frame_id);
    if (found == node_store_.end()) {
      return;
    }
    auto iter = found->second;
    bool evictable = iter->GetEvictable();
    BUSTUB_ASSERT(evictable, "frame is not evictable in Remove()");
    if (!evictable) {
      return;
    }
    // remove from link list
    if (iter->GetHistorySize() >= k_) {
      node_list_more_k_.erase(iter);
    } else {
      node_list_less_k_.erase(iter);
    }
    // remove from node_store_
    node_store_.erase(found);
    --curr_size_;
  }
}

auto LRUKReplacer::Size() -> size_t {
  {
    LockGuard lock_guard(latch_);
    return curr_size_;
  }
}

void LRUKReplacer::RecordAccessLF(frame_id_t frame_id) {
  if (node_store_.find(frame_id) == node_store_.end()) {
    node_list_less_k_.emplace_front(k_, frame_id, false);
    auto iter = node_list_less_k_.begin();
    iter->AddHistory(current_timestamp_++);
    node_store_[frame_id] = iter;
    // 否则更新access time
  } else {
    auto iter = node_store_[frame_id];
    if (iter->GetHistorySize() == k_ - 1) {
      node_list_more_k_.splice(node_list_more_k_.end(), node_list_less_k_, iter);
    }
    iter->AddHistory(current_timestamp_++);
  }
}

void LRUKReplacer::SetEvictableLF(frame_id_t frame_id, bool set_evictable) {
  auto iter = node_store_[frame_id];
  bool evictable = iter->GetEvictable();
  iter->SetEvictable(set_evictable);
  if (evictable ^ set_evictable) {
    curr_size_ = set_evictable ? curr_size_ + 1 : curr_size_ - 1;
  }
}

}  // namespace bustub
