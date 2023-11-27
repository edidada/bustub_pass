/*
 * @Author: ghost 13038089398@163.com
 * @Date: 2023-09-06 11:46:19
 * @LastEditors: ghost 13038089398@163.com
 * @LastEditTime: 2023-10-06 19:26:22
 * @FilePath: /cmu15445/src/storage/page/b_plus_tree_leaf_page.cpp
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置:
 * https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/page/b_plus_tree_leaf_page.cpp
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <sstream>

#include "common/exception.h"
#include "common/rid.h"
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {

/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * Init method after creating a new leaf page
 * Including set page type, set current size to zero, set next page id and set max size
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::LEAF_PAGE);
  SetSize(0);
  SetMaxSize(max_size);
  SetNextPageId(-1);
}

/**
 * Helper methods to set/get next page id
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetNextPageId() const -> page_id_t { return next_page_id_; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetNextPageId(page_id_t next_page_id) { next_page_id_ = next_page_id; }

/*
 * Helper method to find and return the key associated with input "index"(a.k.a
 * array offset)
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAt(int index) const -> KeyType {
  // replace with your own code
  return array_[index].first;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::ValueAt(int index) const -> ValueType {
  // replace with your own code
  return array_[index].second;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::PairAt(int index) const -> const MappingType & { return array_[index]; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetPairAt(int index, const MappingType &pair) { array_[index] = pair; }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::InsertAt(const KeyType &key, const ValueType &value, int index) -> bool {
  if (GetSize() >= GetMaxSize() || index < 0 || index > GetSize()) {
    return false;
  }
  // insert
  for (int i = GetSize(); i > index; --i) {
    array_[i] = array_[i - 1];
  }
  array_[index] = MappingType(key, value);
  IncreaseSize(1);
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SplitTo(BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *dst,
                                         page_id_t dst_page_id) {
  int end = GetSize();
  int start = end >> 1;
  int dst_begin = 0;
  for (int i = start; i < end; ++i) {
    dst->array_[dst_begin++] = array_[i];
  }
  SetSize(start);
  dst->SetSize(dst_begin);
  dst->SetNextPageId(next_page_id_);
  next_page_id_ = dst_page_id;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::BorrowFromLeft(BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *brother_page_left,
                                                int borrow_cnt) -> bool {
  if (brother_page_left->GetSize() < borrow_cnt + GetMinSize()) {
    return false;
  }
  for (int i = GetMinSize() - 1; i >= borrow_cnt; --i) {
    array_[i] = array_[i - borrow_cnt];
  }
  int borrow_index = brother_page_left->GetSize();
  for (int i = borrow_cnt - 1; i >= 0; --i) {
    array_[i] = brother_page_left->array_[--borrow_index];
  }
  SetSize(GetMinSize());
  brother_page_left->SetSize(borrow_index);
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::BorrowFromRight(
    BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *brother_page_right, int borrow_cnt) -> bool {
  if (brother_page_right->GetSize() < borrow_cnt + GetMinSize()) {
    return false;
  }
  int index = GetSize();
  for (int i = 0; i < borrow_cnt; ++i) {
    array_[index++] = brother_page_right->array_[i];
  }
  int borrow_index = 0;
  for (int i = borrow_cnt; i < brother_page_right->GetSize(); ++i) {
    brother_page_right->array_[borrow_index++] = brother_page_right->array_[i];
  }
  SetSize(index);
  brother_page_right->SetSize(borrow_index);
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MergeToLeft(BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *brother_page_left) {
  int merge_index = brother_page_left->GetSize();
  for (int i = 0; i < GetSize(); ++i) {
    brother_page_left->array_[merge_index++] = array_[i];
  }
  brother_page_left->SetSize(merge_index);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MergeToRight(
    BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *brother_page_right) {
  int merge_index = brother_page_right->GetSize() + GetSize();
  int j = merge_index;
  for (int i = brother_page_right->GetSize() - 1; i >= 0; --i) {
    brother_page_right->array_[--j] = brother_page_right->array_[i];
  }
  for (int i = GetSize(); i >= 0; i--) {
    brother_page_right->array_[--j] = array_[i];
  }
  brother_page_right->SetSize(merge_index);
}

template class BPlusTreeLeafPage<GenericKey<4>, RID, GenericComparator<4>>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTreeLeafPage<GenericKey<16>, RID, GenericComparator<16>>;
template class BPlusTreeLeafPage<GenericKey<32>, RID, GenericComparator<32>>;
template class BPlusTreeLeafPage<GenericKey<64>, RID, GenericComparator<64>>;
}  // namespace bustub
