/*
 * @Author: ghost 13038089398@163.com
 * @Date: 2023-09-06 11:30:35
 * @LastEditors: ghost 13038089398@163.com
 * @LastEditTime: 2023-10-06 19:11:36
 * @FilePath: /cmu15445/src/storage/page/b_plus_tree_internal_page.cpp
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置:
 * https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/page/b_plus_tree_internal_page.cpp
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <sstream>

#include "common/exception.h"
#include "storage/page/b_plus_tree_internal_page.h"

namespace bustub {
/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/
/*
 * Init method after creating a new internal page
 * Including set page type, set current size, and set max page size
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::INTERNAL_PAGE);
  SetSize(1);
  SetMaxSize(max_size);
}

/*
 * Helper method to get/set the key associated with input "index"(a.k.a
 * array offset)
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(int index) const -> KeyType {
  // replace with your own code
  return array_[index].first;
}

/*
 * Helper method to get the value associated with input "index"(a.k.a array
 * offset)
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(int index) const -> ValueType { return array_[index].second; }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::InsertAt(const KeyType &key, const ValueType &value, int index) -> bool {
  if (GetSize() >= GetMaxSize() || index < 0 || index > GetSize()) {
    return false;
  }
  for (int i = GetSize(); i > index; --i) {
    array_[i] = array_[i - 1];
  }
  array_[index].first = key;
  array_[index].second = value;
  IncreaseSize(1);
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyAt(const KeyType &key, int index) { array_[index].first = key; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetValueAt(const ValueType &value, int index) { array_[index].second = value; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::RemoveAt(int index) {
  for (int i = index; i < GetSize(); ++i) {
    array_[i] = array_[i + 1];
  }
  IncreaseSize(-1);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SplitTo(BPlusTreeInternalPage<KeyType, ValueType, KeyComparator> *dst) {
  int end = GetSize();
  int start = end >> 1;
  int dst_begin = 0;
  for (int i = start; i < end; ++i) {
    dst->array_[dst_begin++] = array_[i];
  }
  SetSize(start);
  dst->SetSize(dst_begin);
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::BorrowFromLeft(
    BPlusTreeInternalPage<KeyType, ValueType, KeyComparator> *brother_page_left, int borrow_cnt) -> bool {
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
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::BorrowFromRight(
    BPlusTreeInternalPage<KeyType, ValueType, KeyComparator> *brother_page_right, int borrow_cnt) -> bool {
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
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MergeToLeft(
    BPlusTreeInternalPage<KeyType, ValueType, KeyComparator> *brother_page_left) {
  int merge_index = brother_page_left->GetSize();
  for (int i = 0; i < GetSize(); ++i) {
    brother_page_left->array_[merge_index++] = array_[i];
  }
  brother_page_left->SetSize(merge_index);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MergeToRight(
    BPlusTreeInternalPage<KeyType, ValueType, KeyComparator> *brother_page_right) {
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

// valuetype for internalNode should be page id_t
template class BPlusTreeInternalPage<GenericKey<4>, page_id_t, GenericComparator<4>>;
template class BPlusTreeInternalPage<GenericKey<8>, page_id_t, GenericComparator<8>>;
template class BPlusTreeInternalPage<GenericKey<16>, page_id_t, GenericComparator<16>>;
template class BPlusTreeInternalPage<GenericKey<32>, page_id_t, GenericComparator<32>>;
template class BPlusTreeInternalPage<GenericKey<64>, page_id_t, GenericComparator<64>>;
}  // namespace bustub
