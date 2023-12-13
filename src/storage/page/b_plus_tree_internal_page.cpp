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
#include "storage/page/b_plus_tree_page.h"

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
  SetSize(0);
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
  for (int i = index; i < GetSize() - 1; ++i) {
    array_[i] = array_[i + 1];
  }
  IncreaseSize(-1);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::InsertAtAndSplitTo(const KeyType &key, const ValueType &value, int index,
                                                        BPlusTreeInternalPage<KeyType, ValueType, KeyComparator> *dst) {
  auto buffer = new MappingType[GetSize() + 1];
  for (int i = 0; i < index; ++i) {
    buffer[i] = array_[i];
  }
  buffer[index] = MappingType(key, value);
  for (int i = index; i < GetSize(); ++i) {
    buffer[i + 1] = array_[i];
  }
  int end = GetSize() + 1;
  int start = (end + 1) >> 1;
  for (int i = 0; i < start; ++i) {
    array_[i] = buffer[i];
  }
  for (int i = start; i < end; ++i) {
    dst->array_[i - start] = buffer[i];
  }
  SetSize(start);
  dst->SetSize(end - start);
  delete[] buffer;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::BorrowFromLeft(B_PLUS_TREE_INTERNAL_PAGE_TYPE *left_internal_page) -> bool {
  if (left_internal_page->GetSize() <= GetMinSize()) {
    return false;
  }
  for (int i = GetSize(); i > 0; --i) {
    array_[i] = array_[i - 1];
  }
  array_[0] = left_internal_page->array_[left_internal_page->GetSize() - 1];
  SetSize(GetSize() + 1);
  left_internal_page->SetSize(left_internal_page->GetSize() - 1);
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::BorrowFromRight(B_PLUS_TREE_INTERNAL_PAGE_TYPE *right_internal_page) -> bool {
  if (right_internal_page->GetSize() <= GetMinSize()) {
    return false;
  }
  array_[GetSize()] = right_internal_page->array_[0];
  for (int i = 0; i < right_internal_page->GetSize() - 1; ++i) {
    right_internal_page->array_[i] = right_internal_page->array_[i + 1];
  }
  SetSize(GetSize() + 1);
  right_internal_page->SetSize(right_internal_page->GetSize() - 1);
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MergeToLeft(
    BPlusTreeInternalPage<KeyType, ValueType, KeyComparator> *left_internal_page) {
  int merge_index = left_internal_page->GetSize();
  for (int i = 0; i < GetSize(); ++i) {
    left_internal_page->array_[merge_index++] = array_[i];
  }
  left_internal_page->SetSize(merge_index);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MergeToRight(
    BPlusTreeInternalPage<KeyType, ValueType, KeyComparator> *right_internal_page) {
  int merge_index = right_internal_page->GetSize() + GetSize();
  int j = merge_index;
  for (int i = right_internal_page->GetSize() - 1; i >= 0; --i) {
    right_internal_page->array_[--j] = right_internal_page->array_[i];
  }
  for (int i = GetSize(); i >= 0; i--) {
    right_internal_page->array_[--j] = array_[i];
  }
  right_internal_page->SetSize(merge_index);
}

// valuetype for internalNode should be page id_t
template class BPlusTreeInternalPage<GenericKey<4>, page_id_t, GenericComparator<4>>;
template class BPlusTreeInternalPage<GenericKey<8>, page_id_t, GenericComparator<8>>;
template class BPlusTreeInternalPage<GenericKey<16>, page_id_t, GenericComparator<16>>;
template class BPlusTreeInternalPage<GenericKey<32>, page_id_t, GenericComparator<32>>;
template class BPlusTreeInternalPage<GenericKey<64>, page_id_t, GenericComparator<64>>;
}  // namespace bustub
