//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager.h"
#include <cstdio>

#include "common/config.h"
#include "common/exception.h"
#include "common/macros.h"
#include "storage/page/page.h"
#include "storage/page/page_guard.h"

namespace bustub {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager, size_t replacer_k,
                                     LogManager *log_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager), log_manager_(log_manager) {
  // TODO(students): remove this line after you have implemented the buffer pool manager
  // throw NotImplementedException(
  //     "BufferPoolManager is not implemented yet. If you have finished implementing BPM, please remove the throw "
  //     "exception line in `buffer_pool_manager.cpp`.");

  // we allocate a consecutive memory space for the buffer pool
  pages_ = new Page[pool_size_];
  replacer_ = std::make_unique<LRUKReplacer>(pool_size, replacer_k);

  // Initially, every page is in the free list.
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<int>(i));
  }
}

BufferPoolManager::~BufferPoolManager() { delete[] pages_; }

auto BufferPoolManager::NewPage(page_id_t *page_id) -> Page * {
  // 1.get a frame and record access (pinned default)
  // no other thread can access this frame
  frame_id_t frame_id = -1;
  page_id_t page_id_new = -1;
  {
    LockGuard guard(latch_);
    if (!GetFreeFrameLF(&frame_id)) {
      *page_id = -1;
      return nullptr;
    }
    replacer_->RecordAccess(frame_id);
    replacer_->SetEvictable(frame_id, false);

    // 2.get new page id
    page_id_new = AllocatePage();

    // debug
    BUSTUB_ASSERT(frame_id != -1, "error, allocated frame id is -1");
    BUSTUB_ASSERT(page_id_new != -1, "error, allocated page id is -1");

    // 3.reset alloc_page
    auto alloc_page = pages_ + frame_id;
    ResetPage(alloc_page, page_id_new, 1, false, true);
    // 4. insert page in page_table_
    page_table_.insert({page_id_new, frame_id});

    *page_id = page_id_new;
    // debug 
    printf("new page_id: %d\n", page_id_new);
    return alloc_page;
  }
}

auto BufferPoolManager::FetchPage(page_id_t page_id, [[maybe_unused]] AccessType access_type) -> Page * {
  // debug
  printf("fetch page_id: %d\n", page_id);
  // find in page_table_
  frame_id_t frame_id = -1;
  Page *alloc_page = nullptr;
  {
    LockGuard guard(latch_);
    // if page_id is already in page_table_, just add pin count and return
    auto found = page_table_.find(page_id);
    if (found != page_table_.end()) {
      frame_id = found->second;
      alloc_page = pages_ + frame_id;
      ++alloc_page->pin_count_;
      replacer_->RecordAccess(frame_id);
      replacer_->SetEvictable(frame_id, false);
      return alloc_page;
    }

    // if page_id is not in page_table_, get a free frame
    // no other thread can access this frame
    if (!GetFreeFrameLF(&frame_id)) {
      return alloc_page;
    }
    replacer_->RecordAccess(frame_id);
    replacer_->SetEvictable(frame_id, false);

    // reset page_id page
    alloc_page = pages_ + frame_id;
    ResetPage(alloc_page, page_id, 1, false, false);

    page_table_.insert({page_id, frame_id});

    return alloc_page;
  }
}

auto BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty, [[maybe_unused]] AccessType access_type) -> bool {
  // debug
  int old_pin_count = 0;
  bool old_is_dirty = false;
  printf("unpin page_id: %d, is_dirty: %d\n", page_id, static_cast<int>(is_dirty));

  {
    LockGuard guard(latch_);
    auto found = page_table_.find(page_id);
    if (found == page_table_.end()) {
      printf("\tunpin page_id: %d not found\n", page_id);
      return false;
    }
    frame_id_t frame_id = found->second;
    Page *page = pages_ + frame_id;
    // debug
    old_pin_count = page->pin_count_;
    old_is_dirty = page->is_dirty_;
    printf("\tunpin page_id: %d found [pin_count: %d, is_dirty: %d]\n", page_id, old_pin_count, static_cast<int>(old_is_dirty));

    if (page->pin_count_ == 0) {
      printf("\tunpin page_id: %d reunpinned\n", page_id);
      return false;
    }
    --(page->pin_count_);
    page->is_dirty_ |= is_dirty;
    if (page->pin_count_ == 0) {
      replacer_->SetEvictable(frame_id, true);
    }
    // debug
    printf("\t\t-> [pin_count: %d, is_dirty: %d]\n", page->pin_count_, static_cast<int>(page->IsDirty()));
  }
  return true;
}

