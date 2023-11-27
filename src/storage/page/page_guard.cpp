/*
 * @Author: ghost 13038089398@163.com
 * @Date: 2023-09-06 21:21:03
 * @LastEditors: ghost 13038089398@163.com
 * @LastEditTime: 2023-10-04 16:12:07
 * @FilePath: /cmu15445/src/storage/page/page_guard.cpp
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置:
 * https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#include "storage/page/page_guard.h"
#include "buffer/buffer_pool_manager.h"

namespace bustub {

BasicPageGuard::BasicPageGuard(BasicPageGuard &&that) noexcept {
  bpm_ = that.bpm_;
  page_ = that.page_;
  is_dirty_ = that.is_dirty_;
  that.bpm_ = nullptr;
  that.page_ = nullptr;
  that.is_dirty_ = false;
}

void BasicPageGuard::Drop() {
  if (this->bpm_ != nullptr) {
    bpm_->UnpinPage(PageId(), is_dirty_);
  }
  bpm_ = nullptr;
  page_ = nullptr;
  is_dirty_ = false;
}

auto BasicPageGuard::operator=(BasicPageGuard &&that) noexcept -> BasicPageGuard & {
  if (this->bpm_ != nullptr) {
    bpm_->UnpinPage(PageId(), is_dirty_);
  }
  bpm_ = that.bpm_;
  page_ = that.page_;
  is_dirty_ = that.is_dirty_;
  that.bpm_ = nullptr;
  that.page_ = nullptr;
  that.is_dirty_ = false;
  return *this;
}

BasicPageGuard::~BasicPageGuard() { Drop(); };  // NOLINT

ReadPageGuard::ReadPageGuard(ReadPageGuard &&that) noexcept { this->guard_ = std::move(that.guard_); }

auto ReadPageGuard::operator=(ReadPageGuard &&that) noexcept -> ReadPageGuard & {
  if (this->guard_.page_ != nullptr) {
    this->guard_.page_->RUnlatch();
  }
  this->guard_ = std::move(that.guard_);
  return *this;
}

void ReadPageGuard::Drop() {
  if (this->guard_.page_ != nullptr) {
    this->guard_.page_->RUnlatch();
  }
  this->guard_.Drop();
}

ReadPageGuard::~ReadPageGuard() { Drop(); }  // NOLINT

WritePageGuard::WritePageGuard(WritePageGuard &&that) noexcept { this->guard_ = std::move(that.guard_); }

auto WritePageGuard::operator=(WritePageGuard &&that) noexcept -> WritePageGuard & {
  if (this->guard_.page_ != nullptr) {
    this->guard_.page_->WUnlatch();
  }
  this->guard_ = std::move(that.guard_);
  return *this;
}

void WritePageGuard::Drop() {
  if (this->guard_.page_ != nullptr) {
    this->guard_.page_->WUnlatch();
  }
  this->guard_.Drop();
}

WritePageGuard::~WritePageGuard() { Drop(); }  // NOLINT

}  // namespace bustub