auto BufferPoolManager::FlushPage(page_id_t page_id) -> bool {
  {
    LockGuard guard(latch_);
    auto found = page_table_.find(page_id);
    if (found == page_table_.end()) {
      return false;
    }
    frame_id_t frame_id = found->second;
    Page* flush_page = pages_ + frame_id;
    disk_manager_->WritePage(page_id, flush_page->GetData());
    flush_page->is_dirty_ = false;
    return true;
  }
}

void BufferPoolManager::FlushAllPages() {
  {
    LockGuard guard(latch_);
    for (size_t i = 0; i < pool_size_; ++i) {
      Page *page = pages_ + i;
      disk_manager_->WritePage(page->GetPageId(), page->GetData());
      page->is_dirty_ = false;
    }
  }
}

auto BufferPoolManager::DeletePage(page_id_t page_id) -> bool {
  // debug
  printf("delete page_id: %d\n", page_id);
  {
    LockGuard guard(latch_);
    auto found = page_table_.find(page_id);
    if (found == page_table_.end()) {
      return true;
    }
    frame_id_t frame_id = found->second;
    Page *page = pages_ + frame_id;
    if (page->GetPinCount() > 0) {
      return false;
    }
    page_table_.erase(page_id);
    replacer_->Remove(frame_id);
    free_list_.push_back(frame_id);
  }
  return true;
}

auto BufferPoolManager::AllocatePage() -> page_id_t { return next_page_id_++; }

auto BufferPoolManager::FetchPageBasic(page_id_t page_id) -> BasicPageGuard {
  Page *page = FetchPage(page_id);
  return {this, page};
}

auto BufferPoolManager::FetchPageRead(page_id_t page_id) -> ReadPageGuard {
  Page *page = FetchPage(page_id);
  page->RLatch();
  // debug
  // printf("fetch page_id: %d for read [pin_count: %d]\n", page_id, page->pin_count_);
  return {this, page};
}

auto BufferPoolManager::FetchPageWrite(page_id_t page_id) -> WritePageGuard {
  Page *page = FetchPage(page_id);
  page->WLatch();
  // debug
  // printf("fetch page_id: %d for write [pin_count: %d]\n", page_id, page->pin_count_);
  return {this, page};
}

auto BufferPoolManager::NewPageGuarded(page_id_t *page_id) -> BasicPageGuard {
  Page *page = NewPage(page_id);
  return {this, page};
}

auto BufferPoolManager::GetFreeFrameLF(frame_id_t *frame_id) -> bool {
  bool success = false;
  // find a free frame in freelist
  if (!free_list_.empty()) {
    *frame_id = free_list_.front();
    free_list_.pop_front();
    success = true;
    // debug
    // printf("get free frame_id: %d\n", *frame_id);
  } else {
    // evict a frame in replacer
    success = replacer_->Evict(frame_id);
    if (success) {
      Page *evict_page = pages_ + *frame_id;
      BUSTUB_ASSERT(evict_page->GetPinCount() == 0, "error, allocated page pin count is not 0");
      // remove page from page_table_
      page_table_.erase(evict_page->GetPageId());
      // debug
      // printf("get evict frame_id: %d (evict page %d)\n", *frame_id, evict_page->GetPageId());
    }
  }
  return success;
}

auto BufferPoolManager::ResetPage(Page *page, page_id_t page_id, int pin_count, bool is_dirty, bool is_new) -> void {
  // write back dirty page
  if (page->IsDirty()) {
    disk_manager_->WritePage(page->GetPageId(), page->GetData());
  }
  // reset memory and metadata
  page->page_id_ = page_id;
  page->pin_count_ = pin_count;
  page->is_dirty_ = is_dirty;
  if (is_new) {
    page->ResetMemory();
  } else {
    disk_manager_->ReadPage(page_id, page->GetData());
  }
}

}  // namespace bustub
